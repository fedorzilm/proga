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

// Вспомогательная функция для удаления пробельных символов с начала и конца строки
static std::string trimStringSC(const std::string& str) {
    const std::string whitespace = " \t\n\r\f\v";
    size_t start = str.find_first_not_of(whitespace);
    if (start == std::string::npos) return ""; // Строка пуста или только из пробелов
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
    const std::string cfg_log_prefix = "[ServerConfig LoadFile] ";
    Logger::info(cfg_log_prefix + "Попытка загрузки конфигурации из файла: '" + config_filename + "'");
    std::ifstream configFile(config_filename);
    if (!configFile.is_open()) {
        // Это не ошибка, если файл не найден, т.к. могут использоваться значения по умолчанию или аргументы командной строки.
        // ServerMain будет решать, является ли отсутствие файла ошибкой.
        Logger::info(cfg_log_prefix + "Файл конфигурации '" + config_filename + "' не найден или не удалось открыть. Будут использованы текущие значения (по умолчанию/из других источников).");
        return true; // Возвращаем true, т.к. отсутствие файла - не ошибка для этого метода.
    }

    std::string line;
    int line_num = 0;
    bool parse_error_occurred = false;

    while (std::getline(configFile, line)) {
        line_num++;
        std::string trimmed_line = trimStringSC(line);
        if (trimmed_line.empty() || trimmed_line[0] == '#') { // Пропуск пустых строк и комментариев
            continue;
        }

        std::istringstream line_ss(trimmed_line);
        std::string key, value;
        // Разделяем строку по первому символу '='
        if (std::getline(line_ss, key, '=') && std::getline(line_ss, value)) {
            key = trimStringSC(key);
            value = trimStringSC(value); // Удаляем пробелы и из значения

            if (key.empty()) {
                Logger::warn(cfg_log_prefix + "Пропущена строка " + std::to_string(line_num) + " в файле '" + config_filename + "' (пустой ключ). Строка: \"" + line + "\"");
                continue;
            }

            std::string key_upper = toUpperSC(key);
            // Logger::debug(cfg_log_prefix + "Чтение из конфига: Ключ='" + key + "', Значение='" + value + "'");

            try {
                if (key_upper == "PORT") {
                    if (value.empty()) throw std::invalid_argument("пустое значение для PORT");
                    int p_val = std::stoi(value);
                    if (p_val <= 0 || p_val > 65535) throw std::out_of_range("Port должен быть в диапазоне 1-65535, получено: " + value);
                    port = p_val;
                } else if (key_upper == "THREAD_POOL_SIZE") {
                    if (value.empty()) throw std::invalid_argument("пустое значение для THREAD_POOL_SIZE");
                    unsigned long pool_s_ul = std::stoul(value); // stoul для size_t
                    if (pool_s_ul == 0) {
                        Logger::warn(cfg_log_prefix + "THREAD_POOL_SIZE не может быть 0, установлено в 1 (из файла '" + value + "').");
                        thread_pool_size = 1;
                    } else if (pool_s_ul > 256) { // Произвольное верхнее ограничение для разумности
                        Logger::warn(cfg_log_prefix + "THREAD_POOL_SIZE (" + value + ") слишком большой, установлено в 256.");
                        thread_pool_size = 256;
                    }
                    else {
                        thread_pool_size = static_cast<size_t>(pool_s_ul);
                    }
                } else if (key_upper == "TARIFF_FILE_PATH") {
                    tariff_file_path = value; // Пути могут быть пустыми, если пользователь так указал
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
                    else {
                        // Не меняем log_level, если значение не распознано, просто предупреждаем
                        Logger::warn(cfg_log_prefix + "Неизвестное значение '" + value + "' для LOG_LEVEL в файле '" + config_filename + "'. Используется текущее значение: " + std::to_string(static_cast<int>(log_level)));
                    }
                } else if (key_upper == "LOG_FILE_PATH") {
                    log_file_path = value; // Путь может быть "" для вывода только в консоль
                } else {
                    Logger::warn(cfg_log_prefix + "Неизвестный ключ '" + key + "' в файле конфигурации '" + config_filename + "' (строка " + std::to_string(line_num) + "). Ключ проигнорирован.");
                    // Не считаем это фатальной ошибкой парсинга, просто пропускаем неизвестный ключ
                }
            } catch (const std::invalid_argument& e_ia) {
                Logger::error(cfg_log_prefix + "Ошибка парсинга значения для ключа '" + key + "' (значение: '" + value + "') в файле '" + config_filename + "' (строка " + std::to_string(line_num) + "): " + e_ia.what());
                parse_error_occurred = true; // Ошибка значения - это ошибка парсинга
            } catch (const std::out_of_range& e_oor) {
                 Logger::error(cfg_log_prefix + "Значение для ключа '" + key + "' (значение: '" + value + "') в файле '" + config_filename + "' (строка " + std::to_string(line_num) + ") выходит за допустимый диапазон: " + e_oor.what());
                parse_error_occurred = true; // Выход за диапазон - ошибка парсинга
            }
        } else if (!key.empty() || !trimmed_line.empty()) { // Если строка не пуста, но не в формате ключ=значение
            Logger::warn(cfg_log_prefix + "Пропущена некорректная строка " + std::to_string(line_num) + " в файле '" + config_filename + "' (не в формате ключ=значение): \"" + line + "\"");
            // Не считаем это фатальной ошибкой, если это не основная структура
        }
    }
    configFile.close();

    if (parse_error_occurred) {
        Logger::error(cfg_log_prefix + "Обнаружены ошибки при парсинге значений в файле конфигурации '" + config_filename + "'. Проверьте файл. Некоторые параметры могли быть не установлены или установлены некорректно.");
        return false; // Возвращаем false, если были ошибки парсинга *значений*
    }

    Logger::info(cfg_log_prefix + "Конфигурация из файла '" + config_filename + "' успешно обработана.");
    return true; // Успех, даже если были пропущены неизвестные ключи или некорректные строки без '='
}

