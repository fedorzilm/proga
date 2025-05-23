/*!
 * \file client_main.cpp
 * \brief Главный файл и точка входа для клиентского приложения базы данных интернет-провайдера.
 * Обеспечивает взаимодействие с сервером, отправку запросов и обработку структурированных ответов,
 * включая поддержку многочастных ответов от сервера.
 * Для Этапа 5: добавлена поддержка ID клиента и команд задержки в пакетном режиме.
 */
#include "common_defs.h"    // Общие определения и константы протокола
#include "logger.h"         // Логгер клиента
#include "tcp_socket.h"     // Класс TCPSocket для сетевого взаимодействия

#include <iostream>         // Для std::cout, std::cin, std::cerr
#include <string>           // Для std::string
#include <vector>           // Для std::vector
#include <fstream>          // Для std::ifstream, std::ofstream (пакетный режим)
#include <filesystem>       // Для работы с путями в пакетном режиме
#include <algorithm>        // Для std::transform (clientToUpper)
#include <sstream>          // Для std::istringstream (парсинг заголовков ответа и команд задержки)
#include <cstring>          // Для std::strerror в Linux/macOS
#include <thread>           // Для std::this_thread::sleep_for 
#include <chrono>           // Для std::chrono::milliseconds 
#include <random>           // Для случайных задержек
#include <iomanip>          // Для std::put_time, std::setw, std::setfill 

#ifndef UNIT_TESTING // Начало блока для функций, не нужных в unit-тестах напрямую

// Функция для получения текущей временной метки для вывода
static std::string get_current_timestamp_for_output() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t t_now = std::chrono::system_clock::to_time_t(now);
    std::tm timeinfo_tm{};
#ifdef _WIN32
    localtime_s(&timeinfo_tm, &t_now);
#else
    localtime_r(&t_now, &timeinfo_tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&timeinfo_tm, "%Y-%m-%d %H:%M:%S");
    oss << "." << std::setw(3) << std::setfill('0') << ms.count();
    return oss.str();
}

// Вспомогательная функция для преобразования строки в верхний регистр
static std::string clientToUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return static_cast<char>(std::toupper(c)); });
    return s;
}

// Вывод справки по аргументам командной строки клиента
static void printClientCommandLineHelp(const char* app_name_char) {
    // ... (код функции printClientCommandLineHelp без изменений) ...
    std::string app_name = (app_name_char && app_name_char[0] != '\0') ? app_name_char : "database_client";
    std::cout << "\nКлиент Базы Данных Интернет-Провайдера\n";
    std::cout << "Использование: " << app_name << " -s <адрес_сервера> [опции]\n\n";
    std::cout << "Обязательные опции:\n";
    std::cout << "  -s, --server <адрес_сервера>  Адрес или имя хоста сервера базы данных.\n\n";
    std::cout << "Необязательные опции:\n";
    std::cout << "  -p, --port <номер_порта>      Сетевой порт сервера (по умолчанию: 12345).\n";
    std::cout << "  -f, --file <файл_запросов>    Пакетный режим: выполнить запросы из указанного <файла_запросов>.\n"
              << "                                (Без этой опции клиент работает в интерактивном режиме).\n"
              << "                                В файле запросов можно использовать команды DELAY_MS <мс> и DELAY_RANDOM_MS <мин_мс> <макс_мс>.\n"; 
    std::cout << "  -o, --output <файл_вывода>    Для пакетного режима (-f): указать файл для сохранения ответов сервера.\n"
              << "                                По умолчанию: <имя_файла_запросов_без_расширения>.out.<оригинальное_расширение> (или .txt).\n";
    std::cout << "  --timeout <мс>                Таймаут ожидания ответа от сервера в миллисекундах.\n"
              << "                                (По умолчанию: " << DEFAULT_CLIENT_RECEIVE_TIMEOUT_MS << " мс).\n";
    std::cout << "  -l, --log-level <УРОВЕНЬ>     Уровень логирования клиента (DEBUG, INFO, WARN, ERROR, NONE).\n"
              << "                                (По умолчанию: INFO).\n";
    std::cout << "  --log-file <путь_к_файлу>    Путь к файлу лога клиента.\n"
              << "                                (По умолчанию: '" << DEFAULT_CLIENT_LOG_FILE << "'). Если пусто, логи только в консоль.\n";
    std::cout << "  --client-id <ID_клиента>      Строковый идентификатор для этого экземпляра клиента (полезно при запуске нескольких клиентов).\n";
    std::cout << "  -h, --help                      Показать это справочное сообщение и выйти.\n" << std::endl;
}

#endif // UNIT_TESTING 

// ... остальная часть client_main.cpp, включая process_single_request_to_server и main ...
// Убедитесь, что вызовы get_current_timestamp_for_output() в main НЕ находятся внутри блока #ifndef UNIT_TESTING,
// если main компилируется только для основного приложения клиента, а не для тестов.
// Если main также компилируется для тестов (что маловероятно, если у вас есть test_main.cpp для gtest),
// то логика должна быть другой.
// Судя по ошибке, проблема в том, что client_main.cpp.o собирается для run_unit_tests,
// и в этом контексте main() из client_main.cpp не является точкой входа,
// следовательно, статические функции, вызываемые только из него, считаются неиспользуемыми.

// Структура для хранения разобранного ответа сервера
struct ParsedServerResponse {
    int statusCode = -1;
    std::string statusMessage;
    size_t recordsInPayload = 0;
    size_t totalRecordsOverall = 0;
    std::string payloadType;
    std::string payloadData; 

    void reset() {
        statusCode = -1;
        statusMessage.clear();
        recordsInPayload = 0;
        totalRecordsOverall = 0;
        payloadType.clear();
        payloadData.clear();
    }
};

