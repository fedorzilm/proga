// Предполагаемый путь: src/server/server_main.cpp
#include "common_defs.h"    // Общие заголовки и определения
#include "logger.h"         // Наш логгер
#include "file_utils.h"     // Для getProjectRootPath
#include "database.h"       // Ядро БД
#include "tariff_plan.h"    // Тарифный план
#include "query_parser.h"   // Парсер запросов (может быть и не нужен здесь, если Server его не требует в конструктор)
#include "server.h"         // Наш класс Server

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
    // Прямой вывод в stderr, т.к. Logger может использовать мьютексы, что небезопасно в обработчике сигнала
    // Однако, если Logger::log_internal реализован безопасно для сигналов (например, пишет в pipe), то можно.
    // Для простоты и надежности - прямой вывод или установка флага.
    write(STDERR_FILENO, "\n", 1); // Новая строка для чистоты вывода
    write(STDERR_FILENO, msg.c_str(), msg.length());
    write(STDERR_FILENO, "\n", 1);
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
    Logger::init(LogLevel::DEBUG); // Инициализируем логгер с уровнем DEBUG по умолчанию
    Logger::info("=================================================");
    Logger::info("========== Запуск Сервера Базы Данных ==========");
    Logger::info("=================================================");

    // Установка обработчиков сигналов/событий консоли
#ifndef _WIN32
    setup_signal_handlers();
#else
    if (!SetConsoleCtrlHandler(consoleCtrlHandler, TRUE)) {
        Logger::error("[ServerMain] Failed to set console control handler. Error: " + std::to_string(GetLastError()));
    } else {
        Logger::info("[ServerMain] Console control handler set up for Windows.");
    }
