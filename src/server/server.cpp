/*!
 * \file server.cpp
 * \author Fedor Zilnitskiy
 * \brief Реализация класса Server для управления TCP-сервером базы данных.
 */
#include "server.h"
#include "logger.h"
#include "server_command_handler.h"
#include "file_utils.h" 
#include <iostream>      
#include <algorithm>     
#include <filesystem>    
#include <limits>        
#include <memory>        

extern std::atomic<bool> g_server_should_stop;

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

    if (!config_.server_data_root_dir.empty()) {
        std::filesystem::path data_root_path_obj(config_.server_data_root_dir);
        if (data_root_path_obj.is_absolute()) {
            server_base_path_for_commands_ = data_root_path_obj.string();
            Logger::info("Server Ctor: Используется абсолютная директория данных из конфига: '" + server_base_path_for_commands_ + "'");
        } else { 
            if (!server_executable_path.empty()) {
                try {
                    std::filesystem::path exec_dir = std::filesystem::path(server_executable_path).parent_path();
                    server_base_path_for_commands_ = (exec_dir / data_root_path_obj).lexically_normal().string();
                    Logger::info("Server Ctor: Относительная директория данных из конфига ('" + config_.server_data_root_dir + "') разрешена относительно исполняемого файла в: '" + server_base_path_for_commands_ + "'");
                } catch (const std::filesystem::filesystem_error& e_fs) {
                     Logger::warn("Server Ctor: Ошибка при разрешении относительного пути '" + config_.server_data_root_dir + "' относительно '" + server_executable_path + "': " + e_fs.what() + ". Попытка разрешить относительно CWD.");
                     try {
                        server_base_path_for_commands_ = std::filesystem::weakly_canonical(std::filesystem::absolute(data_root_path_obj)).string();
                        Logger::info("Server Ctor: Относительная директория данных из конфига ('" + config_.server_data_root_dir + "') разрешена относительно CWD в: '" + server_base_path_for_commands_ + "'");
                     } catch (const std::filesystem::filesystem_error& e_fs_cwd) {
                         Logger::error("Server Ctor: КРИТИЧЕСКАЯ ОШИБКА при разрешении относительного пути '" + config_.server_data_root_dir + "' относительно CWD: " + e_fs_cwd.what() + ". Базовый путь для команд не установлен!");
                         server_base_path_for_commands_.clear(); 
                     }
                }
            } else { 
                try {
                    server_base_path_for_commands_ = std::filesystem::weakly_canonical(std::filesystem::absolute(data_root_path_obj)).string();
                    Logger::warn("Server Ctor: Путь к исполняемому файлу не предоставлен. Относительная директория данных из конфига ('" + config_.server_data_root_dir + "') разрешена относительно CWD в: '" + server_base_path_for_commands_ + "'");
                } catch (const std::filesystem::filesystem_error& e_fs_cwd) {
                     Logger::error("Server Ctor: КРИТИЧЕСКАЯ ОШИБКА при разрешении относительного пути '" + config_.server_data_root_dir + "' относительно CWD: " + e_fs_cwd.what() + ". Базовый путь для команд не установлен!");
                     server_base_path_for_commands_.clear();
                }
            }
        }
    } else { 
        if (!server_executable_path.empty()) {
            try {
                server_base_path_for_commands_ = std::filesystem::path(server_executable_path).parent_path().string();
                Logger::info("Server Ctor: server_data_root_dir не указан. Базовый путь для команд ServerCommandHandler определен как директория исполняемого файла: '" + server_base_path_for_commands_ + "'");
            } catch (const std::exception& e_path) { 
                 Logger::warn("Server Ctor: Ошибка при получении директории исполняемого файла: " + std::string(e_path.what()) + ". Попытка использовать CWD.");
                 server_base_path_for_commands_ = std::filesystem::current_path().string();
            }
        } else { 
            server_base_path_for_commands_ = std::filesystem::current_path().string();
            Logger::warn("Server Ctor: server_data_root_dir и путь к исполняемому файлу не указаны. Базовый путь для команд ServerCommandHandler установлен в CWD: '" + server_base_path_for_commands_ + "'");
        }
    }
    if(server_base_path_for_commands_.empty()){
        Logger::warn("Server Ctor: Не удалось определить server_base_path_for_commands_. ServerCommandHandler будет использовать автоопределение/CWD для операций LOAD/SAVE.");
    }

    try {
        size_t num_threads_for_pool = config_.thread_pool_size;
        if (config_.thread_pool_size == 0) {
             Logger::warn("Server Ctor: Размер пула потоков в конфигурации равен 0. Устанавливается в 1.");
             num_threads_for_pool = 1;
        }
        thread_pool_ = std::make_unique<ThreadPool>(num_threads_for_pool);
        Logger::info("Server Ctor: ThreadPool успешно создан с " + std::to_string(num_threads_for_pool) + " потоками.");
    } catch (const std::exception& e) {
        Logger::error("Server Ctor: КРИТИЧЕСКАЯ ОШИБКА при создании ThreadPool: " + std::string(e.what()));
        throw; 
    }

    Logger::info("Server Ctor: Объект сервера сконструирован. Порт: " + std::to_string(config_.port) +
                 ". Базовый путь для команд ServerCommandHandler: '" + 
                 (server_base_path_for_commands_.empty() ? "[Будет автоопределен ServerCommandHandler]" : server_base_path_for_commands_) + "'");
}

