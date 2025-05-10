#include "user_interface.h"
#include "query_parser.h"
#include "common_defs.h" // Содержит is_valid_cmd_argument_path и is_valid_simple_filename
#include "provider_record.h"
#include "date.h"
#include "ip_address.h"
#include <iostream>
#include <limits>
#include <iomanip>
#include <sstream>
#include <algorithm>

UserInterface::UserInterface(Database& db, TariffPlan& tariff, const std::string& db_file, const std::string& tariff_file)
    : database(db), tariff_plan(tariff), db_filename_used(db_file), tariff_filename_used(tariff_file)
{
    std::cout << get_current_timestamp() << " [UI] Initializing User Interface..." << std::endl;
    // Валидация путей db_file и tariff_file уже должна была произойти в main()
    // перед созданием объекта UserInterface. Здесь мы просто используем их.
    if (!database.load_from_file(db_filename_used)) {
        std::cerr << get_current_timestamp() << " [UI] Warning: Could not fully load database from '" << db_filename_used << "'. Check logs." << std::endl;
    } else {
        std::cout << get_current_timestamp() << " [UI] Database loaded from '" << db_filename_used << "'. " << database.record_count() << " records." << std::endl;
    }
    if (!tariff_plan.load_from_file(tariff_filename_used)) {
         std::cerr << get_current_timestamp() << " [UI] Warning: Could not load tariff plan from '" << tariff_filename_used << "'. Using default rates (0.0)." << std::endl;
    } else {
         std::cout << get_current_timestamp() << " [UI] Tariff plan loaded from '" << tariff_filename_used << "'." << std::endl;
    }
}

void UserInterface::display_menu() {
    std::cout << "\n===== Internet Provider Database Menu (Standalone) =====\n";
    std::cout << "1. Add Record\n";
    std::cout << "2. Select Records (Query)\n";
    std::cout << "3. Delete Records (Query)\n";
    std::cout << "4. Edit Record (by exact match)\n";
    std::cout << "5. Print All Records\n";
    std::cout << "6. Calculate Bill (Query)\n";
    std::cout << "7. Load Tariff Plan from File\n";
    std::cout << "8. View Current Tariff Plan\n";
    std::cout << "9. Save Database to File\n";
    std::cout << "0. Exit\n";
    std::cout << "======================================================\n";
    std::cout << "Enter your choice: ";
}

template<typename T>
std::optional<T> UserInterface::get_validated_input(const std::string& prompt, bool allow_empty_for_edit) {
    std::string line_input;
    while (true) {
        std::cout << prompt;
        if (!std::getline(std::cin, line_input)) {
             if (std::cin.eof()) {
                std::cout << "\nEOF detected. Cancelling input." << std::endl;
                return std::nullopt;
            }
            std::cerr << "Error: Input stream failure. Please try again." << std::endl;
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            return std::nullopt;
        }

        if (line_input.empty()) {
            if (allow_empty_for_edit) {
                return std::nullopt;
            } else {
                std::cerr << "Error: Input cannot be empty. Please try again.\n";
                continue;
            }
        }

        T value{};
        std::stringstream ss(line_input);
        ss >> value;

        if (ss.eof() && !ss.fail()) {
            bool is_type_valid = true;
            if constexpr (std::is_same_v<T, Date>) {
                 is_type_valid = value.is_valid();
                 if(!is_type_valid) std::cerr << "Error: Invalid date format or value (expected DD/MM/YYYY and valid date). Please try again.\n";
            } else if constexpr (std::is_same_v<T, IpAddress>) {
                 is_type_valid = value.is_valid();
                  if(!is_type_valid) std::cerr << "Error: Invalid IP address format or value (expected A.B.C.D, 0-255). Please try again.\n";
            }
            if(is_type_valid) return value; else continue;
        } else {
            std::cerr << "Error: Invalid input format for the expected type (e.g., '" << line_input << "'). Unconsumed: '" << ss.str().substr(static_cast<size_t>(ss.tellg())) << "'. Please try again.\n";
        }
    }
}

