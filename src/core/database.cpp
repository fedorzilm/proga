/*!
 * \file database.cpp
 * \author Fedor Zilnitskiy
 * \brief Реализация класса Database для управления коллекцией записей ProviderRecord.
 */
#include "database.h"
#include "logger.h"
#include <filesystem>   // Для std::filesystem::weakly_canonical, std::filesystem::path
#include <algorithm>    // Для std::sort, std::unique, std::remove_if
#include <sstream>      // Для std::ostringstream (в сообщениях об ошибках)

/*!
 * \brief Конструктор Database.
 */
Database::Database() noexcept : records_(), currentFilename_("") {
    Logger::debug("Database: Экземпляр создан (конструктор по умолчанию).");
}

/*!
 * \brief Загрузка данных из файла.
 * \param filename Имя файла.
 * \return Структура FileOperationResult с результатом.
 */
FileOperationResult Database::loadFromFile(const std::string& filename) {
    FileOperationResult result;
    result.success = false;

    Logger::info("Database Load: Попытка загрузки данных из файла: '" + filename + "'");
    std::ifstream inFile(filename);
    if (!inFile.is_open()) {
        result.user_message = "Ошибка [БД]: Не удалось открыть файл данных \"" + filename + "\" для загрузки.";
        result.error_details = "File open failed: " + filename;
        Logger::error("Database Load: " + result.error_details);
        return result;
    }

    records_.clear(); // Очищаем текущие записи перед загрузкой новых
    ProviderRecord tempRecord;
    unsigned long attemptedToRead = 0;
    std::string line_buffer_for_skip; // Для пропуска "сломанных" строк
    std::ostringstream skipped_log_stream; // Для сбора информации о первых нескольких пропущенных записях
    const int MAX_DETAILED_SKIPPED_ERRORS = 3; // Макс. кол-во ошибок формата для детального лога в error_details

    int line_num_approx = 0; // Приблизительный номер строки для логов

    // Цикл чтения записей из файла
    while (true) {
        // Пропускаем пустые строки между записями
        char next_char;
        while ((next_char = static_cast<char>(inFile.peek())) == '\n' || next_char == '\r') {
            if(!inFile.ignore()){ // Если ignore не удался (например, EOF)
                goto end_loop; // Используем goto для выхода из вложенных циклов/условий
            }
            line_num_approx++;
        }
        if (inFile.eof() || !inFile.good()) break; // Достигли конца или ошибка после пропуска

        attemptedToRead++;
        line_num_approx++; // Приблизительно для каждой новой записи

        if (inFile >> tempRecord) { // Используем operator>> для ProviderRecord
            records_.push_back(tempRecord);
            result.records_processed++;
        } else { // Ошибка чтения записи
            if (inFile.eof()) { // Если ошибка произошла из-за конца файла (например, неполная последняя запись)
                if (attemptedToRead == result.records_processed + 1 && records_.empty() && attemptedToRead == 1) {
                    Logger::info("Database Load: Файл '" + filename + "' пуст или не содержит валидных записей.");
                } else if (attemptedToRead > result.records_processed){ // Неполная запись в конце
                     Logger::warn("Database Load: Обнаружена неполная запись в конце файла '" + filename + "' (строка ~" + std::to_string(line_num_approx) + ").");
                     result.records_skipped++;
                     if(result.records_skipped <= MAX_DETAILED_SKIPPED_ERRORS) {
                        skipped_log_stream << "Запись #" << attemptedToRead << " (неполная в EOF). ";
                        if (!result.error_details.empty()) result.error_details += "; ";
                        result.error_details += "Record #" + std::to_string(attemptedToRead) + " incomplete at EOF (near line ~" + std::to_string(line_num_approx) + ")";
                     }
                }
                break; // Выход из цикла чтения, если EOF
            }
            if (inFile.bad()){ // Критическая ошибка IO
                result.user_message = "Критическая ошибка чтения файла данных (IO error) во время разбора записи #" + std::to_string(attemptedToRead) + " в \"" + filename + "\". Загрузка прервана.";
                result.error_details = "IO error while reading record #" + std::to_string(attemptedToRead) + " from " + filename;
                Logger::error("Database Load: " + result.error_details);
                result.records_skipped = attemptedToRead - result.records_processed; // Все оставшиеся считаем пропущенными
                inFile.close();
                return result;
            }

            // Если не EOF и не BAD, значит ошибка формата в текущей записи
            std::string skip_msg_detail = "Ошибка формата данных: Запись #" +
                                   std::to_string(attemptedToRead) + " в файле \"" + filename +
                                   "\" (строка ~" + std::to_string(line_num_approx) +") не может быть полностью прочитана/разобрана. Запись пропущена.";
            Logger::warn("Database Load: " + skip_msg_detail);
            result.records_skipped++;
            if(result.records_skipped <= MAX_DETAILED_SKIPPED_ERRORS) { // Ограничиваем количество деталей
                skipped_log_stream << "Запись #" << attemptedToRead << " (ошибка формата). ";
                if (!result.error_details.empty()) result.error_details += "; ";
                result.error_details += "Record #" + std::to_string(attemptedToRead) + " format error (near line ~" + std::to_string(line_num_approx) + ")";
            }

            inFile.clear(); // Сбрасываем флаги ошибок потока (например, failbit), чтобы можно было продолжить
            if (!std::getline(inFile, line_buffer_for_skip)) { // Читаем до конца текущей (возможно, битой) строки
                 if (!inFile.eof()) { // Если это не конец файла, значит ошибка при пропуске
                    std::string critical_skip_err = " Критическая ошибка при попытке пропустить некорректную запись #" + std::to_string(attemptedToRead) + ".";
                    result.error_details += critical_skip_err;
                    Logger::error("Database Load: " + critical_skip_err);
                 }
                 break; // Выход из цикла, если не удалось пропустить строку или EOF
            }
        }
    }
end_loop:;

    inFile.close();

    std::ostringstream user_msg_oss;
    user_msg_oss << "Загрузка из файла \"" << filename << "\" завершена. ";
    user_msg_oss << "Успешно загружено записей: " << result.records_processed << ".";
    if (result.records_skipped > 0) {
        user_msg_oss << " Пропущено из-за ошибок формата или неполных данных: " << result.records_skipped << ".";
    }
    result.user_message = user_msg_oss.str();

    std::string log_details_skipped = skipped_log_stream.str();
    Logger::info("Database Load: " + result.user_message +
                 (log_details_skipped.empty() ? "" : " Детали первых пропущенных: " + log_details_skipped));

    if (result.error_details.find("IO error") == std::string::npos && result.error_details.find("File open failed") == std::string::npos ) {
        result.success = true;
        std::filesystem::path canonical_path;
        try {
            if (std::filesystem::exists(filename)) {
                 canonical_path = std::filesystem::canonical(filename);
            } else {
                 canonical_path = std::filesystem::weakly_canonical(filename);
            }
            currentFilename_ = canonical_path.string();
            Logger::info("Database: currentFilename_ обновлен на: '" + currentFilename_ + "' после операции LOAD.");
        } catch (const std::filesystem::filesystem_error& fs_err) {
            Logger::error("Database Load: Ошибка канонизации пути '" + filename + "' для currentFilename_: " + fs_err.what() + ". Имя текущего файла не будет обновлено, используется исходное имя.");
            currentFilename_ = filename;
        }
    } else {
        result.success = false;
    }

    return result;
}

