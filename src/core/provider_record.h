/*!
 * \file provider_record.h
 * \brief Определяет класс ProviderRecord для представления записи о данных интернет-провайдера.
 *
 * ProviderRecord инкапсулирует информацию об одном абоненте, включая его ФИО, IP-адрес,
 * дату записи, а также почасовые данные о входящем и исходящем трафике за сутки.
 * Класс обеспечивает валидацию данных трафика, преобразование в строковый формат
 * для вывода и сохранения, а также считывание из потока.
 */
#ifndef PROVIDER_RECORD_H
#define PROVIDER_RECORD_H

#include "common_defs.h" // Для HOURS_IN_DAY, DOUBLE_EPSILON и стандартных заголовков <string>, <vector>, <iostream>, <stdexcept>
#include "ip_address.h"  // Предполагаемый путь: src/core/ip_address.h
#include "date.h"        // Предполагаемый путь: src/core/date.h

#include <string>
#include <vector>
#include <iostream>     // Для std::ostream и std::istream
#include <stdexcept>    // Для std::invalid_argument

/*!
 * \class ProviderRecord
 * \brief Представляет одну запись в базе данных интернет-провайдера.
 *
 * Содержит информацию об абоненте (ФИО), его IP-адресе, дате, к которой относится запись,
 * и векторы с данными о входящем и исходящем трафике (в ГБ) для каждого из 24 часов суток.
 * Предоставляет конструкторы, геттеры, сеттеры с валидацией для данных о трафике,
 * операторы сравнения и операторы для потокового ввода/вывода.
 * Класс помечен как `final`, так как не предназначен для наследования.
 */
class ProviderRecord final {
private:
    std::string subscriberName_{};               /*!< Полное имя абонента. */
    IPAddress ipAddress_{};                      /*!< IP-адрес абонента (использует класс IPAddress). */
    Date date_{};                                /*!< Дата, к которой относится запись (использует класс Date). */
    std::vector<double> trafficInByHour_{};      /*!< Вектор данных о входящем трафике (ГБ) по часам (0-23). Размер должен быть HOURS_IN_DAY. */
    std::vector<double> trafficOutByHour_{};     /*!< Вектор данных об исходящем трафике (ГБ) по часам (0-23). Размер должен быть HOURS_IN_DAY. */

    /*!
     * \brief Вспомогательный метод для валидации вектора трафика.
     * Проверяет, что вектор содержит ровно `HOURS_IN_DAY` неотрицательных значений.
     * \param traffic Вектор данных о трафике для проверки.
     * \param traffic_type_name Строковое описание типа трафика (например, "входящем", "исходящем") для использования в сообщениях об ошибках.
     * \throw std::invalid_argument если вектор трафика имеет неверный размер или содержит отрицательные значения.
     */
    void validateTrafficVector(const std::vector<double>& traffic, const std::string& traffic_type_name) const;

public:
    /*!
     * \brief Конструктор по умолчанию.
     * Инициализирует все поля значениями по умолчанию:
     * - Имя абонента: пустая строка.
     * - IP-адрес: 0.0.0.0 (значение по умолчанию для IPAddress).
     * - Дата: 01.01.1970 (значение по умолчанию для Date).
     * - Данные о трафике: векторы из `HOURS_IN_DAY` нулей.
     */
    ProviderRecord() noexcept;

    /*!
     * \brief Конструктор, инициализирующий запись указанными значениями.
     * \param name Имя абонента.
     * \param ip IP-адрес абонента.
     * \param recordDate Дата записи.
     * \param trafficIn Вектор входящего трафика по часам. Должен содержать `HOURS_IN_DAY` неотрицательных значений.
     * \param trafficOut Вектор исходящего трафика по часам. Должен содержать `HOURS_IN_DAY` неотрицательных значений.
     * \throw std::invalid_argument если данные о трафике (`trafficIn` или `trafficOut`) некорректны (неверный размер или отрицательные значения).
     */
    ProviderRecord(const std::string& name,
                   const IPAddress& ip,
                   const Date& recordDate,
                   const std::vector<double>& trafficIn,
                   const std::vector<double>& trafficOut);

