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
#include <memory> // Required for std::make_shared and std::shared_ptr

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
                std::filesystem::path exec_dir = std::filesystem::path(server_executable_path).parent_path();
                server_base_path_for_commands_ = (exec_dir / data_root_path_obj).lexically_normal().string();
                 Logger::info("Server Ctor: Используется относительная директория данных из конфига (относительно исполняемого файла): '" + config_.server_data_root_dir + "', разрешена в: '" + server_base_path_for_commands_ + "'");
            } else {
                server_base_path_for_commands_ = std::filesystem::weakly_canonical(std::filesystem::absolute(data_root_path_obj)).string();
                Logger::warn("Server Ctor: Путь к исполняемому файлу не предоставлен, относительный server_data_root_dir ('" + config_.server_data_root_dir + "') разрешен относительно CWD: '" + server_base_path_for_commands_ + "'");
            }
        }
    } else {
        if (!server_executable_path.empty()) {
            server_base_path_for_commands_ = std::filesystem::path(server_executable_path).parent_path().string();
            Logger::info("Server Ctor: server_data_root_dir не указан. Базовый путь для команд определен как директория исполняемого файла: '" + server_base_path_for_commands_ + "'");
        } else {
            server_base_path_for_commands_ = std::filesystem::current_path().string();
            Logger::warn("Server Ctor: server_data_root_dir и путь к исполняемому файлу не указаны. Базовый путь для команд установлен в CWD: '" + server_base_path_for_commands_ + "'");
        }
    }

    try {
        thread_pool_ = std::make_unique<ThreadPool>(config_.thread_pool_size);
        Logger::info("Server Ctor: ThreadPool успешно создан с " + std::to_string(config_.thread_pool_size) + " потоками.");
    } catch (const std::exception& e) {
        Logger::error("Server Ctor: КРИТИЧЕСКАЯ ОШИБКА при создании ThreadPool: " + std::string(e.what()));
        throw;
    }

    Logger::info("Server Ctor: Объект сервера сконструирован. Порт: " + std::to_string(config_.port) +
                 ". Базовый путь для команд ServerCommandHandler: '" + server_base_path_for_commands_ + "'");
}

/*!
 * \brief Деструктор сервера.
 */
