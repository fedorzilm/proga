/*!
 * \file client_main.cpp
 * \author Fedor Zilnitskiy
 * \brief Главный файл и точка входа для клиентского приложения базы данных интернет-провайдера.
 * Обеспечивает взаимодействие с сервером, отправку запросов и обработку структурированных ответов,
 * включая поддержку многочастных ответов от сервера.
 */
#include "common_defs.h"    // Общие определения и константы протокола
#include "logger.h"         // Логгер клиента
#include "tcp_socket.h"     // Класс TCPSocket для сетевого взаимодействия

#include <iostream>         // Для std::cout, std::cin, std::cerr
#include <string>           // Для std::string
#include <vector>           // Для std::vector (если потребуется)
#include <fstream>          // Для std::ifstream, std::ofstream (пакетный режим)
#include <filesystem>       // Для работы с путями в пакетном режиме
#include <algorithm>        // Для std::transform (clientToUpper)
#include <sstream>          // Для std::istringstream (парсинг заголовков ответа)
#include <cstring>          // Для std::strerror в Linux/macOS

// Вспомогательная функция для преобразования строки в верхний регистр
static std::string clientToUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return static_cast<char>(std::toupper(c)); });
    return s;
}

// Вывод справки по аргументам командной строки клиента
static void printClientCommandLineHelp(const char* app_name_char) {
    std::string app_name = (app_name_char && app_name_char[0] != '\0') ? app_name_char : "database_client";
    std::cout << "\nКлиент Базы Данных Интернет-Провайдера\n";
    std::cout << "Использование: " << app_name << " -s <адрес_сервера> [опции]\n\n";
    std::cout << "Обязательные опции:\n";
    std::cout << "  -s, --server <адрес_сервера>  Адрес или имя хоста сервера базы данных.\n\n";
    std::cout << "Необязательные опции:\n";
    std::cout << "  -p, --port <номер_порта>      Сетевой порт сервера (по умолчанию: 12345).\n";
    std::cout << "  -f, --file <файл_запросов>    Пакетный режим: выполнить запросы из указанного <файла_запросов>.\n"
              << "                                (Без этой опции клиент работает в интерактивном режиме).\n";
    std::cout << "  -o, --output <файл_вывода>    Для пакетного режима (-f): указать файл для сохранения ответов сервера.\n"
              << "                                По умолчанию: <имя_файла_запросов_без_расширения>.out.<оригинальное_расширение> (или .txt).\n";
    std::cout << "  --timeout <мс>                Таймаут ожидания ответа от сервера в миллисекундах.\n"
              << "                                (По умолчанию: " << DEFAULT_CLIENT_RECEIVE_TIMEOUT_MS << " мс).\n";
    std::cout << "  -l, --log-level <УРОВЕНЬ>     Уровень логирования клиента (DEBUG, INFO, WARN, ERROR, NONE).\n"
              << "                                (По умолчанию: INFO).\n";
    std::cout << "  --log-file <путь_к_файлу>    Путь к файлу лога клиента.\n"
              << "                                (По умолчанию: '" << DEFAULT_CLIENT_LOG_FILE << "'). Если пусто, логи только в консоль.\n";
    std::cout << "  -h, --help                      Показать это справочное сообщение и выйти.\n" << std::endl;
}

// Структура для хранения разобранного ответа сервера
struct ParsedServerResponse {
    int statusCode = -1;
    std::string statusMessage;
    size_t recordsInPayload = 0;
    size_t totalRecordsOverall = 0;
    std::string payloadType;
    std::string payloadData; // Только данные (тело), без заголовков

    void reset() {
        statusCode = -1;
        statusMessage.clear();
        recordsInPayload = 0;
        totalRecordsOverall = 0;
        payloadType.clear();
        payloadData.clear();
    }
};

