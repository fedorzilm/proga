// Предполагаемый путь: src/client/client_main.cpp
#include "common_defs.h"    // Общие заголовки и определения
#include "logger.h"         // Наш логгер
#include "tcp_socket.h"     // Класс для работы с сокетами
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem> // Для работы с путями C++17
#include <algorithm>  // Для std::transform toUpperQP

// Вспомогательная функция для преобразования строки в верхний регистр (если не вынесена в common).
static std::string clientToUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return static_cast<char>(std::toupper(c)); });
    return s;
}

// Вспомогательная функция для вывода справки по командной строке клиента
static void printClientCommandLineHelp(const char* app_name_char) {
    std::string app_name = (app_name_char && app_name_char[0] != '\0') ? app_name_char : "database_client";
    std::cout << "Использование: " << app_name << " -s <адрес_сервера> [опции]\n";
    std::cout << "Обязательные опции:\n";
    std::cout << "  -s, --server <адрес_сервера>  Адрес сервера (IP или имя хоста).\n";
    std::cout << "Опциональные опции:\n";
    std::cout << "  -p, --port <номер_порта>      Порт сервера (по умолч.: 12345).\n";
    std::cout << "  -f, --file <файл_запросов>    Обработать запросы из <файла_запросов>.\n";
    std::cout << "                                Результаты будут сохранены в <файл_запросов_база>.out.<расширение_исх>,\n";
    std::cout << "                                если не указана опция -o.\n";
    std::cout << "  -o, --output <файл_вывода>    Указать файл для вывода результатов (только для пакетного режима -f).\n";
    std::cout << "  -l, --log-level <LEVEL>       Уровень логирования клиента (DEBUG, INFO, WARN, ERROR, NONE).\n";
    std::cout << "                                По умолчанию INFO.\n";
    std::cout << "  -h, --help                      Показать это справочное сообщение.\n";
}

// Функция для обработки одного запроса и получения ответа
bool process_single_request_to_server(TCPSocket& socket, const std::string& query, std::ostream& out_stream_for_response, const std::string& client_id_log_prefix) {
    if (query.empty()) {
        Logger::debug(client_id_log_prefix + "Пропущен пустой запрос.");
        return true;
    }

    Logger::info(client_id_log_prefix + "Отправка запроса на сервер: \"" + query + "\"");
    if (!socket.sendAllDataWithLengthPrefix(query)) {
        // Сообщение об ошибке будет выведено в out_stream_for_response, т.к. это видимая клиенту ошибка
        out_stream_for_response << "ОШИБКА КЛИЕНТА: Не удалось отправить запрос на сервер. Проверьте соединение.\n";
        Logger::error(client_id_log_prefix + "Не удалось отправить запрос: sendAllDataWithLengthPrefix вернул false.");
        return false;
    }

    bool receive_success = false;
    // Таймаут для ожидания ответа от сервера (например, 2 минуты)
    std::string response = socket.receiveAllDataWithLengthPrefix(receive_success, 120000);

    if (!receive_success) {
        out_stream_for_response << "ОШИБКА КЛИЕНТА: Не удалось получить полный ответ от сервера (возможно, таймаут или разрыв соединения).\n";
        Logger::error(client_id_log_prefix + "Ошибка получения ответа: receiveAllDataWithLengthPrefix вернул false.");
        return false;
    }

    Logger::debug(client_id_log_prefix + "Получен ответ от сервера (длина: " + std::to_string(response.length()) + ").");
    // Вывод ответа сервера как есть, т.к. он уже должен быть отформатирован
    out_stream_for_response << response;
    if (!response.empty() && response.back() != '\n') {
         out_stream_for_response << "\n"; // Для чистоты вывода
    }
    // Разделитель можно добавить, если вывод идет в консоль, а не в файл напрямую.
    // Если out_stream_for_response это std::cout, можно добавить:
    // if (&out_stream_for_response == &std::cout) {
    //     out_stream_for_response << "----------------------------------------" << std::endl;
    // }
    return true;
}


