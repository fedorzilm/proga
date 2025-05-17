/*!
 * \file server_config.cpp
 * \author Fedor Zilnitskiy
 * \brief Реализация класса ServerConfig для управления конфигурацией сервера.
 */
#include "server_config.h"
#include "file_utils.h" // Для getProjectDataPath, если потребуется разрешать пути относительно проекта
#include "logger.h"     // Для логирования процесса загрузки конфигурации
#include <fstream>
#include <sstream>
#include <algorithm> // для std::transform (toUpperSC)
#include <iostream>  // для ServerConfig::printHelp
#include <filesystem> // для std::filesystem::path, std::filesystem::exists

static std::string trimStringSC(const std::string& str) {
    const std::string whitespace = " \t\n\r\f\v";
    size_t start = str.find_first_not_of(whitespace);
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(whitespace);
    return str.substr(start, (end - start + 1));
}

static std::string toUpperSC(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return static_cast<char>(std::toupper(c)); });
    return s;
}

bool ServerConfig::loadFromFile(const std::string& config_filename) {
    Logger::info("ServerConfig: Попытка загрузки конфигурации из файла: '" + config_filename + "'");
    std::ifstream configFile(config_filename);
    if (!configFile.is_open()) {
        Logger::info("ServerConfig: Файл конфигурации '" + config_filename + "' не найден. Будут использованы значения по умолчанию и аргументы командной строки.");
        return true;
    }

    std::string line;
    int line_num = 0;
    bool parse_error_occurred = false;

    while (std::getline(configFile, line)) {
        line_num++;
        std::string trimmed_line = trimStringSC(line);
        if (trimmed_line.empty() || trimmed_line[0] == '#') {
            continue;
        }

        std::istringstream line_ss(trimmed_line);
        std::string key, value;
        if (std::getline(line_ss, key, '=') && std::getline(line_ss, value)) {
            key = trimStringSC(key);
            value = trimStringSC(value);

            if (key.empty()) {
                Logger::warn("ServerConfig: Пропущена строка " + std::to_string(line_num) + " в файле '" + config_filename + "' (пустой ключ). Строка: \"" + line + "\"");
                continue;
            }

            std::string key_upper = toUpperSC(key);
            Logger::debug("ServerConfig: Чтение из конфига: Ключ='" + key + "', Значение='" + value + "'");

            try {
                if (key_upper == "PORT") {
                    if (value.empty()) throw std::invalid_argument("пустое значение для PORT");
                    port = std::stoi(value);
                    if (port <= 0 || port > 65535) throw std::out_of_range("Port должен быть в диапазоне 1-65535");
                } else if (key_upper == "THREAD_POOL_SIZE") {
                    if (value.empty()) throw std::invalid_argument("пустое значение для THREAD_POOL_SIZE");
                    unsigned long pool_s_ul = std::stoul(value);
                    if (pool_s_ul == 0) {
                        Logger::warn("ServerConfig: THREAD_POOL_SIZE не может быть 0, установлено в 1.");
                        thread_pool_size = 1;
                    } else {
                        thread_pool_size = static_cast<size_t>(pool_s_ul);
                    }
                } else if (key_upper == "TARIFF_FILE_PATH") {
                    tariff_file_path = value;
                } else if (key_upper == "SERVER_DATA_ROOT_DIR") {
                    server_data_root_dir = value;
                } else if (key_upper == "LOG_LEVEL") {
                    if (value.empty()) throw std::invalid_argument("пустое значение для LOG_LEVEL");
                    std::string level_val_upper = toUpperSC(value);
                    if (level_val_upper == "DEBUG") log_level = LogLevel::DEBUG;
                    else if (level_val_upper == "INFO") log_level = LogLevel::INFO;
                    else if (level_val_upper == "WARN") log_level = LogLevel::WARN;
                    else if (level_val_upper == "ERROR") log_level = LogLevel::ERROR;
                    else if (level_val_upper == "NONE") log_level = LogLevel::NONE;
                    else Logger::warn("ServerConfig: Неизвестное значение '" + value + "' для LOG_LEVEL в файле конфигурации. Используется текущее: " + std::to_string(static_cast<int>(log_level)));
                } else if (key_upper == "LOG_FILE_PATH") {
                    log_file_path = value;
                } else {
                    Logger::error("ServerConfig: Неизвестный ключ '" + key + "' в файле конфигурации '" + config_filename + "' (строка " + std::to_string(line_num) + ").");
                    parse_error_occurred = true;
                }
            } catch (const std::invalid_argument& e_ia) {
                Logger::error("ServerConfig: Ошибка парсинга значения для ключа '" + key + "' (значение: '" + value + "') в файле '" + config_filename + "' (строка " + std::to_string(line_num) + "): " + e_ia.what());
                parse_error_occurred = true;
            } catch (const std::out_of_range& e_oor) {
                 Logger::error("ServerConfig: Значение для ключа '" + key + "' (значение: '" + value + "') в файле '" + config_filename + "' (строка " + std::to_string(line_num) + ") выходит за допустимый диапазон: " + e_oor.what());
                parse_error_occurred = true;
            }
        } else if (!key.empty() || !trimmed_line.empty()) {
            Logger::warn("ServerConfig: Пропущена некорректная строка " + std::to_string(line_num) + " в файле '" + config_filename + "' (не в формате ключ=значение): \"" + line + "\"");
        }
    }
    configFile.close();

    if (parse_error_occurred) {
        Logger::error("ServerConfig: Обнаружены ошибки (включая неизвестные ключи или неверные значения) при парсинге файла конфигурации '" + config_filename + "'. Проверьте файл конфигурации. Некоторые значения могут быть некорректны или не установлены.");
        return false;
    }

    Logger::info("ServerConfig: Конфигурация из файла '" + config_filename + "' успешно загружена (или файл не найден и использованы значения по умолчанию).");
    return true;
}

