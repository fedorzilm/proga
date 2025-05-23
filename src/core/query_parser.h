/*!
 * \file query_parser.h
 * \brief Определяет класс QueryParser и связанные структуры для разбора (парсинга) строковых запросов к базе данных интернет-провайдера.
 *
 * QueryParser отвечает за преобразование текстовых команд, вводимых пользователем или получаемых от клиента,
 * в структурированный формат (`Query` и `QueryParameters`), который затем используется
 * для выполнения операций с базой данных. Поддерживает различные типы запросов,
 * включая добавление, выборку, удаление, редактирование записей, расчет начислений,
 * загрузку/сохранение данных и другие.
 */
#ifndef QUERY_PARSER_H
#define QUERY_PARSER_H

#include "common_defs.h" // Для общих определений, таких как HOURS_IN_DAY, и стандартных заголовков <string>, <vector>, <map>, <sstream>
#include "ip_address.h"  // Для использования класса IPAddress в параметрах запроса
#include "date.h"        // Для использования класса Date в параметрах запроса

#include <string>
#include <vector>
#include <map>     // Для хранения параметров SET в команде EDIT
#include <sstream> // Для токенизации и разбора строк

/*!
 * \enum QueryType
 * \brief Перечисляет различные типы запросов к базе данных.
 * Используется для определения действия, которое должен выполнить сервер.
 */
enum class QueryType {
    ADD,                /*!< Запрос на добавление новой записи в базу данных. */
    SELECT,             /*!< Запрос на выборку записей из базы данных по заданным критериям. */
    DELETE,             /*!< Запрос на удаление записей из базы данных по заданным критериям. */
    EDIT,               /*!< Запрос на редактирование существующей записи в базе данных. */
    CALCULATE_CHARGES,  /*!< Запрос на расчет стоимости услуг для выбранных записей за указанный период. */
    PRINT_ALL,          /*!< Запрос на печать (вывод) всех записей из базы данных. */
    LOAD,               /*!< Запрос на загрузку данных из файла на сервере в базу данных. */
    SAVE,               /*!< Запрос на сохранение текущего состояния базы данных в файл на сервере. */
    EXIT,               /*!< Команда для корректного завершения клиентской сессии с сервером. */
    HELP,               /*!< Команда для отображения справки (обычно обрабатывается клиентом, но сервер может вернуть список команд). */
    UNKNOWN             /*!< Тип для неизвестного или некорректно сформированного запроса. */
};

/*!
 * \struct QueryParameters
 * \brief Хранит разобранные параметры для различных типов запросов к базе данных.
 *
 * Содержит поля для критериев фильтрации (используемых в WHERE-подобных условиях
 * команд SELECT, DELETE, EDIT, CALCULATE_CHARGES), поля для данных новой или
 * изменяемой записи (для ADD, EDIT SET), имя файла для LOAD/SAVE, а также флаги,
 * указывающие, какие из критериев или полей данных активны/установлены в запросе.
 */
struct QueryParameters {
    // --- Поля критериев (для условий типа WHERE) ---
    std::string criteriaName{};             /*!< Критерий фильтрации по имени абонента. */
    IPAddress criteriaIpAddress{};          /*!< Критерий фильтрации по IP-адресу. */
    Date criteriaDate{};                    /*!< Критерий фильтрации по дате записи. */
    Date criteriaStartDate{};               /*!< Начальная дата для периода (например, в CALCULATE_CHARGES). */
    Date criteriaEndDate{};                 /*!< Конечная дата для периода (например, в CALCULATE_CHARGES). */

    // --- Поля данных (для команды ADD или секции SET в EDIT) ---
    std::string subscriberNameData{};       /*!< Имя абонента для добавления/редактирования. */
    IPAddress ipAddressData{};              /*!< IP-адрес для добавления/редактирования. */
    Date dateData{};                        /*!< Дата записи для добавления/редактирования. */
    std::vector<double> trafficInData{};    /*!< Данные о входящем трафике по часам для ADD/EDIT. */
    std::vector<double> trafficOutData{};   /*!< Данные об исходящем трафике по часам для ADD/EDIT. */

    // --- Поля для команд LOAD/SAVE ---
    std::string filename{};                 /*!< Имя файла для операций LOAD или SAVE на сервере. */

