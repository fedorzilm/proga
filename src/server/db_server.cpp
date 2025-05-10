#include "db_server.h"
#include "../network/network_protocol.h" // Этот инклюд правильный и нужен
#include "../query_parser.h"
#include "../common_defs.h"
#include "../provider_record.h"
#include "../date.h"
#include "../ip_address.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm> // Для std::transform, std::replace, std::min
#include <vector>
#include <map>
#include <cstdio>    // Для sscanf в url_decode
#include <cctype>    // Для isxdigit, std::toupper, std::isspace
#include <cstring>   // Для strerror
#include <cerrno>    // Для errno
#include <filesystem> // Для std::filesystem::path (C++17)

// ... (анонимное пространство имен и статические методы ParsedHttpRequest остаются без изменений) ...
// Копирую их из вашего предыдущего файла для полноты:
namespace { // Анонимное пространство имен для вспомогательных элементов

    // Вспомогательная функция для URL декодирования
    std::string url_decode_db_server_internal(const std::string& str) {
        std::string result;
        char ch_hex_val;
        int i_loop_var;
        unsigned int intermediate_unsigned_val;
        result.reserve(str.length()); // Предварительное выделение памяти
        for (i_loop_var = 0; i_loop_var < static_cast<int>(str.length()); i_loop_var++) {
            if (str[i_loop_var] == '%') {
                if (i_loop_var + 2 < static_cast<int>(str.length()) &&
                    isxdigit(static_cast<unsigned char>(str[i_loop_var+1])) &&
                    isxdigit(static_cast<unsigned char>(str[i_loop_var+2]))) {

                    char hex_pair_str[3] = {str[i_loop_var+1], str[i_loop_var+2], '\0'};
                    if (sscanf(hex_pair_str, "%x", &intermediate_unsigned_val) == 1) {
                        ch_hex_val = static_cast<char>(intermediate_unsigned_val);
                        result += ch_hex_val;
                    } else {
                         result += '%';
                         result += str[i_loop_var+1];
                         result += str[i_loop_var+2];
                    }
                    i_loop_var = i_loop_var + 2;
                } else {
                     result += '%';
                     if (i_loop_var + 1 < static_cast<int>(str.length())) result += str[i_loop_var+1];
                     if (i_loop_var + 2 < static_cast<int>(str.length())) result += str[i_loop_var+2];
                     i_loop_var = std::min(i_loop_var + 2, static_cast<int>(str.length() - 1));
                }
            } else if (str[i_loop_var] == '+') {
                result += ' ';
            } else {
                result += str[i_loop_var];
            }
        }
        return result;
    }

    std::string escape_html_chars_internal(const std::string& data) {
        std::string buffer;
        buffer.reserve(data.size());
        for(size_t pos = 0; pos != data.size(); ++pos) {
            switch(data[pos]) {
                case '&':  buffer.append("&amp;");       break;
                case '\"': buffer.append("&quot;");      break;
                case '\'': buffer.append("&#39;");       break;
                case '<':  buffer.append("&lt;");        break;
                case '>':  buffer.append("&gt;");        break;
                default:   buffer.append(&data[pos], 1); break;
            }
        }
        return buffer;
    }

    std::optional<ProviderRecord> parse_add_payload_universal(
        const std::string& name_str,
        const std::string& ip_str,
        const std::string& date_str,
        const std::string& traffic_str) {
        ProviderRecord rec;
        std::string error_msg_prefix_add = get_current_timestamp() + " [SrvUniversalADDParse] Error: ";
        if (name_str.empty()) { std::cerr << error_msg_prefix_add << "Name is empty." << std::endl; return std::nullopt;}
        rec.full_name = name_str;
        auto ip_opt = IpAddress::from_string(ip_str);
        if (!ip_opt) { std::cerr << error_msg_prefix_add << "Invalid IP '" << ip_str << "'." << std::endl; return std::nullopt; }
        rec.ip_address = *ip_opt;
        auto date_opt = Date::from_string(date_str);
        if (!date_opt) { std::cerr << error_msg_prefix_add << "Invalid Date '" << date_str << "'." << std::endl; return std::nullopt; }
        rec.record_date = *date_opt;
        std::stringstream traffic_ss(traffic_str);
        rec.hourly_traffic.assign(HOURS_IN_DAY, TrafficReading{});
        long long in_val, out_val;
        size_t hour_count = 0;
        while (hour_count < HOURS_IN_DAY) {
            if (!(traffic_ss >> in_val >> out_val)) {
                 std::cerr << error_msg_prefix_add << "Not enough numbers for traffic (expected " << HOURS_IN_DAY
                           << " pairs, read up to hour " << hour_count << ")." << std::endl;
                return std::nullopt;
            }
            if (in_val < 0 || out_val < 0) {
                std::cerr << error_msg_prefix_add << "Negative traffic value for hour " << hour_count << "." << std::endl;
                return std::nullopt;
            }
            rec.hourly_traffic[hour_count].incoming = in_val;
            rec.hourly_traffic[hour_count].outgoing = out_val;
            hour_count++;
        }
        std::string remaining_in_tr_stream_check;
        if (traffic_ss >> remaining_in_tr_stream_check && !trim_string(remaining_in_tr_stream_check).empty()) {
             std::cerr << error_msg_prefix_add << "Extra data in traffic string after " << hour_count << " pairs: ["
                       << trim_string(remaining_in_tr_stream_check) << "]" << std::endl;
             return std::nullopt;
        }
        return rec;
    }
    const std::string EDIT_COMMAND_SEPARATOR_INTERNAL = "###SEP###";
    struct ParsedEditCommandInternal { /* ... как было ... */ };
    std::optional<ParsedEditCommandInternal> parse_edit_payload_for_server_internal(const std::string& payload) { /* ... как было ... */ return ParsedEditCommandInternal{}; } // Заглушка для краткости

} // конец анонимного пространства имен

// Implementation of ParsedHttpRequest static methods
std::map<std::string, std::string> ParsedHttpRequest::parse_query_string(const std::string& query_str) {
    std::map<std::string, std::string> params;
    std::stringstream ss(query_str);
    std::string segment;
    while(std::getline(ss, segment, '&')) {
        size_t eq_pos = segment.find('=');
        if (eq_pos != std::string::npos) {
            params[trim_string(url_decode_db_server_internal(segment.substr(0, eq_pos)))] = trim_string(url_decode_db_server_internal(segment.substr(eq_pos + 1)));
        } else if (!segment.empty()) {
            params[trim_string(url_decode_db_server_internal(segment))] = "";
        }
    }
    return params;
}