/*!
 * \brief Сохранение данных в файл.
 * \param filename_param Имя файла.
 * \return Структура FileOperationResult с результатом.
 */
FileOperationResult Database::saveToFile(const std::string& filename_param) {
    FileOperationResult result;
    result.success = false;

    if (filename_param.empty()) {
        result.user_message = "Ошибка [БД]: Имя файла для сохранения не указано.";
        result.error_details = "Save failed: filename_param is empty.";
        Logger::error("Database Save: " + result.error_details);
        return result;
    }

    Logger::info("Database Save: Попытка сохранения данных в файл: '" + filename_param + "'");
    std::ofstream outFile(filename_param);
    if (!outFile.is_open()) {
        result.user_message = "Ошибка [БД]: Не удалось открыть файл данных \"" + filename_param + "\" для сохранения.";
        result.error_details = "File open for writing failed: " + filename_param;
        Logger::error("Database Save: " + result.error_details);
        return result;
    }

    for (size_t i = 0; i < records_.size(); ++i) {
        outFile << records_[i];
        if (i < records_.size() - 1) {
            outFile << "\n";
        }
    }
    result.records_processed = records_.size();

    if (outFile.bad()) {
        result.user_message = "Ошибка [БД]: Произошла ошибка ввода-вывода при записи в файл \"" + filename_param + "\". Данные могут быть повреждены.";
        result.error_details = "IO error during writing to " + filename_param;
        Logger::error("Database Save: " + result.error_details);
        outFile.close();
        return result;
    }

    outFile.close();
    if (!outFile.good()) {
        result.user_message = "Ошибка [БД]: Не удалось корректно сохранить все данные и/или закрыть файл \"" + filename_param + "\".";
        result.error_details = "Error after closing file (e.g. flush error) " + filename_param;
        Logger::error("Database Save: " + result.error_details);
        return result;
    }

    result.success = true;
    result.user_message = "Успешно сохранено " + std::to_string(result.records_processed) + " записей в файл \"" + filename_param + "\".";
    Logger::info("Database Save: " + result.user_message);
    if (result.success) {
        std::filesystem::path canonical_path;
        try {
            if (std::filesystem::exists(filename_param)) {
                canonical_path = std::filesystem::canonical(filename_param);
            } else {
                Logger::warn("Database Save: Файл '" + filename_param + "' не найден после успешного сохранения для канонизации. Используется weakly_canonical.");
                canonical_path = std::filesystem::weakly_canonical(filename_param);
            }
            currentFilename_ = canonical_path.string();
            Logger::info("Database: currentFilename_ обновлен на: '" + currentFilename_ + "' после операции SAVE.");
        } catch (const std::filesystem::filesystem_error& fs_err) {
            Logger::error("Database Save: Ошибка канонизации пути '" + filename_param + "' для currentFilename_: " + fs_err.what() + ". Имя текущего файла не будет обновлено, используется исходное имя.");
            currentFilename_ = filename_param;
        }
    }
    return result;
}

