// Предполагаемый путь: src/core/query_parser.h
#ifndef QUERY_PARSER_H
#define QUERY_PARSER_H

/*!
 * \file query_parser.h
 * \brief Определяет класс QueryParser и связанные структуры для разбора (парсинга) строковых запросов к базе данных.
 */

#include "common_defs.h" // Для общих определений, таких как HOURS_IN_DAY
#include "ip_address.h"  // Предполагаемый путь: src/core/ip_address.h
#include "date.h"        // Предполагаемый путь: src/core/date.h
#include <string>
#include <vector>
#include <map>     // Для хранения параметров SET в команде EDIT
#include <sstream> // Для токенизации и разбора строк

/*!
 * \enum QueryType
 * \brief Перечисляет различные типы запросов к базе данных.
 */
enum class QueryType {
    ADD,                /*!< Запрос на добавление записи. */
    SELECT,             /*!< Запрос на выборку записей. */
    DELETE,             /*!< Запрос на удаление записей. */
    EDIT,               /*!< Запрос на редактирование записи. */
    CALCULATE_CHARGES,  /*!< Запрос на расчет стоимости услуг. */
    PRINT_ALL,          /*!< Запрос на печать всех записей. */
    LOAD,               /*!< Запрос на загрузку данных из файла (серверная операция). */
    SAVE,               /*!< Запрос на сохранение данных в файл (серверная операция). */
    EXIT,               /*!< Команда выхода из клиентского приложения или завершения сессии. */
    HELP,               /*!< Команда для отображения справки (обычно обрабатывается клиентом). */
    UNKNOWN             /*!< Неизвестный или некорректный тип запроса. */
};

/*!
 * \struct QueryParameters
 * \brief Хранит разобранные параметры для запроса к базе данных.
 */
struct QueryParameters {
    // --- Поля критериев (для условий типа WHERE) ---
    std::string criteriaName{};
    IPAddress criteriaIpAddress{};
    Date criteriaDate{};
    Date criteriaStartDate{};
    Date criteriaEndDate{};

    // --- Поля данных (для команды ADD или секции SET в EDIT) ---
    std::string subscriberNameData{};
    IPAddress ipAddressData{};
    Date dateData{};
    std::vector<double> trafficInData{};
    std::vector<double> trafficOutData{};

    // --- Поля для команд LOAD/SAVE ---
    std::string filename{};

    // --- Флаги, указывающие, какие критерии активны ---
    bool useNameFilter = false;
    bool useIpFilter = false;
    bool useDateFilter = false;
    bool useStartDateFilter = false;
    bool useEndDateFilter = false;

    // --- Данные для секции SET команды EDIT ---
    std::map<std::string, std::string> setData{};
    bool hasTrafficInToSet = false;
    bool hasTrafficOutToSet = false;

    /*! \brief Сбрасывает все параметры в их состояние по умолчанию/пустое состояние. */
    void reset() noexcept {
        criteriaName.clear();
        criteriaIpAddress = IPAddress(); // Предполагается конструктор по умолчанию
        criteriaDate = Date();           // Предполагается конструктор по умолчанию
        criteriaStartDate = Date();
        criteriaEndDate = Date();

        subscriberNameData.clear();
        ipAddressData = IPAddress();
        dateData = Date();
        trafficInData.clear();
        trafficOutData.clear();
        filename.clear();

        useNameFilter = false;
        useIpFilter = false;
        useDateFilter = false;
        useStartDateFilter = false;
        useEndDateFilter = false;

        setData.clear();
        hasTrafficInToSet = false;
        hasTrafficOutToSet = false;
    }

    /*! \brief Конструктор. Гарантирует чистое состояние при создании объекта. */
    QueryParameters() { // Не может быть noexcept из-за конструкторов IPAddress/Date
        reset();
    }
};

/*!
 * \struct Query
 * \brief Представляет разобранный запрос к базе данных.
 */
struct Query {
    QueryType type = QueryType::UNKNOWN;
    QueryParameters params{};
    std::string originalQueryString{}; // Может быть полезно для логирования
};

/*!
 * \class QueryParser
 * \brief Разбирает (парсит) необработанные строки запросов в структурированные объекты Query.
 * Класс не предназначен для наследования и помечен как final.
 */
class QueryParser final {
private:
    /*!
     * \brief Разбивает строку запроса на токены (лексемы).
     * Учитывает строки в двойных кавычках.
     * \param queryString Необработанная строка запроса.
     * \return Вектор строковых токенов.
     * \throw std::runtime_error при ошибке токенизации (например, незакрытая кавычка).
     */
    std::vector<std::string> tokenize(const std::string& queryString) const;

    /*!
     * \brief Вспомогательный метод для парсинга блока из HOURS_IN_DAY значений трафика.
     * \param tokens Вектор токенов.
     * \param traffic_vector Вектор для сохранения разобранных значений трафика (выходной параметр).
     * \param currentIndex Текущий индекс в векторе токенов (передается по ссылке, обновляется).
     * \param commandName Имя команды (например, "ADD", "EDIT SET") для сообщений об ошибках.
     * \param traffic_type_name Описание типа трафика ("TRAFFIC_IN" или "TRAFFIC_OUT") для сообщений об ошибках.
     * \throw std::runtime_error при синтаксических ошибках (недостаточно значений, нечисловые значения, отрицательные значения).
     */
    void parseTrafficBlock(const std::vector<std::string>& tokens,
                           std::vector<double>& traffic_vector,
                           size_t& currentIndex,
                           const std::string& commandName,
                           const std::string& traffic_type_name) const;

    /*! \brief Разбирает параметры для запроса ADD. */
    void parseAddParams(const std::vector<std::string>& tokens, QueryParameters& params, size_t& currentIndex) const;

    /*! \brief Разбирает параметры критериев для SELECT, DELETE или начальной части EDIT. */
    void parseCriteriaParams(const std::vector<std::string>& tokens, QueryParameters& params, size_t& currentIndex) const;

    /*! \brief Разбирает параметры секции SET для запроса EDIT. */
    void parseEditSetParams(const std::vector<std::string>& tokens, QueryParameters& params, size_t& currentIndex) const;

    /*! \brief Разбирает параметры для запроса CALCULATE_CHARGES. */
    void parseCalculateChargesParams(const std::vector<std::string>& tokens, QueryParameters& params, size_t& currentIndex) const;

public:
    /*! \brief Конструктор по умолчанию. */
    QueryParser() = default;

    /*!
     * \brief Разбирает (парсит) необработанную строку запроса.
     * \param queryString Строка запроса для разбора.
     * \return Структурированный объект Query.
     * \throw std::runtime_error если разбор не удался из-за синтаксических ошибок или неизвестных команд.
     */
    Query parseQuery(const std::string& queryString) const;
};

#endif // QUERY_PARSER_H
