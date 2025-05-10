#include "db_server.h"
#include "../database.h"
#include "../tariff_plan.h"
#include "../common_defs.h"
#include <iostream>
#include <csignal>
#include <memory>
#include <vector>
#include <string>
#include <locale> // Для std::locale

std::unique_ptr<DBServer> global_server_ptr_instance_final;

void server_signal_handler_final(int signum) {
    std::cout << "\n" << get_current_timestamp() << " [ServerMain] Interrupt signal (" << signum << ") received." << std::endl;
    if (global_server_ptr_instance_final) {
        std::cout << get_current_timestamp() << " [ServerMain] Signaling server to stop..." << std::endl;
        global_server_ptr_instance_final->stop();
    }
}

int main(int argc, char* argv[]) {
    try {
        std::locale::global(std::locale(""));
    } catch (const std::runtime_error& e) {
        std::cerr << get_current_timestamp() << " [ServerMain] Warning: Could not set global locale: " << e.what() << ". Trying C.UTF-8." << std::endl;
        try { std::locale::global(std::locale("C.UTF-8")); } catch (const std::runtime_error& e2) {
            std::cerr << get_current_timestamp() << " [ServerMain] Warning: Could not set C.UTF-8 locale: " << e2.what() << std::endl;
        }
    }

    Network::init_networking();

    std::string db_filename = "data/provider_data.txt";
    std::string tariff_filename = "data/tariff.cfg";
    int port = 12345;

    std::vector<std::string> args_vec(argv + 1, argv + argc);
    for (size_t i = 0; i < args_vec.size(); ++i) {
        const std::string& arg = args_vec[i];
        if ((arg == "-p" || arg == "--port") && i + 1 < args_vec.size()) {
            try {
                port = std::stoi(args_vec[++i]);
                if (port <= 0 || port > 65535) {
                     std::cerr << "Invalid port number: " << port << ". Using default 12345." << std::endl;
                     port = 12345;
                }
            } catch (const std::exception& e) {
                std::cerr << "Invalid port number argument: '" << args_vec[i] << "'. Error: " << e.what() << ". Using default port 12345." << std::endl;
                 port = 12345;
            }
        } else if (arg == "--dbfile" && i + 1 < args_vec.size()) {
            db_filename = args_vec[++i];
        } else if (arg == "--tariff" && i + 1 < args_vec.size()) {
            tariff_filename = args_vec[++i];
        } else {
            std::cerr << "Usage: " << argv[0] << " [--port <port_num>] [--dbfile <path>] [--tariff <path>]" << std::endl;
            Network::cleanup_networking();
            return 1;
        }
    }

    if (!is_valid_cmd_argument_path(db_filename)) { std::cerr << get_current_timestamp() << " [ServerMain] Error: Invalid DB filepath argument: " << db_filename << std::endl; Network::cleanup_networking(); return 1;}
    if (!is_valid_cmd_argument_path(tariff_filename)) { std::cerr << get_current_timestamp() << " [ServerMain] Error: Invalid Tariff filepath argument: " << tariff_filename << std::endl; Network::cleanup_networking(); return 1;}

    ensure_directory_exists_util(db_filename);
    ensure_directory_exists_util(tariff_filename);

    std::cout << get_current_timestamp() << " [ServerMain] Using Database File: " << db_filename << std::endl;
    std::cout << get_current_timestamp() << " [ServerMain] Using Tariff File:   " << tariff_filename << std::endl;

    Database db_instance;
    TariffPlan tariff_instance;

    if (!db_instance.load_from_file(db_filename)) {
        std::cerr << get_current_timestamp() << " [ServerMain] Warning: Could not fully load database from '" << db_filename << "'. May start empty or partially loaded." << std::endl;
    } else {
        std::cout << get_current_timestamp() << " [ServerMain] Database loaded with " << db_instance.record_count() << " records." << std::endl;
    }
    if (!tariff_instance.load_from_file(tariff_filename)) {
        std::cerr << get_current_timestamp() << " [ServerMain] Warning: Could not load tariff plan from '" << tariff_filename << "'. Using default rates (0.0)." << std::endl;
    } else {
        std::cout << get_current_timestamp() << " [ServerMain] Tariff plan loaded." << std::endl;
    }

    global_server_ptr_instance_final = std::make_unique<DBServer>(db_instance, tariff_instance, tariff_filename);

    signal(SIGINT, server_signal_handler_final);
    signal(SIGTERM, server_signal_handler_final);

    try {
        global_server_ptr_instance_final->start(port);
        global_server_ptr_instance_final->wait_for_shutdown();
    } catch (const std::exception& e) {
        std::cerr << get_current_timestamp() << " [ServerMain] FATAL unhandled std::exception: " << e.what() << std::endl;
        std::string emergency_save_name = db_filename.empty() ? "data/db_server.emergency_save" : db_filename + ".emergency_server_save";
        std::cerr << get_current_timestamp() << " [ServerMain] Attempting to emergency save database to '" << emergency_save_name << "'..." << std::endl;
        if (db_instance.save_to_file(emergency_save_name)) {
             std::cerr << get_current_timestamp() << " [ServerMain] Emergency save successful." << std::endl;
        } else {
             std::cerr << get_current_timestamp() << " [ServerMain] Emergency save FAILED." << std::endl;
        }
        Network::cleanup_networking();
        return 2;
    }

    std::cout << get_current_timestamp() << " [ServerMain] Server process has stopped. Saving database finally..." << std::endl;
    if (db_instance.save_to_file(db_filename)) {
        std::cout << get_current_timestamp() << " [ServerMain] Database saved successfully." << std::endl;
    } else {
        std::cerr << get_current_timestamp() << " [ServerMain] Failed to save database after server shutdown." << std::endl;
    }

    Network::cleanup_networking();
    return 0;
}