std::optional<std::string> UserInterface::get_validated_line(const std::string& prompt, bool allow_empty) {
    std::string line;
    while (true) {
        std::cout << prompt;
        if (!std::getline(std::cin, line)) {
             if (std::cin.eof()) {
                std::cout << "\nEOF detected. Cancelling input." << std::endl;
                return std::nullopt;
            }
            std::cerr << "Error: Input stream failure. Please try again.\n";
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            return std::nullopt;
        }
        if (line.empty() && !allow_empty) {
            std::cerr << "Error: Input cannot be empty. Please try again.\n";
            continue;
        }
        return line;
    }
}

std::optional<TrafficData> UserInterface::get_validated_traffic_data_interactive(const std::string& prompt_detail) {
    TrafficData traffic_readings(HOURS_IN_DAY);
    std::cout << prompt_detail << "(enter " << 2 * HOURS_IN_DAY
              << " space-separated non-negative numbers for In/Out traffic: In0 Out0 In1 Out1 ... In23 Out23).\n"
              << "Or leave blank to skip/keep current in edit mode:\n";
    std::string line;

    while(true) {
        std::cout << "> ";
        if (!std::getline(std::cin, line)) {
            if(std::cin.eof()) {std::cout << "EOF detected. Cancelling.\n"; return std::nullopt;}
            std::cerr << "Error: Input stream failure.\n"; std::cin.clear(); clear_cin_buffer(); return std::nullopt;
        }

        if (line.empty()) {
            return std::nullopt;
        }

        std::stringstream ss(line);
        long long in_val, out_val;
        size_t hour_count = 0;
        bool error_in_line = false;

        for(size_t i=0; i < HOURS_IN_DAY; ++i) {
            if (!(ss >> in_val >> out_val)) {
                std::cerr << "Error: Not enough numbers. Expected " << 2*HOURS_IN_DAY << " (In/Out pairs). "
                          << "Read up to hour " << i << ". Please re-enter all or leave blank.\n";
                error_in_line = true; break;
            }
            if (in_val < 0 || out_val < 0) {
                std::cerr << "Error: Traffic values cannot be negative (In=" << in_val << ", Out=" << out_val
                          << " at hour " << i << "). Please re-enter all or leave blank.\n";
                error_in_line = true; break;
            }
            traffic_readings[i].incoming = in_val;
            traffic_readings[i].outgoing = out_val;
            hour_count++;
        }

        if (error_in_line) continue;

        std::string remaining_in_stream_check;
        if (ss >> remaining_in_stream_check) {
             if (!trim_string(remaining_in_stream_check).empty()){
                std::cerr << "Error: Too many numbers entered. Expected " << 2*HOURS_IN_DAY
                          << ". Found extra: '" << remaining_in_stream_check << "'. Please re-enter or leave blank.\n";
                continue;
             }
        }

        if (hour_count == HOURS_IN_DAY) {
            return traffic_readings;
        }
        // Эта ошибка может быть избыточной, если предыдущие проверки покрывают все случаи
        // std::cerr << "Error: Please enter exactly " << 2*HOURS_IN_DAY
        //           << " space-separated non-negative numbers for In/Out traffic or leave blank.\n";
    }
}

void UserInterface::handle_add() {
    std::cout << "\n--- Add New Record ---\n";
    ProviderRecord new_record;

    auto name_opt = get_validated_line("Enter Full Name: ", false);
    if (!name_opt) { std::cout << get_current_timestamp() << " [UI] Add cancelled by user (name).\n"; return; }
    new_record.full_name = *name_opt;

    auto ip_opt = get_validated_input<IpAddress>("Enter IP Address (e.g., 192.168.0.1): ");
    if (!ip_opt) { std::cout << get_current_timestamp() << " [UI] Add cancelled by user (IP).\n"; return; }
    new_record.ip_address = *ip_opt;

    auto date_opt = get_validated_input<Date>("Enter Date (DD/MM/YYYY): ");
    if (!date_opt) { std::cout << get_current_timestamp() << " [UI] Add cancelled by user (Date).\n"; return; }
    new_record.record_date = *date_opt;

    auto traffic_opt = get_validated_traffic_data_interactive("Enter Hourly Traffic Data ");
    if (!traffic_opt) { std::cout << get_current_timestamp() << " [UI] Add cancelled by user (Traffic).\n"; return; }
    new_record.hourly_traffic = *traffic_opt;

    if (database.add_record(std::move(new_record))) {
        std::cout << get_current_timestamp() << " [UI] Record added successfully.\n";
    } else {
        // Сообщение об ошибке уже выводится из Database::add_record
    }
}

