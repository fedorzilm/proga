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
 * \brief Генерирует случайное число с плавающей запятой (double) в заданном диапазоне [min_val, max_val).
 * \param min_val Минимальное возможное значение (включительно).
 * \param max_val Максимальное возможное значение (исключая, если используется стандартный uniform_real_distribution).
 * Для включения max_val, можно использовать `std::nextafter(max_val, std::numeric_limits<double>::infinity())`
 * или более сложную логику, но для генерации трафика это обычно не критично.
 * \return Случайное число double.
 */
double random_double(double min_val, double max_val) {
    if (min_val > max_val) {
        std::swap(min_val, max_val);
    }
    // uniform_real_distribution генерирует в [min, max)
    // Если нужно [min, max], можно немного скорректировать max_val или использовать другую технику.
    // Для простоты оставим стандартное поведение.
    std::uniform_real_distribution<> distrib(min_val, max_val); 
    return distrib(get_random_engine());
}

/*!
 * \brief Генерирует случайное полное имя (Фамилия Имя Отчество) из предопределенных списков.
 * \return Строка со случайным ФИО.
 */
std::string generate_random_name() {
    static const std::vector<std::string> first_names = {
        "Иван", "Петр", "Сидор", "Алексей", "Дмитрий", "Сергей", "Андрей", "Михаил", "Владимир", "Артем",
        "Егор", "Максим", "Никита", "Олег", "Павел", "Роман", "Степан", "Тимур", "Федор", "Юрий",
        "Анна", "Мария", "Елена", "Ольга", "Светлана", "Татьяна", "Наталья", "Ирина", "Виктория", "Екатерина"
    };
    static const std::vector<std::string> last_names = {
        "Иванов", "Петров", "Сидоров", "Кузнецов", "Смирнов", "Попов", "Волков", "Зайцев", "Белов", "Соколов",
        "Михайлов", "Новиков", "Федоров", "Морозов", "Васильев", "Орлов", "Егоров", "Козлов", "Степанов", "Николаев"
    };
    // Для женских имен можно добавить "-а" к фамилиям, но для простоты генератора это опущено.
    static const std::vector<std::string> patronymics = {
        "Иванович", "Петрович", "Сидорович", "Алексеевич", "Дмитриевич", "Сергеевич", "Андреевич", "Михайлович", "Владимирович", "Артемович",
        "Егорович", "Максимович", "Никитич", "Олегович", "Павлович", "Романович", "Степанович", "Тимурович", "Федорович", "Юрьевич"
    };
    // Можно добавить женские отчества (Ивановна, Петровна и т.д.) и выбирать в зависимости от имени, но для простоты...
    
    return last_names[static_cast<size_t>(random_int(0, static_cast<int>(last_names.size()) - 1))] + " " +
           first_names[static_cast<size_t>(random_int(0, static_cast<int>(first_names.size()) - 1))] + " " +
           patronymics[static_cast<size_t>(random_int(0, static_cast<int>(patronymics.size()) - 1))];
}

/*!
 * \brief Генерирует случайный IPv4-адрес.
 * Первый октет генерируется в диапазоне [1, 223] (классы A, B, C, исключая широковещательные и специальные).
 * Остальные октеты [0, 255], последний хостовый октет [1, 254] (исключая адрес сети и широковещательный адрес подсети).
 * \return Объект `IPAddress` со случайным значением.
 */
IPAddress generate_random_ip() {
    // Генерируем IP-адреса из "обычных" диапазонов
    return IPAddress(random_int(1, 223),    // Первый октет (исключая 0.x.x.x, 127.x.x.x (частично), 224+ x.x.x)
                     random_int(0, 255),    // Второй октет
                     random_int(0, 255),    // Третий октет
                     random_int(1, 254));   // Четвертый октет (исключая .0 и .255 для хостов)
}

/*!
 * \brief Генерирует случайную дату в заданном диапазоне лет.
 * Учитывает корректное количество дней в месяце, включая високосные годы.
 * \param start_year Начальный год диапазона (включительно).
 * \param end_year Конечный год диапазона (включительно).
 * \return Объект `Date` со случайным значением.
 * \throw std::runtime_error если Date не может быть создан (маловероятно с текущей логикой).
 */
