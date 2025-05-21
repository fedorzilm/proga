/*!
 * \file logger.cpp
 * \author Fedor Zilnitskiy
 * \brief Реализация статического класса Logger для логирования сообщений.
 */
#include "logger.h"

// Инициализация статических членов класса Logger
LogLevel Logger::current_level_ = LogLevel::INFO; // Уровень по умолчанию
std::mutex Logger::log_mutex_;
std::ofstream Logger::log_file_stream_;
bool Logger::use_file_ = false;
bool Logger::initialized_ = false;


void Logger::init(LogLevel initial_level, const std::string& log_file_path) {
    std::lock_guard<std::mutex> lock(log_mutex_); // Защита на время инициализации

    // Если логгер уже был инициализирован и файл был открыт, закрываем его перед переинициализацией
    if (initialized_ && use_file_ && log_file_stream_.is_open()) {
        // Не используем log_internal здесь, так как он может зависеть от initialized_
        // и мы находимся в процессе изменения этого состояния.
        std::cout << "[" << get_timestamp() << "] [REINITIALIZATION] ["
                  << get_thread_id_str() << "] "
                  << "Логгер переинициализируется. Закрытие предыдущего файла лога." << std::endl;
        if (log_file_stream_.is_open()) { // Дополнительная проверка перед закрытием
            log_file_stream_ << "[" << get_timestamp() << "] [REINITIALIZATION] ["
                             << get_thread_id_str() << "] "
                             << "Логгер переинициализируется. Закрытие этого файла лога." << std::endl;
            log_file_stream_.close();
        }
        use_file_ = false; // Сбрасываем флаг использования файла
    }

    current_level_ = initial_level; // Устанавливаем новый уровень

    if (!log_file_path.empty()) {
        log_file_stream_.open(log_file_path, std::ios::app); // Открываем в режиме добавления
        if (log_file_stream_.is_open()) {
            use_file_ = true;
            // Формируем сообщение об инициализации
            std::string init_msg = "Логирование в файл: " + log_file_path + ". Уровень: " + std::to_string(static_cast<int>(current_level_));
            // Прямой вывод в файл и консоль, так как log_internal может еще не работать корректно с initialized_
            log_file_stream_ << "[" << get_timestamp() << "] [INITIALIZATION] [" << get_thread_id_str() << "] " << init_msg << std::endl;
            std::cout << "[" << get_timestamp() << "] [INITIALIZATION] [" << get_thread_id_str() << "] " << init_msg << std::endl;
        } else {
            use_file_ = false;
            // Ошибка открытия файла лога - выводим в stderr
            std::cerr << "[" << get_timestamp() << "] [INITIALIZATION] [ОШИБКА] ["
                      << get_thread_id_str() << "] "
                      << "Не удалось открыть файл лога: " << log_file_path + ". Логирование только в консоль. Уровень: " << std::to_string(static_cast<int>(current_level_)) << std::endl;
        }
    } else {
        use_file_ = false;
        // Если не используем файл, выводим сообщение об инициализации в консоль
         std::cout << "[" << get_timestamp() << "] [INITIALIZATION] ["
                   << get_thread_id_str() << "] "
                   << "Логирование только в консоль. Уровень: " << std::to_string(static_cast<int>(current_level_)) << std::endl;
    }
    initialized_ = true; // Устанавливаем флаг инициализации в конце
}

Logger::~Logger() {
    // Этот деструктор для статического класса обычно не имеет большого значения,
    // но для полноты и возможной очистки ресурсов (например, закрытие файла).
    std::lock_guard<std::mutex> lock(log_mutex_);
    if (log_file_stream_.is_open()) {
        // Не используем log_internal, так как объект может быть уже частично разрушен
        log_file_stream_ << "[" << get_timestamp() << "] [COMPLETION] ["
                         << get_thread_id_str() << "] "
                         << "Логгер завершает работу. Закрытие файла лога." << std::endl;
        log_file_stream_.close();
    }
    initialized_ = false; 
}