#endif

    int port = 12345;
    std::string arg_tariff_file_path = "";
    std::string arg_server_data_dir = ""; // Директория для LOAD/SAVE на сервере
    std::string server_executable_full_path = (argc > 0 && argv[0] != nullptr) ? std::filesystem::weakly_canonical(std::filesystem::absolute(argv[0])).string() : "";


    // --- Парсинг аргументов командной строки ---
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            try {
                port = std::stoi(argv[++i]);
                if (port <= 0 || port > 65535) {
                     Logger::error("[ServerMain] Неверный номер порта: " + std::to_string(port) + ". Должен быть в диапазоне 1-65535.");
                     return 1;
                }
            } catch (const std::exception& e) {
                Logger::error("[ServerMain] Ошибка парсинга номера порта '" + std::string(argv[i]) + "': " + e.what());
                return 1;
            }
        } else if ((arg == "-t" || arg == "--tariff") && i + 1 < argc) {
            arg_tariff_file_path = argv[++i];
        } else if ((arg == "-d" || arg == "--data-dir") && i + 1 < argc) {
            arg_server_data_dir = argv[++i];
        } else if ((arg == "-l" || arg == "--log-level") && i + 1 < argc) {
            std::string level_str = argv[++i];
            std::transform(level_str.begin(), level_str.end(), level_str.begin(), ::toupper);
            if (level_str == "DEBUG") Logger::setLevel(LogLevel::DEBUG);
            else if (level_str == "INFO") Logger::setLevel(LogLevel::INFO);
            else if (level_str == "WARN") Logger::setLevel(LogLevel::WARN);
            else if (level_str == "ERROR") Logger::setLevel(LogLevel::ERROR);
            else if (level_str == "NONE") Logger::setLevel(LogLevel::NONE);
            else Logger::warn("[ServerMain] Неизвестный уровень логирования: " + level_str + ". Используется текущий.");
        } else if (arg == "-h" || arg == "--help") {
            // Используем std::cout для help, т.к. Logger может быть настроен на NONE
            std::cout << "Использование: database_server [опции]\n";
            std::cout << "Опции:\n";
            std::cout << "  -p, --port <номер_порта>    Установить порт сервера (по умолч.: 12345)\n";
            std::cout << "  -t, --tariff <путь_к_файлу> Путь к файлу тарифов (относительно CWD или абсолютный)\n";
            std::cout << "  -d, --data-dir <путь_к_дир> Корневая директория для файлов БД сервера (LOAD/SAVE)\n"
                      << "                                (по умолч.: PROJECT_ROOT/" << DEFAULT_SERVER_DATA_SUBDIR << " или CWD/" << DEFAULT_SERVER_DATA_SUBDIR << ")\n";
            std::cout << "  -l, --log-level <LEVEL>     Уровень логирования (DEBUG, INFO, WARN, ERROR, NONE)\n";
            std::cout << "  -h, --help                  Показать это справочное сообщение\n";
            return 0;
        } else {
            Logger::error("[ServerMain] Неизвестный аргумент или неверное использование: " + arg);
            return 1;
        }
    }
    Logger::info("[ServerMain] Конфигурация сервера: Порт=" + std::to_string(port) +
                 ", Файл тарифа (арг)='" + arg_tariff_file_path +
                 "', Директория данных (арг)='" + arg_server_data_dir +
                 "', Уровень лога=" + std::to_string(static_cast<int>(Logger::getLevel())) );

    Database db_instance;
    TariffPlan tariff_plan_instance;
    QueryParser query_parser_instance; // QueryParser stateless, можно один на сервер

    // --- Загрузка тарифного плана ---
    std::string effectiveTariffPathToLoad;
    if (!arg_tariff_file_path.empty()) {
        effectiveTariffPathToLoad = std::filesystem::weakly_canonical(std::filesystem::absolute(arg_tariff_file_path)).string();
        Logger::info("[ServerMain] Используется указанный файл тарифов: " + effectiveTariffPathToLoad);
    } else {
        try {
            effectiveTariffPathToLoad = getProjectDataPath("tariff_default.cfg", server_executable_full_path.c_str()).string();
            Logger::info("[ServerMain] Попытка загрузки тарифа по умолчанию: " + effectiveTariffPathToLoad);
        } catch (const std::exception& e) {
            Logger::warn("[ServerMain] Не удалось определить путь к тарифу по умолчанию: " + std::string(e.what()));
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
        Logger::warn("[ServerMain] Файл тарифов не найден (указанный или по умолчанию: '" + effectiveTariffPathToLoad +
                     "'). Команда CALCULATE_CHARGES будет использовать нулевые тарифы.");
    }

    // --- Определение базовой директории данных сервера для ServerCommandHandler ---
    std::string server_command_handler_base_path;
    if (!arg_server_data_dir.empty()) {
        // Если указан путь к директории данных, делаем его абсолютным и используем
        server_command_handler_base_path = std::filesystem::weakly_canonical(std::filesystem::absolute(arg_server_data_dir)).string();
        Logger::info("[ServerMain] Используется указанная директория данных для операций LOAD/SAVE: " + server_command_handler_base_path);
    } else {
        // Если не указан, используем путь на основе server_executable_full_path (для getProjectRootPath)
        // ServerCommandHandler будет добавлять DEFAULT_SERVER_DATA_SUBDIR к этому пути
        server_command_handler_base_path = server_executable_full_path;
        Logger::info("[ServerMain] Директория данных для LOAD/SAVE будет определена ServerCommandHandler на основе пути сервера: " + server_command_handler_base_path);
    }

    // Создание и запуск сервера
    Server server(port, db_instance, tariff_plan_instance, query_parser_instance, server_command_handler_base_path);
    if (!server.start()) {
        Logger::error("[ServerMain] КРИТИЧЕСКАЯ ОШИБКА: Не удалось запустить сервер на порту " + std::to_string(port) + ".");
        return 1;
    }

    Logger::info("[ServerMain] Сервер успешно запущен. Ожидание соединений или сигнала завершения (Ctrl+C)...");

    // Основной цикл ожидания сигнала завершения
    while (!g_server_should_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Проверяем флаг периодически
        if (!server.isRunning() && !g_server_should_stop.load()) {
            Logger::warn("[ServerMain] Обнаружено, что сервер неактивен, но сигнал остановки не получен. Инициирую остановку.");
            g_server_should_stop.store(true); // Инициировать остановку
        }
    }

    Logger::info("[ServerMain] Получен сигнал остановки или сервер завершил работу. Инициирована процедура остановки экземпляра Server...");
    server.stop(); // Запускает процесс остановки сервера (закрытие сокетов, join потоков)

    Logger::info("========== Сервер Базы Данных Завершил Работу ==========");
    return 0;
}
