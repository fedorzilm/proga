#ifndef DB_SERVER_H
#define DB_SERVER_H

#include "../network/socket_utils.h"
#include "../database.h"
#include "../tariff_plan.h"
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <map>
#include <optional>
#include <sstream>

// Forward declaration
struct ProviderRecord;

// Structure to hold parsed HTTP request parts
struct ParsedHttpRequest {
    std::string method;
    std::string path;
    std::string http_version;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> query_params;
    std::map<std::string, std::string> form_params;
    std::string body;
    bool is_valid = false;

    static ParsedHttpRequest parse(const std::string& raw_request);
    static std::map<std::string, std::string> parse_query_string(const std::string& query_str);
};


class DBServer {
public:
    DBServer(Database& db, TariffPlan& tariff, const std::string& default_tariff_path);
    ~DBServer();

    void start(int port);
    void stop();
    void wait_for_shutdown();

private:
    void client_connection_dispatcher(Network::TCPSocket client_socket);

    // ИЗМЕНЕННЫЕ ОБЪЯВЛЕНИЯ: initial_data теперь std::string (по значению), как в определениях .cpp
    void custom_protocol_handler_func(Network::TCPSocket client_socket, socket_t client_fd_for_log, std::string initial_data);
    void http_client_handler_func(Network::TCPSocket client_socket, socket_t client_fd_for_log, std::string initial_data);

    std::string process_custom_protocol_command(const std::string& framed_payload_from_client);
    
    // Объявление для метода, который определен в db_server.cpp и будет вызываться
    std::string process_http_request(const ParsedHttpRequest& http_req);
    // ОБЪЯВЛЕНИЕ execute_http_db_command УДАЛЕНО, т.к. вместо него используется process_http_request

    std::string generate_html_form();
    std::string generate_html_response_page(const std::string& title, const std::string& body_content);
    std::string generate_html_table_from_data(const std::vector<std::vector<std::string>>& table_data);

    Database& database_;
    TariffPlan& tariff_plan_;
    std::string default_tariff_filename_;

    Network::TCPSocket listener_socket_;
    std::vector<std::thread> client_threads_;
    std::mutex client_threads_mutex_;
    std::atomic<bool> is_running_;
};

#endif // DB_SERVER_H
