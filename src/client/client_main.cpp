/*!
 * \file client_main.cpp
 * \author Fedor Zilnitskiy
 * \brief Главный файл и точка входа для клиентского приложения базы данных интернет-провайдера.
 */
#include "common_defs.h"
#include "logger.h"
#include "tcp_socket.h"

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <algorithm>

static std::string clientToUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return static_cast<char>(std::toupper(c)); });
    return s;
}

static void printClientCommandLineHelp(const char* app_name_char) {
    std::string app_name = (app_name_char && app_name_char[0] != '\0') ? app_name_char : "database_client";
    std::cout << "Клиент Базы Данных Интернет-Провайдера\n";
    std::cout << "Использование: " << app_name << " -s <адрес_сервера> [опции]\n\n";
    std::cout << "Обязательные опции:\n";
    std::cout << "  -s, --server <адрес_сервера>  Адрес или имя хоста сервера базы данных.\n\n";
    std::cout << "Опциональные опции:\n";
    std::cout << "  -p, --port <номер_порта>      Сетевой порт сервера (по умолчанию: 12345).\n";
    std::cout << "  -f, --file <файл_запросов>    Пакетный режим: выполнить запросы из указанного <файла_запросов>.\n"
              << "                                (Без этой опции клиент работает в интерактивном режиме).\n";
    std::cout << "  -o, --output <файл_вывода>    Для пакетного режима (-f): указать файл для сохранения ответов сервера.\n"
              << "                                По умолчанию: <имя_файла_запросов_без_расширения>.out.<исходное_расширение> (или .txt).\n";
    std::cout << "  --timeout <мс>                Таймаут ожидания ответа от сервера в миллисекундах.\n"
              << "                                (По умолчанию: " << DEFAULT_CLIENT_RECEIVE_TIMEOUT_MS << " мс).\n";
    std::cout << "  -l, --log-level <LEVEL>       Уровень логирования клиента (DEBUG, INFO, WARN, ERROR, NONE).\n"
              << "                                (По умолчанию: INFO).\n";
    std::cout << "  --log-file <путь_к_файлу>    Путь к файлу лога клиента.\n"
              << "                                (По умолчанию: '" << DEFAULT_CLIENT_LOG_FILE << "'). Если пустой, вывод только в консоль.\n";
    std::cout << "  -h, --help                      Показать это справочное сообщение и выйти.\n";
}

bool process_single_request_to_server(TCPSocket& socket, const std::string& query, std::ostream& out_stream_for_response, const std::string& client_id_log_prefix, int receive_timeout_ms) {
    if (query.empty()) {
        Logger::debug(client_id_log_prefix + "Пропущен пустой запрос (не будет отправлен на сервер).");
        return true;
    }

    Logger::info(client_id_log_prefix + "Отправка запроса на сервер: \"" + query + "\"");
    if (!socket.sendAllDataWithLengthPrefix(query)) {
        int err_code = socket.getLastSocketError();
        std::string err_detail = "Код ошибки сокета: " + std::to_string(err_code);
#ifdef _WIN32
        // Можно добавить форматирование WSA-ошибки
#else
        if (err_code != 0) err_detail += " (" + std::string(strerror(err_code)) + ")";
#endif
        out_stream_for_response << "КЛИЕНТ: ОШИБКА ОТПРАВКИ: Не удалось отправить запрос на сервер. Проверьте соединение. " << err_detail << "\n";
        Logger::error(client_id_log_prefix + "Не удалось отправить запрос: sendAllDataWithLengthPrefix вернул false. " + err_detail);
        return false;
    }

    bool receive_success = false;
    std::string response = socket.receiveAllDataWithLengthPrefix(receive_success, receive_timeout_ms);

    if (!receive_success) {
        int err_code = socket.getLastSocketError();
        std::string err_msg_short = "Не удалось получить полный ответ от сервера.";
        std::string err_msg_detail_log = "receiveAllDataWithLengthPrefix вернул success=false.";

        if (!socket.isValid()) {
            err_msg_short = "Соединение с сервером было разорвано.";
            err_msg_detail_log += " Сокет стал невалиден.";
        } else if (err_code != 0) {
#ifdef _WIN32
            if (err_code == WSAETIMEDOUT) {
                err_msg_short = "Время ожидания ответа от сервера истекло.";
            } else if (err_code == WSAECONNRESET || err_code == WSAECONNABORTED) {
                err_msg_short = "Соединение было разорвано сервером.";
            }
            err_msg_detail_log += " Код ошибки WSA: " + std::to_string(err_code) + ".";
#else // POSIX
            bool is_timeout = (err_code == EAGAIN);
#if defined(EWOULDBLOCK) && (!defined(EAGAIN) || EAGAIN != EWOULDBLOCK)
            if(err_code == EWOULDBLOCK) is_timeout = true;
#endif
            if (is_timeout) {
                err_msg_short = "Время ожидания ответа от сервера истекло.";
            } else if (err_code == ECONNRESET || err_code == EPIPE) {
                err_msg_short = "Соединение было разорвано сервером.";
            }
            err_msg_detail_log += " Код ошибки errno: " + std::to_string(err_code) + " (" + std::strerror(err_code) + ").";
#endif
        } else { // success=false, нет ошибки сокета, сокет валиден -> вероятно recv вернул 0 (корректное закрытие)
             err_msg_short = "Соединение закрыто удаленной стороной во время ожидания ответа.";
             err_msg_detail_log += " Предположительно, соединение закрыто удаленной стороной (recv=0).";
        }
        
        out_stream_for_response << "КЛИЕНТ: ОШИБКА ПОЛУЧЕНИЯ: " << err_msg_short << "\n";
        Logger::error(client_id_log_prefix + "Ошибка получения ответа: " + err_msg_detail_log);
        return false;
    }

    Logger::debug(client_id_log_prefix + "Получен ответ от сервера (длина: " + std::to_string(response.length()) + ").");
    out_stream_for_response << response;
    if (!response.empty() && response.back() != '\n' && (&out_stream_for_response == &std::cout)) {
         out_stream_for_response << "\n";
    }
    if (&out_stream_for_response == &std::cout && !response.empty()) {
        out_stream_for_response << "----------------------------------------" << std::endl;
    }
    return true;
}

