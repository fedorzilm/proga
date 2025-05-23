/*!
 * \file database.h
 * \brief Определяет класс Database для управления коллекцией записей ProviderRecord.
 *
 * Класс Database является центральным компонентом для хранения и манипулирования данными
 * интернет-провайдера. Он предоставляет функциональность для загрузки записей из файла,
 * сохранения их в файл, добавления новых записей, поиска существующих записей по различным
 * критериям, редактирования и удаления записей, а также для расчета начислений
 * на основе тарифного плана.
 */
#ifndef DATABASE_H
#define DATABASE_H

#include "provider_record.h" // Класс для представления одной записи
#include "tariff_plan.h"     // Класс для тарифного плана, используется при расчете начислений
#include "common_defs.h"     // Общие определения, включая <vector>, <string>, <stdexcept>, <iostream>, <fstream>, <algorithm>, <filesystem>, <tuple>

#include <vector>
#include <string>
#include <stdexcept> 
#include <algorithm> 
#include <iostream>  
#include <fstream>   
#include <filesystem> // Для работы с путями и канонизации
#include <tuple>      // Для FileOperationResult (хотя структура FileOperationResult была определена явно)

/*!
 * \struct FileOperationResult
 * \brief Структура для хранения результата файловой операции (загрузка/сохранение).
 *
 * Содержит флаг успеха операции, сообщение для пользователя, количество обработанных
 * и пропущенных записей, а также детали возможной ошибки для внутреннего логирования.
 */
struct FileOperationResult {
    bool success = false;                /*!< `true`, если операция завершилась успешно, иначе `false`. */
    std::string user_message{};          /*!< Сообщение, предназначенное для отображения пользователю/клиенту. */
    size_t records_processed = 0;        /*!< Количество успешно загруженных или сохраненных записей. */
    size_t records_skipped = 0;          /*!< Количество записей, пропущенных при загрузке из-за ошибок формата (только для load). */
    std::string error_details{};         /*!< Дополнительные детали ошибки для внутреннего лога сервера (не для клиента). */
};


/*!
 * \class Database
 * \brief Управляет коллекцией записей `ProviderRecord`.
 *
 * Предоставляет интерфейс для выполнения CRUD-операций (Create, Read, Update, Delete)
 * над записями абонентов, а также для выполнения специфичных для домена операций,
 * таких как расчет начислений. Взаимодействует с файловой системой для персистентности данных.
 * Класс помечен как `final`, так как не предназначен для наследования.
 * \note Операции, модифицирующие базу данных (add, edit, delete, load), должны выполняться
 * под эксклюзивной блокировкой (например, `std::unique_lock<std::shared_mutex>`).
 * Операции чтения (get, find, calculate) должны выполняться под разделяемой блокировкой
 * (например, `std::shared_lock<std::shared_mutex>`).
 */
class Database final {
private:
    std::vector<ProviderRecord> records_{}; /*!< Вектор, хранящий все записи ProviderRecord в памяти. */
    std::string currentFilename_{};         /*!< Полный канонический путь к файлу, из которого была последняя успешная загрузка или в который было последнее успешное сохранение. Используется для команды SAVE без параметров. */

public:
    /*!
     * \brief Конструктор по умолчанию.
     * Инициализирует пустую базу данных (вектор записей пуст, имя текущего файла не установлено).
     */
    Database() noexcept;

    /*!
     * \brief Загружает записи провайдера из указанного файла.
     * Перед загрузкой все существующие записи в памяти очищаются.
     * При успешной загрузке (даже если некоторые записи были пропущены из-за ошибок формата,
     * но хотя бы одна была загружена, или файл был корректно пуст) обновляет `currentFilename_`
     * на полный канонический путь к загруженному файлу.
     * \param filename Полный или относительный путь к файлу данных.
     * \return Структура `FileOperationResult`, содержащая результат операции (успех/неуспех,
     * сообщение для пользователя, количество обработанных/пропущенных записей, детали ошибки).
     */
    FileOperationResult loadFromFile(const std::string& filename);

