/*!
 * \file server.cpp
 * \brief Реализация класса Server для управления TCP-сервером базы данных.
 */
#include "server.h"
#include "logger.h"
#include "server_command_handler.h" // Для ServerCommandHandler
#include "file_utils.h"             // Для FileUtils::getProjectRootPath (если используется для server_base_path_for_commands_)
#include "query_parser.h"           // Для QueryParser и Query

#include <iostream>      // Для std::cerr (редко)
#include <algorithm>     // Для std::remove_if (не используется напрямую здесь)
#include <filesystem>    // Для std::filesystem::path, weakly_canonical, absolute
#include <limits>        // Для std::numeric_limits (не используется напрямую здесь)
#include <memory>        // Для std::make_unique, std::make_shared

// Глобальный флаг g_server_should_stop должен быть определен
Server::Server(const ServerConfig& config,
               Database& db,
               TariffPlan& plan,
               QueryParser& parser,
               const std::string& server_executable_path)
    : config_(config),
      db_(db),
      tariff_plan_(plan),
      query_parser_(parser),
      running_(false) {

    // Определение server_base_path_for_commands_ (для ServerCommandHandler)
    if (!config_.server_data_root_dir.empty()) {
        std::filesystem::path data_root_path_obj(config_.server_data_root_dir);
        if (data_root_path_obj.is_absolute()) {
            server_base_path_for_commands_ = data_root_path_obj.string();
            Logger::info("Конструктор Server: Используется абсолютный server_data_root_dir из конфигурации: '" + server_base_path_for_commands_ + "'");
        } else { // Относительный путь
            if (!server_executable_path.empty()) {
                try {
                    std::filesystem::path exec_dir = std::filesystem::path(server_executable_path).parent_path();
                    // Используем lexically_normal для упрощения пути ".." и "."
                    server_base_path_for_commands_ = (exec_dir / data_root_path_obj).lexically_normal().string();
                    Logger::info("Конструктор Server: Относительный server_data_root_dir ('" + config_.server_data_root_dir + "') разрешен относительно пути исполняемого файла в: '" + server_base_path_for_commands_ + "'");
                } catch (const std::filesystem::filesystem_error& e_fs) {
                     Logger::warn(std::string("Конструктор Server: Ошибка разрешения относительного server_data_root_dir ('") + config_.server_data_root_dir + "') относительно пути исполняемого файла ('" + server_executable_path + "'): " + e_fs.what() + ". Попытка разрешения относительно CWD.");
                     try {
                        server_base_path_for_commands_ = std::filesystem::weakly_canonical(std::filesystem::absolute(data_root_path_obj)).string();
                        Logger::info("Конструктор Server: Относительный server_data_root_dir ('" + config_.server_data_root_dir + "') разрешен относительно CWD в: '" + server_base_path_for_commands_ + "'");
                     } catch (const std::filesystem::filesystem_error& e_fs_cwd) {
                         Logger::error(std::string("Конструктор Server: КРИТИЧЕСКАЯ ОШИБКА разрешения относительного server_data_root_dir ('") + config_.server_data_root_dir + "') относительно CWD: " + e_fs_cwd.what() + ". Базовый путь для команд будет пустым или непредсказуемым!");
                         server_base_path_for_commands_.clear();
                     }
                }
            } else { 
                Logger::warn("Конструктор Server: server_executable_path пуст. Разрешение относительного server_data_root_dir ('" + config_.server_data_root_dir + "') относительно CWD.");
                try {
                    server_base_path_for_commands_ = std::filesystem::weakly_canonical(std::filesystem::absolute(data_root_path_obj)).string();
                    Logger::info("Конструктор Server: Относительный server_data_root_dir ('" + config_.server_data_root_dir + "') разрешен относительно CWD в: '" + server_base_path_for_commands_ + "'");
                } catch (const std::filesystem::filesystem_error& e_fs_cwd) {
                     Logger::error(std::string("Конструктор Server: КРИТИЧЕСКАЯ ОШИБКА разрешения относительного server_data_root_dir ('") + config_.server_data_root_dir + "') относительно CWD: " + e_fs_cwd.what() + ". Базовый путь для команд будет пустым или непредсказуемым!");
                     server_base_path_for_commands_.clear();
                }
            }
        }
    } else { 
        Logger::info("Конструктор Server: server_data_root_dir не указан в конфигурации.");
        if (!server_executable_path.empty()) {
            try {
                // Используем директорию исполняемого файла как базовый путь по умолчанию
                server_base_path_for_commands_ = std::filesystem::path(server_executable_path).parent_path().string();
                Logger::info("Конструктор Server: server_base_path_for_commands по умолчанию устанавливается в директорию исполняемого файла: '" + server_base_path_for_commands_ + "'");
            } catch (const std::exception& e_path) { 
                 Logger::warn(std::string("Конструктор Server: Ошибка получения родительской директории исполняемого файла: ") + e_path.what() + ". server_base_path_for_commands по умолчанию устанавливается в CWD.");
                 try {
                    server_base_path_for_commands_ = std::filesystem::current_path().string();
                 } catch (const std::filesystem::filesystem_error& e_fs_cwd) {
                    Logger::error(std::string("Конструктор Server: КРИТИЧЕСКАЯ ОШИБКА получения CWD: ") + e_fs_cwd.what() + ". Базовый путь для команд будет пустым!");
                    server_base_path_for_commands_.clear();
                 }
            }
        } else { 
             Logger::warn("Конструктор Server: server_executable_path пуст. server_base_path_for_commands по умолчанию устанавливается в CWD.");
             try{
                server_base_path_for_commands_ = std::filesystem::current_path().string();
             } catch (const std::filesystem::filesystem_error& e_fs_cwd) {
                Logger::error(std::string("Конструктор Server: КРИТИЧЕСКАЯ ОШИБКА получения CWD: ") + e_fs_cwd.what() + ". Базовый путь для команд будет пустым!");
                server_base_path_for_commands_.clear();
             }
        }
    }

    if(server_base_path_for_commands_.empty()){
        Logger::error("Конструктор Server: server_base_path_for_commands_ НЕ МОГ быть определен. Операции LOAD/SAVE могут завершиться неудачно или использовать непреднамеренную директорию.");
    } else {
        Logger::info("Конструктор Server: Итоговый server_base_path_for_commands (для файловых операций ServerCommandHandler): '" + server_base_path_for_commands_ + "'");
    }

    try {
        size_t num_threads_for_pool = config_.thread_pool_size;
        if (config_.thread_pool_size == 0) {
             Logger::warn("Конструктор Server: Размер пула потоков в конфигурации равен 0. Установка в 1.");
             num_threads_for_pool = 1;
        } else if (config_.thread_pool_size > 256) { 
             Logger::warn("Конструктор Server: Размер пула потоков в конфигурации (" + std::to_string(config_.thread_pool_size) + ") очень большой. Ограничение до 256 для стабильности.");
             num_threads_for_pool = 256;
        }
        thread_pool_ = std::make_unique<ThreadPool>(num_threads_for_pool);
        Logger::info("Конструктор Server: ThreadPool успешно создан с " + std::to_string(num_threads_for_pool) + " рабочими потоками.");
    } catch (const std::exception& e) {
        Logger::error(std::string("Конструктор Server: КРИТИЧЕСКАЯ ОШИБКА во время создания ThreadPool: ") + e.what());
        throw; 
    }

    Logger::info("Конструктор Server: Объект сервера создан. Целевой порт для прослушивания: " + std::to_string(config_.port));
}

