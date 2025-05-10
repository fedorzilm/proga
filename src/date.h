#ifndef DATE_H
#define DATE_H

#include <iostream>
#include <string>
#include <optional>
#include <iomanip> 

class Date {
public:
    int year = 0;
    int month = 0;
    int day = 0;

    Date(int y = 0, int m = 0, int d = 0);

    bool is_valid() const;
    static std::optional<Date> from_string(const std::string& date_str);
    std::string to_string() const;

    bool operator<(const Date& other) const;
    bool operator>(const Date& other) const;
    bool operator<=(const Date& other) const;
    bool operator>=(const Date& other) const;
    bool operator==(const Date& other) const;
    bool operator!=(const Date& other) const;

    static int days_in_month(int year, int month);
    static bool is_leap(int year);
};

std::ostream& operator<<(std::ostream& os, const Date& dt);
std::istream& operator>>(std::istream& is, Date& dt);

#endif // DATE_H
