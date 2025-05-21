/*!
 * \file generator.cpp
 * \author Fedor Zilnitskiy
 * \brief Утилита для генерации тестовых данных (записей интернет-провайдера).
 *
 * Эта программа создает файл с указанным количеством записей ProviderRecord,
 * содержащих случайные, но правдоподобные данные (ФИО, IP-адреса, даты, объемы трафика).
 * Используется для наполнения базы данных тестовыми данными для отладки и демонстрации.
 * Параметры генерации (количество записей, имя выходного файла, диапазон дат, макс. трафик)
 * задаются через аргументы командной строки.
 */
#include "common_defs.h"     // Для HOURS_IN_DAY, DOUBLE_EPSILON и стандартных заголовков
#include "provider_record.h" // Структура записи ProviderRecord
#include "date.h"            // Класс Date
#include "ip_address.h"      // Класс IPAddress
#include "file_utils.h"      // Не используется напрямую, но может быть полезен для путей в более сложных сценариях
#include "logger.h"          // Для логирования работы генератора

#include <iostream>         // Для std::cout, std::cerr
#include <fstream>          // Для std::ofstream
#include <vector>
#include <string>
#include <random>           // Для std::mt19937, std::random_device, std::uniform_int_distribution, std::uniform_real_distribution
#include <algorithm>        // Для std::generate_n (не используется в текущей версии, но может быть полезно)
#include <stdexcept>        // Для std::runtime_error, std::invalid_argument, std::stoi, std::stod
#include <ctime>            // Для std::time, std::localtime
#include <chrono>           // Для std::chrono::system_clock (C++11 и выше)
#include <iomanip>          // Для std::put_time (C++11 и выше, если нужно форматировать время)
#include <limits>           // Для std::numeric_limits


// --- Вспомогательные функции для генерации случайных данных ---

/*!
 * \brief Получает ссылку на статический генератор случайных чисел Mersenne Twister.
 * Инициализируется с помощью `std::random_device` для обеспечения недетерминированной начальной точки.
 * \return Ссылка на `std::mt19937`.
 */
std::mt19937& get_random_engine() {
    static std::random_device rd_device; // Недетерминированный источник энтропии
    static std::mt19937 mersenne_twister_engine(rd_device()); // Инициализация генератора
    return mersenne_twister_engine;
}

/*!
 * \brief Генерирует случайное целое число в заданном диапазоне [min_val, max_val].
 * \param min_val Минимальное возможное значение (включительно).
 * \param max_val Максимальное возможное значение (включительно).
 * \return Случайное целое число.
 */
int random_int(int min_val, int max_val) {
    if (min_val > max_val) { // Защита от некорректного диапазона
        std::swap(min_val, max_val);
    }
    std::uniform_int_distribution<> distrib(min_val, max_val);
    return distrib(get_random_engine());
}

/*!
 * \brief Генерирует случайное число с плавающей запятой (double) в заданном диапазоне [min_val, max_val].
 * Для генерации используется `std::uniform_real_distribution`, который генерирует значения в [min_val, max_val).
 * Чтобы включить max_val, можно использовать `std::nextafter(max_val, std::numeric_limits<double>::infinity())`
 * или немного увеличить верхнюю границу, если это критично. Для генерации трафика это обычно не принципиально.
 * \param min_val Минимальное возможное значение (включительно).
 * \param max_val Максимальное возможное значение (почти включительно, см. описание).
 * \return Случайное число double.
 */
double random_double(double min_val, double max_val) {
    if (min_val > max_val) {
        std::swap(min_val, max_val);
    }
    std::uniform_real_distribution<> distrib(min_val, std::nextafter(max_val, std::numeric_limits<double>::max()));
    return distrib(get_random_engine());
}

/*!
 * \brief Генерирует случайное полное имя (Фамилия Имя Отчество) из предопределенных списков,
 * с учетом согласования по роду.
 * \return Строка со случайным ФИО.
 */