void ServerConfig::printHelp(const char* app_name_char) {
    std::string app_name = (app_name_char && app_name_char[0] != '\0') ? app_name_char : "database_server";
    std::cout << "Использование: " << app_name << " [опции]\n";
    std::cout << "Опции:\n";
    std::cout << "  -c, --config <файл>         Путь к файлу конфигурации сервера (например, server.conf).\n"
              << "                                Опции из командной строки имеют приоритет над файлом конфигурации.\n";
    std::cout << "  -p, --port <номер_порта>    Сетевой порт для прослушивания сервером (по умолч.: " << ServerConfig().port << ").\n";
    std::cout << "  --threads <кол-во>        Количество рабочих потоков в пуле (по умолч.: " << ServerConfig().thread_pool_size << ").\n";
    std::cout << "  -t, --tariff <путь_к_файлу> Путь к файлу тарифного плана (по умолч.: '" << ServerConfig().tariff_file_path << "').\n";
    std::cout << "  -d, --data-dir <путь_к_дир> Корневая директория для файлов баз данных сервера (операции LOAD/SAVE).\n"
              << "                                Если не указан, используется автоопределение (корень проекта или CWD),\n"
              << "                                файлы будут в поддиректории '" << DEFAULT_SERVER_DATA_SUBDIR << "'.\n";
    std::cout << "  -l, --log-level <LEVEL>     Уровень логирования (DEBUG, INFO, WARN, ERROR, NONE).\n"
              << "                                По умолчанию: INFO.\n";
    std::cout << "  --log-file <путь_к_файлу>  Путь к файлу лога сервера (по умолч.: '" << DEFAULT_SERVER_LOG_FILE << "').\n"
              << "                                Если указан пустой путь, логирование только в консоль.\n";
    std::cout << "  -h, --help                  Показать это справочное сообщение и выйти.\n";
}

