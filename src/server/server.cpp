/*!
 * \file server.cpp
 * \author Fedor Zilnitskiy
 * \brief Реализация класса Server для управления TCP-сервером базы данных.
 */
#include "server.h"
#include "logger.h"
#include "server_command_handler.h" 
#include "file_utils.h" // Для getProjectRootPath (если потребуется, но сейчас не используется напрямую здесь)
#include <iostream>     // Для отладочного вывода (минимизировать использование)
#include <algorithm>    // Для std::remove_if (не используется после перехода на ThreadPool)
#include <filesystem>   // Для работы с путями (например, при определении server_base_path_for_commands_)

// Глобальный флаг g_server_should_stop объявляется и определяется в server_main.cpp
extern std::atomic<bool> g_server_should_stop;

/*!
 * \brief Конструктор сервера.
 * \param config Конфигурация сервера.
 * \param db Ссылка на базу данных.
 * \param plan Ссылка на тарифный план.
 * \param parser Ссылка на парсер запросов.
 * \param server_executable_path Полный путь к исполняемому файлу сервера.
 */
Server::Server(const ServerConfig& config, 
               Database& db, 
               TariffPlan& plan, 
               QueryParser& parser,
               const std::string& server_executable_path) // server_executable_path нужен для определения server_base_path_for_commands_
    : config_(config), 
      db_(db), 
      tariff_plan_(plan), 
      query_parser_(parser),
      running_(false) {

    // Определение server_base_path_for_commands_
    // Приоритет:
    // 1. Значение из config.server_data_root_dir, если оно абсолютное.
    // 2. Значение из config.server_data_root_dir, если оно относительное, разрешенное относительно server_executable_path (или его директории).
    // 3. Если config.server_data_root_dir пусто, используется директория исполняемого файла сервера как база.
    // ServerCommandHandler затем добавит DEFAULT_SERVER_DATA_SUBDIR к этому пути.

    if (!config_.server_data_root_dir.empty()) {
        std::filesystem::path data_root_path_obj(config_.server_data_root_dir);
        if (data_root_path_obj.is_absolute()) {
            server_base_path_for_commands_ = data_root_path_obj.string();
            Logger::info("Server Ctor: Используется абсолютная директория данных из конфига: '" + server_base_path_for_commands_ + "'");
        } else {
            // Если относительный путь, считаем его относительно директории исполняемого файла сервера
            if (!server_executable_path.empty()) {
                std::filesystem::path exec_dir = std::filesystem::path(server_executable_path).parent_path();
                server_base_path_for_commands_ = (exec_dir / data_root_path_obj).lexically_normal().string();
                 Logger::info("Server Ctor: Используется относительная директория данных из конфига (относительно исполняемого файла): '" + config_.server_data_root_dir + "', разрешена в: '" + server_base_path_for_commands_ + "'");
            } else {
                // Редкий случай, если server_executable_path пуст
                server_base_path_for_commands_ = std::filesystem::weakly_canonical(std::filesystem::absolute(data_root_path_obj)).string();
                Logger::warn("Server Ctor: Путь к исполняемому файлу не предоставлен, относительный server_data_root_dir ('" + config_.server_data_root_dir + "') разрешен относительно CWD: '" + server_base_path_for_commands_ + "'");
            }
        }
    } else {
        // Если server_data_root_dir не указан в конфиге, используем директорию исполняемого файла как базу
        if (!server_executable_path.empty()) {
            server_base_path_for_commands_ = std::filesystem::path(server_executable_path).parent_path().string();
            Logger::info("Server Ctor: server_data_root_dir не указан. Базовый путь для команд определен как директория исполняемого файла: '" + server_base_path_for_commands_ + "'");
        } else {
            server_base_path_for_commands_ = std::filesystem::current_path().string(); // Крайний случай
            Logger::warn("Server Ctor: server_data_root_dir и путь к исполняемому файлу не указаны. Базовый путь для команд установлен в CWD: '" + server_base_path_for_commands_ + "'");
        }
    }

    try {
        thread_pool_ = std::make_unique<ThreadPool>(config_.thread_pool_size);
        Logger::info("Server Ctor: ThreadPool успешно создан с " + std::to_string(config_.thread_pool_size) + " потоками.");
    } catch (const std::exception& e) {
        Logger::error("Server Ctor: КРИТИЧЕСКАЯ ОШИБКА при создании ThreadPool: " + std::string(e.what()));
        // thread_pool_ останется nullptr, start() должен это проверить и вернуть false.
        // Выбрасывать исключение из конструктора здесь приведет к завершению программы, если main не ловит.
        // Лучше позволить start() вернуть false.
        throw; // Перебрасываем, чтобы main мог обработать эту критическую ошибку инициализации.
    }

    Logger::info("Server Ctor: Объект сервера сконструирован. Порт: " + std::to_string(config_.port) +
                 ". Базовый путь для команд ServerCommandHandler: '" + server_base_path_for_commands_ + "'");
}

