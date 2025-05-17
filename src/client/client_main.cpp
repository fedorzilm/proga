/*!
 * \file client_main.cpp
 * \author Fedor Zilnitskiy
 * \brief Главный файл и точка входа для клиентского приложения базы данных интернет-провайдера.
 *
 * Клиентское приложение позволяет пользователю взаимодействовать с удаленным сервером базы данных.
 * Поддерживает два режима работы:
 * 1. Интерактивный режим: пользователь вводит команды в консоль, которые отправляются на сервер.
 * 2. Пакетный режим: команды считываются из файла, отправляются на сервер, а ответы сохраняются в другой файл.
 *
 * Клиент подключается к серверу по TCP/IP, используя адрес и порт, указанные в аргументах
 * командной строки. Также через аргументы настраиваются параметры логирования и таймаут
 * ожидания ответа от сервера.
 */
#include "common_defs.h"    // Общие заголовки и определения (включая DEFAULT_CLIENT_RECEIVE_TIMEOUT_MS, DEFAULT_CLIENT_LOG_FILE)
#include "logger.h"         // Логгер
#include "tcp_socket.h"     // Класс для работы с TCP сокетами

#include <iostream>         // Для std::cout, std::cin, std::cerr
#include <string>
#include <vector>
#include <fstream>          // Для std::ifstream (чтение файла команд), std::ofstream (запись файла результатов)
#include <filesystem>       // Для работы с путями C++17 (std::filesystem)
#include <algorithm>        // Для std::transform (приведение к верхнему регистру)

/*!
 * \brief Вспомогательная функция для преобразования строки в верхний регистр.
 * Используется для регистронезависимого сравнения локальных команд клиента (HELP, QUIT_CLIENT)
 * и команды EXIT, отправляемой на сервер.
 * \param s Исходная строка.
 * \return Строка в верхнем регистре.
 */
static std::string clientToUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return static_cast<char>(std::toupper(c)); });
    return s;
}

/*!
 * \brief Выводит справочную информацию по аргументам командной строки клиента.
 * \param app_name_char Имя исполняемого файла клиента (обычно `argv[0]`).
 */
