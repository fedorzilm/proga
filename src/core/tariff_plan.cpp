// Предполагаемый путь: src/core/tariff_plan.cpp
#include "tariff_plan.h" // Предполагаемый путь: src/core/tariff_plan.h
#include "logger.h"      // Предполагаемый путь: src/utils/logger.h
#include <fstream>       // Для std::ifstream
#include <sstream>       // Для std::istringstream, std::ostringstream
#include <vector>        // Для std::vector
#include <string>        // Для std::string, std::stod, std::to_string
#include <stdexcept>     // Для исключений
#include <iomanip>       // Для std::fixed, std::setprecision (если нужно для отладки)

TariffPlan::TariffPlan() noexcept
    : costInPerGBPerHour_(HOURS_IN_DAY, 0.0),
      costOutPerGBPerHour_(HOURS_IN_DAY, 0.0) {
    // Logger::debug("TariffPlan: Default constructor called, tariffs initialized to 0.0.");
}

bool TariffPlan::loadFromFile(const std::string& filename) {
    Logger::info("TariffPlan: Attempting to load tariffs from file: " + filename);
    std::ifstream inFile(filename);
    if (!inFile.is_open()) {
        std::string error_msg = "Не удается открыть файл тарифов: " + filename;
        Logger::error("TariffPlan::loadFromFile: " + error_msg);
        throw std::runtime_error(error_msg);
    }

    std::vector<double> tempInRates;
    tempInRates.reserve(HOURS_IN_DAY);
    std::vector<double> tempOutRates;
    tempOutRates.reserve(HOURS_IN_DAY);

    std::string line;
    int rates_count_total = 0;
    const int expected_total_rates = HOURS_IN_DAY * 2;

    while (std::getline(inFile, line)) {
        // 1. Удаление комментариев
        size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }

        // 2. Удаление начальных и конечных пробелов
        line.erase(0, line.find_first_not_of(" \t\n\r\f\v"));
        line.erase(line.find_last_not_of(" \t\n\r\f\v") + 1);

        if (line.empty()) { // Пропускаем пустые строки или строки, ставшие пустыми после удаления комментария/пробелов
            continue;
        }

        // На этом этапе строка line содержит только число (и, возможно, лишние символы после него, что будет ошибкой)
        double rate;
        size_t processed_chars = 0;
        try {
            rate = std::stod(line, &processed_chars);
            // Проверяем, что вся непустая строка была числом
            if (processed_chars != line.length()) {
                 std::string error_msg = "Файл тарифов '" + filename + "' содержит лишние символы на строке после числа: '" + line + "'";
                 Logger::error("TariffPlan::loadFromFile: " + error_msg);
                 throw std::invalid_argument(error_msg);
            }
        } catch (const std::invalid_argument& e_stod) { // stod не смог распарсить
            std::string error_msg = "Файл тарифов '" + filename + "' содержит нечисловые данные: '" + line + "'. Ошибка: " + e_stod.what();
            Logger::error("TariffPlan::loadFromFile: " + error_msg);
            throw std::invalid_argument(error_msg);
        } catch (const std::out_of_range& e_oor) { // stod распарсил, но число слишком большое/маленькое для double
            std::string error_msg = "Файл тарифов '" + filename + "' содержит число вне диапазона double: '" + line + "'. Ошибка: " + e_oor.what();
            Logger::error("TariffPlan::loadFromFile: " + error_msg);
            throw std::invalid_argument(error_msg);
        }


        if (rate < 0.0) {
            std::string error_msg = "Тарифная ставка не может быть отрицательной. Найдено: " +
                                    std::to_string(rate) + " в файле '" + filename + "' на строке: '" + line + "'";
            Logger::error("TariffPlan::loadFromFile: " + error_msg);
            throw std::invalid_argument(error_msg);
        }

        rates_count_total++;
        if (rates_count_total > expected_total_rates) {
             std::string error_msg = "Файл тарифов '" + filename + "' содержит более " +
                                    std::to_string(expected_total_rates) + " валидных тарифных ставок. Текущая ставка: " + std::to_string(rate);
            Logger::error("TariffPlan::loadFromFile: " + error_msg);
            throw std::invalid_argument(error_msg);
        }

        if (tempInRates.size() < static_cast<size_t>(HOURS_IN_DAY)) {
            tempInRates.push_back(rate);
        } else {
            tempOutRates.push_back(rate);
        }
    }

    // Проверка на ошибки чтения файла, не связанные с EOF (например, IO error)
    if (inFile.bad()) {
        std::string error_msg = "Ошибка чтения из файла тарифов: " + filename;
        Logger::error("TariffPlan::loadFromFile: " + error_msg);
        throw std::runtime_error(error_msg);
    }
    // inFile.eof() будет true, если достигнут конец файла, это нормально.
    // inFile.fail() может быть true, если последняя операция getline не удалась (например, из-за eof без новой строки),
    // но если мы уже прочитали все нужные данные, это не проблема.

    if (rates_count_total != expected_total_rates) { // Проверяем общее количество успешно прочитанных валидных чисел
        std::ostringstream ss_err;
        ss_err << "Файл тарифов '" << filename << "' должен содержать ровно " << expected_total_rates
               << " валидных числовых ставок (" << HOURS_IN_DAY << " для входящего и " << HOURS_IN_DAY << " для исходящего). "
               << "Найдено валидных ставок: " << rates_count_total << ". "
               << "(Разобрано для входящих: " << tempInRates.size() << ", для исходящих: " << tempOutRates.size() << ").";
        Logger::error("TariffPlan::loadFromFile: " + ss_err.str());
        throw std::invalid_argument(ss_err.str());
    }
    // Дополнительная проверка, что векторы заполнились правильно (хотя предыдущая проверка должна это покрывать)
    if (tempInRates.size() != static_cast<size_t>(HOURS_IN_DAY) || tempOutRates.size() != static_cast<size_t>(HOURS_IN_DAY)) {
         std::ostringstream ss_err;
         ss_err << "Внутренняя ошибка парсинга тарифов из файла '" << filename << "'. Неверное количество ставок в векторах. "
                << "Входящих: " << tempInRates.size() << ", Исходящих: " << tempOutRates.size() << ".";
        Logger::error("TariffPlan::loadFromFile: " + ss_err.str());
        throw std::logic_error(ss_err.str()); // logic_error, т.к. это проблема логики парсера
    }


    costInPerGBPerHour_ = tempInRates;
    costOutPerGBPerHour_ = tempOutRates;

    Logger::info("TariffPlan: Тарифы успешно загружены из " + filename + ". " +
                 std::to_string(costInPerGBPerHour_.size()) + " входящих, " +
                 std::to_string(costOutPerGBPerHour_.size()) + " исходящих ставок.");
    return true;
}