/*!
 * \brief Деструктор сервера.
 */
Server::~Server() {
    Logger::info("Server Dtor: Деструктор сервера вызван. Попытка остановить сервер, если он еще работает.");
    // `running_` может быть true, если stop() не был вызван или если сервер упал до вызова stop().
    // `acceptor_thread_.joinable()` проверяет, был ли поток запущен и еще не присоединен.
    if (running_.load() || (acceptor_thread_.joinable())) { 
        stop(); 
    }
    // `thread_pool_` (unique_ptr) автоматически вызовет деструктор ThreadPool,
    // который, в свою очередь, вызовет свой `stop()` и присоединит рабочие потоки.
    Logger::info("Server Dtor: Объект сервера уничтожен.");
}

/*!
 * \brief Запускает сервер.
 * \return true при успешном запуске, false при ошибке.
 */
bool Server::start() {
    if (running_.load()) {
        Logger::warn("Server Start: Сервер уже запущен.");
        return true;
    }
    if (!thread_pool_) { 
        Logger::error("Server Start: ThreadPool не был инициализирован (ошибка в конструкторе). Сервер не может быть запущен.");
        return false;
    }
    if (!thread_pool_->isRunning()) { // Дополнительная проверка, если пул был создан, но сразу остановлен из-за ошибки
        Logger::error("Server Start: ThreadPool не активен. Сервер не может быть запущен.");
        return false;
    }

    Logger::info("Server Start: Попытка запуска сервера на порту " + std::to_string(config_.port));
    if (!listen_socket_.bindSocket(config_.port)) {
        // bindSocket уже логирует ошибку
        return false;
    }
    if (!listen_socket_.listenSocket(config_.thread_pool_size * 2)) { // Backlog можно связать с размером пула
        // listenSocket уже логирует ошибку
        listen_socket_.closeSocket(); // Закрываем сокет, если listen не удался
        return false;
    }

    running_.store(true); // Устанавливаем флаг, что сервер активен
    g_server_should_stop.store(false); // Сбрасываем глобальный флаг остановки при старте

    // Запускаем поток для приема соединений
    try {
        acceptor_thread_ = std::thread(&Server::acceptorThreadLoop, this);
    } catch (const std::system_error& e) {
        Logger::error("Server Start: Не удалось запустить поток acceptorThreadLoop: " + std::string(e.what()));
        running_.store(false); // Сбрасываем флаг, так как сервер не запустился корректно
        listen_socket_.closeSocket();
        if (thread_pool_ && thread_pool_->isRunning()) { // Останавливаем пул, если он был запущен
            thread_pool_->stop();
        }
        return false;
    }

    Logger::info("Server Start: Сервер успешно запущен и прослушивает порт " + std::to_string(config_.port));
    return true;
}

/*!
 * \brief Останавливает сервер.
 */