static void printClientCommandLineHelp(const char* app_name_char) {
    std::string app_name = (app_name_char && app_name_char[0] != '\0') ? app_name_char : "database_client";
    // Используем std::cout, так как Logger может быть еще не настроен или выключен
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

/*!
 * \brief Обрабатывает один запрос к серверу: отправляет запрос и получает ответ.
 * \param socket Активный TCP-сокет для связи с сервером.
 * \param query Строка запроса для отправки на сервер.
 * \param out_stream_for_response Поток для вывода ответа сервера (например, `std::cout` или `std::ofstream`).
 * \param client_id_log_prefix Префикс для сообщений логгера, идентифицирующий клиента.
 * \param receive_timeout_ms Таймаут в миллисекундах для ожидания ответа от сервера.
 * \return `true`, если запрос успешно отправлен и ответ получен (или если запрос был пуст и пропущен).
 * `false` в случае ошибки отправки или получения данных.
 */
bool process_single_request_to_server(TCPSocket& socket, const std::string& query, std::ostream& out_stream_for_response, const std::string& client_id_log_prefix, int receive_timeout_ms) {
    if (query.empty()) {
        Logger::debug(client_id_log_prefix + "Пропущен пустой запрос (не будет отправлен на сервер).");
        return true; // Пустые запросы не отправляем, считаем операцию "успешной" для цикла
    }

    Logger::info(client_id_log_prefix + "Отправка запроса на сервер: \"" + query + "\"");
    if (!socket.sendAllDataWithLengthPrefix(query)) {
        // Сообщение об ошибке будет выведено в out_stream_for_response, так как это видимая клиенту ошибка
        out_stream_for_response << "КЛИЕНТ: ОШИБКА ОТПРАВКИ: Не удалось отправить запрос на сервер. Проверьте соединение.\n";
        Logger::error(client_id_log_prefix + "Не удалось отправить запрос: sendAllDataWithLengthPrefix вернул false.");
        return false; // Ошибка отправки
    }

    bool receive_success = false;
    std::string response = socket.receiveAllDataWithLengthPrefix(receive_success, receive_timeout_ms);

    if (!receive_success) {
        // TCPSocket::receiveAllDataWithLengthPrefix уже логирует детали ошибки (таймаут, разрыв и т.д.)
        out_stream_for_response << "КЛИЕНТ: ОШИБКА ПОЛУЧЕНИЯ: Не удалось получить полный ответ от сервера (возможно, таймаут или разрыв соединения).\n";
        Logger::error(client_id_log_prefix + "Ошибка получения ответа: receiveAllDataWithLengthPrefix вернул success=false.");
        return false; // Ошибка получения
    }

    Logger::debug(client_id_log_prefix + "Получен ответ от сервера (длина: " + std::to_string(response.length()) + ").");
    // Вывод ответа сервера "как есть". Предполагается, что сервер форматирует ответ, включая \n где нужно.
    out_stream_for_response << response;
    // Дополнительный перевод строки для вывода в консоль, если ответ его не содержит, для лучшей читаемости.
    if (!response.empty() && response.back() != '\n' && (&out_stream_for_response == &std::cout)) {
         out_stream_for_response << "\n";
    }
    // Можно добавить разделитель в интерактивном режиме для визуального отделения ответов
    if (&out_stream_for_response == &std::cout && !response.empty()) {
        out_stream_for_response << "----------------------------------------" << std::endl;
    }
    return true; // Успешная обработка запроса
}

/*!
 * \brief Главная функция клиентского приложения.
 * \param argc Количество аргументов командной строки.
 * \param argv Массив строк аргументов командной строки.
 * \return 0 в случае успешного завершения, 1 или другое ненулевое значение при ошибке.
 */
int main(int argc, char* argv[]) {
    // 1. Начальная инициализация логгера (уровень INFO, файл по умолчанию).
    // Это позволит логировать процесс парсинга аргументов.
    // Файл лога и уровень могут быть переопределены аргументами командной строки.
    Logger::init(LogLevel::INFO, DEFAULT_CLIENT_LOG_FILE);
    const std::string client_log_prefix = "[ClientMain] "; // Префикс для логов из main
    Logger::info(client_log_prefix + "===================================================");
    Logger::info(client_log_prefix + "====== Клиент Базы Данных Интернет-Провайдера ======");
    Logger::info(client_log_prefix + "===================================================");

    // 2. Параметры клиента со значениями по умолчанию
    std::string server_host = "";
    int server_port = 12345; // Порт по умолчанию
    std::string arg_command_file_path = ""; // Путь к файлу команд (для пакетного режима)
    std::string arg_output_file_path = "";  // Путь к файлу вывода (для пакетного режима)
    bool interactive_mode = true;           // Режим работы по умолчанию - интерактивный
    LogLevel client_log_level = LogLevel::INFO; // Уровень лога по умолчанию
    std::string client_log_file = DEFAULT_CLIENT_LOG_FILE; // Файл лога по умолчанию
    int client_receive_timeout = DEFAULT_CLIENT_RECEIVE_TIMEOUT_MS; // Таймаут по умолчанию

    // 3. Парсинг аргументов командной строки
    if (argc <= 1) { // Если нет аргументов, кроме имени программы, выводим справку
        printClientCommandLineHelp(argv[0]);
        Logger::info(client_log_prefix + "Завершение работы: не указаны аргументы (кроме, возможно, имени программы).");
        return 1; // Завершение с ошибкой, так как -s обязателен
    }

    // Сначала проверяем на -h / --help, так как это завершает программу
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printClientCommandLineHelp(argv[0]);
            Logger::info(client_log_prefix + "Запрошена справка (--help). Завершение работы.");
            return 0; // Успешное завершение после вывода справки
        }
    }

    // Парсинг остальных аргументов
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-s" || arg == "--server") && i + 1 < argc) {
            server_host = argv[++i];
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            try {
                server_port = std::stoi(argv[++i]);
                if (server_port <= 0 || server_port > 65535) { // Валидация порта
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
            interactive_mode = false; // Переключаемся в пакетный режим
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            arg_output_file_path = argv[++i];
        } else if (arg == "--timeout" && i + 1 < argc) {
            try {
                client_receive_timeout = std::stoi(argv[++i]);
                if (client_receive_timeout < 0) { // Таймаут не может быть отрицательным
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
        }
    }

    // 4. Переинициализация логгера с настройками из аргументов командной строки
    Logger::init(client_log_level, client_log_file); // Повторный вызов init обновит настройки
    Logger::info(client_log_prefix + "Логгер клиента переинициализирован. Уровень: " + std::to_string(static_cast<int>(client_log_level)) +
                 ", Файл: '" + (client_log_file.empty() ? "Только консоль (по умолчанию)" : client_log_file) + "'");

    // 5. Проверка обязательных аргументов и логика режимов
    if (server_host.empty()) {
        Logger::error(client_log_prefix + "Критическая ошибка: Адрес сервера (опция -s или --server) не указан.");
        std::cerr << "КЛИЕНТ: ОШИБКА: Адрес сервера (-s) должен быть указан." << std::endl;
        printClientCommandLineHelp(argv[0]);
        return 1;
    }
    if (!arg_output_file_path.empty() && interactive_mode) {
        Logger::warn(client_log_prefix + "Опция -o/--output ('" + arg_output_file_path + "') применима только в пакетном режиме (-f/--file). В интерактивном режиме будет проигнорирована.");
        arg_output_file_path.clear(); // Сбрасываем, чтобы не использовать случайно
    }
    if (interactive_mode && !arg_command_file_path.empty()){
        Logger::warn(client_log_prefix + "Обнаружен путь к файлу команд ('" + arg_command_file_path +"'), но режим остался интерактивным. Проверьте логику парсинга аргументов.");
    }


    Logger::info(client_log_prefix + "Итоговая конфигурация клиента:");
    Logger::info(client_log_prefix + "  Сервер: " + server_host + ":" + std::to_string(server_port));
    Logger::info(client_log_prefix + "  Режим: " + (interactive_mode ? "Интерактивный" : "Пакетный (Файл команд: '" + arg_command_file_path + "')"));
    if (!interactive_mode) {
        Logger::info(client_log_prefix + "  Файл вывода (для пакетного режима, если указан): '" + arg_output_file_path + "'");
    }
    Logger::info(client_log_prefix + "  Таймаут ожидания ответа от сервера: " + std::to_string(client_receive_timeout) + " мс");
    Logger::info(client_log_prefix + "  Уровень логирования: " + std::to_string(static_cast<int>(Logger::getLevel())));
    Logger::info(client_log_prefix + "  Файл лога клиента: '" + client_log_file + "'");

    // 6. Подключение к серверу
    TCPSocket client_socket;
    Logger::info(client_log_prefix + "Попытка подключения к серверу " + server_host + ":" + std::to_string(server_port) + "...");
    std::cout << "Клиент: Подключение к серверу " << server_host << ":" << server_port << "..." << std::endl;

    if (!client_socket.connectSocket(server_host, server_port)) {
        std::cout << "КЛИЕНТ: ОШИБКА: Не удалось подключиться к серверу " << server_host << ":" << server_port << ". Проверьте доступность сервера и правильность адреса/порта." << std::endl;
        Logger::info(client_log_prefix + "========== Клиент Базы Данных Завершил Работу (Ошибка Подключения) ==========");
        return 1;
    }
    Logger::info(client_log_prefix + "Успешно подключен к серверу " + server_host + ":" + std::to_string(server_port));
    std::cout << "Клиент: Успешно подключен к серверу." << std::endl;

    // 7. Основная логика работы клиента (интерактивный или пакетный режим)
    if (!interactive_mode) {
        // --- Пакетный режим ---
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

        std::filesystem::path effective_output_file_path_obj;
        if (!arg_output_file_path.empty()) {
            effective_output_file_path_obj = arg_output_file_path;
        } else {
            std::filesystem::path input_path_obj(arg_command_file_path);
            std::string output_filename_base = input_path_obj.stem().string();
            std::string input_file_extension = input_path_obj.extension().string();
            effective_output_file_path_obj = input_path_obj.parent_path() /
                                             (output_filename_base + ".out" + (input_file_extension.empty() ? ".txt" : input_file_extension) );
        }

        std::ofstream out_file_stream(effective_output_file_path_obj.string());
        if (!out_file_stream.is_open()) {
            Logger::error(client_log_prefix + "Не удалось открыть выходной файл для записи: \"" + effective_output_file_path_obj.string() + "\"");
            std::cout << "КЛИЕНТ: ОШИБКА: Не удалось открыть выходной файл: " << effective_output_file_path_obj.string() << std::endl;
            cmd_file.close();
            if(client_socket.isValid()) client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION");
            client_socket.closeSocket();
            Logger::info(client_log_prefix + "========== Клиент Базы Данных Завершил Работу (Ошибка Выходного Файла) ==========");
            return 1;
        }
        Logger::info(client_log_prefix + "Вывод результатов пакетной обработки будет осуществляться в файл: '" + effective_output_file_path_obj.string() + "'");
        std::cout << "Клиент: Результаты пакетной обработки будут сохранены в: \"" << effective_output_file_path_obj.string() << "\"" << std::endl;

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
            trimmed_line.erase(0, trimmed_line.find_first_not_of(" \t\n\r\f\v"));
            trimmed_line.erase(trimmed_line.find_last_not_of(" \t\n\r\f\v") + 1);
            if (trimmed_line.empty() || (!trimmed_line.empty() && trimmed_line[0] == '#')) {
                Logger::debug(client_log_prefix + "Пропущена строка #" + std::to_string(line_num_in_file) +" из файла команд (пустая или комментарий): \"" + line + "\"");
                continue;
            }
            query_num++;

            out_file_stream << "\n[Клиент] Запрос #" << query_num << " (строка файла #" << line_num_in_file << "): " << line << "\n";
            out_file_stream << "----------------------------------------\n";
            out_file_stream.flush();

            if (!process_single_request_to_server(client_socket, line, out_file_stream, client_log_prefix, client_receive_timeout)) {
                Logger::error(client_log_prefix + "Фатальная ошибка при обработке запроса (строка #" + std::to_string(line_num_in_file) + ") из файла: \"" + line + "\". Прерывание обработки файла.");
                out_file_stream << "\nОШИБКА КЛИЕНТА: Связь с сервером потеряна или не удалось обработать запрос. Обработка файла прервана.\n";
                connection_error_occurred = true;
                break;
            }
             if (clientToUpper(line) == "EXIT") {
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
                  << " Результаты сохранены в: \"" << effective_output_file_path_obj.string() << "\"" << std::endl;

        if (!client_initiated_exit_cmd && !connection_error_occurred && client_socket.isValid()) {
             Logger::info(client_log_prefix + "Отправка EXIT_CLIENT_SESSION на сервер после завершения обработки файла команд.");
             if (!client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION")) {
                 Logger::warn(client_log_prefix + "Не удалось отправить EXIT_CLIENT_SESSION серверу после пакетной обработки.");
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
                             Logger::warn(client_log_prefix + "Не удалось отправить EXIT_CLIENT_SESSION серверу при обработке EOF.");
                         }
                     }
                } else {
                    Logger::error(client_log_prefix + "Ошибка ввода std::cin в интерактивном режиме. Завершение.");
                     std::cout << "КЛИЕНТ: Критическая ошибка ввода. Завершение работы." << std::endl;
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
                        Logger::warn(client_log_prefix + "Не удалось отправить EXIT_CLIENT_SESSION серверу при выполнении QUIT_CLIENT.");
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
                 std::cout << "КЛИЕНТ: ОШИБКА: Соединение с сервером потеряно или произошла ошибка при обмене данными. Пожалуйста, перезапустите клиент." << std::endl;
                 session_active = false;
                 break;
            }

            if (upper_input == "EXIT") {
                Logger::info(client_log_prefix + "Команда EXIT отправлена серверу, ответ получен. Завершение сессии со стороны клиента.");
                std::cout << "Клиент: Сессия с сервером завершена по команде EXIT." << std::endl;
                session_active = false;
                break;
            }
        }
    }

    if (client_socket.isValid()) {
        Logger::info(client_log_prefix + "Закрытие соединения с сервером со стороны клиента...");
        client_socket.closeSocket();
        Logger::info(client_log_prefix + "Соединение с сервером успешно закрыто.");
    } else {
        Logger::info(client_log_prefix + "Соединение с сервером уже было закрыто ранее или не установлено.");
    }

    Logger::info(client_log_prefix + "========== Клиент Базы Данных Завершил Работу ==========");
    return 0;
}