/*!
 * \brief Сохранение данных в текущий файл (currentFilename_).
 * \return Структура FileOperationResult с результатом.
 */
FileOperationResult Database::saveToFile() {
    if (currentFilename_.empty()) {
        FileOperationResult result;
        result.success = false;
        result.user_message = "Ошибка [БД]: Имя файла для SAVE не было ранее установлено (через LOAD или SAVE с именем). Операция невозможна.";
        result.error_details = "SAVE failed: currentFilename_ is empty.";
        Logger::error("Database Save (no-arg): " + result.error_details);
        return result;
    }
    return saveToFile(currentFilename_);
}

/*!
 * \brief Добавляет запись в БД.
 * \param record Запись для добавления.
 */
void Database::addRecord(const ProviderRecord& record) {
    records_.push_back(record);
    Logger::debug("Database AddRecord: Запись для '" + record.getName() + "' добавлена. Всего записей: " + std::to_string(records_.size()));
}

/*!
 * \brief Получает запись по индексу (константная версия).
 * \param index Индекс.
 * \return Константная ссылка на запись.
 * \throw std::out_of_range Если индекс невалиден.
 */
const ProviderRecord& Database::getRecordByIndex(size_t index) const {
    if (index >= records_.size()) {
        std::string msg = "getRecordByIndex: Индекс " + std::to_string(index) + " выходит за пределы. Размер БД: " + std::to_string(records_.size());
        Logger::error("Database: " + msg);
        throw std::out_of_range(msg);
    }
    return records_.at(index);
}

/*!
 * \brief Получает запись по индексу для редактирования (неконстантная версия).
 * \deprecated Рекомендуется использовать editRecord.
 * \param index Индекс.
 * \return Ссылка на запись.
 * \throw std::out_of_range Если индекс невалиден.
 */
ProviderRecord& Database::getRecordByIndexForEdit(size_t index) {
    if (index >= records_.size()) {
        std::string msg = "getRecordByIndexForEdit: Индекс " + std::to_string(index) + " выходит за пределы для редактирования. Размер БД: " + std::to_string(records_.size());
        Logger::error("Database: " + msg);
        throw std::out_of_range(msg);
    }
    return records_.at(index);
}

/*!
 * \brief Редактирует запись по индексу.
 * \param index Индекс записи для редактирования.
 * \param updatedRecord Новые данные для записи.
 * \throw std::out_of_range Если индекс невалиден.
 */
void Database::editRecord(size_t index, const ProviderRecord& updatedRecord) {
    if (index >= records_.size()) {
        std::string msg = "editRecord: Индекс " + std::to_string(index) + " выходит за пределы для редактирования. Размер БД: " + std::to_string(records_.size());
        Logger::error("Database: " + msg);
        throw std::out_of_range(msg);
    }
    records_[index] = updatedRecord;
    Logger::debug("Database EditRecord: Запись по индексу " + std::to_string(index) + " (абонент: '" + updatedRecord.getName() + "') отредактирована.");
}

/*!
 * \brief Поиск записей по имени абонента.
 * \param name Имя для поиска.
 * \return Вектор индексов найденных записей.
 */
