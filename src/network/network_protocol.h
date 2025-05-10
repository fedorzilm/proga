#ifndef NETWORK_PROTOCOL_H
#define NETWORK_PROTOCOL_H

#include <string>
#include <vector>
#include <sstream>
#include <iomanip> 
#include <cstdint> 
#include <limits> // Для std::numeric_limits в parse_message_payload (если используется ignore)

const int MAX_MSG_LEN = 8192; // Максимальная длина полезной нагрузки сообщения (без заголовка длины и типа)
const int LENGTH_PREFIX_DIGITS = 8; // Количество цифр для префикса длины

enum class CommandType : uint8_t {
    PING = 0,
    EXECUTE_QUERY = 1, 
    SHUTDOWN_SERVER_CMD, 
    EDIT_RECORD_CMD,     // <--- ДОБАВЛЕННЫЙ ТИП КОМАНДЫ
    ERROR_RESPONSE,
    SUCCESS_RESPONSE_DATA,
    SUCCESS_RESPONSE_NO_DATA,
    ACK_PONG 
};

// Функция для обрамления сообщения: добавляет префикс длины
inline std::string frame_message(const std::string& payload) {
    if (payload.length() > static_cast<size_t>(MAX_MSG_LEN) * 100) { // Добавим некую разумную проверку, чтобы не создавать гигантские префиксы для ошибок
        // Это аварийная ситуация, возможно, стоит бросить исключение или вернуть специальную ошибку
        // Здесь просто для примера обрежем или вернем ошибку
        // throw std::runtime_error("Payload too large for framing"); 
        // или
        // return "ERROR_PAYLOAD_TOO_LARGE"; 
    }
    std::stringstream ss;
    // Убедимся, что длина payload не превышает то, что может быть представлено LENGTH_PREFIX_DIGITS
    // Например, если LENGTH_PREFIX_DIGITS = 8, максимальная длина 99,999,999.
    // payload.length() должно быть меньше этого.
    ss << std::setw(LENGTH_PREFIX_DIGITS) << std::setfill('0') << payload.length();
    return ss.str() + payload;
}

// Создает полную строку запроса (с типом команды и полезной нагрузкой), готовую к обрамлению
inline std::string create_request_string(CommandType cmd, const std::string& payload_data) {
    // Тип команды как int, затем \n, затем payload
    std::string full_payload = std::to_string(static_cast<int>(cmd)) + "\n" + payload_data;
    return frame_message(full_payload);
}

// Создает полную строку ответа (с типом ответа и данными), готовую к обрамлению
inline std::string create_response_string(CommandType cmd_response_type, const std::string& data_payload) {
    std::string full_payload = std::to_string(static_cast<int>(cmd_response_type)) + "\n" + data_payload;
    return frame_message(full_payload);
}

// Структура для разобранного сообщения (после удаления обрамления длины)
struct ParsedMessage {
    CommandType type = CommandType::ERROR_RESPONSE; // По умолчанию, если парсинг не удался
    std::string payload_data;
    bool valid = false;
};

// Разбирает полезную нагрузку сообщения (после удаления обрамления длины)
// на тип команды и фактические данные
inline ParsedMessage parse_message_payload(const std::string& full_payload_after_length_removed) {
    ParsedMessage result;
    std::stringstream ss(full_payload_after_length_removed);
    int cmd_int_val = -1; 
    char newline_char_check;

    // Пытаемся прочитать целое число (тип команды)
    if (ss >> cmd_int_val) {
        // Пытаемся прочитать следующий символ, который должен быть '\n'
        if (ss.get(newline_char_check) && newline_char_check == '\n') {
            // Проверяем, что cmd_int_val находится в допустимом диапазоне CommandType
            if (cmd_int_val >= static_cast<int>(CommandType::PING) && 
                cmd_int_val <= static_cast<int>(CommandType::ACK_PONG)) { // ACK_PONG - последний в enum
                 result.type = static_cast<CommandType>(cmd_int_val);
                 // Все остальное в потоке - это payload_data
                 std::getline(ss, result.payload_data); 
                 result.valid = true;
            } else {
                result.payload_data = "Invalid command type integer value in message: " + std::to_string(cmd_int_val);
                // result.valid остается false
            }
        } else {
            // Если после числа не '\n', значит формат нарушен
            result.payload_data = "Missing newline separator after command type in message.";
            if (ss.eof() && newline_char_check != '\n' && cmd_int_val != -1){ // Число прочитали, но \n нет и конец строки
                 result.payload_data = "Command type integer present, but no newline separator before end of message.";
            } else if (newline_char_check != '\n'){ // Какой-то другой символ после числа
                 result.payload_data = "Invalid character '" + std::string(1, newline_char_check) + "' after command type, expected newline.";
            }
            // result.valid остается false
        }
    } else {
        // Не удалось прочитать целое число в начале
        result.payload_data = "Could not parse command type integer from message beginning.";
        // result.valid остается false
    }
    return result;
}

#endif // NETWORK_PROTOCOL_H