Server::~Server() {
    Logger::info("Деструктор Server: Вызван деструктор сервера.");
    if (running_.load() || (acceptor_thread_.joinable())) { 
        Logger::debug("Деструктор Server: Сервер, по-видимому, работал или поток приема соединений доступен для join. Вызов stop() для обеспечения очистки.");
        stop(); 
    } else {
        if (thread_pool_ && thread_pool_->isRunning()) {
            Logger::debug("Деструктор Server: Сервер не был отмечен как работающий, но пул потоков работает. Остановка пула потоков.");
            thread_pool_->stop();
        }
    }
    Logger::info("Деструктор Server: Объект сервера уничтожен.");
}

bool Server::start() {
    if (running_.load()) {
        Logger::warn("Запуск Server: Сервер уже запущен. Игнорирование вызова start().");
        return true;
    }
    if (!thread_pool_) { 
        Logger::error("Запуск Server: ThreadPool не был инициализирован (nullptr). Сервер не может запуститься.");
        return false;
    }
    if (!thread_pool_->isRunning()) { 
        Logger::error("Запуск Server: ThreadPool не в рабочем состоянии. Сервер не может запуститься.");
        return false;
    }

    Logger::info("Запуск Server: Попытка запустить сервер на порту " + std::to_string(config_.port));
    if (!listen_socket_.bindSocket(config_.port)) {
        return false; 
    }

    int backlog_val = static_cast<int>(config_.thread_pool_size * 2); 
    if (backlog_val < 5) backlog_val = 5; 
    
#ifdef SOMAXCONN
    if (backlog_val > SOMAXCONN) {
        Logger::debug("Запуск Server: Рассчитанная очередь (" + std::to_string(backlog_val) + ") превышает SOMAXCONN (" + std::to_string(SOMAXCONN) + "). Используется SOMAXCONN.");
        backlog_val = SOMAXCONN;
    }
#else
    const int reasonable_max_backlog = 128; 
    if (backlog_val > reasonable_max_backlog) {
        Logger::debug("Запуск Server: Рассчитанная очередь (" + std::to_string(backlog_val) + ") превышает reasonable_max_backlog (" + std::to_string(reasonable_max_backlog) + "). Используется " + std::to_string(reasonable_max_backlog) + ".");
        backlog_val = reasonable_max_backlog;
    }
#endif

    if (!listen_socket_.listenSocket(backlog_val)) {
        listen_socket_.closeSocket(); 
        return false;
    }

    const int acceptor_timeout_ms = 500; // Таймаут 500 мс
    if (!listen_socket_.setRecvTimeout(acceptor_timeout_ms)) {
        Logger::warn("Запуск Server: Не удалось установить таймаут (" + std::to_string(acceptor_timeout_ms) + " мс) на слушающий сокет. Поток acceptor может блокироваться дольше при остановке.");
        // Не делаем это фатальной ошибкой, продолжаем, но имеем в виду, что завершение может быть долгим.
    } else {
        Logger::debug("Запуск Server: Установлен таймаут " + std::to_string(acceptor_timeout_ms) + " мс на слушающий сокет (для accept).");
    }

    running_.store(true);
    g_server_should_stop.store(false); 

    try {
        acceptor_thread_ = std::thread(&Server::acceptorThreadLoop, this);
    } catch (const std::system_error& e) {
        Logger::error(std::string("Запуск Server: КРИТИЧЕСКАЯ ОШИБКА: Не удалось запустить acceptorThreadLoop: ") + e.what());
        running_.store(false); 
        listen_socket_.closeSocket();
        if (thread_pool_ && thread_pool_->isRunning()) { 
            thread_pool_->stop();
        }
        return false;
    }

    Logger::info("Запуск Server: Сервер успешно запущен. Прослушивание на порту " + std::to_string(config_.port) + " с очередью " + std::to_string(backlog_val) + ".");
    return true;
}