void UserInterface::handle_select() {
    std::cout << "\n--- Select Records ---\n";
    std::cout << "Enter query (e.g., 'SELECT name, ip WHERE date >= 01/01/2024 SORT BY name ASC')\n";
    std::cout << "SELECT [field1,field2,...|*] [WHERE conditions] [SORT BY field [ASC|DESC]]\n";

    auto query_str_opt = get_validated_line("Query: ", true);
    if (!query_str_opt && std::cin.eof()) {std::cout << get_current_timestamp() << " [UI] Select cancelled by EOF.\n"; return;}

    std::string query_to_parse = (query_str_opt && !query_str_opt->empty()) ? *query_str_opt : "SELECT *";


    auto parsed_query_opt = QueryParser::parse_select(query_to_parse);
    if (!parsed_query_opt) {
        // Сообщение об ошибке уже выводится из QueryParser
        return;
    }
    SelectQuery parsed_query = *parsed_query_opt;

    std::vector<std::vector<std::string>> results_table = database.get_formatted_select_results(parsed_query);
    size_t data_row_count = 0;
    if (!results_table.empty()) {
        if (results_table.size() > 1) { // Есть заголовки и данные
            data_row_count = results_table.size() - 1;
        } else if (results_table.size() == 1) { // Только одна строка
            // Проверяем, это строка заголовков (без данных) или одна строка данных
            bool is_header = false;
            if (!results_table[0].empty()) {
                const auto& default_fields = ProviderRecord::get_all_field_names();
                if (!default_fields.empty()){
                    if (parsed_query.wants_all_fields() && results_table[0][0] == default_fields[0]) is_header = true;
                    else if (!parsed_query.select_fields.empty() && results_table[0][0] == parsed_query.select_fields[0]) is_header = true;
                }
            }
            if (!is_header && !results_table[0].empty()) data_row_count = 1;
        }
    }

    if (data_row_count == 0) {
        std::cout << "No records found matching the criteria.\n";
    } else {
        std::cout << "\n--- Query Results (" << data_row_count << " data rows) ---\n";

        std::vector<size_t> col_widths;
        if(!results_table.empty() && !results_table[0].empty()){
            col_widths.resize(results_table[0].size(), 0);
            for(const auto& row : results_table){
                 for(size_t i=0; i < row.size() && i < col_widths.size(); ++i){
                    col_widths[i] = std::max(col_widths[i], row[i].length());
                }
            }
        } else if (!results_table.empty() && results_table.size() > 1 && !results_table[1].empty()){
             col_widths.resize(results_table[1].size(), 0);
             for(const auto& row : results_table){
                 if(row.empty()) continue;
                 for(size_t i=0; i < row.size() && i < col_widths.size(); ++i){
                    col_widths[i] = std::max(col_widths[i], row[i].length());
                }
            }
        }

        bool first_row_is_header = !results_table.empty() && !results_table[0].empty();
        for (size_t r_idx = 0; r_idx < results_table.size(); ++r_idx) {
            const auto& row_data = results_table[r_idx];
            if (row_data.empty() && first_row_is_header && r_idx == 0 && results_table.size() > 1) continue; // Пропустить пустой заголовок, если есть данные
            if (row_data.empty() && !first_row_is_header) continue;

            for (size_t i = 0; i < row_data.size() && i < col_widths.size(); ++i) {
                std::cout << std::left << std::setw(static_cast<int>(col_widths[i] + 2)) << row_data[i];
            }
            std::cout << "\n";
            if (r_idx == 0 && first_row_is_header && !col_widths.empty() && results_table.size() > 1) { // Печатать разделитель, только если есть данные после заголовка
                for (size_t width : col_widths) {
                    std::cout << std::string(width + 2, '-');
                }
                std::cout << "\n";
            }
        }
        std::cout << std::right;
    }
}

