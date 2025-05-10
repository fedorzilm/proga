#include "db_client.h"
#include "../network/network_protocol.h" // Для CommandType и функций протокола
#include "../common_defs.h"  
#include "../date.h"             
#include "../ip_address.h"       
// provider_record.h не нужен здесь напрямую, если интерактивные хелперы только конструируют строки

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip> 
#include <vector>  
#include <algorithm> // Для std::max, std::transform
#include <thread>      
#include <chrono>      
#include <limits>    // Для std::numeric_limits

namespace { 
    // Вспомогательная функция для разделения строки по разделителю
    std::vector<std::string> split_string_client_internal(const std::string& str_to_split, char delimiter_char) {
        std::vector<std::string> tokens_vec;
        if (str_to_split.empty() && delimiter_char != '\n') {
            return tokens_vec;
        }
        if (str_to_split.empty() && delimiter_char == '\n') {
            // tokens_vec.push_back(""); // Для пустой строки при разделении по '\n' можно вернуть один пустой токен или ничего
            return tokens_vec;           // Пока возвращаем ничего, если исходная строка пуста
        }
        std::string current_token;
        std::istringstream token_stream(str_to_split);
        while (std::getline(token_stream, current_token, delimiter_char)) {
            tokens_vec.push_back(current_token);
        }
        return tokens_vec;
    }

    // Константа для EDIT команды, должна совпадать с серверной
    const std::string EDIT_COMMAND_SEPARATOR_CLIENT = "###SEP###"; 
}

DBClient::DBClient(std::string client_id_val) : client_id_(std::move(client_id_val)) {
    // Network::init_networking(); // Вызывается один раз в client_main
}
DBClient::~DBClient() {
    disconnect(); 
    // Network::cleanup_networking(); // Вызывается один раз в client_main
}

bool DBClient::connect_to_server(const std::string& host, int port) {
    if (socket_.is_valid()) { // Если уже подключены, сначала отключаемся
        disconnect();
    }
    if (!socket_.connect_to(host, port)) {
        std::cerr << get_current_timestamp() << " [" << client_id_ << "] Error: Failed to connect to server " << host << ":" << port << std::endl;
        return false;
    }
    std::cout << get_current_timestamp() << " [" << client_id_ << "] Successfully connected to server " << host << ":" << port << std::endl;
    return true;
}

void DBClient::disconnect() {
    if (socket_.is_valid()) {
        std::cout << get_current_timestamp() << " [" << client_id_ << "] Disconnecting from server..." << std::endl;
        // Можно попытаться отправить "вежливое" сообщение о закрытии, но не обязательно для TCP
        socket_.close_socket();
    }
}

std::pair<bool, std::string> DBClient::send_raw_command(const std::string& command_line) {
    if (!socket_.is_valid()) {
        return {false, "Error: Not connected to server."};
    }
    // Эта команда всегда использует CommandType::EXECUTE_QUERY
    std::string request_to_send = create_request_string(CommandType::EXECUTE_QUERY, command_line);
    
    if (socket_.send_data(request_to_send) <= 0) {
        socket_.close_socket(); 
        return {false, "Error: Failed to send command to server (connection lost?)."};
    }

    std::string server_response_framed = socket_.receive_framed_message();
    if (server_response_framed.empty()) { 
        bool sock_still_valid = socket_.is_valid(); // Проверяем состояние сокета
        socket_.close_socket(); // В любом случае закрываем, если ответа нет или он пустой
        if(!sock_still_valid) return {false, "Error: No response and connection lost while receiving from server."};
        return {false, "Error: Empty framed message received from server (server might have closed connection)."};
    }

    ParsedMessage response_msg = parse_message_payload(server_response_framed);

    if (!response_msg.valid) {
        return {false, "Error: Invalid message format received from server: " + response_msg.payload_data};
    }
    
    if (response_msg.type == CommandType::ERROR_RESPONSE) {
        return {false, "Server Error: " + response_msg.payload_data};
    }
    
    return {true, response_msg.payload_data}; 
}