void ServerConfig::printHelp(const char* app_name_char) {
    std::string app_name = (app_name_char && app_name_char[0] != '\0') ? app_name_char : "database_server";
    // Используем std::cout для вывода справки, т.к. Logger может быть еще не настроен
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
    std::cout << "  -l, --log-level <LEVEL>     Уровень логирования (DEBUG, INFO, WARN, ERROR, NONE).\n"
              << "                                По умолчанию: INFO.\n";
    std::cout << "  --log-file <путь_к_файлу>  Путь к файлу лога сервера.\n"
              << "                                По умолчанию: '" << DEFAULT_SERVER_LOG_FILE << "'.\n"
              << "                                Если указан пустой путь, логирование только в консоль.\n"
              << "                                Относительные пути разрешаются от директории исполняемого файла.\n";
    std::cout << "  -h, --help                  Показать это справочное сообщение и выйти.\n\n";
}

bool ServerConfig::parseCommandLineArgs(int argc, char* argv[], [[maybe_unused]] const std::string& server_executable_path) {
    // server_executable_path не используется напрямую здесь, т.к. пути разрешаются в Server или ServerMain.
    // Но он может быть полезен, если бы parseCommandLineArgs сам пытался разрешать пути.
    const std::string cla_log_prefix = "[ServerConfig Args] ";

    // Сначала ищем опцию -c/--config, так как она может загрузить файл,
    // значения из которого затем могут быть переопределены другими аргументами командной строки.
    // Это означает, что аргументы командной строки имеют наивысший приоритет.
    // Файл из -c имеет приоритет над файлом по умолчанию, загруженным ранее.
    std::string config_file_from_args;
    for (int i = 1; i < argc; ++i) {
        std::string arg_str = argv[i];
        if ((arg_str == "-c" || arg_str == "--config")) {
            if (i + 1 < argc) {
                config_file_from_args = argv[++i]; // Берем следующий аргумент как путь к файлу
                Logger::info(cla_log_prefix + "Указан файл конфигурации из командной строки: '" + config_file_from_args + "'. Попытка загрузки.");
                if (!std::filesystem::exists(config_file_from_args)) {
                     Logger::error(cla_log_prefix + "Указанный файл конфигурации '" + config_file_from_args + "' не найден. Загрузка не будет выполнена.");
                     // Не прерываем, т.к. другие аргументы могут быть важными. loadFromFile вернет true, если файл не найден.
                } else {
                    if (!loadFromFile(config_file_from_args)) { // Загружаем (и потенциально переопределяем) из указанного файла
                        // Ошибка уже залогирована в loadFromFile
                        Logger::error(cla_log_prefix + "Ошибка загрузки/парсинга файла конфигурации '" + config_file_from_args + "', указанного в командной строке. Проверьте файл. Завершение невозможно.");
                        printHelp(argv[0]);
                        return false; // Ошибка парсинга файла из -c фатальна
                    }
                }
            } else {
                Logger::error(cla_log_prefix + "Опция '" + arg_str + "' требует аргумент (путь к файлу).");
                printHelp(argv[0]);
                return false;
            }
             // После обработки -c, он нам больше не нужен в основном цикле парсинга
             // Можно было бы его "удалить" из argv или пропускать, но проще обработать один раз.
             break; // Предполагаем, что -c указывается один раз
        }
    }

    // Теперь парсим все остальные аргументы. Они переопределят значения из server_config (дефолтные или из файла).
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if ((arg == "-p" || arg == "--port")) {
            if (i + 1 < argc) {
                try {
                    int p_val = std::stoi(argv[++i]);
                    if (p_val <= 0 || p_val > 65535) {
                         Logger::error(cla_log_prefix + "Неверный номер порта '" + std::to_string(p_val) + "' из арг. командной строки. Должен быть 1-65535.");
                         printHelp(argv[0]); return false;
                    }
                    port = p_val;
                    // Logger::debug(cla_log_prefix + "Порт установлен из командной строки: " + std::to_string(port));
                } catch (const std::exception& e_stoi_port) {
                    Logger::error(cla_log_prefix + "Ошибка парсинга номера порта '" + std::string(argv[i]) + "' из арг. командной строки: " + e_stoi_port.what());
                    printHelp(argv[0]); return false;
                }
            } else { Logger::error(cla_log_prefix + "Опция '" + arg + "' требует аргумент (номер порта)."); printHelp(argv[0]); return false;}
        } else if (arg == "--threads") {
             if (i + 1 < argc) {
                try {
                    unsigned long pool_s_ul = std::stoul(argv[++i]);
                    if (pool_s_ul == 0) { thread_pool_size = 1; Logger::warn(cla_log_prefix + "--threads не может быть 0, установлено в 1."); }
                    else if (pool_s_ul > 256) { thread_pool_size = 256; Logger::warn(cla_log_prefix + "--threads (" + std::to_string(pool_s_ul) + ") слишком большой, установлено в 256.");}
                    else { thread_pool_size = static_cast<size_t>(pool_s_ul); }
                    // Logger::debug(cla_log_prefix + "Размер пула потоков установлен из командной строки: " + std::to_string(thread_pool_size));
                } catch (const std::exception& e_stoul_threads) {
                    Logger::error(cla_log_prefix + "Ошибка парсинга количества потоков '" + std::string(argv[i]) + "' из арг. командной строки: " + e_stoul_threads.what());
                    printHelp(argv[0]); return false;
                }
            } else { Logger::error(cla_log_prefix + "Опция '" + arg + "' требует аргумент (количество потоков)."); printHelp(argv[0]); return false;}
        } else if ((arg == "-t" || arg == "--tariff")) {
            if (i + 1 < argc) { tariff_file_path = argv[++i]; /* Logger::debug(cla_log_prefix + "Путь к файлу тарифов установлен из командной строки: " + tariff_file_path); */ }
            else { Logger::error(cla_log_prefix + "Опция '" + arg + "' требует аргумент (путь к файлу)."); printHelp(argv[0]); return false;}
        } else if ((arg == "-d" || arg == "--data-dir")) {
            if (i + 1 < argc) { server_data_root_dir = argv[++i]; /* Logger::debug(cla_log_prefix + "Директория данных сервера установлена из командной строки: " + server_data_root_dir); */ }
            else { Logger::error(cla_log_prefix + "Опция '" + arg + "' требует аргумент (путь к директории)."); printHelp(argv[0]); return false;}
        } else if ((arg == "-l" || arg == "--log-level")) {
            if (i + 1 < argc) {
                std::string level_str_arg = argv[++i];
                std::string level_val_upper = toUpperSC(level_str_arg);
                // bool level_set_from_arg = false; // Не используется, т.к. мы просто переопределяем
                if (level_val_upper == "DEBUG") { log_level = LogLevel::DEBUG; /*level_set_from_arg = true;*/ }
                else if (level_val_upper == "INFO") { log_level = LogLevel::INFO; /*level_set_from_arg = true;*/ }
                else if (level_val_upper == "WARN") { log_level = LogLevel::WARN; /*level_set_from_arg = true;*/ }
                else if (level_val_upper == "ERROR") { log_level = LogLevel::ERROR; /*level_set_from_arg = true;*/ }
                else if (level_val_upper == "NONE") { log_level = LogLevel::NONE; /*level_set_from_arg = true;*/ }
                else {
                    Logger::warn(cla_log_prefix + "Неизвестный уровень логирования '" + level_str_arg + "' из арг. командной строки. Уровень лога не изменен этим аргументом.");
                    // log_level остается тем, что был (из файла или по умолчанию)
                }
                // if(level_set_from_arg) Logger::debug(cla_log_prefix + "Уровень логирования установлен из командной строки: " + level_val_upper);
            } else { Logger::error(cla_log_prefix + "Опция '" + arg + "' требует аргумент (уровень логирования)."); printHelp(argv[0]); return false;}
        } else if (arg == "--log-file") {
             if (i + 1 < argc) { log_file_path = argv[++i]; /* Logger::debug(cla_log_prefix + "Файл лога установлен из командной строки: " + log_file_path); */ }
            else { Logger::error(cla_log_prefix + "Опция '" + arg + "' требует аргумент (путь к файлу)."); printHelp(argv[0]); return false;}
        } else if (arg == "-h" || arg == "--help") {
            printHelp(argv[0]); // Выводим справку
            return false; // Сигнализируем, что нужно завершить программу после справки
        } else if ((arg == "-c" || arg == "--config")) {
            // Пропускаем -c и его значение, так как они уже обработаны в начале
            if (i + 1 < argc) { ++i; } // Пропускаем значение для -c
        }
        else { // Неизвестный аргумент
            Logger::error(cla_log_prefix + "Неизвестный аргумент командной строки: " + arg);
            printHelp(argv[0]);
            return false;
        }
    }
    return true; // Все аргументы успешно разобраны (или не было ошибок)
}