    // --- Флаги, указывающие, какие критерии или данные активны/установлены ---
    bool useNameFilter = false;             /*!< `true`, если критерий по имени абонента активен. */
    bool useIpFilter = false;               /*!< `true`, если критерий по IP-адресу активен. */
    bool useDateFilter = false;             /*!< `true`, если критерий по дате записи активен. */
    bool useStartDateFilter = false;        /*!< `true`, если начальная дата периода установлена. */
    bool useEndDateFilter = false;          /*!< `true`, если конечная дата периода установлена. */

    // --- Данные для секции SET команды EDIT ---
    std::map<std::string, std::string> setData{}; /*!< Карта "имя поля" -> "строковое значение" для полей FIO, IP, DATE в секции SET команды EDIT. */
    bool hasTrafficInToSet = false;         /*!< `true`, если в секции SET команды EDIT указан блок TRAFFIC_IN для изменения. */
    bool hasTrafficOutToSet = false;        /*!< `true`, если в секции SET команды EDIT указан блок TRAFFIC_OUT для изменения. */

    /*! \brief Сбрасывает все параметры структуры в их состояние по умолчанию или пустое состояние. */
    void reset() noexcept {
        criteriaName.clear();
        criteriaIpAddress = IPAddress(); // Конструктор по умолчанию для IPAddress (0.0.0.0)
        criteriaDate = Date();           // Конструктор по умолчанию для Date (01.01.1970)
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

    /*!
     * \brief Конструктор. Гарантирует инициализацию всех полей значениями по умолчанию
     * путем вызова `reset()`.
     */
    QueryParameters() { // Не может быть noexcept из-за конструкторов IPAddress/Date, если они могут бросать исключения (хотя наши noexcept)
        reset();
    }
};

/*!
 * \struct Query
 * \brief Представляет полностью разобранный запрос к базе данных.
 * Содержит тип запроса (`QueryType`), разобранные параметры (`QueryParameters`)
 * и оригинальную строку запроса для логирования или отладки.
 */
struct Query {
    QueryType type = QueryType::UNKNOWN;    /*!< Тип выполненного запроса. */
    QueryParameters params{};               /*!< Разобранные параметры запроса. */
    std::string originalQueryString{};      /*!< Оригинальная строка запроса, полученная от клиента. Полезна для логирования. */
};

/*!
 * \class QueryParser
 * \brief Разбирает (парсит) необработанные строки запросов в структурированные объекты `Query`.
 *
 * Класс является stateless (не хранит состояние между вызовами `parseQuery`)
 * и может быть использован многократно для разбора различных строк запросов.
 * Обрабатывает различные синтаксические конструкции, ключевые слова и параметры,
 * специфичные для каждого типа запроса. Использует токенизацию с учетом строк в кавычках.
 * В случае синтаксических ошибок или неизвестных команд выбрасывает `std::runtime_error`.
 * Класс помечен как `final`, так как не предназначен для наследования.
 */
class QueryParser final {
private:
    /*!
     * \brief Разбивает строку запроса на отдельные лексемы (токены).
     * Учитывает строки, заключенные в двойные кавычки, как единый токен.
     * Пробельные символы вне кавычек служат разделителями.
     * \param queryString Необработанная строка запроса.
     * \return Вектор строковых токенов.
     * \throw std::runtime_error при ошибке токенизации (например, незакрытая двойная кавычка).
     */
    std::vector<std::string> tokenize(const std::string& queryString) const;

    /*!
     * \brief Вспомогательный метод для парсинга блока из `HOURS_IN_DAY` значений трафика.
     * Используется для разбора секций `TRAFFIC_IN` и `TRAFFIC_OUT` в командах `ADD` и `EDIT SET`.
     * Значения трафика должны быть неотрицательными числами.
     * \param tokens Вектор токенов, полученный от `tokenize`.
     * \param traffic_vector Вектор (выходной параметр), в который сохраняются разобранные значения трафика.
     * \param currentIndex Текущий индекс в векторе `tokens` (передается по ссылке и обновляется функцией). Указывает на первый токен значения трафика.
     * \param commandName Имя основной команды (например, "ADD", "EDIT SET") для использования в сообщениях об ошибках.
     * \param traffic_type_name Описание типа трафика ("TRAFFIC_IN" или "TRAFFIC_OUT") для сообщений об ошибках.
     * \throw std::runtime_error при синтаксических ошибках: недостаточно значений, нечисловые значения, отрицательные значения.
     */
    void parseTrafficBlock(const std::vector<std::string>& tokens,
                           std::vector<double>& traffic_vector,
                           size_t& currentIndex,
                           const std::string& commandName,
                           const std::string& traffic_type_name) const;