    // Геттеры
    /*! \brief Возвращает имя абонента. */
    const std::string& getName() const noexcept { return subscriberName_; }
    /*! \brief Возвращает IP-адрес абонента. */
    const IPAddress& getIpAddress() const noexcept { return ipAddress_; }
    /*! \brief Возвращает дату записи. */
    const Date& getDate() const noexcept { return date_; }
    /*! \brief Возвращает вектор данных о входящем трафике по часам. */
    const std::vector<double>& getTrafficInByHour() const noexcept { return trafficInByHour_; }
    /*! \brief Возвращает вектор данных об исходящем трафике по часам. */
    const std::vector<double>& getTrafficOutByHour() const noexcept { return trafficOutByHour_; }

    // Сеттеры
    /*! \brief Устанавливает имя абонента. \param name Новое имя абонента. */
    void setName(const std::string& name) { subscriberName_ = name; }
    /*! \brief Устанавливает IP-адрес абонента. \param ip Новый IP-адрес. */
    void setIpAddress(const IPAddress& ip) { ipAddress_ = ip; }
    /*! \brief Устанавливает дату записи. \param recordDate Новая дата. */
    void setDate(const Date& recordDate) { date_ = recordDate; }

    /*!
     * \brief Устанавливает данные о входящем трафике по часам.
     * Производит валидацию переданного вектора.
     * \param trafficIn Вектор данных о входящем трафике. Должен содержать `HOURS_IN_DAY` неотрицательных значений.
     * \throw std::invalid_argument если вектор трафика некорректен.
     */
    void setTrafficInByHour(const std::vector<double>& trafficIn);

    /*!
     * \brief Устанавливает данные об исходящем трафике по часам.
     * Производит валидацию переданного вектора.
     * \param trafficOut Вектор данных об исходящем трафике. Должен содержать `HOURS_IN_DAY` неотрицательных значений.
     * \throw std::invalid_argument если вектор трафика некорректен.
     */
    void setTrafficOutByHour(const std::vector<double>& trafficOut);

    // Операторы сравнения
    /*!
     * \brief Оператор сравнения на равенство.
     * Сравнивает все поля, включая данные о трафике (с учетом `DOUBLE_EPSILON` для `double`).
     * \param other Другая запись ProviderRecord для сравнения.
     * \return `true`, если записи равны, иначе `false`.
     */
    bool operator==(const ProviderRecord& other) const noexcept;
    /*!
     * \brief Оператор сравнения на неравенство.
     * \param other Другая запись ProviderRecord для сравнения.
     * \return `true`, если записи не равны, иначе `false`.
     */
    bool operator!=(const ProviderRecord& other) const noexcept;

    /*!
     * \brief Перегруженный оператор вывода в поток.
     * Выводит запись в многострочном формате: ФИО, IP, Дата, строка входящего трафика, строка исходящего трафика.
     * Значения трафика выводятся с точностью до двух знаков после запятой.
     * \param os Выходной поток.
     * \param record Объект ProviderRecord для вывода.
     * \return Ссылка на выходной поток.
     */
    friend std::ostream& operator<<(std::ostream& os, const ProviderRecord& record);

    /*!
     * \brief Перегруженный оператор ввода из потока.
     * Считывает запись в том же многострочном формате, в котором она выводится оператором `<<`.
     * Устанавливает флаг `failbit` для потока в случае ошибки формата или невалидных данных.
     * \param is Входной поток.
     * \param record Объект ProviderRecord для сохранения считанных значений.
     * \return Ссылка на входной поток.
     */
    friend std::istream& operator>>(std::istream& is, ProviderRecord& record);
};

#endif // PROVIDER_RECORD_H
