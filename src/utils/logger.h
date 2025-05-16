// Предполагаемый путь: src/utils/logger.h
#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <iostream>
#include <fstream> // Для возможного вывода в файл
#include <sstream>
#include <mutex>
#include <chrono>
#include <iomanip>  // Для std::put_time, std::setw, std::setfill
#include <thread>   // Для std::this_thread::get_id()

enum class LogLevel {
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERROR = 3,
    NONE  = 4 // Для полного отключения логов
};

class Logger {
public:
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    static void init(LogLevel initial_level = LogLevel::INFO, const std::string& log_file_path = "");
    static void setLevel(LogLevel level);
    static LogLevel getLevel(); // Метод для получения текущего уровня

    static void debug(const std::string& message, const std::string& module = "");
    static void info(const std::string& message, const std::string& module = "");
    static void warn(const std::string& message, const std::string& module = "");
    static void error(const std::string& message, const std::string& module = "");

    // Для более сложного форматирования можно использовать потоковый интерфейс
    // class LogStream { ... };
    // static LogStream log(LogLevel level, const std::string& module = "");

private:
    Logger() = default; // Приватный конструктор для синглтона или статического класса
    ~Logger();          // Для закрытия файла лога, если он используется

    static LogLevel current_level_;
    static std::mutex log_mutex_;
    static std::ofstream log_file_stream_;
    static bool use_file_;
    static bool initialized_;

    static void log_internal(LogLevel level, const std::string& level_str, const std::string& module, const std::string& message);
    static std::string get_timestamp();
    static std::string get_thread_id_str();
};

#endif // LOGGER_H
