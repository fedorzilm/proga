#ifndef DB_CLIENT_H
#define DB_CLIENT_H

#include "../network/socket_utils.h"
#include "../common_defs.h" // Для TrafficData, если ProviderRecord.h не включен и оно там
#include <string>
#include <utility> 
#include <optional> 
#include <vector> 

// Forward declare TrafficData if ProviderRecord.h isn't included,
// или если common_defs.h не объявляет его, а он нужен здесь (например, для build_edit_command...)
// В нашем случае common_defs.h объявляет TrafficData.

class DBClient {
public:
    DBClient(std::string client_id = "DefaultClient");
    ~DBClient(); 

    bool connect_to_server(const std::string& host, int port);
    void disconnect();

    // Отправляет "сырую" командную строку серверу (для команд типа SELECT, ADD, DELETE через EXECUTE_QUERY)
    std::pair<bool, std::string> send_raw_command(const std::string& command_line);
    
    // Отправляет структурированную команду EDIT с новым CommandType::EDIT_RECORD_CMD
    // key_*: поля для идентификации записи
    // set_*: опциональные новые значения (строки). Пустой optional или пустая строка внутри optional означает "не менять это поле".
    std::pair<bool, std::string> send_edit_command(
        const std::string& key_name, 
        const std::string& key_ip_str, // Передаем как строки для удобства формирования payload
        const std::string& key_date_str,
        const std::optional<std::string>& set_name,
        const std::optional<std::string>& set_ip_str,
        const std::optional<std::string>& set_date_str,
        const std::optional<std::string>& set_traffic_as_string 
    );

    std::pair<bool, std::string> send_ping(); 

    void run_interactive();
    void run_batch(const std::string& input_filename, const std::string& output_filename);

private:
    Network::TCPSocket socket_;
    std::string client_id_; 
    
    void display_formatted_table_response(const std::string& table_data_str);
    // Вспомогательные методы для интерактивного режима
    std::optional<std::string> build_add_command_from_interactive_input(); 
    std::optional<std::string> build_edit_command_payload_from_interactive_input(); // Возвращает payload для EDIT_RECORD_CMD
};

#endif // DB_CLIENT_H