void Logger::setLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    LogLevel old_level = current_level_;
    current_level_ = level;
    
    // Логируем изменение уровня, если логгер инициализирован.
    // Сообщение об изменении уровня важно, поэтому логируем его даже если старый уровень был NONE.
    if (initialized_) {
        log_internal(LogLevel::INFO, "УРОВЕНЬ", "", "Уровень логирования изменен с " + std::to_string(static_cast<int>(old_level)) +
                                               " на " + std::to_string(static_cast<int>(current_level_)));
    } else {
        // Если логгер еще не инициализирован, просто выводим в консоль
        std::cout << "[" << get_timestamp() << "] [LEVEL] ["
                  << get_thread_id_str() << "] "
                  << "Уровень логирования установлен в " + std::to_string(static_cast<int>(current_level_)) 
                  << " (Логгер еще не полностью инициализирован)." << std::endl;
    }
}

LogLevel Logger::getLevel() noexcept {
    // Чтение int потокобезопасно, если он не слишком большой (для current_level_ это enum class, что безопасно).
    // Для большей строгости можно было бы обернуть в мьютекс или использовать std::atomic<LogLevel>.
    return current_level_;
}

std::string Logger::get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t t_now = std::chrono::system_clock::to_time_t(now);

    std::tm timeinfo_tm{}; 
#ifdef _WIN32
    localtime_s(&timeinfo_tm, &t_now); 
#else
    localtime_r(&t_now, &timeinfo_tm); 
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

void Logger::log_internal(LogLevel level, const std::string& level_str_param, const std::string& module, const std::string& message) {
    // Эта функция вызывается под мьютексом log_mutex_ из публичных методов и setLevel.
    // init() имеет свою логику вывода сообщений INIT/REINIT.

    if (!initialized_) { // Если логгер не инициализирован, кроме специальных случаев в init/setLevel
        // Только критические ошибки могут быть выведены до инициализации, и то напрямую в stderr.
        // Этот путь не должен часто срабатывать, если init вызывается первым.
        if (level >= LogLevel::ERROR) {
             std::cerr << "[INITIALIZATION-WARNING] [" << get_timestamp() << "] [" << level_str_param << "] "
                       << "[" << get_thread_id_str() << "] "
                       << (!module.empty() ? "[" + module + "] " : "")
                       << message << std::endl;
        }
        return;
    }
    
    // Убираем пробелы из level_str_param, если они там есть для выравнивания
    std::string level_str = level_str_param;
    size_t first_char = level_str.find_first_not_of(' ');
    if (std::string::npos != first_char) {
        size_t last_char = level_str.find_last_not_of(' ');
        level_str = level_str.substr(first_char, (last_char - first_char + 1));
    }


    // Сообщения об изменении уровня логируются всегда, если новый уровень не NONE,
    // или если старый уровень не был NONE (чтобы залогировать переход в NONE).
    bool force_log_level_change = (level_str_param == "УРОВЕНЬ" || level_str_param == "УРОВЕНЬ "); // Используем русское слово
    
    if (level >= current_level_ || force_log_level_change) {
        // Если текущий уровень NONE, то логируем только сообщения УРОВЕНЬ
        if (current_level_ == LogLevel::NONE && !force_log_level_change) {
            return;
        }

        std::string formatted_message = "[" + get_timestamp() + "] [" + level_str + "] " + // Используем очищенный level_str
                                        "[" + get_thread_id_str() + "] " +
                                        (!module.empty() ? "[" + module + "] " : "") +
                                        message;

        if (use_file_ && log_file_stream_.is_open()) {
            log_file_stream_ << formatted_message << std::endl;
            if (level >= LogLevel::ERROR) { // Сбрасываем буфер для ошибок
                log_file_stream_.flush();
            }
        }

        // Дублирование в консоль: ERROR и WARN всегда в cerr, остальное в cout.
        // Сообщения УРОВЕНЬ также в cout.
        if (level == LogLevel::ERROR || level == LogLevel::WARN) {
             std::cerr << formatted_message << std::endl;
        } else if (level == LogLevel::INFO || level == LogLevel::DEBUG || force_log_level_change) {
             std::cout << formatted_message << std::endl;
        }
    }
}

void Logger::debug(const std::string& message, const std::string& module) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    log_internal(LogLevel::DEBUG, "DEBUG", module, message);
}

void Logger::info(const std::string& message, const std::string& module) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    log_internal(LogLevel::INFO, "INFO", module, message);
}

void Logger::warn(const std::string& message, const std::string& module) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    log_internal(LogLevel::WARN, "WARNING", module, message);
}

void Logger::error(const std::string& message, const std::string& module) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    log_internal(LogLevel::ERROR, "ERROR", module, message);
}