void UserInterface::handle_delete() {
    std::cout << "\n--- Delete Records ---\n";
    std::cout << "Enter WHERE conditions to select records for deletion (e.g., 'name = \"John Doe\" AND ip = 1.2.3.4'):\n";
    std::cout << "Or type full SELECT query whose results will be deleted.\n";
    std::cout << "WARNING: This action is irreversible!\n";

    auto query_conditions_str_opt = get_validated_line("Delete WHERE conditions or full SELECT query: ", false);
    if (!query_conditions_str_opt || query_conditions_str_opt->empty()) {
        std::cout << get_current_timestamp() << " [UI] Delete cancelled or empty query.\n"; return;
    }
    std::string query_for_delete = *query_conditions_str_opt;
    if (to_lower_util(query_for_delete.substr(0,6)) != "select"){
        query_for_delete = "SELECT * WHERE " + query_for_delete;
    }

    auto parsed_query_opt = QueryParser::parse_select(query_for_delete);
    if (!parsed_query_opt) {
        return;
    }
    SelectQuery parsed_query = *parsed_query_opt;

    // Сначала покажем, какие записи будут удалены
    SelectQuery display_query = parsed_query; // Копируем для отображения
    if (display_query.select_fields.empty() || display_query.select_fields[0] != "*") { // Если не SELECT *
        display_query.select_fields = {"*"}; // Для отображения всегда показываем все поля
    }
    std::vector<std::vector<std::string>> records_to_delete_display = database.get_formatted_select_results(display_query);

    size_t data_rows_to_delete = 0;
    if (!records_to_delete_display.empty() && records_to_delete_display.size() > 1) {
        data_rows_to_delete = records_to_delete_display.size() -1;
    }

    if (data_rows_to_delete == 0) {
        std::cout << "No records match the deletion criteria.\n";
        return;
    }

    std::cout << "The following " << data_rows_to_delete << " record(s) will be DELETED:\n";
    // Код для вывода таблицы (аналогично handle_select)
    std::vector<size_t> col_widths_del;
     if(!records_to_delete_display.empty() && !records_to_delete_display[0].empty()){
        col_widths_del.resize(records_to_delete_display[0].size(), 0);
        for(const auto& row : records_to_delete_display){
            for(size_t i=0; i < row.size() && i < col_widths_del.size(); ++i){
                col_widths_del[i] = std::max(col_widths_del[i], row[i].length());
            }
        }
    }
    bool first_row_del_header = !records_to_delete_display.empty() && !records_to_delete_display[0].empty();
    for (size_t r_idx = 0; r_idx < records_to_delete_display.size(); ++r_idx) {
        const auto& row_data = records_to_delete_display[r_idx];
        if (row_data.empty()) continue;
        for (size_t i = 0; i < row_data.size() && i < col_widths_del.size(); ++i) {
            std::cout << std::left << std::setw(static_cast<int>(col_widths_del[i] + 2)) << row_data[i];
        }
        std::cout << "\n";
        if (r_idx == 0 && first_row_del_header && !col_widths_del.empty() && records_to_delete_display.size() > 1) {
            for (size_t width : col_widths_del) std::cout << std::string(width + 2, '-');
            std::cout << "\n";
        }
    }
    std::cout << std::right;

    auto confirmation_opt = get_validated_line("Are you sure you want to delete these records? (yes/no): ", false);
    if (!confirmation_opt || (to_lower_util(*confirmation_opt) != "yes")) {
        std::cout << get_current_timestamp() << " [UI] Deletion cancelled by user.\n";
        return;
    }

    size_t deleted_count = database.delete_records(parsed_query); // Используем оригинальный parsed_query для удаления
    std::cout << get_current_timestamp() << " [UI] " << deleted_count << " record(s) deleted.\n";
}

