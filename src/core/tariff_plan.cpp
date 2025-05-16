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
#include <iomanip>       

/*!
 * \brief Конструктор по умолчанию. Инициализирует тарифы нулями.
 */
TariffPlan::TariffPlan() noexcept
    : costInPerGBPerHour_(HOURS_IN_DAY, 0.0),
      costOutPerGBPerHour_(HOURS_IN_DAY, 0.0) {
    // Logger::debug("TariffPlan: Default constructor. Tariffs initialized to 0.0 for all hours.");
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
        Logger::error("TariffPlan Load: " + error_msg);
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
        // 1. Удаление комментариев (от символа '#' до конца строки)
        size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }

        std::istringstream line_stream(line);
        double rate_val;
        // Читаем все числа из строки
        while(line_stream >> rate_val) {
            if (rate_val < -DOUBLE_EPSILON) { // Сравнение с небольшим отрицательным эпсилон, чтобы считать 0 валидным
                std::ostringstream error_ss;
                error_ss << "Тарифная ставка не может быть отрицательной. Найдено: " << std::fixed << std::setprecision(2) << rate_val 
                         << " в файле '" << filename << "' (строка ~" << line_number << ").";
                Logger::error("TariffPlan Load: " + error_ss.str());
                throw std::invalid_argument(error_ss.str());
            }

            rates_count_total++;
            if (rates_count_total > expected_total_rates) {
                std::string error_msg = "Файл тарифов '" + filename + "' содержит более " +
                                        std::to_string(expected_total_rates) + " валидных тарифных ставок. Лишняя ставка: " + std::to_string(rate_val) + " (строка ~" + std::to_string(line_number) + ").";
                Logger::error("TariffPlan Load: " + error_msg);
                throw std::invalid_argument(error_msg);
            }

            if (tempInRates.size() < static_cast<size_t>(HOURS_IN_DAY)) {
                tempInRates.push_back(rate_val);
            } else {
                tempOutRates.push_back(rate_val);
            }
        } // конец while(line_stream >> rate_val)

        // Проверяем, остались ли в строке нечисловые данные после чтения всех чисел
        if (!line_stream.eof()) { // Если не достигнут конец строки (после всех чисел)
            std::string remaining_junk;
            line_stream.clear(); // Сбрасываем флаги ошибок, чтобы можно было прочитать остаток
            if (line_stream >> remaining_junk && !remaining_junk.empty()) {
                 std::string error_msg = "Файл тарифов '" + filename + "' содержит лишние нечисловые символы на строке ~" + std::to_string(line_number) +
                                        " после числовых данных: \"" + remaining_junk + "\". Исходная строка (без комментария): \"" + line + "\"";
                 Logger::error("TariffPlan Load: " + error_msg);
                 throw std::invalid_argument(error_msg);
            }
        }
    } // конец while (std::getline(inFile, line))

    // Проверка на ошибки чтения файла, не связанные с EOF
    if (inFile.bad()) {
        std::string error_msg = "Ошибка чтения из файла тарифов (IO error): " + filename;
        Logger::error("TariffPlan Load: " + error_msg);
        throw std::runtime_error(error_msg);
    }
    
    if (rates_count_total != expected_total_rates) {
        std::ostringstream ss_err;
        ss_err << "Файл тарифов '" << filename << "' должен содержать ровно " << expected_total_rates
               << " валидных числовых ставок (" << HOURS_IN_DAY << " для входящего и " << HOURS_IN_DAY << " для исходящего). "
               << "Найдено валидных ставок: " << rates_count_total << ".";
        Logger::error("TariffPlan Load: " + ss_err.str());
        throw std::invalid_argument(ss_err.str());
    }
    
    // Дополнительная проверка размеров векторов (хотя предыдущая проверка должна это покрывать)
    if (tempInRates.size() != static_cast<size_t>(HOURS_IN_DAY) || tempOutRates.size() != static_cast<size_t>(HOURS_IN_DAY)) {
         std::ostringstream ss_err;
         ss_err << "Внутренняя ошибка парсинга тарифов из файла '" << filename << "'. Неверное количество ставок в итоговых векторах. "
                << "Входящих: " << tempInRates.size() << ", Исходящих: " << tempOutRates.size() << ".";
        Logger::error("TariffPlan Load: " + ss_err.str());
        throw std::logic_error(ss_err.str()); // logic_error, т.к. это проблема логики парсера
    }

    costInPerGBPerHour_ = tempInRates;
    costOutPerGBPerHour_ = tempOutRates;

    Logger::info("TariffPlan: Тарифы успешно загружены из файла '" + filename + "'. " +
                 "Загружено " + std::to_string(costInPerGBPerHour_.size()) + " входящих и " +
                 std::to_string(costOutPerGBPerHour_.size()) + " исходящих тарифных ставок.");
    return true;
}

/*!
 * \brief Получает стоимость входящего трафика для указанного часа.
 * \param hour Час (0-23).
 * \return Стоимость.
 * \throw std::out_of_range Если час невалиден.
 * \throw std::logic_error Если тарифы не загружены.
 */
double TariffPlan::getCostInForHour(int hour) const {
    if (hour < 0 || hour >= HOURS_IN_DAY) {
        std::ostringstream ss;
        ss << "Некорректный час для получения входящего тарифа: " << hour 
           << ". Час должен быть в диапазоне от 0 до " << (HOURS_IN_DAY - 1) << ".";
        throw std::out_of_range(ss.str());
    }
    // costInPerGBPerHour_ всегда инициализирован (размером HOURS_IN_DAY),
    // либо нулями (по умолчанию), либо значениями из файла.
    // Проверка на empty() избыточна, если конструктор и loadFromFile работают корректно.
    // if (costInPerGBPerHour_.empty()) { // Эта ситуация не должна возникать
    //      std::string error_msg = "Попытка получить входящий тариф, но внутренний вектор тарифов пуст (ошибка инициализации).";
    //      Logger::error("TariffPlan GetCostIn: " + error_msg);
    //      throw std::logic_error(error_msg); 
    // }
    return costInPerGBPerHour_.at(static_cast<std::vector<double>::size_type>(hour));
}

/*!
 * \brief Получает стоимость исходящего трафика для указанного часа.
 * \param hour Час (0-23).
 * \return Стоимость.
 * \throw std::out_of_range Если час невалиден.
 * \throw std::logic_error Если тарифы не загружены.
 */
double TariffPlan::getCostOutForHour(int hour) const {
    if (hour < 0 || hour >= HOURS_IN_DAY) {
        std::ostringstream ss;
        ss << "Некорректный час для получения исходящего тарифа: " << hour 
           << ". Час должен быть в диапазоне от 0 до " << (HOURS_IN_DAY - 1) << ".";
        throw std::out_of_range(ss.str());
    }
    // Аналогично getCostInForHour, costOutPerGBPerHour_ всегда должен быть корректно инициализирован.
    // if (costOutPerGBPerHour_.empty()) {
    //      std::string error_msg = "Попытка получить исходящий тариф, но внутренний вектор тарифов пуст (ошибка инициализации).";
    //      Logger::error("TariffPlan GetCostOut: " + error_msg);
    //      throw std::logic_error(error_msg);
    // }
    return costOutPerGBPerHour_.at(static_cast<std::vector<double>::size_type>(hour));
}
