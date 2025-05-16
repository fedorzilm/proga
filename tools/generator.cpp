// Предполагаемый путь: tools/generator.cpp
#include "common_defs.h"     // Предполагаемый путь: src/common_defs.h
#include "provider_record.h" // Предполагаемый путь: src/core/provider_record.h
#include "date.h"            // Предполагаемый путь: src/core/date.h
#include "ip_address.h"      // Предполагаемый путь: src/core/ip_address.h
#include "file_utils.h"      // Предполагаемый путь: src/utils/file_utils.h
#include "logger.h"          // Предполагаемый путь: src/utils/logger.h

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <random>
#include <algorithm> // Для std::generate_n
#include <stdexcept> // Для std::runtime_error

// --- Вспомогательные функции для генерации случайных данных ---

// Генератор случайных чисел (можно вынести в отдельную утилиту, если используется где-то еще)
std::mt19937& get_random_engine() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    return gen;
}

int random_int(int min_val, int max_val) {
    std::uniform_int_distribution<> distrib(min_val, max_val);
    return distrib(get_random_engine());
}

double random_double(double min_val, double max_val) {
    std::uniform_real_distribution<> distrib(min_val, max_val);
    return distrib(get_random_engine());
}

std::string generate_random_name() {
    static const std::vector<std::string> first_names = {"Иван", "Петр", "Сидор", "Алексей", "Дмитрий", "Сергей", "Андрей", "Михаил", "Владимир", "Артем"};
    static const std::vector<std::string> last_names = {"Иванов", "Петров", "Сидоров", "Кузнецов", "Смирнов", "Попов", "Волков", "Зайцев", "Белов", "Соколов"};
    static const std::vector<std::string> patronymics = {"Иванович", "Петрович", "Сидорович", "Алексеевич", "Дмитриевич", "Сергеевич", "Андреевич", "Михайлович", "Владимирович", "Артемович"};
    return last_names[static_cast<size_t>(random_int(0, static_cast<int>(last_names.size()) - 1))] + " " +
           first_names[static_cast<size_t>(random_int(0, static_cast<int>(first_names.size()) - 1))] + " " +
           patronymics[static_cast<size_t>(random_int(0, static_cast<int>(patronymics.size()) - 1))];
}

IPAddress generate_random_ip() {
    return IPAddress(random_int(1, 223), random_int(0, 255), random_int(0, 255), random_int(1, 254));
}

Date generate_random_date(int start_year = 2022, int end_year = 2024) {
    while (true) {
        try {
            int year = random_int(start_year, end_year);
            int month = random_int(1, 12);
            // Определение максимального дня для месяца (упрощенно, без учета високосных лет для простоты генератора)
            int day_max = 28; // Безопасный минимум
            if (month == 2) day_max = 28;
            else if (month == 4 || month == 6 || month == 9 || month == 11) day_max = 30;
            else day_max = 31;
            // Для более точного можно добавить Date::isLeap
            if (month == 2) {
                 bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
                 if(leap) day_max = 29;
            }

            int day = random_int(1, day_max);
            return Date(day, month, year);
        } catch (const std::invalid_argument& ) {
            // Повторить, если сгенерировалась невалидная дата (маловероятно с такой логикой)
        }
    }
}

std::vector<double> generate_random_traffic(double max_gb_per_hour = 10.0) {
    std::vector<double> traffic(HOURS_IN_DAY);
    for (int i = 0; i < HOURS_IN_DAY; ++i) {
        traffic[static_cast<size_t>(i)] = random_double(0.0, max_gb_per_hour);
    }
    return traffic;
}