Server::~Server() {
    Logger::info("Server Dtor: Деструктор сервера вызван.");
    if (running_.load() || (acceptor_thread_.joinable())) { 
        stop(); 
    } else {
        if (thread_pool_ && thread_pool_->isRunning()) {
            thread_pool_->stop();
        }
    }
    Logger::info("Server Dtor: Объект сервера уничтожен.");
}

bool Server::start() {
    if (running_.load()) {
        Logger::warn("Server Start: Сервер уже запущен.");
        return true;
    }
    if (!thread_pool_) { 
        Logger::error("Server Start: ThreadPool не был инициализирован. Сервер не может быть запущен.");
        return false;
    }
    if (!thread_pool_->isRunning()) { 
        Logger::error("Server Start: ThreadPool не активен (возможно, ошибка при его создании). Сервер не может быть запущен.");
        return false;
    }

    Logger::info("Server Start: Попытка запуска сервера на порту " + std::to_string(config_.port));
    if (!listen_socket_.bindSocket(config_.port)) {
        return false; 
    }

    int backlog_val = static_cast<int>(config_.thread_pool_size * 2); 
    if (backlog_val < 5) backlog_val = 5; 
#ifdef SOMAXCONN // SOMAXCONN может быть не определен на всех системах, лучше проверить
    if (backlog_val > SOMAXCONN) { 
        // Logger::debug("Server Start: Рассчитанный backlog (" + std::to_string(backlog_val) + ") превышает SOMAXCONN (" + std::to_string(SOMAXCONN) + "). Используется SOMAXCONN.");
        backlog_val = SOMAXCONN;
    }
#else // Если SOMAXCONN не определен, используем разумный максимум
    const int rozsądny_max_backlog = 128;
    if (backlog_val > rozsądny_max_backlog) {
        // Logger::debug("Server Start: Рассчитанный backlog (" + std::to_string(backlog_val) + ") превышает " + std::to_string(rozsądny_max_backlog) + ". Используется " + std::to_string(rozsądny_max_backlog) + ".");
        backlog_val = rozsądny_max_backlog;
    }
#endif


    if (!listen_socket_.listenSocket(backlog_val)) {
        listen_socket_.closeSocket(); 
        return false;
    }

    running_.store(true);
    g_server_should_stop.store(false); 

    try {
        acceptor_thread_ = std::thread(&Server::acceptorThreadLoop, this);
    } catch (const std::system_error& e) {
        Logger::error("Server Start: КРИТИЧЕСКАЯ ОШИБКА: Не удалось запустить поток acceptorThreadLoop: " + std::string(e.what()));
        running_.store(false); 
        listen_socket_.closeSocket();
        if (thread_pool_ && thread_pool_->isRunning()) { 
            thread_pool_->stop();
        }
        return false;
    }

    Logger::info("Server Start: Сервер успешно запущен и прослушивает порт " + std::to_string(config_.port) + " с backlog=" + std::to_string(backlog_val));
    return true;
}

void Server::stop() {
    bool expected_running = true;
    if (!running_.compare_exchange_strong(expected_running, false)) {
        if (listen_socket_.isValid()){ 
            listen_socket_.closeSocket();
        }
        if (acceptor_thread_.joinable()) {
             try { acceptor_thread_.join(); } 
             catch (const std::system_error& e) { Logger::error("Server Stop: Exception on acceptor_thread.join (re-entry): " + std::string(e.what())); }
        }
        if (thread_pool_ && thread_pool_->isRunning()) {
            thread_pool_->stop();
        }
        return;
    }

    Logger::info("Server Stop: Инициирована процедура остановки сервера...");
    g_server_should_stop.store(true); 

    listen_socket_.closeSocket(); 

    if (acceptor_thread_.joinable()) {
        try {
            acceptor_thread_.join();
        } catch (const std::system_error& e) {
            Logger::error("Server Stop: Ошибка при ожидании завершения acceptor_thread_: " + std::string(e.what()));
        }
    }

    if (thread_pool_) {
        thread_pool_->stop(); 
    }

    Logger::info("Server Stop: Сервер полностью остановлен.");
}

