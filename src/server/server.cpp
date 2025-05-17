/*!
 * \file server.cpp
 * \author Fedor Zilnitskiy
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

// Глобальный флаг g_server_should_stop должен быть определен где-то, обычно в server_main.cpp
// extern std::atomic<bool> g_server_should_stop; // Если он не в common_defs.h

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
            Logger::info("Server Ctor: Using absolute server_data_root_dir from config: '" + server_base_path_for_commands_ + "'");
        } else { // Относительный путь
            if (!server_executable_path.empty()) {
                try {
                    std::filesystem::path exec_dir = std::filesystem::path(server_executable_path).parent_path();
                    // Используем lexically_normal для упрощения пути ".." и "."
                    server_base_path_for_commands_ = (exec_dir / data_root_path_obj).lexically_normal().string();
                    Logger::info("Server Ctor: Relative server_data_root_dir ('" + config_.server_data_root_dir + "') resolved against executable path to: '" + server_base_path_for_commands_ + "'");
                } catch (const std::filesystem::filesystem_error& e_fs) {
                     Logger::warn(std::string("Server Ctor: Error resolving relative server_data_root_dir ('") + config_.server_data_root_dir + "') against executable path ('" + server_executable_path + "'): " + e_fs.what() + ". Attempting resolution against CWD.");
                     try {
                        server_base_path_for_commands_ = std::filesystem::weakly_canonical(std::filesystem::absolute(data_root_path_obj)).string();
                        Logger::info("Server Ctor: Relative server_data_root_dir ('" + config_.server_data_root_dir + "') resolved against CWD to: '" + server_base_path_for_commands_ + "'");
                     } catch (const std::filesystem::filesystem_error& e_fs_cwd) {
                         Logger::error(std::string("Server Ctor: CRITICAL ERROR resolving relative server_data_root_dir ('") + config_.server_data_root_dir + "') against CWD: " + e_fs_cwd.what() + ". Base path for commands will be empty or unpredictable!");
                         server_base_path_for_commands_.clear();
                     }
                }
            } else { 
                Logger::warn("Server Ctor: server_executable_path is empty. Resolving relative server_data_root_dir ('" + config_.server_data_root_dir + "') against CWD.");
                try {
                    server_base_path_for_commands_ = std::filesystem::weakly_canonical(std::filesystem::absolute(data_root_path_obj)).string();
                    Logger::info("Server Ctor: Relative server_data_root_dir ('" + config_.server_data_root_dir + "') resolved against CWD to: '" + server_base_path_for_commands_ + "'");
                } catch (const std::filesystem::filesystem_error& e_fs_cwd) {
                     Logger::error(std::string("Server Ctor: CRITICAL ERROR resolving relative server_data_root_dir ('") + config_.server_data_root_dir + "') against CWD: " + e_fs_cwd.what() + ". Base path for commands will be empty or unpredictable!");
                     server_base_path_for_commands_.clear();
                }
            }
        }
    } else { 
        Logger::info("Server Ctor: server_data_root_dir is not specified in config.");
        if (!server_executable_path.empty()) {
            try {
                // Используем директорию исполняемого файла как базовый путь по умолчанию
                server_base_path_for_commands_ = std::filesystem::path(server_executable_path).parent_path().string();
                Logger::info("Server Ctor: Defaulting server_base_path_for_commands to executable's directory: '" + server_base_path_for_commands_ + "'");
            } catch (const std::exception& e_path) { 
                 Logger::warn(std::string("Server Ctor: Error getting executable's parent directory: ") + e_path.what() + ". Defaulting server_base_path_for_commands to CWD.");
                 try {
                    server_base_path_for_commands_ = std::filesystem::current_path().string();
                 } catch (const std::filesystem::filesystem_error& e_fs_cwd) {
                    Logger::error(std::string("Server Ctor: CRITICAL ERROR getting CWD: ") + e_fs_cwd.what() + ". Base path for commands will be empty!"); // ИСПРАВЛЕНО
                    server_base_path_for_commands_.clear();
                 }
            }
        } else { 
             Logger::warn("Server Ctor: server_executable_path is empty. Defaulting server_base_path_for_commands to CWD.");
             try{
                server_base_path_for_commands_ = std::filesystem::current_path().string();
             } catch (const std::filesystem::filesystem_error& e_fs_cwd) {
                Logger::error(std::string("Server Ctor: CRITICAL ERROR getting CWD: ") + e_fs_cwd.what() + ". Base path for commands will be empty!"); // ИСПРАВЛЕНО
                server_base_path_for_commands_.clear();
             }
        }
    }

    if(server_base_path_for_commands_.empty()){
        Logger::error("Server Ctor: server_base_path_for_commands_ could NOT be determined. LOAD/SAVE operations may fail or use an unintended directory.");
    } else {
        Logger::info("Server Ctor: Final server_base_path_for_commands (for ServerCommandHandler's file operations): '" + server_base_path_for_commands_ + "'");
    }

    try {
        size_t num_threads_for_pool = config_.thread_pool_size;
        if (config_.thread_pool_size == 0) {
             Logger::warn("Server Ctor: Thread pool size in config is 0. Setting to 1.");
             num_threads_for_pool = 1;
        } else if (config_.thread_pool_size > 256) { 
             Logger::warn("Server Ctor: Thread pool size in config (" + std::to_string(config_.thread_pool_size) + ") is very large. Capping at 256 for stability.");
             num_threads_for_pool = 256;
        }
        thread_pool_ = std::make_unique<ThreadPool>(num_threads_for_pool);
        Logger::info("Server Ctor: ThreadPool successfully created with " + std::to_string(num_threads_for_pool) + " worker threads.");
    } catch (const std::exception& e) {
        Logger::error(std::string("Server Ctor: CRITICAL ERROR during ThreadPool creation: ") + e.what());
        throw; 
    }

    Logger::info("Server Ctor: Server object constructed. Target listening port: " + std::to_string(config_.port));
}

Server::~Server() {
    Logger::info("Server Dtor: Server destructor invoked.");
    if (running_.load() || (acceptor_thread_.joinable())) { 
        Logger::debug("Server Dtor: Server appears to have been running or acceptor thread is joinable. Calling stop() to ensure cleanup.");
        stop(); 
    } else {
        if (thread_pool_ && thread_pool_->isRunning()) {
            Logger::debug("Server Dtor: Server was not marked as running, but thread pool is. Stopping thread pool.");
            thread_pool_->stop();
        }
    }
    Logger::info("Server Dtor: Server object destroyed.");
}

bool Server::start() {
    if (running_.load()) {
        Logger::warn("Server Start: Server is already running. Ignoring start() call.");
        return true;
    }
    if (!thread_pool_) { 
        Logger::error("Server Start: ThreadPool was not initialized (nullptr). Server cannot start.");
        return false;
    }
    if (!thread_pool_->isRunning()) { 
        Logger::error("Server Start: ThreadPool is not in a running state. Server cannot start.");
        return false;
    }

    Logger::info("Server Start: Attempting to start server on port " + std::to_string(config_.port));
    if (!listen_socket_.bindSocket(config_.port)) {
        return false; 
    }

    int backlog_val = static_cast<int>(config_.thread_pool_size * 2); 
    if (backlog_val < 5) backlog_val = 5; 
    
#ifdef SOMAXCONN
    if (backlog_val > SOMAXCONN) {
        Logger::debug("Server Start: Calculated backlog (" + std::to_string(backlog_val) + ") exceeds SOMAXCONN (" + std::to_string(SOMAXCONN) + "). Using SOMAXCONN.");
        backlog_val = SOMAXCONN;
    }
#else
    // ИСПРАВЛЕНО: Переменная reasonable_max_backlog используется только здесь
    const int reasonable_max_backlog = 128; 
    if (backlog_val > reasonable_max_backlog) {
        Logger::debug("Server Start: Calculated backlog (" + std::to_string(backlog_val) + ") exceeds reasonable_max_backlog (" + std::to_string(reasonable_max_backlog) + "). Using " + std::to_string(reasonable_max_backlog) + ".");
        backlog_val = reasonable_max_backlog;
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
        Logger::error(std::string("Server Start: CRITICAL ERROR: Failed to launch acceptorThreadLoop: ") + e.what());
        running_.store(false); 
        listen_socket_.closeSocket();
        if (thread_pool_ && thread_pool_->isRunning()) { 
            thread_pool_->stop();
        }
        return false;
    }

    Logger::info("Server Start: Server successfully started. Listening on port " + std::to_string(config_.port) + " with backlog " + std::to_string(backlog_val) + ".");
    return true;
}

void Server::stop() {
    bool expected_running = true;
    if (!running_.compare_exchange_strong(expected_running, false)) {
        Logger::info("Server Stop: Stop procedure already initiated or server was not marked as running.");
        if (listen_socket_.isValid()){ 
            Logger::debug("Server Stop (already stopping/not running): Closing listen_socket_ just in case.");
            listen_socket_.closeSocket(); 
        }
        if (acceptor_thread_.joinable()) {
             Logger::debug("Server Stop (already stopping/not running): Attempting to join acceptor_thread_.");
             try { acceptor_thread_.join(); }
             catch (const std::system_error& e) { Logger::error(std::string("Server Stop (already stopping/not running): Exception on acceptor_thread_.join: ") + e.what()); }
        }
        if (thread_pool_ && thread_pool_->isRunning()) {
            Logger::debug("Server Stop (already stopping/not running): Ensuring thread_pool_ is stopped.");
            thread_pool_->stop();
        }
        return; 
    }

    Logger::info("Server Stop: Initiating server shutdown procedure...");
    g_server_should_stop.store(true); 

    Logger::debug("Server Stop: Closing listening socket to unblock acceptor thread...");
    listen_socket_.closeSocket(); 

    if (acceptor_thread_.joinable()) {
        Logger::debug("Server Stop: Waiting for acceptor_thread_ to complete...");
        try {
            acceptor_thread_.join();
            Logger::info("Server Stop: Acceptor_thread_ successfully joined.");
        } catch (const std::system_error& e) {
            Logger::error(std::string("Server Stop: Exception caught while joining acceptor_thread_: ") + e.what());
        }
    } else {
        Logger::debug("Server Stop: Acceptor_thread_ was not joinable (either already finished or never started properly).");
    }

    if (thread_pool_) {
        Logger::debug("Server Stop: Initiating ThreadPool shutdown...");
        thread_pool_->stop(); 
        Logger::info("Server Stop: ThreadPool successfully stopped.");
    } else {
        Logger::warn("Server Stop: ThreadPool was null, no pool to stop.");
    }
    
    Logger::info("Server Stop: Server shutdown procedure completed.");
}

void Server::acceptorThreadLoop() {
    Logger::info("Server AcceptorLoop: Acceptor thread started. Thread ID: " + Logger::get_thread_id_str());
    while (running_.load() && !g_server_should_stop.load()) { 
        std::string client_ip_str;
        int client_port_num = 0;

        TCPSocket client_socket_obj = listen_socket_.acceptSocket(&client_ip_str, &client_port_num);

        if (!running_.load() || g_server_should_stop.load()) { 
            if (client_socket_obj.isValid()) {
                Logger::info("Server AcceptorLoop: Loop interrupted during/after accept(). Server is stopping. Closing newly accepted client socket (fd_raw: " + std::to_string(client_socket_obj.getRawSocketDescriptor()) + ").");
                client_socket_obj.closeSocket();
            }
            break; 
        }

        if (client_socket_obj.isValid()) {
            Logger::info("Server AcceptorLoop: Accepted new connection from " + (client_ip_str.empty() ? "unknown_ip" : client_ip_str) + ":" + std::to_string(client_port_num) +
                         ". Client FD(raw): " + std::to_string(client_socket_obj.getRawSocketDescriptor()) + ". Enqueuing to ThreadPool...");

            if (thread_pool_ && thread_pool_->isRunning()) {
                auto shared_client_socket = std::make_shared<TCPSocket>(std::move(client_socket_obj));
                
                if (!shared_client_socket || !shared_client_socket->isValid()) { 
                    Logger::error("Server AcceptorLoop: Accepted client socket (fd_raw from original obj: " + std::to_string(client_socket_obj.getRawSocketDescriptor()) + ") became invalid immediately after move or make_shared. Client from " + client_ip_str + " will not be serviced.");
                    continue; 
                }

                bool enqueued_status = thread_pool_->enqueue([this, client_sckt_sptr = shared_client_socket]() {
                    clientHandlerTask(client_sckt_sptr); 
                });

                if (!enqueued_status) {
                    Logger::error("Server AcceptorLoop: Failed to enqueue client handler task (ThreadPool might be stopping or full). Client from " + client_ip_str + " (fd_raw: " + std::to_string(shared_client_socket->getRawSocketDescriptor()) + ") will not be serviced. Socket will be closed by shared_ptr destructor.");
                }
            } else { 
                 Logger::error("Server AcceptorLoop: ThreadPool is not initialized or not running! Cannot service client from " + client_ip_str + ". Socket will be closed by TCPSocket destructor if moved, or if client_socket_obj goes out of scope.");
            }
        } else { 
            if (listen_socket_.isValid() && running_.load() && !g_server_should_stop.load()) {
                 Logger::debug("Server AcceptorLoop: acceptSocket() returned an invalid socket, but server and listening socket are still active. This might be a non-critical error (e.g., EINTR) or a timeout on accept (if configured, though not by default here).");
                 std::this_thread::sleep_for(std::chrono::milliseconds(20)); 
            } else if (!listen_socket_.isValid()) { 
                Logger::info("Server AcceptorLoop: Listening socket is no longer valid (likely closed by stop()). Terminating acceptor loop.");
                break; 
            }
        }
    }
    Logger::info("Server AcceptorLoop: Acceptor thread terminated. Thread ID: " + Logger::get_thread_id_str());
}


void Server::clientHandlerTask(std::shared_ptr<TCPSocket> client_socket_sptr) {
    if (!client_socket_sptr) { 
        Logger::error("Server ClientHandler: Received a null shared_ptr for client socket. Task cannot be executed.");
        return;
    }
    if (!client_socket_sptr->isValid()) {
        Logger::error("Server ClientHandler: Received shared_ptr to an already invalid client socket (fd_raw: " + std::to_string(client_socket_sptr->getRawSocketDescriptor()) + "). Task cannot be executed.");
        return;
    }

    const std::string client_id_for_log = "Client[fd_raw:" + std::to_string(client_socket_sptr->getRawSocketDescriptor()) +
                                          ", tp_thread_id:" + Logger::get_thread_id_str() + "]";
    Logger::info(client_id_for_log + ": Client handler task started.");

    ServerCommandHandler command_handler(db_, tariff_plan_, server_base_path_for_commands_);
    
    const int current_client_receive_timeout_ms = DEFAULT_CLIENT_RECEIVE_TIMEOUT_MS; 

    while (running_.load() && !g_server_should_stop.load() && client_socket_sptr->isValid()) {
        bool receive_query_success = false;
        std::string query_str = client_socket_sptr->receiveAllDataWithLengthPrefix(receive_query_success, current_client_receive_timeout_ms);

        if (g_server_should_stop.load() || !running_.load()){
             Logger::info(client_id_for_log + ": Server is stopping. Forcing client task termination after read attempt.");
             break;
        }

        if (!receive_query_success) {
            int err_code = client_socket_sptr->getLastSocketError();
            std::string reason_log = "reason unknown (receive_query_success=false)";
            if (!client_socket_sptr->isValid()) {
                reason_log = "socket became invalid during or after read attempt";
            } else if (err_code != 0) { 
                #ifdef _WIN32
                if (err_code == WSAETIMEDOUT) reason_log = "client request read timeout (WSAETIMEDOUT)";
                else if (err_code == WSAECONNRESET || err_code == WSAECONNABORTED) reason_log = "connection reset/aborted by client or network";
                else reason_log = "socket read error WSA: " + std::to_string(err_code);
                #else
                // ИСПРАВЛЕНО: [-Wlogical-op]
                bool is_timeout_on_read = (err_code == EAGAIN);
                #if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
                if (err_code == EWOULDBLOCK) is_timeout_on_read = true;
                #endif
                if (is_timeout_on_read) reason_log = "client request read timeout (EAGAIN/EWOULDBLOCK)";
                else if (err_code == ECONNRESET || err_code == EPIPE) reason_log = "connection reset by client or broken pipe";
                else reason_log = "socket read error errno: " + std::to_string(err_code) + " (" + std::strerror(err_code) + ")";
                #endif
            } else { 
                 reason_log = "connection gracefully closed by client (recv returned 0 on length/payload part)";
            }
            Logger::info(client_id_for_log + ": Failed to receive data from client or connection closed (" + reason_log + "). Terminating session handler for this client.");
            break; 
        }

        if (query_str.empty() && receive_query_success) { 
            Logger::debug(client_id_for_log + ": Received an empty query message (length prefix was 0) from client. Awaiting next query.");
            continue;
        }

        Logger::info(client_id_for_log + ": Received query from client: \"" + query_str + "\"");

        if (query_str == "EXIT_CLIENT_SESSION") { 
            Logger::info(client_id_for_log + ": Client sent explicit 'EXIT_CLIENT_SESSION' command. Terminating handler task gracefully.");
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
                Logger::debug(client_id_for_log + ": Acquired unique_lock on DB for write operation: " + parsed_query_obj.originalQueryString);
                command_handler.processAndSendCommandResponse(client_socket_sptr, parsed_query_obj);
                Logger::debug(client_id_for_log + ": Released unique_lock on DB.");
            } else { 
                std::shared_lock<std::shared_mutex> db_lock(db_shared_mutex_);
                Logger::debug(client_id_for_log + ": Acquired shared_lock on DB for read/neutral operation: " + parsed_query_obj.originalQueryString);
                command_handler.processAndSendCommandResponse(client_socket_sptr, parsed_query_obj);
                Logger::debug(client_id_for_log + ": Released shared_lock on DB.");
            }

            if (parsed_query_obj.type == QueryType::EXIT) {
                Logger::info(client_id_for_log + ": EXIT command processed and response sent by handler. Server-side task for this client will now terminate, expecting client to close connection.");
                break; 
            }

        } catch (const std::runtime_error& e_parse) { 
            Logger::error(client_id_for_log + ": Error parsing client query string: '" + query_str + "'. Parser Error: " + e_parse.what());
            
            ServerResponse error_response_obj; 
            error_response_obj.statusCode = SRV_STATUS_BAD_REQUEST;
            error_response_obj.statusMessage = "Server failed to parse your query: " + std::string(e_parse.what());
            error_response_obj.payloadType = SRV_PAYLOAD_TYPE_ERROR_INFO;
            error_response_obj.payloadDataStream << "SERVER_PARSE_ERROR_DETAIL: The server could not understand the structure of your request.\n"
                                                 << "Parser Message: " << e_parse.what() << "\n"
                                                 << "Original Query Sent by Client: \"" << query_str << "\"\n";
            command_handler.sendSingleMessageResponsePart(client_socket_sptr, error_response_obj);
        }
    } 

    if (client_socket_sptr->isValid()) {
        Logger::debug(client_id_for_log + ": Client session handler task ending. Closing client socket.");
        client_socket_sptr->closeSocket();
    }
    Logger::info(client_id_for_log + ": Client handler task finished, client socket has been closed.");
}
