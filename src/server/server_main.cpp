// Предполагаемый путь: src/server/server_main.cpp
#include "common_defs.h"    // Общие заголовки и определения
#include "logger.h"         // Наш логгер
#include "file_utils.h"     // Для getProjectRootPath
#include "database.h"       // Ядро БД
#include "tariff_plan.h"    // Тарифный план
#include "query_parser.h"   // Парсер запросов (может быть и не нужен здесь, если Server его не требует в конструктор)
#include "server.h"         // Наш класс Server
#include "server_config.h"  // <<<<<<<<<<< ADDED: Include ServerConfig header

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <stdexcept>
#include <csignal>    // Для signal/sigaction
#include <atomic>     // Для std::atomic
#include <thread>     // Для std::this_thread::sleep_for
#include <chrono>     // Для std::chrono::*

// Глобальный флаг для корректной остановки сервера по сигналу
std::atomic<bool> g_server_should_stop(false);

#ifndef _WIN32 // POSIX-специфичная обработка сигналов
void signalHandler(int signum) {
    std::string msg = "[ServerMain] Signal (" + std::to_string(signum) + ") received. Requesting server shutdown.";
    (void)write(STDERR_FILENO, "\n", 1); // Новая строка для чистоты вывода
    (void)write(STDERR_FILENO, msg.c_str(), msg.length());
    (void)write(STDERR_FILENO, "\n", 1);
    g_server_should_stop.store(true);
}

void setup_signal_handlers() {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask); // Не блокируем другие сигналы во время обработки
    sa.sa_flags = 0; // Не SA_RESTART, чтобы прерывать блокирующие вызовы типа accept

    if (sigaction(SIGINT, &sa, nullptr) == -1) {
        Logger::error("[ServerMain] Failed to set SIGINT handler.");
    }
    if (sigaction(SIGTERM, &sa, nullptr) == -1) {
        Logger::error("[ServerMain] Failed to set SIGTERM handler.");
    }
    Logger::info("[ServerMain] Signal handlers for SIGINT and SIGTERM set up.");
}
#else
// Для Windows можно использовать SetConsoleCtrlHandler
BOOL WINAPI consoleCtrlHandler(DWORD ctrlType) {
    switch (ctrlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        Logger::info("[ServerMain] Console control event (" + std::to_string(ctrlType) + ") received. Requesting server shutdown.");
        g_server_should_stop.store(true);
        // Даем немного времени главному потоку среагировать перед тем, как ОС закроет приложение
        // Sleep(10000); // Можно закомментировать, если server.stop() отрабатывает быстро
        return TRUE; // Сообщаем системе, что мы обработали событие
    default:
        return FALSE; // Передаем необработанные события дальше
    }
}
#endif


