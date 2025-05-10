#ifndef USER_INTERFACE_H
#define USER_INTERFACE_H

#include "database.h"
#include "tariff_plan.h"
#include "common_defs.h" 
#include <string>
#include <optional> 

class UserInterface {
public:
    UserInterface(Database& db, TariffPlan& tariff, const std::string& db_file, const std::string& tariff_file);
    void run();

    std::string get_db_filename_used() const { return db_filename_used; } 
    std::string get_tariff_filename_used() const { return tariff_filename_used; }

private:
    Database& database;
    TariffPlan& tariff_plan;
    std::string db_filename_used; 
    std::string tariff_filename_used; 

    void display_menu();
    void handle_add();
    void handle_select();
    void handle_delete();
    void handle_edit();
    void handle_print_all();
    void handle_calculate_bill();
    void handle_load_tariff();
    void handle_save_db();

    template<typename T>
    std::optional<T> get_validated_input(const std::string& prompt, bool allow_empty_for_edit = false);
    std::optional<std::string> get_validated_line(const std::string& prompt, bool allow_empty = false);
    std::optional<TrafficData> get_validated_traffic_data_interactive(const std::string& prompt_detail); 
};

#endif // USER_INTERFACE_H