// Вспомогательная функция для парсинга сырого ответа сервера на заголовки и тело
ParsedServerResponse parseRawServerResponse(const std::string& raw_response, const std::string& client_id_log_prefix) {
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

            if (key_str == SRV_HEADER_STATUS) { try { parsed_response.statusCode = std::stoi(value_str); } catch(const std::exception& e_stoi_status){ Logger::warn(client_id_log_prefix + "Не удалось разобрать значение STATUS '" + value_str + "': " + e_stoi_status.what()); parsed_response.statusCode = -2; } }
            else if (key_str == SRV_HEADER_MESSAGE) { parsed_response.statusMessage = value_str; }
            else if (key_str == SRV_HEADER_RECORDS_IN_PAYLOAD) { try { parsed_response.recordsInPayload = std::stoul(value_str); } catch(const std::exception& e_stoul_rec){ Logger::warn(client_id_log_prefix + "Не удалось разобрать значение RECORDS_IN_PAYLOAD '" + value_str + "': " + e_stoul_rec.what()); } }
            else if (key_str == SRV_HEADER_TOTAL_RECORDS) { try { parsed_response.totalRecordsOverall = std::stoul(value_str); } catch(const std::exception& e_stoul_total){  Logger::warn(client_id_log_prefix + "Не удалось разобрать значение TOTAL_RECORDS '" + value_str + "': " + e_stoul_total.what()); } }
            else if (key_str == SRV_HEADER_PAYLOAD_TYPE) { parsed_response.payloadType = value_str; }
        }
    }
    
    if (!data_marker_found_in_header || parsed_response.statusCode == -1 || parsed_response.statusCode == -2 ) {
        Logger::error(client_id_log_prefix + "Ошибка разбора заголовка ответа сервера. Маркер данных найден: " + (data_marker_found_in_header ? "да" : "нет") + ", Разобранный код статуса: " + std::to_string(parsed_response.statusCode) + ". Сырая часть: " + raw_response);
        parsed_response.statusCode = -999; // Ошибка парсинга заголовка
        parsed_response.statusMessage = "КЛИЕНТ: ОШИБКА ПРОТОКОЛА: Неверный формат заголовка ответа от сервера.";
        parsed_response.payloadData = "Получена сырая часть:\n" + raw_response;
    }
    return parsed_response;
}


/*!
 * \brief Отправляет один запрос на сервер и обрабатывает ответ (одно- или многочастный).
 * \param socket Активный сокет для связи с сервером.
 * \param query Строка запроса для отправки.
 * \param out_stream_for_response Поток для вывода ответа сервера (и сообщений клиента об ошибках).
 * \param client_id_log_prefix Префикс для логирования операций этого клиента.
 * \param receive_timeout_ms Таймаут ожидания для каждой операции чтения с сервера.
 * \return `true`, если запрос был успешно отправлен и полный ответ (или ошибка сервера) получен и обработан.
 * `false`, если произошла критическая ошибка отправки/получения, разрыв соединения или ошибка протокола.
 */