void UserInterface::handle_edit() {
    std::cout << "\n--- Edit Record (identified by exact Name, IP, Date) ---\n";
    auto name_key_opt = get_validated_line("Enter Current Full Name of record to edit: ", false);
    if (!name_key_opt) { std::cout << get_current_timestamp() << " [UI] Edit cancelled.\n"; return; }

    auto ip_key_opt = get_validated_input<IpAddress>("Enter Current IP Address of record to edit: ");
    if (!ip_key_opt) { std::cout << get_current_timestamp() << " [UI] Edit cancelled.\n"; return; }

    auto date_key_opt = get_validated_input<Date>("Enter Current Date of record to edit (DD/MM/YYYY): ");
    if (!date_key_opt) { std::cout << get_current_timestamp() << " [UI] Edit cancelled.\n"; return; }

    ProviderRecord* record_to_edit_ptr = database.find_exact_record_for_edit(*name_key_opt, *ip_key_opt, *date_key_opt);

    if (!record_to_edit_ptr) {
        std::cerr << "Error: Record not found for the given Name, IP, and Date.\n";
        return;
    }
    ProviderRecord temp_edited_record = *record_to_edit_ptr;

    std::cout << "--- Found Record --- (Leave new value blank to keep current)\n";
    temp_edited_record.print();
    std::cout << "--------------------\n";

    bool changed = false;

    auto new_name_opt = get_validated_line("New Name [" + temp_edited_record.full_name + "]: ", true);
    if (new_name_opt && !new_name_opt->empty()) { temp_edited_record.full_name = *new_name_opt; changed = true;}
    else if (!new_name_opt && std::cin.eof()) { std::cout << "Edit cancelled.\n"; return;}

    auto new_ip_opt = get_validated_input<IpAddress>("New IP [" + temp_edited_record.ip_address.to_string() + "]: ", true);
    if (new_ip_opt) { temp_edited_record.ip_address = *new_ip_opt; changed = true; }
    else if (std::cin.eof() && !new_ip_opt) { std::cout << "Edit cancelled.\n"; return;}

    auto new_date_opt = get_validated_input<Date>("New Date [" + temp_edited_record.record_date.to_string() + "]: ", true);
    if (new_date_opt) { temp_edited_record.record_date = *new_date_opt; changed = true; }
    else if (std::cin.eof() && !new_date_opt) { std::cout << "Edit cancelled.\n"; return;}

    auto new_traffic_opt = get_validated_traffic_data_interactive("New Hourly Traffic Data ");
    if (new_traffic_opt) { temp_edited_record.hourly_traffic = *new_traffic_opt; changed = true;}
    else if (std::cin.eof() && !new_traffic_opt) { std::cout << "Edit cancelled.\n"; return;}


    if (changed) {
        if (temp_edited_record.is_valid()) {
            bool id_fields_changed = (temp_edited_record.full_name != record_to_edit_ptr->full_name ||
                                      temp_edited_record.ip_address != record_to_edit_ptr->ip_address ||
                                      temp_edited_record.record_date != record_to_edit_ptr->record_date);

            bool can_update = true;
            if (id_fields_changed) {
                // Проверяем, не создаст ли изменение идентифицирующих полей дубликат с другой существующей записью
                ProviderRecord* conflict_check = database.find_exact_record_for_edit(
                    temp_edited_record.full_name, temp_edited_record.ip_address, temp_edited_record.record_date
                );
                if (conflict_check != nullptr && conflict_check != record_to_edit_ptr) { // Конфликт, если найдена ДРУГАЯ запись
                     std::cerr << "Error: Another record with the new identifying fields (Name, IP, Date) already exists.\n";
                     can_update = false;
                }
            }

            if (can_update) {
                *record_to_edit_ptr = temp_edited_record;
                std::cout << get_current_timestamp() << " [UI] Record updated successfully.\n";
            } else {
                 std::cout << get_current_timestamp() << " [UI] Update failed due to conflict. No changes made to original.\n";
            }
        } else {
            std::cerr << "Error: Edited record data is invalid. Changes not applied.\n";
        }
    } else {
        std::cout << get_current_timestamp() << " [UI] No changes made to the record.\n";
    }
}