ParsedServerResponse parseRawServerResponse(const std::string& raw_response, const std::string& client_log_prefix_param) {
    ParsedServerResponse parsed_response;
    std::istringstream response_parser_stream(raw_response);
    std::string header_line_buffer;
    bool data_marker_found_in_header = false;

    while (std::getline(response_parser_stream, header_line_buffer)) {
        if (header_line_buffer.rfind(SRV_HEADER_DATA_MARKER, 0) == 0) {
            data_marker_found_in_header = true;
            std::ostringstream payload_collector_oss;
            payload_collector_oss << response_parser_stream.rdbuf();
            parsed_response.payloadData = payload_collector_oss.str();
            break;
        }
        std::string key_str, value_str;
        size_t colon_pos = header_line_buffer.find(':');
        if (colon_pos != std::string::npos) {
            key_str = header_line_buffer.substr(0, colon_pos);
            value_str = header_line_buffer.substr(colon_pos + 1);
            key_str.erase(0, key_str.find_first_not_of(" \t\r\n")); key_str.erase(key_str.find_last_not_of(" \t\r\n") + 1);
            value_str.erase(0, value_str.find_first_not_of(" \t\r\n")); value_str.erase(value_str.find_last_not_of(" \t\r\n") + 1);

            if (key_str == SRV_HEADER_STATUS) { try { parsed_response.statusCode = std::stoi(value_str); } catch(const std::exception& e_stoi_status){ Logger::warn(client_log_prefix_param + "Не удалось разобрать значение STATUS '" + value_str + "': " + e_stoi_status.what()); parsed_response.statusCode = -2; } }
            else if (key_str == SRV_HEADER_MESSAGE) { parsed_response.statusMessage = value_str; }
            else if (key_str == SRV_HEADER_RECORDS_IN_PAYLOAD) { try { parsed_response.recordsInPayload = std::stoul(value_str); } catch(const std::exception& e_stoul_rec){ Logger::warn(client_log_prefix_param + "Не удалось разобрать значение RECORDS_IN_PAYLOAD '" + value_str + "': " + e_stoul_rec.what()); } }
            else if (key_str == SRV_HEADER_TOTAL_RECORDS) { try { parsed_response.totalRecordsOverall = std::stoul(value_str); } catch(const std::exception& e_stoul_total){  Logger::warn(client_log_prefix_param + "Не удалось разобрать значение TOTAL_RECORDS '" + value_str + "': " + e_stoul_total.what()); } }
            else if (key_str == SRV_HEADER_PAYLOAD_TYPE) { parsed_response.payloadType = value_str; }
        }
    }
    
    if (!data_marker_found_in_header || parsed_response.statusCode == -1 || parsed_response.statusCode == -2 ) {
        Logger::error(client_log_prefix_param + "Ошибка разбора заголовка ответа сервера. Маркер данных найден: " + (data_marker_found_in_header ? "да" : "нет") + ", Разобранный код статуса: " + std::to_string(parsed_response.statusCode) + ". Сырая часть: " + raw_response.substr(0, 200) + (raw_response.length() > 200 ? "..." : ""));
        parsed_response.statusCode = -999; 
        parsed_response.statusMessage = "КЛИЕНТ: ОШИБКА ПРОТОКОЛА: Неверный формат заголовка ответа от сервера.";
        parsed_response.payloadData = "Получена сырая часть:\n" + raw_response;
    }
    return parsed_response;
}