bool ServerConfig::parseCommandLineArgs(int argc, char* argv[], [[maybe_unused]] const std::string& server_executable_path) {
    std::string config_file_from_args;
    for (int i = 1; i < argc; ++i) {
        std::string arg_str = argv[i];
        if ((arg_str == "-c" || arg_str == "--config")) {
            if (i + 1 < argc) {
                config_file_from_args = argv[++i];
                Logger::info("ServerConfig Args: Указан файл конфигурации из командной строки: '" + config_file_from_args + "' для переопределения.");
                if (!std::filesystem::exists(config_file_from_args)) {
                     Logger::error("ServerConfig Args: Указанный файл конфигурации '" + config_file_from_args + "' не найден.");
                } else {
                    if (!loadFromFile(config_file_from_args)) {
                        Logger::error("ServerConfig Args: Ошибка загрузки/парсинга файла конфигурации '" + config_file_from_args + "', указанного в командной строке. Завершение.");
                        return false;
                    }
                }
            } else {
                Logger::error("ServerConfig Args: Опция '" + arg_str + "' требует аргумент (путь к файлу).");
                return false;
            }
             break;
        }
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if ((arg == "-p" || arg == "--port")) {
            if (i + 1 < argc) {
                try {
                    int p_val = std::stoi(argv[++i]);
                    if (p_val <= 0 || p_val > 65535) {
                         Logger::error("ServerConfig Args: Неверный номер порта '" + std::to_string(p_val) + "' из арг. командной строки. Должен быть 1-65535.");
                         return false;
                    }
                    port = p_val;
                    Logger::debug("ServerConfig Args: Порт установлен из командной строки: " + std::to_string(port));
                } catch (const std::exception& e) {
                    Logger::error("ServerConfig Args: Ошибка парсинга номера порта '" + std::string(argv[i]) + "' из арг. командной строки: " + e.what());
                    return false;
                }
            } else { Logger::error("ServerConfig Args: Опция '" + arg + "' требует аргумент (номер порта)."); return false;}
        } else if (arg == "--threads") {
             if (i + 1 < argc) {
                try {
                    unsigned long pool_s_ul = std::stoul(argv[++i]);
                    if (pool_s_ul == 0) { thread_pool_size = 1; Logger::warn("ServerConfig Args: --threads не может быть 0, установлено в 1."); }
                    else { thread_pool_size = static_cast<size_t>(pool_s_ul); }
                     Logger::debug("ServerConfig Args: Размер пула потоков установлен из командной строки: " + std::to_string(thread_pool_size));
                } catch (const std::exception& e) {
                    Logger::error("ServerConfig Args: Ошибка парсинга количества потоков '" + std::string(argv[i]) + "' из арг. командной строки: " + e.what());
                    return false;
                }
            } else { Logger::error("ServerConfig Args: Опция '" + arg + "' требует аргумент (количество потоков)."); return false;}
        } else if ((arg == "-t" || arg == "--tariff")) {
            if (i + 1 < argc) { tariff_file_path = argv[++i]; Logger::debug("ServerConfig Args: Путь к файлу тарифов установлен из командной строки: " + tariff_file_path); }
            else { Logger::error("ServerConfig Args: Опция '" + arg + "' требует аргумент (путь к файлу)."); return false;}
        } else if ((arg == "-d" || arg == "--data-dir")) {
            if (i + 1 < argc) { server_data_root_dir = argv[++i]; Logger::debug("ServerConfig Args: Директория данных сервера установлена из командной строки: " + server_data_root_dir); }
            else { Logger::error("ServerConfig Args: Опция '" + arg + "' требует аргумент (путь к директории)."); return false;}
        } else if ((arg == "-l" || arg == "--log-level")) {
            if (i + 1 < argc) {
                std::string level_str_arg = argv[++i];
                std::string level_val_upper = toUpperSC(level_str_arg);
                bool level_set = true;
                if (level_val_upper == "DEBUG") log_level = LogLevel::DEBUG;
                else if (level_val_upper == "INFO") log_level = LogLevel::INFO;
                else if (level_val_upper == "WARN") log_level = LogLevel::WARN;
                else if (level_val_upper == "ERROR") log_level = LogLevel::ERROR;
                else if (level_val_upper == "NONE") log_level = LogLevel::NONE;
                else {
                    Logger::warn("ServerConfig Args: Неизвестный уровень логирования '" + level_str_arg + "' из арг. командной строки. Используется текущий ("+ std::to_string(static_cast<int>(log_level)) +").");
                    level_set = false;
                }
                if(level_set) Logger::debug("ServerConfig Args: Уровень логирования установлен из командной строки: " + level_val_upper);
            } else { Logger::error("ServerConfig Args: Опция '" + arg + "' требует аргумент (уровень логирования)."); return false;}
        } else if (arg == "--log-file") {
             if (i + 1 < argc) { log_file_path = argv[++i]; Logger::debug("ServerConfig Args: Файл лога установлен из командной строки: " + log_file_path); }
            else { Logger::error("ServerConfig Args: Опция '" + arg + "' требует аргумент (путь к файлу)."); return false;}
        } else if (arg == "-h" || arg == "--help") {
            return false;
        } else if ((arg == "-c" || arg == "--config")) {
            if (i + 1 < argc) { ++i; }
        } else {
            Logger::error("ServerConfig Args: Неизвестный аргумент командной строки: " + arg);
            return false;
        }
    }
    return true;
}