void Server::stop() {
    bool expected_running = true;
    if (!running_.compare_exchange_strong(expected_running, false)) {
        Logger::info("Остановка Server: Процедура остановки уже инициирована или сервер не был отмечен как работающий.");
        if (listen_socket_.isValid()){ 
            Logger::debug("Остановка Server (уже останавливается/не работает): Закрытие listen_socket_ на всякий случай.");
            listen_socket_.closeSocket(); 
        }
        if (acceptor_thread_.joinable()) {
             Logger::debug("Остановка Server (уже останавливается/не работает): Попытка присоединить acceptor_thread_.");
             try { acceptor_thread_.join(); }
             catch (const std::system_error& e) { Logger::error(std::string("Остановка Server (уже останавливается/не работает): Исключение при acceptor_thread_.join: ") + e.what()); }
        }
        if (thread_pool_ && thread_pool_->isRunning()) {
            Logger::debug("Остановка Server (уже останавливается/не работает): Гарантируем, что thread_pool_ остановлен.");
            thread_pool_->stop();
        }
        return; 
    }

    Logger::info("Остановка Server: Инициализация процедуры завершения работы сервера...");
    g_server_should_stop.store(true); 

    Logger::debug("Остановка Server: Закрытие слушающего сокета для разблокировки потока приема соединений...");
    listen_socket_.closeSocket(); 

    if (acceptor_thread_.joinable()) {
        Logger::debug("Остановка Server: Ожидание завершения acceptor_thread_...");
        try {
            acceptor_thread_.join();
            Logger::info("Остановка Server: acceptor_thread_ успешно присоединен.");
        } catch (const std::system_error& e) {
            Logger::error(std::string("Остановка Server: Поймано исключение при присоединении acceptor_thread_: ") + e.what());
        }
    } else {
        Logger::debug("Остановка Server: acceptor_thread_ не был доступен для join (либо уже завершился, либо не был правильно запущен).");
    }

    if (thread_pool_) {
        Logger::debug("Остановка Server: Инициализация остановки ThreadPool...");
        thread_pool_->stop(); 
        Logger::info("Остановка Server: ThreadPool успешно остановлен.");
    } else {
        Logger::warn("Остановка Server: ThreadPool был null, останавливать нечего.");
    }
    
    Logger::info("Остановка Server: Процедура завершения работы сервера завершена.");
}