std::pair<bool, std::string> DBClient::send_edit_command(
    const std::string& key_name, 
    const std::string& key_ip_str, 
    const std::string& key_date_str,
    const std::optional<std::string>& set_name,
    const std::optional<std::string>& set_ip_str,
    const std::optional<std::string>& set_date_str,
    const std::optional<std::string>& set_traffic_as_string) {

    if (!socket_.is_valid()) {
        return {false, "Error: Not connected to server."};
    }

    // Валидация ключевых полей на клиенте (базовая)
    if (key_name.empty() || !IpAddress::from_string(key_ip_str) || !Date::from_string(key_date_str)) {
        return {false, "Client Error: Invalid key fields for EDIT command."};
    }
    // Валидация опциональных полей (если предоставлены)
    if (set_ip_str && !set_ip_str->empty() && !IpAddress::from_string(*set_ip_str)) {
         return {false, "Client Error: Invalid format for new IP in EDIT command."};
    }
    if (set_date_str && !set_date_str->empty() && !Date::from_string(*set_date_str)) {
         return {false, "Client Error: Invalid format for new Date in EDIT command."};
    }
    if (set_traffic_as_string && !set_traffic_as_string->empty()) {
        // Простая проверка на наличие 48 чисел (можно улучшить)
        std::stringstream temp_ss(*set_traffic_as_string); long long temp_ll; size_t count = 0;
        while(temp_ss >> temp_ll) count++;
        if(count != 2 * HOURS_IN_DAY) return {false, "Client Error: Invalid traffic string for EDIT (must be 48 numbers or empty)."};
    }


    std::stringstream payload_ss_edit; // Переименована
    payload_ss_edit << "key_name \"" << key_name << "\" "
                    << "key_ip \"" << key_ip_str << "\" "
                    << "key_date \"" << key_date_str << "\""
                    << EDIT_COMMAND_SEPARATOR_CLIENT; 

    bool set_fields_were_added = false; // Переименована
    if (set_name && !set_name->empty()) { // Проверяем, что optional содержит значение и оно не пустое
        payload_ss_edit << (set_fields_were_added ? " " : "") << "set_name \"" << *set_name << "\"";
        set_fields_were_added = true;
    }
    if (set_ip_str && !set_ip_str->empty()) {
        payload_ss_edit << (set_fields_were_added ? " " : "") << "set_ip \"" << *set_ip_str << "\"";
        set_fields_were_added = true;
    }
    if (set_date_str && !set_date_str->empty()) {
        payload_ss_edit << (set_fields_were_added ? " " : "") << "set_date \"" << *set_date_str << "\"";
        set_fields_were_added = true;
    }
    if (set_traffic_as_string && !set_traffic_as_string->empty()) {
        payload_ss_edit << (set_fields_were_added ? " " : "") << "set_traffic \"" << *set_traffic_as_string << "\"";
        set_fields_were_added = true; // Не использовалась, но для единообразия
    }
    
    std::string request_to_send = create_request_string(CommandType::EDIT_RECORD_CMD, payload_ss_edit.str());
    
    if (socket_.send_data(request_to_send) <= 0) {
        socket_.close_socket(); 
        return {false, "Error: Failed to send EDIT command to server (connection lost?)."};
    }

    std::string server_response_framed = socket_.receive_framed_message();
     if (server_response_framed.empty()) { 
        bool sock_still_valid = socket_.is_valid();
        socket_.close_socket();
        if(!sock_still_valid) return {false, "Error: No response and connection lost after EDIT."};
        return {false, "Error: Empty framed message after EDIT."};
    }

    ParsedMessage response_msg = parse_message_payload(server_response_framed);

    if (!response_msg.valid) {
        return {false, "Error: Invalid server response format after EDIT: " + response_msg.payload_data};
    }
    
    if (response_msg.type == CommandType::ERROR_RESPONSE) {
        return {false, "Server Error on EDIT: " + response_msg.payload_data};
    }
    
    return {true, response_msg.payload_data}; // Ожидаем SUCCESS_RESPONSE_NO_DATA
}


std::pair<bool, std::string> DBClient::send_ping() {
    // ... (реализация как была)
     if (!socket_.is_valid()) {
        return {false, "Error: Not connected to server."};
    }
    std::string request_to_send = create_request_string(CommandType::PING, client_id_ + "_PingDataFromClient"); 
    if (socket_.send_data(request_to_send) <= 0) {
        socket_.close_socket();
        return {false, "Error: Failed to send PING to server."};
    }
    std::string server_response_framed = socket_.receive_framed_message();
    if (server_response_framed.empty()) {
        bool sock_still_valid = socket_.is_valid();
        socket_.close_socket();
        if(!sock_still_valid) return {false, "Error: No PONG and connection lost."};
        return {false, "Error: Empty PONG response."};
    }
    ParsedMessage response_msg = parse_message_payload(server_response_framed);
    if (!response_msg.valid || response_msg.type != CommandType::ACK_PONG) {
        return {false, "Error: Invalid PONG response: " + response_msg.payload_data};
    }
    return {true, response_msg.payload_data};
}

