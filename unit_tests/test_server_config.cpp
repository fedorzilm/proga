#include "gtest/gtest.h"
#include "server_config.h" 
#include "common_defs.h"   
#include "logger.h"        

#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <iostream> 

// Вспомогательная функция для создания временного файла конфигурации
void create_temp_config_file_for_serverconfig_test(const std::string& filename, const std::vector<std::string>& lines) {
    std::ofstream outfile(filename);
    ASSERT_TRUE(outfile.is_open()) << "Не удалось создать временный файл конфигурации: " << filename;
    for (const auto& line : lines) {
        outfile << line << std::endl;
    }
    outfile.close();
}

class ServerConfigTest : public ::testing::Test {
protected:
    ServerConfig config;
    std::string test_config_filename = "temp_test_server_config.conf";
    std::string test_executable_path;
    
    static constexpr size_t DEFAULT_TEST_MAX_THREADS = 256; 

    void SetUp() override {
        config = ServerConfig(); 
        try {
            test_executable_path = (std::filesystem::current_path() / "dummy_server_executable").string();
        } catch (const std::filesystem::filesystem_error& e) {
            test_executable_path = "./dummy_server_executable";
            std::cerr << "Предупреждение в ServerConfigTest::SetUp: Не удалось получить std::filesystem::current_path(). Используется относительный путь '"
                      << test_executable_path << "'. Ошибка: " << e.what() << std::endl;
        }
    }

    void TearDown() override {
        if (std::filesystem::exists(test_config_filename)) {
            std::filesystem::remove(test_config_filename);
        }
    }
};

TEST_F(ServerConfigTest, DefaultConstructorValues) {
    EXPECT_EQ(config.port, 12345);
    EXPECT_EQ(config.thread_pool_size, 4); 
    EXPECT_EQ(config.tariff_file_path, "data/tariff_default.cfg");
    EXPECT_EQ(config.server_data_root_dir, "");
    EXPECT_EQ(config.log_file_path, DEFAULT_SERVER_LOG_FILE);
    EXPECT_EQ(config.log_level, LogLevel::INFO);
}

TEST_F(ServerConfigTest, LoadFromFile_NonExistentFile) {
    EXPECT_TRUE(config.loadFromFile("this_config_file_should_not_exist.conf"));
    EXPECT_EQ(config.port, 12345);
    EXPECT_EQ(config.log_level, LogLevel::INFO);
}

TEST_F(ServerConfigTest, LoadFromFile_EmptyFile) {
    create_temp_config_file_for_serverconfig_test(test_config_filename, {});
    EXPECT_TRUE(config.loadFromFile(test_config_filename));
    EXPECT_EQ(config.port, 12345);
}

TEST_F(ServerConfigTest, LoadFromFile_AllValidParameters) {
    create_temp_config_file_for_serverconfig_test(test_config_filename, {
        "PORT = 54321",
        "THREAD_POOL_SIZE = 8",
        "TARIFF_FILE_PATH = /custom/tariffs.cfg",
        "SERVER_DATA_ROOT_DIR = /srv/data_root",
        "LOG_LEVEL = DEBUG",
        "LOG_FILE_PATH = /var/log/server_custom.log"
    });
    EXPECT_TRUE(config.loadFromFile(test_config_filename));
    EXPECT_EQ(config.port, 54321);
    EXPECT_EQ(config.thread_pool_size, 8);
    EXPECT_EQ(config.tariff_file_path, "/custom/tariffs.cfg");
    EXPECT_EQ(config.server_data_root_dir, "/srv/data_root");
    EXPECT_EQ(config.log_level, LogLevel::DEBUG);
    EXPECT_EQ(config.log_file_path, "/var/log/server_custom.log");
}

TEST_F(ServerConfigTest, LoadFromFile_CommentsAndWhitespace) {
    create_temp_config_file_for_serverconfig_test(test_config_filename, {
        "# Это комментарий",
        "  PORT   =  \t 2222  ",
        "",
        "LOG_LEVEL=WARN#Встроенный комментарий"
    });
    EXPECT_TRUE(config.loadFromFile(test_config_filename));
    EXPECT_EQ(config.port, 2222);
    EXPECT_EQ(config.log_level, LogLevel::WARN); // Ожидаем WARN после исправления в server_config.cpp
}