// --- Основная функция генератора ---
int main(int argc, char* argv[]) {
    Logger::init(LogLevel::INFO, "generator.log"); // Логируем работу генератора
    std::string gen_log_prefix = "[Generator] ";
    Logger::info(gen_log_prefix + "Запуск генератора тестовых данных...");

    if (argc < 3) {
        // Используем Logger::error, но также выводим в cerr для немедленной видимости
        std::string usage_msg = "Использование: " + std::string(argv[0]) + " <количество_записей> <выходной_файл_данных> [макс_трафик_в_час_ГБ] [начальный_год] [конечный_год]";
        Logger::error(gen_log_prefix + "Недостаточно аргументов. " + usage_msg);
        std::cerr << usage_msg << std::endl;
        return 1;
    }

    int num_records = 0;
    try {
        num_records = std::stoi(argv[1]);
        if (num_records <= 0) {
            throw std::invalid_argument("Количество записей должно быть положительным.");
        }
    } catch (const std::exception& e) {
        Logger::error(gen_log_prefix + "Ошибка парсинга количества записей '" + std::string(argv[1]) + "': " + e.what());
        std::cerr << "Ошибка: Некорректное количество записей: " << argv[1] << ". " << e.what() << std::endl;
        return 1;
    }

    std::string output_filename = argv[2];
    double max_traffic_per_hour = 10.0; // ГБ
    int start_year = 2023;
    int end_year = 2024;

    if (argc > 3) {
        try { max_traffic_per_hour = std::stod(argv[3]); if(max_traffic_per_hour < 0) max_traffic_per_hour = 10.0;}
        catch (const std::exception&) { Logger::warn(gen_log_prefix + "Не удалось разобрать макс_трафик_в_час, используется значение по умолчанию: " + std::to_string(max_traffic_per_hour)); }
    }
    if (argc > 4) {
        try { start_year = std::stoi(argv[4]); }
        catch (const std::exception&) { Logger::warn(gen_log_prefix + "Не удалось разобрать начальный_год, используется значение по умолчанию: " + std::to_string(start_year)); }
    }
    if (argc > 5) {
        try { end_year = std::stoi(argv[5]); }
        catch (const std::exception&) { Logger::warn(gen_log_prefix + "Не удалось разобрать конечный_год, используется значение по умолчанию: " + std::to_string(end_year)); }
    }
    if (start_year > end_year) {
        Logger::warn(gen_log_prefix + "Начальный год (" + std::to_string(start_year) + ") больше конечного (" + std::to_string(end_year) + "). Используются значения по умолчанию.");
        start_year = 2023; end_year = 2024;
    }


    Logger::info(gen_log_prefix + "Генерация " + std::to_string(num_records) + " записей в файл: " + output_filename);
    Logger::info(gen_log_prefix + "Параметры: макс. трафик/час=" + std::to_string(max_traffic_per_hour) +
                 " ГБ, Годы=[" + std::to_string(start_year) + "-" + std::to_string(end_year) + "]");


    std::ofstream outFile(output_filename);
    if (!outFile.is_open()) {
        Logger::error(gen_log_prefix + "Не удалось открыть выходной файл: " + output_filename);
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
            if (i < num_records - 1) {
                outFile << "\n"; // Новая строка между записями, кроме последней
            }
        } catch (const std::exception& e) {
            Logger::error(gen_log_prefix + "Ошибка при генерации или записи записи #" + std::to_string(i) + ": " + e.what());
            // Продолжаем генерацию остальных записей
        }
    }

    outFile.close();
    if (!outFile.good()) { // Проверка после закрытия
        Logger::error(gen_log_prefix + "Произошла ошибка при записи или закрытии файла: " + output_filename);
        std::cerr << "Ошибка: Запись в файл " << output_filename << " могла завершиться некорректно." << std::endl;
        // Не возвращаем ошибку, т.к. файл мог быть частично записан
    } else {
         Logger::info(gen_log_prefix + "Успешно сгенерировано " + std::to_string(num_records) + " записей в файл " + output_filename);
         std::cout << "Успешно сгенерировано " << num_records << " записей в файл: " << output_filename << std::endl;
    }

    return 0;
}