void DBClient::display_formatted_table_response(const std::string& table_data_str_input_disp) { // Переименована
    std::string table_data_str_disp = trim_string(table_data_str_input_disp); // Переименована

    if (table_data_str_disp.empty()) {
        std::cout << "(No data in response payload)" << std::endl;
        return;
    }
    if (table_data_str_disp.find('\n') == std::string::npos && table_data_str_disp.find('\t') == std::string::npos) {
        std::cout << table_data_str_disp << std::endl;
        return;
    }

    std::vector<std::string> rows_disp = split_string_client_internal(table_data_str_disp, '\n'); // Переименована
    if (rows_disp.empty()) {
        std::cout << "(Response was empty or only whitespace)" << std::endl;
        return;
    }
    
    std::vector<std::vector<std::string>> table_cells_disp; // Переименована
    bool has_any_content = false; // Переименована
    for (const auto& row_str_disp : rows_disp) { // Переименована
        if (row_str_disp.empty() && rows_disp.size() == 1 && !has_any_content) continue; 
        table_cells_disp.push_back(split_string_client_internal(row_str_disp, '\t'));
        if (!table_cells_disp.back().empty() && !(table_cells_disp.back().size()==1 && table_cells_disp.back()[0].empty()) ) has_any_content = true;
    }

    if (!has_any_content) { 
         std::cout << (table_data_str_input_disp.empty() ? "(Empty Response)" : table_data_str_input_disp ) << std::endl; 
         return;
    }
    if (table_cells_disp.empty()){
         std::cout << "(No table data parsed from response)" << std::endl;
         return;
    }
    
    std::vector<size_t> col_widths_disp; // Переименована
    // Определяем ширину колонок по самой широкой ячейке в каждой колонке
    // Сначала определяем количество колонок (по первой непустой строке)
    size_t num_cols_disp = 0; // Переименована
    for(const auto& row_cells_item : table_cells_disp){ // Переименована
        if(!row_cells_item.empty()){
            num_cols_disp = row_cells_item.size();
            break;
        }
    }
    if(num_cols_disp == 0 && !table_cells_disp.empty()) { // Если все строки пустые, но есть (например, ["", ""])
        num_cols_disp = table_cells_disp[0].size();
    }


    if (num_cols_disp > 0) col_widths_disp.resize(num_cols_disp, 0);

    for(const auto& row_cells_item_width : table_cells_disp){ // Переименована
        for(size_t i_col = 0; i_col < row_cells_item_width.size() && i_col < col_widths_disp.size(); ++i_col){ // Переименована
            col_widths_disp[i_col] = std::max(col_widths_disp[i_col], row_cells_item_width[i_col].length());
        }
    }

    bool header_printed_disp = false; // Переименована
    for (const auto& row_cells_print : table_cells_disp) { // Переименована
        // Пропускаем полностью пустые строки в середине данных, если такие есть
        bool current_row_is_empty = true;
        for(const auto& cell_check : row_cells_print) if(!cell_check.empty()) current_row_is_empty = false;
        if(current_row_is_empty && table_cells_disp.size() > 1 && header_printed_disp) continue;


        for (size_t i_cell = 0; i_cell < row_cells_print.size() && i_cell < col_widths_disp.size(); ++i_cell) { // Переименована
            std::cout << std::left << std::setw(static_cast<int>(col_widths_disp[i_cell] + 2)) << row_cells_print[i_cell];
        }
        std::cout << "\n";

        if (!header_printed_disp && !col_widths_disp.empty()) { 
            for (size_t width_val : col_widths_disp) { // Переименована
                std::cout << std::string(width_val + 2, '-');
            }
            std::cout << "\n";
            header_printed_disp = true;
        }
    }
    std::cout << std::right << std::setw(0); // Сброс ширины и выравнивания
}