TEST_F(ServerConfigTest, LoadFromFile_InvalidValues) {
    create_temp_config_file_for_serverconfig_test(test_config_filename, {"PORT = not_a_number"});
    EXPECT_FALSE(config.loadFromFile(test_config_filename));
    EXPECT_EQ(config.port, 12345);

    config = ServerConfig();
    create_temp_config_file_for_serverconfig_test(test_config_filename, {"PORT = 70000"});
    EXPECT_FALSE(config.loadFromFile(test_config_filename));
    EXPECT_EQ(config.port, 12345);

    config = ServerConfig();
    create_temp_config_file_for_serverconfig_test(test_config_filename, {"THREAD_POOL_SIZE = text"});
    EXPECT_FALSE(config.loadFromFile(test_config_filename));
    EXPECT_EQ(config.thread_pool_size, 4);

    config = ServerConfig();
    create_temp_config_file_for_serverconfig_test(test_config_filename, {"LOG_LEVEL = VERY_HIGH"});
    EXPECT_TRUE(config.loadFromFile(test_config_filename)); // Неизвестный уровень лога не должен быть фатальной ошибкой
    EXPECT_EQ(config.log_level, LogLevel::INFO); // Уровень лога должен остаться INFO
}

TEST_F(ServerConfigTest, LoadFromFile_EmptyValueForRequired) {
    create_temp_config_file_for_serverconfig_test(test_config_filename, {"PORT = "});
    // После исправления в server_config.cpp, ожидаем false
    EXPECT_FALSE(config.loadFromFile(test_config_filename)); 
    EXPECT_EQ(config.port, 12345); // Порт должен остаться по умолчанию
}

TEST_F(ServerConfigTest, LoadFromFile_SpecialThreadPoolValues_Test) {
    config = ServerConfig();
    create_temp_config_file_for_serverconfig_test(test_config_filename, {"THREAD_POOL_SIZE = 0"});
    EXPECT_TRUE(config.loadFromFile(test_config_filename));
    EXPECT_EQ(config.thread_pool_size, 1); 

    config = ServerConfig();
    create_temp_config_file_for_serverconfig_test(test_config_filename, {"THREAD_POOL_SIZE = 1000"});
    EXPECT_TRUE(config.loadFromFile(test_config_filename));
    EXPECT_EQ(config.thread_pool_size, DEFAULT_TEST_MAX_THREADS);
}

TEST_F(ServerConfigTest, ParseCommandLineArgs_HelpFlags) {
    const char* argv_h[] = {const_cast<char*>("./server"), const_cast<char*>("-h")};
    EXPECT_FALSE(config.parseCommandLineArgs(2, const_cast<char**>(argv_h), test_executable_path));

    const char* argv_help[] = {const_cast<char*>("./server"), const_cast<char*>("--help")};
    EXPECT_FALSE(config.parseCommandLineArgs(2, const_cast<char**>(argv_help), test_executable_path));
}

TEST_F(ServerConfigTest, ParseCommandLineArgs_ValidOverrides) {
    const char* argv[] = {
        const_cast<char*>("./server"),
        const_cast<char*>("-p"), const_cast<char*>("9000"),
        const_cast<char*>("--threads"), const_cast<char*>("10"),
        const_cast<char*>("-t"), const_cast<char*>("new_tariff.cfg"),
        const_cast<char*>("-d"), const_cast<char*>("/new/data_dir"),
        const_cast<char*>("-l"), const_cast<char*>("ERROR"),
        const_cast<char*>("--log-file"), const_cast<char*>("new_server.log")
    };
    int argc = sizeof(argv) / sizeof(argv[0]);
    EXPECT_TRUE(config.parseCommandLineArgs(argc, const_cast<char**>(argv), test_executable_path));
    EXPECT_EQ(config.port, 9000);
    EXPECT_EQ(config.thread_pool_size, 10);
    EXPECT_EQ(config.tariff_file_path, "new_tariff.cfg");
    EXPECT_EQ(config.server_data_root_dir, "/new/data_dir");
    EXPECT_EQ(config.log_level, LogLevel::ERROR);
    EXPECT_EQ(config.log_file_path, "new_server.log");
}

