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
    // Это обработчик сигнала, здесь можно использовать только async-signal-safe функции.
    // std::string и Logger::info/write могут быть небезопасны.
    // Безопасный способ - установить флаг и, возможно, записать простое сообщение в stderr.
    const char* msg_sigint = "\n[ServerMain] SIGINT received. Requesting shutdown.\n";
    const char* msg_sigterm = "\n[ServerMain] SIGTERM received. Requesting shutdown.\n";
    const char* msg_unknown = "\n[ServerMain] Unknown signal received. Requesting shutdown.\n";
    ssize_t written_bytes = 0;

    if (signum == SIGINT) {
        written_bytes = write(STDERR_FILENO, msg_sigint, strlen(msg_sigint));
    } else if (signum == SIGTERM) {
        written_bytes = write(STDERR_FILENO, msg_sigterm, strlen(msg_sigterm));
    } else {
        written_bytes = write(STDERR_FILENO, msg_unknown, strlen(msg_unknown));
    }
    // Подавление предупреждения о неиспользуемой переменной (если very-Wextra)
    (void)written_bytes; 
    g_server_should_stop.store(true);
}

void setup_signal_handlers() {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask); 
    sa.sa_flags = 0; // Не SA_RESTART, чтобы прерывать блокирующие вызовы типа accept

    if (sigaction(SIGINT, &sa, nullptr) == -1) {
        Logger::error("[ServerMain] Failed to set SIGINT handler. Errno: " + std::to_string(errno));
    }
    if (sigaction(SIGTERM, &sa, nullptr) == -1) {
        Logger::error("[ServerMain] Failed to set SIGTERM handler. Errno: " + std::to_string(errno));
    }
    // Logger::info("[ServerMain] Signal handlers for SIGINT and SIGTERM set up."); // Логируем до или после вызова
}
#else
// Для Windows можно использовать SetConsoleCtrlHandler
BOOL WINAPI consoleCtrlHandler(DWORD ctrlType) {
    // Logger::info("[ServerMain] Console control event (" + std::to_string(ctrlType) + ") received. Requesting server shutdown.");
    // Вывод в консоль из обработчика может быть проблематичен, особенно если это shutdown event.
    // Лучше минимизировать действия здесь.
    switch (ctrlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT: // Пользователь закрывает консоль
    case CTRL_LOGOFF_EVENT: // Пользователь выходит из системы
    case CTRL_SHUTDOWN_EVENT: // Система выключается
        g_server_should_stop.store(true);
        // Дать немного времени главному потоку среагировать, особенно при системном шатдауне.
        // Sleep(5000); // Можно увеличить, если серверу нужно больше времени на остановку.
        //              // Однако, система может принудительно завершить процесс раньше.
        return TRUE; // Сообщаем системе, что мы обработали событие
    default:
        return FALSE; // Передаем необработанные события дальше
    }
}
#endif