void Server::acceptorThreadLoop() {
    Logger::info("Цикл Приема Соединений Server: Поток приема соединений запущен. ID потока: " + Logger::get_thread_id_str());
    
    // Цикл продолжается, пока сервер должен работать И слушающий сокет валиден (хотя isValid может не сразу обновиться после close из другого потока
    while (running_.load() && !g_server_should_stop.load()) { 
        std::string client_ip_str;
        int client_port_num = 0;

        // Перед блокирующим вызовом accept, еще раз проверяем флаги
        if (!running_.load() || g_server_should_stop.load()) {
            Logger::info("Цикл Приема Соединений Server: Обнаружен запрос на остановку ПЕРЕД вызовом accept(). Завершение потока.");
            break;
        }
        
        TCPSocket client_socket_obj = listen_socket_.acceptSocket(&client_ip_str, &client_port_num);

        // Проверяем флаги сразу после возврата из accept, так как они могли измениться во время блокировки
        if (!running_.load() || g_server_should_stop.load()) { 
            if (client_socket_obj.isValid()) {
                Logger::info("Цикл Приема Соединений Server: Цикл прерван во время/после accept(). Сервер останавливается. Закрытие только что принятого клиентского сокета (fd_raw: " + std::to_string(client_socket_obj.getRawSocketDescriptor()) + ").");
                client_socket_obj.closeSocket();
            }
            Logger::info("Цикл Приема Соединений Server: Обнаружен запрос на остановку ПОСЛЕ вызова accept(). Завершение потока.");
            break; 
        }

        if (client_socket_obj.isValid()) {
            Logger::info("Цикл Приема Соединений Server: Принято новое соединение от " + (client_ip_str.empty() ? "неизвестный_ip" : client_ip_str) + ":" + std::to_string(client_port_num) +
                         ". FD клиента(raw): " + std::to_string(client_socket_obj.getRawSocketDescriptor()) + ". Постановка в очередь ThreadPool...");

            if (thread_pool_ && thread_pool_->isRunning()) {
                auto shared_client_socket = std::make_shared<TCPSocket>(std::move(client_socket_obj));
                
                if (!shared_client_socket || !shared_client_socket->isValid()) { 
                    Logger::error("Цикл Приема Соединений Server: Принятый клиентский сокет (fd_raw из исходного объекта: " + std::to_string(client_socket_obj.getRawSocketDescriptor()) + ") стал невалидным сразу после перемещения или make_shared. Клиент от " + client_ip_str + " не будет обслужен.");
                    // client_socket_obj здесь уже перемещен и невалиден
                    continue; 
                }

                bool enqueued_status = thread_pool_->enqueue([this, client_sckt_sptr = shared_client_socket]() {
                    clientHandlerTask(client_sckt_sptr); 
                });

                if (!enqueued_status) {
                    Logger::error("Цикл Приема Соединений Server: Не удалось поставить задачу обработчика клиента в очередь (ThreadPool может останавливаться или быть полным). Клиент от " + client_ip_str + " (fd_raw: " + std::to_string(shared_client_socket->getRawSocketDescriptor()) + ") не будет обслужен. Сокет будет закрыт деструктором shared_ptr.");
                }
            } else { 
                 Logger::error("Цикл Приема Соединений Server: ThreadPool не инициализирован или не работает! Невозможно обслужить клиента от " + client_ip_str + ". Сокет будет закрыт деструктором TCPSocket.");
                 if(client_socket_obj.isValid()) client_socket_obj.closeSocket(); // Закрываем, если не смогли передать и он еще валиден
            }
        } else { 

            if (!listen_socket_.isValid()) { // Проверяем, не был ли слушающий сокет закрыт
                 Logger::info("Цикл Приема Соединений Server: Слушающий сокет стал невалидным (вероятно, закрыт stop() и accept вернул ошибку). Завершение цикла.");
                 break; // Выход из цикла while
            } else if (running_.load() && !g_server_should_stop.load()) { // Если сервер все еще должен работать
                 // Это может быть ошибка EINTR или таймаут accept
                 int accept_err = listen_socket_.getLastSocketError(); // Получаем ошибку, которую вернул acceptSocket
                 Logger::debug("Цикл Приема Соединений Server: acceptSocket() вернул невалидный сокет, но сервер и слушающий сокет все еще активны. Ошибка accept: " + std::to_string(accept_err) + " (" + listen_socket_.getLastSocketErrorString() +"). Возможно, таймаут или EINTR. Продолжение цикла.");
                 // Небольшая пауза, чтобы не загружать CPU в случае частых некритических ошибок accept
                 std::this_thread::sleep_for(std::chrono::milliseconds(50)); 
            }
            // Если ни одно из вышеперечисленных, то условие внешнего while (running_.load() && !g_server_should_stop.load())
            // должно привести к выходу на следующей итерации.
        }
    }
    Logger::info("Цикл Приема Соединений Server: Поток приема соединений завершен. ID потока: " + Logger::get_thread_id_str());
}