    /*!
     * \brief Разбирает параметры для запроса `ADD`.
     * Ожидает ключевые слова `FIO`, `IP`, `DATE`, `TRAFFIC_IN`, `TRAFFIC_OUT`, за которыми следуют их значения.
     * Опционально завершается ключевым словом `END`.
     * \param tokens Вектор токенов.
     * \param params Структура `QueryParameters` для сохранения разобранных параметров (выходной параметр).
     * \param currentIndex Текущий индекс в `tokens` (вход/выход).
     * \throw std::runtime_error при синтаксических ошибках.
     */
    void parseAddParams(const std::vector<std::string>& tokens, QueryParameters& params, size_t& currentIndex) const;

    /*!
     * \brief Разбирает параметры критериев для команд `SELECT`, `DELETE` или начальной части (условия WHERE) команды `EDIT`.
     * Ожидает пары "ключевое слово критерия" (`FIO`, `IP`, `DATE`) - "значение критерия".
     * Завершается при обнаружении ключевого слова `END`, `SET` (для `EDIT`) или конца токенов.
     * \param tokens Вектор токенов.
     * \param params Структура `QueryParameters` для сохранения разобранных критериев (выходной параметр).
     * \param currentIndex Текущий индекс в `tokens` (вход/выход).
     * \throw std::runtime_error при синтаксических ошибках.
     */
    void parseCriteriaParams(const std::vector<std::string>& tokens, QueryParameters& params, size_t& currentIndex) const;

    /*!
     * \brief Разбирает параметры секции `SET` для запроса `EDIT`.
     * Ожидает ключевое слово `SET`, за которым следуют пары "имя поля для изменения" - "новое значение".
     * Поддерживаемые поля: `FIO`, `IP`, `DATE`, `TRAFFIC_IN`, `TRAFFIC_OUT`.
     * Опционально завершается ключевым словом `END`.
     * \param tokens Вектор токенов.
     * \param params Структура `QueryParameters` для сохранения данных для изменения (выходной параметр).
     * \param currentIndex Текущий индекс в `tokens` (вход/выход). Должен указывать на токен `SET` при вызове.
     * \throw std::runtime_error при синтаксических ошибках (например, неизвестное поле, отсутствие значения, пустая секция SET).
     */
    void parseEditSetParams(const std::vector<std::string>& tokens, QueryParameters& params, size_t& currentIndex) const;

    /*!
     * \brief Разбирает параметры для запроса `CALCULATE_CHARGES`.
     * Сначала разбирает опциональные критерии фильтрации записей (используя `parseCriteriaParams`).
     * Затем ожидает обязательные параметры `START_DATE` и `END_DATE`.
     * Опционально завершается ключевым словом `END`.
     * \param tokens Вектор токенов.
     * \param params Структура `QueryParameters` для сохранения разобранных параметров (выходной параметр).
     * \param currentIndex Текущий индекс в `tokens` (вход/выход).
     * \throw std::runtime_error при синтаксических ошибках (например, отсутствие `START_DATE` или `END_DATE`).
     */
    void parseCalculateChargesParams(const std::vector<std::string>& tokens, QueryParameters& params, size_t& currentIndex) const;

public:
    /*! \brief Конструктор по умолчанию. */
    QueryParser() = default;

    /*!
     * \brief Основной метод для разбора (парсинга) необработанной строки запроса.
     * Определяет тип команды и вызывает соответствующие приватные методы для разбора ее параметров.
     * \param queryString Строка запроса для разбора.
     * \return Структурированный объект `Query`, содержащий тип запроса и его параметры.
     * Если строка запроса пуста, возвращает `Query` с типом `UNKNOWN`.
     * \throw std::runtime_error если разбор не удался из-за синтаксических ошибок,
     * неизвестных команд или некорректных параметров.
     */
    Query parseQuery(const std::string& queryString) const;
};

#endif // QUERY_PARSER_H