std::string generate_random_name() {
    static const std::vector<std::string> male_first_names = {
        "Иван", "Петр", "Сидор", "Алексей", "Дмитрий", "Сергей", "Андрей", "Михаил", "Владимир", "Артем",
        "Егор", "Максим", "Никита", "Олег", "Павел", "Роман", "Степан", "Тимур", "Федор", "Юрий"
    };
    static const std::vector<std::string> female_first_names = {
        "Анна", "Мария", "Елена", "Ольга", "Светлана", "Татьяна", "Наталья", "Ирина", "Виктория", "Екатерина"
    };

    static const std::vector<std::string> base_last_names = {
        "Иванов", "Петров", "Сидоров", "Кузнецов", "Смирнов", "Попов", "Волков", "Зайцев", "Белов", "Соколов",
        "Михайлов", "Новиков", "Федоров", "Морозов", "Васильев", "Орлов", "Егоров", "Козлов", "Степанов", "Николаев"
    };

    struct PatronymicBase {
        std::string base_name_for_selection; 
        std::string male_patronymic_form;
        std::string female_patronymic_form;
    };

    static const std::vector<PatronymicBase> patronymic_options = {
        {"Иван", "Иванович", "Ивановна"},
        {"Петр", "Петрович", "Петровна"},
        {"Сидор", "Сидорович", "Сидоровна"},
        {"Алексей", "Алексеевич", "Алексеевна"},
        {"Дмитрий", "Дмитриевич", "Дмитриевна"},
        {"Сергей", "Сергеевич", "Сергеевна"},
        {"Андрей", "Андреевич", "Андреевна"},
        {"Михаил", "Михайлович", "Михайловна"},
        {"Владимир", "Владимирович", "Владимировна"},
        {"Артем", "Артемович", "Артемовна"},
        {"Егор", "Егорович", "Егоровна"},
        {"Максим", "Максимович", "Максимовна"},
        {"Никита", "Никитич", "Никитична"}, 
        {"Олег", "Олегович", "Олеговна"},
        {"Павел", "Павлович", "Павловна"},
        {"Роман", "Романович", "Романовна"},
        {"Степан", "Степанович", "Степановна"},
        {"Тимур", "Тимурович", "Тимуровна"},
        {"Федор", "Федорович", "Федоровна"},
        {"Юрий", "Юрьевич", "Юрьевна"}
    };

    bool is_female = (random_int(0, 1) == 1);

    std::string first_name;
    std::string last_name;
    std::string patronymic;

    if (is_female) {
        if (female_first_names.empty()) { 
            Logger::warn("[Generator] Список женских имен пуст. Используется мужское имя.");
             if (male_first_names.empty()) {
                 Logger::error("[Generator] Списки мужских и женских имен пусты. Невозможно сгенерировать ФИО.");
                 return "ОшибкаИмени ОшибкаФамилии ОшибкаОтчества";
             }
            first_name = male_first_names[static_cast<size_t>(random_int(0, static_cast<int>(male_first_names.size()) - 1))];
            is_female = false; 
        } else {
            first_name = female_first_names[static_cast<size_t>(random_int(0, static_cast<int>(female_first_names.size()) - 1))];
        }
    } else {
         if (male_first_names.empty()) { 
            Logger::error("[Generator] Список мужских имен пуст. Невозможно сгенерировать ФИО.");
            return "ОшибкаИмени ОшибкаФамилии ОшибкаОтчества";
        }
        first_name = male_first_names[static_cast<size_t>(random_int(0, static_cast<int>(male_first_names.size()) - 1))];
    }

    if (base_last_names.empty()) {
        Logger::error("[Generator] Список базовых фамилий пуст. Невозможно сгенерировать ФИО.");
        return first_name + " ОшибкаФамилии ОшибкаОтчества";
    }
    last_name = base_last_names[static_cast<size_t>(random_int(0, static_cast<int>(base_last_names.size()) - 1))];
    
    // ИСПРАВЛЕННАЯ ЛОГИКА ДЛЯ ЖЕНСКИХ ФАМИЛИЙ
    if (is_female) {
        if (last_name.length() >= 2) { // Убедимся, что фамилия достаточно длинная для суффикса
            // Суффиксы мужских фамилий, которые образуют женскую форму добавлением 'а'
            // В UTF-8 русские буквы занимают 2 байта. "ов", "ев", "ин" - это 4 байта.
            // last_name.substr() работает с количеством байт, а не символов, если не используется <codecvt>
            // Однако, std::string сравнение ("==") для UTF-8 строк работает корректно.
            
            std::string suffix_ov = "ов"; // UTF-8: d0 be d0 b2
            std::string suffix_ev = "ев"; // UTF-8: d0 b5 d0 b2
            // std::string suffix_in = "ин"; // UTF-8: d0 b8 d0 bd (на случай расширения списка)

            bool ends_with_known_suffix = false;
            // Проверяем, оканчивается ли строка на один из суффиксов
            if (last_name.length() >= suffix_ov.length() && 
                last_name.substr(last_name.length() - suffix_ov.length()) == suffix_ov) {
                ends_with_known_suffix = true;
            } else if (last_name.length() >= suffix_ev.length() &&
                       last_name.substr(last_name.length() - suffix_ev.length()) == suffix_ev) {
                ends_with_known_suffix = true;
            } 
            // else if (last_name.length() >= suffix_in.length() && 
            //            last_name.substr(last_name.length() - suffix_in.length()) == suffix_in) {
            //     ends_with_known_suffix = true;
            // }

            if (ends_with_known_suffix) {
                 last_name += "а"; // Добавляем 'а' (UTF-8: d0 b0)
            } else {
                // Если фамилия не оканчивается на стандартные -ов/-ев/-ин,
                // можно либо оставить как есть, либо добавить специальное правило,
                // либо просто добавить 'а' как общее правило (менее точно).
                // Для текущего списка base_last_names это условие (else) не должно срабатывать.
                // Logger::warn("[Generator-FIO] Фамилия '" + last_name + "' не оканчивается на -ов/-ев. Добавляем 'а' по умолчанию.");
                // last_name += "а"; // Оставить это, если хотим добавлять 'а' даже к нестандартным фамилиям
            }
        }
    }

    if (patronymic_options.empty()) {
         Logger::error("[Generator] Список опций для отчеств пуст. Невозможно сгенерировать ФИО.");
        return last_name + " " + first_name + " ОшибкаОтчества";
    }
    const PatronymicBase& p_option = patronymic_options[static_cast<size_t>(random_int(0, static_cast<int>(patronymic_options.size()) - 1))];
    if (is_female) {
        patronymic = p_option.female_patronymic_form;
    } else {
        patronymic = p_option.male_patronymic_form;
    }

    return last_name + " " + first_name + " " + patronymic;
}


