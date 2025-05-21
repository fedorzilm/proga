// Предполагаемый путь: src/server/server_main.cpp
#include "common_defs.h"    // Общие заголовки и определения
#include "logger.h"         // Наш логгер
#include "file_utils.h"     // Для FileUtils::getProjectRootPath и FileUtils::getProjectDataPath
#include "database.h"       // Ядро БД
#include "tariff_plan.h"    // Тарифный план
#include "query_parser.h"   // Парсер запросов (QueryParser теперь передается в Server)
#include "server.h"         // Наш класс Server
#include "server_config.h"  // Конфигурация сервера

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
// Он должен быть определен только в одном .cpp файле (здесь),
// а в других (например, server.h) объявлен как extern, если нужен.
// В текущей структуре он используется в Server.cpp через server_main.cpp (где Server создается).
// Если Server.h включает common_defs.h, а common_defs.h объявляет extern, то все ок.
// В вашем common_defs.h его нет, значит server_main - единственное место.
std::atomic<bool> g_server_should_stop(false);

#ifndef _WIN32 // POSIX-специфичная обработка сигналов
#include <unistd.h> // Для write в обработчике сигнала
#include <cstring>  // Для std::memset, strlen в обработчике сигнала
void signalHandler(int signum) {
    const char* msg_sigint = "\n[ServerMain] Получен SIGINT. Запрос на завершение работы.\n";
    const char* msg_sigterm = "\n[ServerMain] Получен SIGTERM. Запрос на завершение работы.\n";
    const char* msg_unknown = "\n[ServerMain] Получен неизвестный сигнал. Запрос на завершение работы.\n";
    ssize_t written_bytes [[maybe_unused]] = 0; // Подавляем предупреждение о неиспользуемой переменной

    if (signum == SIGINT) {
        written_bytes = write(STDERR_FILENO, msg_sigint, strlen(msg_sigint));
    } else if (signum == SIGTERM) {
        written_bytes = write(STDERR_FILENO, msg_sigterm, strlen(msg_sigterm));
    } else {
        written_bytes = write(STDERR_FILENO, msg_unknown, strlen(msg_unknown));
    }
    g_server_should_stop.store(true);
}

void setup_signal_handlers() {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask); 
    sa.sa_flags = 0; 

    if (sigaction(SIGINT, &sa, nullptr) == -1) {
        // Logger может быть еще не полностью готов, или это может быть вызвано из потока сигнала
        // Безопаснее использовать perror или fprintf(stderr, ...)
        perror("[ServerMain FatalError] Не удалось установить обработчик SIGINT");
        // Logger::error("[ServerMain] Failed to set SIGINT handler. Errno: " + std::to_string(errno));
    }
    if (sigaction(SIGTERM, &sa, nullptr) == -1) {
        perror("[ServerMain FatalError] Не удалось установить обработчик SIGTERM");
        // Logger::error("[ServerMain] Failed to set SIGTERM handler. Errno: " + std::to_string(errno));
    }
}
#else // Windows
#include <windows.h> // Для SetConsoleCtrlHandler
BOOL WINAPI consoleCtrlHandler(DWORD ctrlType) {
    switch (ctrlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT: 
    case CTRL_LOGOFF_EVENT: 
    case CTRL_SHUTDOWN_EVENT: 
        // Запись в stderr из обработчика консоли Windows может быть небезопасна,
        // особенно при системном шатдауне. Установка флага - самое безопасное.
        // fprintf(stderr, "\n[ServerMain] Console event (%lu). Requesting shutdown.\n", ctrlType);
        g_server_should_stop.store(true);
        // Даем серверу немного времени на корректное завершение перед тем, как система его убьет.
        // Sleep(5000); // Это может быть слишком долго или непредсказуемо.
        // Лучше, чтобы основной цикл сервера быстро реагировал на g_server_should_stop.
        return TRUE; 
    default:
        return FALSE; 
    }
}
#endif


