// Предполагаемый путь: src/core/date.h
#ifndef DATE_H
#define DATE_H

#include <string>
#include <iostream> // Для std::ostream и std::istream
#include <stdexcept> // Для std::invalid_argument

/*!
 * \file date.h
 * \brief Определяет класс Date для представления и управления датами.
 */

/*!
 * \class Date
 * \brief Представляет дату (день, месяц, год) и предоставляет операции для работы с ней.
 *
 * Класс обеспечивает валидацию даты при создании и изменении,
 * а также операции сравнения и строкового представления.
 * Класс помечен как final, так как не предназначен для наследования.
 */
class Date final {
private:
    int day_;   /*!< День месяца (1-31). */
    int month_; /*!< Месяц (1-12). */
    int year_;  /*!< Год (например, 1900-2100). */

    /*! \brief Проверяет, является ли год високосным. \param y Год для проверки. \return true, если год високосный, иначе false. */
    bool isLeap(int y) const noexcept;

    /*!
     * \brief Проверяет корректность даты. Выбрасывает std::invalid_argument, если дата некорректна.
     * \param d День.
     * \param m Месяц.
     * \param y Год.
     * \throw std::invalid_argument если день, месяц или год выходят за допустимые диапазоны.
     */
    void validate(int d, int m, int y);

public:
    /*!
     * \brief Конструктор по умолчанию.
     * Инициализирует дату значением по умолчанию (например, 01.01.1970 или другая валидная дата).
     */
    Date() noexcept;

    /*!
     * \brief Конструктор, инициализирующий дату указанными значениями дня, месяца и года.
     * \param d День.
     * \param m Месяц.
     * \param y Год.
     * \throw std::invalid_argument если указанная дата некорректна.
     */
    Date(int d, int m, int y);

    /*! \brief Возвращает день месяца. \return День. */
    int getDay() const noexcept { return day_; }
    /*! \brief Возвращает месяц. \return Месяц. */
    int getMonth() const noexcept { return month_; }
    /*! \brief Возвращает год. \return Год. */
    int getYear() const noexcept { return year_; }

    // Сеттеры не предоставляются, чтобы обеспечить неизменяемость после создания,
    // либо они должны вызывать validate. Для простоты, предполагаем неизменяемость.

    /*! \brief Преобразует дату в строку формата "ДД.ММ.ГГГГ". \return Строковое представление даты. */
    std::string toString() const;

    // Операторы сравнения
    bool operator==(const Date& other) const noexcept;
    bool operator!=(const Date& other) const noexcept;
    bool operator<(const Date& other) const noexcept;
    bool operator>(const Date& other) const noexcept;
    bool operator<=(const Date& other) const noexcept;
    bool operator>=(const Date& other) const noexcept;

    // Друзья для перегрузки операторов ввода/вывода
    friend std::ostream& operator<<(std::ostream& os, const Date& date);
    friend std::istream& operator>>(std::istream& is, Date& date);
};

#endif // DATE_H