ParsedHttpRequest ParsedHttpRequest::parse(const std::string& raw_request) {
    ParsedHttpRequest req;
    std::stringstream request_stream(raw_request);
    std::string line;

    if (std::getline(request_stream, line)) {
        line = trim_string(line);
        std::stringstream request_line_ss(line);
        request_line_ss >> req.method >> req.path >> req.http_version;
        std::transform(req.method.begin(), req.method.end(), req.method.begin(),
                       [](unsigned char c_uc){ return static_cast<char>(std::toupper(c_uc)); });

        size_t q_pos = req.path.find('?');
        if (q_pos != std::string::npos) {
            req.query_params = parse_query_string(req.path.substr(q_pos + 1));
            req.path = req.path.substr(0, q_pos);
        }
    } else {
        req.is_valid = false; return req;
    }

    while (std::getline(request_stream, line)) {
        line = trim_string(line);
        if (line.empty()) break;
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            req.headers[trim_string(line.substr(0, colon_pos))] = trim_string(line.substr(colon_pos + 1));
        }
    }

    size_t body_start_pos_in_raw = raw_request.find("\r\n\r\n");
    if (body_start_pos_in_raw != std::string::npos) {
        body_start_pos_in_raw += 4;
    } else {
        body_start_pos_in_raw = raw_request.find("\n\n");
        if (body_start_pos_in_raw != std::string::npos) {
            body_start_pos_in_raw += 2;
        } else {
            body_start_pos_in_raw = std::string::npos;
        }
    }

    if (body_start_pos_in_raw != std::string::npos && body_start_pos_in_raw < raw_request.length()) {
        req.body = raw_request.substr(body_start_pos_in_raw);
    } else {
        req.body = "";
    }

    auto it_content_type = req.headers.find("Content-Type");
    if (req.method == "POST" && it_content_type != req.headers.end() &&
        it_content_type->second.rfind("application/x-www-form-urlencoded",0) == 0) {
        req.form_params = parse_query_string(req.body);
    }

    if (!req.method.empty() && !req.path.empty() && !req.http_version.empty()) {
        req.is_valid = true;
    } else {
        req.is_valid = false;
    }
    return req;
}
// Конец скопированной части ParsedHttpRequest

DBServer::DBServer(Database& db, TariffPlan& tariff, const std::string& default_tariff_path)
    : database_(db), tariff_plan_(tariff), default_tariff_filename_(default_tariff_path), is_running_(false) {}

DBServer::~DBServer() {
    if (is_running_.load()) { stop(); }
}