    /*!
     * \brief Сохраняет текущие записи провайдера в указанный файл.
     * Если файл с таким именем уже существует, он будет перезаписан.
     * При успешном сохранении обновляет `currentFilename_` на полный канонический путь к файлу.
     * \param filename_param Полный или относительный путь к файлу данных для сохранения.
     * Если пуст, поведение не определено (должно обрабатываться вызывающей стороной,
     * например, вызовом `saveToFile()` без параметров).
     * \return Структура `FileOperationResult` с результатом операции.
     */
    FileOperationResult saveToFile(const std::string& filename_param);

    /*!
     * \brief Сохраняет текущие записи провайдера в файл, путь к которому хранится в `currentFilename_`.
     * Используется, когда команда `SAVE` вызывается без указания имени файла.
     * \return Структура `FileOperationResult` с результатом операции. Если `currentFilename_` не установлен
     * (пуст), возвращает результат с ошибкой.
     */
    FileOperationResult saveToFile();


    /*!
     * \brief Добавляет новую запись `ProviderRecord` в базу данных.
     * \param record Запись для добавления.
     */
    void addRecord(const ProviderRecord& record);

    /*!
     * \brief Получает константную ссылку на запись по ее индексу в базе данных.
     * \param index Индекс запрашиваемой записи.
     * \return Константная ссылка на объект `ProviderRecord`.
     * \throw std::out_of_range если индекс выходит за пределы допустимого диапазона записей.
     */
    const ProviderRecord& getRecordByIndex(size_t index) const;
    
    /*!
     * \brief Получает неконстантную ссылку на запись по индексу для прямого редактирования (использовать с осторожностью!).
     * \deprecated Рекомендуется использовать `editRecord(size_t index, const ProviderRecord& updatedRecord)` для более контролируемого изменения.
     * Этот метод может быть удален в будущих версиях или его использование ограничено.
     * \param index Индекс записи.
     * \return Ссылка на `ProviderRecord`.
     * \throw std::out_of_range если индекс некорректен.
     */
    ProviderRecord& getRecordByIndexForEdit(size_t index);
    
    /*!
     * \brief Заменяет существующую запись по указанному индексу новой записью.
     * \param index Индекс записи, которую необходимо отредактировать.
     * \param updatedRecord Новая запись, которая заменит старую.
     * \throw std::out_of_range если индекс выходит за пределы допустимого диапазона.
     */
    void editRecord(size_t index, const ProviderRecord& updatedRecord);

    /*!
     * \brief Находит индексы всех записей, соответствующих указанному имени абонента.
     * Поиск регистрозависимый.
     * \param name Имя абонента для поиска.
     * \return Вектор индексов найденных записей. Пустой, если ничего не найдено.
     */
    std::vector<size_t> findRecordsBySubscriberName(const std::string& name) const;
    
    /*!
     * \brief Находит индексы всех записей, соответствующих указанному IP-адресу.
     * \param ip IP-адрес для поиска.
     * \return Вектор индексов найденных записей. Пустой, если ничего не найдено.
     */
    std::vector<size_t> findRecordsByIpAddress(const IPAddress& ip) const;
    
    /*!
     * \brief Находит индексы всех записей, соответствующих указанной дате.
     * \param date Дата для поиска.
     * \return Вектор индексов найденных записей. Пустой, если ничего не найдено.
     */
    std::vector<size_t> findRecordsByDate(const Date& date) const;