void Server::stop() {
    bool expected_running = true;
    // compare_exchange_strong атомарно проверяет и устанавливает running_ в false.
    // Это гарантирует, что основная логика stop() выполнится только один раз.
    if (!running_.compare_exchange_strong(expected_running, false)) {
        Logger::info("Server Stop: Сервер не был запущен или уже находится в процессе остановки.");
        // Если acceptor_thread_ все еще joinable, это может означать, что stop() был вызван из другого потока,
        // или произошла ошибка при старте после установки running_ = true, но до полного запуска acceptor_thread_.
        // В любом случае, если он joinable, нужно его присоединить.
        if (acceptor_thread_.joinable()) {
             Logger::debug("Server Stop (re-entry/post-fail): acceptor_thread_ еще joinable, ожидание...");
             try { acceptor_thread_.join(); } catch (const std::system_error& e) { Logger::error("Server Stop: Exception on acceptor_thread.join (re-entry): " + std::string(e.what())); }
        }
        // ThreadPool::stop() также идемпотентен, поэтому его можно вызвать снова, если он еще работает.
        if (thread_pool_ && thread_pool_->isRunning()) {
            Logger::info("Server Stop (re-entry/post-fail): Остановка ThreadPool...");
            thread_pool_->stop();
        }
        return;
    }

    Logger::info("Server Stop: Инициирована процедура остановки сервера...");
    g_server_should_stop.store(true); // Устанавливаем глобальный флаг для внешних циклов (например, в main)

    // 1. Закрываем слушающий сокет, чтобы прервать accept() в acceptorThreadLoop
    Logger::debug("Server Stop: Закрытие слушающего сокета (fd: " + std::to_string(listen_socket_.getRawSocketDescriptor()) + ")...");
    listen_socket_.closeSocket(); // Это должно разблокировать accept() в acceptorThreadLoop

    // 2. Ожидаем завершения потока acceptorThreadLoop
    if (acceptor_thread_.joinable()) {
        Logger::debug("Server Stop: Ожидание завершения acceptor_thread_...");
        try {
            acceptor_thread_.join();
            Logger::info("Server Stop: Поток acceptor_thread_ успешно завершен.");
        } catch (const std::system_error& e) {
            Logger::error("Server Stop: Ошибка при ожидании завершения acceptor_thread_: " + std::string(e.what()));
            // Поток может быть уже завершен или возникла ошибка при join
        }
    } else {
        Logger::debug("Server Stop: acceptor_thread_ не был joinable (возможно, не был запущен или уже завершен).");
    }

    // 3. Останавливаем пул потоков. ThreadPool::stop() ожидает завершения всех задач и рабочих потоков.
    if (thread_pool_) {
        Logger::info("Server Stop: Остановка ThreadPool...");
        thread_pool_->stop(); 
        Logger::info("Server Stop: ThreadPool успешно остановлен.");
    } else {
        Logger::warn("Server Stop: ThreadPool не был инициализирован, пропускаем остановку пула.");
    }
    
    Logger::info("Server Stop: Сервер полностью остановлен.");
}

/*!
 * \brief Цикл потока, принимающего клиентские соединения.
 */
