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

// Вспомогательная функция для преобразования строки в верхний регистр
static std::string clientToUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return static_cast<char>(std::toupper(c)); });
    return s;
}

// Вывод справки по аргументам командной строки клиента
static void printClientCommandLineHelp(const char* app_name_char) {
    std::string app_name = (app_name_char && app_name_char[0] != '\0') ? app_name_char : "database_client";
    std::cout << "\nInternet Provider Database Client\n";
    std::cout << "Usage: " << app_name << " -s <server_address> [options]\n\n";
    std::cout << "Required options:\n";
    std::cout << "  -s, --server <server_address>  Address or hostname of the database server.\n\n";
    std::cout << "Optional options:\n";
    std::cout << "  -p, --port <port_number>      Network port of the server (default: 12345).\n";
    std::cout << "  -f, --file <request_file>     Batch mode: execute requests from the specified <request_file>.\n"
              << "                                (Without this option, the client runs in interactive mode).\n";
    std::cout << "  -o, --output <output_file>    For batch mode (-f): specify a file to save server responses.\n"
              << "                                Default: <request_filename_without_ext>.out.<original_ext> (or .txt).\n";
    std::cout << "  --timeout <ms>                Timeout for waiting for a server response in milliseconds.\n"
              << "                                (Default: " << DEFAULT_CLIENT_RECEIVE_TIMEOUT_MS << " ms).\n";
    std::cout << "  -l, --log-level <LEVEL>       Client logging level (DEBUG, INFO, WARN, ERROR, NONE).\n"
              << "                                (Default: INFO).\n";
    std::cout << "  --log-file <path_to_file>    Path to the client log file.\n"
              << "                                (Default: '" << DEFAULT_CLIENT_LOG_FILE << "'). If empty, logs only to console.\n";
    std::cout << "  -h, --help                      Show this help message and exit.\n" << std::endl;
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
        Logger::debug(client_id_log_prefix + "Skipping empty query (will not be sent to server).");
        return true;
    }

    Logger::info(client_id_log_prefix + "Sending request to server: \"" + query + "\"");
    if (!socket.sendAllDataWithLengthPrefix(query)) {
        int err_code = socket.getLastSocketError();
        std::string err_detail_str = "Socket error code: " + std::to_string(err_code);
#ifdef _WIN32
        // Можно добавить форматирование WSA-ошибки, если нужно
#else
        if (err_code != 0) err_detail_str += " (" + std::string(strerror(err_code)) + ")";
#endif
        out_stream_for_response << "CLIENT: SEND ERROR: Failed to send request to server. Check connection. " << err_detail_str << "\n";
        Logger::error(client_id_log_prefix + "Failed to send request: sendAllDataWithLengthPrefix returned false. " + err_detail_str);
        return false; 
    }

    std::string accumulated_payload_for_output_stream; 
    bool in_multi_part_receive_session = false;
    size_t total_records_expected_in_multi_session = 0;
    size_t records_actually_accumulated_in_session = 0;
    bool server_reported_error_status = false; 
    std::string first_status_message_from_server; // ИСПРАВЛЕНО: Объявляем здесь для хранения первого сообщения статуса

    do { 
        // in_multi_part_receive_session сбрасывается в false в конце предыдущей итерации, если это не продолжающийся multi-part
        // Для первого прохода цикла он будет false.

        bool current_part_receive_success = false;
        std::string raw_message_part_from_server = socket.receiveAllDataWithLengthPrefix(current_part_receive_success, receive_timeout_ms);

        if (!current_part_receive_success) {
            int err_code = socket.getLastSocketError();
            std::string err_msg_short = "Failed to receive a complete response part from the server.";
            std::string err_msg_detail_log = "receiveAllDataWithLengthPrefix returned success=false.";

            if (!socket.isValid()) {
                err_msg_short = "Connection to server was lost.";
                err_msg_detail_log += " Socket became invalid.";
            } else if (err_code != 0) { 
                #ifdef _WIN32
                if (err_code == WSAETIMEDOUT) err_msg_short = "Server response timed out.";
                else if (err_code == WSAECONNRESET || err_code == WSAECONNABORTED) err_msg_short = "Connection was reset by server.";
                else err_msg_short = "Network error receiving data.";
                err_msg_detail_log += " WSA Error Code: " + std::to_string(err_code) + ".";
                #else
                bool is_timeout_err = (err_code == EAGAIN);
                #if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
                if (err_code == EWOULDBLOCK) is_timeout_err = true;
                #endif
                if (is_timeout_err) err_msg_short = "Server response timed out.";
                else if (err_code == ECONNRESET || err_code == EPIPE) err_msg_short = "Connection was reset by server or pipe broken.";
                else err_msg_short = "Network error receiving data.";
                err_msg_detail_log += " Errno: " + std::to_string(err_code) + " (" + std::strerror(err_code) + ").";
                #endif
            } else {
                 err_msg_short = "Connection closed by server while waiting for response part.";
                 err_msg_detail_log += " Assumed connection gracefully closed by peer (recv returned 0 on length/payload).";
            }
            out_stream_for_response << "CLIENT: RECEIVE ERROR: " << err_msg_short << "\n";
            Logger::error(client_id_log_prefix + "Error receiving response part: " + err_msg_detail_log);
            return false; 
        }
        
        Logger::debug(client_id_log_prefix + "Received a response block from server (raw length: " + std::to_string(raw_message_part_from_server.length()) + ").");

        std::istringstream response_parser_stream(raw_message_part_from_server);
        std::string header_line_buffer;
        int parsed_status_code = -1; 
        std::string current_parsed_status_message_text; // Сообщение из текущего блока
        size_t parsed_records_in_this_payload = 0;
        size_t parsed_total_records_for_multi = 0;
        std::string parsed_payload_data_type;
        std::string current_message_payload_data_str;
        bool data_marker_found_in_header = false;

        while (std::getline(response_parser_stream, header_line_buffer)) {
            if (header_line_buffer.rfind(SRV_HEADER_DATA_MARKER, 0) == 0) { 
                data_marker_found_in_header = true;
                std::ostringstream payload_collector_oss;
                payload_collector_oss << response_parser_stream.rdbuf(); 
                current_message_payload_data_str = payload_collector_oss.str();
                break; 
            }
            std::string key_str, value_str;
            size_t colon_pos = header_line_buffer.find(':');
            if (colon_pos != std::string::npos) {
                key_str = header_line_buffer.substr(0, colon_pos);
                value_str = header_line_buffer.substr(colon_pos + 1);
                key_str.erase(0, key_str.find_first_not_of(" \t\r\n")); key_str.erase(key_str.find_last_not_of(" \t\r\n") + 1);
                value_str.erase(0, value_str.find_first_not_of(" \t\r\n")); value_str.erase(value_str.find_last_not_of(" \t\r\n") + 1);

                if (key_str == SRV_HEADER_STATUS) { try { parsed_status_code = std::stoi(value_str); } catch(const std::exception& e_stoi_status){ Logger::warn(client_id_log_prefix + "Failed to parse STATUS value '" + value_str + "': " + e_stoi_status.what()); parsed_status_code = -2; } }
                else if (key_str == SRV_HEADER_MESSAGE) { current_parsed_status_message_text = value_str; } // Сохраняем сообщение текущего блока
                else if (key_str == SRV_HEADER_RECORDS_IN_PAYLOAD) { try { parsed_records_in_this_payload = std::stoul(value_str); } catch(const std::exception& e_stoul_rec){ Logger::warn(client_id_log_prefix + "Failed to parse RECORDS_IN_PAYLOAD value '" + value_str + "': " + e_stoul_rec.what()); } }
                else if (key_str == SRV_HEADER_TOTAL_RECORDS) { try { parsed_total_records_for_multi = std::stoul(value_str); } catch(const std::exception& e_stoul_total){  Logger::warn(client_id_log_prefix + "Failed to parse TOTAL_RECORDS value '" + value_str + "': " + e_stoul_total.what()); } }
                else if (key_str == SRV_HEADER_PAYLOAD_TYPE) { parsed_payload_data_type = value_str; }
            }
        }
        
        if (!data_marker_found_in_header || parsed_status_code == -1 || parsed_status_code == -2 ) {
            out_stream_for_response << "CLIENT: PROTOCOL ERROR: Invalid response header format from server (missing or unparsable "
                                    << (!data_marker_found_in_header ? SRV_HEADER_DATA_MARKER : "") 
                                    << ( (!data_marker_found_in_header && (parsed_status_code == -1 || parsed_status_code == -2)) ? " and " : "")
                                    << ((parsed_status_code == -1 || parsed_status_code == -2) ? SRV_HEADER_STATUS : "")
                                    << ").\nRaw part received:\n" << raw_message_part_from_server << "\n";
            Logger::error(client_id_log_prefix + "Error parsing server response header. Data marker found: " + (data_marker_found_in_header ? "yes" : "no") + ", Parsed status code: " + std::to_string(parsed_status_code) + ". Raw part: " + raw_message_part_from_server);
            return false; 
        }
        
        Logger::info(client_id_log_prefix + "Server Response Part Parsed: Status=" + std::to_string(parsed_status_code) +
                     ", Msg=\"" + current_parsed_status_message_text + "\", PayloadType=" + parsed_payload_data_type +
                     ", RecordsInThisPart=" + std::to_string(parsed_records_in_this_payload) + 
                     ", TotalExpected(if multi)=" + std::to_string(parsed_total_records_for_multi));

        if (accumulated_payload_for_output_stream.empty() && first_status_message_from_server.empty()) { // Если это самый первый блок ответа на запрос
            first_status_message_from_server = current_parsed_status_message_text; // Сохраняем первое сообщение статуса
            if (!first_status_message_from_server.empty()) {
                out_stream_for_response << "Server: " << first_status_message_from_server << "\n";
            }
        } else if (parsed_status_code == SRV_STATUS_OK_MULTI_PART_END && !current_parsed_status_message_text.empty()) {
            // Выводим сообщение от END чанка, если оно есть и отличается от первого
             if (current_parsed_status_message_text != first_status_message_from_server) {
                out_stream_for_response << "Server: " << current_parsed_status_message_text << "\n";
             }
        }
        
        if (parsed_status_code >= SRV_STATUS_BAD_REQUEST) { 
             server_reported_error_status = true; 
             if (accumulated_payload_for_output_stream.empty() && !first_status_message_from_server.empty() && current_parsed_status_message_text != first_status_message_from_server) {
                // Если первое сообщение статуса уже выведено, а это другое сообщение об ошибке
             } else if (accumulated_payload_for_output_stream.empty() && first_status_message_from_server.empty() && !current_parsed_status_message_text.empty()){
                // Если это первое сообщение и оно об ошибке, но еще не было выведено
                out_stream_for_response << "Server: " << current_parsed_status_message_text << "\n";
             }

             if (!current_message_payload_data_str.empty()){
                out_stream_for_response << "Error Details from Server:\n" << current_message_payload_data_str;
             }
             Logger::error(client_id_log_prefix + "Server reported an error: Status=" + std::to_string(parsed_status_code) + ", Message=\"" + current_parsed_status_message_text + "\"");
        }
        
        if (parsed_status_code < SRV_STATUS_BAD_REQUEST && !current_message_payload_data_str.empty()) {
            accumulated_payload_for_output_stream += current_message_payload_data_str;
            if ( (parsed_status_code == SRV_STATUS_OK_MULTI_PART_BEGIN || parsed_status_code == SRV_STATUS_OK_MULTI_PART_CHUNK) &&
                 !current_message_payload_data_str.empty() && current_message_payload_data_str.back() != '\n') {
                accumulated_payload_for_output_stream += "\n"; 
            }
        }
        records_actually_accumulated_in_session += parsed_records_in_this_payload;

        if (parsed_status_code == SRV_STATUS_OK_MULTI_PART_BEGIN) {
            in_multi_part_receive_session = true;
            total_records_expected_in_multi_session = parsed_total_records_for_multi; 
            Logger::debug(client_id_log_prefix + "Multi-part response started. Expecting total records: " + std::to_string(total_records_expected_in_multi_session));
        } else if (parsed_status_code == SRV_STATUS_OK_MULTI_PART_CHUNK) {
            if (!in_multi_part_receive_session && total_records_expected_in_multi_session == 0 && records_actually_accumulated_in_session == parsed_records_in_this_payload) { 
                 out_stream_for_response << "CLIENT: PROTOCOL ERROR: Received a data chunk (STATUS " << SRV_STATUS_OK_MULTI_PART_CHUNK << ") without an active multi-part session initiation.\n";
                 Logger::error(client_id_log_prefix + "Protocol error: SRV_STATUS_OK_MULTI_PART_CHUNK received without prior SRV_STATUS_OK_MULTI_PART_BEGIN.");
                 return false;
            }
            in_multi_part_receive_session = true; 
        } else if (parsed_status_code == SRV_STATUS_OK_MULTI_PART_END) {
            in_multi_part_receive_session = false; 
            Logger::debug(client_id_log_prefix + "Multi-part response end message received. Total records accumulated in this session: " + std::to_string(records_actually_accumulated_in_session));
            if (total_records_expected_in_multi_session > 0 && records_actually_accumulated_in_session != total_records_expected_in_multi_session) {
                 std::string warning_str = "CLIENT WARNING: Number of accumulated records (" + std::to_string(records_actually_accumulated_in_session) +
                                          ") does not match total expected by server (" + std::to_string(total_records_expected_in_multi_session) + ") in multi-part response.\n";
                 out_stream_for_response << warning_str;
                 Logger::warn(client_id_log_prefix + warning_str);
            }
        } else { 
            in_multi_part_receive_session = false;
        }

    } while (in_multi_part_receive_session && socket.isValid() && !server_reported_error_status); 

    if (!server_reported_error_status && !accumulated_payload_for_output_stream.empty()){
        out_stream_for_response << accumulated_payload_for_output_stream;
    }

    // ИСПРАВЛЕНО: Используем first_status_message_from_server для условия, если оно не пустое
    if (!accumulated_payload_for_output_stream.empty() && 
        accumulated_payload_for_output_stream.back() != '\n' && 
        (&out_stream_for_response == &std::cout)) {
         out_stream_for_response << "\n"; 
    }
    if ((&out_stream_for_response == &std::cout) && (!accumulated_payload_for_output_stream.empty() || !first_status_message_from_server.empty() || server_reported_error_status) ) { 
        out_stream_for_response << "----------------------------------------" << std::endl;
    } else if ((&out_stream_for_response != &std::cout) && !accumulated_payload_for_output_stream.empty() && accumulated_payload_for_output_stream.back() != '\n'){
        out_stream_for_response << "\n"; 
    }

    return !server_reported_error_status; 
}


