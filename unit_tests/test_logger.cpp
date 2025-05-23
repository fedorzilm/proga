#include "gtest/gtest.h"
#include "logger.h"
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <sstream>      // Для std::ostringstream
#include <regex>        // Для проверки формата логов
#include <algorithm>    // Для std::any_of

// Вспомогательная функция для чтения содержимого файла
std::vector<std::string> read_lines_from_file(const std::string& filename) {
    std::vector<std::string> lines;
    std::ifstream file(filename);
    std::string line;
    if (file.is_open()) {
        while (std::getline(file, line)) {
            lines.push_back(line);
        }
        file.close();
    }
    return lines;
}

// Вспомогательная функция для проверки формата строки лога
bool check_log_format(const std::string& log_line, LogLevel expected_level, const std::string& expected_message_part, const std::string& expected_module_part = "") {
    std::string level_str;
    switch (expected_level) {
        case LogLevel::DEBUG: level_str = "[DEBUG]"; break;
        case LogLevel::INFO:  level_str = "[INFO]";  break;
        case LogLevel::WARN:  level_str = "[WARNING]"; break; 
        case LogLevel::ERROR: level_str = "[ERROR]"; break;
        default: level_str = "[UNKNOWN_IN_TEST]"; break;
    }
    std::regex timestamp_regex(R"(\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}\])");
    // Для УРОВЕНЬ, INITIALIZATION, ПЕРЕИНИЦ - они часть level_str в log_internal
    // или напрямую в выводе init.
    std::string effective_level_str_for_regex = level_str;
    if (expected_module_part == "УРОВЕНЬ" || expected_module_part == "INITIALIZATION" || expected_module_part == "ПЕРЕИНИЦ") {
        effective_level_str_for_regex = "[" + expected_module_part + "]";
    }
    
    std::regex level_regex_pattern(std::regex_replace(effective_level_str_for_regex, std::regex("([\\[\\]])"), "\\$1"));


    std::regex thread_id_regex(R"(\[.+?\])"); 

    bool timestamp_ok = std::regex_search(log_line, timestamp_regex);
    bool level_ok = std::regex_search(log_line, level_regex_pattern);
    
    std::smatch match_ts, match_lvl;
    std::string::const_iterator search_start = log_line.cbegin();
    bool thread_id_ok = false;
    if (std::regex_search(search_start, log_line.cend(), match_ts, timestamp_regex)) {
        search_start = match_ts.suffix().first;
        if (std::regex_search(search_start, log_line.cend(), match_lvl, level_regex_pattern)) {
            search_start = match_lvl.suffix().first;
            std::smatch match_tid;
            if (std::regex_search(search_start, log_line.cend(), match_tid, thread_id_regex) &&
                match_tid.prefix().str().find_first_not_of(" ") == std::string::npos) { 
                 thread_id_ok = true;
            }
        }
    }

    bool message_ok = log_line.find(expected_message_part) != std::string::npos;
    bool module_ok = true; // По умолчанию true
    if (!expected_module_part.empty() && !(expected_module_part == "УРОВЕНЬ" || expected_module_part == "INITIALIZATION" || expected_module_part == "ПЕРЕИНИЦ")) {
        module_ok = (log_line.find("[" + expected_module_part + "]") != std::string::npos);
    }
    
    return timestamp_ok && level_ok && thread_id_ok && message_ok && module_ok;
}

// Helper для поиска строки, содержащей все части 
bool find_log_containing_parts(const std::vector<std::string>& lines, const std::vector<std::string>& parts_to_find) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& line){
        bool all_parts_found_in_line = true;
        for(const auto& part : parts_to_find) {
            if (line.find(part) == std::string::npos) {
                all_parts_found_in_line = false;
                break;
            }
        }
        return all_parts_found_in_line;
    });
}


class LoggerTest : public ::testing::Test {
protected:
    std::string test_log_filename = "temp_test_logger_output.log";
    std::streambuf* original_cout_buf = nullptr;
    std::streambuf* original_cerr_buf = nullptr;
    std::ostringstream cout_capture;
    std::ostringstream cerr_capture;

