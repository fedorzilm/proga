// Предполагаемый путь: src/core/date.cpp
#include "date.h" // Предполагаемый путь: src/core/date.h
#include "logger.h" // Предполагаемый путь: src/utils/logger.h
#include <iomanip>  // Для std::setw, std::setfill
#include <sstream>  // Для std::ostringstream, std::istringstream
#include <array>    // Для std::array в validate

// Вспомогательная функция isLeap (может быть членом класса или свободной)
bool Date::isLeap(int y) const noexcept {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

void Date::validate(int d, int m, int y) {
    // Диапазоны взяты из ваших unit_tests (1900-2100)
    if (y < 1900 || y > 2100) {
        std::string error_msg = "Год " + std::to_string(y) + " вне допустимого диапазона (1900-2100).";
        // Logger::warn("Date::validate: " + error_msg); // Логирование не всегда нужно для invalid_argument
        throw std::invalid_argument(error_msg);
    }
    if (m < 1 || m > 12) {
        std::string error_msg = "Месяц " + std::to_string(m) + " должен быть в диапазоне от 1 до 12.";
        throw std::invalid_argument(error_msg);
    }

    const std::array<int, 13> daysInMonth = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int days_in_current_month = daysInMonth.at(static_cast<size_t>(m));

    if (m == 2 && isLeap(y)) {
        days_in_current_month = 29;
    }

    if (d < 1 || d > days_in_current_month) {
        std::string error_msg = "День " + std::to_string(d) + " некорректен для месяца " + std::to_string(m) +
                                " в году " + std::to_string(y) + ". Допустимо дней: " + std::to_string(days_in_current_month) + ".";
        throw std::invalid_argument(error_msg);
    }
}

Date::Date() noexcept : day_(1), month_(1), year_(1970) {
    // Дата по умолчанию (01.01.1970) считается валидной и не требует вызова validate.
}

Date::Date(int d, int m, int y) : day_(0), month_(0), year_(0) { // Инициализируем нулями перед валидацией
    validate(d, m, y); // Валидация установит корректные значения или выбросит исключение
    day_ = d;
    month_ = m;
    year_ = y;
}

std::string Date::toString() const {
    std::ostringstream oss;
    oss << std::setw(2) << std::setfill('0') << day_ << "."
        << std::setw(2) << std::setfill('0') << month_ << "."
        << year_;
    return oss.str();
}

bool Date::operator==(const Date& other) const noexcept {
    return year_ == other.year_ && month_ == other.month_ && day_ == other.day_;
}

bool Date::operator!=(const Date& other) const noexcept {
    return !(*this == other);
}

bool Date::operator<(const Date& other) const noexcept {
    if (year_ != other.year_) return year_ < other.year_;
    if (month_ != other.month_) return month_ < other.month_;
    return day_ < other.day_;
}

bool Date::operator>(const Date& other) const noexcept {
    return other < *this;
}

bool Date::operator<=(const Date& other) const noexcept {
    return !(*this > other);
}

bool Date::operator>=(const Date& other) const noexcept {
    return !(*this < other);
}

std::ostream& operator<<(std::ostream& os, const Date& date) {
    os << date.toString();
    return os;
}

std::istream& operator>>(std::istream& is, Date& date) {
    std::string s;
    // Читаем "слово" из потока. Предполагается, что дата отделена пробелами.
    if (!(is >> s)) {
        // Ошибка чтения или конец потока. is уже будет в состоянии fail.
        return is;
    }

    if (s.empty()) { // Маловероятно после is >> s, но для полноты
        is.setstate(std::ios_base::failbit);
        return is;
    }

    int d_in = 0, m_in = 0, y_in = 0;
    char dot1 = 0, dot2 = 0;
    char extra_char = 0; // Для проверки лишних символов

    std::istringstream date_ss(s);
    date_ss >> d_in >> dot1 >> m_in >> dot2 >> y_in;

    // Проверяем, удалось ли считать все компоненты, корректны ли разделители,
    // и нет ли лишних символов в строке s после даты.
    if (date_ss.fail() || dot1 != '.' || dot2 != '.' || (date_ss.get(extra_char) && !date_ss.eof())) {
        // date_ss.fail() - если разбор чисел или символов не удался
        // (date_ss.get(extra_char) && !date_ss.eof()) - проверяет, остались ли неразобранные символы
        is.setstate(std::ios_base::failbit); // Ошибка формата
        return is;
    }
    // Если date_ss.eof() после >> y_in, это значит, что вся строка была успешно разобрана.
    // Но если было date_ss >> y_in >> std::ws, и потом eof(), это тоже ок.
    // Проверка date_ss.rdbuf()->in_avail() != 0 из ваших тестов - хороший способ.
    // Или, после чтения y_in, попытаться прочитать еще что-то не пробельное.
    // Текущая проверка с date_ss.get(extra_char) должна работать.

    try {
        Date temp_date(d_in, m_in, y_in); // Валидация произойдет в конструкторе temp_date
        date = temp_date; // Присваиваем, если валидация прошла
    } catch (const std::invalid_argument& /*e*/) {
        is.setstate(std::ios_base::failbit); // Устанавливаем флаг ошибки для входного потока
    }
    return is;
}
