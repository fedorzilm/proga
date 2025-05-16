// Предполагаемый путь: src/server/server.cpp
#include "server.h"
#include "logger.h"
#include "server_command_handler.h" // Подключаем обработчик
#include <iostream> // Для отладочного вывода, если Logger не используется повсеместно
#include <algorithm> // Для std::remove_if в деструкторе/stop

// Глобальный флаг g_server_should_stop объявляется в server_main.cpp
// extern std::atomic<bool> g_server_should_stop; // Не нужно здесь, если он в server.h

Server::Server(int port, Database& db, TariffPlan& plan, QueryParser& parser, const std::string& server_exec_path)
    : port_(port), db_(db), tariff_plan_(plan), query_parser_(parser), // query_parser_ может быть не нужен здесь напрямую, если CommandHandler создает свой
      server_executable_path_(server_exec_path), running_(false) {
    Logger::info("Server: Объект сервера создан. Порт: " + std::to_string(port_) +
                 ". Базовый путь для файлов: " + server_executable_path_);
}

Server::~Server() {
    Logger::info("Server: Деструктор сервера. Попытка остановить сервер, если он еще работает.");
    if (running_.load() || (acceptor_thread_.joinable() || !client_threads_.empty())) {
        stop(); // Вызовет join для потоков
    }
    Logger::info("Server: Объект сервера уничтожен.");
}

bool Server::start() {
    if (running_.load()) {
        Logger::warn("Server::start: Сервер уже запущен.");
        return true;
    }

    Logger::info("Server::start: Попытка запуска сервера на порту " + std::to_string(port_));
    if (!listen_socket_.bindSocket(port_)) {
        Logger::error("Server::start: Не удалось привязать слушающий сокет к порту " + std::to_string(port_));
        return false;
    }
    if (!listen_socket_.listenSocket()) { // backlog по умолчанию используется из TCPSocket::listenSocket
        Logger::error("Server::start: Не удалось перевести слушающий сокет в режим прослушивания.");
        listen_socket_.closeSocket();
        return false;
    }

    running_.store(true);
    g_server_should_stop.store(false); // Сбрасываем глобальный флаг при старте

    // Запускаем поток для acceptConnections
    try {
        acceptor_thread_ = std::thread(&Server::acceptorThreadLoop, this);
    } catch (const std::system_error& e) {
        Logger::error("Server::start: Не удалось запустить поток acceptorThreadLoop: " + std::string(e.what()));
        running_.store(false);
        listen_socket_.closeSocket();
        return false;
    }

    Logger::info("Server::start: Сервер успешно запущен и прослушивает порт " + std::to_string(port_));
    return true;
}

void Server::stop() {
    bool expected_running = true;
    // Атомарно проверяем и устанавливаем running_ в false, чтобы stop() вызывался только один раз
    if (!running_.compare_exchange_strong(expected_running, false)) {
        Logger::info("Server::stop: Сервер не был запущен или уже находится в процессе остановки.");
        // Если acceptor_thread_ все еще joinable, но running_ уже false, значит stop() был вызван из другого места
        if (acceptor_thread_.joinable()) {
             Logger::debug("Server::stop: acceptor_thread_ еще joinable, ожидание...");
             acceptor_thread_.join();
        }
        // Очистка клиентских потоков, если они не были присоединены
        {
            std::lock_guard<std::mutex> lock(client_threads_mutex_);
            for (std::thread& t : client_threads_) {
                if (t.joinable()) t.join();
            }
            client_threads_.clear();
        }
        return;
    }

    Logger::info("Server::stop: Инициирована остановка сервера...");
    g_server_should_stop.store(true); // Устанавливаем глобальный флаг для внешнего цикла в main

    // 1. Закрываем слушающий сокет, чтобы прервать accept() в acceptorThreadLoop
    Logger::debug("Server::stop: Закрытие слушающего сокета...");
    listen_socket_.closeSocket(); // Это должно разблокировать accept()

    // 2. Ожидаем завершения потока acceptorThreadLoop
    if (acceptor_thread_.joinable()) {
        Logger::debug("Server::stop: Ожидание завершения acceptor_thread_...");
        try {
            acceptor_thread_.join();
            Logger::info("Server::stop: Поток acceptor_thread_ успешно завершен.");
        } catch (const std::system_error& e) {
            Logger::error("Server::stop: Ошибка при ожидании acceptor_thread_: " + std::string(e.what()));
        }
    } else {
        Logger::debug("Server::stop: acceptor_thread_ не был joinable (возможно, не был запущен или уже завершен).");
    }


    // 3. Сигнализируем и ожидаем завершения всех активных клиентских потоков
    // Закрытие клиентских сокетов из этого потока может быть рискованным,
    // лучше, чтобы clientHandlerThread сами завершались при running_ == false или ошибке сокета.
    // Здесь мы просто дождемся их. Таймауты в recv помогут им не зависнуть навечно.
    Logger::info("Server::stop: Ожидание завершения всех клиентских потоков...");
    {
        std::lock_guard<std::mutex> lock(client_threads_mutex_); // Защищаем доступ к client_threads_
        // Сначала пройдемся и попробуем join те, которые уже завершились или могут быстро завершиться
        client_threads_.erase(std::remove_if(client_threads_.begin(), client_threads_.end(),
            [](std::thread& t){
                if (t.joinable()) {
                    // Попытка join с очень коротким таймаутом или просто проверка joinable
                    // Для простоты, здесь просто join. В реальном приложении можно добавить таймаут.
                    // t.join(); // Это может заблокировать stop() надолго, если поток клиента завис
                    return false; // Пока не удаляем, будем join-ить ниже
                }
                return true; // Удаляем, если не joinable
            }), client_threads_.end());

        // Теперь join-им оставшиеся
        for (size_t i = 0; i < client_threads_.size(); ++i) {
            if (client_threads_[i].joinable()) {
                Logger::debug("Server::stop: Ожидание клиентского потока #" + std::to_string(i));
                 try {
                    client_threads_[i].join(); // Это может заблокировать
                } catch (const std::system_error& e) {
                     Logger::error("Server::stop: Ошибка при join клиентского потока #" + std::to_string(i) + ": " + e.what());
                }
            }
        }
        client_threads_.clear();
    }
    Logger::info("Server::stop: Все клиентские потоки обработаны. Сервер полностью остановлен.");
}


