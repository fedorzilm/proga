#include "tariff_plan.h"
#include "common_defs.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <locale> // Для std::locale

TariffPlan::TariffPlan() : hourly_rates(HOURS_IN_DAY, 0.0) {}

bool TariffPlan::load_from_file(const std::string& filename) {
    if (!is_valid_cmd_argument_path(filename)) {
        std::cerr << get_current_timestamp() << " [TariffPlan] Error: Invalid filepath for tariff plan: " << filename << std::endl;
        return false;
    }
    std::ifstream ifs(filename);
    if (!ifs) {
        std::cerr << get_current_timestamp() << " [TariffPlan] Error: Could not open tariff file: " << filename << std::endl;
        return false;
    }

    std::vector<double> temp_rates;
    temp_rates.reserve(HOURS_IN_DAY);
    std::string line;
    int line_num = 0;

    std::locale c_locale("C"); // Локаль для корректного парсинга double с точкой

    while (std::getline(ifs, line)) {
        line_num++;
        std::string original_line_for_log = line;
        
        size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }
        line = trim_string(line);

        if (line.empty()) {
            continue;
        }

        std::stringstream ss_line(line);
        ss_line.imbue(c_locale); // Применяем локаль к stringstream
        double rate_value;

        // Пытаемся прочитать число. Если не удалось, или если после числа что-то осталось
        if (!(ss_line >> rate_value) || !(ss_line >> std::ws).eof()) { // (ss_line >> std::ws).eof() проверяет, что после числа и пробелов ничего нет
            std::cerr << get_current_timestamp() << " [TariffPlan] Error: Invalid format or trailing characters in tariff file '" << filename
                      << "' at line " << line_num << ". Processed line content: '" << line << "'" << std::endl;
            return false;
        }
        
        if (rate_value < 0.0) {
            std::cerr << get_current_timestamp() << " [TariffPlan] Error: Invalid negative rate (" << rate_value << ") found in '"
                      << filename << "' at line " << line_num << ". Original line content: '" << original_line_for_log << "'" << std::endl;
            return false;
        }
        temp_rates.push_back(rate_value);
    }

    if (temp_rates.size() != HOURS_IN_DAY) {
        std::cerr << get_current_timestamp() << " [TariffPlan] Error: Tariff file '" << filename << "' must contain exactly "
                  << HOURS_IN_DAY << " valid rates. Found " << temp_rates.size() << "." << std::endl;
        return false;
    }

    hourly_rates = temp_rates;
    std::cout << get_current_timestamp() << " [TariffPlan] Info: Tariff plan loaded successfully from '" << filename << "'." << std::endl;
    return true;
}

void TariffPlan::print(std::ostream& os) const {
    os << "Hourly Tariff Rates (Cost per unit of total traffic):\n";
    os << std::fixed << std::setprecision(4);
    for (size_t i = 0; i < hourly_rates.size(); ++i) {
        os << "Hour " << std::setfill('0') << std::setw(2) << i << ": " << hourly_rates[i] << "\n";
    }
    os << std::defaultfloat << std::setprecision(6);
}

double TariffPlan::get_rate(int hour) const {
    if (hour >= 0 && static_cast<size_t>(hour) < hourly_rates.size()) {
        return hourly_rates[static_cast<size_t>(hour)];
    }
    return 0.0;
}

