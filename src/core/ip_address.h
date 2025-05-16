// Предполагаемый путь: src/core/ip_address.h
#ifndef IP_ADDRESS_H
#define IP_ADDRESS_H

#include <string>
#include <array>    // Для std::array
#include <iostream> // Для std::ostream и std::istream
#include <stdexcept> // Для std::invalid_argument

/*!
 * \file ip_address.h
 * \brief Определяет класс IPAddress для представления и управления IPv4-адресами.
 */

/*!
 * \class IPAddress
 * \brief Представляет IPv4-адрес и предоставляет операции для работы с ним.
 *
 * Хранит IP-адрес в виде четырех октетов. Обеспечивает валидацию,
 * строковое представление и операции сравнения.
 * Класс помечен как final, так как не предназначен для наследования.
 */
class IPAddress final {
private:
    std::array<unsigned char, 4> octets_{}; /*!< Массив из четырех октетов IP-адреса. */

    /*! \brief Проверяет, является ли значение октета валидным (0-255). \param octet_val Значение октета. \return true, если валидно. */
    bool isValidOctet(int octet_val) const noexcept;

    /*!
     * \brief Проверяет корректность всех четырех октетов. Выбрасывает std::invalid_argument, если есть некорректные.
     * \param o1 Первый октет.
     * \param o2 Второй октет.
     * \param o3 Третий октет.
     * \param o4 Четвертый октет.
     * \throw std::invalid_argument если какой-либо из октетов выходит за пределы 0-255.
     */
    void validateOctets(int o1, int o2, int o3, int o4);

public:
    /*!
     * \brief Конструктор по умолчанию.
     * Инициализирует IP-адрес значением по умолчанию (например, 0.0.0.0).
     */
    IPAddress() noexcept;

    /*!
     * \brief Конструктор, инициализирующий IP-адрес указанными значениями октетов.
     * \param o1 Первый октет.
     * \param o2 Второй октет.
     * \param o3 Третий октет.
     * \param o4 Четвертый октет.
     * \throw std::invalid_argument если значения октетов некорректны.
     */
    IPAddress(int o1, int o2, int o3, int o4);

    // Геттеры для октетов (если нужны)
    // int getOctet1() const noexcept { return static_cast<int>(octets_[0]); }
    // ...

    /*! \brief Преобразует IP-адрес в строку формата "o1.o2.o3.o4". \return Строковое представление IP-адреса. */
    std::string toString() const;

    // Операторы сравнения
    bool operator==(const IPAddress& other) const noexcept;
    bool operator!=(const IPAddress& other) const noexcept;
    bool operator<(const IPAddress& other) const noexcept; // Для возможности использования в std::map и т.д.

    // Друзья для перегрузки операторов ввода/вывода
    friend std::ostream& operator<<(std::ostream& os, const IPAddress& ip);
    friend std::istream& operator>>(std::istream& is, IPAddress& ip);
};

#endif // IP_ADDRESS_H
