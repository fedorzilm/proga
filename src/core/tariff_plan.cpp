/*!
 * \file tariff_plan.cpp
 * \author Fedor Zilnitskiy
 * \brief Реализация класса TariffPlan для управления почасовыми интернет-тарифами.
 */
#include "tariff_plan.h" 
#include "logger.h"      
#include <fstream>       
#include <sstream>       
#include <vector>        
#include <string>        
#include <stdexcept>     
#include <iomanip>       // Для std::fixed, std::setprecision
#include <cmath>         // Для std::fabs

/*!
 * \brief Конструктор по умолчанию. Инициализирует тарифы нулями.
 */
TariffPlan::TariffPlan() noexcept
    : costInPerGBPerHour_(HOURS_IN_DAY, 0.0),
      costOutPerGBPerHour_(HOURS_IN_DAY, 0.0) {
}

/*!
 * \brief Загружает тарифный план из файла.
 * \param filename Путь к файлу тарифов.
 * \return true если загрузка успешна.
 * \throw std::runtime_error или std::invalid_argument при ошибках.
 */
bool TariffPlan::loadFromFile(const std::string& filename) {
    Logger::info("TariffPlan: Попытка загрузки тарифов из файла: " + filename);
    std::ifstream inFile(filename);
    if (!inFile.is_open()) {
        std::string error_msg = "Не удается открыть файл тарифов: '" + filename + "'.";
        Logger::error("TariffPlan Загрузка: " + error_msg);
        throw std::runtime_error(error_msg);
    }

    std::vector<double> tempInRates;
    tempInRates.reserve(HOURS_IN_DAY);
    std::vector<double> tempOutRates;
    tempOutRates.reserve(HOURS_IN_DAY);

    std::string line;
    int rates_count_total = 0;
    const int expected_total_rates = HOURS_IN_DAY * 2;
    int line_number = 0;

    while (std::getline(inFile, line)) {
        line_number++;
        size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }

        std::istringstream line_stream(line);
        double rate_val;
        std::string token_for_debug; 

        while(line_stream >> token_for_debug) { 
            try {
                size_t processed_chars = 0;
                rate_val = std::stod(token_for_debug, &processed_chars);

                if (processed_chars != token_for_debug.length()) {
                     std::string error_msg = "Файл тарифов '" + filename + "' содержит нечисловой токен или число с лишними символами: \"" + token_for_debug +
                                            "\" на строке ~" + std::to_string(line_number) + ".";
                    Logger::error("TariffPlan Загрузка: " + error_msg);
                    throw std::invalid_argument(error_msg);
                }
            } catch (const std::invalid_argument& e_stod) {
                 std::string error_msg = "Файл тарифов '" + filename + "' содержит неконвертируемый в число токен: \"" + token_for_debug +
                                        "\" на строке ~" + std::to_string(line_number) + ". Ошибка stod: " + e_stod.what();
                Logger::error("TariffPlan Загрузка: " + error_msg);
                throw std::invalid_argument(error_msg);
            } catch (const std::out_of_range& e_oor) {
                 std::string error_msg = "Файл тарифов '" + filename + "' содержит число вне диапазона double: \"" + token_for_debug +
                                        "\" на строке ~" + std::to_string(line_number) + ". Ошибка stod: " + e_oor.what();
                Logger::error("TariffPlan Загрузка: " + error_msg);
                throw std::invalid_argument(error_msg);
            }
            
            Logger::debug("TariffPlan: Проверка значения тарифа: " + std::to_string(rate_val) + " (из токена: '" + token_for_debug + "')");

            // ИСПРАВЛЕНИЕ: Проверка на строго отрицательные значения.
            if (rate_val < 0.0) { 
                std::ostringstream error_ss;
                error_ss << "Тарифная ставка не может быть отрицательной. Найдено: " << std::fixed << std::setprecision(10) << rate_val 
                         << " (из токена: '" << token_for_debug << "') в файле '" << filename << "' (строка ~" << line_number << ").";
                Logger::error("TariffPlan Загрузка: " + error_ss.str());
                throw std::invalid_argument(error_ss.str());
            }

            rates_count_total++;
            if (rates_count_total > expected_total_rates) {
                std::string error_msg = "Файл тарифов '" + filename + "' содержит более " +
                                        std::to_string(expected_total_rates) + " валидных тарифных ставок. Лишняя ставка: " + std::to_string(rate_val) + " (строка ~" + std::to_string(line_number) + ").";
                Logger::error("TariffPlan Загрузка: " + error_msg);
                throw std::invalid_argument(error_msg);
            }

            if (tempInRates.size() < static_cast<size_t>(HOURS_IN_DAY)) {
                tempInRates.push_back(rate_val);
            } else {
                tempOutRates.push_back(rate_val);
            }
        } 
        
        std::string remaining_junk_in_line;
        if (line_stream.clear(), (line_stream >> remaining_junk_in_line) && !remaining_junk_in_line.empty()) {
             std::string error_msg = "Файл тарифов '" + filename + "' содержит лишние нечисловые символы на строке ~" + std::to_string(line_number) +
                                    " после всех валидных числовых данных: \"" + remaining_junk_in_line + "\". Исходная строка (без комментария): \"" + line + "\"";
             Logger::error("TariffPlan Загрузка: " + error_msg);
             throw std::invalid_argument(error_msg);
        }
    } 

    if (inFile.bad()) {
        std::string error_msg = "Ошибка чтения из файла тарифов (IO error): " + filename;
        Logger::error("TariffPlan Загрузка: " + error_msg);
        throw std::runtime_error(error_msg);
    }
    
    if (rates_count_total != expected_total_rates) {
        std::ostringstream ss_err;
        ss_err << "Файл тарифов '" << filename << "' должен содержать ровно " << expected_total_rates
               << " валидных числовых ставок (" << HOURS_IN_DAY << " для входящего и " << HOURS_IN_DAY << " для исходящего). "
               << "Найдено валидных ставок: " << rates_count_total << ".";
        Logger::error("TariffPlan Загрузка: " + ss_err.str());
        throw std::invalid_argument(ss_err.str());
    }
    
    if (tempInRates.size() != static_cast<size_t>(HOURS_IN_DAY) || tempOutRates.size() != static_cast<size_t>(HOURS_IN_DAY)) {
         std::ostringstream ss_err;
         ss_err << "Внутренняя ошибка парсинга тарифов из файла '" << filename << "'. Неверное количество ставок в итоговых векторах. "
                << "Входящих: " << tempInRates.size() << ", Исходящих: " << tempOutRates.size() << ".";
        Logger::error("TariffPlan Загрузка: " + ss_err.str());
        throw std::logic_error(ss_err.str()); 
    }

    costInPerGBPerHour_ = tempInRates;
    costOutPerGBPerHour_ = tempOutRates;

    Logger::info("TariffPlan: Тарифы успешно загружены из файла '" + filename + "'. " +
                 "Загружено " + std::to_string(costInPerGBPerHour_.size()) + " входящих и " +
                 std::to_string(costOutPerGBPerHour_.size()) + " исходящих тарифных ставок.");
    return true;
}