void UserInterface::handle_print_all() {
    std::cout << "\n--- All Records in Database ---\n";
    SelectQuery all_query;
    all_query.select_fields.push_back("*");

    std::vector<std::vector<std::string>> results_table = database.get_formatted_select_results(all_query);

    size_t data_row_count = 0;
    if(!results_table.empty()){
        if (results_table.size() > 1) data_row_count = results_table.size() - 1;
        // Если results_table.size() == 1, это только заголовки, data_row_count = 0
    }

    if (data_row_count == 0 && (results_table.empty() || results_table.size() <=1) ) { // Условие для "нет данных"
        std::cout << "Database is empty or no records to display.\n";
        if (!results_table.empty() && !results_table[0].empty() && data_row_count == 0) { // Если есть только заголовки
            for(const auto& header_cell : results_table[0]) std::cout << header_cell << "\t";
            std::cout << "\n(No data records)\n";
        }
    } else {
        std::cout << "\n--- All Records (" << data_row_count << " data rows) ---\n";
        // Код для вывода таблицы (аналогично handle_select)
        std::vector<size_t> col_widths_all;
         if(!results_table.empty() && !results_table[0].empty()){
            col_widths_all.resize(results_table[0].size(), 0);
            for(const auto& row : results_table){
                 for(size_t i=0; i < row.size() && i < col_widths_all.size(); ++i){
                    col_widths_all[i] = std::max(col_widths_all[i], row[i].length());
                }
            }
        }
        bool first_row_header_all = !results_table.empty() && !results_table[0].empty();
        for (size_t r_idx = 0; r_idx < results_table.size(); ++r_idx) {
            const auto& row_data = results_table[r_idx];
             if (row_data.empty()) continue;
            for (size_t i = 0; i < row_data.size() && i < col_widths_all.size(); ++i) {
                std::cout << std::left << std::setw(static_cast<int>(col_widths_all[i] + 2)) << row_data[i];
            }
            std::cout << "\n";
            if (r_idx == 0 && first_row_header_all && !col_widths_all.empty() && results_table.size() > 1) {
                for (size_t width : col_widths_all) {
                    std::cout << std::string(width + 2, '-');
                }
                std::cout << "\n";
            }
        }
         std::cout << std::right;
    }
}

void UserInterface::handle_calculate_bill() {
    std::cout << "\n--- Calculate Bill ---\n";
    std::cout << "Enter WHERE conditions for billing (e.g., 'ip = 1.2.3.4') or full SELECT query:\n";

    auto query_str_opt = get_validated_line("Billing Query Conditions/SELECT: ", false);
    if (!query_str_opt || query_str_opt->empty()) { std::cout << get_current_timestamp() << " [UI] Bill calculation cancelled.\n"; return; }

    std::string bill_query_str = *query_str_opt;
    if(to_lower_util(bill_query_str.substr(0,6)) != "select") {
        bill_query_str = "SELECT * WHERE " + bill_query_str;
    }

    auto parsed_query_opt = QueryParser::parse_select(bill_query_str);
    if (!parsed_query_opt) { return; }
    SelectQuery parsed_query = *parsed_query_opt;

    auto start_date_opt = get_validated_input<Date>("Enter Start Date (DD/MM/YYYY): ");
    if (!start_date_opt) { std::cout << get_current_timestamp() << " [UI] Bill calculation cancelled.\n"; return; }

    auto end_date_opt = get_validated_input<Date>("Enter End Date (DD/MM/YYYY): ");
    if (!end_date_opt) { std::cout << get_current_timestamp() << " [UI] Bill calculation cancelled.\n"; return; }

    if (*end_date_opt < *start_date_opt) {
        std::cerr << "Error: End date cannot be before start date.\n";
        return;
    }

    auto bill_amount_opt = database.calculate_bill(parsed_query, *start_date_opt, *end_date_opt, tariff_plan);

    if (bill_amount_opt) {
        std::cout << "\n--- Billing Calculation Result ---\n";
        std::cout << "For records matching: " << bill_query_str << "\n";
        std::cout << "Period: From " << start_date_opt->to_string() << " To " << end_date_opt->to_string() << "\n";
        std::cout << "Total Calculated Bill: " << std::fixed << std::setprecision(2) << *bill_amount_opt << "\n";
        std::cout << std::defaultfloat;
    } else {
        std::cerr << "Could not calculate bill. Ensure dates are valid, tariff plan is loaded, and records exist for the period.\n";
    }
}