    void SetUp() override {
        if (std::filesystem::exists(test_log_filename)) {
            std::filesystem::remove(test_log_filename);
        }
        original_cout_buf = std::cout.rdbuf();
        std::cout.rdbuf(cout_capture.rdbuf());
        original_cerr_buf = std::cerr.rdbuf();
        std::cerr.rdbuf(cerr_capture.rdbuf());

        // Инициализируем логгер на NONE, чтобы SetUp/TearDown не создавали много логов по умолчанию.
        // Сообщения от этой инициализации будут проигнорированы в тестах.
        Logger::init(LogLevel::NONE, ""); 
        cout_capture.str(""); cout_capture.clear(); 
        cerr_capture.str(""); cerr_capture.clear();
    }

    void TearDown() override {
        std::cout.rdbuf(original_cout_buf);
        std::cerr.rdbuf(original_cerr_buf);
        Logger::init(LogLevel::NONE, ""); // Снова, чтобы не влиять на другие наборы тестов
        if (std::filesystem::exists(test_log_filename)) {
            std::filesystem::remove(test_log_filename);
        }
    }

    std::vector<std::string> getCapturedCout() {
        std::string s = cout_capture.str();
        std::istringstream iss(s);
        std::vector<std::string> lines;
        std::string line;
        while(std::getline(iss, line)) if(!line.empty()) lines.push_back(line);
        cout_capture.str(""); cout_capture.clear(); 
        return lines;
    }
    std::vector<std::string> getCapturedCerr() {
        std::string s = cerr_capture.str();
        std::istringstream iss(s);
        std::vector<std::string> lines;
        std::string line;
        while(std::getline(iss, line)) if(!line.empty()) lines.push_back(line);
        cerr_capture.str(""); cerr_capture.clear(); 
        return lines;
    }
};

TEST_F(LoggerTest, InitialStateAndSetLevel) {
    // SetUp уже вызвал Logger::init(LogLevel::NONE, "");
    EXPECT_EQ(Logger::getLevel(), LogLevel::NONE); // Проверяем начальное состояние после SetUp

    Logger::init(LogLevel::INFO); // Инициализация на INFO (вывод только в консоль по умолчанию)
    cout_capture.str(""); // Очищаем буфер после init

    Logger::setLevel(LogLevel::ERROR); // Меняем уровень
    EXPECT_EQ(Logger::getLevel(), LogLevel::ERROR);
    
    auto cout_lines = getCapturedCout();
    // Ожидаем сообщение об изменении уровня. Оно должно пройти через log_internal
    // и иметь формат [УРОВЕНЬ] как level_str.
    EXPECT_TRUE(find_log_containing_parts(cout_lines, {"[УРОВЕНЬ]", "Уровень логирования изменен с 1 на 3"}))
        << "Сообщение об изменении уровня не найдено или не соответствует формату в cout.";
}

TEST_F(LoggerTest, LogToFileAndConsole) {
    ASSERT_TRUE(Logger::getLevel() == LogLevel::NONE); // Проверка после SetUp

    Logger::init(LogLevel::DEBUG, test_log_filename); // Это вызовет логирование инициализации
    EXPECT_EQ(Logger::getLevel(), LogLevel::DEBUG);
    
    auto init_cout_lines = getCapturedCout(); // Захватываем вывод от init
    EXPECT_TRUE(find_log_containing_parts(init_cout_lines, {"[INITIALIZATION]", "Логирование в файл: " + test_log_filename}));

    std::string debug_msg = "Это тестовое DEBUG сообщение.";
    std::string info_msg = "Это тестовое INFO сообщение.";
    std::string warn_msg = "Это тестовое WARN сообщение.";
    std::string error_msg = "Это тестовое ERROR сообщение.";
    std::string module_name = "TestModule";

    Logger::debug(debug_msg, module_name);
    Logger::info(info_msg); 
    Logger::warn(warn_msg, module_name);
    Logger::error(error_msg);

    auto cout_after_logs = getCapturedCout();
    auto cerr_after_logs = getCapturedCerr();

    EXPECT_TRUE(check_log_format(cout_after_logs[0], LogLevel::DEBUG, debug_msg, module_name));
    EXPECT_TRUE(check_log_format(cout_after_logs[1], LogLevel::INFO, info_msg));
    
    EXPECT_TRUE(check_log_format(cerr_after_logs[0], LogLevel::WARN, warn_msg, module_name));
    EXPECT_TRUE(check_log_format(cerr_after_logs[1], LogLevel::ERROR, error_msg));

    auto file_lines = read_lines_from_file(test_log_filename);
    ASSERT_GE(file_lines.size(), 2U); 
    
    EXPECT_TRUE(find_log_containing_parts(file_lines, {"[INITIALIZATION]", "Логирование в файл: " + test_log_filename}));
    bool found_debug_in_file = false, found_info_in_file = false, found_warn_in_file = false, found_error_in_file = false;
    for(const auto& line : file_lines){
        if(check_log_format(line, LogLevel::DEBUG, debug_msg, module_name)) found_debug_in_file = true;
        if(check_log_format(line, LogLevel::INFO, info_msg)) found_info_in_file = true; // module_name пуст для info
        if(check_log_format(line, LogLevel::WARN, warn_msg, module_name)) found_warn_in_file = true;
        if(check_log_format(line, LogLevel::ERROR, error_msg)) found_error_in_file = true; // module_name пуст для error
    }
    EXPECT_TRUE(found_debug_in_file);
    EXPECT_TRUE(found_info_in_file);
    EXPECT_TRUE(found_warn_in_file);
    EXPECT_TRUE(found_error_in_file);
}