void Server::acceptorThreadLoop() {
    Logger::info("Server: Поток принятия соединений (acceptorThreadLoop) запущен. ID: " + Logger::get_thread_id_str());
    while (running_.load() && !g_server_should_stop.load()) {
        std::string client_ip_str;
        int client_port_num = 0;
        TCPSocket client_socket = listen_socket_.acceptSocket(&client_ip_str, &client_port_num);

        if (!running_.load() || g_server_should_stop.load()) { // Повторная проверка после блокирующего accept
            if (client_socket.isValid()) {
                Logger::info("Server: acceptorThreadLoop прерван, закрытие только что принятого сокета.");
                client_socket.closeSocket();
            }
            break;
        }

        if (client_socket.isValid()) {
            Logger::info("Server: Принято новое соединение от " + (client_ip_str.empty() ? "unknown_ip" : client_ip_str) + ":" + std::to_string(client_port_num) +
                         ". FD: " + std::to_string(client_socket.getRawSocketDescriptor()) + ". Запуск потока обработчика...");
            try {
                std::lock_guard<std::mutex> lock(client_threads_mutex_);
                // Очистка завершенных потоков перед добавлением нового
                client_threads_.erase(
                    std::remove_if(client_threads_.begin(), client_threads_.end(),
                                   [](const std::thread& t) { return !t.joinable(); }),
                    client_threads_.end());

                client_threads_.emplace_back(&Server::clientHandlerThread, this, std::move(client_socket));
                // Отсоединение потока (detach) - простой способ, но тогда нужно убедиться, что Server живет дольше
                // или потоки корректно завершаются. Для join в stop() - не отсоединяем.
                // client_threads_.back().detach();
            } catch (const std::system_error& e) {
                 Logger::error("Server: Не удалось создать поток для клиента " + client_ip_str + ":" + std::to_string(client_port_num) + ". Ошибка: " + e.what());
                 client_socket.closeSocket(); // Закрываем сокет, т.к. обработчик не запустился
            }
        } else {
            // Ошибка accept, если слушающий сокет еще валиден и сервер не останавливается
            if (listen_socket_.isValid() && running_.load() && !g_server_should_stop.load()) {
                Logger::warn("Server: acceptSocket() вернул невалидный сокет, когда сервер активен.");
                std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Небольшая пауза
            }
        }
    }
    Logger::info("Server: Поток принятия соединений (acceptorThreadLoop) завершен. ID: " + Logger::get_thread_id_str());
}