void DBServer::start(int port) {
    // ... (как было)
    if (!listener_socket_.listen_on(port)) {
        std::string error_msg = "Server failed to listen on port " + std::to_string(port);
        std::cerr << get_current_timestamp() << " [Server] Error: " << error_msg << std::endl;
        throw SocketException(error_msg);
    }
    is_running_ = true;
    std::cout << get_current_timestamp() << " [Server] Started. Listening on port " << port << " for connections..." << std::endl;

    try {
        while (is_running_.load()) {
            bool accept_success = false;
            Network::TCPSocket client_sock = listener_socket_.accept_connection(accept_success);

            if (!is_running_.load()) {
                if(client_sock.is_valid()) client_sock.close_socket();
                break;
            }

            if (accept_success && client_sock.is_valid()) {
                socket_t raw_fd = client_sock.get_raw_socket();
                std::cout << get_current_timestamp() << " [Server] Accepted connection (fd: " << raw_fd << "). Dispatching..." << std::endl;
                {
                    std::lock_guard<std::mutex> lock(client_threads_mutex_);
                    client_threads_.emplace_back(&DBServer::client_connection_dispatcher, this, std::move(client_sock));
                }
            } else {
                if (is_running_.load() && !listener_socket_.is_valid()) {
                    std::cerr << get_current_timestamp() << " [Server] Listener socket became invalid while server is running. Stopping accept loop." << std::endl;
                    is_running_ = false;
                } else if (is_running_.load()) {
                     std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
            }
        }
    } catch (const SocketException& e) {
        if(is_running_.load()){
            std::cerr << get_current_timestamp() << " [Server] Socket exception in accept loop: " << e.what() << std::endl;
            is_running_ = false;
        }
    }
    std::cout << get_current_timestamp() << " [Server] Accept loop finished." << std::endl;
}

void DBServer::stop() {
    // ... (как было)
    bool already_stopping = !is_running_.exchange(false);
    if (already_stopping) {
        return;
    }
    std::cout << get_current_timestamp() << " [Server] Stop signal received. Closing listener socket..." << std::endl;
    listener_socket_.close_socket();
}

void DBServer::wait_for_shutdown() {
    // ... (как было)
    std::cout << get_current_timestamp() << " [Server] Waiting for client threads to finish..." << std::endl;
    std::vector<std::thread> threads_to_join_local;
    {
        std::lock_guard<std::mutex> lock(client_threads_mutex_);
        client_threads_.erase(std::remove_if(client_threads_.begin(), client_threads_.end(),
                                           [](const std::thread& t){ return !t.joinable(); }),
                              client_threads_.end());
        threads_to_join_local.swap(client_threads_);
    }

    std::cout << get_current_timestamp() << " [Server] Joining " << threads_to_join_local.size() << " client thread(s)..." << std::endl;
    for (auto& th : threads_to_join_local) {
        if (th.joinable()) {
            th.join();
        }
    }
    std::cout << get_current_timestamp() << " [Server] All client threads joined. Shutdown complete." << std::endl;
}

void DBServer::client_connection_dispatcher(Network::TCPSocket client_socket) {
    // ... (как было)
    socket_t client_fd_for_log_disp = client_socket.get_raw_socket();
    std::cout << get_current_timestamp() << " [Server Dispatcher fd:" << client_fd_for_log_disp << "] Thread started." << std::endl;

    char initial_buf[2048];
    int bytes_read = 0;
    std::string initial_data_str_disp;

    #ifdef _WIN32
        DWORD timeout_ms_disp = 5000;
        setsockopt(client_socket.get_raw_socket(), SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms_disp, sizeof(timeout_ms_disp));
        bytes_read = recv(client_socket.get_raw_socket(), initial_buf, sizeof(initial_buf) -1, 0);
    #else
        struct timeval tv_disp;
        tv_disp.tv_sec = 5;
        tv_disp.tv_usec = 0;
        setsockopt(client_socket.get_raw_socket(), SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv_disp, sizeof tv_disp);
        bytes_read = read(client_socket.get_raw_socket(), initial_buf, sizeof(initial_buf) -1);
    #endif

    if (bytes_read > 0) {
        initial_buf[bytes_read] = '\0';
        initial_data_str_disp.assign(initial_buf, bytes_read);

        std::string upper_initial_peek_disp = initial_data_str_disp.substr(0, std::min((size_t)10, initial_data_str_disp.length()));
        std::transform(upper_initial_peek_disp.begin(), upper_initial_peek_disp.end(), upper_initial_peek_disp.begin(),
                       [](unsigned char c_uc){ return static_cast<char>(std::toupper(c_uc)); });

        if (upper_initial_peek_disp.rfind("GET ", 0) == 0 || upper_initial_peek_disp.rfind("POST ", 0) == 0 ||
            upper_initial_peek_disp.rfind("PUT ", 0) == 0 || upper_initial_peek_disp.rfind("DELETE ", 0) == 0 ||
            upper_initial_peek_disp.rfind("HEAD ", 0) == 0 || upper_initial_peek_disp.rfind("OPTIONS ", 0) == 0 ) {
            std::cout << get_current_timestamp() << " [Server Dispatcher fd:" << client_fd_for_log_disp << "] Detected HTTP request. Passing to HTTP handler." << std::endl;
            http_client_handler_func(std::move(client_socket), client_fd_for_log_disp, initial_data_str_disp);
        } else {
            std::cout << get_current_timestamp() << " [Server Dispatcher fd:" << client_fd_for_log_disp << "] Assuming custom DB protocol. Initial bytes hint: '"
                      << initial_data_str_disp.substr(0,20) << (initial_data_str_disp.length()>20?"...":"") << "'" <<std::endl;
            custom_protocol_handler_func(std::move(client_socket), client_fd_for_log_disp, initial_data_str_disp);
        }
    } else if (bytes_read == 0) {
         std::cout << get_current_timestamp() << " [Server Dispatcher fd:" << client_fd_for_log_disp << "] Connection closed by peer immediately or timeout on initial read." << std::endl;
         client_socket.close_socket();
    } else {
         std::string error_string_disp;
         #ifdef _WIN32
             if(WSAGetLastError() == WSAETIMEDOUT) error_string_disp = "Timeout"; else error_string_disp = std::to_string(WSAGetLastError());
         #else
             if(errno == EAGAIN || errno == EWOULDBLOCK) error_string_disp = "Timeout"; else error_string_disp = strerror(errno);
         #endif
         std::cerr << get_current_timestamp() << " [Server Dispatcher fd:" << client_fd_for_log_disp << "] Error reading initial data: " << error_string_disp << std::endl;
         client_socket.close_socket();
    }
}

void DBServer::custom_protocol_handler_func(Network::TCPSocket client_socket, socket_t client_fd_for_log, std::string initial_data) {
    std::cout << get_current_timestamp() << " [Server CustomProto fd:" << client_fd_for_log << "] Handler started." << std::endl;
    try {
        // Убрано условие с NetworkProtocol::HEADER_SIZE, так как оно не соответствует текущей структуре network_protocol.h
        // if (!initial_data.empty() && initial_data.length() > NetworkProtocol::HEADER_SIZE) {
        // }
        // Если initial_data содержит полезные данные, которые нужно обработать перед циклом,
        // это нужно сделать здесь или передать в первую итерацию.
        // Пока что, как и раньше, предполагаем, что receive_framed_message читает все с сокета.

        while (is_running_.load()) {
            std::string framed_payload = client_socket.receive_framed_message(); // Эта функция должна читать длину и затем payload
            if (framed_payload.empty()) {
                if(!client_socket.is_valid() || !is_running_.load()) {
                    // Сообщение не нужно, если это штатное завершение
                } else { 
                    std::cout << get_current_timestamp() << " [CustomProto fd:" << client_fd_for_log << "] Client disconnected or empty message." << std::endl;
                }
                break;
            }
            
            // framed_payload - это ПОЛЕЗНАЯ НАГРУЗКА после удаления заголовка длины.
            // Теперь эту полезную нагрузку нужно передать в process_custom_protocol_command.
            // process_custom_protocol_command должна вернуть ПОЛЕЗНУЮ НАГРУЗКУ ответа.
            std::string response_data_payload = process_custom_protocol_command(framed_payload);
            
            // Перед отправкой, ответную полезную нагрузку нужно ОБРАМИТЬ.
            // Функции create_success_response/create_error_response из process_custom_protocol_command
            // уже должны были это сделать, вернув строку вида "ТИП\nДАННЫЕ", которую затем обрамляет frame_message.
            // Таким образом, response_data_payload от process_custom_protocol_command должна быть уже полностью готовой строкой для отправки.
            if (client_socket.send_data(response_data_payload) <= 0) { 
                if(is_running_.load()) std::cerr << get_current_timestamp() << " [CustomProto fd:" << client_fd_for_log << "] Send failed. Client might have disconnected." << std::endl;
                break;
            }
        }
    } catch (const SocketException& se) {
        if(is_running_.load()) std::cerr << get_current_timestamp() << " [CustomProto fd:" << client_fd_for_log << "] SocketException: " << se.what() << std::endl;
    } catch (const std::exception& e) {
        if(is_running_.load()) std::cerr << get_current_timestamp() << " [CustomProto fd:" << client_fd_for_log << "] Exception: " << e.what() << std::endl;
    }
    client_socket.close_socket();
}

// Используем функции из network_protocol.h
std::string DBServer::process_custom_protocol_command(const std::string& framed_payload_from_client) {
    std::cout << get_current_timestamp() << " [CustomProto Logic] Processing command payload: " << framed_payload_from_client << std::endl;
    
    // framed_payload_from_client здесь - это то, что было между заголовком длины.
    // Оно должно содержать "ТИП_КОМАНДЫ\nДАННЫЕ_КОМАНДЫ"
    // Используем parse_message_payload для разбора этого:
    ParsedMessage parsed_msg = parse_message_payload(framed_payload_from_client);

    if (!parsed_msg.valid) {
        std::cerr << get_current_timestamp() << " [CustomProto Logic] Invalid message format: " << parsed_msg.payload_data << std::endl;
        // Возвращаем ошибку, обрамленную с типом ERROR_RESPONSE
        return create_response_string(CommandType::ERROR_RESPONSE, "Invalid message format received by server: " + parsed_msg.payload_data);
    }

    std::cout << get_current_timestamp() << " [CustomProto Logic] Parsed CommandType: " 
              << static_cast<int>(parsed_msg.type) << ", Payload: " << parsed_msg.payload_data << std::endl;

    // Здесь должна быть логика разбора parsed_msg.type и parsed_msg.payload_data
    switch (parsed_msg.type) {
        case CommandType::PING:
            // Для PING payload обычно пустой, отвечаем ACK_PONG (или SUCCESS_RESPONSE_DATA с "PONG")
            return create_response_string(CommandType::ACK_PONG, "PONG from server");
        
        case CommandType::EXECUTE_QUERY:
            // Пример: payload_data содержит SQL-подобный запрос "SELECT * WHERE ..."
            // Здесь должна быть логика выполнения запроса к БД database_
            // и формирование ответа.
            // if (database_.execute(parsed_msg.payload_data)) {
            //    return create_response_string(CommandType::SUCCESS_RESPONSE_DATA, "Query results...");
            // } else {
            //    return create_response_string(CommandType::ERROR_RESPONSE, "Query execution failed");
            // }
            return create_response_string(CommandType::SUCCESS_RESPONSE_DATA, "EXECUTE_QUERY received: " + parsed_msg.payload_data + " (not fully implemented)");

        case CommandType::EDIT_RECORD_CMD:
            // payload_data для EDIT_RECORD_CMD должен содержать данные для редактирования.
            // Это может быть сложная строка, которую нужно дополнительно парсить.
            // Например, используя parse_edit_payload_for_server_internal из вашего анонимного пространства имен
            // std::optional<ParsedEditCommandInternal> edit_details = parse_edit_payload_for_server_internal(parsed_msg.payload_data);
            // if (edit_details && edit_details->keys_parsed_successfully) { ... }
            return create_response_string(CommandType::SUCCESS_RESPONSE_DATA, "EDIT_RECORD_CMD received (not fully implemented): " + parsed_msg.payload_data);
        
        // Другие типы команд...
        default:
            std::cerr << get_current_timestamp() << " [CustomProto Logic] Unknown or unimplemented command type: " << static_cast<int>(parsed_msg.type) << std::endl;
            return create_response_string(CommandType::ERROR_RESPONSE, "Unknown or unimplemented command type received by server");
    }
}


void DBServer::http_client_handler_func(Network::TCPSocket client_socket, socket_t client_fd_for_log, std::string initial_data) {
    // ... (эта функция остается такой же, как в моем предыдущем полном ответе, с вызовом process_http_request) ...
    // Копирую ее полностью для ясности:
    std::cout << get_current_timestamp() << " [Server HTTP fd:" << client_fd_for_log << "] Handler started." << std::endl;
    std::string http_response_str_local;
    std::string raw_http_request_data_local = initial_data;

    try {
        size_t headers_end_pos = raw_http_request_data_local.find("\r\n\r\n");
        if (headers_end_pos == std::string::npos) headers_end_pos = raw_http_request_data_local.find("\n\n");

        if (headers_end_pos != std::string::npos) {
            std::string headers_part = raw_http_request_data_local.substr(0, headers_end_pos);
            std::string body_part_initial = raw_http_request_data_local.substr(headers_end_pos + (raw_http_request_data_local.find("\r\n\r\n") == headers_end_pos ? 4 : 2) );

            std::string lower_headers = headers_part;
            std::transform(lower_headers.begin(), lower_headers.end(), lower_headers.begin(), ::tolower);
            size_t cl_pos = lower_headers.find("content-length:");
            if (cl_pos != std::string::npos) {
                size_t cl_val_start = cl_pos + strlen("content-length:");
                size_t cl_val_end = headers_part.find_first_of("\r\n", cl_val_start);
                if (cl_val_end == std::string::npos) cl_val_end = headers_part.length();

                std::string cl_val_str = trim_string(headers_part.substr(cl_val_start, cl_val_end - cl_val_start));
                try {
                    long long content_length = std::stoll(cl_val_str);
                    if (content_length > 0 && static_cast<long long>(body_part_initial.length()) < content_length) {
                        long long remaining_to_read = content_length - body_part_initial.length();
                        std::vector<char> body_buffer(remaining_to_read);
                        int body_bytes_read = 0;
                        int current_read;
                        #ifdef _WIN32
                            DWORD timeout_ms_body = 2000;
                            setsockopt(client_socket.get_raw_socket(), SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms_body, sizeof(timeout_ms_body));
                        #else
                            struct timeval tv_body; tv_body.tv_sec = 2; tv_body.tv_usec = 0;
                            setsockopt(client_socket.get_raw_socket(), SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv_body, sizeof tv_body);
                        #endif

                        while(body_bytes_read < remaining_to_read) {
                            #ifdef _WIN32
                                current_read = recv(client_socket.get_raw_socket(), body_buffer.data() + body_bytes_read, static_cast<int>(remaining_to_read - body_bytes_read), 0);
                            #else
                                current_read = read(client_socket.get_raw_socket(), body_buffer.data() + body_bytes_read, remaining_to_read - body_bytes_read);
                            #endif
                            if (current_read <= 0) {
                                std::cerr << get_current_timestamp() << " [Server HTTP fd:" << client_fd_for_log << "] Error or connection closed while reading POST body. Read " << body_bytes_read << "/" << remaining_to_read << std::endl;
                                break;
                            }
                            body_bytes_read += current_read;
                        }
                        raw_http_request_data_local.append(body_buffer.data(), body_bytes_read);
                    }
                } catch (const std::exception& e_stoll) {
                    std::cerr << get_current_timestamp() << " [Server HTTP fd:" << client_fd_for_log << "] Invalid Content-Length: " << cl_val_str << " (" << e_stoll.what() << ")" << std::endl;
                }
            }
        } else {
            if(raw_http_request_data_local.rfind("GET ",0) !=0 ) {
                 std::cerr << get_current_timestamp() << " [Server HTTP fd:" << client_fd_for_log << "] Incomplete HTTP headers received." << std::endl;
                 std::string err_body_incomplete = "<h1>400 Bad Request</h1><p>Incomplete HTTP headers.</p>";
                 std::stringstream r_incomplete;
                 r_incomplete << "HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\nContent-Length: " << err_body_incomplete.length() << "\r\nConnection: close\r\n\r\n" << err_body_incomplete;
                 http_response_str_local = r_incomplete.str();
                 if (!http_response_str_local.empty()) client_socket.send_data(http_response_str_local);
                 client_socket.close_socket();
                 return;
            }
        }

        ParsedHttpRequest http_req = ParsedHttpRequest::parse(raw_http_request_data_local);
        if (http_req.is_valid) {
            std::string actual_response_body = process_http_request(http_req); // Вызов правильной функции

            std::stringstream http_response_ss_local;
            http_response_ss_local << (http_req.http_version.empty() ? "HTTP/1.1" : http_req.http_version) << " 200 OK\r\n";
            http_response_ss_local << "Content-Type: text/html; charset=utf-8\r\n";
            http_response_ss_local << "Content-Length: " << actual_response_body.length() << "\r\n";
            http_response_ss_local << "Connection: close\r\n\r\n";
            http_response_ss_local << actual_response_body;
            http_response_str_local = http_response_ss_local.str();
        } else {
            std::string err_body = "<h1>400 Bad Request</h1><p>Could not parse HTTP request properly. Check request format.</p>";
            std::stringstream r_local; r_local << "HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\nContent-Length: " << err_body.length() << "\r\nConnection: close\r\n\r\n" << err_body; http_response_str_local = r_local.str();
        }

        if (!http_response_str_local.empty()) {
            client_socket.send_data(http_response_str_local);
        }

    } catch (const SocketException& se_http) {
         if(is_running_.load()) std::cerr << get_current_timestamp() << " [Server HTTP fd:" << client_fd_for_log << "] SocketException: " << se_http.what() << std::endl;
         if(client_socket.is_valid() && is_running_.load()){
            std::string err_body_se = "<h1>500 Internal Server Error</h1><p>Socket processing error on server.</p>";
            std::stringstream r_se; r_se << "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/html\r\nContent-Length: " << err_body_se.length() << "\r\nConnection: close\r\n\r\n" << err_body_se;
            if (client_socket.is_valid()) client_socket.send_data(r_se.str());
         }
    } catch (const std::exception& e_http) {
        if(is_running_.load()) std::cerr << get_current_timestamp() << " [Server HTTP fd:" << client_fd_for_log << "] Exception: " << e_http.what() << std::endl;
        if (client_socket.is_valid() && is_running_.load()){
            std::string err_body_e = "<h1>500 Internal Server Error</h1><p>An internal server exception occurred: " + std::string(e_http.what()) + "</p>";
            std::stringstream r_e; r_e << "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/html\r\nContent-Length: " << err_body_e.length() << "\r\nConnection: close\r\n\r\n" << err_body_e;
            if (client_socket.is_valid()) client_socket.send_data(r_e.str());
        }
    }
    client_socket.close_socket();
}

// Эта функция (process_http_request) и последующие (generate_html_form и т.д.) остаются без изменений
// по сравнению с тем, что я предоставлял в прошлый раз, так как они не использовали NetworkProtocol напрямую.
// Копирую их для полноты.

std::string DBServer::process_http_request(const ParsedHttpRequest& http_req) {
    std::cout << get_current_timestamp() << " [Server HTTP Logic] Processing " << http_req.method << " for " << http_req.path << std::endl;
    std::string response_body_content_str_logic;
    std::string status_message_html_logic;
    std::map<std::string, std::string> params_to_use_http_logic;

    if (http_req.method == "GET" && http_req.path == "/") {
        return generate_html_form();
    }
    else if (http_req.path == "/query") {
        if (http_req.method == "GET") {
            params_to_use_http_logic = http_req.query_params;
            auto q_it_logic = params_to_use_http_logic.find("query");
            if (q_it_logic != params_to_use_http_logic.end()) {
                std::string db_command_str_http_logic = q_it_logic->second;
                std::cout << get_current_timestamp() << " [Server HTTP Logic] DB Command (GET): " << db_command_str_http_logic << std::endl;
                auto pq_opt_get_logic = QueryParser::parse_select(db_command_str_http_logic);
                if (pq_opt_get_logic) {
                    auto tbl_get_logic = database_.get_formatted_select_results(*pq_opt_get_logic);
                    status_message_html_logic = generate_html_table_from_data(tbl_get_logic);
                } else {
                    status_message_html_logic = "<p class='error'>Ошибка парсинга SELECT запроса: " + escape_html_chars_internal(db_command_str_http_logic) + "</p>";
                }
            } else {
                status_message_html_logic = "<p class='error'>Отсутствует параметр 'query' для GET запроса.</p>";
            }
        } else if (http_req.method == "POST") {
            params_to_use_http_logic = http_req.form_params;
            auto cmd_it_logic = params_to_use_http_logic.find("command");
            if (cmd_it_logic == params_to_use_http_logic.end()) {
                status_message_html_logic = "<p class='error'>Отсутствует параметр 'command' для POST запроса.</p>";
            } else {
                std::string cmd_type_logic = cmd_it_logic->second;
                std::cout << get_current_timestamp() << " [Server HTTP Logic] DB Command (POST): " << cmd_type_logic << std::endl;

                if (cmd_type_logic == "add") {
                    auto name_val = params_to_use_http_logic.count("name") ? params_to_use_http_logic.at("name") : "";
                    auto ip_val = params_to_use_http_logic.count("ip") ? params_to_use_http_logic.at("ip") : "";
                    auto date_val = params_to_use_http_logic.count("date") ? params_to_use_http_logic.at("date") : "";
                    auto traffic_val = params_to_use_http_logic.count("traffic") ? params_to_use_http_logic.at("traffic") : "";

                    if (name_val.empty() || ip_val.empty() || date_val.empty() || traffic_val.empty()) {
                         status_message_html_logic = "<p class='error'>Отсутствуют необходимые поля для команды ADD (name, ip, date, traffic).</p>";
                    } else {
                        auto rec_opt_add = parse_add_payload_universal(name_val, ip_val, date_val, traffic_val);
                        if (rec_opt_add) {
                            if (database_.add_record(*rec_opt_add)) status_message_html_logic = "<p class='success'>Запись успешно добавлена!</p>";
                            else status_message_html_logic = "<p class='error'>Ошибка добавления записи (возможно, дубликат или неверные данные).</p>";
                        } else {
                             status_message_html_logic = "<p class='error'>Ошибка парсинга данных для ADD: проверьте формат IP, даты или трафика.</p>";
                        }
                    }
                } else if (cmd_type_logic == "delete") {
                    auto c_it_logic = params_to_use_http_logic.find("conditions");
                    if (c_it_logic!=params_to_use_http_logic.end() && !trim_string(c_it_logic->second).empty()) {
                        std::string dq_del_logic = "SELECT * WHERE " + trim_string(c_it_logic->second);
                        auto pq_opt_del_logic = QueryParser::parse_select(dq_del_logic);
                        if (pq_opt_del_logic) {
                           size_t cnt_del_logic = database_.delete_records(*pq_opt_del_logic);
                           status_message_html_logic = "<p class='success'>" + std::to_string(cnt_del_logic) + " запись(ей) удалено.</p>";
                        } else {
                            status_message_html_logic = "<p class='error'>Ошибка парсинга условий для DELETE: " + escape_html_chars_internal(c_it_logic->second) + "</p>";
                        }
                    } else {
                        status_message_html_logic = "<p class='error'>Отсутствуют или пусты 'conditions' для команды DELETE.</p>";
                    }
                }
                else if (cmd_type_logic == "edit") {
                    auto key_name_it_logic = params_to_use_http_logic.find("key_name");
                    auto key_ip_it_logic = params_to_use_http_logic.find("key_ip");
                    auto key_date_it_logic = params_to_use_http_logic.find("key_date");

                    if (key_name_it_logic == params_to_use_http_logic.end() || key_name_it_logic->second.empty() ||
                        key_ip_it_logic == params_to_use_http_logic.end() || key_ip_it_logic->second.empty() ||
                        key_date_it_logic == params_to_use_http_logic.end() || key_date_it_logic->second.empty()) {
                        status_message_html_logic = "<p class='error'>Не указаны все ключевые поля (key_name, key_ip, key_date) для идентификации записи EDIT.</p>";
                    } else {
                        auto key_ip_obj_logic = IpAddress::from_string(key_ip_it_logic->second);
                        auto key_date_obj_logic = Date::from_string(key_date_it_logic->second);

                        if (!key_ip_obj_logic) status_message_html_logic = "<p class='error'>Неверный формат key_ip для EDIT: " + escape_html_chars_internal(key_ip_it_logic->second) + "</p>";
                        else if (!key_date_obj_logic) status_message_html_logic = "<p class='error'>Неверный формат key_date для EDIT: " + escape_html_chars_internal(key_date_it_logic->second) + "</p>";
                        else {
                            SelectQuery find_query_http_logic;
                            find_query_http_logic.criteria.push_back({"", "name", Condition::EQ, key_name_it_logic->second});
                            find_query_http_logic.criteria.push_back({"", "ip", Condition::EQ, *key_ip_obj_logic});
                            find_query_http_logic.criteria.push_back({"", "date", Condition::EQ, *key_date_obj_logic});
                            find_query_http_logic.select_fields.push_back("*");

                            std::vector<ProviderRecord> found_records_http_logic = database_.select_records(find_query_http_logic);

                            if (found_records_http_logic.empty()) {
                                status_message_html_logic = "<p class='error'>Запись для редактирования не найдена по указанным ключам.</p>";
                            } else {
                                ProviderRecord new_record_data_http_logic = found_records_http_logic[0];
                                bool changed_http_logic = false;
                                bool parse_error_in_set_fields_http_logic = false;

                                auto set_name_it_http_logic = params_to_use_http_logic.find("set_name");
                                if (set_name_it_http_logic != params_to_use_http_logic.end() && !set_name_it_http_logic->second.empty()) {
                                    new_record_data_http_logic.full_name = set_name_it_http_logic->second; changed_http_logic = true;
                                }
                                auto set_ip_it_http_logic = params_to_use_http_logic.find("set_ip");
                                if (set_ip_it_http_logic != params_to_use_http_logic.end() && !set_ip_it_http_logic->second.empty()) {
                                    auto set_ip_obj_http_logic = IpAddress::from_string(set_ip_it_http_logic->second);
                                    if (!set_ip_obj_http_logic) { status_message_html_logic = "<p class='error'>Неверный формат нового IP (set_ip): " + escape_html_chars_internal(set_ip_it_http_logic->second) + "</p>"; parse_error_in_set_fields_http_logic = true;}
                                    else { new_record_data_http_logic.ip_address = *set_ip_obj_http_logic; changed_http_logic = true;}
                                }
                                auto set_date_it_http_logic = params_to_use_http_logic.find("set_date");
                                if (!parse_error_in_set_fields_http_logic && set_date_it_http_logic != params_to_use_http_logic.end() && !set_date_it_http_logic->second.empty()) {
                                    auto set_date_obj_http_logic = Date::from_string(set_date_it_http_logic->second);
                                    if (!set_date_obj_http_logic) { status_message_html_logic = "<p class='error'>Неверный формат новой Даты (set_date): " + escape_html_chars_internal(set_date_it_http_logic->second) + "</p>"; parse_error_in_set_fields_http_logic = true; }
                                    else { new_record_data_http_logic.record_date = *set_date_obj_http_logic; changed_http_logic = true;}
                                }
                                auto set_traffic_it_http_logic = params_to_use_http_logic.find("set_traffic");
                                if (!parse_error_in_set_fields_http_logic && set_traffic_it_http_logic != params_to_use_http_logic.end() && !set_traffic_it_http_logic->second.empty()) {
                                    auto temp_rec_opt_traffic = parse_add_payload_universal("dummy","1.1.1.1","01/01/2000", set_traffic_it_http_logic->second );
                                    if (!temp_rec_opt_traffic) { status_message_html_logic = "<p class='error'>Неверный формат нового трафика (set_traffic).</p>"; parse_error_in_set_fields_http_logic = true; }
                                    else { new_record_data_http_logic.hourly_traffic = temp_rec_opt_traffic->hourly_traffic; changed_http_logic = true;}
                                }

                                if (!parse_error_in_set_fields_http_logic) {
                                     bool no_set_fields_provided = true;
                                     if(set_name_it_http_logic != params_to_use_http_logic.end() && !set_name_it_http_logic->second.empty()) no_set_fields_provided = false;
                                     if(set_ip_it_http_logic != params_to_use_http_logic.end() && !set_ip_it_http_logic->second.empty()) no_set_fields_provided = false;
                                     if(set_date_it_http_logic != params_to_use_http_logic.end() && !set_date_it_http_logic->second.empty()) no_set_fields_provided = false;
                                     if(set_traffic_it_http_logic != params_to_use_http_logic.end() && !set_traffic_it_http_logic->second.empty()) no_set_fields_provided = false;

                                    if (no_set_fields_provided) status_message_html_logic = "<p>Нет изменений для применения к записи (все новые поля пустые).</p>";
                                    else if (!changed_http_logic && !no_set_fields_provided) status_message_html_logic = "<p>Нет фактических изменений для применения (новые значения совпадают со старыми или не были предоставлены валидные значения для изменения).</p>";
                                    else if (!new_record_data_http_logic.is_valid()) status_message_html_logic = "<p class='error'>Данные отредактированной записи невалидны. Изменения не применены.</p>";
                                    else {
                                        int update_status_http_logic = database_.update_record(
                                            key_name_it_logic->second, *key_ip_obj_logic, *key_date_obj_logic,
                                            new_record_data_http_logic
                                        );
                                        if (update_status_http_logic == 0) status_message_html_logic = "<p class='success'>Запись успешно обновлена!</p>";
                                        else if (update_status_http_logic == -1) status_message_html_logic = "<p class='error'>Ошибка обновления: Запись для редактирования не найдена (возможно, удалена в процессе).</p>";
                                        else if (update_status_http_logic == -2) status_message_html_logic = "<p class='error'>Ошибка обновления: Новые идентификационные поля конфликтуют с другой записью.</p>";
                                        else if (update_status_http_logic == -3) status_message_html_logic = "<p class='error'>Ошибка обновления: Новые данные записи невалидны.</p>";
                                        else status_message_html_logic = "<p class='error'>Ошибка обновления: Неизвестная ошибка базы данных (" + std::to_string(update_status_http_logic) + ").</p>";
                                    }
                                }
                            }
                        }
                    }
                }
                else if (cmd_type_logic == "calculate_bill") {
                    auto start_date_it_logic = params_to_use_http_logic.find("start_date");
                    auto end_date_it_logic = params_to_use_http_logic.find("end_date");
                    auto conditions_it_logic = params_to_use_http_logic.find("bill_query_conditions");

                    if (start_date_it_logic == params_to_use_http_logic.end() || start_date_it_logic->second.empty() ||
                        end_date_it_logic == params_to_use_http_logic.end() || end_date_it_logic->second.empty()) {
                        status_message_html_logic = "<p class='error'>Не указаны начальная или конечная дата для расчета счета.</p>";
                    } else {
                        auto start_date_obj_logic = Date::from_string(start_date_it_logic->second);
                        auto end_date_obj_logic = Date::from_string(end_date_it_logic->second);

                        if (!start_date_obj_logic) status_message_html_logic = "<p class='error'>Неверный формат начальной даты: " + escape_html_chars_internal(start_date_it_logic->second) + "</p>";
                        else if (!end_date_obj_logic) status_message_html_logic = "<p class='error'>Неверный формат конечной даты: " + escape_html_chars_internal(end_date_it_logic->second) + "</p>";
                        else if (*end_date_obj_logic < *start_date_obj_logic) status_message_html_logic = "<p class='error'>Конечная дата не может быть раньше начальной.</p>";
                        else {
                            std::string target_query_conditions_str_logic = (conditions_it_logic != params_to_use_http_logic.end()) ? trim_string(conditions_it_logic->second) : "";
                            std::string full_target_query_str_bill_logic = "SELECT *";
                            if (!target_query_conditions_str_logic.empty()){
                                std::string temp_lower_cond_logic = to_lower_util(target_query_conditions_str_logic);
                                if (temp_lower_cond_logic.rfind("select ", 0) == 0) {
                                    full_target_query_str_bill_logic = target_query_conditions_str_logic;
                                } else {
                                     full_target_query_str_bill_logic += " WHERE " + target_query_conditions_str_logic;
                                }
                            }

                            auto target_query_opt_bill_logic = QueryParser::parse_select(full_target_query_str_bill_logic);
                            if (!target_query_opt_bill_logic) {
                                status_message_html_logic = "<p class='error'>Ошибка парсинга условий выборки для счета: " + escape_html_chars_internal(target_query_conditions_str_logic) + "</p>";
                            } else {
                                auto bill_amount_opt_logic = database_.calculate_bill(*target_query_opt_bill_logic, *start_date_obj_logic, *end_date_obj_logic, tariff_plan_);
                                if (bill_amount_opt_logic) {
                                    std::ostringstream bill_ss_logic;
                                    bill_ss_logic << std::fixed << std::setprecision(2) << *bill_amount_opt_logic;
                                    status_message_html_logic = "<p class='success'>Расчетный счет: <strong>" + bill_ss_logic.str() + "</strong></p>"
                                                                + "<p>Для записей, соответствующих: <code>" + escape_html_chars_internal(full_target_query_str_bill_logic) + "</code></p>"
                                                                + "<p>За период: с " + escape_html_chars_internal(start_date_it_logic->second) + " по " + escape_html_chars_internal(end_date_it_logic->second) + "</p>";
                                } else {
                                    status_message_html_logic = "<p class='error'>Не удалось рассчитать счет (возможно, нет данных за период или тариф не загружен).</p>";
                                }
                            }
                        }
                    }
                }
                 else if (cmd_type_logic == "load_tariff") {
                    auto filename_it_logic = params_to_use_http_logic.find("tariff_filename_http");
                    if (filename_it_logic == params_to_use_http_logic.end() || filename_it_logic->second.empty()) {
                        status_message_html_logic = "<p class='error'>Не указано имя файла для команды LOAD_TARIFF.</p>";
                    } else {
                        std::string client_tariff_filename_logic = filename_it_logic->second;
                        if (client_tariff_filename_logic.find('/') != std::string::npos ||
                            client_tariff_filename_logic.find('\\') != std::string::npos ||
                            !is_valid_simple_filename(client_tariff_filename_logic) ) {
                            status_message_html_logic = "<p class='error'>Недопустимое имя файла тарифа: " + escape_html_chars_internal(client_tariff_filename_logic) + ". Должно быть простое имя файла.</p>";
                        } else {
                            std::filesystem::path base_tariff_path_logic(default_tariff_filename_);
                            std::filesystem::path full_tariff_path_to_load_logic = base_tariff_path_logic.parent_path() / client_tariff_filename_logic;

                            std::cout << get_current_timestamp() << " [Server HTTP Logic] Attempting to LOAD_TARIFF from: " << full_tariff_path_to_load_logic.string() << std::endl;

                            if (!is_valid_cmd_argument_path(full_tariff_path_to_load_logic.string())) {
                                 status_message_html_logic = "<p class='error'>Сконструированный путь к файлу тарифа невалиден: " + escape_html_chars_internal(full_tariff_path_to_load_logic.string()) + "</p>";
                            } else if (tariff_plan_.load_from_file(full_tariff_path_to_load_logic.string())) {
                                status_message_html_logic = "<p class='success'>Тарифный план успешно загружен из '" + escape_html_chars_internal(client_tariff_filename_logic) + "' (серверный путь: " + escape_html_chars_internal(full_tariff_path_to_load_logic.string()) + ").</p>";
                            } else {
                                status_message_html_logic = "<p class='error'>Не удалось загрузить тарифный план из '" + escape_html_chars_internal(client_tariff_filename_logic) + "' (серверный путь: " + escape_html_chars_internal(full_tariff_path_to_load_logic.string()) + "). Проверьте файл.</p>";
                            }
                        }
                    }
                }
                else {
                    status_message_html_logic = "<p class='error'>Неподдерживаемый тип POST команды: " + escape_html_chars_internal(cmd_type_logic) + "</p>";
                }
            }
        } else {
             status_message_html_logic = "<p class='error'>Неподдерживаемый HTTP метод: " + escape_html_chars_internal(http_req.method) + "</p>";
        }
        response_body_content_str_logic = generate_html_response_page("Результат Запроса", status_message_html_logic);
    } else {
        response_body_content_str_logic = generate_html_response_page("404 Не найдено", "<p>Путь '" + escape_html_chars_internal(http_req.path) + "' не найден.</p>");
    }
    return response_body_content_str_logic;
}

std::string DBServer::generate_html_form() {
    // ... (как было)
    std::stringstream html;
    html << "<!DOCTYPE html><html lang='ru'><head><meta charset='UTF-8'><title>DB Query</title>"
         << "<style>body{font-family:Arial,sans-serif;margin:20px auto;max-width:900px;background-color:#f0f2f5;color:#333;padding:10px;}h1,h2{color:#2c3e50;text-align:center;}form, .results{background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 10px rgba(0,0,0,0.1);margin-bottom:30px;}label{display:block;margin-top:15px;margin-bottom:5px;font-weight:bold;color:#555;}input[type='text'],textarea,select{width:calc(100% - 22px);padding:10px;margin-bottom:10px;border:1px solid #ccc;border-radius:4px;box-sizing:border-box;font-size:14px;}textarea{resize:vertical;min-height:60px;}input[type='submit']{background-color:#3498db;color:white;padding:12px 20px;border:none;border-radius:4px;cursor:pointer;font-size:16px;transition:background-color 0.2s;}input[type='submit']:hover{background-color:#2980b9;}.result-table{width:100%;border-collapse:collapse;margin-top:20px;}.result-table th,.result-table td{border:1px solid #ddd;padding:10px;text-align:left;font-size:14px;}.result-table th{background-color:#3498db;color:#fff;}.result-table tr:nth-child(even){background-color:#ecf0f1;}.error{color:#e74c3c;font-weight:bold;background-color:#fdd;padding:10px;border-radius:4px;border-left:5px solid #e74c3c;}.success{color:#2ecc71;font-weight:bold;background-color:#e6ffed;padding:10px;border-radius:4px;border-left:5px solid #2ecc71;} fieldset{border:1px solid #ddd; padding:15px; margin-top:20px; border-radius:5px;} legend{font-weight:bold; color:#333; padding:0 10px;}</style>"
         << "</head><body><h1>Интерфейс Базы Данных Интернет Провайдера</h1>";

    html << "<fieldset><legend>SELECT Запрос</legend><form action='/query' method='get'>"
         << "<label for='s_query'>SQL-like SELECT (например, <code>SELECT name, ip WHERE date = 01/01/2024 SORT BY name ASC</code>):</label><textarea id='s_query' name='query' rows='3' placeholder='SELECT *'>SELECT *</textarea><br>"
         << "<input type='submit' value='Выполнить SELECT'></form></fieldset>";

    html << "<fieldset><legend>ADD Запись</legend><form action='/query' method='post'>"
         << "<input type='hidden' name='command' value='add'>"
         << "<label for='a_name'>ФИО:</label><input type='text' id='a_name' name='name' required><br>"
         << "<label for='a_ip'>IP (A.B.C.D):</label><input type='text' id='a_ip' name='ip' required placeholder='1.2.3.4'><br>"
         << "<label for='a_date'>Дата (ДД/ММ/ГГГГ):</label><input type='text' id='a_date' name='date' required placeholder='01/01/2024'><br>"
         << "<label for='a_traffic'>Почасовой трафик (48 чисел: Вх0 Исх0 ... Вх23 Исх23):</label><input type='text' id='a_traffic' name='traffic' required placeholder='0 0 10 5 ...'><br>"
         << "<input type='submit' value='Добавить Запись'></form></fieldset>";

    html << "<fieldset><legend>EDIT Запись</legend><form action='/query' method='post'>"
         << "<input type='hidden' name='command' value='edit'>"
         << "<h4>Идентификация записи (обязательно):</h4>"
         << "<label for='e_key_name'>Текущее ФИО:</label><input type='text' id='e_key_name' name='key_name' required><br>"
         << "<label for='e_key_ip'>Текущий IP (A.B.C.D):</label><input type='text' id='e_key_ip' name='key_ip' required placeholder='1.2.3.4'><br>"
         << "<label for='e_key_date'>Текущая Дата (ДД/ММ/ГГГГ):</label><input type='text' id='e_key_date' name='key_date' required placeholder='01/01/2024'><br>"
         << "<h4>Новые значения (оставьте пустым, если не меняете):</h4>"
         << "<label for='e_set_name'>Новое ФИО:</label><input type='text' id='e_set_name' name='set_name'><br>"
         << "<label for='e_set_ip'>Новый IP (A.B.C.D):</label><input type='text' id='e_set_ip' name='set_ip' placeholder='1.2.3.5'><br>"
         << "<label for='e_set_date'>Новая Дата (ДД/ММ/ГГГГ):</label><input type='text' id='e_set_date' name='set_date' placeholder='02/02/2024'><br>"
         << "<label for='e_set_traffic'>Новый почасовой трафик (48 чисел или пусто):</label><input type='text' id='e_set_traffic' name='set_traffic' placeholder='1 1 2 2 ...'><br>"
         << "<input type='submit' value='Редактировать Запись'></form></fieldset>";

    html << "<fieldset><legend>DELETE Записи</legend><form action='/query' method='post'>"
         << "<input type='hidden' name='command' value='delete'>"
         << "<label for='d_cond'>Условия WHERE для DELETE (например, <code>name = \"Test User\"</code>):</label><input type='text' id='d_cond' name='conditions' required><br>"
         << "<input type='submit' value='Удалить Записи'></form></fieldset>";

    html << "<fieldset><legend>CALCULATE_BILL (Расчет счета)</legend><form action='/query' method='post'>"
         << "<input type='hidden' name='command' value='calculate_bill'>"
         << "<label for='cb_start_date'>Начальная дата (ДД/ММ/ГГГГ):</label><input type='text' id='cb_start_date' name='start_date' required placeholder='01/01/2024'><br>"
         << "<label for='cb_end_date'>Конечная дата (ДД/ММ/ГГГГ):</label><input type='text' id='cb_end_date' name='end_date' required placeholder='31/01/2024'><br>"
         << "<label for='cb_conditions'>Условия WHERE для выборки (пусто для всех):</label><input type='text' id='cb_conditions' name='bill_query_conditions' placeholder='name = \"Alice Wonderland\"'><br>"
         << "<input type='submit' value='Рассчитать Счет'></form></fieldset>";

    html << "<fieldset><legend>LOAD_TARIFF (Загрузка тарифного плана)</legend><form action='/query' method='post'>"
         << "<input type='hidden' name='command' value='load_tariff'>"
         << "<label for='lt_filename'>Имя файла тарифа (например, <code>tariff_alt.cfg</code>):</label>"
         << "<input type='text' id='lt_filename' name='tariff_filename_http' required placeholder='tariff_alt.cfg'><br>"
         << "<input type='submit' value='Загрузить Тариф'></form></fieldset>";

    html << "</body></html>"; return html.str();
}

std::string DBServer::generate_html_response_page(const std::string& title, const std::string& body_content) {
    // ... (как было)
    std::stringstream html;
    html << "<!DOCTYPE html><html lang='ru'><head><meta charset='UTF-8'><title>" << escape_html_chars_internal(title) << "</title>"
         << "<style>body{font-family:Arial,sans-serif;margin:20px auto;max-width:900px;background-color:#f0f2f5;color:#333;padding:10px;}h1{color:#2c3e50;text-align:center;} .content, .results{background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 10px rgba(0,0,0,0.1);margin-bottom:20px;}.result-table{width:100%;border-collapse:collapse;margin-top:20px;}.result-table th,.result-table td{border:1px solid #ddd;padding:10px;text-align:left;font-size:14px;}.result-table th{background-color:#3498db;color:#fff;}.result-table tr:nth-child(even){background-color:#ecf0f1;}a{color:#3498db;text-decoration:none;font-weight:bold;}a:hover{text-decoration:underline;}.error{color:#e74c3c;font-weight:bold;background-color:#fdd;padding:10px;border-radius:4px;border-left:5px solid #e74c3c;}.success{color:#2ecc71;font-weight:bold;background-color:#e6ffed;padding:10px;border-radius:4px;border-left:5px solid #2ecc71;}</style>"
         << "</head><body><h1>" << escape_html_chars_internal(title) << "</h1><div class='content'>" << body_content << "</div><p><a href='/'>Вернуться к форме запросов</a></p></body></html>"; return html.str();
}

std::string DBServer::generate_html_table_from_data(const std::vector<std::vector<std::string>>& table_data) {
    // ... (как было)
    if (table_data.empty() || (table_data.size() == 1 && (table_data[0].empty() || (table_data[0].size()==1 && table_data[0][0].rfind("[N/A",0) == 0 ) ) )) {
         return "<p>Нет данных для отображения в таблице или запрошенные поля не найдены.</p>";
    }
    std::stringstream table_html_ss;

    bool header_present_and_has_content_table = false;
    if (!table_data.empty() && !table_data[0].empty()) {
        for(const auto& h_cell_table : table_data[0]) {
            if (h_cell_table.rfind("[N/A", 0) != 0) {
                header_present_and_has_content_table = true;
                break;
            }
        }
    }

    if (!header_present_and_has_content_table && table_data.size() <=1) {
        return "<p>Нет данных для отображения (возможно, неверные поля в SELECT или нет записей).</p>";
    }

    table_html_ss << "<table class='result-table'><thead><tr>";
    if (header_present_and_has_content_table){
        for (const auto& h_cell_table_content : table_data[0]) table_html_ss << "<th>" << escape_html_chars_internal(h_cell_table_content) << "</th>";
    } else if (!table_data.empty() && table_data.size() > 1 && !table_data[1].empty()) {
        for (size_t k_idx=0; k_idx < table_data[1].size(); ++k_idx) table_html_ss << "<th>Поле " << k_idx+1 << "</th>";
    } else {
        table_html_ss << "<th>Данные</th>";
    }
    table_html_ss << "</tr></thead><tbody>";

    size_t start_row_idx_table = header_present_and_has_content_table ? 1 : 0;
    if (table_data.size() <= start_row_idx_table) {
         table_html_ss << "<tr><td colspan='" << (header_present_and_has_content_table && !table_data[0].empty() ? table_data[0].size() : 1)  << "'>Нет записей, соответствующих вашему запросу.</td></tr>";
    } else {
        for (size_t i_row = start_row_idx_table; i_row < table_data.size(); ++i_row) {
            table_html_ss << "<tr>";
            for (const auto& cell_content : table_data[i_row]) {
                table_html_ss << "<td>" << escape_html_chars_internal(cell_content) << "</td>";
            }
            table_html_ss << "</tr>";
        }
    }
    table_html_ss << "</tbody></table>"; return table_html_ss.str();
}