    /*!
     * \brief Находит индексы записей, удовлетворяющих комбинации критериев фильтрации.
     * Запись считается соответствующей, если она удовлетворяет *всем* активным (установленным) критериям.
     * Если ни один критерий не активен, метод вернет индексы всех записей (хотя это поведение может быть нежелательным и должно контролироваться вызывающим кодом).
     * \param name Критерий по имени абонента.
     * \param useNameFilter `true`, если использовать критерий по имени.
     * \param ip Критерий по IP-адресу.
     * \param useIpFilter `true`, если использовать критерий по IP.
     * \param recordDate Критерий по дате записи.
     * \param useDateFilter `true`, если использовать критерий по дате.
     * \return Вектор индексов записей, удовлетворяющих всем активным критериям.
     */
    std::vector<size_t> findRecordsByCriteria(
        const std::string& name, bool useNameFilter,
        const IPAddress& ip, bool useIpFilter,
        const Date& recordDate, bool useDateFilter) const;

    // Методы удаления по конкретным полям оставлены, но в командах обычно используется deleteRecordsByIndices
    // после предварительного поиска через findRecordsByCriteria.
    /*! \brief Удаляет записи по имени абонента. \param name Имя абонента. \return Количество удаленных записей. */
    size_t deleteRecordsBySubscriberName(const std::string& name);
    /*! \brief Удаляет записи по IP-адресу. \param ip IP-адрес. \return Количество удаленных записей. */
    size_t deleteRecordsByIpAddress(const IPAddress& ip);
    /*! \brief Удаляет записи по дате. \param date Дата. \return Количество удаленных записей. */
    size_t deleteRecordsByDate(const Date& date);
    
    /*!
     * \brief Удаляет записи по списку их индексов.
     * Индексы в предоставленном векторе должны быть валидными на момент вызова.
     * Список индексов сортируется и из него удаляются дубликаты перед удалением записей
     * в порядке убывания индексов для корректности операции `erase` на `std::vector`.
     * \param indices Вектор индексов записей для удаления. Содержимое вектора может быть изменено функцией.
     * \return Количество фактически удаленных записей.
     */
    size_t deleteRecordsByIndices(std::vector<size_t>& indices);

    /*!
     * \brief Рассчитывает стоимость интернет-услуг для одной конкретной записи за указанный период.
     * Расчет производится на основе данных о трафике из `record` и тарифов из `plan`.
     * Учитывается, что дата самой записи (`record.getDate()`) должна попадать в указанный период `[startDate, endDate]`.
     * \param record Запись `ProviderRecord`, для которой производится расчет.
     * \param plan Тарифный план, содержащий почасовые ставки.
     * \param startDate Начальная дата периода для расчета (включительно).
     * \param endDate Конечная дата периода для расчета (включительно).
     * \return Рассчитанная стоимость. Возвращает 0.0, если запись не попадает в указанный период,
     * если данные о трафике в записи некорректны, или если произошла ошибка при получении тарифов.
     * \throw std::out_of_range если `TariffPlan::getCostInForHour` или `TariffPlan::getCostOutForHour`
     * выбрасывают это исключение (например, из-за некорректного часа, хотя это не должно происходить при правильном цикле).
     */
    double calculateChargesForRecord(const ProviderRecord& record,
                                     const TariffPlan& plan,
                                     const Date& startDate,
                                     const Date& endDate) const;

    /*! \brief Получает константную ссылку на все записи в базе данных. \return Константная ссылка на вектор всех `ProviderRecord`. */
    const std::vector<ProviderRecord>& getAllRecords() const noexcept { return records_; }
    
    /*! \brief Получает текущее количество записей в базе данных. \return Количество записей. */
    size_t getRecordCount() const noexcept { return records_.size(); }
    
    /*! \brief Очищает все записи из базы данных и сбрасывает имя текущего файла (`currentFilename_`). */
    void clearAllRecords() noexcept;
    
    /*! 
     * \brief Получает имя файла (полный канонический путь), с которым база данных работала в последний раз (через `loadFromFile` или `saveToFile`). 
     * \return Строка с полным путем к файлу. Пустая строка, если файл не был загружен или сохранен.
     */
    std::string getCurrentFilename() const noexcept { return currentFilename_; }
};

#endif // DATABASE_H