bool process_single_request_to_server(TCPSocket& socket, const std::string& query,
                                      std::ostream& out_stream_for_response,
                                      const std::string& client_log_prefix_param, int receive_timeout_ms) {
    if (query.empty()) {
        Logger::debug(client_log_prefix_param + "Пропуск пустого запроса (не будет отправлен на сервер).");
        return true;
    }

    Logger::info(client_log_prefix_param + "Отправка запроса на сервер: \"" + query + "\"");
    if (!socket.sendAllDataWithLengthPrefix(query)) {
        int err_code = socket.getLastSocketError();
        std::string err_detail_str = "Код ошибки сокета: " + std::to_string(err_code);
#ifdef _WIN32
#else
        if (err_code != 0) err_detail_str += " (" + std::string(strerror(err_code)) + ")";
#endif
        out_stream_for_response << client_log_prefix_param << "КЛИЕНТ: ОШИБКА ОТПРАВКИ: Не удалось отправить запрос на сервер. Проверьте соединение. " << err_detail_str << "\n";
        Logger::error(client_log_prefix_param + "Не удалось отправить запрос: sendAllDataWithLengthPrefix вернул false. " + err_detail_str);
        return false; 
    }

    bool in_multipart_session = false;
    size_t multipart_total_records_overall = 0;
    size_t multipart_records_processed_so_far = 0;
    bool any_server_error_reported = false;
    std::string raw_message_part_from_server; 

    do {
        in_multipart_session = false; 

        bool current_part_receive_success = false;
        raw_message_part_from_server = socket.receiveAllDataWithLengthPrefix(current_part_receive_success, receive_timeout_ms);

        if (!current_part_receive_success) {
            int err_code = socket.getLastSocketError();
            std::string err_msg_short = "Не удалось получить полную часть ответа от сервера.";
            std::string err_msg_detail_log = "receiveAllDataWithLengthPrefix вернул success=false.";

            if (!socket.isValid()) {
                err_msg_short = "Соединение с сервером было потеряно.";
                err_msg_detail_log += " Сокет стал невалидным.";
            } else if (err_code != 0) { 
                #ifdef _WIN32
                if (err_code == WSAETIMEDOUT) err_msg_short = "Таймаут ответа от сервера.";
                else if (err_code == WSAECONNRESET || err_code == WSAECONNABORTED) err_msg_short = "Соединение было сброшено сервером.";
                else err_msg_short = "Сетевая ошибка при получении данных.";
                err_msg_detail_log += " Код ошибки WSA: " + std::to_string(err_code) + ".";
                #else
                bool is_timeout_err = (err_code == EAGAIN);
                #if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
                if (err_code == EWOULDBLOCK) is_timeout_err = true;
                #endif
                if (is_timeout_err) err_msg_short = "Таймаут ответа от сервера.";
                else if (err_code == ECONNRESET || err_code == EPIPE) err_msg_short = "Соединение было сброшено сервером или канал поврежден.";
                else err_msg_short = "Сетевая ошибка при получении данных.";
                err_msg_detail_log += " Errno: " + std::to_string(err_code) + " (" + std::strerror(err_code) + ").";
                #endif
            } else { 
                 err_msg_short = "Соединение закрыто сервером во время ожидания части ответа.";
                 err_msg_detail_log += " Предполагается корректное закрытие соединения удаленной стороной (recv вернул 0 на части длины/нагрузки).";
            }
            out_stream_for_response << client_log_prefix_param << "КЛИЕНТ: ОШИБКА ПОЛУЧЕНИЯ: " << err_msg_short << "\n";
            Logger::error(client_log_prefix_param + "Ошибка получения части ответа: " + err_msg_detail_log);
            return false; 
        }
        
        Logger::debug(client_log_prefix_param + "Получен блок ответа от сервера (сырая длина: " + std::to_string(raw_message_part_from_server.length()) + ").");

        ParsedServerResponse response_data = parseRawServerResponse(raw_message_part_from_server, client_log_prefix_param);

        if (response_data.statusCode == -999) { 
             out_stream_for_response << client_log_prefix_param << response_data.statusMessage << "\n" << response_data.payloadData << "\n";
             return false;
        }
        
        Logger::info(client_log_prefix_param + "Часть Ответа Сервера Разобрана: Статус=" + std::to_string(response_data.statusCode) +
                     ", Сообщения=\"" + response_data.statusMessage + "\", Тип Нагрузки=" + response_data.payloadType +
                     ", Записей В Этой Части=" + std::to_string(response_data.recordsInPayload) + 
                     ", Всего Ожидается (если многочаст.)=" + std::to_string(response_data.totalRecordsOverall));

        std::string user_message_to_display = "Сервер: " + response_data.statusMessage;
        
        if (response_data.statusCode == SRV_STATUS_OK_MULTI_PART_BEGIN) {
            user_message_to_display = "Сервер: " + response_data.statusMessage +
                                      " Всего записей: " + std::to_string(response_data.totalRecordsOverall) +
                                      ". Записей в этой части: " + std::to_string(response_data.recordsInPayload) + ".";
            multipart_total_records_overall = response_data.totalRecordsOverall;
            multipart_records_processed_so_far = 0; 
            in_multipart_session = true;
        } else if (response_data.statusCode == SRV_STATUS_OK_MULTI_PART_CHUNK) {
            size_t records_still_to_come_including_current = (multipart_total_records_overall > multipart_records_processed_so_far) ? (multipart_total_records_overall - multipart_records_processed_so_far) : 0;
             user_message_to_display = "Сервер: " + response_data.statusMessage + 
                                       " Осталось записей (оценка): " + std::to_string(records_still_to_come_including_current) + 
                                       ". Записей в этой части: " + std::to_string(response_data.recordsInPayload) + ".";
            in_multipart_session = true;
        } else if (response_data.statusCode == SRV_STATUS_OK_MULTI_PART_END) {
            in_multipart_session = false;
             if (multipart_total_records_overall > 0 && (multipart_records_processed_so_far + response_data.recordsInPayload) != multipart_total_records_overall) {
                 std::string warning_str = client_log_prefix_param + "КЛИЕНТ ПРЕДУПРЕЖДЕНИЕ: Количество обработанных записей (" + std::to_string(multipart_records_processed_so_far + response_data.recordsInPayload) +
                                          ") не совпадает с общим ожидаемым сервером (" + std::to_string(multipart_total_records_overall) + ") в многочастном ответе.\n";
                 out_stream_for_response << warning_str; 
                 Logger::warn(client_log_prefix_param + warning_str);
            }
        } else { 
            in_multipart_session = false;
        }

        if (!user_message_to_display.empty()) {
            out_stream_for_response << client_log_prefix_param << user_message_to_display << "\n";
        }

        if (response_data.statusCode < SRV_STATUS_BAD_REQUEST) {
            if (!response_data.payloadData.empty()) { 
                out_stream_for_response << response_data.payloadData; 
                if (response_data.payloadData.back() != '\n') {
                    out_stream_for_response << "\n";
                }
            }
            if (response_data.payloadType == SRV_PAYLOAD_TYPE_PROVIDER_RECORDS_LIST) {
                 multipart_records_processed_so_far += response_data.recordsInPayload;
            }
        } else { 
            any_server_error_reported = true;
            if (response_data.payloadType == SRV_PAYLOAD_TYPE_ERROR_INFO && !response_data.payloadData.empty()) {
                 if (response_data.payloadData.find(response_data.statusMessage) == std::string::npos || response_data.statusMessage.empty()) { // ИЗМЕНЕНО: добавлена проверка на пустое statusMessage
                    out_stream_for_response << response_data.payloadData; 
                     if (!response_data.payloadData.empty() && response_data.payloadData.back() != '\n') {
                        out_stream_for_response << "\n";
                    }
                 }
            }
            in_multipart_session = false; 
        }

    } while (in_multipart_session && socket.isValid() && !any_server_error_reported);
    
    if (!query.empty() && (&out_stream_for_response == &std::cout)) {
        out_stream_for_response << "----------------------------------------" << std::endl;
    } else if ((&out_stream_for_response != &std::cout) && !raw_message_part_from_server.empty()){
    }

    return true;
}