Server::~Server() {
    Logger::info("Server Dtor: Деструктор сервера вызван. Попытка остановить сервер, если он еще работает.");
    if (running_.load() || (acceptor_thread_.joinable())) {
        stop();
    }
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
    if (!thread_pool_->isRunning()) {
        Logger::error("Server Start: ThreadPool не активен. Сервер не может быть запущен.");
        return false;
    }

    Logger::info("Server Start: Попытка запуска сервера на порту " + std::to_string(config_.port));
    if (!listen_socket_.bindSocket(config_.port)) {
        return false;
    }

    int backlog_val = 20;
    size_t calculated_backlog = config_.thread_pool_size * 2;
    if (calculated_backlog <= static_cast<size_t>(std::numeric_limits<int>::max())) {
        backlog_val = static_cast<int>(calculated_backlog);
    } else {
        Logger::warn("Server Start: Рассчитанный backlog (" + std::to_string(calculated_backlog) + ") превышает максимальное значение для int. Используется максимальное значение int.");
        backlog_val = std::numeric_limits<int>::max();
    }
    if (backlog_val <= 0) {
        Logger::warn("Server Start: Рассчитанный backlog (" + std::to_string(backlog_val) + ") не является положительным. Используется значение по умолчанию 20.");
        backlog_val = 20;
    }

    if (!listen_socket_.listenSocket(backlog_val)) {
        listen_socket_.closeSocket();
        return false;
    }

    running_.store(true);
    g_server_should_stop.store(false);

    try {
        acceptor_thread_ = std::thread(&Server::acceptorThreadLoop, this);
    } catch (const std::system_error& e) {
        Logger::error("Server Start: Не удалось запустить поток acceptorThreadLoop: " + std::string(e.what()));
        running_.store(false);
        listen_socket_.closeSocket();
        if (thread_pool_ && thread_pool_->isRunning()) {
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
    if (!running_.compare_exchange_strong(expected_running, false)) {
        Logger::info("Server Stop: Сервер не был запущен или уже находится в процессе остановки.");
        if (acceptor_thread_.joinable()) {
             Logger::debug("Server Stop (re-entry/post-fail): acceptor_thread_ еще joinable, ожидание...");
             try { acceptor_thread_.join(); } catch (const std::system_error& e) { Logger::error("Server Stop: Exception on acceptor_thread.join (re-entry): " + std::string(e.what())); }
        }
        if (thread_pool_ && thread_pool_->isRunning()) {
            Logger::info("Server Stop (re-entry/post-fail): Остановка ThreadPool...");
            thread_pool_->stop();
        }
        return;
    }

    Logger::info("Server Stop: Инициирована процедура остановки сервера...");
    g_server_should_stop.store(true);

    Logger::debug("Server Stop: Закрытие слушающего сокета (fd: " + std::to_string(listen_socket_.getRawSocketDescriptor()) + ")...");
    listen_socket_.closeSocket();

    if (acceptor_thread_.joinable()) {
        Logger::debug("Server Stop: Ожидание завершения acceptor_thread_...");
        try {
            acceptor_thread_.join();
            Logger::info("Server Stop: Поток acceptor_thread_ успешно завершен.");
        } catch (const std::system_error& e) {
            Logger::error("Server Stop: Ошибка при ожидании завершения acceptor_thread_: " + std::string(e.what()));
        }
    } else {
        Logger::debug("Server Stop: acceptor_thread_ не был joinable (возможно, не был запущен или уже завершен).");
    }

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
                // Оборачиваем TCPSocket в std::shared_ptr. Лямбда захватит этот shared_ptr.
                // std::shared_ptr копируем, поэтому лямбда будет копируемой.
                auto shared_client_socket = std::make_shared<TCPSocket>(std::move(client_socket_obj));

                bool enqueued_status = thread_pool_->enqueue([this, shared_client_socket]() {
                    // clientHandlerTask теперь должен принимать std::shared_ptr<TCPSocket>
                    // или мы должны здесь решить, как передать TCPSocket.
                    // Если clientHandlerTask остается с TCPSocket по значению, то
                    // это изменение здесь только отодвигает проблему.
                    // ИЗМЕНЕНИЕ: clientHandlerTask теперь принимает shared_ptr
                    clientHandlerTask(shared_client_socket);
                });

                if (!enqueued_status) {
                    Logger::error("Server AcceptorLoop: Не удалось добавить задачу обработки клиента в ThreadPool. Клиент не будет обслужен.");
                    // shared_client_socket уничтожится, TCPSocket закроется.
                }
            } else {
                 Logger::error("Server AcceptorLoop: ThreadPool не инициализирован или не активен! Невозможно обработать клиента. Закрытие сокета.");
                 client_socket_obj.closeSocket();
            }
        } else {
            if (listen_socket_.isValid() && running_.load() && !g_server_should_stop.load()) {
                 std::this_thread::sleep_for(std::chrono::milliseconds(50));
            } else if (!listen_socket_.isValid()) {
                Logger::info("Server AcceptorLoop: Слушающий сокет стал невалиден. Завершение цикла принятия соединений.");
                break;
            }
        }
    }
    Logger::info("Server AcceptorLoop: Поток принятия соединений завершен. ID: " + Logger::get_thread_id_str());
}


/*!
 * \brief Обработка одного клиента в отдельной задаче (выполняется в пуле потоков).
 * \param client_socket_sptr Умный указатель на сокет клиента.
 */