void UserInterface::handle_load_tariff() {
    std::cout << "\n--- Load Tariff Plan ---\n";
    auto filename_opt = get_validated_line("Enter tariff filename [" + tariff_filename_used + "] (leave blank for current): ", true);

    std::string new_tariff_file_to_load = (filename_opt && !filename_opt->empty()) ? *filename_opt : tariff_filename_used;

    // Используем is_valid_cmd_argument_path, так как пользователь может ввести полный путь
    if (!is_valid_cmd_argument_path(new_tariff_file_to_load)) { // <--- ИЗМЕНЕНИЕ
         std::cerr << "Error: Invalid tariff filename or path provided: " << new_tariff_file_to_load << std::endl; return;
    }
    ensure_directory_exists_util(new_tariff_file_to_load); // Убедиться, что директория существует, если это полный путь

    if (tariff_plan.load_from_file(new_tariff_file_to_load)) { // Внутри load_from_file тоже is_valid_cmd_argument_path
        tariff_filename_used = new_tariff_file_to_load;
        std::cout << get_current_timestamp() << " [UI] Tariff plan loaded successfully from '" << tariff_filename_used << "'.\n";
    } else {
        // Сообщение об ошибке уже выводится из TariffPlan::load_from_file
        std::cerr << get_current_timestamp() << " [UI] Failed to load new tariff plan. Previous plan (if any) remains active.\n";
    }
}

void UserInterface::handle_save_db() {
    std::cout << "\n--- Save Database ---\n";
    auto filename_opt = get_validated_line("Enter filename to save database [" + db_filename_used + "] (leave blank for current): ", true);

    std::string file_to_save_to = (filename_opt && !filename_opt->empty()) ? *filename_opt : db_filename_used;

    // Используем is_valid_cmd_argument_path, так как пользователь может ввести полный путь
    if (!is_valid_cmd_argument_path(file_to_save_to)){ // <--- ИЗМЕНЕНИЕ
        std::cerr << "Error: Invalid database filename or path for saving: " << file_to_save_to << std::endl; return;
    }
    ensure_directory_exists_util(file_to_save_to); // Убедиться, что директория существует

    if (database.save_to_file(file_to_save_to)) { // Внутри save_to_file тоже is_valid_cmd_argument_path
        db_filename_used = file_to_save_to;
        std::cout << get_current_timestamp() << " [UI] Database saved successfully to '" << db_filename_used << "'.\n";
    } else {
        // Сообщение об ошибке уже выводится из Database::save_to_file
    }
}

void UserInterface::run() {
    int choice_num;
    std::string choice_str;

    while (true) {
        display_menu();
        if (!std::getline(std::cin, choice_str)) {
            if(std::cin.eof()){ std::cout << "\nEOF detected. Exiting...\n"; choice_num = 0;}
            else { std::cerr << "Critical input error. Exiting.\n"; return; }
        } else if (choice_str.empty()) {
            std::cerr << "No choice entered. Please enter a number.\n";
            continue;
        }
        else {
            std::stringstream ss(choice_str);
            if (!(ss >> choice_num) || !ss.eof()) {
                std::cerr << "Invalid input. Please enter a number from the menu.\n";
                continue;
            }
        }

        switch (choice_num) {
            case 1: handle_add(); break;
            case 2: handle_select(); break;
            case 3: handle_delete(); break;
            case 4: handle_edit(); break;
            case 5: handle_print_all(); break;
            case 6: handle_calculate_bill(); break;
            case 7: handle_load_tariff(); break;
            case 8: std::cout << "\n--- Current Tariff Plan ---\n"; tariff_plan.print(); break;
            case 9: handle_save_db(); break;
            case 0:
            {
                std::cout << "Exiting. Save changes to '" << db_filename_used << "'? (yes/no): ";
                std::string save_choice_str_line;
                if (!std::getline(std::cin, save_choice_str_line)){
                     if(std::cin.eof()) std::cout << "\nEOF. Exiting without saving." << std::endl;
                     else std::cerr << "Input error. Exiting without saving." << std::endl;
                } else {
                    std::string save_choice_str_ui = to_lower_util(trim_string(save_choice_str_line));
                    if (save_choice_str_ui == "yes" || save_choice_str_ui == "y") {
                        if(database.save_to_file(db_filename_used)) {
                            std::cout << "Database saved to '" << db_filename_used << "'.\n";
                        } else { /* Error printed by save_to_file */ }
                    } else if (save_choice_str_ui == "no" || save_choice_str_ui == "n") {
                        std::cout << "Exiting without saving changes.\n";
                    } else {
                         std::cout << "Invalid save choice. Exiting without saving.\n";
                    }
                }
                std::cout << "Goodbye!\n";
                return;
            }
            default:
                std::cerr << "Invalid choice value. Please select from the menu.\n";
        }
    }
}