std::vector<size_t> Database::findRecordsBySubscriberName(const std::string& name) const {
    std::vector<size_t> indices;
    for (size_t i = 0; i < records_.size(); ++i) {
        if (records_[i].getName() == name) {
            indices.push_back(i);
        }
    }
    Logger::debug("Database FindByName: Найдено " + std::to_string(indices.size()) + " записей для имени '" + name + "'.");
    return indices;
}

/*!
 * \brief Поиск записей по IP-адресу.
 * \param ip IP-адрес для поиска.
 * \return Вектор индексов найденных записей.
 */
std::vector<size_t> Database::findRecordsByIpAddress(const IPAddress& ip) const {
    std::vector<size_t> indices;
    for (size_t i = 0; i < records_.size(); ++i) {
        if (records_[i].getIpAddress() == ip) {
            indices.push_back(i);
        }
    }
    Logger::debug("Database FindByIp: Найдено " + std::to_string(indices.size()) + " записей для IP " + ip.toString() + ".");
    return indices;
}

/*!
 * \brief Поиск записей по дате.
 * \param date Дата для поиска.
 * \return Вектор индексов найденных записей.
 */
std::vector<size_t> Database::findRecordsByDate(const Date& date) const {
    std::vector<size_t> indices;
    for (size_t i = 0; i < records_.size(); ++i) {
        if (records_[i].getDate() == date) {
            indices.push_back(i);
        }
    }
    Logger::debug("Database FindByDate: Найдено " + std::to_string(indices.size()) + " записей для даты " + date.toString() + ".");
    return indices;
}

/*!
 * \brief Поиск записей по комбинации критериев.
 * \param name Имя.
 * \param useNameFilter Использовать ли фильтр по имени.
 * \param ip IP-адрес.
 * \param useIpFilter Использовать ли фильтр по IP.
 * \param recordDate Дата записи.
 * \param useDateFilter Использовать ли фильтр по дате.
 * \return Вектор индексов найденных записей.
 */
std::vector<size_t> Database::findRecordsByCriteria(
    const std::string& name, bool useNameFilter,
    const IPAddress& ip, bool useIpFilter,
    const Date& recordDate, bool useDateFilter) const {
    std::vector<size_t> indices;
    for (size_t i = 0; i < records_.size(); ++i) {
        bool match = true;
        if (useNameFilter && records_[i].getName() != name) {
            match = false;
        }
        if (match && useIpFilter && records_[i].getIpAddress() != ip) {
            match = false;
        }
        if (match && useDateFilter && records_[i].getDate() != recordDate) {
            match = false;
        }

        if (match) {
            indices.push_back(i);
        }
    }
    Logger::debug("Database FindByCriteria: Найдено " + std::to_string(indices.size()) + " записей по заданным критериям.");
    return indices;
}

/*! \brief Удаляет записи по имени абонента. */
size_t Database::deleteRecordsBySubscriberName(const std::string& name) {
    size_t original_size = records_.size();
    records_.erase(std::remove_if(records_.begin(), records_.end(),
                                 [&](const ProviderRecord& rec) { return rec.getName() == name; }),
                  records_.end());
    size_t count_deleted = original_size - records_.size();
    if (count_deleted > 0) {
        Logger::info("Database DeleteByName: Удалено " + std::to_string(count_deleted) + " записей для имени '" + name + "'.");
    }
    return count_deleted;
}

/*! \brief Удаляет записи по IP-адресу. */
size_t Database::deleteRecordsByIpAddress(const IPAddress& ip) {
    size_t original_size = records_.size();
    records_.erase(std::remove_if(records_.begin(), records_.end(),
                                 [&](const ProviderRecord& rec) { return rec.getIpAddress() == ip; }),
                  records_.end());
    size_t count_deleted = original_size - records_.size();
     if (count_deleted > 0) {
        Logger::info("Database DeleteByIp: Удалено " + std::to_string(count_deleted) + " записей для IP " + ip.toString() + ".");
    }
    return count_deleted;
}

/*! \brief Удаляет записи по дате. */
size_t Database::deleteRecordsByDate(const Date& date) {
    size_t original_size = records_.size();
    records_.erase(std::remove_if(records_.begin(), records_.end(),
                                 [&](const ProviderRecord& rec) { return rec.getDate() == date; }),
                  records_.end());
    size_t count_deleted = original_size - records_.size();
    if (count_deleted > 0) {
        Logger::info("Database DeleteByDate: Удалено " + std::to_string(count_deleted) + " записей для даты " + date.toString() + ".");
    }
    return count_deleted;
}

/*!
 * \brief Удаляет записи по списку их индексов.
 * \param indices Вектор индексов (будет отсортирован).
 * \return Количество удаленных записей.
 */