double TariffPlan::getCostInForHour(int hour) const {
    if (hour < 0 || hour >= HOURS_IN_DAY) {
        std::ostringstream ss;
        ss << "Некорректный час для входящего тарифа: " << hour << ". Час должен быть в диапазоне от 0 до " << (HOURS_IN_DAY - 1) << ".";
        // Logger::warn("TariffPlan::getCostInForHour: " + ss.str()); // Не всегда нужно логировать out_of_range
        throw std::out_of_range(ss.str());
    }
    // Проверка на то, что тарифы были загружены (векторы не пусты)
    if (costInPerGBPerHour_.empty()) {
         std::string error_msg = "Попытка получить входящий тариф, но тарифы не были загружены или пусты.";
         Logger::error("TariffPlan::getCostInForHour: " + error_msg);
         throw std::logic_error(error_msg); // logic_error, т.к. это неправильное состояние объекта
    }
    return costInPerGBPerHour_.at(static_cast<std::vector<double>::size_type>(hour));
}

double TariffPlan::getCostOutForHour(int hour) const {
    if (hour < 0 || hour >= HOURS_IN_DAY) {
        std::ostringstream ss;
        ss << "Некорректный час для исходящего тарифа: " << hour << ". Час должен быть в диапазоне от 0 до " << (HOURS_IN_DAY - 1) << ".";
        throw std::out_of_range(ss.str());
    }
    if (costOutPerGBPerHour_.empty()) {
         std::string error_msg = "Попытка получить исходящий тариф, но тарифы не были загружены или пусты.";
         Logger::error("TariffPlan::getCostOutForHour: " + error_msg);
         throw std::logic_error(error_msg);
    }
    return costOutPerGBPerHour_.at(static_cast<std::vector<double>::size_type>(hour));
}
