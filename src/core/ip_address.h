/*!
 * \file ip_address.h
 * \author Fedor Zilnitskiy
 * \brief Определяет класс IPAddress для представления и управления IPv4-адресами.
 *
 * Класс IPAddress инкапсулирует логику работы с IPv4-адресами, включая их хранение,
 * валидацию, преобразование в строковый формат и операции сравнения.
 * Это обеспечивает типобезопасность и удобство при работе с IP-адресами в системе.
 */
#ifndef IP_ADDRESS_H
#define IP_ADDRESS_H

#include "common_defs.h" // Включает <string>, <array>, <iostream>, <stdexcept>
#include <string>
#include <array>
#include <iostream>
#include <stdexcept>

/*!
 * \class IPAddress
 * \brief Представляет IPv4-адрес и предоставляет операции для работы с ним.
 *
 * Хранит IP-адрес в виде четырех октетов (от 0 до 255 каждый).
 * Обеспечивает валидацию при создании, преобразование в строку формата "o1.o2.o3.o4"
 * и операции сравнения (==, !=, <). Поддерживает ввод/вывод через потоки C++.
 * Класс помечен как `final`, так как не предназначен для наследования.
 */
class IPAddress final {
private:
    std::array<unsigned char, 4> octets_{}; /*!< Массив из четырех октетов IP-адреса. */

    /*!
     * \brief Проверяет, является ли переданное целочисленное значение валидным октетом IP-адреса.
     * Валидный октет находится в диапазоне [0, 255].
     * \param octet_val Значение октета для проверки.
     * \return `true`, если значение октета валидно, иначе `false`.
     */
    bool isValidOctet(int octet_val) const noexcept;

    /*!
     * \brief Проверяет корректность всех четырех переданных октетов.
     * Если какой-либо из октетов выходит за пределы допустимого диапазона [0, 255],
     * выбрасывает исключение `std::invalid_argument`.
     * \param o1 Первый октет.
     * \param o2 Второй октет.
     * \param o3 Третий октет.
     * \param o4 Четвертый октет.
     * \throw std::invalid_argument если какой-либо из октетов некорректен.
     */
    void validateOctets(int o1, int o2, int o3, int o4);

public:
    /*!
     * \brief Конструктор по умолчанию.
     * Инициализирует IP-адрес значением по умолчанию 0.0.0.0.
     */
    IPAddress() noexcept;

    /*!
     * \brief Конструктор, инициализирующий IP-адрес указанными значениями октетов.
     * \param o1 Первый октет (0-255).
     * \param o2 Второй октет (0-255).
     * \param o3 Третий октет (0-255).
     * \param o4 Четвертый октет (0-255).
     * \throw std::invalid_argument если какое-либо из значений октетов некорректно (вне диапазона 0-255).
     */
    IPAddress(int o1, int o2, int o3, int o4);

    // Геттеры для октетов могут быть добавлены при необходимости, но для текущих задач они не требуются.
    // Пример:
    // unsigned char getOctet1() const noexcept { return octets_[0]; }
    // unsigned char getOctet2() const noexcept { return octets_[1]; }
    // unsigned char getOctet3() const noexcept { return octets_[2]; }
    // unsigned char getOctet4() const noexcept { return octets_[3]; }

    /*!
     * \brief Преобразует IP-адрес в строковое представление.
     * \return Строка IP-адреса в формате "ddd.ddd.ddd.ddd".
     */
    std::string toString() const;

    /*!
     * \brief Оператор сравнения на равенство.
     * \param other Другой IPAddress для сравнения.
     * \return `true`, если IP-адреса равны, иначе `false`.
     */
    bool operator==(const IPAddress& other) const noexcept;

    /*!
     * \brief Оператор сравнения на неравенство.
     * \param other Другой IPAddress для сравнения.
     * \return `true`, если IP-адреса не равны, иначе `false`.
     */
    bool operator!=(const IPAddress& other) const noexcept;

    /*!
     * \brief Оператор "меньше".
     * Позволяет использовать IPAddress в упорядоченных контейнерах, таких как `std::map`.
     * Сравнение производится лексикографически по октетам.
     * \param other Другой IPAddress для сравнения.
     * \return `true`, если текущий IP-адрес меньше `other`, иначе `false`.
     */
    bool operator<(const IPAddress& other) const noexcept;

    /*!
     * \brief Перегруженный оператор вывода в поток.
     * Выводит IP-адрес в строковом формате в указанный поток.
     * \param os Выходной поток.
     * \param ip Объект IPAddress для вывода.
     * \return Ссылка на выходной поток.
     */
    friend std::ostream& operator<<(std::ostream& os, const IPAddress& ip);

    /*!
     * \brief Перегруженный оператор ввода из потока.
     * Считывает IP-адрес в строковом формате "ddd.ddd.ddd.ddd" из указанного потока.
     * Устанавливает флаг `failbit` для потока в случае ошибки формата или невалидных значений октетов.
     * \param is Входной поток.
     * \param ip Объект IPAddress для сохранения считанного значения.
     * \return Ссылка на входной поток.
     */
    friend std::istream& operator>>(std::istream& is, IPAddress& ip);
};

#endif // IP_ADDRESS_H