#ifndef UNIT_TESTING 
int main(int argc, char* argv[]) {
    std::string client_instance_id_str = "client"; // НОВОЕ: ID клиента по умолчанию
    std::string base_log_prefix = "[ClientMain"; 
    
    std::string server_host_address;
    int server_port_number = 12345; 
    std::string batch_command_file_path;
    std::string batch_output_file_path;
    bool is_interactive_mode = true;
    LogLevel client_final_log_level = LogLevel::INFO;
    std::string client_final_log_file = DEFAULT_CLIENT_LOG_FILE; // Изначально дефолтное имя
    int client_final_receive_timeout_ms = DEFAULT_CLIENT_RECEIVE_TIMEOUT_MS;
    bool connection_lost_during_batch = false;

    if (argc <= 1) { 
        printClientCommandLineHelp(argv[0]);
        return 1; 
    }

    // НОВОЕ: Первый проход для --client-id и --log-file, чтобы правильно настроить имя лог-файла ДО первой инициализации логгера
    bool custom_log_file_set = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg_str(argv[i]);
        if (arg_str == "--client-id") {
            if (i + 1 < argc) {
                client_instance_id_str = argv[i+1]; // Не инкрементируем i здесь, это сделает второй проход
            }
        } else if (arg_str == "--log-file") {
            if (i + 1 < argc) {
                client_final_log_file = argv[i+1];
                custom_log_file_set = true;
            }
        }
    }
    if (client_instance_id_str != "client" && !custom_log_file_set) { // Если ID не дефолтный и файл лога не задан явно
        client_final_log_file = client_instance_id_str + "_" + DEFAULT_CLIENT_LOG_FILE;
    }
    
    std::string current_client_log_prefix = base_log_prefix + (client_instance_id_str != "client" ? ":" + client_instance_id_str : "") + "] ";
    Logger::init(client_final_log_level, client_final_log_file); // Первая/основная инициализация логгера

    // Обработка -h/--help до основного парсинга, но после инициализации логгера
    for (int i = 1; i < argc; ++i) {
        std::string arg_str(argv[i]);
        if (arg_str == "-h" || arg_str == "--help") {
            printClientCommandLineHelp(argv[0]);
            Logger::info(current_client_log_prefix + "Запрошена справка через командную строку. Завершение.");
            return 0;
        }
    }
    
    Logger::info(current_client_log_prefix + "===================================================");
    Logger::info(current_client_log_prefix + "====== КЛИЕНТ БД ИНТЕРНЕТ-ПРОВАЙДЕРА (Этап 5) ======");
    Logger::info(current_client_log_prefix + "===================================================");

    try {
        for (int i = 1; i < argc; ++i) {
            std::string arg_str(argv[i]);
            if ((arg_str == "-s" || arg_str == "--server")) {
                if (i + 1 < argc) { server_host_address = argv[++i]; } 
                else { throw std::runtime_error("Опция '" + arg_str + "' требует аргумент (адрес сервера)."); }
            } else if ((arg_str == "-p" || arg_str == "--port")) {
                 if (i + 1 < argc) { 
                    std::string port_val_str = argv[++i];
                    try { server_port_number = std::stoi(port_val_str); }
                    catch (const std::invalid_argument&) { throw std::runtime_error("Неверный формат номера порта: " + port_val_str); }
                    catch (const std::out_of_range&) { throw std::runtime_error("Номер порта вне диапазона: " + port_val_str); }
                    if (server_port_number <= 0 || server_port_number > 65535) {
                        throw std::runtime_error("Неверный номер порта: " + std::to_string(server_port_number) + ". Порт должен быть между 1-65535.");
                    }
                } else { throw std::runtime_error("Опция '" + arg_str + "' требует аргумент (номер порта)."); }
            } else if ((arg_str == "-f" || arg_str == "--file")) {
                if (i + 1 < argc) { batch_command_file_path = argv[++i]; is_interactive_mode = false; } 
                else { throw std::runtime_error("Опция '" + arg_str + "' требует аргумент (путь к файлу запросов)."); }
            } else if ((arg_str == "-o" || arg_str == "--output")) {
                if (i + 1 < argc) { batch_output_file_path = argv[++i]; } 
                else { throw std::runtime_error("Опция '" + arg_str + "' требует аргумент (путь к файлу вывода)."); }
            } else if (arg_str == "--timeout") {
                if (i + 1 < argc) { 
                    std::string timeout_val_str = argv[++i];
                    try { client_final_receive_timeout_ms = std::stoi(timeout_val_str); }
                    catch (const std::invalid_argument&) { throw std::runtime_error("Неверный формат таймаута: " + timeout_val_str); }
                    catch (const std::out_of_range&) { throw std::runtime_error("Значение таймаута вне диапазона: " + timeout_val_str); }
                    if (client_final_receive_timeout_ms < 0) {
                         Logger::warn(current_client_log_prefix + "Таймаут получения не может быть отрицательным (" + std::to_string(client_final_receive_timeout_ms) + "). Используется по умолчанию: " + std::to_string(DEFAULT_CLIENT_RECEIVE_TIMEOUT_MS) + " мс.");
                         client_final_receive_timeout_ms = DEFAULT_CLIENT_RECEIVE_TIMEOUT_MS;
                    }
                } else { throw std::runtime_error("Опция '" + arg_str + "' требует аргумент (таймаут в мс)."); }
            } else if ((arg_str == "-l" || arg_str == "--log-level")) {
                 if (i + 1 < argc) {
                    std::string level_str_val = argv[++i];
                    std::string level_val_upper = clientToUpper(level_str_val);
                    LogLevel new_level = client_final_log_level; // Сохраняем текущий на случай ошибки
                    if (level_val_upper == "DEBUG") new_level = LogLevel::DEBUG;
                    else if (level_val_upper == "INFO") new_level = LogLevel::INFO;
                    else if (level_val_upper == "WARN") new_level = LogLevel::WARN;
                    else if (level_val_upper == "ERROR") new_level = LogLevel::ERROR;
                    else if (level_val_upper == "NONE") new_level = LogLevel::NONE;
                    else { Logger::warn(current_client_log_prefix + "Указан неизвестный уровень логирования: '" + level_str_val + "'. Уровень не изменен.");}
                    if (new_level != client_final_log_level) {
                        client_final_log_level = new_level;
                        Logger::init(client_final_log_level, client_final_log_file); // Переинициализация, если уровень изменился
                        current_client_log_prefix = base_log_prefix + (client_instance_id_str != "client" ? ":" + client_instance_id_str : "") + "] "; // Обновить префикс, т.к. Logger::init его сбрасывает
                         Logger::info(current_client_log_prefix + "Уровень логирования изменен на: " + level_str_val);
                    }
                } else { throw std::runtime_error("Опция '" + arg_str + "' требует аргумент (уровень логирования: DEBUG, INFO, WARN, ERROR, NONE)."); }
            } else if (arg_str == "--log-file") { // Уже обработано для имени файла, здесь просто пропускаем значение
                if (i + 1 < argc) { ++i; } 
                else { throw std::runtime_error("Опция '" + arg_str + "' требует аргумент (путь к файлу)."); }
            } else if (arg_str == "--client-id") { 
                 if (i + 1 < argc) { ++i; } // Уже обработано, просто пропускаем значение
                 else { throw std::runtime_error("Опция '" + arg_str + "' требует аргумент (ID клиента).");}
            } else {
                if (arg_str != "-h" && arg_str != "--help") { // -h/--help уже должны были завершить программу
                    throw std::runtime_error("Неизвестная опция или ошибка аргумента: " + arg_str);
                }
            }
        }
    } catch (const std::exception& e_args) {
        Logger::error(current_client_log_prefix + "Ошибка разбора аргументов командной строки: " + e_args.what());
        std::cerr << current_client_log_prefix << "КЛИЕНТ: ОШИБКА АРГУМЕНТА: " << e_args.what() << std::endl;
        printClientCommandLineHelp(argv[0]);
        return 1;
    }
    
    if (server_host_address.empty()) {
        Logger::error(current_client_log_prefix + "Критическая ошибка: Адрес сервера (-s или --server) не указан.");
        std::cerr << current_client_log_prefix << "КЛИЕНТ: КРИТИЧЕСКАЯ ОШИБКА: Адрес сервера (-s или --server) должен быть указан." << std::endl;
        printClientCommandLineHelp(argv[0]);
        return 1;
    }
    if (!batch_output_file_path.empty() && is_interactive_mode) {
        Logger::warn(current_client_log_prefix + "Опция файла вывода (-o/--output: '" + batch_output_file_path + "') применима только в пакетном режиме (-f/--file). В интерактивном режиме она будет проигнорирована.");
        batch_output_file_path.clear(); 
    }
    if (!is_interactive_mode && batch_output_file_path.empty() && !batch_command_file_path.empty()) {
        try {
            std::filesystem::path input_fs_path(batch_command_file_path);
            std::string output_base_filename = input_fs_path.stem().string(); 
            std::string input_file_extension_str = input_fs_path.extension().string();
            // НОВОЕ: Добавляем ID клиента к имени файла вывода по умолчанию
            std::string id_prefix_for_output = (client_instance_id_str != "client" ? client_instance_id_str + "_" : "");
            if (input_fs_path.parent_path().empty()){
                 batch_output_file_path = (id_prefix_for_output + output_base_filename + ".out" + (input_file_extension_str.empty() ? ".txt" : input_file_extension_str) );
            } else {
                 batch_output_file_path = (input_fs_path.parent_path() / (id_prefix_for_output + output_base_filename + ".out" + (input_file_extension_str.empty() ? ".txt" : input_file_extension_str) )).string();
            }
        } catch (const std::exception& e_fs_path) { 
            Logger::error(current_client_log_prefix + "Ошибка создания пути к файлу вывода по умолчанию из входного '" + batch_command_file_path + "': " + e_fs_path.what());
            std::cerr << current_client_log_prefix << "КЛИЕНТ: ОШИБКА: Не удалось создать путь к файлу вывода по умолчанию. Пожалуйста, укажите файл вывода с помощью -o." << std::endl;
            return 1;
        }
    }

    Logger::info(current_client_log_prefix + "Итоговый снимок конфигурации клиента:");
    Logger::info(current_client_log_prefix + "  ID Клиента: " + client_instance_id_str);
    Logger::info(current_client_log_prefix + "  Целевой сервер: " + server_host_address + ":" + std::to_string(server_port_number));
    Logger::info(current_client_log_prefix + "  Режим работы: " + (is_interactive_mode ? "Интерактивный" : "Пакетный (Исходный файл команд: '" + batch_command_file_path + "')"));
    if (!is_interactive_mode) {
        Logger::info(current_client_log_prefix + "  Файл вывода пакетного режима: '" + batch_output_file_path + "'");
    }
    Logger::info(current_client_log_prefix + "  Настройка таймаута ответа сервера: " + std::to_string(client_final_receive_timeout_ms) + " мс");

    TCPSocket client_socket;
    Logger::info(current_client_log_prefix + "Попытка установить соединение с сервером " + server_host_address + ":" + std::to_string(server_port_number) + "...");
    if (is_interactive_mode) {
        std::cout << current_client_log_prefix << "КЛИЕНТ: Подключение к серверу " << server_host_address << ":" << server_port_number << "..." << std::endl;
    }

    if (!client_socket.connectSocket(server_host_address, server_port_number)) {
        int err_code = client_socket.getLastSocketError();
        std::string err_detail_str = "Код ошибки сокета: " + std::to_string(err_code);
        #ifdef _WIN32 
        #else
        if (err_code != 0) err_detail_str += " (" + std::string(strerror(err_code)) + ")";
        #endif
        if (is_interactive_mode) {
            std::cout << current_client_log_prefix << "КЛИЕНТ: СБОЙ ПОДКЛЮЧЕНИЯ: Не удалось подключиться к серверу " << server_host_address << ":" << server_port_number
                      << ". Убедитесь, что сервер запущен и доступен. " << err_detail_str << std::endl;
        } else { 
            std::cerr << current_client_log_prefix << "КЛИЕНТ: СБОЙ ПОДКЛЮЧЕНИЯ: Не удалось подключиться к серверу " << server_host_address << ":" << server_port_number
                      << ". " << err_detail_str << std::endl;
        }
        Logger::error(current_client_log_prefix + "Не удалось подключиться к серверу " + server_host_address + ":" + std::to_string(server_port_number) + ". " + err_detail_str);
        Logger::info(current_client_log_prefix + "========== ЗАВЕРШЕНИЕ РАБОТЫ КЛИЕНТА (Ошибка Подключения) ==========");
        return 1;
    }
    Logger::info(current_client_log_prefix + "Успешно подключен к серверу " + server_host_address + ":" + std::to_string(server_port_number));
    if (is_interactive_mode) { 
        std::cout << current_client_log_prefix << "КЛИЕНТ: Успешно подключен к серверу." << std::endl;
    }

    if (!is_interactive_mode) {
        Logger::info(current_client_log_prefix + "Работа в пакетном режиме. Файл команд: '" + batch_command_file_path + "'");
        std::ifstream command_input_file(batch_command_file_path);
        if (!command_input_file.is_open()) {
            Logger::error(current_client_log_prefix + "Не удалось открыть файл команд для чтения: \"" + batch_command_file_path + "\"");
            if(client_socket.isValid()) { client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION");} 
            client_socket.closeSocket();
            Logger::info(current_client_log_prefix + "========== ЗАВЕРШЕНИЕ РАБОТЫ КЛИЕНТА (Ошибка Файла Команд) ==========");
            std::cerr << current_client_log_prefix << "КЛИЕНТ: ОШИБКА: Не удалось открыть файл команд: " << batch_command_file_path << std::endl;
            return 1;
        }

        std::ofstream batch_output_file_stream(batch_output_file_path);
        if (!batch_output_file_stream.is_open()) {
            Logger::error(current_client_log_prefix + "Не удалось открыть файл вывода для записи: \"" + batch_output_file_path + "\"");
            std::cerr << current_client_log_prefix << "КЛИЕНТ: ОШИБКА: Не удалось открыть файл вывода: " << batch_output_file_path << std::endl;
            command_input_file.close();
            if(client_socket.isValid()) { client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION");}
            client_socket.closeSocket();
            Logger::info(current_client_log_prefix + "========== ЗАВЕРШЕНИЕ РАБОТЫ КЛИЕНТА (Ошибка Файла Вывода) ==========");
            return 1;
        }
        Logger::info(current_client_log_prefix + "Вывод пакетного режима будет записан в: '" + batch_output_file_path + "'");
        std::cout << current_client_log_prefix << "КЛИЕНТ: Пакетная обработка файла команд \"" << batch_command_file_path << "\" запущена." << std::endl;
        std::cout << current_client_log_prefix << "КЛИЕНТ: Результаты будут сохранены в: \"" << batch_output_file_path << "\"" << std::endl;

        batch_output_file_stream << "--- " << current_client_log_prefix << "КЛИЕНТ: Начата пакетная обработка. Файл команд: " << batch_command_file_path << " ---\n";
        batch_output_file_stream << "--- " << current_client_log_prefix << "КЛИЕНТ: Подключен к серверу: " << server_host_address << ":" << server_port_number << " ---\n\n";
        batch_output_file_stream.flush();

        std::string current_line_from_file;
        int query_counter = 0;
        int file_line_number = 0;
        bool client_explicitly_sent_exit = false;
        
        std::random_device rd;
        std::mt19937 gen(rd());

        while (std::getline(command_input_file, current_line_from_file)) {
            file_line_number++;
            if (!current_line_from_file.empty() && current_line_from_file.back() == '\r') {
                current_line_from_file.pop_back();
            }
            std::string trimmed_query_line = current_line_from_file;
            size_t first_char_idx = trimmed_query_line.find_first_not_of(" \t\n\r\f\v");
            if (std::string::npos == first_char_idx) { 
                trimmed_query_line.clear();
            } else {
                size_t last_char_idx = trimmed_query_line.find_last_not_of(" \t\n\r\f\v");
                trimmed_query_line = trimmed_query_line.substr(first_char_idx, (last_char_idx - first_char_idx + 1));
            }

            if (trimmed_query_line.empty() || (!trimmed_query_line.empty() && trimmed_query_line[0] == '#')) {
                continue;
            }

            std::string upper_line_check = clientToUpper(trimmed_query_line);
            bool is_delay_command = false;
            if (upper_line_check.rfind("DELAY_MS ", 0) == 0) {
                is_delay_command = true;
                std::string value_str = trimmed_query_line.substr(std::string("DELAY_MS ").length());
                try {
                    int delay_val_ms = std::stoi(value_str);
                    if (delay_val_ms >= 0) { // Разрешаем нулевую задержку
                        Logger::info(current_client_log_prefix + "Выполнение задержки: DELAY_MS " + std::to_string(delay_val_ms));
                        batch_output_file_stream << "[" << get_current_timestamp_for_output() << "] " << current_client_log_prefix << "CMD: DELAY_MS " << delay_val_ms << " ms\n";
                        if (delay_val_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay_val_ms));
                    } else {
                        Logger::warn(current_client_log_prefix + "Отрицательное значение для DELAY_MS проигнорировано: " + value_str);
                        batch_output_file_stream << "[" << get_current_timestamp_for_output() << "] " << current_client_log_prefix << "CMD_WARN: Отрицательное значение DELAY_MS: " << trimmed_query_line << "\n";
                    }
                } catch (const std::exception& e_delay) {
                    Logger::warn(current_client_log_prefix + "Ошибка парсинга DELAY_MS '" + value_str + "': " + e_delay.what());
                    batch_output_file_stream << "[" << get_current_timestamp_for_output() << "] " << current_client_log_prefix << "CMD_ERROR: Ошибка парсинга DELAY_MS: " << trimmed_query_line << "\n";
                }
            } else if (upper_line_check.rfind("DELAY_RANDOM_MS ", 0) == 0) {
                 is_delay_command = true;
                 std::istringstream iss_delay_random(trimmed_query_line.substr(std::string("DELAY_RANDOM_MS ").length()));
                 int min_ms, max_ms;
                 if (iss_delay_random >> min_ms >> max_ms) {
                     if (min_ms >= 0 && max_ms >= min_ms) {
                         std::uniform_int_distribution<> distrib(min_ms, max_ms);
                         int delay_val_ms = distrib(gen);
                         Logger::info(current_client_log_prefix + "Выполнение задержки: DELAY_RANDOM_MS " + std::to_string(min_ms) + "-" + std::to_string(max_ms) + " -> " + std::to_string(delay_val_ms) + " ms");
                         batch_output_file_stream << "[" << get_current_timestamp_for_output() << "] " << current_client_log_prefix << "CMD: DELAY_RANDOM_MS (" << min_ms << "-" << max_ms << "), результат: " << delay_val_ms << " ms\n";
                         if (delay_val_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay_val_ms));
                     } else {
                         Logger::warn(current_client_log_prefix + "Некорректные параметры для DELAY_RANDOM_MS: " + trimmed_query_line);
                         batch_output_file_stream << "[" << get_current_timestamp_for_output() << "] " << current_client_log_prefix << "CMD_WARN: Некорректные параметры DELAY_RANDOM_MS: " << trimmed_query_line << "\n";
                     }
                 } else {
                     Logger::warn(current_client_log_prefix + "Ошибка парсинга параметров DELAY_RANDOM_MS: " + trimmed_query_line);
                     batch_output_file_stream << "[" << get_current_timestamp_for_output() << "] " << current_client_log_prefix << "CMD_ERROR: Ошибка парсинга DELAY_RANDOM_MS: " << trimmed_query_line << "\n";
                 }
            }

            if (is_delay_command) {
                continue; 
            }
            
            query_counter++;

            batch_output_file_stream << "[" << get_current_timestamp_for_output() << "] " << current_client_log_prefix << "ЗАПРОС #" << query_counter << " (из строки файла #" << file_line_number << "): " << trimmed_query_line << "\n";
            batch_output_file_stream << "----------------------------------------\n"; 
            batch_output_file_stream.flush();

            if (!process_single_request_to_server(client_socket, trimmed_query_line, batch_output_file_stream, current_client_log_prefix, client_final_receive_timeout_ms)) {
                Logger::error(current_client_log_prefix + "Фатальная ошибка при обработке запроса (строка файла #" + std::to_string(file_line_number) + "): \"" + trimmed_query_line + "\". Прерывание пакетной обработки файла.");
                batch_output_file_stream << "\n[" << get_current_timestamp_for_output() << "] " << current_client_log_prefix << "КЛИЕНТ: КРИТИЧЕСКАЯ ОШИБКА: Потеряно соединение с сервером или запрос не может быть обработан. Пакетная обработка прервана.\n";
                connection_lost_during_batch = true;
                break; 
            }
            batch_output_file_stream << "[" << get_current_timestamp_for_output() << "] " << current_client_log_prefix << "ОТВЕТ ПОЛУЧЕН для запроса #" << query_counter << "\n";
            batch_output_file_stream << "----------------------------------------\n\n"; 
            batch_output_file_stream.flush();

             if (clientToUpper(trimmed_query_line) == "EXIT") {
                Logger::info(current_client_log_prefix + "Команда EXIT найдена в файле команд (строка #" + std::to_string(file_line_number) + "). Завершение пакетной обработки и сеанса клиента.");
                client_explicitly_sent_exit = true;
                break; 
            }
        }
        command_input_file.close();

        batch_output_file_stream << "--- " << current_client_log_prefix << "КЛИЕНТ: Пакетная обработка для файла завершена: " << batch_command_file_path << " ---";
        if (connection_lost_during_batch) {
             batch_output_file_stream << " (Обработка была прервана из-за ошибки)";
        }
        batch_output_file_stream << std::endl;
        batch_output_file_stream.close();

        std::cout << current_client_log_prefix << "КЛИЕНТ: Пакетная обработка файла команд \"" << batch_command_file_path << "\" завершена. "
                  << "Всего обработано команд: " << query_counter << "."
                  << " Результаты сохранены в: \"" << batch_output_file_path << "\"" << std::endl;
        
        if (!client_explicitly_sent_exit && !connection_lost_during_batch && client_socket.isValid()) {
             Logger::info(current_client_log_prefix + "Отправка EXIT_CLIENT_SESSION на сервер после завершения пакетной обработки файла (команда EXIT не была в файле).");
             if (!client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION")) { 
                 Logger::warn(current_client_log_prefix + "Не удалось отправить EXIT_CLIENT_SESSION на сервер после пакетной обработки. Ошибка сокета: " + std::to_string(client_socket.getLastSocketError()));
             }
        }

    } else {
        Logger::info(current_client_log_prefix + "Вход в интерактивный режим с сервером.");
        std::cout << "\n" << current_client_log_prefix << "Клиент в интерактивном режиме. Подключен к " << server_host_address << ":" << server_port_number << ".\n";
        std::cout << current_client_log_prefix << "Введите 'HELP' для списка команд, 'EXIT' для завершения сессии с сервером, или 'QUIT_CLIENT' для выхода из программы клиента.\n";
        
        std::string user_input_line;
        bool current_session_active = true;
        while (current_session_active && client_socket.isValid()) { 
            std::cout << current_client_log_prefix << "[" << server_host_address << ":" << server_port_number << "] > "; // ИЗМЕНЕНО
            std::cout.flush(); 
            if (!std::getline(std::cin, user_input_line)) {
                if (std::cin.eof()) { 
                     Logger::info(current_client_log_prefix + "Обнаружен EOF в интерактивном режиме. Отправка EXIT_CLIENT_SESSION на сервер.");
                     std::cout << "\n" << current_client_log_prefix << "КЛИЕНТ: Обнаружен EOF (конец ввода). Завершение сессии с сервером..." << std::endl; // ИЗМЕНЕНО
                     if (client_socket.isValid()) { 
                         if(!client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION")){
                             Logger::warn(current_client_log_prefix + "Не удалось отправить EXIT_CLIENT_SESSION на сервер при EOF. Ошибка сокета: " + std::to_string(client_socket.getLastSocketError()));
                         }
                     }
                } else { 
                    Logger::error(current_client_log_prefix + "Критическая ошибка std::cin в интерактивном режиме (не EOF). Завершение.");
                    std::cout << current_client_log_prefix << "КЛИЕНТ: КРИТИЧЕСКАЯ ОШИБКА ВВОДА. Завершение." << std::endl; // ИЗМЕНЕНО
                     if (client_socket.isValid()) {client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION");}
                }
                current_session_active = false; 
                break;
            }

            if (user_input_line.empty()) { 
                continue;
            }

            std::string command_to_process = user_input_line; 
            std::string upper_case_input = clientToUpper(command_to_process);

            if (upper_case_input == "QUIT_CLIENT") {
                Logger::info(current_client_log_prefix + "Получена локальная команда QUIT_CLIENT. Завершение клиента и сессии с сервером.");
                std::cout << current_client_log_prefix << "КЛИЕНТ: Выход из программы по команде QUIT_CLIENT..." << std::endl; // ИЗМЕНЕНО
                if (client_socket.isValid()) { 
                    if(!client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION")){
                        Logger::warn(current_client_log_prefix + "Не удалось отправить EXIT_CLIENT_SESSION на сервер во время QUIT_CLIENT. Ошибка сокета: " + std::to_string(client_socket.getLastSocketError()));
                    }
                }
                current_session_active = false; 
                break;
            }

            if (upper_case_input == "HELP") { 
                // ... (существующий HELP, можно добавить префикс client_id_log_prefix если нужно)
                std::cout << "\n" << current_client_log_prefix << "КЛИЕНТ: Локальная команда HELP:\n"; // ИЗМЕНЕНО
                // ...
                std::cout << "----------------------------------------" << std::endl;
                continue; 
            }
            
            // НОВОЕ: Добавление временной метки и ID клиента перед отправкой запроса в интерактивном режиме (для вывода в консоль)
            std::cout << "[" << get_current_timestamp_for_output() << "] " << current_client_log_prefix << "ЗАПРОС: " << command_to_process << std::endl;
            std::cout << "----------------------------------------" << std::endl;


            if (!process_single_request_to_server(client_socket, command_to_process, std::cout, current_client_log_prefix, client_final_receive_timeout_ms)) { // ИЗМЕНЕНО
                 Logger::error(current_client_log_prefix + "Сессия с сервером, вероятно, завершена из-за ошибки отправки/получения или проблемы на стороне сервера.");
                 current_session_active = false; 
                 break; 
            }
            // НОВОЕ: Добавление временной метки и ID клиента после получения ответа в интерактивном режиме (для вывода в консоль)
            std::cout << "[" << get_current_timestamp_for_output() << "] " << current_client_log_prefix << "ОТВЕТ ПОЛУЧЕН." << std::endl;
            // Разделитель уже выводится из process_single_request_to_server для stdout

            if (upper_case_input == "EXIT") { 
                Logger::info(current_client_log_prefix + "Команда EXIT отправлена на сервер, ответ получен. Клиент завершает сессию.");
                current_session_active = false; 
                break;
            }
        } 
    } 

    if (client_socket.isValid()) {
        Logger::debug(current_client_log_prefix + "Операции клиента завершены. Гарантируется закрытие клиентского сокета.");
        client_socket.closeSocket();
    }

    Logger::info(current_client_log_prefix + "========== ПРОГРАММА КЛИЕНТА ЗАВЕРШЕНА ==========");
    if (is_interactive_mode || (!is_interactive_mode && !connection_lost_during_batch)) { 
        std::cout << current_client_log_prefix << "КЛИЕНТ: Отключен От Сервера. Программа Завершена." << std::endl;
    }
    return 0;
}
#endif // UNIT_TESTING
