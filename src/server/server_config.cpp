/*!
 * \file server_config.cpp
 * \brief Реализация класса ServerConfig для управления конфигурацией сервера.
 */
#include "server_config.h"
#include "file_utils.h" 
#include "logger.h"
#include <fstream>
#include <sstream>
#include <algorithm> // Для std::transform
#include <iostream>  // Для ServerConfig::printHelp
#include <filesystem> // Для std::filesystem (может быть полезно для обработки путей)

// Вспомогательная функция для удаления начальных и конечных пробельных символов
static std::string trimStringSC(const std::string& str) {
    const std::string whitespace = " \t\n\r\f\v";
    size_t start = str.find_first_not_of(whitespace);
    if (start == std::string::npos) return ""; // Строка состоит только из пробелов или пуста
    size_t end = str.find_last_not_of(whitespace);
    return str.substr(start, (end - start + 1));
}

// Вспомогательная функция для преобразования строки в верхний регистр
static std::string toUpperSC(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return static_cast<char>(std::toupper(c)); });
    return s;
}

bool ServerConfig::loadFromFile(const std::string& config_filename) {
    const std::string cfg_log_prefix = "[Конфигурация Сервера Загрузка Файла] ";
    Logger::info(cfg_log_prefix + "Попытка загрузки конфигурации из файла: '" + config_filename + "'");
    std::ifstream configFile(config_filename);
    if (!configFile.is_open()) {
        Logger::info(cfg_log_prefix + "Файл конфигурации '" + config_filename + "' не найден или не удалось открыть. Будут использованы текущие значения.");
        return true; // Не ошибка, если файл не найден, используются значения по умолчанию/текущие
    }

    std::string line_content;
    int line_num = 0;
    
    while (std::getline(configFile, line_content)) {
        line_num++;
        
        // Удаляем комментарии в конце строки
        size_t comment_pos_inline = line_content.find('#');
        if (comment_pos_inline != std::string::npos) {
            line_content = line_content.substr(0, comment_pos_inline);
        }

        std::string trimmed_line = trimStringSC(line_content);
        if (trimmed_line.empty()) { // Пропускаем пустые строки или строки только с комментариями
            continue;
        }

        // Используем find и substr для разделения ключа и значения
        std::string key, value;
        size_t equal_pos = trimmed_line.find('=');

        if (equal_pos != std::string::npos) {
            key = trimmed_line.substr(0, equal_pos);
            value = trimmed_line.substr(equal_pos + 1);

            key = trimStringSC(key);
            value = trimStringSC(value);

            if (key.empty()) {
                Logger::warn(cfg_log_prefix + "Пропущена строка " + std::to_string(line_num) + " в файле '" + config_filename + "' (пустой ключ).");
                continue;
            }

            std::string key_upper = toUpperSC(key);
            
            try {
                if (key_upper == "PORT") {
                    if (value.empty()) { 
                         Logger::error(cfg_log_prefix + "Ошибка парсинга: пустое значение для обязательного ключа 'PORT' в файле '" + config_filename + "' (строка " + std::to_string(line_num) + ").");
                         configFile.close(); 
                         return false; 
                    }
                    int p_val = std::stoi(value);
                    if (p_val <= 0 || p_val > 65535) throw std::out_of_range("Порт должен быть в диапазоне 1-65535, получено: " + value);
                    port = p_val;
                } else if (key_upper == "THREAD_POOL_SIZE") {
                    if (value.empty()){
                        Logger::error(cfg_log_prefix + "Ошибка парсинга: пустое значение для обязательного ключа 'THREAD_POOL_SIZE' в файле '" + config_filename + "' (строка " + std::to_string(line_num) + ").");
                        configFile.close(); return false;
                    }
                    unsigned long pool_s_ul = std::stoul(value);
                    if (pool_s_ul == 0) {
                        Logger::warn(cfg_log_prefix + "THREAD_POOL_SIZE не может быть 0, установлено в 1 (из файла '" + value + "').");
                        thread_pool_size = 1;
                    } else if (pool_s_ul > 256) { 
                        Logger::warn(cfg_log_prefix + "THREAD_POOL_SIZE (" + value + ") слишком большой, установлено в 256.");
                        thread_pool_size = 256;
                    }
                    else {
                        thread_pool_size = static_cast<size_t>(pool_s_ul);
                    }
                } else if (key_upper == "TARIFF_FILE_PATH") {
                    if (value.empty()){
                       Logger::error(cfg_log_prefix + "Ошибка парсинга: пустое значение для обязательного ключа 'TARIFF_FILE_PATH' в файле '" + config_filename + "' (строка " + std::to_string(line_num) + ").");
                       configFile.close(); return false;
                    }
                    tariff_file_path = value; 
                } else if (key_upper == "SERVER_DATA_ROOT_DIR") {
                    server_data_root_dir = value;
                } else if (key_upper == "LOG_LEVEL") {
                    if (value.empty()){
                        Logger::error(cfg_log_prefix + "Ошибка парсинга: пустое значение для обязательного ключа 'LOG_LEVEL' в файле '" + config_filename + "' (строка " + std::to_string(line_num) + ").");
                        configFile.close(); return false;
                    }
                    std::string level_val_upper = toUpperSC(value);
                    if (level_val_upper == "DEBUG") log_level = LogLevel::DEBUG;
                    else if (level_val_upper == "INFO") log_level = LogLevel::INFO;
                    else if (level_val_upper == "WARN") log_level = LogLevel::WARN; 
                    else if (level_val_upper == "ERROR") log_level = LogLevel::ERROR;
                    else if (level_val_upper == "NONE") log_level = LogLevel::NONE;
                    else {
                        Logger::warn(cfg_log_prefix + "Неизвестное значение '" + value + "' для LOG_LEVEL в файле '" + config_filename + "' (строка " + std::to_string(line_num) + "). Используется текущее значение.");
                    }
                } else if (key_upper == "LOG_FILE_PATH") {
                    // Может быть пустым, тогда логирование только в консоль (или файл по умолчанию)
                    log_file_path = value; 
                } else {
                    Logger::warn(cfg_log_prefix + "Неизвестный ключ '" + key + "' в файле конфигурации '" + config_filename + "' (строка " + std::to_string(line_num) + "). Ключ проигнорирован.");
                }
            } catch (const std::invalid_argument& e_ia) {
                Logger::error(cfg_log_prefix + "Ошибка парсинга значения для ключа '" + key + "' (значение: '" + value + "') в файле '" + config_filename + "' (строка " + std::to_string(line_num) + "): " + e_ia.what());
                configFile.close(); return false; 
            } catch (const std::out_of_range& e_oor) {
                 Logger::error(cfg_log_prefix + "Значение для ключа '" + key + "' (значение: '" + value + "') в файле '" + config_filename + "' (строка " + std::to_string(line_num) + ") выходит за допустимый диапазон: " + e_oor.what());
                configFile.close(); return false; 
            }
        } else if (!trimmed_line.empty()) { // Строка не пуста, но не содержит '='
            Logger::warn(cfg_log_prefix + "Пропущена некорректная строка " + std::to_string(line_num) + " в файле '" + config_filename + "' (не в формате ключ=значение): \"" + line_content + "\"");
        }
    }
    configFile.close();
    Logger::info(cfg_log_prefix + "Конфигурация из файла '" + config_filename + "' успешно обработана.");
    return true;
}