TEST_F(LoggerTest, LogLevelFiltering) {
    Logger::init(LogLevel::WARN, test_log_filename); 
    cout_capture.str(""); cerr_capture.str(""); // Очистить после init

    Logger::debug("Это DEBUG, не должно появиться");
    Logger::info("Это INFO, не должно появиться");
    Logger::warn("Это WARN, должно появиться");
    Logger::error("Это ERROR, должно появиться");

    auto cout_lines = getCapturedCout();
    auto cerr_lines = getCapturedCerr();
    auto file_lines = read_lines_from_file(test_log_filename);

    EXPECT_TRUE(cout_lines.empty()); 
    
    ASSERT_GE(cerr_lines.size(), 2U); 
    EXPECT_TRUE(check_log_format(cerr_lines[0], LogLevel::WARN, "Это WARN"));
    EXPECT_TRUE(check_log_format(cerr_lines[1], LogLevel::ERROR, "Это ERROR"));

    // Файл будет содержать сообщение об инициализации + WARN + ERROR
    ASSERT_GE(file_lines.size(), 3U); 
    bool found_warn_in_file = false, found_error_in_file = false, found_debug_in_file = false, found_info_in_file = false;
     for(const auto& line : file_lines){
        if(check_log_format(line, LogLevel::WARN, "Это WARN")) found_warn_in_file = true;
        if(check_log_format(line, LogLevel::ERROR, "Это ERROR")) found_error_in_file = true;
        if(line.find("Это DEBUG") != std::string::npos) found_debug_in_file = true;
        if(line.find("Это INFO") != std::string::npos) found_info_in_file = true;
    }
    EXPECT_TRUE(found_warn_in_file);
    EXPECT_TRUE(found_error_in_file);
    EXPECT_FALSE(found_debug_in_file);
    EXPECT_FALSE(found_info_in_file);
}

TEST_F(LoggerTest, LogLevelNone) {
    Logger::init(LogLevel::NONE, test_log_filename); // init может что-то залогировать
    getCapturedCout(); getCapturedCerr(); // Очищаем буферы после init
    
    Logger::error("Сообщение ERROR при уровне NONE"); 
    
    EXPECT_TRUE(getCapturedCout().empty());
    EXPECT_TRUE(getCapturedCerr().empty());

    auto file_lines = read_lines_from_file(test_log_filename);
    bool found_non_init_error_message = false;
    for(const auto& line : file_lines) {
        if (line.find("Сообщение ERROR при уровне NONE") != std::string::npos) {
            found_non_init_error_message = true;
            break;
        }
    }
    EXPECT_FALSE(found_non_init_error_message) << "Обычное сообщение ERROR не должно было попасть в файл при LogLevel::NONE";
    
    if (std::filesystem::exists(test_log_filename) && !file_lines.empty()) {
         EXPECT_TRUE(find_log_containing_parts(file_lines, {"[INITIALIZATION]", "Логирование"}) ||
                       find_log_containing_parts(file_lines, {"[ПЕРЕИНИЦ]"}));
    }
}