int main(int argc, char* argv[]) {
    Logger::init(LogLevel::INFO, DEFAULT_CLIENT_LOG_FILE);
    const std::string client_log_prefix = "[ClientMain] ";
    Logger::info(client_log_prefix + "===================================================");
    Logger::info(client_log_prefix + "====== Клиент Базы Данных Интернет-Провайдера ======");
    Logger::info(client_log_prefix + "===================================================");

    std::string server_host = "";
    int server_port = 12345; 
    std::string arg_command_file_path = ""; 
    std::string arg_output_file_path = "";  
    bool interactive_mode = true;          
    LogLevel client_log_level = LogLevel::INFO; 
    std::string client_log_file = DEFAULT_CLIENT_LOG_FILE; 
    int client_receive_timeout = DEFAULT_CLIENT_RECEIVE_TIMEOUT_MS;

    if (argc <= 1) {
        printClientCommandLineHelp(argv[0]);
        Logger::info(client_log_prefix + "Завершение работы: не указаны аргументы.");
        return 1; 
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printClientCommandLineHelp(argv[0]);
            Logger::info(client_log_prefix + "Запрошена справка (--help). Завершение работы.");
            return 0; 
        }
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-s" || arg == "--server") && i + 1 < argc) {
            server_host = argv[++i];
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            try {
                server_port = std::stoi(argv[++i]);
                if (server_port <= 0 || server_port > 65535) {
                     Logger::error(client_log_prefix + "Неверный номер порта: " + std::to_string(server_port) + ". Порт должен быть в диапазоне 1-65535.");
                     std::cerr << "КЛИЕНТ: ОШИБКА: Неверный номер порта: " << server_port << std::endl;
                     printClientCommandLineHelp(argv[0]); return 1;
                }
            } catch (const std::exception& e_stoi_port) {
                Logger::error(client_log_prefix + "Ошибка парсинга номера порта '" + std::string(argv[i]) + "': " + e_stoi_port.what());
                std::cerr << "КЛИЕНТ: ОШИБКА: Некорректный номер порта: " << argv[i] << std::endl;
                printClientCommandLineHelp(argv[0]); return 1;
            }
        } else if ((arg == "-f" || arg == "--file") && i + 1 < argc) {
            arg_command_file_path = argv[++i];
            interactive_mode = false; 
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            arg_output_file_path = argv[++i];
        } else if (arg == "--timeout" && i + 1 < argc) {
            try {
                client_receive_timeout = std::stoi(argv[++i]);
                if (client_receive_timeout < 0) { 
                    Logger::warn(client_log_prefix + "Таймаут не может быть отрицательным (" + std::to_string(client_receive_timeout) + "). Используется значение по умолчанию: " + std::to_string(DEFAULT_CLIENT_RECEIVE_TIMEOUT_MS) + " мс.");
                    client_receive_timeout = DEFAULT_CLIENT_RECEIVE_TIMEOUT_MS;
                }
            } catch (const std::exception& e_stoi_timeout) {
                 Logger::error(client_log_prefix + "Ошибка парсинга таймаута '" + std::string(argv[i]) + "': " + e_stoi_timeout.what());
                 std::cerr << "КЛИЕНТ: ОШИБКА: Некорректное значение таймаута: " << argv[i] << std::endl;
                 printClientCommandLineHelp(argv[0]); return 1;
            }
        } else if ((arg == "-l" || arg == "--log-level") && i + 1 < argc) {
            std::string level_str_arg = argv[++i];
            std::string level_val_upper = clientToUpper(level_str_arg);
            if (level_val_upper == "DEBUG") client_log_level = LogLevel::DEBUG;
            else if (level_val_upper == "INFO") client_log_level = LogLevel::INFO;
            else if (level_val_upper == "WARN") client_log_level = LogLevel::WARN;
            else if (level_val_upper == "ERROR") client_log_level = LogLevel::ERROR;
            else if (level_val_upper == "NONE") client_log_level = LogLevel::NONE;
            else Logger::warn(client_log_prefix + "Неизвестный уровень логирования: '" + level_str_arg + "'. Используется текущий уровень логгера.");
        } else if (arg == "--log-file" && i + 1 < argc) {
            client_log_file = argv[++i];
        } else if ( (arg == "-s" || arg == "--server" || arg == "-p" || arg == "--port" ||
                     arg == "-f" || arg == "--file" || arg == "-o" || arg == "--output" ||
                     arg == "--timeout" || arg == "-l" || arg == "--log-level" || arg == "--log-file") &&
                    i + 1 >= argc) {
            Logger::error(client_log_prefix + "Опция '" + arg + "' требует аргумент, который не был предоставлен.");
            std::cerr << "КЛИЕНТ: ОШИБКА: Опция '" << arg << "' требует аргумент." << std::endl;
            printClientCommandLineHelp(argv[0]);
            return 1;
        } else if (arg != "-h" && arg != "--help") { // -h/--help уже обработаны
             Logger::error(client_log_prefix + "Неизвестная опция или ошибка в аргументах: " + arg);
             printClientCommandLineHelp(argv[0]);
             return 1;
        }
    }

    Logger::init(client_log_level, client_log_file); 
    Logger::info(client_log_prefix + "Логгер клиента переинициализирован. Уровень: " + std::to_string(static_cast<int>(client_log_level)) +
                 ", Файл: '" + (client_log_file.empty() ? "Только консоль" : client_log_file) + "'");

    if (server_host.empty()) {
        Logger::error(client_log_prefix + "Критическая ошибка: Адрес сервера (опция -s или --server) не указан.");
        std::cerr << "КЛИЕНТ: ОШИБКА: Адрес сервера (-s) должен быть указан." << std::endl;
        printClientCommandLineHelp(argv[0]);
        return 1;
    }
    if (!arg_output_file_path.empty() && interactive_mode) {
        Logger::warn(client_log_prefix + "Опция -o/--output ('" + arg_output_file_path + "') применима только в пакетном режиме (-f/--file). В интерактивном режиме будет проигнорирована.");
        arg_output_file_path.clear(); 
    }
    
    Logger::info(client_log_prefix + "Итоговая конфигурация клиента:");
    Logger::info(client_log_prefix + "  Сервер: " + server_host + ":" + std::to_string(server_port));
    Logger::info(client_log_prefix + "  Режим: " + (interactive_mode ? "Интерактивный" : "Пакетный (Файл команд: '" + arg_command_file_path + "')"));
    if (!interactive_mode) {
        if (arg_output_file_path.empty() && !arg_command_file_path.empty()) {
             std::filesystem::path input_path_obj(arg_command_file_path);
             std::string output_filename_base = input_path_obj.stem().string();
             std::string input_file_extension = input_path_obj.extension().string();
             try {
                arg_output_file_path = (input_path_obj.parent_path() / (output_filename_base + ".out" + (input_file_extension.empty() ? ".txt" : input_file_extension) )).string();
                Logger::info(client_log_prefix + "  Файл вывода для пакетного режима не указан, будет использован: '" + arg_output_file_path + "'");
             } catch (const std::exception& e_fs) {
                Logger::error(client_log_prefix + "Ошибка при формировании пути к выходному файлу по умолчанию: " + e_fs.what());
                std::cerr << "КЛИЕНТ: ОШИБКА: Не удалось сформировать путь к выходному файлу по умолчанию." << std::endl;
                return 1;
             }
        } else if (!arg_command_file_path.empty()){ // arg_output_file_path был указан
            Logger::info(client_log_prefix + "  Файл вывода (для пакетного режима): '" + arg_output_file_path + "'");
        }
    }
    Logger::info(client_log_prefix + "  Таймаут ожидания ответа от сервера: " + std::to_string(client_receive_timeout) + " мс");
    Logger::info(client_log_prefix + "  Уровень логирования: " + std::to_string(static_cast<int>(Logger::getLevel())));

    TCPSocket client_socket;
    Logger::info(client_log_prefix + "Попытка подключения к серверу " + server_host + ":" + std::to_string(server_port) + "...");
    std::cout << "Клиент: Подключение к серверу " << server_host << ":" << server_port << "..." << std::endl;

    if (!client_socket.connectSocket(server_host, server_port)) {
        int err_code = client_socket.getLastSocketError();
        std::string err_detail = "Код ошибки сокета: " + std::to_string(err_code);
#ifdef _WIN32
        // Дополнительное форматирование для WSA ошибок
#else
        if (err_code != 0) err_detail += " (" + std::string(strerror(err_code)) + ")";
#endif
        std::cout << "КЛИЕНТ: ОШИБКА: Не удалось подключиться к серверу " << server_host << ":" << server_port 
                  << ". Проверьте доступность сервера и правильность адреса/порта. " << err_detail << std::endl;
        Logger::error(client_log_prefix + "Не удалось подключиться к серверу " + server_host + ":" + std::to_string(server_port) + ". " + err_detail);
        Logger::info(client_log_prefix + "========== Клиент Базы Данных Завершил Работу (Ошибка Подключения) ==========");
        return 1;
    }
    Logger::info(client_log_prefix + "Успешно подключен к серверу " + server_host + ":" + std::to_string(server_port));
    std::cout << "Клиент: Успешно подключен к серверу." << std::endl;

    if (!interactive_mode) {
        Logger::info(client_log_prefix + "Работа в пакетном режиме. Файл команд: '" + arg_command_file_path + "'");
        std::ifstream cmd_file(arg_command_file_path);
        if (!cmd_file.is_open()) {
            Logger::error(client_log_prefix + "Не удалось открыть файл команд для чтения: \"" + arg_command_file_path + "\"");
            std::cout << "КЛИЕНТ: ОШИБКА: Не удалось открыть файл команд: " << arg_command_file_path << std::endl;
            if(client_socket.isValid()) {
                client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION"); 
            }
            client_socket.closeSocket();
            Logger::info(client_log_prefix + "========== Клиент Базы Данных Завершил Работу (Ошибка Файла Команд) ==========");
            return 1;
        }

        std::ofstream out_file_stream(arg_output_file_path); 
        if (!out_file_stream.is_open()) {
            Logger::error(client_log_prefix + "Не удалось открыть выходной файл для записи: \"" + arg_output_file_path + "\"");
            std::cout << "КЛИЕНТ: ОШИБКА: Не удалось открыть выходной файл: " << arg_output_file_path << std::endl;
            cmd_file.close();
            if(client_socket.isValid()) client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION");
            client_socket.closeSocket();
            Logger::info(client_log_prefix + "========== Клиент Базы Данных Завершил Работу (Ошибка Выходного Файла) ==========");
            return 1;
        }
        Logger::info(client_log_prefix + "Вывод результатов пакетной обработки будет осуществляться в файл: '" + arg_output_file_path + "'");
        std::cout << "Клиент: Результаты пакетной обработки будут сохранены в: \"" << arg_output_file_path << "\"" << std::endl;

        out_file_stream << "--- Клиент: Начало выполнения команд из файла: " << arg_command_file_path << " ---\n";
        out_file_stream << "--- Клиент: Подключен к серверу: " << server_host << ":" << server_port << " ---\n\n";
        out_file_stream.flush();

        std::string line;
        int query_num = 0;
        int line_num_in_file = 0;
        bool client_initiated_exit_cmd = false;
        bool connection_error_occurred = false;

        while (std::getline(cmd_file, line)) {
            line_num_in_file++;
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            std::string trimmed_line = line;
            size_t first_not_space = trimmed_line.find_first_not_of(" \t\n\r\f\v");
            if (std::string::npos == first_not_space) { 
                trimmed_line.clear();
            } else {
                size_t last_not_space = trimmed_line.find_last_not_of(" \t\n\r\f\v");
                trimmed_line = trimmed_line.substr(first_not_space, (last_not_space - first_not_space + 1));
            }

            if (trimmed_line.empty() || (!trimmed_line.empty() && trimmed_line[0] == '#')) {
                continue;
            }
            query_num++;

            out_file_stream << "\n[Клиент] Запрос #" << query_num << " (строка файла #" << line_num_in_file << "): " << trimmed_line << "\n";
            out_file_stream << "----------------------------------------\n";
            out_file_stream.flush();

            if (!process_single_request_to_server(client_socket, trimmed_line, out_file_stream, client_log_prefix, client_receive_timeout)) {
                Logger::error(client_log_prefix + "Фатальная ошибка при обработке запроса (строка #" + std::to_string(line_num_in_file) + ") из файла: \"" + trimmed_line + "\". Прерывание обработки файла.");
                out_file_stream << "\nКЛИЕНТ: ОШИБКА: Связь с сервером потеряна или не удалось обработать запрос. Обработка файла прервана.\n";
                connection_error_occurred = true;
                break;
            }
             if (clientToUpper(trimmed_line) == "EXIT") {
                Logger::info(client_log_prefix + "Команда EXIT найдена в файле команд (строка #" + std::to_string(line_num_in_file) + "). Завершение обработки файла и сессии.");
                client_initiated_exit_cmd = true;
                break; 
            }
        }
        cmd_file.close();

        out_file_stream << "\n--- Клиент: Конец обработки файла команд: " << arg_command_file_path << " ---";
        if (connection_error_occurred) {
             out_file_stream << " (обработка прервана из-за ошибки)";
        }
        out_file_stream << std::endl;
        out_file_stream.close();

        std::cout << "Клиент: Обработка файла команд \"" << arg_command_file_path << "\" завершена. "
                  << "Всего обработано команд: " << query_num << "."
                  << " Результаты сохранены в: \"" << arg_output_file_path << "\"" << std::endl;

        if (!client_initiated_exit_cmd && !connection_error_occurred && client_socket.isValid()) {
             Logger::info(client_log_prefix + "Отправка EXIT_CLIENT_SESSION на сервер после завершения обработки файла команд.");
             if (!client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION")) {
                 Logger::warn(client_log_prefix + "Не удалось отправить EXIT_CLIENT_SESSION серверу после пакетной обработки. Код ошибки сокета: " + std::to_string(client_socket.getLastSocketError()));
             }
        }

    } else {
        // --- Интерактивный режим ---
        Logger::info(client_log_prefix + "Вход в интерактивный режим работы с сервером.");
        std::cout << "\nКлиент в интерактивном режиме. Подключен к " << server_host << ":" << server_port << ".\n";
        std::cout << "Введите 'HELP' для списка команд, 'EXIT' для завершения сессии с сервером, 'QUIT_CLIENT' для выхода из программы.\n";

        std::string user_input;
        bool session_active = true;
        while (session_active && client_socket.isValid()) { 
            std::cout << "[" << server_host << ":" << server_port << "] > ";
            if (!std::getline(std::cin, user_input)) {
                if (std::cin.eof()) {
                     Logger::info(client_log_prefix + "Обнаружен EOF в интерактивном режиме. Отправка EXIT_CLIENT_SESSION на сервер.");
                     std::cout << "\nКлиент: Обнаружен EOF (конец ввода). Завершение сессии с сервером..." << std::endl;
                     if (client_socket.isValid()) { 
                         if(!client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION")){
                             Logger::warn(client_log_prefix + "Не удалось отправить EXIT_CLIENT_SESSION серверу при обработке EOF. Код ошибки сокета: " + std::to_string(client_socket.getLastSocketError()));
                         }
                     }
                } else { 
                    Logger::error(client_log_prefix + "Ошибка ввода std::cin в интерактивном режиме (не EOF). Завершение.");
                     std::cout << "КЛИЕНТ: Критическая ошибка ввода. Завершение работы." << std::endl;
                     if (client_socket.isValid()) client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION");
                }
                session_active = false; 
                break;
            }

            if (user_input.empty()) continue;

            std::string upper_input = clientToUpper(user_input);

            if (upper_input == "QUIT_CLIENT") {
                Logger::info(client_log_prefix + "Получена локальная команда QUIT_CLIENT. Завершение работы клиента и сессии с сервером.");
                std::cout << "Клиент: Завершение работы по команде QUIT_CLIENT..." << std::endl;
                if (client_socket.isValid()) {
                    if(!client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION")){
                        Logger::warn(client_log_prefix + "Не удалось отправить EXIT_CLIENT_SESSION серверу при выполнении QUIT_CLIENT. Код ошибки сокета: " + std::to_string(client_socket.getLastSocketError()));
                    }
                }
                session_active = false; 
                break;
            }

            if (upper_input == "HELP") {
                 std::cout << "\nКлиент: Локальная команда HELP:\n";
                 std::cout << "  Доступные команды для отправки на сервер (синтаксис как в ТЗ Этапа 3):\n";
                 std::cout << "  ADD FIO \"<полное имя>\" IP \"<ip>\" DATE \"<дд.мм.гггг>\"\n";
                 std::cout << "      [TRAFFIC_IN <t0> ... <t23>] [TRAFFIC_OUT <t0> ... <t23>] [END]\n";
                 std::cout << "  SELECT [FIO \"<имя>\"] [IP \"<ip>\"] [DATE \"<дд.мм.гггг>\"] [END]\n";
                 std::cout << "  DELETE [FIO \"<имя>\"] [IP \"<ip>\"] [DATE \"<дд.мм.гггг>\"] [END]\n";
                 std::cout << "  EDIT [<критерии_фильтрации>] SET <поле1> \"<значение1>\" [<поле2> \"<значение2>\"] ... [END]\n";
                 std::cout << "      Поля для SET: FIO, IP, DATE, TRAFFIC_IN <t0..t23>, TRAFFIC_OUT <t0..t23>\n";
                 std::cout << "  CALCULATE_CHARGES [<критерии_фильтрации>] START_DATE <дата1> END_DATE <дата2> [END]\n";
                 std::cout << "  PRINT_ALL [END]\n";
                 std::cout << "  LOAD \"<имя_файла_на_сервере>\" [END]\n";
                 std::cout << "  SAVE [\"<имя_файла_на_сервере>\"] [END] (если имя файла не указано, используется последнее загруженное/сохраненное на сервере)\n";
                 std::cout << "  EXIT (для завершения текущей сессии с сервером)\n";
                 std::cout << "-------------------------------------------------------------------------------------\n";
                 std::cout << "  Локальные команды клиента (не отправляются на сервер):\n";
                 std::cout << "  HELP          - Показать эту справку.\n";
                 std::cout << "  QUIT_CLIENT   - Немедленный выход из этой клиентской программы (также завершает сессию с сервером).\n";
                 std::cout << "-------------------------------------------------------------------------------------\n";
                 std::cout << "Примечания:\n";
                 std::cout << "  * Строковые значения, содержащие пробелы, должны быть заключены в двойные кавычки (например, FIO \"Иванов Иван Иванович\").\n";
                 std::cout << "  * Ключевое слово END в конце большинства запросов опционально и может быть опущено.\n";
                 std::cout << "  * Даты вводятся в формате ДД.ММ.ГГГГ. IP-адреса в формате xxx.xxx.xxx.xxx.\n";
                 std::cout << "  * Трафик (TRAFFIC_IN, TRAFFIC_OUT) - это 24 числа (double), разделенных пробелами.\n";
                 std::cout << "----------------------------------------" << std::endl;
                continue;
            }

            if (!process_single_request_to_server(client_socket, user_input, std::cout, client_log_prefix, client_receive_timeout)) {
                 Logger::error(client_log_prefix + "Сессия с сервером прервана из-за ошибки передачи/получения данных.");
                 session_active = false; 
                 break; 
            }

            if (upper_input == "EXIT") { 
                Logger::info(client_log_prefix + "Команда EXIT отправлена серверу, ответ получен. Завершение сессии со стороны клиента.");
                session_active = false; 
                break;
            }
        }
    }

    if (client_socket.isValid()) {
        client_socket.closeSocket();
    } 

    Logger::info(client_log_prefix + "========== Клиент Базы Данных Завершил Работу ==========");
    return 0;
}