/*!
 * \brief Генерирует случайный IPv4-адрес.
 * Первый октет генерируется в диапазоне [1, 223] (классы A, B, C, исключая широковещательные и специальные).
 * Остальные октеты [0, 255], последний хостовый октет [1, 254] (исключая адрес сети и широковещательный адрес подсети).
 * \return Объект `IPAddress` со случайным значением.
 */
IPAddress generate_random_ip() {
    return IPAddress(random_int(1, 223),    
                     random_int(0, 255),    
                     random_int(0, 255),    
                     random_int(1, 254));   
}

/*!
 * \brief Генерирует случайную дату в заданном диапазоне лет.
 * Учитывает корректное количество дней в месяце, включая високосные годы.
 * \param start_year Начальный год диапазона (включительно).
 * \param end_year Конечный год диапазона (включительно).
 * \return Объект `Date` со случайным значением.
 */
Date generate_random_date(int start_year = 2022, int end_year = 2024) {
    if (start_year > end_year) std::swap(start_year, end_year);
    
    while (true) { 
        try {
            int year = random_int(start_year, end_year);
            int month = random_int(1, 12);
            int day_max = 31; 
            if (month == 2) { 
                 bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
                 day_max = leap ? 29 : 28;
            } else if (month == 4 || month == 6 || month == 9 || month == 11) { 
                day_max = 30;
            }
            int day = random_int(1, day_max);
            return Date(day, month, year); 
        } catch (const std::invalid_argument& e_date) {
            Logger::warn("[Generator] Исключение при генерации случайной даты (повторная попытка): " + std::string(e_date.what()));
        }
    }
}

/*!
 * \brief Генерирует вектор случайных значений трафика (в ГБ) для каждого часа суток.
 * \param max_gb_per_hour Максимальное значение трафика (в ГБ) для одного часа.
 * Значения генерируются в диапазоне [0.0, max_gb_per_hour].
 * \return Вектор из `HOURS_IN_DAY` значений типа `double`.
 */
std::vector<double> generate_random_traffic(double max_gb_per_hour = 10.0) {
    if (max_gb_per_hour < 0.0) max_gb_per_hour = 0.0; 
    std::vector<double> traffic(HOURS_IN_DAY);
    for (int i = 0; i < HOURS_IN_DAY; ++i) {
        double val = random_double(0.0, max_gb_per_hour);
        if ((i < 6 || i > 22) && random_int(0, 3) == 0) { 
            val = 0.0;
        } else if (random_int(0, 10) == 0) { 
             val = 0.0;
        }
        traffic[static_cast<size_t>(i)] = val;
    }
    return traffic;
}

