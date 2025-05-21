// unit_tests/test_server.cpp
#include "gtest/gtest.h"
// #include "gmock/gmock.h" // GMock не используется для мокирования final классов таким способом
#include "server.h"        // Этот include может остаться для доступа к ServerConfig
#include "server_config.h"
#include "database.h"
#include "tariff_plan.h"
#include "query_parser.h"
#include "thread_pool.h"
#include "tcp_socket.h"
#include "logger.h"
#include "common_defs.h" // Для DEFAULT_SERVER_DATA_SUBDIR

#include <atomic>
#include <functional>
#include <thread>
#include <chrono>
#include <cstdlib>   // для rand()
#include <ctime>     // для time() для srand()
#include <filesystem>// для std::filesystem
#include <iostream>  // для std::cerr

// Определение g_server_should_stop специально для этого тестового файла
std::atomic<bool> g_server_should_stop(false);


class ServerTest : public ::testing::Test {
protected:
    ServerConfig config;
    Database realDb;
    TariffPlan realTariffPlan;
    QueryParser realQueryParser;

    std::string dummy_exec_path;

    static void SetUpTestSuite() {
        // Инициализация Winsock один раз для всех тестов TCPSocket, если это Windows
        #ifdef _WIN32
            TCPSocket::initializeSockets();
        #endif
        Logger::init(LogLevel::DEBUG, "test_server_suite.log"); // Лог для всех тестов этого набора
        srand(static_cast<unsigned int>(time(nullptr)));
    }

    static void TearDownTestSuite() {
        #ifdef _WIN32
            TCPSocket::cleanupSockets();
        #endif
        Logger::init(LogLevel::NONE); // Сброс логгера
    }


    void SetUp() override {
        g_server_should_stop.store(false); 
        
        config.port = 50000 + (rand() % 10000);
        config.thread_pool_size = 2; 
        
        std::filesystem::path base_test_dir = std::filesystem::current_path() / "server_test_runtime_data";
        config.server_data_root_dir = (base_test_dir / ("data_root_" + std::to_string(config.port))).string();
        
        dummy_exec_path = (base_test_dir / "dummy_exec" / "dummy_server_executable_for_test_server_cpp").string();
        
        std::filesystem::remove_all(config.server_data_root_dir);
        std::filesystem::remove_all(std::filesystem::path(dummy_exec_path).parent_path());

        std::filesystem::create_directories(config.server_data_root_dir);
        std::filesystem::create_directories(std::filesystem::path(config.server_data_root_dir) / DEFAULT_SERVER_DATA_SUBDIR);
        std::filesystem::create_directories(std::filesystem::path(dummy_exec_path).parent_path());
        
        std::ofstream dummy_exec_file(dummy_exec_path);
        if (dummy_exec_file.is_open()) {
            dummy_exec_file << "#!/bin/sh\n echo 'dummy_exec'";
            dummy_exec_file.close();
            std::filesystem::permissions(dummy_exec_path, 
                std::filesystem::perms::owner_all | std::filesystem::perms::group_read | std::filesystem::perms::others_read, 
                std::filesystem::perm_options::add);
        } else {
             std::cerr << "ПРЕДУПРЕЖДЕНИЕ в ServerTest::SetUp: не удалось создать фиктивный исполняемый файл: "
                       << dummy_exec_path << std::endl;
        }

        config.tariff_file_path = (std::filesystem::path(config.server_data_root_dir) / "dummy_tariff.cfg").string();
        std::ofstream tariff_file(config.tariff_file_path);
        if (tariff_file.is_open()) {
            for(int i=0; i < HOURS_IN_DAY; ++i) tariff_file << "0.1\n"; 
            for(int i=0; i < HOURS_IN_DAY; ++i) tariff_file << "0.05\n";
            tariff_file.close();
        } else {
            std::cerr << "ПРЕДУПРЕЖДЕНИЕ в ServerTest::SetUp: не удалось создать фиктивный файл тарифов: "
                      << config.tariff_file_path << std::endl;
        }
         // Явно загружаем тарифный план, чтобы избежать ошибок в конструкторе Server, если он это делает
        if (std::filesystem::exists(config.tariff_file_path)) {
            if (!realTariffPlan.loadFromFile(config.tariff_file_path)) {
                 std::cerr << "ПРЕДУПРЕЖДЕНИЕ в ServerTest::SetUp: не удалось загрузить фиктивный файл тарифов: "
                           << config.tariff_file_path << std::endl;
            }
        }
    }

