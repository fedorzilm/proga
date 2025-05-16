// Предполагаемый путь: src/core/provider_record.h
#ifndef PROVIDER_RECORD_H
#define PROVIDER_RECORD_H

#include "common_defs.h" // Для HOURS_IN_DAY и стандартных заголовков
#include "ip_address.h"  // Предполагаемый путь: src/core/ip_address.h
#include "date.h"        // Предполагаемый путь: src/core/date.h
#include <string>
#include <vector>
#include <iostream>     // Для std::ostream и std::istream
#include <stdexcept>    // Для std::invalid_argument

/*!
 * \file provider_record.h
 * \brief Определяет класс ProviderRecord для представления записи о данных интернет-провайдера.
 */

/*!
 * \class ProviderRecord
 * \brief Представляет одну запись в базе данных интернет-провайдера.
 *
 * Содержит информацию об абоненте, его IP-адресе, дате записи,
 * а также почасовые данные о входящем и исходящем трафике.
 * Класс помечен как final, так как не предназначен для наследования.
 */
class ProviderRecord final {
private:
    std::string subscriberName_{};               /*!< Полное имя абонента. */
    IPAddress ipAddress_{};                      /*!< IP-адрес абонента. */
    Date date_{};                                /*!< Дата, к которой относится запись. */
    std::vector<double> trafficInByHour_{};      /*!< Вектор данных о входящем трафике (ГБ) по часам (0-23). */
    std::vector<double> trafficOutByHour_{};     /*!< Вектор данных об исходящем трафике (ГБ) по часам (0-23). */

    /*!
     * \brief Вспомогательный метод для валидации вектора трафика.
     * Проверяет, что вектор содержит ровно HOURS_IN_DAY неотрицательных значений.
     * \param traffic Вектор для проверки.
     * \param traffic_type_name Строковое описание типа трафика для сообщений об ошибках (например, "входящем", "исходящем").
     * \throw std::invalid_argument если вектор трафика некорректен.
     */
    void validateTrafficVector(const std::vector<double>& traffic, const std::string& traffic_type_name) const;

public:
    /*!
     * \brief Конструктор по умолчанию.
     * Инициализирует все поля значениями по умолчанию (пустая строка, IP 0.0.0.0, дата 01.01.1970, трафик по нулям).
     */
    ProviderRecord() noexcept;

    /*!
     * \brief Конструктор, инициализирующий запись указанными значениями.
     * \param name Имя абонента.
     * \param ip IP-адрес абонента.
     * \param recordDate Дата записи.
     * \param trafficIn Вектор входящего трафика по часам (должен содержать HOURS_IN_DAY значений).
     * \param trafficOut Вектор исходящего трафика по часам (должен содержать HOURS_IN_DAY значений).
     * \throw std::invalid_argument если данные о трафике некорректны.
     */
    ProviderRecord(const std::string& name,
                   const IPAddress& ip,
                   const Date& recordDate,
                   const std::vector<double>& trafficIn,
                   const std::vector<double>& trafficOut);

    // Геттеры
    const std::string& getName() const noexcept { return subscriberName_; }
    const IPAddress& getIpAddress() const noexcept { return ipAddress_; }
    const Date& getDate() const noexcept { return date_; }
    const std::vector<double>& getTrafficInByHour() const noexcept { return trafficInByHour_; }
    const std::vector<double>& getTrafficOutByHour() const noexcept { return trafficOutByHour_; }

    // Сеттеры (с валидацией, если необходимо)
    void setName(const std::string& name) { subscriberName_ = name; }
    void setIpAddress(const IPAddress& ip) { ipAddress_ = ip; }
    void setDate(const Date& recordDate) { date_ = recordDate; }

    /*!
     * \brief Устанавливает данные о входящем трафике.
     * \param trafficIn Вектор данных о входящем трафике (должен содержать HOURS_IN_DAY значений).
     * \throw std::invalid_argument если вектор трафика некорректен.
     */
    void setTrafficInByHour(const std::vector<double>& trafficIn);

    /*!
     * \brief Устанавливает данные об исходящем трафике.
     * \param trafficOut Вектор данных об исходящем трафике (должен содержать HOURS_IN_DAY значений).
     * \throw std::invalid_argument если вектор трафика некорректен.
     */
    void setTrafficOutByHour(const std::vector<double>& trafficOut);

    // Операторы сравнения
    bool operator==(const ProviderRecord& other) const noexcept;
    bool operator!=(const ProviderRecord& other) const noexcept;
    // Оператор < может быть полезен, если записи будут храниться в упорядоченных контейнерах,
    // но его реализация зависит от того, по какому полю/полям сортировать.

    // Друзья для перегрузки операторов ввода/вывода
    friend std::ostream& operator<<(std::ostream& os, const ProviderRecord& record);
    friend std::istream& operator>>(std::istream& is, ProviderRecord& record);
};

#endif // PROVIDER_RECORD_H