int main(int argc, char* argv[]) {
    Logger::init(LogLevel::INFO, DEFAULT_SERVER_LOG_FILE); 
    const std::string main_log_prefix = "[ServerMain] ";

    Logger::info(main_log_prefix + "=================================================");
    Logger::info(main_log_prefix + "========== ЗАПУСК СЕРВЕРА БАЗЫ ДАННЫХ ==========");
    Logger::info(main_log_prefix + "=================================================");

#ifndef _WIN32
    setup_signal_handlers();
    Logger::info(main_log_prefix + "Обработчики сигналов SIGINT и SIGTERM настроены (POSIX).");
#else
    if (!SetConsoleCtrlHandler(consoleCtrlHandler, TRUE)) {
        Logger::error(main_log_prefix + "Не удалось установить обработчик консольных событий. Код ошибки Windows: " + std::to_string(GetLastError()));
    } else {
        Logger::info(main_log_prefix + "Обработчик консольных событий настроен для Windows.");
    }
#endif

    std::string server_executable_full_path_str;
    if (argc > 0 && argv[0] != nullptr && argv[0][0] != '\0') {
        try {
            // Используем weakly_canonical, так как absolute может выбросить исключение, если argv[0] невалиден
            server_executable_full_path_str = std::filesystem::weakly_canonical(std::filesystem::absolute(std::filesystem::path(argv[0]))).string();
            Logger::debug(main_log_prefix + "Полный путь к исполняемому файлу сервера определен как: '" + server_executable_full_path_str + "'");
        } catch (const std::filesystem::filesystem_error& e_fs) {
            Logger::warn(main_log_prefix + "Не удалось определить полный канонический путь для исполняемого файла сервера из argv[0] ('" + std::string(argv[0]) + "'): " + e_fs.what() + ". Разрешение относительных путей для конфигураций может быть затронуто.");
        }
    } else {
        Logger::warn(main_log_prefix + "Не удалось получить путь к исполняемому файлу из argv[0]. Разрешение относительных путей для конфигурационных файлов может быть затронуто.");
    }
    
    ServerConfig server_config; 

    std::string default_config_file_path_to_try;
    if (!server_executable_full_path_str.empty()) {
        std::filesystem::path exec_path_obj(server_executable_full_path_str);
        std::vector<std::filesystem::path> search_dirs_for_conf;
        if (exec_path_obj.has_parent_path()) {
            search_dirs_for_conf.push_back(exec_path_obj.parent_path()); 
            if (exec_path_obj.parent_path().has_parent_path()) {
                 search_dirs_for_conf.push_back(exec_path_obj.parent_path().parent_path()); 
                 if (exec_path_obj.parent_path().parent_path().has_parent_path()){
                    search_dirs_for_conf.push_back(exec_path_obj.parent_path().parent_path().parent_path());
                 }
            }
        }
        // Добавляем CWD как место поиска по умолчанию, если ничего не найдено рядом с исполняемым файлом
        try { search_dirs_for_conf.push_back(std::filesystem::current_path()); } catch(...){}


        for (const auto& dir : search_dirs_for_conf) {
            if (!std::filesystem::exists(dir)) continue; // Пропускаем несуществующие директории
            std::filesystem::path potential_conf_path = dir / "server.conf";
            if (std::filesystem::exists(potential_conf_path) && std::filesystem::is_regular_file(potential_conf_path)) {
                default_config_file_path_to_try = potential_conf_path.string();
                Logger::info(main_log_prefix + "Файл конфигурации по умолчанию найден по адресу: '" + default_config_file_path_to_try + "'");
                break;
            }
        }
    }

    if (!default_config_file_path_to_try.empty()) {
        if (!server_config.loadFromFile(default_config_file_path_to_try)) {
             Logger::warn(main_log_prefix + "Обнаружены ошибки при загрузке файла конфигурации по умолчанию '" + default_config_file_path_to_try + "'. Будут использованы значения по умолчанию и аргументы командной строки (которые их переопределят).");
        }
    } else {
        Logger::info(main_log_prefix + "Файл конфигурации по умолчанию 'server.conf' не найден в стандартных расположениях. Используются внутренние значения по умолчанию и аргументы командной строки.");
    }

    if (!server_config.parseCommandLineArgs(argc, argv, server_executable_full_path_str)) {
        bool help_was_requested = false;
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "-h" || std::string(argv[i]) == "--help") {
                help_was_requested = true;
                break;
            }
        }
        if (help_was_requested) {
             Logger::info(main_log_prefix + "Запрошена справка через командную строку. Завершение приложения.");
             return 0; 
        } else {
            Logger::error(main_log_prefix + "Ошибка разбора аргументов командной строки. Завершение приложения.");
            // printHelp уже должен был быть вызван из parseCommandLineArgs при ошибке
            return 1; 
        }
    }

    // Переинициализация логгера с финальными настройками
    std::string final_log_file_path = server_config.log_file_path;
    if (!final_log_file_path.empty() && 
        std::filesystem::path(final_log_file_path).is_relative() && 
        !server_executable_full_path_str.empty()) {
        try {
            std::filesystem::path base_dir_for_log = std::filesystem::path(server_executable_full_path_str).parent_path();
            final_log_file_path = (base_dir_for_log / final_log_file_path).lexically_normal().string();
        } catch (const std::exception& e_fs_log_resolve) {
            // Используем std::cerr, так как Logger еще не переинициализирован
            std::cerr << main_log_prefix << "ПРЕДУПРЕЖДЕНИЕ: Ошибка разрешения относительного пути к файлу лога '" 
                      << server_config.log_file_path << "': " << e_fs_log_resolve.what() 
                      << ". Используется исходный (возможно, относительный) путь." << std::endl;
            final_log_file_path = server_config.log_file_path; // Оставляем как есть
        }
    }
    Logger::init(server_config.log_level, final_log_file_path); // Используем разрешенный путь
    // Логируем это уже новым логгером
    Logger::info(main_log_prefix + "Логгер сервера переинициализирован с финальными настройками. Уровень: " + std::to_string(static_cast<int>(Logger::getLevel())) + ", Файл: '" + (final_log_file_path.empty() ? "Только консоль" : final_log_file_path) + "'");


    Logger::info(main_log_prefix + "Итоговая конфигурация сервера:");
    Logger::info(main_log_prefix + "  Порт: " + std::to_string(server_config.port));
    Logger::info(main_log_prefix + "  Файл тарифов: '" + server_config.tariff_file_path + "'");
    Logger::info(main_log_prefix + "  Корневая директория данных сервера (для контекста LOAD/SAVE): '" + server_config.server_data_root_dir + "' (Файлы будут в " + DEFAULT_SERVER_DATA_SUBDIR + " внутри нее)");
    Logger::info(main_log_prefix + "  Размер пула потоков: " + std::to_string(server_config.thread_pool_size));


    Database db_instance;
    TariffPlan tariff_plan_instance;
    QueryParser query_parser_instance; 

    std::string effective_tariff_path_to_load = server_config.tariff_file_path;
    if (effective_tariff_path_to_load.empty()) { 
         try {
            std::string base_path_for_data_lookup = "."; 
            if (!server_executable_full_path_str.empty()) {
                 base_path_for_data_lookup = server_executable_full_path_str;
            }
            // ИСПРАВЛЕНО: Используем FileUtils::getProjectDataPath
            effective_tariff_path_to_load = FileUtils::getProjectDataPath("tariff_default.cfg", base_path_for_data_lookup.c_str()).string();
            Logger::info(main_log_prefix + "Файл тарифов не указан в конфигурации, попытка загрузить по умолчанию: '" + effective_tariff_path_to_load + "'");
        } catch (const std::exception& e_path) {
            Logger::warn(main_log_prefix + "Не удалось определить путь к файлу тарифов по умолчанию: " + std::string(e_path.what()) + ". Тарифы не будут загружены, если не указаны явно другими средствами (в настоящее время не поддерживается).");
            effective_tariff_path_to_load.clear(); 
        }
    } else { 
        if (std::filesystem::path(effective_tariff_path_to_load).is_relative() && !server_executable_full_path_str.empty()) {
            try {
                std::filesystem::path exec_dir = std::filesystem::path(server_executable_full_path_str).parent_path();
                effective_tariff_path_to_load = (exec_dir / effective_tariff_path_to_load).lexically_normal().string();
                Logger::info(main_log_prefix + "Относительный путь к файлу тарифов '" + server_config.tariff_file_path + "' разрешен в: '" + effective_tariff_path_to_load + "'");
            } catch (const std::exception& e_fs_tariff_resolve) {
                 Logger::warn(main_log_prefix + "Ошибка разрешения относительного пути к файлу тарифов '" + server_config.tariff_file_path + "': " + e_fs_tariff_resolve.what() + ". Используется исходный относительный путь.");
                 // effective_tariff_path_to_load остается server_config.tariff_file_path
            }
        }
    }

    if (!effective_tariff_path_to_load.empty() && std::filesystem::exists(effective_tariff_path_to_load)) {
        try {
            if (tariff_plan_instance.loadFromFile(effective_tariff_path_to_load)) {
                Logger::info(main_log_prefix + "Тарифный план успешно загружен из \"" + effective_tariff_path_to_load + "\"");
            } else {
                 // loadFromFile должен был выбросить исключение, но если нет (что не должно быть по контракту), логируем
                 Logger::error(main_log_prefix + "Загрузка тарифного плана из \"" + effective_tariff_path_to_load + "\" вернула false без исключения. CALCULATE_CHARGES будет использовать нулевые тарифы.");
            }
        } catch (const std::exception& e_tariff_load) {
            Logger::error(main_log_prefix + "Ошибка загрузки тарифного плана из \"" + effective_tariff_path_to_load + "\": " + e_tariff_load.what());
            Logger::warn(main_log_prefix + "Команда CALCULATE_CHARGES будет использовать тарифы по умолчанию (нулевые).");
        }
    } else {
        if (effective_tariff_path_to_load.empty() && server_config.tariff_file_path.empty()) {
             Logger::warn(main_log_prefix + "Файл тарифов не указан и файл по умолчанию не найден. Команда CALCULATE_CHARGES будет использовать нулевые тарифы.");
        } else {
             Logger::warn(main_log_prefix + "Файл тарифов не найден (указан: '" + server_config.tariff_file_path +
                         "', разрешенный/по умолчанию: '" + effective_tariff_path_to_load +
                         "'). Команда CALCULATE_CHARGES будет использовать нулевые тарифы.");
        }
    }

    // Передаем server_executable_full_path_str в конструктор Server
    Server server(server_config, db_instance, tariff_plan_instance, query_parser_instance, server_executable_full_path_str);
    
    if (!server.start()) {
        Logger::error(main_log_prefix + "КРИТИЧЕСКАЯ ОШИБКА: Не удалось запустить сервер на порту " + std::to_string(server_config.port) + ".");
        Logger::info(main_log_prefix + "========== ЗАВЕРШЕНИЕ РАБОТЫ СЕРВЕРА БАЗЫ ДАННЫХ (Ошибка Запуска) ==========");
        return 1;
    }

    Logger::info(main_log_prefix + "Сервер успешно запущен. Ожидание подключений или сигнала завершения (Ctrl+C / SIGINT / SIGTERM)...");

    while (!g_server_should_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); 
        if (!server.isRunning() && !g_server_should_stop.load()) { // Дополнительная проверка
            Logger::warn(main_log_prefix + "Экземпляр сервера сообщил, что не работает, но внешний сигнал остановки не получен. Инициализация завершения работы.");
            g_server_should_stop.store(true); 
        }
    }

    Logger::info(main_log_prefix + "Получен сигнал завершения или сервер остановлен внутренне. Инициализация Server::stop()...");
    server.stop(); 
    Logger::info(main_log_prefix + "Экземпляр сервера остановлен.");

    Logger::info(main_log_prefix + "========== СЕРВЕР БАЗЫ ДАННЫХ УСПЕШНО ЗАВЕРШИЛ РАБОТУ ==========");
    return 0;
}