void Server::clientHandlerTask(std::shared_ptr<TCPSocket> client_socket_sptr) {
    // Используем TCPSocket через shared_ptr
    TCPSocket& client_socket = *client_socket_sptr;

    const std::string client_id_for_log = "Клиент[fd:" + std::to_string(client_socket.getRawSocketDescriptor()) +
                                          ",tp_th:" + Logger::get_thread_id_str() + "]";
    Logger::info(client_id_for_log + ": Задача обработки клиента запущена.");

    ServerCommandHandler command_handler(db_, tariff_plan_, server_base_path_for_commands_);
    const int client_receive_timeout_ms = DEFAULT_CLIENT_RECEIVE_TIMEOUT_MS;

    while (running_.load() && !g_server_should_stop.load() && client_socket.isValid()) {
        bool receive_success = false;
        std::string query_str = client_socket.receiveAllDataWithLengthPrefix(receive_success, client_receive_timeout_ms);

        if (!running_.load() || g_server_should_stop.load()){
             Logger::info(client_id_for_log + ": Сервер останавливается, принудительное завершение задачи клиента.");
             break;
        }

        if (!receive_success) {
            if (running_.load() && !g_server_should_stop.load() && client_socket.isValid()) {
                Logger::warn(client_id_for_log + ": Ошибка получения данных от клиента (или таймаут). Завершение сессии.");
            } else if (!client_socket.isValid()) {
                 Logger::info(client_id_for_log + ": Сокет клиента стал невалиден во время ожидания данных.");
            }
            else {
                 Logger::info(client_id_for_log + ": Получение данных прервано из-за остановки сервера.");
            }
            break;
        }

        if (query_str.empty() && receive_success) {
            Logger::debug(client_id_for_log + ": Получен пустой запрос (длина 0) от клиента. Игнорируем.");
            continue;
        }

        Logger::info(client_id_for_log + ": Получен запрос от клиента: \"" + query_str + "\"");

        if (query_str == "EXIT_CLIENT_SESSION") {
            Logger::info(client_id_for_log + ": Клиент запросил немедленное завершение сессии командой EXIT_CLIENT_SESSION.");
            break;
        }

        std::string response_str;
        try {
            Query query = query_parser_.parseQuery(query_str);
            query.originalQueryString = query_str;
            Logger::debug(client_id_for_log + ": Запрос успешно разобран, тип: " + std::to_string(static_cast<int>(query.type)));

            bool is_write_operation = (query.type == QueryType::ADD ||
                                     query.type == QueryType::DELETE ||
                                     query.type == QueryType::EDIT ||
                                     query.type == QueryType::LOAD ||
                                     query.type == QueryType::SAVE);

            if (is_write_operation) {
                std::unique_lock<std::shared_mutex> lock(db_shared_mutex_);
                Logger::debug(client_id_for_log + ": db_shared_mutex_ ЗАХВАЧЕН (unique_lock) для операции записи (тип: " + std::to_string(static_cast<int>(query.type)) + ").");
                response_str = command_handler.processCommand(query);
                Logger::debug(client_id_for_log + ": db_shared_mutex_ ОСВОБОЖДЕН (unique_lock) после операции записи.");
            } else {
                std::shared_lock<std::shared_mutex> lock(db_shared_mutex_);
                Logger::debug(client_id_for_log + ": db_shared_mutex_ ЗАХВАЧЕН (shared_lock) для операции чтения (тип: " + std::to_string(static_cast<int>(query.type)) + ").");
                response_str = command_handler.processCommand(query);
                Logger::debug(client_id_for_log + ": db_shared_mutex_ ОСВОБОЖДЕН (shared_lock) после операции чтения.");
            }

            if (query.type == QueryType::EXIT) {
                Logger::info(client_id_for_log + ": Обработана команда EXIT от клиента. Отправка подтверждения и завершение сессии.");
                if (!client_socket.sendAllDataWithLengthPrefix(response_str)) {
                    Logger::error(client_id_for_log + ": Не удалось отправить финальный ответ на команду EXIT клиенту.");
                } else {
                    Logger::debug(client_id_for_log + ": Финальный ответ на команду EXIT успешно отправлен клиенту.");
                }
                break;
            }

        } catch (const std::runtime_error& e_parse) {
            response_str = "Ошибка [Сервер]: Ошибка разбора вашего запроса: " + std::string(e_parse.what()) + "\n";
            Logger::error(client_id_for_log + ": Ошибка разбора запроса от клиента: " + std::string(e_parse.what()) + " (Оригинальный запрос: '" + query_str + "')");
        } catch (const std::exception& e_handler) {
             response_str = "Ошибка [Сервер]: Внутренняя ошибка при обработке вашего запроса: " + std::string(e_handler.what()) + "\n";
             Logger::error(client_id_for_log + ": Исключение std::exception при обработке команды: " + std::string(e_handler.what()) + " (Оригинальный запрос: '" + query_str + "')");
        } catch (...) {
             response_str = "Ошибка [Сервер]: Произошла неизвестная критическая ошибка при обработке вашего запроса.\n";
             Logger::error(client_id_for_log + ": Неизвестное исключение (...) при обработке команды (Оригинальный запрос: '" + query_str + "')");
        }

        if (!client_socket.sendAllDataWithLengthPrefix(response_str)) {
            Logger::error(client_id_for_log + ": Не удалось отправить ответ клиенту. Завершение сессии.");
            break;
        }
        Logger::debug(client_id_for_log + ": Ответ успешно отправлен клиенту (длина: " + std::to_string(response_str.length()) + ").");
    }

    if (client_socket.isValid()) {
        client_socket.closeSocket();
    }
    Logger::info(client_id_for_log + ": Задача обработки клиента завершена, сокет закрыт.");
}