std::optional<std::string> DBClient::build_add_command_from_interactive_input() {
    // ... (реализация как была)
    std::string name_add, ip_str_add, date_str_add, traffic_line_str_add;
    
    std::cout << get_current_timestamp() << " [" << client_id_ << "] Enter Full Name: ";
    if (!std::getline(std::cin, name_add) || name_add.empty()) { 
        if(std::cin.eof()) {std::cout << "\nEOF detected. Add cancelled.\n"; return std::nullopt;}
        std::cerr << get_current_timestamp() << " [" << client_id_ << "] Name cannot be empty. Add cancelled.\n"; return std::nullopt; 
    }

    std::cout << get_current_timestamp() << " [" << client_id_ << "] Enter IP Address (A.B.C.D): ";
    if (!std::getline(std::cin, ip_str_add) || std::cin.eof() || !IpAddress::from_string(ip_str_add)) { 
        if(std::cin.eof() && !IpAddress::from_string(ip_str_add)) {std::cout << "\nEOF detected. Add cancelled.\n"; return std::nullopt;}
        std::cerr << get_current_timestamp() << " [" << client_id_ << "] Invalid IP format or empty. Add cancelled.\n"; return std::nullopt; 
    }
    
    std::cout << get_current_timestamp() << " [" << client_id_ << "] Enter Date (DD/MM/YYYY): ";
    if (!std::getline(std::cin, date_str_add) || std::cin.eof() || !Date::from_string(date_str_add)) { 
        if(std::cin.eof() && !Date::from_string(date_str_add)) {std::cout << "\nEOF detected. Add cancelled.\n"; return std::nullopt;}
        std::cerr << get_current_timestamp() << " [" << client_id_ << "] Invalid Date format or empty. Add cancelled.\n"; return std::nullopt; 
    }

    std::cout << get_current_timestamp() << " [" << client_id_ << "] Enter Hourly Traffic (" << 2*HOURS_IN_DAY 
              << " space-separated non-negative numbers: In0 Out0 ... In23 Out23):\n> ";
    if (!std::getline(std::cin, traffic_line_str_add) || std::cin.eof()){ 
        if(std::cin.eof()) {std::cout << "\nEOF detected. Add cancelled.\n"; return std::nullopt;}
        std::cerr << get_current_timestamp() << " [" << client_id_ << "] Error reading traffic. Add cancelled.\n"; return std::nullopt;
    }
    traffic_line_str_add = trim_string(traffic_line_str_add);
    
    std::stringstream traffic_ss_add_val(traffic_line_str_add); // Переименована
    long long temp_val_check_add; size_t count_check_add = 0; bool traffic_valid_add = true;
    while(traffic_ss_add_val >> temp_val_check_add) {
        if (temp_val_check_add < 0) { traffic_valid_add = false; break; }
        count_check_add++;
    }
    std::string remaining_in_traffic_add; 
    traffic_ss_add_val.clear(); 
    traffic_ss_add_val.seekg(0); 
    for(size_t i_val=0; i_val<count_check_add; ++i_val) traffic_ss_add_val >> temp_val_check_add; // Переименована
    if (traffic_ss_add_val >> remaining_in_traffic_add && !trim_string(remaining_in_traffic_add).empty()) {
        traffic_valid_add = false;
    }

    if (!traffic_valid_add || count_check_add != 2*HOURS_IN_DAY ){
        std::cerr << get_current_timestamp() << " [" << client_id_ << "] Invalid traffic data (must be " << 2*HOURS_IN_DAY 
                  << " non-negative numbers). Got " << count_check_add << ". Add cancelled.\n"; return std::nullopt;
    }

    std::stringstream add_payload_ss_build; // Переименована
    add_payload_ss_build << "name \"" << name_add << "\" "
                         << "ip \"" << ip_str_add << "\" "
                         << "date \"" << date_str_add << "\" "
                         << "traffic \"" << traffic_line_str_add << "\"";
    return "ADD " + add_payload_ss_build.str(); // Возвращаем полную команду ADD
}