void Server::acceptorThreadLoop() {
    Logger::info("Server AcceptorLoop: Поток принятия соединений запущен. ID: " + Logger::get_thread_id_str());
    while (running_.load() && !g_server_should_stop.load()) { // Проверяем и глобальный флаг
        std::string client_ip_str;
        int client_port_num = 0;
        
        // acceptSocket будет блокирующим, пока не появится соединение или слушающий сокет не будет закрыт.
        TCPSocket client_socket = listen_socket_.acceptSocket(&client_ip_str, &client_port_num);

        // Повторная проверка флагов после блокирующего accept
        if (!running_.load() || g_server_should_stop.load()) { 
            if (client_socket.isValid()) { // Если успели принять соединение перед самой остановкой
                Logger::info("Server AcceptorLoop: Цикл прерван во время/после accept. Закрытие только что принятого клиентского сокета (fd: " + std::to_string(client_socket.getRawSocketDescriptor()) + ").");
                client_socket.closeSocket();
            }
            break; // Выход из цикла acceptorThreadLoop
        }

        if (client_socket.isValid()) {
            Logger::info("Server AcceptorLoop: Принято новое соединение от " + (client_ip_str.empty() ? "unknown_ip" : client_ip_str) + ":" + std::to_string(client_port_num) +
                         ". FD: " + std::to_string(client_socket.getRawSocketDescriptor()) + ". Добавление задачи в ThreadPool...");
            
            if (thread_pool_ && thread_pool_->isRunning()) {
                // Захватываем client_socket по значению (через std::move) для передачи в лямбду.
                // `mutable` для лямбды, чтобы можно было вызвать std::move для client_socket внутри.
                bool enqueued = thread_pool_->enqueue([this, sock = std::move(client_socket)]() mutable {
                    clientHandlerTask(std::move(sock)); 
                });

                if (!enqueued) {
                    Logger::error("Server AcceptorLoop: Не удалось добавить задачу обработки клиента в ThreadPool (возможно, пул остановлен или переполнен). Клиент не будет обслужен.");
                    // client_socket здесь уже невалиден из-за std::move(client_socket) в захвате лямбды.
                    // Если enqueue вернул false, то сокет, переданный в лямбду, не будет обработан.
                    // Его деструктор будет вызван, когда лямбда (если она не была сохранена и вызвана) будет разрушена.
                    // Если лямбда была сохранена, но не вызвана, а потом разрушена, сокет закроется.
                    // Для большей надежности, если enqueue() возвращает false, это означает, что задача не была принята,
                    // и сокет, который был перемещен в лямбду, должен быть закрыт, если лямбда не будет вызвана.
                    // Однако, здесь мы не можем напрямую закрыть 'sock'.
                    // Пул потоков должен корректно обрабатывать задачи или их отсутствие.
                    // Если enqueue == false, значит клиент не будет обслужен. Сокет закроется при выходе из области видимости, если не был перемещен.
                    // В данном случае, так как `sock` захвачен по значению и перемещен, если лямбда не выполнится,
                    // `sock` будет уничтожен, и его деструктор закроет сокет.
                }
            } else {
                 Logger::error("Server AcceptorLoop: ThreadPool не инициализирован или не активен! Невозможно обработать клиента (fd: " + std::to_string(client_socket.getRawSocketDescriptor()) + "). Закрытие сокета.");
                 client_socket.closeSocket(); 
            }
        } else { // acceptSocket вернул невалидный сокет
            // Это может произойти, если listen_socket_ был закрыт (например, в Server::stop()),
            // или при других ошибках accept.
            if (listen_socket_.isValid() && running_.load() && !g_server_should_stop.load()) {
                // Если слушающий сокет все еще валиден и сервер должен работать, но accept вернул ошибку.
                // Logger::warn("Server AcceptorLoop: acceptSocket() вернул невалидный сокет, когда сервер активен. Повторная попытка...");
                 std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Небольшая пауза, чтобы не загружать ЦП
            } else if (!listen_socket_.isValid()) {
                Logger::info("Server AcceptorLoop: Слушающий сокет стал невалиден. Завершение цикла принятия соединений.");
                break; // Выход, если слушающий сокет закрыт
            }
        }
    }
    Logger::info("Server AcceptorLoop: Поток принятия соединений завершен. ID: " + Logger::get_thread_id_str());
}

/*!
 * \brief Обработка одного клиента в отдельной задаче (выполняется в пуле потоков).
 * \param client_socket_param Сокет клиента (передается по значению для перемещения).
 */
