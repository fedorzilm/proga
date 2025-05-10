// src/main.cpp

#include "user_interface.h"
#include "database.h"
#include "tariff_plan.h"
#include "common_defs.h"
#include "query_parser.h"
#include "provider_record.h"
#include "date.h"
#include "ip_address.h"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <locale> // Для std::locale

namespace StandaloneBatch {
    std::optional<ProviderRecord> parse_add_command_payload_standalone(std::stringstream& ss_payload) {
        ProviderRecord rec;
        std::string key;
        std::string value_str;
        bool name_set = false, ip_set = false, date_set = false, traffic_set = false;

        while (ss_payload >> key) {
            key = to_lower_util(key);
            ss_payload >> std::ws;

            if (ss_payload.peek() == '"') {
                ss_payload.get();
                std::getline(ss_payload, value_str, '"');
                if (ss_payload.fail() && !ss_payload.eof()) {
                     std::cerr << get_current_timestamp() << " [Batch ADD] Error: Unterminated quote for key '" << key << "'." << std::endl;
                     return std::nullopt;
                }
            } else {
                 std::cerr << get_current_timestamp() << " [Batch ADD] Error: Expected quoted value for key '" << key << "'." << std::endl;
                 return std::nullopt;
            }

            if (key == "name") {
                rec.full_name = value_str;
                name_set = true;
            } else if (key == "ip") {
                auto ip_opt = IpAddress::from_string(value_str);
                if (!ip_opt) { std::cerr << get_current_timestamp() << " [Batch ADD] Error: Invalid IP '" << value_str << "'." << std::endl; return std::nullopt; }
                rec.ip_address = *ip_opt;
                ip_set = true;
            } else if (key == "date") {
                auto date_opt = Date::from_string(value_str);
                if (!date_opt) { std::cerr << get_current_timestamp() << " [Batch ADD] Error: Invalid Date '" << value_str << "'." << std::endl; return std::nullopt; }
                rec.record_date = *date_opt;
                date_set = true;
            } else if (key == "traffic") {
                // std::cerr << "[DEBUG Batch ADD traffic] Raw traffic string value_str: [" << value_str << "]" << std::endl;
                std::stringstream traffic_ss(value_str);
                rec.hourly_traffic.assign(HOURS_IN_DAY, TrafficReading{});
                long long in_val, out_val;
                size_t hour = 0;
                while (hour < HOURS_IN_DAY) {
                    if (!(traffic_ss >> in_val >> out_val)) {
                        // std::cerr << "[DEBUG Batch ADD traffic] Failed to read in/out pair for hour " << hour << ". Stream state good/eof/fail/bad: "
                        //           << traffic_ss.good() << "/" << traffic_ss.eof() << "/" << traffic_ss.fail() << "/" << traffic_ss.bad() << std::endl;
                        break;
                    }
                    if (in_val < 0 || out_val < 0) {
                        std::cerr << get_current_timestamp() << " [Batch ADD] Error: Negative traffic value for hour " << hour << "." << std::endl;
                        return std::nullopt;
                    }
                    rec.hourly_traffic[hour].incoming = in_val;
                    rec.hourly_traffic[hour].outgoing = out_val;
                    hour++;
                }
                // std::cerr << "[DEBUG Batch ADD traffic] Total hours successfully parsed: " << hour << std::endl;
                std::string remaining_in_tr_stream;
                if (traffic_ss >> remaining_in_tr_stream && !trim_string(remaining_in_tr_stream).empty()) {
                     std::cerr << get_current_timestamp() << " [Batch ADD] Error: Extra data in traffic string after " << hour << " pairs: [" << trim_string(remaining_in_tr_stream) << "]" << std::endl;
                     return std::nullopt;
                }
                if (hour != HOURS_IN_DAY) {
                    std::cerr << get_current_timestamp() << " [Batch ADD] Error: Invalid traffic count (expected " << HOURS_IN_DAY << " pairs, got " << hour << "). Original string: [" << value_str << "]" << std::endl;
                    return std::nullopt;
                }
                traffic_set = true;
            } else {
                std::cerr << get_current_timestamp() << " [Batch ADD] Error: Unknown key '" << key << "'." << std::endl;
                return std::nullopt;
            }
        }

        if (!name_set || !ip_set || !date_set || !traffic_set) {
            std::cerr << get_current_timestamp() << " [Batch ADD] Error: Missing required fields (name, ip, date, traffic)." << std::endl;
            return std::nullopt;
        }
        return rec;
    }