void Server::acceptorThreadLoop() {
    Logger::info("Server AcceptorLoop: Поток принятия соединений запущен. ID: " + Logger::get_thread_id_str());
    while (running_.load() && !g_server_should_stop.load()) {
        std::string client_ip_str;
        int client_port_num = 0;

        TCPSocket client_socket_obj = listen_socket_.acceptSocket(&client_ip_str, &client_port_num);

        if (!running_.load() || g_server_should_stop.load()) { 
            if (client_socket_obj.isValid()) {
                Logger::info("Server AcceptorLoop: Цикл прерван во время/после accept. Закрытие только что принятого клиентского сокета (fd: " + std::to_string(client_socket_obj.getRawSocketDescriptor()) + ").");
                client_socket_obj.closeSocket();
            }
            break; 
        }

        if (client_socket_obj.isValid()) {
            Logger::info("Server AcceptorLoop: Принято новое соединение от " + (client_ip_str.empty() ? "unknown_ip" : client_ip_str) + ":" + std::to_string(client_port_num) +
                         ". FD: " + std::to_string(client_socket_obj.getRawSocketDescriptor()) + ". Добавление задачи в ThreadPool...");

            if (thread_pool_ && thread_pool_->isRunning()) {
                auto shared_client_socket = std::make_shared<TCPSocket>(std::move(client_socket_obj));
                
                if (!shared_client_socket || !shared_client_socket->isValid()) {
                    Logger::error("Server AcceptorLoop: Принятый клиентский сокет стал невалиден перед добавлением в очередь ThreadPool. Клиент не будет обслужен.");
                    continue; 
                }

                bool enqueued_status = thread_pool_->enqueue([this, client_sckt_sptr = std::move(shared_client_socket)]() mutable {
                    clientHandlerTask(std::move(client_sckt_sptr)); 
                });

                if (!enqueued_status) {
                    Logger::error("Server AcceptorLoop: Не удалось добавить задачу обработки клиента в ThreadPool (возможно, пул останавливается или переполнен). Клиент от " + client_ip_str + " не будет обслужен.");
                }
            } else {
                 Logger::error("Server AcceptorLoop: ThreadPool не инициализирован или не активен! Невозможно обработать клиента от " + client_ip_str + ". Закрытие сокета.");
                 // client_socket_obj здесь уже невалиден из-за std::move. Если shared_ptr не был создан, закрывать нечего.
                 // Если shared_ptr был создан, но enqueue не удалось, он сам закроет сокет при уничтожении.
            }
        } else { 
            if (listen_socket_.isValid() && running_.load() && !g_server_should_stop.load()) {
                 std::this_thread::sleep_for(std::chrono::milliseconds(10)); 
            } else if (!listen_socket_.isValid()) { 
                Logger::info("Server AcceptorLoop: Слушающий сокет стал невалиден. Завершение цикла принятия соединений.");
                break; 
            }
        }
    }
    Logger::info("Server AcceptorLoop: Поток принятия соединений завершен. ID: " + Logger::get_thread_id_str());
}


