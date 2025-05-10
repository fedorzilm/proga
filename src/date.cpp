#include "date.h"
#include <sstream>   
#include <iomanip>   
#include <charconv>  
#include <stdexcept> 

Date::Date(int y, int m, int d) : year(y), month(m), day(d) {}

bool Date::is_leap(int y) {
    if (y < 1) return false; 
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

int Date::days_in_month(int y, int m) {
    if (m < 1 || m > 12 || y < 1) return 0; 
    if (m == 2) return is_leap(y) ? 29 : 28;
    if (m == 4 || m == 6 || m == 9 || m == 11) return 30;
    return 31;
}

bool Date::is_valid() const {
    if (year < 1 || month < 1 || month > 12 ) return false;
    if (day < 1 || day > days_in_month(year, month)) return false;
    return true;
}

std::optional<Date> Date::from_string(const std::string& date_str) {
    if (date_str.length() != 10 || date_str[2] != '/' || date_str[5] != '/') {
        return std::nullopt;
    }

    int d = 0, m = 0, y = 0;

    auto parse_int_segment = [&](const char* start, const char* end, int& val) {
        auto result = std::from_chars(start, end, val);
        return result.ec == std::errc() && result.ptr == end;
    };

    if (!parse_int_segment(date_str.data(), date_str.data() + 2, d) ||
        !parse_int_segment(date_str.data() + 3, date_str.data() + 5, m) ||
        !parse_int_segment(date_str.data() + 6, date_str.data() + 10, y)) {
        return std::nullopt;
    }

    Date dt(y, m, d);
    if (dt.is_valid()) {
        return dt;
    } else {
        return std::nullopt;
    }
}

std::string Date::to_string() const {
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << day << "/"
        << std::setfill('0') << std::setw(2) << month << "/"
        << std::setfill('0') << std::setw(4) << year;
    return oss.str();
}

bool Date::operator<(const Date& other) const {
    if (year != other.year) return year < other.year;
    if (month != other.month) return month < other.month;
    return day < other.day;
}

bool Date::operator>(const Date& other) const {
    return other < *this;
}

bool Date::operator<=(const Date& other) const {
    return !(*this > other); 
}

bool Date::operator>=(const Date& other) const {
    return !(*this < other);
}

bool Date::operator==(const Date& other) const {
    return year == other.year && month == other.month && day == other.day;
}

bool Date::operator!=(const Date& other) const {
    return !(*this == other);
}

std::ostream& operator<<(std::ostream& os, const Date& dt) {
    os << dt.to_string();
    return os;
}

std::istream& operator>>(std::istream& is, Date& dt) {
    std::string date_str;
    if (is >> date_str) {
        auto parsed_date = Date::from_string(date_str);
        if (parsed_date) {
            dt = *parsed_date;
        } else {
            is.setstate(std::ios::failbit); 
        }
    }
    return is;
}