void ServerConfig::printHelp(const char* app_name_char) {
    std::string app_name = (app_name_char && app_name_char[0] != '\0') ? app_name_char : "database_server";
    std::cout << "\nИспользование: " << app_name << " [опции]\n";
    std::cout << "Опции:\n";
    std::cout << "  -c, --config <файл>         Путь к файлу конфигурации сервера (например, server.conf).\n"
              << "                                Опции из командной строки имеют приоритет над файлом конфигурации.\n"
              << "                                Если указан, будет загружен ПОСЛЕ конфигурационного файла по умолчанию (если найден).\n";
    std::cout << "  -p, --port <номер_порта>    Сетевой порт для прослушивания сервером (1-65535).\n"
              << "                                По умолчанию: " << ServerConfig().port << ".\n"; 
    std::cout << "  --threads <кол-во>        Количество рабочих потоков в пуле (1-256).\n"
              << "                                По умолчанию: " << ServerConfig().thread_pool_size << ".\n";
    std::cout << "  -t, --tariff <путь_к_файлу> Путь к файлу тарифного плана.\n"
              << "                                По умолчанию: '" << ServerConfig().tariff_file_path << "'.\n"
              << "                                Относительные пути разрешаются от директории исполняемого файла.\n";
    std::cout << "  -d, --data-dir <путь_к_дир> Корневая директория для файлов баз данных сервера (операции LOAD/SAVE).\n"
              << "                                Если не указан, используется автоопределение (корень проекта или директория исполняемого файла),\n"
              << "                                файлы будут в поддиректории '" << DEFAULT_SERVER_DATA_SUBDIR << "'.\n"
              << "                                Относительные пути разрешаются от директории исполняемого файла.\n";
    std::cout << "  -l, --log-level <УРОВЕНЬ>   Уровень логирования (DEBUG, INFO, WARN, ERROR, NONE).\n"
              << "                                По умолчанию: INFO.\n";
    std::cout << "  --log-file <путь_к_файлу>  Путь к файлу лога сервера.\n"
              << "                                По умолчанию: '" << DEFAULT_SERVER_LOG_FILE << "'.\n"
              << "                                Если указан пустой путь, логирование только в консоль.\n"
              << "                                Относительные пути разрешаются от директории исполняемого файла.\n";
    std::cout << "  -h, --help                  Показать это справочное сообщение и выйти.\n\n";
}