void Server::clientHandlerTask(std::shared_ptr<TCPSocket> client_socket_sptr) {
    if (!client_socket_sptr) { 
        Logger::error("Обработчик Клиента Server: Получен нулевой shared_ptr для клиентского сокета. Задача не может быть выполнена.");
        return;
    }
    if (!client_socket_sptr->isValid()) {
        Logger::error("Обработчик Клиента Server: Получен shared_ptr на уже невалидный клиентский сокет (fd_raw: " + std::to_string(client_socket_sptr->getRawSocketDescriptor()) + "). Задача не может быть выполнена.");
        return;
    }

    const std::string client_id_for_log = "Клиент[fd_raw:" + std::to_string(client_socket_sptr->getRawSocketDescriptor()) +
                                          ", id_потока_tp:" + Logger::get_thread_id_str() + "]";
    Logger::info(client_id_for_log + ": Задача обработчика клиента запущена.");

    ServerCommandHandler command_handler(db_, tariff_plan_, server_base_path_for_commands_);
    
    const int current_client_receive_timeout_ms = DEFAULT_CLIENT_RECEIVE_TIMEOUT_MS; 

    while (running_.load() && !g_server_should_stop.load() && client_socket_sptr->isValid()) {
        bool receive_query_success = false;
        std::string query_str = client_socket_sptr->receiveAllDataWithLengthPrefix(receive_query_success, current_client_receive_timeout_ms);

        if (g_server_should_stop.load() || !running_.load()){
             Logger::info(client_id_for_log + ": Сервер останавливается. Принудительное завершение задачи клиента после попытки чтения.");
             break;
        }

        if (!receive_query_success) {
            int err_code = client_socket_sptr->getLastSocketError();
            std::string reason_log = "причина неизвестна (receive_query_success=false)";
            if (!client_socket_sptr->isValid()) {
                reason_log = "сокет стал невалидным во время или после попытки чтения";
            } else if (err_code != 0) { 
                #ifdef _WIN32
                if (err_code == WSAETIMEDOUT) reason_log = "таймаут чтения запроса клиента (WSAETIMEDOUT)";
                else if (err_code == WSAECONNRESET || err_code == WSAECONNABORTED) reason_log = "соединение сброшено/прервано клиентом или сетью";
                else reason_log = "ошибка чтения сокета WSA: " + std::to_string(err_code);
                #else
                bool is_timeout_err = (err_code == EAGAIN);
                #if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
                if (err_code == EWOULDBLOCK) is_timeout_err = true;
                #endif
                if (is_timeout_err) reason_log = "таймаут чтения запроса клиента (EAGAIN/EWOULDBLOCK)";
                else if (err_code == ECONNRESET || err_code == EPIPE) reason_log = "соединение сброшено клиентом или обрыв канала";
                else reason_log = "ошибка чтения сокета errno: " + std::to_string(err_code) + " (" + std::strerror(err_code) + ")";
                #endif
            } else { 
                 reason_log = "соединение корректно закрыто клиентом (recv вернул 0 на части длины/нагрузки)";
            }
            Logger::info(client_id_for_log + ": Не удалось получить данные от клиента или соединение закрыто (" + reason_log + "). Завершение обработчика сессии для этого клиента.");
            break; 
        }

        if (query_str.empty() && receive_query_success) { 
            Logger::debug(client_id_for_log + ": От клиента получено пустое сообщение запроса (префикс длины был 0). Ожидание следующего запроса.");
            continue;
        }

        Logger::info(client_id_for_log + ": От клиента получен запрос: \"" + query_str + "\"");

        if (query_str == "EXIT_CLIENT_SESSION") { 
            Logger::info(client_id_for_log + ": Клиент отправил явную команду 'EXIT_CLIENT_SESSION'. Корректное завершение задачи обработчика.");
            break;
        }

        Query parsed_query_obj;
        try {
            parsed_query_obj = query_parser_.parseQuery(query_str); 
            parsed_query_obj.originalQueryString = query_str;

            bool is_write_operation = (parsed_query_obj.type == QueryType::ADD ||
                                     parsed_query_obj.type == QueryType::DELETE ||
                                     parsed_query_obj.type == QueryType::EDIT ||
                                     parsed_query_obj.type == QueryType::LOAD ||
                                     parsed_query_obj.type == QueryType::SAVE);
            
            if (is_write_operation) {
                std::unique_lock<std::shared_mutex> db_lock(db_shared_mutex_);
                Logger::debug(client_id_for_log + ": Получена эксклюзивная блокировка БД для операции записи: " + parsed_query_obj.originalQueryString);
                command_handler.processAndSendCommandResponse(client_socket_sptr, parsed_query_obj);
                Logger::debug(client_id_for_log + ": Снята эксклюзивная блокировка БД.");
            } else { 
                std::shared_lock<std::shared_mutex> db_lock(db_shared_mutex_);
                Logger::debug(client_id_for_log + ": Получена разделяемая блокировка БД для операции чтения/нейтральной операции: " + parsed_query_obj.originalQueryString);
                command_handler.processAndSendCommandResponse(client_socket_sptr, parsed_query_obj);
                Logger::debug(client_id_for_log + ": Снята разделяемая блокировка БД.");
            }

            if (parsed_query_obj.type == QueryType::EXIT) {
                Logger::info(client_id_for_log + ": Команда EXIT обработана и ответ отправлен обработчиком. Серверная задача для этого клиента теперь завершится, ожидается закрытие соединения клиентом.");
                break; 
            }

        } catch (const std::runtime_error& e_parse) { 
            Logger::error(client_id_for_log + ": Ошибка разбора строки запроса клиента: '" + query_str + "'. Ошибка парсера: " + e_parse.what());
            
            ServerResponse error_response_obj; 
            error_response_obj.statusCode = SRV_STATUS_BAD_REQUEST;
            error_response_obj.statusMessage = "Сервер не смог разобрать ваш запрос: " + std::string(e_parse.what());
            error_response_obj.payloadType = SRV_PAYLOAD_TYPE_ERROR_INFO;
            error_response_obj.payloadDataStream << "ДЕТАЛИ_ОШИБКИ_РАЗБОРА_СЕРВЕРОМ: Сервер не смог понять структуру вашего запроса.\n"
                                                 << "Сообщение парсера: " << e_parse.what() << "\n"
                                                 << "Оригинальный запрос, отправленный клиентом: \"" << query_str << "\"\n";
            command_handler.sendSingleMessageResponsePart(client_socket_sptr, error_response_obj);
        }
    } 

    if (client_socket_sptr->isValid()) {
        Logger::debug(client_id_for_log + ": Задача обработчика сессии клиента завершается. Закрытие клиентского сокета.");
        client_socket_sptr->closeSocket();
    }
    Logger::info(client_id_for_log + ": Задача обработчика клиента завершена, клиентский сокет закрыт.");
}