std::optional<std::string> DBClient::build_edit_command_payload_from_interactive_input() {
    // ... (реализация как была в предыдущем шаге, но с улучшенной обработкой EOF)
    std::string key_name_str_edit, key_ip_str_edit, key_date_str_edit; // Переименованы
    std::string set_name_str_edit, set_ip_str_edit, set_date_str_edit, set_traffic_line_str_edit; // Переименованы
    
    std::cout << get_current_timestamp() << " [" << client_id_ << "] --- Edit Record ---" << std::endl;
    std::cout << get_current_timestamp() << " [" << client_id_ << "] Enter current Full Name (must not be empty): ";
    if (!std::getline(std::cin, key_name_str_edit) || key_name_str_edit.empty()) { 
        if(std::cin.eof()) {std::cout << "\nEOF detected. Edit cancelled.\n"; return std::nullopt;}
        std::cerr << "Current Name cannot be empty. Edit cancelled.\n"; return std::nullopt; 
    }

    std::cout << get_current_timestamp() << " [" << client_id_ << "] Enter current IP Address (A.B.C.D, must not be empty): ";
    if (!std::getline(std::cin, key_ip_str_edit) || std::cin.eof() || !IpAddress::from_string(key_ip_str_edit)) { 
        if(std::cin.eof() && !IpAddress::from_string(key_ip_str_edit)) {std::cout << "\nEOF detected. Edit cancelled.\n"; return std::nullopt;}
        std::cerr << "Invalid current IP format or empty. Edit cancelled.\n"; return std::nullopt; 
    }
    
    std::cout << get_current_timestamp() << " [" << client_id_ << "] Enter current Date (DD/MM/YYYY, must not be empty): ";
    if (!std::getline(std::cin, key_date_str_edit) || std::cin.eof() || !Date::from_string(key_date_str_edit)) { 
        if(std::cin.eof() && !Date::from_string(key_date_str_edit)) {std::cout << "\nEOF detected. Edit cancelled.\n"; return std::nullopt;}
        std::cerr << "Invalid current Date format or empty. Edit cancelled.\n"; return std::nullopt; 
    }

    std::cout << get_current_timestamp() << " [" << client_id_ << "] --- New Values (leave blank to keep current) ---" << std::endl;
    std::cout << get_current_timestamp() << " [" << client_id_ << "] New Full Name [" << key_name_str_edit << "]: ";
    if(!std::getline(std::cin, set_name_str_edit) && std::cin.eof()) { std::cout << "\nEOF. Edit Cancelled.\n"; return std::nullopt;}

    std::cout << get_current_timestamp() << " [" << client_id_ << "] New IP Address [" << key_ip_str_edit << "]: ";
    if(!std::getline(std::cin, set_ip_str_edit) && std::cin.eof()) { std::cout << "\nEOF. Edit Cancelled.\n"; return std::nullopt;}
    if (!set_ip_str_edit.empty() && !IpAddress::from_string(set_ip_str_edit)) {
        std::cerr << "Invalid new IP format. Edit cancelled.\n"; return std::nullopt;
    }

    std::cout << get_current_timestamp() << " [" << client_id_ << "] New Date [" << key_date_str_edit << "]: ";
    if(!std::getline(std::cin, set_date_str_edit) && std::cin.eof()) { std::cout << "\nEOF. Edit Cancelled.\n"; return std::nullopt;}
     if (!set_date_str_edit.empty() && !Date::from_string(set_date_str_edit)) {
        std::cerr << "Invalid new Date format. Edit cancelled.\n"; return std::nullopt;
    }
    
    std::cout << get_current_timestamp() << " [" << client_id_ << "] New Hourly Traffic (48 numbers or blank to keep current):\n> ";
    if(!std::getline(std::cin, set_traffic_line_str_edit) && std::cin.eof()) { std::cout << "\nEOF. Edit Cancelled.\n"; return std::nullopt;}
    
    if (!set_traffic_line_str_edit.empty()) {
        set_traffic_line_str_edit = trim_string(set_traffic_line_str_edit);
        std::stringstream traffic_ss_val_edit(set_traffic_line_str_edit); // Переименована
        long long temp_val_check_edit; size_t count_check_edit = 0; bool traffic_valid_edit = true;
        while(traffic_ss_val_edit >> temp_val_check_edit) {
            if (temp_val_check_edit < 0) { traffic_valid_edit = false; break; }
            count_check_edit++;
        }
        std::string remaining_in_traffic_edit; 
        traffic_ss_val_edit.clear(); 
        traffic_ss_val_edit.seekg(0); 
        for(size_t i_val_edit=0; i_val_edit<count_check_edit; ++i_val_edit) traffic_ss_val_edit >> temp_val_check_edit; // Переименована
        if (traffic_ss_val_edit >> remaining_in_traffic_edit && !trim_string(remaining_in_traffic_edit).empty()) {
            traffic_valid_edit = false;
        }
        if (!traffic_valid_edit || count_check_edit != 2*HOURS_IN_DAY ){
            std::cerr << "Invalid new traffic data (must be " << 2*HOURS_IN_DAY << " non-negative numbers or blank). Edit cancelled.\n"; return std::nullopt;
        }
    }
    
    std::stringstream final_payload_ss_edit; // Переименована
    final_payload_ss_edit << "key_name \"" << key_name_str_edit << "\" "
                          << "key_ip \"" << key_ip_str_edit << "\" "
                          << "key_date \"" << key_date_str_edit << "\""
                          << EDIT_COMMAND_SEPARATOR_CLIENT; 

    bool set_fields_added_payload_edit = false; // Переименована
    if (!set_name_str_edit.empty()) {
        final_payload_ss_edit << (set_fields_added_payload_edit ? " " : "") << "set_name \"" << set_name_str_edit << "\"";
        set_fields_added_payload_edit = true;
    }
    if (!set_ip_str_edit.empty()) {
        final_payload_ss_edit << (set_fields_added_payload_edit ? " " : "") << "set_ip \"" << set_ip_str_edit << "\"";
        set_fields_added_payload_edit = true;
    }
    if (!set_date_str_edit.empty()) {
        final_payload_ss_edit << (set_fields_added_payload_edit ? " " : "") << "set_date \"" << set_date_str_edit << "\"";
        set_fields_added_payload_edit = true;
    }
    if (!set_traffic_line_str_edit.empty()) {
        final_payload_ss_edit << (set_fields_added_payload_edit ? " " : "") << "set_traffic \"" << set_traffic_line_str_edit << "\"";
        // set_fields_added_payload_edit = true; // Уже не нужна эта переменная здесь
    }
        
    return final_payload_ss_edit.str();
}