TEST_F(LoggerTest, Reinitialization) {
    Logger::init(LogLevel::INFO, test_log_filename); // Файл 1
    Logger::info("Первое сообщение в первый файл.");
    getCapturedCout(); getCapturedCerr(); // Очистить буферы после первого init и info
    
    std::string second_log_filename = "temp_test_logger_output_second.log";
    if (std::filesystem::exists(second_log_filename)) {
        std::filesystem::remove(second_log_filename);
    }

    Logger::init(LogLevel::DEBUG, second_log_filename); // Файл 2, переинициализация
    Logger::debug("Сообщение во второй файл.");

    // Проверяем первый файл
    auto first_file_lines = read_lines_from_file(test_log_filename);
    ASSERT_GE(first_file_lines.size(), 2U); // INIT + Info + REINIT
    EXPECT_TRUE(find_log_containing_parts(first_file_lines, {"[INITIALIZATION]", "Логирование в файл: " + test_log_filename}));
    bool first_msg_found = false, reinit_msg_found = false;
    for(const auto& line : first_file_lines) {
        if(line.find("Первое сообщение в первый файл.") != std::string::npos) first_msg_found = true;
        if(line.find("Логгер переинициализируется. Закрытие этого файла лога.") != std::string::npos) reinit_msg_found = true;
    }
    EXPECT_TRUE(first_msg_found);
    EXPECT_TRUE(reinit_msg_found);

    // Проверяем второй файл
    auto second_file_lines = read_lines_from_file(second_log_filename);
    ASSERT_GE(second_file_lines.size(), 2U); // INIT + Debug
    EXPECT_TRUE(find_log_containing_parts(second_file_lines, {"[INITIALIZATION]", "Логирование в файл: " + second_log_filename}));
    bool second_msg_found = false;
     for(const auto& line : second_file_lines) {
        if(check_log_format(line, LogLevel::DEBUG, "Сообщение во второй файл.")) second_msg_found = true;
    }
    EXPECT_TRUE(second_msg_found);


    if (std::filesystem::exists(second_log_filename)) {
        std::filesystem::remove(second_log_filename);
    }
}

TEST_F(LoggerTest, GetThreadIdStr_IsNotEmpty) {
    std::string thread_id = Logger::get_thread_id_str();
    EXPECT_FALSE(thread_id.empty());
}

TEST_F(LoggerTest, Init_ConsoleOnly) {
    Logger::init(LogLevel::DEBUG, ""); // Путь к файлу пуст
    
    auto init_cout_lines = getCapturedCout();
    EXPECT_TRUE(find_log_containing_parts(init_cout_lines, {"[INITIALIZATION]", "Логирование только в консоль"}));

    Logger::debug("Debug для консоли");
    Logger::error("Error для консоли");

    auto cout_lines = getCapturedCout();
    auto cerr_lines = getCapturedCerr();

    ASSERT_FALSE(cout_lines.empty());
    EXPECT_TRUE(check_log_format(cout_lines[0], LogLevel::DEBUG, "Debug для консоли"));
    ASSERT_FALSE(cerr_lines.empty());
    EXPECT_TRUE(check_log_format(cerr_lines[0], LogLevel::ERROR, "Error для консоли"));

    EXPECT_FALSE(std::filesystem::exists(test_log_filename)); 
}

TEST_F(LoggerTest, Init_FailsToOpenFile) {
    #ifdef _WIN32
        std::string unwritable_path = "C:\\Windows\\System32\\unwritable_logger_test.log"; 
    #else
        std::string unwritable_path = "/unwritable_logger_test.log"; 
    #endif
    
    Logger::init(LogLevel::INFO, unwritable_path); // Это должно залогировать ошибку
    
    auto cerr_lines = getCapturedCerr(); 
    auto cout_lines = getCapturedCout(); 

    bool open_error_found = find_log_containing_parts(cerr_lines, {"[INITIALIZATION]", "[ОШИБКА]", "Не удалось открыть файл лога: " + unwritable_path}) ||
                            find_log_containing_parts(cout_lines, {"[INITIALIZATION]", "[ОШИБКА]", "Не удалось открыть файл лога: " + unwritable_path}); // Ошибка может пойти в cout, если cerr не настроен
    
    EXPECT_TRUE(open_error_found) << "Сообщение об ошибке открытия файла не найдено ни в cerr, ни в cout";

    cout_capture.str(""); cerr_capture.str(""); 
    Logger::info("Тест после ошибки файла"); // Это должно пойти в консоль
    auto cout_after_error = getCapturedCout();
    ASSERT_FALSE(cout_after_error.empty());
    EXPECT_TRUE(check_log_format(cout_after_error[0], LogLevel::INFO, "Тест после ошибки файла"));
}
