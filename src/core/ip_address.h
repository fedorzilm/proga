/*!
 * \file ip_address.h
 * \author Fedor Zilnitskiy
 * \brief Заголовочный файл для класса IPAddress, представляющего IPv4-адрес.
 */
#ifndef IP_ADDRESS_H
#define IP_ADDRESS_H

#include "common_defs.h" // Для std::string, std::array, std::ostream, std::istream
#include <string>
#include <array>
#include <stdexcept>    // Для std::invalid_argument

/*!
 * \brief Класс для представления и валидации IPv4-адреса.
 */
class IPAddress final {
public:
    /*! \brief Конструктор по умолчанию (0.0.0.0). */
    IPAddress() noexcept;

    /*!
     * \brief Конструктор из четырех октетов.
     * \throw std::invalid_argument при невалидных значениях октетов.
     */
    IPAddress(int o1, int o2, int o3, int o4);

    /*! \brief Преобразование IP-адреса в строку "o1.o2.o3.o4". */
    std::string toString() const;

    /*! \brief Оператор равенства. */
    bool operator==(const IPAddress& other) const noexcept;
    /*! \brief Оператор неравенства. */
    bool operator!=(const IPAddress& other) const noexcept;
    /*! \brief Оператор "меньше". */
    bool operator<(const IPAddress& other) const noexcept;
    /*! \brief Оператор "больше". */
    bool operator>(const IPAddress& other) const noexcept; // <-- ДОБАВЛЕНО ОБЪЯВЛЕНИЕ
    // Операторы <= и >= могут быть реализованы как свободные функции или через ! и <, >
    // В тестах используются !(b < a) для a <= b и !(a < b) для a >= b, что корректно.

    // Друзья для операторов ввода/вывода
    friend std::ostream& operator<<(std::ostream& os, const IPAddress& ip);
    friend std::istream& operator>>(std::istream& is, IPAddress& ip);

private:
    std::array<unsigned char, 4> octets_{}; ///< Хранилище октетов IP-адреса.

    /*! \brief Проверка валидности одного октета (0-255). */
    bool isValidOctet(int octet_val) const noexcept;
    /*! \brief Валидация всех четырех октетов. */
    void validateOctets(int o1, int o2, int o3, int o4);
};

#endif // IP_ADDRESS_H