void DBClient::run_interactive() {
    if (!socket_.is_valid()) {
        std::cerr << get_current_timestamp() << " [" << client_id_ << "] Error: Not connected. Cannot run interactive mode." << std::endl;
        return;
    }
    std::cout << "\n" << get_current_timestamp() << " [" << client_id_ << "] Interactive DB Client Connected. Type 'EXIT' to quit." << std::endl;
    std::cout << get_current_timestamp() << " [" << client_id_ << "] Special commands: 'PING', 'ADD_INTERACTIVE', 'EDIT_INTERACTIVE', 'SHUTDOWN_SERVER' (admin)." << std::endl;
    std::string command_line_interactive_run; // Переименована

    while (true) { // Изменен на бесконечный цикл, выход по break
        std::cout << get_current_timestamp() << " [" << client_id_ << "] db_client> ";
        if (!std::getline(std::cin, command_line_interactive_run)) { 
            if (std::cin.eof()) {
                std::cout << get_current_timestamp() << " [" << client_id_ << "] EOF detected. Exiting interactive mode." << std::endl;
            } else { // Ошибка чтения, не EOF
                std::cerr << get_current_timestamp() << " [" << client_id_ << "] Input stream error. Exiting." << std::endl;
            }
            break; // Выход из цикла при EOF или ошибке
        }
        
        std::string trimmed_cmd_interactive_run = trim_string(command_line_interactive_run); // Переименована
        if (to_lower_util(trimmed_cmd_interactive_run) == "exit") {
            std::cout << get_current_timestamp() << " [" << client_id_ << "] Exiting interactive mode." << std::endl;
            break;
        }
        if (trimmed_cmd_interactive_run.empty()) continue;
        
        std::pair<bool, std::string> result_pair_interactive; // Переименована

        if (to_lower_util(trimmed_cmd_interactive_run) == "ping") {
            result_pair_interactive = send_ping();
            std::cout << get_current_timestamp() << " [" << client_id_ << "] PING Response: " << (result_pair_interactive.first ? "Success" : "Failed") << " - " << result_pair_interactive.second << std::endl;
        } else if (to_lower_util(trimmed_cmd_interactive_run) == "add_interactive") {
            auto add_command_opt_interactive = build_add_command_from_interactive_input(); // Переименована
            if (add_command_opt_interactive) {
                 std::cout << get_current_timestamp() << " [" << client_id_ << "] Sending ADD command: " << *add_command_opt_interactive << std::endl;
                result_pair_interactive = send_raw_command(*add_command_opt_interactive); 
                 std::cout << get_current_timestamp() << " [" << client_id_ << "] " << (result_pair_interactive.first ? "Server OK: " : "Server/Client ERROR: ") << result_pair_interactive.second << std::endl;
            } else { continue; /* Отмена ввода в хелпере */ }
        } 
        else if (to_lower_util(trimmed_cmd_interactive_run) == "edit_interactive") {
            auto edit_payload_opt_interactive = build_edit_command_payload_from_interactive_input(); // Переименована
            if (edit_payload_opt_interactive) {
                std::cout << get_current_timestamp() << " [" << client_id_ << "] Sending EDIT command with payload: " << *edit_payload_opt_interactive << std::endl;
                // Используем create_request_string напрямую, так как send_edit_command ожидает типизированные параметры,
                // а build_edit_command_payload_from_interactive_input уже вернул строку payload.
                std::string request_str_edit_interactive = create_request_string(CommandType::EDIT_RECORD_CMD, *edit_payload_opt_interactive); // Переименована
                
                if (socket_.send_data(request_str_edit_interactive) <= 0) {
                    result_pair_interactive = {false, "Error: Failed to send EDIT command (socket send failed)."};
                } else {
                    std::string server_response_framed_edit = socket_.receive_framed_message(); // Переименована
                    if (server_response_framed_edit.empty()) {
                         result_pair_interactive = {false, socket_.is_valid() ? "Error: Empty response after EDIT." : "Error: No response and connection lost after EDIT."};
                    } else {
                        ParsedMessage response_msg_edit = parse_message_payload(server_response_framed_edit); // Переименована
                        if (!response_msg_edit.valid) result_pair_interactive = {false, "Error: Invalid server response format after EDIT: " + response_msg_edit.payload_data};
                        else if (response_msg_edit.type == CommandType::ERROR_RESPONSE) result_pair_interactive = {false, "Server Error on EDIT: " + response_msg_edit.payload_data};
                        else result_pair_interactive = {true, response_msg_edit.payload_data}; 
                    }
                }
                std::cout << get_current_timestamp() << " [" << client_id_ << "] " << (result_pair_interactive.first ? "Server OK: " : "Server/Client ERROR: ") << result_pair_interactive.second << std::endl;
            } else { continue; /* Отмена ввода в хелпере */ }
        }
        else if (to_lower_util(trimmed_cmd_interactive_run) == "shutdown_server") {
            std::cout << get_current_timestamp() << " [" << client_id_ << "] Sending SHUTDOWN command to server..." << std::endl;
            std::string request_str_shutdown_interactive = create_request_string(CommandType::SHUTDOWN_SERVER_CMD, client_id_); // Переименована
            socket_.send_data(request_str_shutdown_interactive); 
            // Ответ от сервера на SHUTDOWN может прийти, а может и нет, если сервер сразу закроет соединение.
            // Для простоты, просто выходим после отправки.
            std::cout << get_current_timestamp() << " [" << client_id_ << "] Shutdown command sent. Client will disconnect." << std::endl;
            break; 
        }
        else { 
            std::cout << get_current_timestamp() << " [" << client_id_ << "] Sending RAW command: " << trimmed_cmd_interactive_run << std::endl;
            result_pair_interactive = send_raw_command(trimmed_cmd_interactive_run);
            std::cout << get_current_timestamp() << " [" << client_id_ << "] Received: " << (result_pair_interactive.first ? "Success" : "Failure") << std::endl;
            if (result_pair_interactive.first) {
                display_formatted_table_response(result_pair_interactive.second);
            } else {
                std::cout << result_pair_interactive.second << std::endl; 
            }
        }
        // Проверяем состояние сокета после каждой операции
        if (!socket_.is_valid()) {
            std::cerr << get_current_timestamp() << " [" << client_id_ << "] Connection to server lost. Exiting interactive mode." << std::endl;
            break;
        }
    }
}