int main(int argc, char* argv[]) {
    // Начальная инициализация логгера с дефолтными значениями,
    // чтобы логировать парсинг аргументов и загрузку конфига.
    // Будет переинициализирован позже с настройками из конфига/аргументов.
    Logger::init(LogLevel::INFO, DEFAULT_SERVER_LOG_FILE); 
    const std::string main_log_prefix = "[ServerMain] ";

    Logger::info(main_log_prefix + "=================================================");
    Logger::info(main_log_prefix + "========== Запуск Сервера Базы Данных ==========");
    Logger::info(main_log_prefix + "=================================================");

#ifndef _WIN32
    setup_signal_handlers();
    Logger::info(main_log_prefix + "Signal handlers for SIGINT and SIGTERM set up (POSIX).");
#else
    if (!SetConsoleCtrlHandler(consoleCtrlHandler, TRUE)) {
        Logger::error(main_log_prefix + "Failed to set console control handler. Error: " + std::to_string(GetLastError()));
    } else {
        Logger::info(main_log_prefix + "Console control handler set up for Windows.");
    }
#endif

    std::string server_executable_full_path;
    if (argc > 0 && argv[0] != nullptr && argv[0][0] != '\0') {
        try {
            server_executable_full_path = std::filesystem::weakly_canonical(std::filesystem::absolute(argv[0])).string();
            Logger::debug(main_log_prefix + "Полный путь к исполняемому файлу сервера: '" + server_executable_full_path + "'");
        } catch (const std::filesystem::filesystem_error& e_fs) {
            Logger::warn(main_log_prefix + "Не удалось определить полный канонический путь к исполняемому файлу: " + e_fs.what() + ". Некоторые относительные пути могут разрешаться некорректно.");
            // В этом случае server_executable_full_path останется пустым или неполным.
        }
    } else {
        Logger::warn(main_log_prefix + "Не удалось получить путь к исполняемому файлу из argv[0]. Относительные пути к конфигурационным файлам могут быть разрешены неверно.");
    }
    
    ServerConfig server_config; // Создаем объект конфига со значениями по умолчанию
    // Путь к лог-файлу и уровень лога в server_config будут установлены из файла/аргументов,
    // а затем Logger будет переинициализирован.

    // Попытка загрузить конфигурацию из файла server.conf рядом с исполняемым файлом или в родительских директориях
    std::string default_config_file_path_resolved;
    if (!server_executable_full_path.empty()) {
        std::filesystem::path exec_path_obj(server_executable_full_path);
        std::vector<std::filesystem::path> search_dirs;
        if (exec_path_obj.has_parent_path()) {
            search_dirs.push_back(exec_path_obj.parent_path()); // Директория исполняемого файла
            if (exec_path_obj.parent_path().has_parent_path()) {
                 search_dirs.push_back(exec_path_obj.parent_path().parent_path()); // Родительская директория (например, корень проекта, если бинарник в build/bin)
                 if (exec_path_obj.parent_path().parent_path().has_parent_path()){
                    // Еще на уровень выше (например, если бинарник в build/config/bin)
                    search_dirs.push_back(exec_path_obj.parent_path().parent_path().parent_path());
                 }
            }
        }
        
        for (const auto& dir : search_dirs) {
            std::filesystem::path potential_conf_path = dir / "server.conf";
            if (std::filesystem::exists(potential_conf_path)) {
                default_config_file_path_resolved = potential_conf_path.string();
                Logger::info(main_log_prefix + "Найден файл конфигурации по умолчанию: '" + default_config_file_path_resolved + "'");
                break;
            }
        }
    }

    if (!default_config_file_path_resolved.empty()) {
        if (!server_config.loadFromFile(default_config_file_path_resolved)) {
             // Ошибка уже залогирована в loadFromFile
             Logger::warn(main_log_prefix + "Обнаружены ошибки при загрузке файла конфигурации по умолчанию '" + default_config_file_path_resolved + "'. Будут использованы значения по умолчанию и аргументы командной строки.");
        }
    } else {
        Logger::info(main_log_prefix + "Файл конфигурации по умолчанию 'server.conf' не найден в стандартных расположениях. Будут использованы значения по умолчанию и аргументы командной строки.");
    }

    // Парсинг аргументов командной строки (они могут переопределить значения из файла или дефолтные)
    // server_config.parseCommandLineArgs также может загрузить другой файл конфигурации, если указан -c
    if (!server_config.parseCommandLineArgs(argc, argv, server_executable_full_path)) {
        // parseCommandLineArgs возвращает false, если была запрошена справка (-h) или произошла ошибка парсинга.
        // Если была справка, то это нормальное завершение.
        bool help_requested = false;
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "-h" || std::string(argv[i]) == "--help") {
                help_requested = true;
                break;
            }
        }
        if (help_requested) {
             Logger::info(main_log_prefix + "Запрошена справка по командной строке. Завершение работы.");
             return 0; // Успешное завершение после вывода справки
        } else {
            Logger::error(main_log_prefix + "Ошибка при разборе аргументов командной строки. Завершение работы.");
            // ServerConfig::printHelp(argv[0]); // parseCommandLineArgs должен был уже вывести справку при ошибке
            return 1; // Ошибка парсинга
        }
    }

    // Переинициализация логгера с финальными настройками из server_config
    // Сначала разрешаем относительный путь к лог-файлу, если он указан
    if (!server_config.log_file_path.empty() && 
        !std::filesystem::path(server_config.log_file_path).is_absolute() && 
        !server_executable_full_path.empty()) {
        std::filesystem::path resolved_log_path;
        try {
            // Предпочтительно разрешать относительно директории исполняемого файла,
            // если только пользователь не указал абсолютный путь или если путь к исполняемому файлу неизвестен.
            std::filesystem::path base_dir_for_log = std::filesystem::path(server_executable_full_path).parent_path();
            resolved_log_path = (base_dir_for_log / server_config.log_file_path).lexically_normal();
            // Обновляем путь в конфигурации перед инициализацией логгера.
            // Логировать это изменение можно будет уже после переинициализации.
            server_config.log_file_path = resolved_log_path.string();
        } catch (const std::filesystem::filesystem_error& e_fs_log) {
            // Если ошибка при конструировании пути, лучше вывести в cerr, т.к. логгер может быть не готов
            std::cerr << main_log_prefix << "ПРЕДУПРЕЖДЕНИЕ: Ошибка при разрешении относительного пути к лог-файлу '" 
                      << server_config.log_file_path << "': " << e_fs_log.what() 
                      << ". Будет использован исходный (возможно, относительный) путь." << std::endl;
        }
    }
    Logger::init(server_config.log_level, server_config.log_file_path);
    Logger::info(main_log_prefix + "Логгер сервера переинициализирован с финальными настройками.");


    Logger::info(main_log_prefix + "Итоговая конфигурация сервера:");
    Logger::info(main_log_prefix + "  Порт: " + std::to_string(server_config.port));
    Logger::info(main_log_prefix + "  Файл тарифов: '" + server_config.tariff_file_path + "'");
    Logger::info(main_log_prefix + "  Директория данных сервера (для LOAD/SAVE): '" + server_config.server_data_root_dir + "'");
    Logger::info(main_log_prefix + "  Размер пула потоков: " + std::to_string(server_config.thread_pool_size));
    Logger::info(main_log_prefix + "  Уровень лога: " + std::to_string(static_cast<int>(Logger::getLevel())));
    Logger::info(main_log_prefix + "  Файл лога: '" + (server_config.log_file_path.empty() ? "Только консоль" : server_config.log_file_path) + "'");


    Database db_instance;
    TariffPlan tariff_plan_instance;
    QueryParser query_parser_instance; // Может быть создан внутри ServerCommandHandler или Server

    // Разрешение пути к файлу тарифов
    std::string effectiveTariffPathToLoad = server_config.tariff_file_path;
    if (effectiveTariffPathToLoad.empty()) { 
         try {
            // Если путь к исполняемому файлу известен, пытаемся найти data/tariff_default.cfg относительно него
            std::string base_path_for_data_lookup = "."; // По умолчанию текущая директория
            if (!server_executable_full_path.empty()) {
                 base_path_for_data_lookup = server_executable_full_path;
            }
            effectiveTariffPathToLoad = getProjectDataPath("tariff_default.cfg", base_path_for_data_lookup.c_str()).string();
            Logger::info(main_log_prefix + "Файл тарифов не указан в конфигурации, попытка загрузки тарифа по умолчанию: '" + effectiveTariffPathToLoad + "'");
        } catch (const std::exception& e_path) {
            Logger::warn(main_log_prefix + "Не удалось определить путь к тарифу по умолчанию: " + std::string(e_path.what()) + ". Тарифы не будут загружены, если не указаны явно.");
            effectiveTariffPathToLoad.clear(); // Убедимся, что путь пуст, если не удалось определить
        }
    } else { 
        // Если путь указан, но он относительный, и известен путь к исполняемому файлу, разрешаем его
        if (!std::filesystem::path(effectiveTariffPathToLoad).is_absolute() && !server_executable_full_path.empty()) {
            try {
                std::filesystem::path exec_dir = std::filesystem::path(server_executable_full_path).parent_path();
                effectiveTariffPathToLoad = (exec_dir / effectiveTariffPathToLoad).lexically_normal().string();
                Logger::info(main_log_prefix + "Относительный путь к файлу тарифов '" + server_config.tariff_file_path + "' разрешен в: '" + effectiveTariffPathToLoad + "'");
            } catch (const std::filesystem::filesystem_error& e_fs_tariff) {
                 Logger::warn(main_log_prefix + "Ошибка при разрешении относительного пути к файлу тарифов '" + server_config.tariff_file_path + "': " + e_fs_tariff.what() + ". Будет использован исходный относительный путь.");
                 // effectiveTariffPathToLoad остается server_config.tariff_file_path
            }
        }
    }


    if (!effectiveTariffPathToLoad.empty() && std::filesystem::exists(effectiveTariffPathToLoad)) {
        try {
            if (tariff_plan_instance.loadFromFile(effectiveTariffPathToLoad)) {
                Logger::info(main_log_prefix + "Успешно загружен тарифный план из \"" + effectiveTariffPathToLoad + "\"");
            } else {
                 // loadFromFile должен был выбросить исключение при серьезной ошибке,
                 // но если он вернул false без исключения (что не должно быть по его контракту), логируем это.
                 Logger::error(main_log_prefix + "Ошибка при загрузке тарифного плана из \"" + effectiveTariffPathToLoad + "\" (loadFromFile вернул false). Команда CALCULATE_CHARGES будет использовать нулевые тарифы.");
            }
        } catch (const std::exception& e_tariff_load) {
            Logger::error(main_log_prefix + "Ошибка при загрузке тарифного плана из \"" + effectiveTariffPathToLoad + "\": " + e_tariff_load.what());
            Logger::warn(main_log_prefix + "Команда CALCULATE_CHARGES будет использовать нулевые тарифы.");
            // tariff_plan_instance останется с тарифами по умолчанию (нули)
        }
    } else {
        if (effectiveTariffPathToLoad.empty() && server_config.tariff_file_path.empty()) {
             Logger::warn(main_log_prefix + "Файл тарифов не указан и не найден по умолчанию. Команда CALCULATE_CHARGES будет использовать нулевые тарифы.");
        } else {
            Logger::warn(main_log_prefix + "Файл тарифов не найден (указанный: '" + server_config.tariff_file_path +
                         "', разрешенный/по умолчанию: '" + effectiveTariffPathToLoad +
                         "'). Команда CALCULATE_CHARGES будет использовать нулевые тарифы.");
        }
        // tariff_plan_instance останется с тарифами по умолчанию (нули)
    }

    Server server(server_config, db_instance, tariff_plan_instance, query_parser_instance, server_executable_full_path);
    
    if (!server.start()) {
        Logger::error(main_log_prefix + "КРИТИЧЕСКАЯ ОШИБКА: Не удалось запустить сервер на порту " + std::to_string(server_config.port) + ".");
        Logger::info(main_log_prefix + "========== Сервер Базы Данных Завершил Работу (Ошибка Запуска) ==========");
        return 1;
    }

    Logger::info(main_log_prefix + "Сервер успешно запущен. Ожидание соединений или сигнала завершения (Ctrl+C / SIGINT / SIGTERM)...");

    // Основной цикл ожидания сигнала остановки
    while (!g_server_should_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Проверяем флаг периодически
        // Дополнительная проверка, если сервер мог остановиться по внутренней причине
        if (!server.isRunning() && !g_server_should_stop.load()) {
            Logger::warn(main_log_prefix + "Обнаружено, что экземпляр сервера неактивен (server.isRunning() == false), но сигнал остановки не получен. Инициирую остановку.");
            g_server_should_stop.store(true); 
        }
    }

    Logger::info(main_log_prefix + "Получен сигнал остановки или сервер завершил работу по другой причине. Инициирована процедура остановки экземпляра Server...");
    server.stop(); 
    Logger::info(main_log_prefix + "Экземпляр Server остановлен.");

    Logger::info(main_log_prefix + "========== Сервер Базы Данных Завершил Работу ==========");
    return 0;
}