size_t Database::deleteRecordsByIndices(std::vector<size_t>& indices) {
    if (indices.empty() || records_.empty()) {
        Logger::debug("Database DeleteByIndices: Список индексов пуст или база данных пуста. Удалено 0 записей.");
        return 0;
    }

    indices.erase(std::remove_if(indices.begin(), indices.end(),
                                 [this](size_t idx){ return idx >= records_.size(); }),
                  indices.end());

    if (indices.empty()) {
        Logger::debug("Database DeleteByIndices: Все предоставленные индексы были невалидны или отфильтрованы. Удалено 0 записей.");
        return 0;
    }

    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    std::sort(indices.rbegin(), indices.rend());

    size_t count_deleted = 0;
    std::ostringstream deleted_indices_log_stream;
    bool first_log = true;

    for (size_t index_to_delete : indices) {
        if (index_to_delete < records_.size()) {
            records_.erase(records_.begin() + static_cast<ptrdiff_t>(index_to_delete));
            count_deleted++;
            if (!first_log) deleted_indices_log_stream << ", ";
            deleted_indices_log_stream << index_to_delete;
            first_log = false;
        } else {
            Logger::warn("Database DeleteByIndices: Попытка удалить невалидный индекс " + std::to_string(index_to_delete) + " после всех фильтраций.");
        }
    }
    if (count_deleted > 0) {
        Logger::info("Database DeleteByIndices: Удалено " + std::to_string(count_deleted) + " записей по индексам: [" + deleted_indices_log_stream.str() + "].");
    } else {
        Logger::debug("Database DeleteByIndices: Не было удалено ни одной записи по предоставленным индексам.");
    }
    return count_deleted;
}

/*!
 * \brief Расчет начислений для одной записи за период.
 * \param record Запись для расчета.
 * \param plan Тарифный план.
 * \param startDate Начальная дата периода.
 * \param endDate Конечная дата периода.
 * \return Сумма начислений.
 * \throw std::out_of_range Если тариф не найден.
 */
double Database::calculateChargesForRecord(const ProviderRecord& record,
                                         const TariffPlan& plan,
                                         const Date& startDate,
                                         const Date& endDate) const {
    if (record.getDate() < startDate || record.getDate() > endDate) {
        Logger::debug("Database CalcCharges: Запись '" + record.getName() + "' от " + record.getDate().toString() +
                      " не попадает в расчетный период [" + startDate.toString() + " - " + endDate.toString() + "]. Начислено: 0.0");
        return 0.0;
    }

    double totalCharge = 0.0;
    const auto& trafficIn = record.getTrafficInByHour();
    const auto& trafficOut = record.getTrafficOutByHour();

    if (trafficIn.size() != static_cast<size_t>(HOURS_IN_DAY) || trafficOut.size() != static_cast<size_t>(HOURS_IN_DAY)) {
        Logger::error("Database CalcCharges: ОШИБКА: Запись для \"" + record.getName() + "\" от " + record.getDate().toString()
                      + " имеет некорректный размер данных о трафике (вх: " + std::to_string(trafficIn.size())
                      + ", исх: " + std::to_string(trafficOut.size()) + "). Расчет для этой записи не будет произведен (вернет 0).");
        return 0.0;
    }

    try {
        for (int hour = 0; hour < HOURS_IN_DAY; ++hour) {
            size_t hour_idx = static_cast<size_t>(hour);
            if (trafficIn[hour_idx] > DOUBLE_EPSILON) {
                 totalCharge += trafficIn[hour_idx] * plan.getCostInForHour(hour);
            }
            if (trafficOut[hour_idx] > DOUBLE_EPSILON) {
                 totalCharge += trafficOut[hour_idx] * plan.getCostOutForHour(hour);
            }
        }
    } catch (const std::out_of_range& e_tariff) {
        Logger::error("Database CalcCharges: Ошибка при расчете платежей для \"" + record.getName() + "\" от " + record.getDate().toString()
                      + " (проблема с тарифным планом или индексом часа): " + e_tariff.what());
        return 0.0;
    }
    Logger::debug("Database CalcCharges: Для '" + record.getName() + "' (" + record.getDate().toString() +
                  ") начислено: " + std::to_string(totalCharge) + " за период [" + startDate.toString() + " - " + endDate.toString() + "].");
    return totalCharge;
}

/*!
 * \brief Очищает все записи и сбрасывает currentFilename_.
 */
void Database::clearAllRecords() noexcept {
    records_.clear();
    records_.shrink_to_fit();
    currentFilename_.clear();
    Logger::info("Database: Все записи очищены, currentFilename сброшен.");
}