bool ServerConfig::parseCommandLineArgs(int argc, char* argv[], [[maybe_unused]] const std::string& server_executable_path) {
    const std::string cla_log_prefix = "[Конфигурация Сервера Аргументы] ";
    std::string config_file_from_args;

    for (int i = 1; i < argc; ++i) {
        std::string arg_str = argv[i];
        if ((arg_str == "-c" || arg_str == "--config")) {
            if (i + 1 < argc) {
                config_file_from_args = argv[++i]; 
                Logger::info(cla_log_prefix + "Указан файл конфигурации из командной строки: '" + config_file_from_args + "'. Попытка загрузки.");
                if (!std::filesystem::exists(config_file_from_args)) {
                     Logger::warn(cla_log_prefix + "Указанный файл конфигурации '" + config_file_from_args + "' не найден. Загрузка не будет выполнена.");
                } else {
                    if (!loadFromFile(config_file_from_args)) { 
                        Logger::error(cla_log_prefix + "Ошибка загрузки/парсинга файла конфигурации '" + config_file_from_args + "', указанного в командной строке. Проверьте файл. Завершение невозможно.");
                        printHelp(argv[0]);
                        return false;
                    }
                }
            } else {
                Logger::error(cla_log_prefix + "Опция '" + arg_str + "' требует аргумент (путь к файлу).");
                printHelp(argv[0]);
                return false;
            }
             break; 
        }
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if ((arg == "-p" || arg == "--port")) {
            if (i + 1 < argc) {
                std::string port_str = argv[++i];
                try {
                    int p_val = std::stoi(port_str);
                    if (p_val <= 0 || p_val > 65535) {
                         Logger::error(cla_log_prefix + "Неверный номер порта '" + port_str + "' из арг. командной строки. Должен быть 1-65535.");
                         printHelp(argv[0]); return false;
                    }
                    port = p_val;
                } catch (const std::exception& e_stoi_port) {
                    Logger::error(cla_log_prefix + "Ошибка парсинга номера порта '" + port_str + "' из арг. командной строки: " + e_stoi_port.what());
                    printHelp(argv[0]); return false;
                }
            } else { Logger::error(cla_log_prefix + "Опция '" + arg + "' требует аргумент (номер порта)."); printHelp(argv[0]); return false;}
        } else if (arg == "--threads") {
             if (i + 1 < argc) {
                std::string threads_str = argv[++i];
                try {
                    if (!threads_str.empty() && threads_str[0] == '-') {
                        throw std::invalid_argument("отрицательное значение для --threads");
                    }
                    unsigned long pool_s_ul = std::stoul(threads_str);
                    if (pool_s_ul == 0) { thread_pool_size = 1; Logger::warn(cla_log_prefix + "--threads не может быть 0, установлено в 1."); }
                    else if (pool_s_ul > 256) { thread_pool_size = 256; Logger::warn(cla_log_prefix + "--threads (" + threads_str + ") слишком большой, установлено в 256.");}
                    else { thread_pool_size = static_cast<size_t>(pool_s_ul); }
                } catch (const std::exception& e_stoul_threads) {
                    Logger::error(cla_log_prefix + "Ошибка парсинга количества потоков '" + threads_str + "' из арг. командной строки: " + e_stoul_threads.what());
                    printHelp(argv[0]); return false;
                }
            } else { Logger::error(cla_log_prefix + "Опция '" + arg + "' требует аргумент (количество потоков)."); printHelp(argv[0]); return false;}
        } else if ((arg == "-t" || arg == "--tariff")) {
            if (i + 1 < argc) { tariff_file_path = argv[++i]; }
            else { Logger::error(cla_log_prefix + "Опция '" + arg + "' требует аргумент (путь к файлу)."); printHelp(argv[0]); return false;}
        } else if ((arg == "-d" || arg == "--data-dir")) {
            if (i + 1 < argc) { server_data_root_dir = argv[++i]; }
            else { Logger::error(cla_log_prefix + "Опция '" + arg + "' требует аргумент (путь к директории)."); printHelp(argv[0]); return false;}
        } else if ((arg == "-l" || arg == "--log-level")) {
            if (i + 1 < argc) {
                std::string level_str_arg = argv[++i];
                std::string level_val_upper = toUpperSC(level_str_arg);
                if (level_val_upper == "DEBUG") { log_level = LogLevel::DEBUG; }
                else if (level_val_upper == "INFO") { log_level = LogLevel::INFO; }
                else if (level_val_upper == "WARN") { log_level = LogLevel::WARN; }
                else if (level_val_upper == "ERROR") { log_level = LogLevel::ERROR; }
                else if (level_val_upper == "NONE") { log_level = LogLevel::NONE; }
                else {
                    Logger::warn(cla_log_prefix + "Неизвестный уровень логирования '" + level_str_arg + "' из арг. командной строки. Уровень лога не изменен этим аргументом.");
                }
            } else { Logger::error(cla_log_prefix + "Опция '" + arg + "' требует аргумент (уровень логирования)."); printHelp(argv[0]); return false;}
        } else if (arg == "--log-file") {
             if (i + 1 < argc) { log_file_path = argv[++i]; }
            else { Logger::error(cla_log_prefix + "Опция '" + arg + "' требует аргумент (путь к файлу)."); printHelp(argv[0]); return false;}
        } else if (arg == "-h" || arg == "--help") {
            printHelp(argv[0]); 
            return false; 
        } else if ((arg == "-c" || arg == "--config")) {
            // Аргумент -c/--config и его значение уже обработаны в первом проходе,
            // поэтому здесь их нужно пропустить, чтобы не вызвать ошибку "неизвестный аргумент".
            if (i + 1 < argc) { // Пропускаем значение, если оно есть
                // Дополнительно проверяем, не является ли следующий токен новой опцией,
                // на случай если -c был последним без значения (хотя это должно было быть поймано выше)
                std::string next_arg_check = argv[i+1];
                if (next_arg_check.rfind("-", 0) != 0) { // Если следующий токен не начинается с '-'
                     i++; // то это значение для -c, пропускаем его
                }
            }
        }
        else { 
            Logger::error(cla_log_prefix + "Неизвестный аргумент командной строки: " + arg);
            printHelp(argv[0]);
            return false;
        }
    }
    return true; 
}
