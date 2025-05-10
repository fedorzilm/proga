#ifndef TARIFF_PLAN_H
#define TARIFF_PLAN_H

#include "common_defs.h" 
#include <vector>
#include <string>
#include <iostream>
#include <iomanip> 

class TariffPlan {
public:
    std::vector<double> hourly_rates;

    TariffPlan();

    bool load_from_file(const std::string& filename);
    void print(std::ostream& os = std::cout) const;
    double get_rate(int hour) const; 
};

#endif // TARIFF_PLAN_H
