// Предполагаемый путь: src/core/database.h
#ifndef DATABASE_H
#define DATABASE_H

#include "provider_record.h" // Предполагаемый путь: src/core/provider_record.h
#include "tariff_plan.h"     // Предполагаемый путь: src/core/tariff_plan.h
#include "common_defs.h"     // Предполагаемый путь: src/common_defs.h
#include <vector>
#include <string>
#include <stdexcept> // Для std::out_of_range, std::runtime_error
#include <algorithm> // Для std::remove_if, std::sort, std::unique
#include <iostream>  // Для std::ostream в load/save
#include <fstream>   // Для std::ifstream, std::ofstream в load/save

/*!
 * \class Database
 * \brief Управляет коллекцией записей ProviderRecord.
 *
 * Предоставляет функциональность для загрузки, сохранения, добавления,
 * поиска, редактирования и удаления записей, а также для расчета начислений.
 * Класс помечен как final, так как не предназначен для наследования.
 */
class Database final {
private:
    std::vector<ProviderRecord> records_{}; /*!< Вектор, хранящий все записи ProviderRecord. */
    std::string currentFilename_{};         /*!< Полный канонический путь к файлу, из которого была последняя успешная загрузка или в который было последнее успешное сохранение. */

public:
    /*! \brief Конструктор по умолчанию. Инициализирует пустую базу данных. */
    Database() noexcept;

    /*!
     * \brief Загружает записи провайдера из указанного файла.
     * Очищает все существующие записи перед загрузкой.
     * При успешной загрузке обновляет currentFilename_ на полный канонический путь к файлу.
     * \param filename Полный путь к файлу данных.
     * \param log_os Поток для вывода сообщений о ходе загрузки и ошибках (например, если файл не открылся).
     * Подробные ошибки парсинга отдельных записей выводятся в std::cerr (или Logger::error).
     * \return true, если файл был успешно открыт и предпринята попытка чтения (даже если не все записи корректны).
     * false, если файл не удалось открыть.
     */
    bool loadFromFile(const std::string& filename, std::ostream& log_os);

    /*!
     * \brief Сохраняет текущие записи провайдера в указанный файл.
     * При успешном сохранении обновляет currentFilename_ на полный канонический путь к файлу.
     * \param filename_param Полный путь к файлу данных. Если пустой, используется currentFilename_.
     * \param log_os Поток для вывода сообщения об успехе или ошибке сохранения.
     * \return true, если сохранение прошло успешно, иначе false.
     * \throw std::runtime_error если filename_param пуст и currentFilename_ также пуст.
     */
    bool saveToFile(const std::string& filename_param, std::ostream& log_os);

    /*!
     * \brief Сохраняет текущие записи провайдера в файл, указанный в currentFilename_.
     * Используется, когда команда SAVE вызывается без указания имени файла.
     * \param log_os Поток для вывода сообщения об успехе или ошибке сохранения.
     * \return true, если сохранение прошло успешно, иначе false.
     * \throw std::runtime_error если currentFilename_ пуст.
     */
    bool saveToFile(std::ostream& log_os);


    /*! \brief Добавляет новую запись в базу данных. \param record Запись для добавления. */
    void addRecord(const ProviderRecord& record);

    /*! \brief Получает константную ссылку на запись по индексу. \param index Индекс записи. \return Константная ссылка на ProviderRecord. \throw std::out_of_range если индекс некорректен. */
    const ProviderRecord& getRecordByIndex(size_t index) const;
    /*! \brief Получает неконстантную ссылку на запись по индексу для редактирования. \param index Индекс записи. \return Ссылка на ProviderRecord. \throw std::out_of_range если индекс некорректен. */
    ProviderRecord& getRecordByIndexForEdit(size_t index);
    /*! \brief Заменяет запись по указанному индексу новой записью. \param index Индекс записи для редактирования. \param updatedRecord Новая запись. \throw std::out_of_range если индекс некорректен. */
    void editRecord(size_t index, const ProviderRecord& updatedRecord);

    /*! \brief Находит индексы записей по имени абонента. \param name Имя абонента. \return Вектор индексов. */
    std::vector<size_t> findRecordsBySubscriberName(const std::string& name) const;
    /*! \brief Находит индексы записей по IP-адресу. \param ip IP-адрес. \return Вектор индексов. */
    std::vector<size_t> findRecordsByIpAddress(const IPAddress& ip) const;
    /*! \brief Находит индексы записей по дате. \param date Дата. \return Вектор индексов. */
    std::vector<size_t> findRecordsByDate(const Date& date) const;

    /*!
     * \brief Находит индексы записей по комбинации критериев.
     * \param name Критерий по имени абонента.
     * \param useNameFilter true, если использовать критерий по имени.
     * \param ip Критерий по IP-адресу.
     * \param useIpFilter true, если использовать критерий по IP.
     * \param recordDate Критерий по дате записи.
     * \param useDateFilter true, если использовать критерий по дате.
     * \return Вектор индексов записей, удовлетворяющих всем активным критериям.
     */
    std::vector<size_t> findRecordsByCriteria(
        const std::string& name, bool useNameFilter,
        const IPAddress& ip, bool useIpFilter,
        const Date& recordDate, bool useDateFilter) const;

    /*! \brief Удаляет записи по имени абонента. \param name Имя абонента. \return Количество удаленных записей. */
    size_t deleteRecordsBySubscriberName(const std::string& name);
    /*! \brief Удаляет записи по IP-адресу. \param ip IP-адрес. \return Количество удаленных записей. */
    size_t deleteRecordsByIpAddress(const IPAddress& ip);
    /*! \brief Удаляет записи по дате. \param date Дата. \return Количество удаленных записей. */
    size_t deleteRecordsByDate(const Date& date);
    /*!
     * \brief Удаляет записи по списку их индексов.
     * Индексы должны быть валидными и уникальными. Список сортируется для корректного удаления.
     * \param indices Вектор индексов для удаления (может быть изменен функцией).
     * \return Количество фактически удаленных записей.
     */
    size_t deleteRecordsByIndices(std::vector<size_t>& indices);

    /*!
     * \brief Рассчитывает стоимость услуг для одной записи за указанный период.
     * Учитывает, что дата самой записи должна попадать в указанный период [startDate, endDate].
     * \param record Запись ProviderRecord.
     * \param plan Тарифный план.
     * \param startDate Начальная дата периода для расчета.
     * \param endDate Конечная дата периода для расчета.
     * \return Рассчитанная стоимость. 0.0, если запись не попадает в период или тарифы нулевые.
     * \throw std::out_of_range если TariffPlan не может предоставить тариф для какого-либо часа.
     */
    double calculateChargesForRecord(const ProviderRecord& record,
                                     const TariffPlan& plan,
                                     const Date& startDate,
                                     const Date& endDate) const;

    /*! \brief Получает константную ссылку на все записи в базе данных. \return Вектор всех ProviderRecord. */
    const std::vector<ProviderRecord>& getAllRecords() const noexcept { return records_; }
    /*! \brief Получает количество записей в базе данных. \return Количество записей. */
    size_t getRecordCount() const noexcept { return records_.size(); }
    /*! \brief Очищает все записи из базы данных и сбрасывает currentFilename_. */
    void clearAllRecords() noexcept;
    /*! \brief Получает имя файла, с которым база данных работала в последний раз (полный путь). \return Строка с именем файла. */
    std::string getCurrentFilename() const noexcept { return currentFilename_; }
};

#endif // DATABASE_H