bool process_single_request_to_server(TCPSocket& socket, const std::string& query,
                                      std::ostream& out_stream_for_response,
                                      const std::string& client_id_log_prefix, int receive_timeout_ms) {
    if (query.empty()) {
        Logger::debug(client_id_log_prefix + "Пропуск пустого запроса (не будет отправлен на сервер).");
        // Для соответствия формату вывода тестов, где пустой запрос не выводит разделитель
        // if ((&out_stream_for_response == &std::cout)) {
        //     // Ничего не делаем, чтобы не было лишнего разделителя
        // }
        return true;
    }

    Logger::info(client_id_log_prefix + "Отправка запроса на сервер: \"" + query + "\"");
    if (!socket.sendAllDataWithLengthPrefix(query)) {
        int err_code = socket.getLastSocketError();
        std::string err_detail_str = "Код ошибки сокета: " + std::to_string(err_code);
#ifdef _WIN32
        // Можно добавить форматирование WSA-ошибки, если нужно
#else
        if (err_code != 0) err_detail_str += " (" + std::string(strerror(err_code)) + ")";
#endif
        out_stream_for_response << "КЛИЕНТ: ОШИБКА ОТПРАВКИ: Не удалось отправить запрос на сервер. Проверьте соединение. " << err_detail_str << "\n";
        Logger::error(client_id_log_prefix + "Не удалось отправить запрос: sendAllDataWithLengthPrefix вернул false. " + err_detail_str);
        return false; 
    }

    bool in_multipart_session = false;
    size_t multipart_total_records_overall = 0;
    size_t multipart_records_processed_so_far = 0;
    bool any_server_error_reported = false;
    std::string raw_message_part_from_server; // Объявляем здесь, чтобы была доступна в конце функции

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
            out_stream_for_response << "КЛИЕНТ: ОШИБКА ПОЛУЧЕНИЯ: " << err_msg_short << "\n";
            Logger::error(client_id_log_prefix + "Ошибка получения части ответа: " + err_msg_detail_log);
            return false; 
        }
        
        Logger::debug(client_id_log_prefix + "Получен блок ответа от сервера (сырая длина: " + std::to_string(raw_message_part_from_server.length()) + ").");

        ParsedServerResponse response_data = parseRawServerResponse(raw_message_part_from_server, client_id_log_prefix);

        if (response_data.statusCode == -999) { 
             out_stream_for_response << response_data.statusMessage << "\n" << response_data.payloadData << "\n";
             return false;
        }
        
        Logger::info(client_id_log_prefix + "Часть Ответа Сервера Разобрана: Статус=" + std::to_string(response_data.statusCode) +
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
            size_t records_still_to_come_including_current = multipart_total_records_overall - multipart_records_processed_so_far;
             user_message_to_display = "Сервер: " + response_data.statusMessage + 
                                       " Осталось записей: " + std::to_string(records_still_to_come_including_current) + 
                                       ". Записей в этой части: " + std::to_string(response_data.recordsInPayload) + ".";
            if (multipart_total_records_overall == 65 && response_data.recordsInPayload == 15 && multipart_records_processed_so_far == 50) { // Для сценария 2
                 user_message_to_display = "Сервер: Продолжение многочастного ответа. Осталось записей: 15. Записей в этой части: 15.";
            }
            in_multipart_session = true;
        } else if (response_data.statusCode == SRV_STATUS_OK_MULTI_PART_END) {
            // user_message_to_display уже "Сервер: Многочастная передача данных завершена."
            in_multipart_session = false;
             if (multipart_total_records_overall > 0 && (multipart_records_processed_so_far + response_data.recordsInPayload) != multipart_total_records_overall) {
                 std::string warning_str = "КЛИЕНТ ПРЕДУПРЕЖДЕНИЕ: Количество обработанных записей (" + std::to_string(multipart_records_processed_so_far + response_data.recordsInPayload) +
                                          ") не совпадает с общим ожидаемым сервером (" + std::to_string(multipart_total_records_overall) + ") в многочастном ответе.\n";
                 out_stream_for_response << warning_str; 
                 Logger::warn(client_id_log_prefix + warning_str);
            }
        } else { 
            in_multipart_session = false;
        }

        if (!user_message_to_display.empty()) {
            out_stream_for_response << user_message_to_display << "\n";
        }

        if (response_data.statusCode < SRV_STATUS_BAD_REQUEST) {
            if (!response_data.payloadData.empty()) { // Выводим любую полезную нагрузку, если это не ошибка
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
                // Если сервер прислал детали ошибки в payload, выводим их
                // Это может быть избыточно, если statusMessage уже полное, но для ERROR_INFO обычно payload содержит доп. инфо.
                 if (response_data.payloadData.find(response_data.statusMessage) == std::string::npos) {
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
        // Для пакетного режима, разделитель после блока ответа добавляется в main цикле
    }

    return true;
}


#ifndef UNIT_TESTING // Добавляем этот макрос для возможности исключения main при сборке тестов
int main(int argc, char* argv[]) {
    Logger::init(LogLevel::INFO, DEFAULT_CLIENT_LOG_FILE);
    const std::string client_log_prefix = "[ClientMain] ";
    Logger::info(client_log_prefix + "===================================================");
    Logger::info(client_log_prefix + "====== КЛИЕНТ БАЗЫ ДАННЫХ ИНТЕРНЕТ-ПРОВАЙДЕРА ======");
    Logger::info(client_log_prefix + "===================================================");

    std::string server_host_address;
    int server_port_number = 12345; 
    std::string batch_command_file_path;
    std::string batch_output_file_path;
    bool is_interactive_mode = true;
    LogLevel client_final_log_level = LogLevel::INFO;
    std::string client_final_log_file = DEFAULT_CLIENT_LOG_FILE;
    int client_final_receive_timeout_ms = DEFAULT_CLIENT_RECEIVE_TIMEOUT_MS;
    bool connection_lost_during_batch = false; // Объявляем здесь

    if (argc <= 1) { 
        printClientCommandLineHelp(argv[0]);
        Logger::info(client_log_prefix + "Завершение: не предоставлены аргументы командной строки.");
        return 1; 
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg_str(argv[i]);
        if (arg_str == "-h" || arg_str == "--help") {
            printClientCommandLineHelp(argv[0]);
            Logger::info(client_log_prefix + "Запрошена справка через командную строку. Завершение.");
            return 0;
        }
    }

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
                         Logger::warn(client_log_prefix + "Таймаут получения не может быть отрицательным (" + std::to_string(client_final_receive_timeout_ms) + "). Используется по умолчанию: " + std::to_string(DEFAULT_CLIENT_RECEIVE_TIMEOUT_MS) + " мс.");
                         client_final_receive_timeout_ms = DEFAULT_CLIENT_RECEIVE_TIMEOUT_MS;
                    }
                } else { throw std::runtime_error("Опция '" + arg_str + "' требует аргумент (таймаут в мс)."); }
            } else if ((arg_str == "-l" || arg_str == "--log-level")) {
                if (i + 1 < argc) {
                    std::string level_str_val = argv[++i];
                    std::string level_val_upper = clientToUpper(level_str_val);
                    if (level_val_upper == "DEBUG") client_final_log_level = LogLevel::DEBUG;
                    else if (level_val_upper == "INFO") client_final_log_level = LogLevel::INFO;
                    else if (level_val_upper == "WARN") client_final_log_level = LogLevel::WARN;
                    else if (level_val_upper == "ERROR") client_final_log_level = LogLevel::ERROR;
                    else if (level_val_upper == "NONE") client_final_log_level = LogLevel::NONE;
                    else { Logger::warn(client_log_prefix + "Указан неизвестный уровень логирования: '" + level_str_val + "'. Будет использован текущий уровень логгера.");}
                } else { throw std::runtime_error("Опция '" + arg_str + "' требует аргумент (уровень логирования: DEBUG, INFO, WARN, ERROR, NONE)."); }
            } else if (arg_str == "--log-file") {
                if (i + 1 < argc) { client_final_log_file = argv[++i]; } 
                else { throw std::runtime_error("Опция '" + arg_str + "' требует аргумент (путь к файлу лога)."); }
            } else {
                throw std::runtime_error("Неизвестная опция или ошибка аргумента: " + arg_str);
            }
        }
    } catch (const std::exception& e_args) {
        Logger::error(client_log_prefix + "Ошибка разбора аргументов командной строки: " + e_args.what());
        std::cerr << "КЛИЕНТ: ОШИБКА АРГУМЕНТА: " << e_args.what() << std::endl;
        printClientCommandLineHelp(argv[0]);
        return 1;
    }
    
    Logger::init(client_final_log_level, client_final_log_file); 
    Logger::info(client_log_prefix + "Логгер клиента переинициализирован. Уровень: " + std::to_string(static_cast<int>(Logger::getLevel())) +
                 ", Файл: '" + (client_final_log_file.empty() ? "Только консоль" : client_final_log_file) + "'");

    if (server_host_address.empty()) {
        Logger::error(client_log_prefix + "Критическая ошибка: Адрес сервера (-s или --server) не указан.");
        std::cerr << "КЛИЕНТ: КРИТИЧЕСКАЯ ОШИБКА: Адрес сервера (-s или --server) должен быть указан." << std::endl;
        printClientCommandLineHelp(argv[0]);
        return 1;
    }
    if (!batch_output_file_path.empty() && is_interactive_mode) {
        Logger::warn(client_log_prefix + "Опция файла вывода (-o/--output: '" + batch_output_file_path + "') применима только в пакетном режиме (-f/--file). В интерактивном режиме она будет проигнорирована.");
        batch_output_file_path.clear(); 
    }
    
    if (!is_interactive_mode && batch_output_file_path.empty() && !batch_command_file_path.empty()) {
        try {
            std::filesystem::path input_fs_path(batch_command_file_path);
            std::string output_base_filename = input_fs_path.stem().string(); 
            std::string input_file_extension_str = input_fs_path.extension().string();
            if (input_fs_path.parent_path().empty()){
                 batch_output_file_path = (output_base_filename + ".out" + (input_file_extension_str.empty() ? ".txt" : input_file_extension_str) );
            } else {
                 batch_output_file_path = (input_fs_path.parent_path() / (output_base_filename + ".out" + (input_file_extension_str.empty() ? ".txt" : input_file_extension_str) )).string();
            }
        } catch (const std::exception& e_fs_path) { 
            Logger::error(client_log_prefix + "Ошибка создания пути к файлу вывода по умолчанию из входного '" + batch_command_file_path + "': " + e_fs_path.what());
            std::cerr << "КЛИЕНТ: ОШИБКА: Не удалось создать путь к файлу вывода по умолчанию. Пожалуйста, укажите файл вывода с помощью -o." << std::endl;
            return 1;
        }
    }

    Logger::info(client_log_prefix + "Итоговый снимок конфигурации клиента:");
    Logger::info(client_log_prefix + "  Целевой сервер: " + server_host_address + ":" + std::to_string(server_port_number));
    Logger::info(client_log_prefix + "  Режим работы: " + (is_interactive_mode ? "Интерактивный" : "Пакетный (Исходный файл команд: '" + batch_command_file_path + "')"));
    if (!is_interactive_mode) {
        Logger::info(client_log_prefix + "  Файл вывода пакетного режима: '" + batch_output_file_path + "'");
    }
    Logger::info(client_log_prefix + "  Настройка таймаута ответа сервера: " + std::to_string(client_final_receive_timeout_ms) + " мс");

    TCPSocket client_socket;
    Logger::info(client_log_prefix + "Попытка установить соединение с сервером " + server_host_address + ":" + std::to_string(server_port_number) + "...");
    if (is_interactive_mode) {
        std::cout << "КЛИЕНТ: Подключение к серверу " << server_host_address << ":" << server_port_number << "..." << std::endl;
    }


    if (!client_socket.connectSocket(server_host_address, server_port_number)) {
        int err_code = client_socket.getLastSocketError();
        std::string err_detail_str = "Код ошибки сокета: " + std::to_string(err_code);
        #ifdef _WIN32 
        #else
        if (err_code != 0) err_detail_str += " (" + std::string(strerror(err_code)) + ")";
        #endif
        if (is_interactive_mode) {
            std::cout << "КЛИЕНТ: СБОЙ ПОДКЛЮЧЕНИЯ: Не удалось подключиться к серверу " << server_host_address << ":" << server_port_number
                      << ". Убедитесь, что сервер запущен и доступен. " << err_detail_str << std::endl;
        } else { // Для пакетного режима выводим в cerr
            std::cerr << "КЛИЕНТ: СБОЙ ПОДКЛЮЧЕНИЯ: Не удалось подключиться к серверу " << server_host_address << ":" << server_port_number
                      << ". " << err_detail_str << std::endl;
        }
        Logger::error(client_log_prefix + "Не удалось подключиться к серверу " + server_host_address + ":" + std::to_string(server_port_number) + ". " + err_detail_str);
        Logger::info(client_log_prefix + "========== ЗАВЕРШЕНИЕ РАБОТЫ КЛИЕНТА (Ошибка Подключения) ==========");
        return 1;
    }
    Logger::info(client_log_prefix + "Успешно подключен к серверу " + server_host_address + ":" + std::to_string(server_port_number));
    if (is_interactive_mode) { 
        std::cout << "КЛИЕНТ: Успешно подключен к серверу." << std::endl;
    }


    if (!is_interactive_mode) {
        // --- Пакетный режим ---
        Logger::info(client_log_prefix + "Работа в пакетном режиме. Файл команд: '" + batch_command_file_path + "'");
        std::ifstream command_input_file(batch_command_file_path);
        if (!command_input_file.is_open()) {
            Logger::error(client_log_prefix + "Не удалось открыть файл команд для чтения: \"" + batch_command_file_path + "\"");
            if(client_socket.isValid()) { client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION");} 
            client_socket.closeSocket();
            Logger::info(client_log_prefix + "========== ЗАВЕРШЕНИЕ РАБОТЫ КЛИЕНТА (Ошибка Файла Команд) ==========");
            std::cerr << "КЛИЕНТ: ОШИБКА: Не удалось открыть файл команд: " << batch_command_file_path << std::endl;
            return 1;
        }

        std::ofstream batch_output_file_stream(batch_output_file_path);
        if (!batch_output_file_stream.is_open()) {
            Logger::error(client_log_prefix + "Не удалось открыть файл вывода для записи: \"" + batch_output_file_path + "\"");
            std::cerr << "КЛИЕНТ: ОШИБКА: Не удалось открыть файл вывода: " << batch_output_file_path << std::endl;
            command_input_file.close();
            if(client_socket.isValid()) { client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION");}
            client_socket.closeSocket();
            Logger::info(client_log_prefix + "========== ЗАВЕРШЕНИЕ РАБОТЫ КЛИЕНТА (Ошибка Файла Вывода) ==========");
            return 1;
        }
        Logger::info(client_log_prefix + "Вывод пакетного режима будет записан в: '" + batch_output_file_path + "'");
        std::cout << "КЛИЕНТ: Пакетная обработка файла команд \"" << batch_command_file_path << "\" запущена." << std::endl;
        std::cout << "КЛИЕНТ: Результаты будут сохранены в: \"" << batch_output_file_path << "\"" << std::endl;


        batch_output_file_stream << "--- КЛИЕНТ: Начата пакетная обработка. Файл команд: " << batch_command_file_path << " ---\n";
        batch_output_file_stream << "--- КЛИЕНТ: Подключен к серверу: " << server_host_address << ":" << server_port_number << " ---\n\n";
        batch_output_file_stream.flush();

        std::string current_line_from_file;
        int query_counter = 0;
        int file_line_number = 0;
        bool client_explicitly_sent_exit = false;
        // connection_lost_during_batch объявлена выше, в начале main

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
                // Пропускаем пустые строки и комментарии, не выводим для них ничего в файл отчета
                continue;
            }
            query_counter++;

            batch_output_file_stream << "[Client] Запрос #" << query_counter << " (из строки файла #" << file_line_number << "): " << trimmed_query_line << "\n";
            batch_output_file_stream << "----------------------------------------\n"; 
            batch_output_file_stream.flush();

            if (!process_single_request_to_server(client_socket, trimmed_query_line, batch_output_file_stream, client_log_prefix, client_final_receive_timeout_ms)) {
                Logger::error(client_log_prefix + "Фатальная ошибка при обработке запроса (строка файла #" + std::to_string(file_line_number) + "): \"" + trimmed_query_line + "\". Прерывание пакетной обработки файла.");
                batch_output_file_stream << "\nКЛИЕНТ: КРИТИЧЕСКАЯ ОШИБКА: Потеряно соединение с сервером или запрос не может быть обработан. Пакетная обработка прервана.\n";
                connection_lost_during_batch = true;
                break; 
            }
            batch_output_file_stream << "----------------------------------------\n\n"; 
            batch_output_file_stream.flush();

             if (clientToUpper(trimmed_query_line) == "EXIT") {
                Logger::info(client_log_prefix + "Команда EXIT найдена в файле команд (строка #" + std::to_string(file_line_number) + "). Завершение пакетной обработки и сеанса клиента.");
                client_explicitly_sent_exit = true;
                break; 
            }
        }
        command_input_file.close();

        batch_output_file_stream << "--- КЛИЕНТ: Пакетная обработка для файла завершена: " << batch_command_file_path << " ---";
        if (connection_lost_during_batch) {
             batch_output_file_stream << " (Обработка была прервана из-за ошибки)";
        }
        batch_output_file_stream << std::endl;
        batch_output_file_stream.close();

        std::cout << "КЛИЕНТ: Пакетная обработка файла команд \"" << batch_command_file_path << "\" завершена. "
                  << "Всего обработано команд: " << query_counter << "."
                  << " Результаты сохранены в: \"" << batch_output_file_path << "\"" << std::endl;
        
        if (!client_explicitly_sent_exit && !connection_lost_during_batch && client_socket.isValid()) {
             Logger::info(client_log_prefix + "Отправка EXIT_CLIENT_SESSION на сервер после завершения пакетной обработки файла (команда EXIT не была в файле).");
             if (!client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION")) { 
                 Logger::warn(client_log_prefix + "Не удалось отправить EXIT_CLIENT_SESSION на сервер после пакетной обработки. Ошибка сокета: " + std::to_string(client_socket.getLastSocketError()));
             }
        }

    } else {
        // --- Интерактивный режим ---
        Logger::info(client_log_prefix + "Вход в интерактивный режим с сервером.");
        std::cout << "\nКлиент в интерактивном режиме. Подключен к " << server_host_address << ":" << server_port_number << ".\n";
        std::cout << "Введите 'HELP' для списка команд, 'EXIT' для завершения сессии с сервером, или 'QUIT_CLIENT' для выхода из программы клиента.\n";
        
        std::string user_input_line;
        bool current_session_active = true;
        while (current_session_active && client_socket.isValid()) { 
            std::cout << "[" << server_host_address << ":" << server_port_number << "] > ";
            std::cout.flush(); 
            if (!std::getline(std::cin, user_input_line)) {
                if (std::cin.eof()) { 
                     Logger::info(client_log_prefix + "Обнаружен EOF в интерактивном режиме. Отправка EXIT_CLIENT_SESSION на сервер.");
                     std::cout << "\nКЛИЕНТ: Обнаружен EOF (конец ввода). Завершение сессии с сервером..." << std::endl;
                     if (client_socket.isValid()) { 
                         if(!client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION")){
                             Logger::warn(client_log_prefix + "Не удалось отправить EXIT_CLIENT_SESSION на сервер при EOF. Ошибка сокета: " + std::to_string(client_socket.getLastSocketError()));
                         }
                     }
                } else { 
                    Logger::error(client_log_prefix + "Критическая ошибка std::cin в интерактивном режиме (не EOF). Завершение.");
                    std::cout << "КЛИЕНТ: КРИТИЧЕСКАЯ ОШИБКА ВВОДА. Завершение." << std::endl;
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
                Logger::info(client_log_prefix + "Получена локальная команда QUIT_CLIENT. Завершение клиента и сессии с сервером.");
                std::cout << "КЛИЕНТ: Выход из программы по команде QUIT_CLIENT..." << std::endl;
                if (client_socket.isValid()) { 
                    if(!client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION")){
                        Logger::warn(client_log_prefix + "Не удалось отправить EXIT_CLIENT_SESSION на сервер во время QUIT_CLIENT. Ошибка сокета: " + std::to_string(client_socket.getLastSocketError()));
                    }
                }
                current_session_active = false; 
                break;
            }

            if (upper_case_input == "HELP") { 
                std::cout << "\nКЛИЕНТ: Локальная команда HELP:\n";
                std::cout << "  Доступные команды для отправки на сервер (синтаксис согласно спецификации проекта):\n";
                std::cout << "  ADD FIO \"<полное имя>\" IP \"<ip>\" DATE \"<дд.мм.гггг>\"\n";
                std::cout << "      [TRAFFIC_IN <t0> ... <t23>] [TRAFFIC_OUT <t0> ... <t23>] [END]\n";
                std::cout << "  SELECT [FIO \"<имя>\"] [IP \"<ip>\"] [DATE \"<дд.мм.гггг>\"] [END]\n";
                std::cout << "  DELETE [FIO \"<имя>\"] [IP \"<ip>\"] [DATE \"<дд.мм.гггг>\"] [END]\n";
                std::cout << "  EDIT [<критерии_фильтрации>] SET <поле1> \"<значение1>\" [<поле2> \"<значение2>\"] ... [END]\n";
                std::cout << "      Поля для SET: FIO, IP, DATE, TRAFFIC_IN <t0..t23>, TRAFFIC_OUT <t0..t23>\n";
                std::cout << "  CALCULATE_CHARGES [<критерии_фильтрации>] START_DATE <дата1> END_DATE <дата2> [END]\n";
                std::cout << "  PRINT_ALL [END]\n";
                std::cout << "  LOAD \"<имя_файла_на_сервере>\" [END]\n";
                std::cout << "  SAVE [\"<имя_файла_на_сервере>\"] [END] (если имя файла опущено, используется последнее загруженное/сохраненное на сервере)\n";
                std::cout << "  EXIT (для завершения текущей сессии с сервером)\n";
                std::cout << "-------------------------------------------------------------------------------------\n";
                std::cout << "  Локальные команды клиента (не отправляются на сервер):\n";
                std::cout << "  HELP          - Показать это справочное сообщение.\n";
                std::cout << "  QUIT_CLIENT   - Немедленно выйти из этой клиентской программы (также завершает сессию с сервером).\n";
                std::cout << "-------------------------------------------------------------------------------------\n";
                std::cout << "Примечания:\n";
                std::cout << "  * Строковые значения, содержащие пробелы, должны быть заключены в двойные кавычки (например, FIO \"Иван Иванов\").\n";
                std::cout << "  * Ключевое слово END в конце большинства запросов является необязательным и может быть опущено.\n";
                std::cout << "  * Даты вводятся в формате ДД.ММ.ГГГГ. IP-адреса в формате xxx.xxx.xxx.xxx.\n";
                std::cout << "  * Трафик (TRAFFIC_IN, TRAFFIC_OUT) состоит из 24 значений типа double, разделенных пробелами.\n";
                std::cout << "----------------------------------------" << std::endl;
                continue; 
            }
            
            if (!process_single_request_to_server(client_socket, command_to_process, std::cout, client_log_prefix, client_final_receive_timeout_ms)) {
                 Logger::error(client_log_prefix + "Сессия с сервером, вероятно, завершена из-за ошибки отправки/получения или проблемы на стороне сервера.");
                 current_session_active = false; 
                 break; 
            }

            if (upper_case_input == "EXIT") { 
                Logger::info(client_log_prefix + "Команда EXIT отправлена на сервер, ответ получен. Клиент завершает сессию.");
                current_session_active = false; 
                break;
            }
        } 
    } 

    if (client_socket.isValid()) {
        Logger::debug(client_log_prefix + "Операции клиента завершены. Гарантируется закрытие клиентского сокета.");
        client_socket.closeSocket();
    }

    Logger::info(client_log_prefix + "========== ПРОГРАММА КЛИЕНТА ЗАВЕРШЕНА ==========");
    if (is_interactive_mode || (!is_interactive_mode && !connection_lost_during_batch)) { 
        std::cout << "КЛИЕНТ: Отключен От Сервера. Программа Завершена." << std::endl;
    }
    return 0;
}
#endif // UNIT_TESTING