    void process_batch_commands_standalone(
        const std::string& input_filename,
        const std::string& output_filename,
        Database& db,
        TariffPlan& tariff_plan,
        const std::string& initial_tariff_filepath,
        const std::string& data_work_dir
    ) {
        std::ifstream infile(input_filename);
        if (!infile) {
            std::cerr << get_current_timestamp() << " [Batch] Error: Cannot open input file '" << input_filename << "'." << std::endl;
            return;
        }

        std::ofstream outfile(output_filename);
        if (!outfile) {
            std::cerr << get_current_timestamp() << " [Batch] Error: Cannot open output file '" << output_filename << "'." << std::endl;
            infile.close();
            return;
        }

        outfile << get_current_timestamp() << " [Batch] Processing Started. Input: " << input_filename << std::endl;
        outfile << "---------------------------------------------" << std::endl;

        std::string line;
        int line_num = 0;
        while (std::getline(infile, line)) {
            line_num++;
            line = trim_string(line);
            if (line.empty() || line[0] == '#') continue;

            outfile << "\n" << get_current_timestamp() << " [Batch] Query " << line_num << ": " << line << std::endl;
            std::stringstream ss_command_line(line);
            std::string command_verb;
            ss_command_line >> command_verb;
            command_verb = to_lower_util(command_verb);
            std::string payload;
            std::getline(ss_command_line >> std::ws, payload);
            payload = trim_string(payload);

            if (command_verb == "select") {
                auto parsed_query_opt = QueryParser::parse_select(line);
                if (parsed_query_opt) {
                    SelectQuery current_parsed_query = *parsed_query_opt;
                    std::vector<std::vector<std::string>> results_table = db.get_formatted_select_results(current_parsed_query);
                    size_t data_rows = 0;
                    if (!results_table.empty()) {
                        if (results_table.size() > 1) {
                            data_rows = results_table.size() - 1;
                        } else if (results_table.size() == 1) {
                            bool is_header = false;
                            if (!results_table[0].empty()) {
                                const auto& default_fields = ProviderRecord::get_all_field_names();
                                if (!default_fields.empty()){
                                    if (current_parsed_query.wants_all_fields() && results_table[0][0] == default_fields[0]) is_header = true;
                                    else if (!current_parsed_query.select_fields.empty() && results_table[0][0] == current_parsed_query.select_fields[0]) is_header = true;
                                }
                            }
                            if (!is_header && !results_table[0].empty()) data_rows = 1;
                        }
                    }

                    if (data_rows == 0) {
                        outfile << "Status: No records found." << std::endl;
                    } else {
                        outfile << "Status: Success (" << data_rows << " data rows found)." << std::endl;
                        outfile << "Results:" << std::endl;
                        std::vector<size_t> col_widths;
                         if(!results_table.empty() && !results_table[0].empty()){
                            col_widths.resize(results_table[0].size(), 0);
                            for(const auto& row_val : results_table){
                                 for(size_t i=0; i < row_val.size() && i < col_widths.size(); ++i){
                                    col_widths[i] = std::max(col_widths[i], row_val[i].length());
                                }
                            }
                        }
                        bool first_row_hdr = !results_table.empty() && !results_table[0].empty();
                        for (size_t r_idx = 0; r_idx < results_table.size(); ++r_idx) {
                            const auto& row_data = results_table[r_idx];
                            if(row_data.empty()) continue;
                            for (size_t i = 0; i < row_data.size() && i < col_widths.size(); ++i) {
                                outfile << std::left << std::setw(static_cast<int>(col_widths[i] + 2)) << row_data[i];
                            }
                            outfile << "\n";
                            if (r_idx == 0 && first_row_hdr && !col_widths.empty() && results_table.size() > 1) {
                                for (size_t width : col_widths) outfile << std::string(width + 2, '-');
                                outfile << "\n";
                            }
                        }
                         outfile << std::right;
                    }
                } else {
                    outfile << "Status: Error parsing SELECT query: " << line << std::endl;
                }
            } else if (command_verb == "add") {
                std::stringstream ss_add_payload(payload);
                auto record_opt = parse_add_command_payload_standalone(ss_add_payload);
                if (record_opt) {
                    if (db.add_record(*record_opt)) {
                        outfile << "Status: Record added successfully." << std::endl;
                    } else {
                        outfile << "Status: Failed to add record (DB error, invalid, or duplicate)." << std::endl;
                    }
                } else {
                    outfile << "Status: Error parsing ADD command payload." << std::endl;
                }
            } else if (command_verb == "delete") {
                if (payload.empty()) {
                    outfile << "Status: Error - DELETE command requires WHERE conditions." << std::endl;
                } else {
                    std::string query_for_delete = "SELECT * WHERE " + payload;
                    auto parsed_query_opt = QueryParser::parse_select(query_for_delete);
                    if (parsed_query_opt) {
                        size_t count = db.delete_records(*parsed_query_opt);
                        outfile << "Status: " << count << " record(s) deleted." << std::endl;
                    } else {
                        outfile << "Status: Error parsing DELETE command's WHERE clause: " << payload << std::endl;
                    }
                }
            } else if (command_verb == "calculate_bill") {
                std::stringstream ss_bill_payload(payload);
                std::string start_date_str, end_date_str, target_query_conditions_str;
                ss_bill_payload >> start_date_str >> end_date_str;
                std::getline(ss_bill_payload >> std::ws, target_query_conditions_str);
                target_query_conditions_str = trim_string(target_query_conditions_str);

                std::string full_target_query_str = "SELECT *";
                if (!target_query_conditions_str.empty()){
                    if (to_lower_util(target_query_conditions_str).rfind("where ", 0) == 0) {
                        full_target_query_str += " " + target_query_conditions_str;
                    } else {
                        full_target_query_str += " WHERE " + target_query_conditions_str;
                    }
                }

                auto start_date_opt = Date::from_string(trim_string(start_date_str));
                auto end_date_opt = Date::from_string(trim_string(end_date_str));
                auto target_query_opt = QueryParser::parse_select(full_target_query_str);

                if (start_date_opt && end_date_opt && target_query_opt) {
                    if (*end_date_opt < *start_date_opt) {
                        outfile << "Status: Error - End date before start date for billing." << std::endl;
                    } else {
                        auto bill_opt = db.calculate_bill(*target_query_opt, *start_date_opt, *end_date_opt, tariff_plan);
                        if (bill_opt) {
                            outfile << "Status: Success. Calculated Bill: " << std::fixed << std::setprecision(2) << *bill_opt << std::defaultfloat << std::endl;
                        } else {
                            outfile << "Status: Error calculating bill (perhaps no matching records in period or invalid tariff)." << std::endl;
                        }
                    }
                } else {
                    outfile << "Status: Error parsing CALCULATE_BILL command parameters." << std::endl;
                    if(!start_date_opt) outfile << " (Invalid start date: " << start_date_str << ")" << std::endl;
                    if(!end_date_opt) outfile << " (Invalid end date: " << end_date_str << ")" << std::endl;
                    if(!target_query_opt) outfile << " (Invalid WHERE clause for bill: [" << target_query_conditions_str << "] from full query [" << full_target_query_str <<"])" << std::endl;
                }
            } else if (command_verb == "load_tariff") {
                std::string tariff_file_arg_from_script = payload;
                std::string path_to_load_tariff;

                if (tariff_file_arg_from_script.empty()) {
                    path_to_load_tariff = initial_tariff_filepath;
                } else if (tariff_file_arg_from_script.front() == '/' ||
                           (tariff_file_arg_from_script.length() > 1 && tariff_file_arg_from_script[1] == ':' &&
                            (tariff_file_arg_from_script[2] == '\\' || tariff_file_arg_from_script[2] == '/'))) {
                    path_to_load_tariff = tariff_file_arg_from_script;
                } else {
                    path_to_load_tariff = data_work_dir + "/" + tariff_file_arg_from_script;
                }
                
                outfile << get_current_timestamp() << " [Batch] Attempting to LOAD_TARIFF from: " << path_to_load_tariff << std::endl;
                // Валидируем ПОЛНЫЙ путь с помощью is_valid_cmd_argument_path
                if (is_valid_cmd_argument_path(path_to_load_tariff)) {
                    if (tariff_plan.load_from_file(path_to_load_tariff)) {
                        outfile << "Status: Tariff plan loaded successfully from '" << path_to_load_tariff << "'." << std::endl;
                    } else {
                        outfile << "Status: Failed to load tariff plan from '" << path_to_load_tariff << "' (check file existence and format)." << std::endl;
                    }
                } else {
                     outfile << "Status: Error - Invalid constructed tariff filepath for LOAD_TARIFF: " << path_to_load_tariff << std::endl;
                }
            } else {
                outfile << "Status: Unknown command '" << command_verb << "'." << std::endl;
            }
            outfile << "---------------------------------------------" << std::endl;
        }

        outfile << get_current_timestamp() << " [Batch] Processing Finished." << std::endl;
        infile.close();
        outfile.close();
    }
} // namespace StandaloneBatch