double TariffPlan::getCostInForHour(int hour) const {
    if (hour < 0 || hour >= HOURS_IN_DAY) {
        std::ostringstream ss;
        ss << "Некорректный час для получения входящего тарифа: " << hour 
           << ". Час должен быть в диапазоне от 0 до " << (HOURS_IN_DAY - 1) << ".";
        throw std::out_of_range(ss.str());
    }
    if (costInPerGBPerHour_.empty() || static_cast<size_t>(hour) >= costInPerGBPerHour_.size()) {
        throw std::logic_error("TariffPlan: Входящие тарифы не загружены или некорректны.");
    }
    return costInPerGBPerHour_.at(static_cast<std::vector<double>::size_type>(hour));
}

double TariffPlan::getCostOutForHour(int hour) const {
    if (hour < 0 || hour >= HOURS_IN_DAY) {
        std::ostringstream ss;
        ss << "Некорректный час для получения исходящего тарифа: " << hour 
           << ". Час должен быть в диапазоне от 0 до " << (HOURS_IN_DAY - 1) << ".";
        throw std::out_of_range(ss.str());
    }
    if (costOutPerGBPerHour_.empty() || static_cast<size_t>(hour) >= costOutPerGBPerHour_.size()) {
        throw std::logic_error("TariffPlan: Исходящие тарифы не загружены или некорректны.");
    }
    return costOutPerGBPerHour_.at(static_cast<std::vector<double>::size_type>(hour));
}