/*!
 * \brief Главная функция утилиты генератора данных.
 * \param argc Количество аргументов командной строки.
 * \param argv Массив строк аргументов командной строки.
 * Ожидаемые аргументы: <количество_записей> <выходной_файл> [макс_трафик_в_час_ГБ] [начальный_год] [конечный_год]
 * \return 0 в случае успеха, 1 при ошибке.
 */
int main(int argc, char* argv[]) {
    Logger::init(LogLevel::INFO, DEFAULT_GENERATOR_LOG_FILE);
    const std::string gen_log_prefix = "[Generator] ";
    Logger::info(gen_log_prefix + "Запуск генератора тестовых данных для базы интернет-провайдера...");

    std::time_t t = std::time(nullptr);
    std::tm local_tm_buf{}; 
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&local_tm_buf, &t);
#else
    localtime_r(&t, &local_tm_buf);
#endif
    int current_year_val = local_tm_buf.tm_year + 1900;

    if (argc < 3) {
        std::string usage_msg = "Использование: " + std::string(argv[0]) +
                                " <количество_записей> <выходной_файл_данных> "
                                "[макс_трафик_в_час_ГБ (по умолч.: 10.0)] "
                                "[начальный_год (по умолч.: 2022)] "
                                "[конечный_год (по умолч.: " + std::to_string(current_year_val) + ")]";
        Logger::error(gen_log_prefix + "Недостаточно аргументов. " + usage_msg);
        std::cerr << usage_msg << std::endl;
        return 1;
    }

    long long num_records_ll = 0; 
    try {
        num_records_ll = std::stoll(argv[1]); 
        if (num_records_ll <= 0) {
            throw std::invalid_argument("Количество записей должно быть положительным числом.");
        }
        if (num_records_ll > 20000000) { 
             Logger::warn(gen_log_prefix + "Запрошено очень большое количество записей (" + std::to_string(num_records_ll) + "). Установлено ограничение в 20,000,000.");
             num_records_ll = 20000000;
        }
    } catch (const std::exception& e_stoll_num) {
        Logger::error(gen_log_prefix + "Ошибка парсинга количества записей '" + std::string(argv[1]) + "': " + e_stoll_num.what());
        std::cerr << "Ошибка: Некорректное количество записей: " << argv[1] << ". " << e_stoll_num.what() << std::endl;
        return 1;
    }
    int num_records = static_cast<int>(num_records_ll); 

    std::string output_filename = argv[2];
    if (output_filename.empty()) {
        Logger::error(gen_log_prefix + "Имя выходного файла не может быть пустым.");
        std::cerr << "Ошибка: Имя выходного файла не указано." << std::endl;
        return 1;
    }

    double max_traffic_per_hour = 10.0; 
    int start_year_default = 2022;
    int end_year_default = current_year_val; 
    if (end_year_default < start_year_default) end_year_default = start_year_default; 

    int start_year = start_year_default;
    int end_year = end_year_default;

    if (argc > 3) {
        try {
            max_traffic_per_hour = std::stod(argv[3]);
            if(max_traffic_per_hour < 0.0) {
                Logger::warn(gen_log_prefix + "Максимальный трафик в час не может быть отрицательным (" + std::string(argv[3]) + "). Используется значение по умолчанию: " + std::to_string(10.0));
                max_traffic_per_hour = 10.0;
            }
        }
        catch (const std::exception& e_stod_traffic) {
            Logger::warn(gen_log_prefix + "Не удалось разобрать макс_трафик_в_час ('" + std::string(argv[3]) + "'): " + e_stod_traffic.what() + ". Используется значение по умолчанию: " + std::to_string(max_traffic_per_hour));
        }
    }
    if (argc > 4) {
        try { start_year = std::stoi(argv[4]); }
        catch (const std::exception& e_stoi_start_year) {
            Logger::warn(gen_log_prefix + "Не удалось разобрать начальный_год ('" + std::string(argv[4]) + "'): " + e_stoi_start_year.what() + ". Используется значение по умолчанию: " + std::to_string(start_year_default));
            start_year = start_year_default;
        }
    }
    if (argc > 5) {
        try { end_year = std::stoi(argv[5]); }
        catch (const std::exception& e_stoi_end_year) {
            Logger::warn(gen_log_prefix + "Не удалось разобрать конечный_год ('" + std::string(argv[5]) + "'): " + e_stoi_end_year.what() + ". Используется значение по умолчанию: " + std::to_string(end_year_default));
            end_year = end_year_default;
        }
    }

    if (start_year > end_year) {
        Logger::warn(gen_log_prefix + "Начальный год (" + std::to_string(start_year) + ") больше конечного (" + std::to_string(end_year) + "). Меняем их местами.");
        std::swap(start_year, end_year);
    }
    
    const int DATE_MIN_YEAR = 1900; 
    const int DATE_MAX_YEAR = 2100; 
    if (start_year < DATE_MIN_YEAR) { Logger::warn(gen_log_prefix + "Начальный год " + std::to_string(start_year) + " меньше минимально допустимого " + std::to_string(DATE_MIN_YEAR) + ". Установлен в " + std::to_string(DATE_MIN_YEAR)); start_year = DATE_MIN_YEAR; }
    if (end_year > DATE_MAX_YEAR)   { Logger::warn(gen_log_prefix + "Конечный год " + std::to_string(end_year) + " больше максимально допустимого " + std::to_string(DATE_MAX_YEAR) + ". Установлен в " + std::to_string(DATE_MAX_YEAR));   end_year = DATE_MAX_YEAR; }
    if (start_year > end_year) { 
        Logger::warn(gen_log_prefix + "После коррекции диапазона лет начальный год (" + std::to_string(start_year) + ") стал больше конечного (" + std::to_string(end_year) + "). Конечный год установлен равным начальному.");
        end_year = start_year;
    }

    Logger::info(gen_log_prefix + "Генерация " + std::to_string(num_records) + " записей в файл: '" + output_filename + "'");
    Logger::info(gen_log_prefix + "Параметры генерации: Макс. трафик/час=" + std::to_string(max_traffic_per_hour) +
                 " ГБ, Диапазон лет для дат=[" + std::to_string(start_year) + " - " + std::to_string(end_year) + "]");

    std::ofstream outFile(output_filename);
    if (!outFile.is_open()) {
        Logger::error(gen_log_prefix + "Не удалось открыть выходной файл для записи: '" + output_filename + "'");
        std::cerr << "Ошибка: Не удалось открыть файл для записи: " << output_filename << std::endl;
        return 1;
    }

    for (int i = 0; i < num_records; ++i) {
        try {
            std::string name = generate_random_name();
            IPAddress ip = generate_random_ip();
            Date date = generate_random_date(start_year, end_year);
            std::vector<double> traffic_in = generate_random_traffic(max_traffic_per_hour);
            std::vector<double> traffic_out = generate_random_traffic(max_traffic_per_hour);

            ProviderRecord record(name, ip, date, traffic_in, traffic_out);
            outFile << record; 
            if (i < num_records - 1) { 
                outFile << "\n";
            }
        } catch (const std::exception& e_gen_record) {
            Logger::error(gen_log_prefix + "Ошибка при генерации или записи записи #" + std::to_string(i+1) + ": " + e_gen_record.what());
        }
        if (outFile.bad()) { 
            Logger::error(gen_log_prefix + "Произошла ошибка ввода-вывода при записи в файл '" + output_filename + "' после записи #" + std::to_string(i+1) + ". Генерация прервана.");
            std::cerr << "Ошибка: Запись в файл " << output_filename << " прервана из-за ошибки IO." << std::endl;
            outFile.close(); 
            return 1;        
        }
    }

    outFile.close();
    if (!outFile.good()) { 
        Logger::error(gen_log_prefix + "Произошла ошибка при записи или корректном закрытии файла: '" + output_filename + "'. Файл может быть неполным или поврежден.");
        std::cerr << "Предупреждение: Запись в файл " << output_filename << " могла завершиться некорректно." << std::endl;
    } else {
         Logger::info(gen_log_prefix + "Успешно сгенерировано " + std::to_string(num_records) + " записей в файл '" + output_filename + "'");
         std::cout << "Успешно сгенерировано " << num_records << " записей в файл: " << output_filename << std::endl;
    }

    Logger::info(gen_log_prefix + "Генератор тестовых данных завершил работу.");
    return 0;
}