void Server::clientHandlerTask(std::shared_ptr<TCPSocket> client_socket_sptr) {
    if (!client_socket_sptr || !client_socket_sptr->isValid()) {
        Logger::error("Server ClientHandler: Получен невалидный или нулевой shared_ptr на клиентский сокет. Задача не может быть выполнена.");
        return;
    }
    TCPSocket& client_socket = *client_socket_sptr;

    const std::string client_id_for_log = "Клиент[fd:" + std::to_string(client_socket.getRawSocketDescriptor()) +
                                          ",tp_th:" + Logger::get_thread_id_str() + "]";
    Logger::info(client_id_for_log + ": Задача обработки клиента запущена.");

    ServerCommandHandler command_handler(db_, tariff_plan_, server_base_path_for_commands_);
    const int client_receive_timeout_ms = DEFAULT_CLIENT_RECEIVE_TIMEOUT_MS; 

    while (running_.load() && !g_server_should_stop.load() && client_socket.isValid()) {
        bool receive_success = false;
        std::string query_str = client_socket.receiveAllDataWithLengthPrefix(receive_success, client_receive_timeout_ms);

        if (g_server_should_stop.load() || !running_.load()){ 
             Logger::info(client_id_for_log + ": Сервер останавливается, принудительное завершение задачи клиента после попытки чтения.");
             break;
        }

        if (!receive_success) {
            int err_code = client_socket.getLastSocketError();
            std::string reason_log = "неизвестная причина (receive_success=false)";
            if (!client_socket.isValid()) {
                reason_log = "сокет стал невалиден";
            } else if (err_code != 0) { 
#ifdef _WIN32
                if (err_code == WSAETIMEDOUT) reason_log = "таймаут чтения";
                else if (err_code == WSAECONNRESET || err_code == WSAECONNABORTED) reason_log = "разрыв соединения клиентом (или сервером при ошибке)";
                else reason_log = "ошибка сокета " + std::to_string(err_code);
#else
                bool is_timeout = (err_code == EAGAIN);
    #if defined(EWOULDBLOCK) && (!defined(EAGAIN) || EAGAIN != EWOULDBLOCK)
                if(err_code == EWOULDBLOCK) is_timeout = true;
    #endif
                if (is_timeout) reason_log = "таймаут чтения";
                else if (err_code == ECONNRESET || err_code == EPIPE) reason_log = "разрыв соединения клиентом (или сервером при ошибке)";
                else reason_log = "ошибка сокета " + std::to_string(err_code) + " (" + std::strerror(err_code) + ")";
#endif
            } else { 
                reason_log = "соединение закрыто клиентом (или получен 0 байт)";
            }

            Logger::info(client_id_for_log + ": Ошибка получения данных от клиента или соединение закрыто (" + reason_log + "). Завершение сессии.");
            break; 
        }

        if (query_str.empty() && receive_success) { 
            Logger::debug(client_id_for_log + ": Получен пустой запрос (длина 0) от клиента. Игнорируем.");
            continue;
        }

        Logger::info(client_id_for_log + ": Получен запрос от клиента: \"" + query_str + "\"");

        if (query_str == "EXIT_CLIENT_SESSION") { 
            Logger::info(client_id_for_log + ": Клиент запросил немедленное завершение сессии командой EXIT_CLIENT_SESSION. Сессия завершается.");
            break; 
        }

        std::string response_str;
        try {
            Query query = query_parser_.parseQuery(query_str); 
            query.originalQueryString = query_str; 
            
            bool is_write_operation = (query.type == QueryType::ADD ||
                                     query.type == QueryType::DELETE ||
                                     query.type == QueryType::EDIT ||
                                     query.type == QueryType::LOAD || 
                                     query.type == QueryType::SAVE);  
                                                                     
            if (is_write_operation) {
                std::unique_lock<std::shared_mutex> lock(db_shared_mutex_); 
                response_str = command_handler.processCommand(query);
            } else { 
                std::shared_lock<std::shared_mutex> lock(db_shared_mutex_); 
                response_str = command_handler.processCommand(query);
            }

            if (query.type == QueryType::EXIT) {
                Logger::info(client_id_for_log + ": Обработана команда EXIT от клиента. Отправка подтверждения и ожидание разрыва соединения клиентом.");
                if (!client_socket.sendAllDataWithLengthPrefix(response_str)) {
                    int err_code = client_socket.getLastSocketError();
                    Logger::error(client_id_for_log + ": Не удалось отправить финальный ответ на команду EXIT клиенту. Код ошибки сокета: " + std::to_string(err_code));
                }
                break; 
            }

        } catch (const std::runtime_error& e_parse) { 
            response_str = "ERROR\nОшибка [Сервер]: Ошибка разбора вашего запроса: " + std::string(e_parse.what()) + "\nЗапрос: \"" + query_str + "\"\n";
            Logger::error(client_id_for_log + ": Ошибка разбора запроса от клиента: " + std::string(e_parse.what()) + " (Оригинальный запрос: '" + query_str + "')");
        } catch (const std::exception& e_handler) { 
             response_str = "ERROR\nОшибка [Сервер]: Внутренняя ошибка при обработке вашего запроса: " + std::string(e_handler.what()) + "\nЗапрос: \"" + query_str + "\"\n";
             Logger::error(client_id_for_log + ": Исключение std::exception при обработке команды: " + std::string(e_handler.what()) + " (Оригинальный запрос: '" + query_str + "')");
        } catch (...) { 
             response_str = "ERROR\nОшибка [Сервер]: Произошла неизвестная критическая ошибка при обработке вашего запроса.\nЗапрос: \"" + query_str + "\"\n";
             Logger::error(client_id_for_log + ": Неизвестное исключение (...) при обработке команды (Оригинальный запрос: '" + query_str + "')");
        }

        if (!client_socket.sendAllDataWithLengthPrefix(response_str)) {
            int err_code = client_socket.getLastSocketError();
            Logger::error(client_id_for_log + ": Не удалось отправить ответ клиенту. Завершение сессии. Код ошибки сокета: " + std::to_string(err_code));
            break; 
        }
    }

    if (client_socket.isValid()) {
        client_socket.closeSocket();
    }
    Logger::info(client_id_for_log + ": Задача обработки клиента завершена, сокет закрыт.");
}