    void TearDown() override {
        if (!config.server_data_root_dir.empty() && std::filesystem::exists(config.server_data_root_dir)){
             try {
                 std::filesystem::remove_all(config.server_data_root_dir);
             } catch (const std::filesystem::filesystem_error& e) {
                 std::cerr << "Ошибка при удалении тестовой директории в ServerTest::TearDown: "
                           << config.server_data_root_dir << " : " << e.what() << std::endl;
             }
        }
        if (!dummy_exec_path.empty() && std::filesystem::exists(std::filesystem::path(dummy_exec_path).parent_path())) {
            try {
                 std::filesystem::remove_all(std::filesystem::path(dummy_exec_path).parent_path());
             } catch (const std::filesystem::filesystem_error& e) {
                 std::cerr << "Ошибка при удалении директории фиктивного исполняемого файла в ServerTest::TearDown: "
                           << dummy_exec_path << " : " << e.what() << std::endl;
             }
        }
        g_server_should_stop.store(false);
    }
};

TEST_F(ServerTest, Constructor_InitializesSuccessfully) {
    EXPECT_NO_THROW({
        Server server(config, realDb, realTariffPlan, realQueryParser, dummy_exec_path);
    });

    ServerConfig config_no_root = config;
    config_no_root.server_data_root_dir = ""; 
    EXPECT_NO_THROW({
        Server server(config_no_root, realDb, realTariffPlan, realQueryParser, dummy_exec_path);
    });
}


TEST_F(ServerTest, Start_And_IsRunning_And_Stop_SmokeTest) {
    Server server(config, realDb, realTariffPlan, realQueryParser, dummy_exec_path);
    EXPECT_FALSE(server.isRunning()) << "Сервер не должен быть запущен до вызова start()";

    bool started_successfully = server.start();
    if (!started_successfully) {
        // Если сервер не запустился, логируем это, но не пытаемся получить ошибку из приватного члена.
        // Класс Server должен был бы предоставить публичный метод для получения последней ошибки инициализации.
        // В данном случае, достаточно информации от Logger, который используется внутри Server.
        std::cerr << "ПРЕДУПРЕЖДЕНИЕ (ServerTest): Не удалось запустить сервер на порту " << config.port
                  << ". Проверьте лог test_server_suite.log для деталей ошибки от TCPSocket. Пропуск активной части теста." << std::endl;
        GTEST_SKIP() << "Не удалось запустить сервер (возможно, порт занят " << config.port << " или другая ошибка сети).";
        return;
    }

    EXPECT_TRUE(server.isRunning()) << "Сервер должен быть запущен после успешного start()";
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    TCPSocket test_client_sock;
    bool can_connect_to_started_server = test_client_sock.connectSocket("127.0.0.1", config.port);
    if (can_connect_to_started_server) {
        Logger::debug("ServerTest Smoke: Тестовый клиент успешно подключился к запущенному серверу.");
        test_client_sock.closeSocket();
    } else {
        Logger::warn("ServerTest Smoke: Тестовый клиент НЕ смог подключиться к серверу после start() на порту " + std::to_string(config.port) + ". Ошибка: " + test_client_sock.getLastSocketErrorString());
    }
    EXPECT_TRUE(server.isRunning()) << "Сервер должен оставаться запущенным после попытки подключения клиента.";


    server.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); 
    EXPECT_FALSE(server.isRunning()) << "Сервер должен быть остановлен после stop()";

    TCPSocket test_client_sock_after_stop;
    EXPECT_FALSE(test_client_sock_after_stop.connectSocket("127.0.0.1", config.port)) 
        << "Подключение к остановленному серверу не должно быть успешным.";
    if (test_client_sock_after_stop.isValid()) test_client_sock_after_stop.closeSocket();
}