int main(int argc, char* argv[]) {
    // Инициализация логгера на клиенте. Можно вынести путь к лог-файлу клиента в аргументы.
    Logger::init(LogLevel::INFO); // Уровень по умолчанию для клиента
    std::string client_log_prefix = "[ClientMain] ";
    Logger::info(client_log_prefix + "===========================================");
    Logger::info(client_log_prefix + "========== Клиент Базы Данных ==========");
    Logger::info(client_log_prefix + "===========================================");

    std::string server_host = "";
    int server_port = 12345;
    std::string arg_command_file_path = "";
    std::string arg_output_file_path = "";
    bool interactive_mode = true;

    if (argc <= 1) {
        printClientCommandLineHelp(argv[0]);
        Logger::info(client_log_prefix + "Завершение работы: не указаны аргументы.");
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-s" || arg == "--server") && i + 1 < argc) {
            server_host = argv[++i];
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            try {
                server_port = std::stoi(argv[++i]);
                if (server_port <= 0 || server_port > 65535) {
                     Logger::error(client_log_prefix + "Неверный номер порта: " + std::to_string(server_port) + ". Должен быть в диапазоне 1-65535.");
                     printClientCommandLineHelp(argv[0]); return 1;
                }
            } catch (const std::exception& e) {
                Logger::error(client_log_prefix + "Ошибка парсинга номера порта '" + std::string(argv[i]) + "': " + e.what());
                printClientCommandLineHelp(argv[0]); return 1;
            }
        } else if ((arg == "-f" || arg == "--file") && i + 1 < argc) {
            arg_command_file_path = argv[++i];
            interactive_mode = false;
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            arg_output_file_path = argv[++i];
        } else if ((arg == "-l" || arg == "--log-level") && i + 1 < argc) {
            std::string level_str_arg = argv[++i];
            std::string level_str_upper = clientToUpper(level_str_arg);
            if (level_str_upper == "DEBUG") Logger::setLevel(LogLevel::DEBUG);
            else if (level_str_upper == "INFO") Logger::setLevel(LogLevel::INFO);
            else if (level_str_upper == "WARN") Logger::setLevel(LogLevel::WARN);
            else if (level_str_upper == "ERROR") Logger::setLevel(LogLevel::ERROR);
            else if (level_str_upper == "NONE") Logger::setLevel(LogLevel::NONE);
            else Logger::warn(client_log_prefix + "Неизвестный уровень логирования: " + level_str_arg + ". Используется текущий (" + std::to_string(static_cast<int>(Logger::getLevel())) + ").");
        } else if (arg == "-h" || arg == "--help") {
            printClientCommandLineHelp(argv[0]);
            return 0;
        } else {
            Logger::error(client_log_prefix + "Неизвестный аргумент или неверное использование: " + arg);
            printClientCommandLineHelp(argv[0]);
            return 1;
        }
    }

    if (server_host.empty()) {
        Logger::error(client_log_prefix + "Ошибка: Адрес сервера (опция -s или --server) должен быть указан.");
        printClientCommandLineHelp(argv[0]);
        return 1;
    }
     if (!arg_output_file_path.empty() && interactive_mode) {
        Logger::warn(client_log_prefix + "Опция -o/--output применима только с пакетным режимом -f/--file. Игнорируется.");
        arg_output_file_path.clear(); // Сбрасываем, чтобы не использовать случайно
    }

    Logger::info(client_log_prefix + "Конфигурация: Сервер=" + server_host + ":" + std::to_string(server_port) +
                 ", Файл команд='" + arg_command_file_path +
                 "', Файл вывода (арг)='" + arg_output_file_path +
                 "', Интерактивный=" + (interactive_mode ? "да" : "нет") +
                 ", Уровень лога=" + std::to_string(static_cast<int>(Logger::getLevel())) );

    TCPSocket client_socket;
    Logger::info(client_log_prefix + "Попытка подключения к серверу " + server_host + ":" + std::to_string(server_port) + "...");

    if (!client_socket.connectSocket(server_host, server_port)) {
        Logger::error(client_log_prefix + "Не удалось подключиться к серверу " + server_host + ":" + std::to_string(server_port) + ". Проверьте доступность сервера и параметры.");
        // Выводим также в cout, т.к. это основная ошибка для пользователя
        std::cout << "КЛИЕНТ: Не удалось подключиться к серверу. Завершение работы." << std::endl;
        return 1;
    }
    Logger::info(client_log_prefix + "Успешно подключен к серверу.");
    std::cout << "Клиент: Успешно подключен к серверу " << server_host << ":" << server_port << std::endl;


    if (!interactive_mode) {
        // --- Пакетный режим ---
        Logger::info(client_log_prefix + "Работа в пакетном режиме. Файл команд: " + arg_command_file_path);
        std::ifstream cmd_file(arg_command_file_path);
        if (!cmd_file.is_open()) {
            Logger::error(client_log_prefix + "Не удалось открыть файл команд \"" + arg_command_file_path + "\"");
            std::cout << "КЛИЕНТ: Ошибка открытия файла команд: " << arg_command_file_path << std::endl;
            if(client_socket.isValid()) client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION");
            client_socket.closeSocket();
            return 1;
        }

        std::filesystem::path effective_output_file_path_obj;
        if (!arg_output_file_path.empty()) {
            effective_output_file_path_obj = arg_output_file_path;
        } else {
            std::filesystem::path input_path_obj(arg_command_file_path);
            std::string output_filename_base = input_path_obj.stem().string();
            std::string input_file_extension = input_path_obj.extension().string(); // Может быть пустым
            // Формируем имя выходного файла: <имя_входного_файла_без_расширения>.out<исходное_расширение_или_.txt>
            effective_output_file_path_obj = input_path_obj.parent_path() / (output_filename_base + ".out" + (input_file_extension.empty() ? ".txt" : input_file_extension) );
        }
        
        std::ofstream out_file_stream(effective_output_file_path_obj);
        if (!out_file_stream.is_open()) {
            Logger::error(client_log_prefix + "Не удалось открыть выходной файл \"" + effective_output_file_path_obj.string() + "\"");
            std::cout << "КЛИЕНТ: Ошибка открытия выходного файла: " << effective_output_file_path_obj.string() << std::endl;
            cmd_file.close();
            if(client_socket.isValid()) client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION");
            client_socket.closeSocket();
            return 1;
        }
        Logger::info(client_log_prefix + "Вывод результатов пакетной обработки в файл: " + effective_output_file_path_obj.string());

        out_file_stream << "--- Клиент: Журнал выполнения команд из файла: " << arg_command_file_path << " ---\n";
        out_file_stream << "--- Клиент: Подключен к серверу: " << server_host << ":" << server_port << " ---\n\n";
        out_file_stream.flush();

        std::string line;
        int query_num = 1;
        bool client_initiated_exit_cmd = false;
        while (std::getline(cmd_file, line)) {
            // Удаляем CR, если есть (для файлов из Windows на Linux)
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty() || (!line.empty() && line[0] == '#')) continue;

            out_file_stream << "[Клиент] Запрос #" << query_num++ << " из файла: " << line << "\n";
            out_file_stream.flush(); // Сбрасываем буфер перед отправкой запроса

            if (!process_single_request_to_server(client_socket, line, out_file_stream, client_log_prefix)) {
                Logger::error(client_log_prefix + "Фатальная ошибка при обработке запроса из файла. Прерывание.");
                client_initiated_exit_cmd = true; 
                break;
            }
             if (clientToUpper(line) == "EXIT") {
                Logger::info(client_log_prefix + "Команда EXIT найдена в файле команд. Завершение обработки файла и сессии.");
                client_initiated_exit_cmd = true; 
                break; // Сервер обработает EXIT и закроет сессию со своей стороны.
            }
        }
        cmd_file.close();
        out_file_stream << "\n--- Клиент: Конец обработки файла команд ---" << std::endl;
        out_file_stream.close();
        std::cout << "Клиент: Обработка файла \"" << arg_command_file_path << "\" завершена. Результаты в: \"" << effective_output_file_path_obj.string() << "\"" << std::endl;
        
        // Если цикл завершился не по команде EXIT из файла, и сокет еще валиден,
        // отправляем EXIT_CLIENT_SESSION, чтобы сервер знал, что клиент завершил работу.
        if (!client_initiated_exit_cmd && client_socket.isValid()) {
             Logger::info(client_log_prefix + "Отправка EXIT_CLIENT_SESSION на сервер после завершения обработки файла.");
             client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION");
        }

    } else {
        // --- Интерактивный режим ---
        Logger::info(client_log_prefix + "Вход в интерактивный режим.");
        std::cout << "Введите 'HELP' для списка команд, 'EXIT' для завершения сессии, 'QUIT_CLIENT' для выхода из программы.\n";
        std::string user_input;
        bool session_active = true;
        while (session_active) {
            std::cout << "[" << server_host << ":" << server_port << "] > ";
            if (!std::getline(std::cin, user_input)) {
                if (std::cin.eof()) {
                     Logger::info(client_log_prefix + "Обнаружен EOF в интерактивном режиме. Отправка EXIT_CLIENT_SESSION.");
                     std::cout << "\nКлиент: Обнаружен EOF. Завершение сессии..." << std::endl;
                     if (client_socket.isValid()) client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION");
                } else { // Другая ошибка std::cin
                    Logger::error(client_log_prefix + "Ошибка ввода в интерактивном режиме.");
                     std::cout << "КЛИЕНТ: Ошибка ввода. Завершение." << std::endl;
                }
                session_active = false; // Выход из цикла
                break;
            }

            if (user_input.empty()) continue;

            std::string upper_input = clientToUpper(user_input);

            if (upper_input == "QUIT_CLIENT") {
                Logger::info(client_log_prefix + "Команда QUIT_CLIENT. Завершение работы клиента и сессии с сервером.");
                std::cout << "Клиент: Завершение работы..." << std::endl;
                if (client_socket.isValid()) client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION");
                session_active = false;
                break;
            }
            
            if (upper_input == "HELP") {
                // Копируем справку из вашего UserInterface::displayHelp или пишем свою краткую
                std::cout << "Доступные команды для отправки на сервер (синтаксис как в ТЗ Этапа 3):\n";
                std::cout << "  ADD FIO \"<полное имя>\" IP \"<ip>\" DATE \"<дд.мм.гггг>\"\n";
                std::cout << "      TRAFFIC_IN <t_in0> ... <t_in23> TRAFFIC_OUT <t_out0> ... <t_out23> [END]\n";
                std::cout << "  SELECT [FIO \"<имя>\"] [IP \"<ip>\"] [DATE \"<дд.мм.гггг>\"] [END]\n";
                std::cout << "  DELETE [FIO \"<имя>\"] [IP \"<ip>\"] [DATE \"<дд.мм.гггг>\"] [END]\n";
                std::cout << "  EDIT [FIO \"<критерий>\"] ... SET [FIO \"<новое>\"] ... [END]\n";
                std::cout << "  CALCULATE_CHARGES [FIO \"<имя>\"] ... START_DATE <дата1> END_DATE <дата2> [END]\n";
                std::cout << "  PRINT_ALL [END]\n";
                std::cout << "  LOAD \"<имя_файла_на_сервере>\" [END]\n";
                std::cout << "  SAVE [\"<имя_файла_на_сервере>\"] [END]\n";
                std::cout << "  EXIT (для завершения сессии с сервером)\n";
                std::cout << "  QUIT_CLIENT (для немедленного выхода из этой клиентской программы)\n";
                std::cout << "Примечание: Строковые значения с пробелами заключайте в двойные кавычки.\n";
                continue;
            }

            if (!process_single_request_to_server(client_socket, user_input, std::cout, client_log_prefix)) {
                 Logger::error(client_log_prefix + "Сессия с сервером прервана из-за ошибки передачи/получения.");
                 std::cout << "КЛИЕНТ: Соединение с сервером потеряно или произошла ошибка. Завершение." << std::endl;
                 session_active = false;
                 break;
            }

            if (upper_input == "EXIT") {
                Logger::info(client_log_prefix + "Команда EXIT отправлена. Сервер должен был подтвердить. Завершение сессии.");
                std::cout << "Клиент: Сессия с сервером завершена по команде EXIT." << std::endl;
                session_active = false; 
                // Сервер уже получил EXIT и должен был закрыть сессию со своей стороны.
                // Клиент тоже может закрыть сокет.
                break; 
            }
        }
    }

    if (client_socket.isValid()) {
        client_socket.closeSocket();
        Logger::info(client_log_prefix + "Соединение с сервером закрыто клиентом.");
    }

    Logger::info(client_log_prefix + "========== Клиент Базы Данных Завершил Работу ==========");
    return 0;
}
