/*!
 * \file date.cpp
 * \author Fedor Zilnitskiy
 * \brief Реализация класса Date для работы с календарными датами.
 */
#include "date.h"
#include "logger.h"     // Для возможного логирования, хотя в основном используются исключения
#include <iomanip>      // Для std::setw, std::setfill при форматировании вывода
#include <sstream>      // Для std::ostringstream (в toString) и std::istringstream (в operator>>)
#include <array>        // Для массива дней в месяце в функции validate

/*!
 * \brief Проверяет, является ли год високосным.
 * \param y Год.
 * \return true, если год високосный.
 */
bool Date::isLeap(int y) const noexcept {
    // Стандартный алгоритм определения високосного года
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

/*!
 * \brief Валидирует дату.
 * \param d День.
 * \param m Месяц.
 * \param y Год.
 * \throw std::invalid_argument Если дата некорректна.
 */
void Date::validate(int d, int m, int y) {
    // Диапазоны года (например, 1900-2100) могут быть вынесены в константы
    const int MIN_YEAR = 1900;
    const int MAX_YEAR = 2100; // Задано в тестах и описании задачи

    if (y < MIN_YEAR || y > MAX_YEAR) {
        std::string error_msg = "Год " + std::to_string(y) + " вне допустимого диапазона (" +
                                std::to_string(MIN_YEAR) + "-" + std::to_string(MAX_YEAR) + ").";
        throw std::invalid_argument(error_msg);
    }
    if (m < 1 || m > 12) {
        std::string error_msg = "Месяц " + std::to_string(m) + " должен быть в диапазоне от 1 до 12.";
        throw std::invalid_argument(error_msg);
    }

    // Массив для хранения количества дней в каждом месяце (0-й элемент не используется)
    //                            Янв Фев Мар Апр Май Июн Июл Авг Сен Окт Ноя Дек
    const std::array<int, 13> daysInMonth = {{0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}};
    int days_in_current_month = daysInMonth.at(static_cast<size_t>(m)); // at() для проверки границ индекса m

    if (m == 2 && isLeap(y)) { // Февраль в високосный год
        days_in_current_month = 29;
    }

    if (d < 1 || d > days_in_current_month) {
        std::string error_msg = "День " + std::to_string(d) + " некорректен для месяца " + std::to_string(m) +
                                " в году " + std::to_string(y) + ". Допустимо дней в этом месяце: " + std::to_string(days_in_current_month) + ".";
        throw std::invalid_argument(error_msg);
    }
}

/*!
 * \brief Конструктор по умолчанию, дата 01.01.1970.
 */
Date::Date() noexcept : day_(1), month_(1), year_(1970) {
    // Дата по умолчанию (01.01.1970) считается валидной и не требует вызова validate здесь,
    // так как поля инициализируются корректными значениями.
}

/*!
 * \brief Конструктор с параметрами дня, месяца, года.
 * \param d День.
 * \param m Месяц.
 * \param y Год.
 * \throw std::invalid_argument Если дата невалидна.
 */
Date::Date(int d, int m, int y) : day_(0), month_(0), year_(0) { // Инициализация нулями перед валидацией
    validate(d, m, y); // Валидация установит корректные значения или выбросит исключение
    // Если validate не выбросил исключение, присваиваем значения
    day_ = d;
    month_ = m;
    year_ = y;
}

/*!
 * \brief Преобразует дату в строку "ДД.ММ.ГГГГ".
 * \return Строковое представление даты.
 */
std::string Date::toString() const {
    std::ostringstream oss;
    oss << std::setw(2) << std::setfill('0') << day_ << "."
        << std::setw(2) << std::setfill('0') << month_ << "."
        << year_; // Год выводится полностью
    return oss.str();
}

/*! \brief Оператор равенства. */
bool Date::operator==(const Date& other) const noexcept {
    return year_ == other.year_ && month_ == other.month_ && day_ == other.day_;
}

/*! \brief Оператор неравенства. */
bool Date::operator!=(const Date& other) const noexcept {
    return !(*this == other);
}

/*! \brief Оператор "меньше". */
bool Date::operator<(const Date& other) const noexcept {
    if (year_ != other.year_) return year_ < other.year_;
    if (month_ != other.month_) return month_ < other.month_;
    return day_ < other.day_;
}

/*! \brief Оператор "больше". */
bool Date::operator>(const Date& other) const noexcept {
    return other < *this; // Реализация через operator<
}

/*! \brief Оператор "меньше или равно". */
bool Date::operator<=(const Date& other) const noexcept {
    return !(*this > other); // Реализация через operator>
}

/*! \brief Оператор "больше или равно". */
bool Date::operator>=(const Date& other) const noexcept {
    return !(*this < other); // Реализация через operator<
}

/*!
 * \brief Оператор вывода даты в поток.
 * \param os Выходной поток.
 * \param date Дата для вывода.
 * \return Ссылка на выходной поток.
 */
std::ostream& operator<<(std::ostream& os, const Date& date) {
    os << date.toString();
    return os;
}

/*!
 * \brief Оператор ввода даты из потока.
 * Ожидает формат "ДД.ММ.ГГГГ".
 * \param is Входной поток.
 * \param date Дата для считывания.
 * \return Ссылка на входной поток.
 */
std::istream& operator>>(std::istream& is, Date& date) {
    std::string s;
    if (!(is >> s)) { // Читаем одно слово, разделенное пробелами
        return is;    // Ошибка чтения или конец потока, состояние потока уже установлено
    }

    // Если токен пустой после успешного извлечения (например, поток содержал только пробелы), это ошибка.
    // Однако, `is >> s` пропустит начальные пробелы. Если после них ничего нет, то `is` будет в состоянии eof.
    // Если `s` пустое, но `is` не eof, это странная ситуация, но лучше обработать.
    if (s.empty() && !is.eof()) {
        is.setstate(std::ios_base::failbit);
        return is;
    }
     // Если s пустое потому что достигнут конец файла сразу, просто выходим
    if (s.empty() && is.eof()){
        return is;
    }

    std::istringstream date_ss(s);
    int d_in = 0, m_in = 0, y_in = 0;
    char dot1 = 0, dot2 = 0;

    // Пытаемся считать компоненты даты и разделители-точки
    // `std::istream::operator>>` для int пропустит пробелы перед числом, если они есть внутри `s`.
    if (!(date_ss >> d_in)) { is.setstate(std::ios_base::failbit); return is; }
    if (!(date_ss >> dot1) || dot1 != '.') { is.setstate(std::ios_base::failbit); return is; }
    if (!(date_ss >> m_in)) { is.setstate(std::ios_base::failbit); return is; }
    if (!(date_ss >> dot2) || dot2 != '.') { is.setstate(std::ios_base::failbit); return is; }
    if (!(date_ss >> y_in)) { is.setstate(std::ios_base::failbit); return is; }

    // Проверяем, остались ли в строке `s` какие-либо не пробельные символы после даты
    // `std::ws` потребит любые пробелы. Если после этого поток не в состоянии eof,
    // значит, в строке `s` были лишние не-пробельные символы.
    date_ss >> std::ws;
    if (!date_ss.eof()) {
        is.setstate(std::ios_base::failbit); // Лишние символы в токене
        return is;
    }

    // Если все компоненты успешно считаны и формат строки `s` корректен,
    // пытаемся создать объект Date (что вызовет валидацию диапазонов).
    try {
        Date temp_date(d_in, m_in, y_in); // Валидация происходит в конструкторе
        date = temp_date; // Присваиваем, если валидация прошла успешно
    } catch (const std::invalid_argument&) {
        // Конструктор Date выбросил исключение (невалидные день, месяц или год)
        is.setstate(std::ios_base::failbit);
    }
    return is;
}