int main(int argc, char* argv[]) {
    Logger::init(LogLevel::INFO, DEFAULT_CLIENT_LOG_FILE);
    const std::string client_log_prefix = "[ClientMain] ";
    Logger::info(client_log_prefix + "===================================================");
    Logger::info(client_log_prefix + "====== Internet Provider Database Client ======");
    Logger::info(client_log_prefix + "===================================================");

    std::string server_host_address;
    int server_port_number = 12345; 
    std::string batch_command_file_path;
    std::string batch_output_file_path;
    bool is_interactive_mode = true;
    LogLevel client_final_log_level = LogLevel::INFO;
    std::string client_final_log_file = DEFAULT_CLIENT_LOG_FILE;
    int client_final_receive_timeout_ms = DEFAULT_CLIENT_RECEIVE_TIMEOUT_MS;

    if (argc <= 1) { 
        printClientCommandLineHelp(argv[0]);
        Logger::info(client_log_prefix + "Exiting: No command line arguments provided.");
        return 1; 
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg_str(argv[i]);
        if (arg_str == "-h" || arg_str == "--help") {
            printClientCommandLineHelp(argv[0]);
            Logger::info(client_log_prefix + "Help requested via command line. Exiting.");
            return 0;
        }
    }

    try {
        for (int i = 1; i < argc; ++i) {
            std::string arg_str(argv[i]);
            if ((arg_str == "-s" || arg_str == "--server")) {
                if (i + 1 < argc) { server_host_address = argv[++i]; } 
                else { throw std::runtime_error("Option '" + arg_str + "' requires an argument (server address)."); }
            } else if ((arg_str == "-p" || arg_str == "--port")) {
                if (i + 1 < argc) { 
                    std::string port_val_str = argv[++i];
                    try { server_port_number = std::stoi(port_val_str); }
                    catch (const std::invalid_argument&) { throw std::runtime_error("Invalid port number format: " + port_val_str); }
                    catch (const std::out_of_range&) { throw std::runtime_error("Port number out of range: " + port_val_str); }

                    if (server_port_number <= 0 || server_port_number > 65535) {
                        throw std::runtime_error("Invalid port number: " + std::to_string(server_port_number) + ". Port must be between 1-65535.");
                    }
                } else { throw std::runtime_error("Option '" + arg_str + "' requires an argument (port number)."); }
            } else if ((arg_str == "-f" || arg_str == "--file")) {
                if (i + 1 < argc) { batch_command_file_path = argv[++i]; is_interactive_mode = false; } 
                else { throw std::runtime_error("Option '" + arg_str + "' requires an argument (request file path)."); }
            } else if ((arg_str == "-o" || arg_str == "--output")) {
                if (i + 1 < argc) { batch_output_file_path = argv[++i]; } 
                else { throw std::runtime_error("Option '" + arg_str + "' requires an argument (output file path)."); }
            } else if (arg_str == "--timeout") {
                if (i + 1 < argc) { 
                    std::string timeout_val_str = argv[++i];
                    try { client_final_receive_timeout_ms = std::stoi(timeout_val_str); }
                    catch (const std::invalid_argument&) { throw std::runtime_error("Invalid timeout format: " + timeout_val_str); }
                    catch (const std::out_of_range&) { throw std::runtime_error("Timeout value out of range: " + timeout_val_str); }

                    if (client_final_receive_timeout_ms < 0) {
                         Logger::warn(client_log_prefix + "Receive timeout cannot be negative (" + std::to_string(client_final_receive_timeout_ms) + "). Using default: " + std::to_string(DEFAULT_CLIENT_RECEIVE_TIMEOUT_MS) + " ms.");
                         client_final_receive_timeout_ms = DEFAULT_CLIENT_RECEIVE_TIMEOUT_MS;
                    }
                } else { throw std::runtime_error("Option '" + arg_str + "' requires an argument (timeout in ms)."); }
            } else if ((arg_str == "-l" || arg_str == "--log-level")) {
                if (i + 1 < argc) {
                    std::string level_str_val = argv[++i];
                    std::string level_val_upper = clientToUpper(level_str_val);
                    if (level_val_upper == "DEBUG") client_final_log_level = LogLevel::DEBUG;
                    else if (level_val_upper == "INFO") client_final_log_level = LogLevel::INFO;
                    else if (level_val_upper == "WARN") client_final_log_level = LogLevel::WARN;
                    else if (level_val_upper == "ERROR") client_final_log_level = LogLevel::ERROR;
                    else if (level_val_upper == "NONE") client_final_log_level = LogLevel::NONE;
                    else { Logger::warn(client_log_prefix + "Unknown log level specified: '" + level_str_val + "'. Current logger level will be used.");}
                } else { throw std::runtime_error("Option '" + arg_str + "' requires an argument (log level: DEBUG, INFO, WARN, ERROR, NONE)."); }
            } else if (arg_str == "--log-file") {
                if (i + 1 < argc) { client_final_log_file = argv[++i]; } 
                else { throw std::runtime_error("Option '" + arg_str + "' requires an argument (log file path)."); }
            } else {
                throw std::runtime_error("Unknown option or argument error: " + arg_str);
            }
        }
    } catch (const std::exception& e_args) {
        Logger::error(client_log_prefix + "Error parsing command line arguments: " + e_args.what());
        std::cerr << "CLIENT: ARGUMENT ERROR: " << e_args.what() << std::endl;
        printClientCommandLineHelp(argv[0]);
        return 1;
    }
    
    Logger::init(client_final_log_level, client_final_log_file); 
    Logger::info(client_log_prefix + "Client logger re-initialized. Level: " + std::to_string(static_cast<int>(Logger::getLevel())) +
                 ", File: '" + (client_final_log_file.empty() ? "Console Only" : client_final_log_file) + "'");

    if (server_host_address.empty()) {
        Logger::error(client_log_prefix + "Critical error: Server address (-s or --server) was not specified.");
        std::cerr << "CLIENT: CRITICAL ERROR: Server address (-s or --server) must be specified." << std::endl;
        printClientCommandLineHelp(argv[0]);
        return 1;
    }
    if (!batch_output_file_path.empty() && is_interactive_mode) {
        Logger::warn(client_log_prefix + "Output file option (-o/--output: '" + batch_output_file_path + "') is only applicable in batch mode (-f/--file). It will be ignored in interactive mode.");
        batch_output_file_path.clear(); 
    }
    
    if (!is_interactive_mode && batch_output_file_path.empty() && !batch_command_file_path.empty()) {
        try {
            std::filesystem::path input_fs_path(batch_command_file_path);
            std::string output_base_filename = input_fs_path.stem().string(); 
            std::string input_file_extension_str = input_fs_path.extension().string();
            batch_output_file_path = (input_fs_path.parent_path() / (output_base_filename + ".out" + (input_file_extension_str.empty() ? ".txt" : input_file_extension_str) )).string();
        } catch (const std::exception& e_fs_path) { 
            Logger::error(client_log_prefix + "Error constructing default output file path from input '" + batch_command_file_path + "': " + e_fs_path.what());
            std::cerr << "CLIENT: ERROR: Could not construct default output file path. Please specify an output file with -o." << std::endl;
            return 1;
        }
    }

    Logger::info(client_log_prefix + "Final client configuration snapshot:");
    Logger::info(client_log_prefix + "  Server Target: " + server_host_address + ":" + std::to_string(server_port_number));
    Logger::info(client_log_prefix + "  Operational Mode: " + (is_interactive_mode ? "Interactive" : "Batch (Source Command File: '" + batch_command_file_path + "')"));
    if (!is_interactive_mode) {
        Logger::info(client_log_prefix + "  Batch Mode Output File: '" + batch_output_file_path + "'");
    }
    Logger::info(client_log_prefix + "  Server Response Timeout Setting: " + std::to_string(client_final_receive_timeout_ms) + " ms");

    TCPSocket client_socket;
    Logger::info(client_log_prefix + "Attempting to establish connection with server " + server_host_address + ":" + std::to_string(server_port_number) + "...");
    std::cout << "Client: Connecting to server " << server_host_address << ":" << server_port_number << "..." << std::endl;

    if (!client_socket.connectSocket(server_host_address, server_port_number)) {
        int err_code = client_socket.getLastSocketError();
        std::string err_detail_str = "Socket error code: " + std::to_string(err_code);
        #ifdef _WIN32 
        #else
        if (err_code != 0) err_detail_str += " (" + std::string(strerror(err_code)) + ")";
        #endif
        std::cout << "CLIENT: CONNECTION FAILED: Could not connect to server " << server_host_address << ":" << server_port_number
                  << ". Please ensure the server is running and accessible. " << err_detail_str << std::endl;
        Logger::error(client_log_prefix + "Failed to connect to server " + server_host_address + ":" + std::to_string(server_port_number) + ". " + err_detail_str);
        Logger::info(client_log_prefix + "========== Client Exiting (Connection Error) ==========");
        return 1;
    }
    Logger::info(client_log_prefix + "Successfully connected to server " + server_host_address + ":" + std::to_string(server_port_number));
    std::cout << "Client: Successfully connected to server." << std::endl;


    if (!is_interactive_mode) {
        // --- Пакетный режим ---
        Logger::info(client_log_prefix + "Operating in batch mode. Command file: '" + batch_command_file_path + "'");
        std::ifstream command_input_file(batch_command_file_path);
        if (!command_input_file.is_open()) {
            Logger::error(client_log_prefix + "Failed to open command file for reading: \"" + batch_command_file_path + "\"");
            std::cout << "CLIENT: ERROR: Could not open command file: " << batch_command_file_path << std::endl;
            if(client_socket.isValid()) { client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION");} 
            client_socket.closeSocket();
            Logger::info(client_log_prefix + "========== Client Exiting (Command File Error) ==========");
            return 1;
        }

        std::ofstream batch_output_file_stream(batch_output_file_path);
        if (!batch_output_file_stream.is_open()) {
            Logger::error(client_log_prefix + "Failed to open output file for writing: \"" + batch_output_file_path + "\"");
            std::cout << "CLIENT: ERROR: Could not open output file: " << batch_output_file_path << std::endl;
            command_input_file.close();
            if(client_socket.isValid()) { client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION");}
            client_socket.closeSocket();
            Logger::info(client_log_prefix + "========== Client Exiting (Output File Error) ==========");
            return 1;
        }
        Logger::info(client_log_prefix + "Batch mode output will be written to: '" + batch_output_file_path + "'");
        std::cout << "Client: Batch processing results will be saved to: \"" << batch_output_file_path << "\"" << std::endl;

        batch_output_file_stream << "--- Client: Batch processing started. Command file: " << batch_command_file_path << " ---\n";
        batch_output_file_stream << "--- Client: Connected to server: " << server_host_address << ":" << server_port_number << " ---\n\n";
        batch_output_file_stream.flush();

        std::string current_line_from_file;
        int query_counter = 0;
        int file_line_number = 0;
        bool client_explicitly_sent_exit = false;
        bool connection_lost_during_batch = false;

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
            query_counter++;

            batch_output_file_stream << "\n[Client] Request #" << query_counter << " (from file line #" << file_line_number << "): " << trimmed_query_line << "\n";
            batch_output_file_stream << "----------------------------------------\n"; 
            batch_output_file_stream.flush();

            if (!process_single_request_to_server(client_socket, trimmed_query_line, batch_output_file_stream, client_log_prefix, client_final_receive_timeout_ms)) {
                Logger::error(client_log_prefix + "Fatal error processing request (file line #" + std::to_string(file_line_number) + "): \"" + trimmed_query_line + "\". Aborting batch file processing.");
                batch_output_file_stream << "\nCLIENT: CRITICAL ERROR: Connection to server lost or request could not be processed. Batch processing aborted.\n";
                connection_lost_during_batch = true;
                break; 
            }
            batch_output_file_stream << "----------------------------------------\n\n"; 
            batch_output_file_stream.flush();

             if (clientToUpper(trimmed_query_line) == "EXIT") {
                Logger::info(client_log_prefix + "EXIT command found in command file (line #" + std::to_string(file_line_number) + "). Terminating batch processing and client session.");
                client_explicitly_sent_exit = true;
                break; 
            }
        }
        command_input_file.close();

        batch_output_file_stream << "--- Client: Batch processing finished for file: " << batch_command_file_path << " ---";
        if (connection_lost_during_batch) {
             batch_output_file_stream << " (Processing was aborted due to an error)";
        }
        batch_output_file_stream << std::endl;
        batch_output_file_stream.close();

        std::cout << "Client: Batch processing of command file \"" << batch_command_file_path << "\" complete. "
                  << "Total commands processed: " << query_counter << "."
                  << " Results saved to: \"" << batch_output_file_path << "\"" << std::endl;
        
        if (!client_explicitly_sent_exit && !connection_lost_during_batch && client_socket.isValid()) {
             Logger::info(client_log_prefix + "Sending EXIT_CLIENT_SESSION to server after completing batch file processing.");
             if (!client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION")) {
                 Logger::warn(client_log_prefix + "Failed to send EXIT_CLIENT_SESSION to server after batch processing. Socket error: " + std::to_string(client_socket.getLastSocketError()));
             }
        }

    } else {
        // --- Интерактивный режим ---
        Logger::info(client_log_prefix + "Entering interactive mode with server.");
        std::cout << "\nClient is in interactive mode. Connected to " << server_host_address << ":" << server_port_number << ".\n";
        std::cout << "Type 'HELP' for a list of commands, 'EXIT' to end the server session, or 'QUIT_CLIENT' to exit the client program.\n";
        
        std::string user_input_line;
        bool current_session_active = true;
        while (current_session_active && client_socket.isValid()) { 
            std::cout << "[" << server_host_address << ":" << server_port_number << "] > ";
            std::cout.flush(); 
            if (!std::getline(std::cin, user_input_line)) {
                if (std::cin.eof()) { 
                     Logger::info(client_log_prefix + "EOF detected in interactive mode. Sending EXIT_CLIENT_SESSION to server.");
                     std::cout << "\nClient: EOF detected (end of input). Ending server session..." << std::endl;
                     if (client_socket.isValid()) { 
                         if(!client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION")){
                             Logger::warn(client_log_prefix + "Failed to send EXIT_CLIENT_SESSION to server on EOF. Socket error: " + std::to_string(client_socket.getLastSocketError()));
                         }
                     }
                } else { 
                    Logger::error(client_log_prefix + "Critical std::cin error in interactive mode (not EOF). Terminating.");
                    std::cout << "CLIENT: CRITICAL INPUT ERROR. Terminating." << std::endl;
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
                Logger::info(client_log_prefix + "Local command QUIT_CLIENT received. Terminating client and server session.");
                std::cout << "Client: Exiting program via QUIT_CLIENT command..." << std::endl;
                if (client_socket.isValid()) { 
                    if(!client_socket.sendAllDataWithLengthPrefix("EXIT_CLIENT_SESSION")){
                        Logger::warn(client_log_prefix + "Failed to send EXIT_CLIENT_SESSION to server during QUIT_CLIENT. Socket error: " + std::to_string(client_socket.getLastSocketError()));
                    }
                }
                current_session_active = false; 
                break;
            }

            if (upper_case_input == "HELP") { 
                std::cout << "\nClient: Local HELP command:\n";
                std::cout << "  Available commands to send to the server (syntax as per project specification):\n";
                std::cout << "  ADD FIO \"<full name>\" IP \"<ip>\" DATE \"<dd.mm.yyyy>\"\n";
                std::cout << "      [TRAFFIC_IN <t0> ... <t23>] [TRAFFIC_OUT <t0> ... <t23>] [END]\n";
                std::cout << "  SELECT [FIO \"<name>\"] [IP \"<ip>\"] [DATE \"<dd.mm.yyyy>\"] [END]\n";
                std::cout << "  DELETE [FIO \"<name>\"] [IP \"<ip>\"] [DATE \"<dd.mm.yyyy>\"] [END]\n";
                std::cout << "  EDIT [<filter_criteria>] SET <field1> \"<value1>\" [<field2> \"<value2>\"] ... [END]\n";
                std::cout << "      Fields for SET: FIO, IP, DATE, TRAFFIC_IN <t0..t23>, TRAFFIC_OUT <t0..t23>\n";
                std::cout << "  CALCULATE_CHARGES [<filter_criteria>] START_DATE <date1> END_DATE <date2> [END]\n";
                std::cout << "  PRINT_ALL [END]\n";
                std::cout << "  LOAD \"<filename_on_server>\" [END]\n";
                std::cout << "  SAVE [\"<filename_on_server>\"] [END] (if filename omitted, uses last loaded/saved on server)\n";
                std::cout << "  EXIT (to end the current session with the server)\n";
                std::cout << "-------------------------------------------------------------------------------------\n";
                std::cout << "  Local client commands (not sent to server):\n";
                std::cout << "  HELP          - Show this help message.\n";
                std::cout << "  QUIT_CLIENT   - Immediately exit this client program (also ends server session).\n";
                std::cout << "-------------------------------------------------------------------------------------\n";
                std::cout << "Notes:\n";
                std::cout << "  * String values containing spaces must be enclosed in double quotes (e.g., FIO \"Ivan Ivanov\").\n";
                std::cout << "  * The END keyword at the end of most requests is optional and can be omitted.\n";
                std::cout << "  * Dates are entered in DD.MM.YYYY format. IP addresses in xxx.xxx.xxx.xxx format.\n";
                std::cout << "  * Traffic (TRAFFIC_IN, TRAFFIC_OUT) consists of 24 double values separated by spaces.\n";
                std::cout << "----------------------------------------" << std::endl;
                continue; 
            }
            
            if (!process_single_request_to_server(client_socket, command_to_process, std::cout, client_log_prefix, client_final_receive_timeout_ms)) {
                 Logger::error(client_log_prefix + "Session with server likely terminated due to send/receive error or server-side issue.");
                 current_session_active = false; 
                 break; 
            }

            if (upper_case_input == "EXIT") { 
                Logger::info(client_log_prefix + "EXIT command sent to server and response received. Client terminating session.");
                current_session_active = false; 
                break;
            }
        } 
    } 

    if (client_socket.isValid()) {
        Logger::debug(client_log_prefix + "Client operations finished. Ensuring client socket is closed.");
        client_socket.closeSocket();
    }

    Logger::info(client_log_prefix + "========== Client Program Terminated ==========");
    std::cout << "Client: Disconnected from server. Program terminated." << std::endl;
    return 0;
}