void Server::clientHandlerTask(TCPSocket client_socket_param) { 
    // Перемещаем сокет во владение этой функции/задачи
    TCPSocket client_socket = std::move(client_socket_param); 
    
    const std::string client_id_for_log = "Клиент[fd:" + std::to_string(client_socket.getRawSocketDescriptor()) + 
                                          ",tp_th:" + Logger::get_thread_id_str() + "]";
    Logger::info(client_id_for_log + ": Задача обработки клиента запущена.");

    // QueryParser stateless, можно создавать локально или использовать общий экземпляр (передан в Server)
    // ServerCommandHandler создается для каждой сессии клиента, так как он может хранить состояние сессии (хотя в текущей реализации он stateless)
    ServerCommandHandler command_handler(db_, tariff_plan_, server_base_path_for_commands_);

    // Таймаут на ожидание данных от клиента (можно сделать настраиваемым через ServerConfig)
    const int client_receive_timeout_ms = DEFAULT_CLIENT_RECEIVE_TIMEOUT_MS; 

    while (running_.load() && !g_server_should_stop.load() && client_socket.isValid()) {
        bool receive_success = false;
        std::string query_str = client_socket.receiveAllDataWithLengthPrefix(receive_success, client_receive_timeout_ms);

        // Проверка флагов остановки сервера *после* блокирующего вызова receive
        if (!running_.load() || g_server_should_stop.load()){ 
             Logger::info(client_id_for_log + ": Сервер останавливается, принудительное завершение задачи клиента.");
             break;
        }

        if (!receive_success) { // Ошибка получения данных или таймаут
            // receiveAllDataWithLengthPrefix уже логирует детали.
            // Если running_ и !g_server_should_stop, значит, это проблема клиента или сети.
            if (running_.load() && !g_server_should_stop.load() && client_socket.isValid()) { 
                Logger::warn(client_id_for_log + ": Ошибка получения данных от клиента (или таймаут). Завершение сессии.");
            } else if (!client_socket.isValid()) { // Сокет стал невалидным
                 Logger::info(client_id_for_log + ": Сокет клиента стал невалиден во время ожидания данных.");
            }
            else { // Остановка сервера
                 Logger::info(client_id_for_log + ": Получение данных прервано из-за остановки сервера.");
            }
            break; // Прерываем цикл обработки этого клиента
        }

        // Клиент может отправить сообщение с нулевой длиной (например, пустая команда или keep-alive)
        // В текущем протоколе это не ожидается как валидная команда, но receiveAllDataWithLengthPrefix вернет success=true и пустую строку.
        if (query_str.empty() && receive_success) { 
            Logger::debug(client_id_for_log + ": Получен пустой запрос (длина 0) от клиента. Игнорируем.");
            // Можно отправить ответ, если такое поведение нежелательно:
            // client_socket.sendAllDataWithLengthPrefix("Предупреждение [Сервер]: Получен пустой запрос. Пожалуйста, отправьте команду.\n");
            continue; // Ожидаем следующую команду
        }
        
        Logger::info(client_id_for_log + ": Получен запрос от клиента: \"" + query_str + "\"");

        // Специальная команда для корректного выхода клиента из пакетного режима или при закрытии окна
        if (query_str == "EXIT_CLIENT_SESSION") { 
            Logger::info(client_id_for_log + ": Клиент запросил немедленное завершение сессии командой EXIT_CLIENT_SESSION.");
            // Ответ не обязателен, так как клиент сам инициирует разрыв.
            break; // Завершаем обработку этого клиента
        }

        std::string response_str;
        try {
            Query query = query_parser_.parseQuery(query_str); // Используем общий query_parser_
            query.originalQueryString = query_str; 
            Logger::debug(client_id_for_log + ": Запрос успешно разобран, тип: " + std::to_string(static_cast<int>(query.type)));

            // Определение типа операции для правильной блокировки
            bool is_write_operation = (query.type == QueryType::ADD ||
                                     query.type == QueryType::DELETE ||
                                     query.type == QueryType::EDIT ||
                                     query.type == QueryType::LOAD ||
                                     query.type == QueryType::SAVE); 

            if (is_write_operation) {
                std::unique_lock<std::shared_mutex> lock(db_shared_mutex_); // Эксклюзивная блокировка для операций записи
                Logger::debug(client_id_for_log + ": db_shared_mutex_ ЗАХВАЧЕН (unique_lock) для операции записи (тип: " + std::to_string(static_cast<int>(query.type)) + ").");
                response_str = command_handler.processCommand(query);
                // Мьютекс освобождается автоматически при выходе lock из области видимости
                Logger::debug(client_id_for_log + ": db_shared_mutex_ ОСВОБОЖДЕН (unique_lock) после операции записи.");
            } else { // Операции чтения
                std::shared_lock<std::shared_mutex> lock(db_shared_mutex_); // Разделяемая блокировка для операций чтения
                Logger::debug(client_id_for_log + ": db_shared_mutex_ ЗАХВАЧЕН (shared_lock) для операции чтения (тип: " + std::to_string(static_cast<int>(query.type)) + ").");
                response_str = command_handler.processCommand(query);
                Logger::debug(client_id_for_log + ": db_shared_mutex_ ОСВОБОЖДЕН (shared_lock) после операции чтения.");
            }
            
            // Если команда была EXIT (отправленная клиентом как обычная команда),
            // сервер подтвердил ее выполнение, и мы должны здесь разорвать цикл для этого клиента.
            if (query.type == QueryType::EXIT) {
                Logger::info(client_id_for_log + ": Обработана команда EXIT от клиента. Отправка подтверждения и завершение сессии.");
                // Ответ уже сформирован в response_str ServerCommandHandler'ом.
                // Отправляем этот ответ перед разрывом.
                if (!client_socket.sendAllDataWithLengthPrefix(response_str)) {
                    Logger::error(client_id_for_log + ": Не удалось отправить финальный ответ на команду EXIT клиенту.");
                } else {
                    Logger::debug(client_id_for_log + ": Финальный ответ на команду EXIT успешно отправлен клиенту.");
                }
                break; // Завершаем сессию с этим клиентом
            }

        } catch (const std::runtime_error& e_parse) { // Ошибки от QueryParser::parseQuery
            response_str = "Ошибка [Сервер]: Ошибка разбора вашего запроса: " + std::string(e_parse.what()) + "\n";
            Logger::error(client_id_for_log + ": Ошибка разбора запроса от клиента: " + std::string(e_parse.what()) + " (Оригинальный запрос: '" + query_str + "')");
        } catch (const std::exception& e_handler) { // Другие ошибки от ServerCommandHandler или Database
             response_str = "Ошибка [Сервер]: Внутренняя ошибка при обработке вашего запроса: " + std::string(e_handler.what()) + "\n";
             Logger::error(client_id_for_log + ": Исключение std::exception при обработке команды: " + std::string(e_handler.what()) + " (Оригинальный запрос: '" + query_str + "')");
        } catch (...) { // Неизвестные исключения
             response_str = "Ошибка [Сервер]: Произошла неизвестная критическая ошибка при обработке вашего запроса.\n";
             Logger::error(client_id_for_log + ": Неизвестное исключение (...) при обработке команды (Оригинальный запрос: '" + query_str + "')");
        }

        // Отправка ответа клиенту
        if (!client_socket.sendAllDataWithLengthPrefix(response_str)) {
            Logger::error(client_id_for_log + ": Не удалось отправить ответ клиенту. Завершение сессии.");
            break; // Прерываем цикл при ошибке отправки
        }
        // Logger::debug(client_id_for_log + ": Ответ успешно отправлен клиенту: \"" + response_str + "\""); // Может быть слишком многословно
        Logger::debug(client_id_for_log + ": Ответ успешно отправлен клиенту (длина: " + std::to_string(response_str.length()) + ").");
    } // конец while (обработка запросов клиента)

    // Закрываем сокет клиента при выходе из функции/задачи (если он еще валиден)
    if (client_socket.isValid()) {
        client_socket.closeSocket();
    }
    Logger::info(client_id_for_log + ": Задача обработки клиента завершена, сокет закрыт.");
}