void Server::clientHandlerThread(TCPSocket client_socket_param) { // Принимаем по значению для перемещения
    TCPSocket client_socket = std::move(client_socket_param); // Перемещаем в локальную переменную
    
    std::string client_id_for_log = "Клиент[fd:" + std::to_string(client_socket.getRawSocketDescriptor()) + 
                                   ",th:" + Logger::get_thread_id_str() + "]";
    Logger::info(client_id_for_log + ": Поток обработки клиента запущен.");

    // QueryParser может быть создан здесь, если он stateless, или передан, если общий
    QueryParser parser_for_session;
    ServerCommandHandler command_handler(db_, tariff_plan_, server_executable_path_);

    const int client_receive_timeout_ms = 600000; // Таймаут для ожидания данных от клиента (10 минут)

    while (running_.load() && !g_server_should_stop.load() && client_socket.isValid()) {
        bool receive_success = false;
        std::string query_str = client_socket.receiveAllDataWithLengthPrefix(receive_success, client_receive_timeout_ms);

        if (!g_server_should_stop.load() && !running_.load()){ // Проверка после блокирующего вызова
             Logger::info(client_id_for_log + ": Сервер останавливается, завершение потока клиента.");
             break;
        }

        if (!receive_success) {
            if (running_.load() && !g_server_should_stop.load()) { // Если ошибка не из-за штатной остановки сервера
                // Это может быть таймаут, разрыв соединения клиентом, или ошибка сокета
                Logger::warn(client_id_for_log + ": Ошибка получения данных, разрыв соединения или таймаут.");
            } else {
                 Logger::info(client_id_for_log + ": Получение данных прервано из-за остановки сервера.");
            }
            break;
        }

        if (query_str.empty() && receive_success) { // Клиент мог отправить сообщение с нулевой длиной (валидно)
            Logger::debug(client_id_for_log + ": Получен пустой запрос (длина 0). Пропускаем.");
            continue;
        }
        
        Logger::info(client_id_for_log + ": Получен запрос: \"" + query_str + "\"");

        if (query_str == "EXIT_CLIENT_SESSION") { // Специальная команда для корректного выхода клиента
            Logger::info(client_id_for_log + ": Клиент запросил завершение сессии командой EXIT_CLIENT_SESSION.");
            // Отправить подтверждение не обязательно, т.к. сессия просто закроется
            // Но можно и отправить, если протокол это предполагает.
            // client_socket.sendAllDataWithLengthPrefix("Сессия завершена по вашему запросу.\n");
            break;
        }

        std::string response_str;
        try {
            Query query = parser_for_session.parseQuery(query_str);
            query.originalQueryString = query_str; // Сохраняем для логов в CommandHandler
            Logger::debug(client_id_for_log + ": Запрос успешно разобран, тип: " + std::to_string(static_cast<int>(query.type)));

            bool is_write_operation = (query.type == QueryType::ADD ||
                                     query.type == QueryType::DELETE ||
                                     query.type == QueryType::EDIT ||
                                     query.type == QueryType::LOAD ||
                                     query.type == QueryType::SAVE); // SAVE тоже блокируем, т.к. меняет currentFilename_ и пишет на диск

            // Логика блокировки мьютекса
            if (is_write_operation) {
                std::lock_guard<std::mutex> lock(db_mutex_);
                Logger::debug(client_id_for_log + ": db_mutex_ ЗАХВАЧЕН для операции записи (тип: " + std::to_string(static_cast<int>(query.type)) + ").");
                response_str = command_handler.processCommand(query);
                Logger::debug(client_id_for_log + ": db_mutex_ ОСВОБОЖДЕН после операции записи.");
            } else { // Операции чтения
                // Для большей параллельности можно использовать std::shared_lock<std::shared_mutex> если db_mutex_ это std::shared_mutex
                std::lock_guard<std::mutex> lock(db_mutex_); // Пока используем эксклюзивный лок для простоты и надежности
                Logger::debug(client_id_for_log + ": db_mutex_ ЗАХВАЧЕН для операции чтения (тип: " + std::to_string(static_cast<int>(query.type)) + ").");
                response_str = command_handler.processCommand(query);
                Logger::debug(client_id_for_log + ": db_mutex_ ОСВОБОЖДЕН после операции чтения.");
            }
             // Если команда была EXIT (клиентская), сервер подтвердил, и мы должны здесь разорвать цикл для этого клиента.
            if (query.type == QueryType::EXIT) {
                Logger::info(client_id_for_log + ": Обработана команда EXIT от клиента. Завершение сессии.");
                // Отправляем ответ и выходим из цикла
                if (!client_socket.sendAllDataWithLengthPrefix(response_str)) {
                    Logger::error(client_id_for_log + ": Не удалось отправить финальный ответ на EXIT.");
                } else {
                    Logger::debug(client_id_for_log + ": Финальный ответ на EXIT отправлен.");
                }
                break; 
            }


        } catch (const std::runtime_error& e_parse) { // Ошибки парсинга от QueryParser
            response_str = "Ошибка [Сервер]: Ошибка разбора вашего запроса: " + std::string(e_parse.what()) + "\n";
            Logger::error(client_id_for_log + ": Ошибка разбора запроса: " + std::string(e_parse.what()) + " (Оригинальный запрос: '" + query_str + "')");
        } catch (const std::exception& e_handler) { // Другие ошибки от ServerCommandHandler
             response_str = "Ошибка [Сервер]: Внутренняя ошибка при обработке вашего запроса: " + std::string(e_handler.what()) + "\n";
             Logger::error(client_id_for_log + ": Исключение при обработке команды: " + std::string(e_handler.what()) + " (Оригинальный запрос: '" + query_str + "')");
        }


        if (!client_socket.sendAllDataWithLengthPrefix(response_str)) {
            Logger::error(client_id_for_log + ": Не удалось отправить ответ клиенту.");
            break; // Прерываем цикл при ошибке отправки
        }
        Logger::debug(client_id_for_log + ": Ответ успешно отправлен клиенту.");
    }

    client_socket.closeSocket(); // Убедимся, что сокет закрыт при выходе из потока
    Logger::info(client_id_for_log + ": Поток обработки клиента завершен.");
}
