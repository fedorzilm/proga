// Предполагаемый путь: src/utils/logger.cpp
#include "logger.h"
#include <vector> // Для std::vector в get_thread_id_str (если используется stringstream)
#include <sstream> // Для std::stringstream в get_thread_id_str

// Инициализация статических членов
LogLevel Logger::current_level_ = LogLevel::INFO; // Уровень по умолчанию INFO
std::mutex Logger::log_mutex_;
std::ofstream Logger::log_file_stream_;
bool Logger::use_file_ = false;
bool Logger::initialized_ = false;


void Logger::init(LogLevel initial_level, const std::string& log_file_path) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    if (initialized_) {
        // Логгер уже инициализирован, возможно, просто меняем уровень или файл
        if (log_file_stream_.is_open()) {
            log_file_stream_.close();
        }
    }

    current_level_ = initial_level;
    if (!log_file_path.empty()) {
        log_file_stream_.open(log_file_path, std::ios::app); // Открываем в режиме добавления
        if (log_file_stream_.is_open()) {
            use_file_ = true;
            log_internal(LogLevel::INFO, "INIT ", "", "Logging to file: " + log_file_path);
        } else {
            use_file_ = false;
            log_internal(LogLevel::ERROR, "INIT ", "", "Failed to open log file: " + log_file_path + ". Logging to console only.");
        }
    } else {
        use_file_ = false;
        log_internal(LogLevel::INFO, "INIT ", "", "Logging to console only.");
    }
    initialized_ = true;
}

Logger::~Logger() { // Деструктор не будет вызван для статического класса, но полезен если Logger станет синглтоном
    std::lock_guard<std::mutex> lock(log_mutex_);
    if (log_file_stream_.is_open()) {
        log_internal(LogLevel::INFO, "END  ", "", "Logger shutting down. Closing log file.");
        log_file_stream_.close();
    }
}

void Logger::setLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    current_level_ = level;
    // Логируем изменение уровня, если новый уровень позволяет это сообщение
    if (LogLevel::INFO >= current_level_) {
         log_internal(LogLevel::INFO, "LEVEL", "", "Log level set to " + std::to_string(static_cast<int>(level)));
    }
}

LogLevel Logger::getLevel() {
    // std::lock_guard<std::mutex> lock(log_mutex_); // Чтение atomic или простого int обычно потокобезопасно
    return current_level_;
}

std::string Logger::get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t t_now = std::chrono::system_clock::to_time_t(now);

    std::tm timeinfo_tm;
#ifdef _WIN32
    localtime_s(&timeinfo_tm, &t_now); // Потокобезопасная версия для Windows
#else
    localtime_r(&t_now, &timeinfo_tm); // Потокобезопасная версия для POSIX
#endif

    std::ostringstream oss;
    oss << std::put_time(&timeinfo_tm, "%Y-%m-%d %H:%M:%S");
    oss << "." << std::setw(3) << std::setfill('0') << ms.count();
    return oss.str();
}

std::string Logger::get_thread_id_str() {
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    return oss.str();
}

void Logger::log_internal(LogLevel level, const std::string& level_str, const std::string& module, const std::string& message) {
    if (!initialized_ && level_str != "INIT ") { // Позволяем INIT сообщениям проходить до полной инициализации
        // Можно выводить в stderr, если логгер еще не готов
        std::cerr << "[" << get_timestamp() << "] [" << level_str << "] "
                  << "[" << get_thread_id_str() << "] "
                  << (!module.empty() ? "[" + module + "] " : "")
                  << message << std::endl;
        return;
    }
    
    if (level >= current_level_) {
        std::string formatted_message = "[" + get_timestamp() + "] [" + level_str + "] " +
                                        "[" + get_thread_id_str() + "] " +
                                        (!module.empty() ? "[" + module + "] " : "") +
                                        message;

        if (use_file_ && log_file_stream_.is_open()) {
            log_file_stream_ << formatted_message << std::endl;
        }

        // Всегда выводим ошибки и предупреждения в cerr, остальное в cout, если не только в файл
        if (!use_file_ || level >= LogLevel::WARN) { // Дублируем WARN/ERROR в консоль, даже если пишем в файл
             std::ostream& console_stream = (level >= LogLevel::WARN) ? std::cerr : std::cout;
             console_stream << formatted_message << std::endl;
        }
    }
}

void Logger::debug(const std::string& message, const std::string& module) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    log_internal(LogLevel::DEBUG, "DEBUG", module, message);
}

void Logger::info(const std::string& message, const std::string& module) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    log_internal(LogLevel::INFO, "INFO ", module, message);
}

void Logger::warn(const std::string& message, const std::string& module) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    log_internal(LogLevel::WARN, "WARN ", module, message);
}

void Logger::error(const std::string& message, const std::string& module) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    log_internal(LogLevel::ERROR, "ERROR", module, message);
}
