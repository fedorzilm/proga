#include "db_client.h"
#include "../common_defs.h"
#include <iostream>
#include <string>
#include <random>
#include <algorithm>
#include <vector>
#include <locale> // Для std::locale

std::string generate_client_id_final(size_t length = 8) {
    const std::string CHARACTERS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::random_device random_device;
    std::mt19937 generator(random_device());
    std::uniform_int_distribution<> distribution(0, static_cast<int>(CHARACTERS.length() - 1));
    std::string random_string;
    std::generate_n(std::back_inserter(random_string), length, [&]() {
        return CHARACTERS[distribution(generator)];
    });
    return "Client-" + random_string;
}

int main(int argc, char* argv[]) {
    try {
        std::locale::global(std::locale(""));
    } catch (const std::runtime_error& e) {
        std::cerr << get_current_timestamp() << " [ClientMain] Warning: Could not set global locale: " << e.what() << ". Trying C.UTF-8." << std::endl;
        try { std::locale::global(std::locale("C.UTF-8")); } catch (const std::runtime_error& e2) {
            std::cerr << get_current_timestamp() << " [ClientMain] Warning: Could not set C.UTF-8 locale: " << e2.what() << std::endl;
        }
    }

    Network::init_networking();

    std::string host = "127.0.0.1";
    int port = 12345;
    std::string batch_input_file;
    std::string batch_output_file;
    bool interactive_mode = true;
    std::string client_id_str = generate_client_id_final();

    std::vector<std::string> args_vec(argv + 1, argv + argc);
    for (size_t i = 0; i < args_vec.size(); ++i) {
        const std::string& arg = args_vec[i];
        if ((arg == "-h" || arg == "--host") && i + 1 < args_vec.size()) {
            host = args_vec[++i];
        } else if ((arg == "-p" || arg == "--port") && i + 1 < args_vec.size()) {
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
        } else if (arg == "--input" && i + 1 < args_vec.size()) {
            batch_input_file = args_vec[++i];
            interactive_mode = false;
        } else if (arg == "--output" && i + 1 < args_vec.size()) {
            batch_output_file = args_vec[++i];
        } else if (arg == "--id" && i + 1 < args_vec.size()) {
            client_id_str = args_vec[++i];
        } else {
             std::cerr << "Usage: " << argv[0] << " [--host <hostname>] [--port <port_num>] [--id <client_id>] [--input <queryfile> --output <resultfile>]" << std::endl;
             Network::cleanup_networking();
             return 1;
        }
    }

    if (!interactive_mode) {
        if (batch_input_file.empty() || batch_output_file.empty()) {
            std::cerr << "Error: For batch mode, both --input and --output files must be specified." << std::endl;
            Network::cleanup_networking();
            return 1;
        }
         if (!is_valid_cmd_argument_path(batch_input_file)){ std::cerr << get_current_timestamp() << " [ClientMain] Error: Batch input filepath argument is invalid: " << batch_input_file << std::endl; Network::cleanup_networking(); return 1;}
         if (!is_valid_cmd_argument_path(batch_output_file)){ std::cerr << get_current_timestamp() << " [ClientMain] Error: Batch output filepath argument is invalid: " << batch_output_file << std::endl; Network::cleanup_networking(); return 1;}
         ensure_directory_exists_util(batch_output_file);
    }

    std::cout << get_current_timestamp() << " [ClientMain] Starting client with ID: " << client_id_str << " connecting to " << host << ":" << port << std::endl;

    DBClient client(client_id_str);
    if (!client.connect_to_server(host, port)) {
        Network::cleanup_networking();
        return 1;
    }

    if (interactive_mode) {
        client.run_interactive();
    } else {
        // Если LOAD_TARIFF поддерживается клиентом в пакетном режиме,
        // и если имя файла в сценарии относительное, db_client.cpp должен будет
        // уметь разрешать этот путь относительно директории, где лежит сам файл сценария (batch_input_file).
        // Сейчас run_batch просто передает команды как есть.
        client.run_batch(batch_input_file, batch_output_file);
    }

    Network::cleanup_networking();
    return 0;
}