int main(int argc, char* argv[]) {
    ServerConfig server_config; 
    server_config.log_level = LogLevel::DEBUG; 

    Logger::init(server_config.log_level, server_config.log_file_path); 
    Logger::info("=================================================");
    Logger::info("========== Запуск Сервера Базы Данных ==========");
    Logger::info("=================================================");

#ifndef _WIN32
    setup_signal_handlers();
#else
    if (!SetConsoleCtrlHandler(consoleCtrlHandler, TRUE)) {
        Logger::error("[ServerMain] Failed to set console control handler. Error: " + std::to_string(GetLastError()));
    } else {
        Logger::info("[ServerMain] Console control handler set up for Windows.");
    }
#endif

    std::string server_executable_full_path = (argc > 0 && argv[0] != nullptr) ? std::filesystem::weakly_canonical(std::filesystem::absolute(argv[0])).string() : "";

    std::string default_config_file_path;
    if (!server_executable_full_path.empty()) {
        std::filesystem::path exec_dir = std::filesystem::path(server_executable_full_path).parent_path();
        if (std::filesystem::exists(exec_dir / "server.conf")) {
            default_config_file_path = (exec_dir / "server.conf").string();
        } else {
            if (std::filesystem::exists(exec_dir.parent_path() / "server.conf")) {
                 default_config_file_path = (exec_dir.parent_path() / "server.conf").string();
            } else {
                 if (std::filesystem::exists(exec_dir.parent_path().parent_path() / "server.conf")) {
                    default_config_file_path = (exec_dir.parent_path().parent_path() / "server.conf").string();
                 }
            }
        }
    }
    if (!default_config_file_path.empty()) {
        Logger::info("[ServerMain] Attempting to load default configuration file: " + default_config_file_path);
        server_config.loadFromFile(default_config_file_path); 
    } else {
        Logger::info("[ServerMain] Default configuration file 'server.conf' not found in typical locations. Using default values and command-line arguments.");
    }


    if (!server_config.parseCommandLineArgs(argc, argv, server_executable_full_path)) {
        if (argc > 1 && (std::string(argv[argc-1]) == "-h" || std::string(argv[argc-1]) == "--help") ) {
             return 0; 
        }
        return 1; 
    }

    Logger::init(server_config.log_level, server_config.log_file_path);

    Logger::info("[ServerMain] Итоговая конфигурация сервера:");
    Logger::info("[ServerMain]   Порт: " + std::to_string(server_config.port));
    Logger::info("[ServerMain]   Файл тарифов: '" + server_config.tariff_file_path + "'");
    Logger::info("[ServerMain]   Директория данных сервера: '" + server_config.server_data_root_dir + "'");
    Logger::info("[ServerMain]   Размер пула потоков: " + std::to_string(server_config.thread_pool_size));
    Logger::info("[ServerMain]   Уровень лога: " + std::to_string(static_cast<int>(Logger::getLevel())));
    Logger::info("[ServerMain]   Файл лога: '" + (server_config.log_file_path.empty() ? "Только консоль" : server_config.log_file_path) + "'");


    Database db_instance;
    TariffPlan tariff_plan_instance;
    QueryParser query_parser_instance;

    std::string effectiveTariffPathToLoad = server_config.tariff_file_path;
    if (effectiveTariffPathToLoad.empty()) { 
         try {
            effectiveTariffPathToLoad = getProjectDataPath("tariff_default.cfg", server_executable_full_path.c_str()).string();
            Logger::info("[ServerMain] Файл тарифов не указан, попытка загрузки тарифа по умолчанию: " + effectiveTariffPathToLoad);
        } catch (const std::exception& e) {
            Logger::warn("[ServerMain] Не удалось определить путь к тарифу по умолчанию: " + std::string(e.what()));
        }
    } else { 
        if (!std::filesystem::path(effectiveTariffPathToLoad).is_absolute() && !server_executable_full_path.empty()) {
            effectiveTariffPathToLoad = (std::filesystem::path(server_executable_full_path).parent_path() / effectiveTariffPathToLoad).lexically_normal().string();
            Logger::info("[ServerMain] Относительный путь к файлу тарифов '" + server_config.tariff_file_path + "' разрешен в: " + effectiveTariffPathToLoad);
        }
    }


    if (!effectiveTariffPathToLoad.empty() && std::filesystem::exists(effectiveTariffPathToLoad)) {
        try {
            tariff_plan_instance.loadFromFile(effectiveTariffPathToLoad);
            Logger::info("[ServerMain] Успешно загружен тарифный план из \"" + effectiveTariffPathToLoad + "\"");
        } catch (const std::exception& e) {
            Logger::error("[ServerMain] Ошибка при загрузке тарифного плана из \"" + effectiveTariffPathToLoad + "\": " + e.what());
            Logger::warn("[ServerMain] Команда CALCULATE_CHARGES будет использовать нулевые тарифы.");
        }
    } else {
        Logger::warn("[ServerMain] Файл тарифов не найден (указанный: '" + server_config.tariff_file_path +"' или по умолчанию: '" + effectiveTariffPathToLoad +
                     "'). Команда CALCULATE_CHARGES будет использовать нулевые тарифы.");
    }

    Server server(server_config, db_instance, tariff_plan_instance, query_parser_instance, server_executable_full_path);
    if (!server.start()) {
        Logger::error("[ServerMain] КРИТИЧЕСКАЯ ОШИБКА: Не удалось запустить сервер на порту " + std::to_string(server_config.port) + ".");
        return 1;
    }

    Logger::info("[ServerMain] Сервер успешно запущен. Ожидание соединений или сигнала завершения (Ctrl+C)...");

    while (!g_server_should_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); 
        if (!server.isRunning() && !g_server_should_stop.load()) {
            Logger::warn("[ServerMain] Обнаружено, что сервер неактивен, но сигнал остановки не получен. Инициирую остановку.");
            g_server_should_stop.store(true); 
        }
    }

    Logger::info("[ServerMain] Получен сигнал остановки или сервер завершил работу. Инициирована процедура остановки экземпляра Server...");
    server.stop(); 

    Logger::info("========== Сервер Базы Данных Завершил Работу ==========");
    return 0;
}