void DBClient::run_batch(const std::string& input_filename, const std::string& output_filename) {
    if (!socket_.is_valid()) {
        std::cerr << get_current_timestamp() << " [" << client_id_ << "] Error: Not connected. Cannot run batch mode." << std::endl;
        return;
    }

    std::ifstream infile_batch(input_filename); // Переименована
    if (!infile_batch) {
        std::cerr << get_current_timestamp() << " [" << client_id_ << "] Client Error: Cannot open input batch file '" << input_filename << "'." << std::endl;
        return;
    }
    // Открываем на дозапись, как и было
    std::ofstream outfile_batch(output_filename, std::ios::app); // Переименована
    if (!outfile_batch) {
        std::cerr << get_current_timestamp() << " [" << client_id_ << "] Client Error: Cannot open output batch file '" << output_filename << "'." << std::endl;
        infile_batch.close();
        return;
    }

    outfile_batch << get_current_timestamp() << " [" << client_id_ << "] Batch Processing Started. Input: " << input_filename << ", Output: " << output_filename << std::endl;
    outfile_batch << "----------------------------------------------------------------------" << std::endl;
    std::cout << get_current_timestamp() << " [" << client_id_ << "] Running batch file: " << input_filename << ", output to: " << output_filename << std::endl;

    std::string line_batch; // Переименована
    int line_num_batch = 0; // Переименована
    while (std::getline(infile_batch, line_batch)) {
        line_num_batch++;
        line_batch = trim_string(line_batch);
        if (line_batch.empty() || line_batch[0] == '#') continue;

        outfile_batch << "\n" << get_current_timestamp() << " [" << client_id_ << "] Query " << line_num_batch << ": " << line_batch << std::endl;
        std::cout << get_current_timestamp() << " [" << client_id_ << "] Sending Query " << line_num_batch << ": " << line_batch << std::endl;

        std::pair<bool, std::string> batch_result; // Переименована

        std::stringstream line_ss_for_verb_batch(line_batch); // Переименована
        std::string first_verb_batch; // Переименована
        line_ss_for_verb_batch >> first_verb_batch;
        std::string upper_first_verb_batch = first_verb_batch; // Переименована
        std::transform(upper_first_verb_batch.begin(), upper_first_verb_batch.end(), upper_first_verb_batch.begin(), 
                       [](unsigned char c_uc){ return static_cast<char>(std::toupper(c_uc)); });


        if (upper_first_verb_batch == "SLEEP") {
            std::stringstream sleep_ss_batch(line_batch); // Переименована
            std::string sleep_cmd_word_val_batch; // Переименована
            int sleep_ms_val_batch = 0; // Переименована
            sleep_ss_batch >> sleep_cmd_word_val_batch >> sleep_ms_val_batch;
            if (!sleep_ss_batch.fail() && sleep_ms_val_batch > 0) { 
                outfile_batch << get_current_timestamp() << " [" << client_id_ << "] Executing SLEEP " << sleep_ms_val_batch << "ms" << std::endl;
                std::cout << get_current_timestamp() << " [" << client_id_ << "] Sleeping for " << sleep_ms_val_batch << "ms..." << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms_val_batch));
                outfile_batch << get_current_timestamp() << " [" << client_id_ << "] SLEEP finished." << std::endl;
            } else {
                outfile_batch << get_current_timestamp() << " [" << client_id_ << "] Warning: Invalid SLEEP command format or duration: " << line_batch << std::endl;
            }
            // Для SLEEP нет ответа от сервера, так что пропускаем часть с batch_result
        } else if (upper_first_verb_batch == "RAW_EDIT_COMMAND") {
            std::string edit_payload_batch = trim_string(line_batch.substr(first_verb_batch.length())); // Переименована
            if (edit_payload_batch.empty()) {
                batch_result = {false, "Client Error: RAW_EDIT_COMMAND payload missing."};
            } else {
                // Используем send_edit_command, если хотим валидацию на клиенте,
                // или формируем строку и отправляем напрямую с CommandType::EDIT_RECORD_CMD
                // Текущий send_edit_command ожидает типизированные параметры, а не строку payload.
                // Поэтому проще отправить напрямую.
                std::string request_str_batch_edit = create_request_string(CommandType::EDIT_RECORD_CMD, edit_payload_batch); // Переименована
                if (socket_.send_data(request_str_batch_edit) <= 0) {
                    batch_result = {false, "Error: Failed to send RAW_EDIT_COMMAND (socket send failed)."};
                } else {
                    std::string server_response_framed_batch_edit = socket_.receive_framed_message(); // Переименована
                    if (server_response_framed_batch_edit.empty()) {
                         batch_result = {false, socket_.is_valid() ? "Error: Empty response after RAW_EDIT." : "Error: No response and connection lost after RAW_EDIT."};
                    } else {
                        ParsedMessage response_msg_batch_edit = parse_message_payload(server_response_framed_batch_edit); // Переименована
                        if (!response_msg_batch_edit.valid) batch_result = {false, "Error: Invalid server response format after RAW_EDIT: " + response_msg_batch_edit.payload_data};
                        else if (response_msg_batch_edit.type == CommandType::ERROR_RESPONSE) batch_result = {false, "Server Error on RAW_EDIT: " + response_msg_batch_edit.payload_data};
                        else batch_result = {true, response_msg_batch_edit.payload_data}; 
                    }
                }
            }
            outfile_batch << get_current_timestamp() << " [" << client_id_ << "] Server Response Status: " << (batch_result.first ? "Success" : "Error") << std::endl;
            if (!batch_result.second.empty()) outfile_batch << "Payload:\n" << batch_result.second << std::endl; 
            else if (batch_result.first) outfile_batch << "Payload: (No data returned, but operation successful)" << std::endl;

        } else { // Все остальные команды (SELECT, ADD, DELETE, CALCULATE_BILL, LOAD_TARIFF, PING, SHUTDOWN_SERVER)
            batch_result = send_raw_command(line_batch); // send_raw_command использует CommandType::EXECUTE_QUERY
                                                        // Это значит, что PING и SHUTDOWN_SERVER в пакетном режиме не будут работать как ожидалось,
                                                        // если сервер их ожидает с другим CommandType.
                                                        // Для PING и SHUTDOWN_SERVER нужно отдельные обработчики в batch или
                                                        // send_raw_command должен быть умнее.

            // ИСПРАВЛЕНИЕ для PING и SHUTDOWN_SERVER в пакетном режиме:
            if (upper_first_verb_batch == "PING") {
                batch_result = send_ping();
            } else if (upper_first_verb_batch == "SHUTDOWN_SERVER") {
                std::string request_str_batch_shutdown = create_request_string(CommandType::SHUTDOWN_SERVER_CMD, client_id_); // Переименована
                if(socket_.send_data(request_str_batch_shutdown) <=0) {
                     batch_result = {false, "Error: Failed to send SHUTDOWN_SERVER."};
                } else {
                    // Ответ может не прийти, или быть простым ACK
                    // Для простоты, предполагаем, что сам факт отправки - это "успех" для клиента.
                    // Сервер должен подтвердить.
                    std::string server_response_framed_batch_sd = socket_.receive_framed_message(); // Переименована
                    if (server_response_framed_batch_sd.empty()) {
                         batch_result = {true, "Shutdown command sent, no specific data in response or connection closed by server."};
                    } else {
                        ParsedMessage response_msg_batch_sd = parse_message_payload(server_response_framed_batch_sd); // Переименована
                        if (!response_msg_batch_sd.valid) batch_result = {false, "Error: Invalid server response for SHUTDOWN: " + response_msg_batch_sd.payload_data};
                        else if (response_msg_batch_sd.type == CommandType::ERROR_RESPONSE) batch_result = {false, "Server Error on SHUTDOWN: " + response_msg_batch_sd.payload_data};
                        else batch_result = {true, response_msg_batch_sd.payload_data};
                    }
                }
            } else { // Остальные (SELECT, ADD, DELETE, CALCULATE_BILL, LOAD_TARIFF) через EXECUTE_QUERY
                 batch_result = send_raw_command(line_batch);
            }
            outfile_batch << get_current_timestamp() << " [" << client_id_ << "] Server Response Status: " << (batch_result.first ? "Success" : "Error") << std::endl;
            if (!batch_result.second.empty()) outfile_batch << "Payload:\n" << batch_result.second << std::endl; 
            else if (batch_result.first) outfile_batch << "Payload: (No data returned, but operation successful)" << std::endl;
        }
       
        if (!batch_result.first && (!socket_.is_valid() || 
            batch_result.second.find("connection lost") != std::string::npos ||
            batch_result.second.find("No response") != std::string::npos)) {
             std::cerr << get_current_timestamp() << " [" << client_id_ << "] Connection to server lost during batch processing. Aborting." << std::endl;
             outfile_batch << get_current_timestamp() << " [" << client_id_ << "] ABORTED: Connection to server lost." << std::endl;
             break; // Прерываем цикл while
        }
        outfile_batch << "----------------------------------------------------------------------" << std::endl;
    }
    outfile_batch << get_current_timestamp() << " [" << client_id_ << "] Batch Processing Finished." << std::endl;
    infile_batch.close();
    outfile_batch.close();
    std::cout << get_current_timestamp() << " [" << client_id_ << "] Batch processing complete. Output in '" << output_filename << "'" << std::endl;
}