Date generate_random_date(int start_year = 2022, int end_year = 2024) {
    if (start_year > end_year) std::swap(start_year, end_year);
    // Цикл на случай, если Date конструктор выбросит исключение (хотя наша логика генерации дня должна это предотвращать)
    while (true) { 
        try {
            int year = random_int(start_year, end_year);
            int month = random_int(1, 12);
            
            int day_max = 28; // Безопасный минимум для февраля невисокосного года
            if (month == 2) {
                 bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
                 day_max = leap ? 29 : 28;
            } else if (month == 4 || month == 6 || month == 9 || month == 11) {
                day_max = 30;
            } else {
                day_max = 31;
            }
            int day = random_int(1, day_max);
            return Date(day, month, year); // Конструктор Date выполнит финальную валидацию
        } catch (const std::invalid_argument& e_date) {
            // Эта ветка маловероятна, если логика day_max верна, но для надежности.
            Logger::warn("[Generator] Исключение при генерации случайной даты (повторная попытка): " + std::string(e_date.what()));
            // Продолжаем цикл для повторной генерации
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
    if (max_gb_per_hour < 0.0) max_gb_per_hour = 0.0; // Трафик не может быть отрицательным
    std::vector<double> traffic(HOURS_IN_DAY);
    for (int i = 0; i < HOURS_IN_DAY; ++i) {
        // Генерируем трафик с некоторой вероятностью нуля (например, ночью)
        // и с разной интенсивностью
        double val = random_double(0.0, max_gb_per_hour);
        // Можно добавить логику для более реалистичного распределения (например, пики днем)
        if (random_int(0,3) == 0 && (i < 6 || i > 22)) { // Чаще 0 ночью
            val = 0.0;
        } else if (random_int(0, 10) == 0) { // Иногда 0 и днем
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
    // Инициализация логгера для генератора
    Logger::init(LogLevel::INFO, DEFAULT_GENERATOR_LOG_FILE); 
    const std::string gen_log_prefix = "[Generator] ";
    Logger::info(gen_log_prefix + "Запуск генератора тестовых данных для базы интернет-провайдера...");

    if (argc < 3) {
        std::string usage_msg = "Использование: " + std::string(argv[0]) + 
                                " <количество_записей> <выходной_файл_данных> "
                                "[макс_трафик_в_час_ГБ (по умолч: 10.0)] "
                                "[начальный_год (по умолч: 2022)] "
                                "[конечный_год (по умолч: " + std::to_string(std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now())}.year()) + ")]";
        Logger::error(gen_log_prefix + "Недостаточно аргументов. " + usage_msg);
        std::cerr << usage_msg << std::endl;
        return 1;
    }

    int num_records = 0;
    try {
        num_records = std::stoi(argv[1]);
        if (num_records <= 0) {
            throw std::invalid_argument("Количество записей должно быть положительным числом.");
        }
    } catch (const std::exception& e_stoi_num) {
        Logger::error(gen_log_prefix + "Ошибка парсинга количества записей '" + std::string(argv[1]) + "': " + e_stoi_num.what());
        std::cerr << "Ошибка: Некорректное количество записей: " << argv[1] << ". " << e_stoi_num.what() << std::endl;
        return 1;
    }

    std::string output_filename = argv[2];
    if (output_filename.empty()) {
        Logger::error(gen_log_prefix + "Имя выходного файла не может быть пустым.");
        std::cerr << "Ошибка: Имя выходного файла не указано." << std::endl;
        return 1;
    }

    // Параметры по умолчанию
    double max_traffic_per_hour = 10.0; // ГБ
    int start_year_default = 2022;
    // Конечный год по умолчанию - текущий год
    auto current_time = std::chrono::system_clock::now();
    std::chrono::year current_year_obj = std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(current_time)}.year();
    int end_year_default = static_cast<int>(current_year_obj);
    if (end_year_default < start_year_default) end_year_default = start_year_default; // На случай если текущий год < 2022

    int start_year = start_year_default;
    int end_year = end_year_default;

    if (argc > 3) {
        try { 
            max_traffic_per_hour = std::stod(argv[3]); 
            if(max_traffic_per_hour < 0.0) {
                Logger::warn(gen_log_prefix + "Максимальный трафик в час не может быть отрицательным (" + argv[3] + "). Используется значение по умолчанию: " + std::to_string(10.0));
                max_traffic_per_hour = 10.0;
            }
        }
        catch (const std::exception& e_stod_traffic) { 
            Logger::warn(gen_log_prefix + "Не удалось разобрать макс_трафик_в_час ('" + argv[3] + "'): " + e_stod_traffic.what() + ". Используется значение по умолчанию: " + std::to_string(max_traffic_per_hour));
        }
    }
    if (argc > 4) {
        try { start_year = std::stoi(argv[4]); }
        catch (const std::exception& e_stoi_start_year) { 
            Logger::warn(gen_log_prefix + "Не удалось разобрать начальный_год ('" + argv[4] + "'): " + e_stoi_start_year.what() + ". Используется значение по умолчанию: " + std::to_string(start_year));
        }
    }
    if (argc > 5) {
        try { end_year = std::stoi(argv[5]); }
        catch (const std::exception& e_stoi_end_year) { 
            Logger::warn(gen_log_prefix + "Не удалось разобрать конечный_год ('" + argv[5] + "'): " + e_stoi_end_year.what() + ". Используется значение по умолчанию: " + std::to_string(end_year));
        }
    }

    if (start_year > end_year) {
        Logger::warn(gen_log_prefix + "Начальный год (" + std::to_string(start_year) + ") больше конечного (" + std::to_string(end_year) + "). Меняем их местами.");
        std::swap(start_year, end_year);
    }
    // Проверка на разумность диапазона лет (например, не слишком далеко в прошлое/будущее относительно ограничений Date)
    const int DATE_MIN_YEAR = 1900;
    const int DATE_MAX_YEAR = 2100;
    if (start_year < DATE_MIN_YEAR) { Logger::warn(gen_log_prefix + "Начальный год " + std::to_string(start_year) + " меньше минимально допустимого " + std::to_string(DATE_MIN_YEAR) + ". Установлен в " + std::to_string(DATE_MIN_YEAR)); start_year = DATE_MIN_YEAR; }
    if (end_year > DATE_MAX_YEAR)   { Logger::warn(gen_log_prefix + "Конечный год " + std::to_string(end_year) + " больше максимально допустимого " + std::to_string(DATE_MAX_YEAR) + ". Установлен в " + std::to_string(DATE_MAX_YEAR));   end_year = DATE_MAX_YEAR; }
    if (start_year > end_year) { // Если после коррекции start_year стал больше end_year
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
            outFile << record; // Используем operator<< для ProviderRecord
            if (i < num_records - 1) { // Добавляем новую строку между записями, кроме последней
                outFile << "\n"; 
            }
        } catch (const std::exception& e_gen_record) {
            // Ошибки от конструкторов Date, IPAddress, ProviderRecord (хотя они маловероятны с такой логикой генерации)
            Logger::error(gen_log_prefix + "Ошибка при генерации или записи записи #" + std::to_string(i+1) + ": " + e_gen_record.what());
            // Можно решить, продолжать ли генерацию или прервать. Пока продолжаем.
        }
        if (outFile.bad()) { // Проверка после каждой записи на случай ошибки IO
            Logger::error(gen_log_prefix + "Произошла ошибка ввода-вывода при записи в файл '" + output_filename + "' после записи #" + std::to_string(i+1) + ". Генерация прервана.");
            std::cerr << "Ошибка: Запись в файл " << output_filename << " прервана из-за ошибки IO." << std::endl;
            outFile.close();
            return 1;
        }
    }

    outFile.close();
    // Проверка состояния потока после закрытия
    if (!outFile.good()) { // good() проверяет !bad() && !fail()
        Logger::error(gen_log_prefix + "Произошла ошибка при записи или корректном закрытии файла: '" + output_filename + "'. Файл может быть неполным или поврежден.");
        std::cerr << "Предупреждение: Запись в файл " << output_filename << " могла завершиться некорректно." << std::endl;
        // Не возвращаем код ошибки, так как файл мог быть частично записан и это может быть приемлемо.
    } else {
         Logger::info(gen_log_prefix + "Успешно сгенерировано " + std::to_string(num_records) + " записей в файл '" + output_filename + "'");
         std::cout << "Успешно сгенерировано " << num_records << " записей в файл: " << output_filename << std::endl;
    }

    Logger::info(gen_log_prefix + "Генератор тестовых данных завершил работу.");
    return 0;
}