int main(int argc, char* argv[]) {
    try {
        std::locale::global(std::locale(""));
    } catch (const std::runtime_error& e) {
        std::cerr << get_current_timestamp() << " [MainStandalone] Warning: Could not set global locale from environment: " << e.what() << ". Trying C.UTF-8." << std::endl;
        try {
            std::locale::global(std::locale("C.UTF-8"));
        } catch (const std::runtime_error& e2) {
            std::cerr << get_current_timestamp() << " [MainStandalone] Warning: Could not set C.UTF-8 locale: " << e2.what() << ". Unicode in paths might not be handled correctly." << std::endl;
        }
    }

    std::string db_filename_main = "data/provider_data.txt";
    std::string tariff_filename_main = "data/tariff.cfg";
    std::string batch_input_file_main;
    std::string batch_output_file_main;
    bool batch_mode_main = false;

    std::vector<std::string> args(argv + 1, argv + argc);
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--input" && i + 1 < args.size()) {
            batch_input_file_main = args[++i];
            batch_mode_main = true;
        } else if (args[i] == "--output" && i + 1 < args.size()) {
            batch_output_file_main = args[++i];
            batch_mode_main = true;
        } else if (args[i] == "--dbfile" && i + 1 < args.size()) {
            db_filename_main = args[++i];
        } else if (args[i] == "--tariff" && i + 1 < args.size()) {
            tariff_filename_main = args[++i];
        } else {
            if (!batch_mode_main) {
                 if (i == 0 && args[i].rfind("--",0) !=0 ) db_filename_main = args[i];
                 else if (i == 1 && args[i].rfind("--",0) !=0 ) tariff_filename_main = args[i];
                 else {
                    std::cerr << "Warning: Unrecognized argument or missing value: " << args[i] << std::endl;
                 }
            } else {
                 std::cerr << "Warning: Unrecognized argument in batch mode: " << args[i] << std::endl;
            }
        }
    }

    if (batch_mode_main && (batch_input_file_main.empty() || batch_output_file_main.empty())) {
        std::cerr << "Error: For batch mode, both --input and --output files must be specified." << std::endl;
        std::cerr << "Usage: " << argv[0] << " --input <queryfile> --output <resultfile> [--dbfile <path>] [--tariff <path>]" << std::endl;
        std::cerr << "Or for interactive mode: " << argv[0] << " [<dbfile_path> [<tariff_path>]]" << std::endl;
        return 1;
    }

    if (!is_valid_cmd_argument_path(db_filename_main)) { std::cerr << get_current_timestamp() << " [MainStandalone] Error: Invalid DB filepath argument: " << db_filename_main << std::endl; return 1;}
    if (!is_valid_cmd_argument_path(tariff_filename_main)) { std::cerr << get_current_timestamp() << " [MainStandalone] Error: Invalid Tariff filepath argument: " << tariff_filename_main << std::endl; return 1;}
    if (batch_mode_main) {
        if (!is_valid_cmd_argument_path(batch_input_file_main)) { std::cerr << get_current_timestamp() << " [MainStandalone] Error: Invalid Batch Input filepath argument: " << batch_input_file_main << std::endl; return 1;}
        if (!is_valid_cmd_argument_path(batch_output_file_main)) { std::cerr << get_current_timestamp() << " [MainStandalone] Error: Invalid Batch Output filepath argument: " << batch_output_file_main << std::endl; return 1;}
    }

    ensure_directory_exists_util(db_filename_main);
    ensure_directory_exists_util(tariff_filename_main);
    if (batch_mode_main) {
        ensure_directory_exists_util(batch_output_file_main);
    }

    std::cout << get_current_timestamp() << " [MainStandalone] Using Database File: " << db_filename_main << std::endl;
    std::cout << get_current_timestamp() << " [MainStandalone] Using Tariff File:   " << tariff_filename_main << std::endl;

    Database db_standalone;
    TariffPlan tariff_standalone;

    if (!db_standalone.load_from_file(db_filename_main)) {
        std::cerr << get_current_timestamp() << " [MainStandalone] Warning: There were issues loading the database from '" << db_filename_main << "'. Application may not function as expected." << std::endl;
    }
    if (!tariff_standalone.load_from_file(tariff_filename_main)) {
        std::cerr << get_current_timestamp() << " [MainStandalone] Warning: There were issues loading the tariff plan from '" << tariff_filename_main << "'. Default rates might be used." << std::endl;
    }

    if (batch_mode_main) {
        std::cout << get_current_timestamp() << " [MainStandalone] Running in BATCH mode." << std::endl;
        std::cout << "Input query file:  " << batch_input_file_main << std::endl;
        std::cout << "Output result file: " << batch_output_file_main << std::endl;
        
        std::string work_dir_path_for_batch = ".";
        if (!tariff_filename_main.empty()) { // Используем путь к основному файлу тарифа для определения WORK_DIR
            size_t last_slash = tariff_filename_main.find_last_of("/\\");
            if (last_slash != std::string::npos) {
                work_dir_path_for_batch = tariff_filename_main.substr(0, last_slash);
            }
        }
        StandaloneBatch::process_batch_commands_standalone(batch_input_file_main, batch_output_file_main, db_standalone, tariff_standalone, tariff_filename_main, work_dir_path_for_batch);

        std::cout << get_current_timestamp() << " [MainStandalone] Attempting to save database to '" << db_filename_main << "' after batch processing..." << std::endl;
        if (db_standalone.save_to_file(db_filename_main)) {
            std::cout << get_current_timestamp() << " [MainStandalone] Database saved successfully after batch mode." << std::endl;
        } else {
            std::cerr << get_current_timestamp() << " [MainStandalone] Failed to save database after batch mode." << std::endl;
        }

    } else {
        std::cout << get_current_timestamp() << " [MainStandalone] Running in INTERACTIVE mode." << std::endl;
        UserInterface ui(db_standalone, tariff_standalone, db_filename_main, tariff_filename_main);
        try {
            ui.run();
        } catch (const std::exception& e) {
            std::cerr << "\n" << get_current_timestamp() << " [MainStandalone] An unexpected std::exception occurred in UI: " << e.what() << std::endl;
            std::string emergency_save_path = ui.get_db_filename_used().empty() ? "data/db.emergency_save" : ui.get_db_filename_used() + ".emergency_save";
            std::cerr << "Attempting to emergency save database to '" << emergency_save_path << "'..." << std::endl;
            if (db_standalone.save_to_file(emergency_save_path)) {
                 std::cerr << "Emergency save successful." << std::endl;
            } else {
                 std::cerr << "Emergency save FAILED." << std::endl;
            }
            return 1;
        } catch (...) {
            std::cerr << "\n" << get_current_timestamp() << " [MainStandalone] An unknown non-standard error occurred in UI." << std::endl;
            std::string emergency_save_path = ui.get_db_filename_used().empty() ? "data/db.emergency_save" : ui.get_db_filename_used() + ".emergency_save";
            std::cerr << "Attempting to emergency save database to '" << emergency_save_path << "'..." << std::endl;
            if (db_standalone.save_to_file(emergency_save_path)) {
                 std::cerr << "Emergency save successful." << std::endl;
            } else {
                 std::cerr << "Emergency save FAILED." << std::endl;
            }
            return 1;
        }
    }
    return 0;
}