TEST_F(ServerConfigTest, ParseCommandLineArgs_ConfigFileViaArgs) {
    create_temp_config_file_for_serverconfig_test(test_config_filename, {"PORT = 7777", "LOG_LEVEL = WARN"});
    const char* argv[] = {const_cast<char*>("./server"), const_cast<char*>("-c"), test_config_filename.c_str()};
    int argc = sizeof(argv) / sizeof(argv[0]);
    EXPECT_TRUE(config.parseCommandLineArgs(argc, const_cast<char**>(argv), test_executable_path));
    EXPECT_EQ(config.port, 7777);
    EXPECT_EQ(config.log_level, LogLevel::WARN);
}

TEST_F(ServerConfigTest, ParseCommandLineArgs_ArgsOverrideFileConfig) {
    create_temp_config_file_for_serverconfig_test(test_config_filename, {"PORT = 7777", "LOG_LEVEL = WARN"});
    const char* argv[] = {
        const_cast<char*>("./server"),
        const_cast<char*>("-c"), test_config_filename.c_str(),
        const_cast<char*>("-p"), const_cast<char*>("8888")
    };
    int argc = sizeof(argv) / sizeof(argv[0]);
    EXPECT_TRUE(config.parseCommandLineArgs(argc, const_cast<char**>(argv), test_executable_path));
    EXPECT_EQ(config.port, 8888); // Командная строка переопределяет файл
    EXPECT_EQ(config.log_level, LogLevel::WARN); // Остается из файла
}

TEST_F(ServerConfigTest, ParseCommandLineArgs_InvalidValues) {
    const char* argv_port[] = {const_cast<char*>("./server"), const_cast<char*>("-p"), const_cast<char*>("invalid_port")};
    EXPECT_FALSE(config.parseCommandLineArgs(3, const_cast<char**>(argv_port), test_executable_path));

    config = ServerConfig(); // Сброс
    const char* argv_threads[] = {const_cast<char*>("./server"), const_cast<char*>("--threads"), const_cast<char*>("-5")};
    // После исправления parseCommandLineArgs (добавление проверки на '-' перед stoul)
    EXPECT_FALSE(config.parseCommandLineArgs(3, const_cast<char**>(argv_threads), test_executable_path));

    config = ServerConfig();
    const char* argv_level[] = {const_cast<char*>("./server"), const_cast<char*>("-l")}; // Не хватает значения
    EXPECT_FALSE(config.parseCommandLineArgs(2, const_cast<char**>(argv_level), test_executable_path));
}

TEST_F(ServerConfigTest, ParseCommandLineArgs_UnknownOption) {
    const char* argv[] = {const_cast<char*>("./server"), const_cast<char*>("--this-is-unknown")};
    EXPECT_FALSE(config.parseCommandLineArgs(2, const_cast<char**>(argv), test_executable_path));
}

TEST_F(ServerConfigTest, ParseCommandLineArgs_ConfigFileViaArgs_NotFound) {
    const char* argv[] = {const_cast<char*>("./server"), const_cast<char*>("--config"), const_cast<char*>("non_existent_config_for_cmd_line.conf")};
    // loadFromFile вернет true, если файл не найден, parseCommandLineArgs тоже должен вернуть true, т.к. это не ошибка парсинга аргументов
    EXPECT_TRUE(config.parseCommandLineArgs(3, const_cast<char**>(argv), test_executable_path));
    EXPECT_EQ(config.port, 12345); // Значения по умолчанию
}

TEST_F(ServerConfigTest, ParseCommandLineArgs_ConfigFileViaArgs_ParseErrorInThatFile) {
    create_temp_config_file_for_serverconfig_test(test_config_filename, {"PORT = bad_value"});
    const char* argv[] = {const_cast<char*>("./server"), const_cast<char*>("-c"), test_config_filename.c_str()};
    // Если loadFromFile из-за -c возвращает false, то и parseCommandLineArgs должен вернуть false
    EXPECT_FALSE(config.parseCommandLineArgs(3, const_cast<char**>(argv), test_executable_path));
}
