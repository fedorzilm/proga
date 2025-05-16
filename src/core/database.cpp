// Предполагаемый путь: src/core/database.cpp
#include "database.h"
#include "logger.h"     // Предполагаемый путь: src/utils/logger.h
#include <filesystem>   // Для std::filesystem::weakly_canonical
#include <algorithm>    // Для std::sort, std::unique, std::remove_if

Database::Database() noexcept : records_(), currentFilename_("") {
    Logger::debug("Database: Экземпляр создан.");
}

bool Database::loadFromFile(const std::string& filename, std::ostream& log_os_param) {
    // log_os_param используется для сообщений, которые должны видеть клиент/UI при пакетной обработке.
    // Внутренние детали и ошибки уровня DEBUG/ERROR идут в Logger.
    Logger::info("Database: Попытка загрузки из файла: " + filename);
    std::ifstream inFile(filename);
    if (!inFile.is_open()) {
        log_os_param << "Ошибка [БД]: Не удалось открыть файл данных \"" << filename << "\" для загрузки.\n";
        Logger::error("Database::loadFromFile: Не удалось открыть файл: " + filename);
        // currentFilename_ не меняем, если файл не открылся
        return false;
    }

    records_.clear(); // Очищаем текущие записи
    ProviderRecord tempRecord;
    unsigned long successfullyRead = 0;
    unsigned long attemptedToRead = 0; // Счетчик строк/записей, которые пытались прочитать
    std::string line_buffer_for_skip;

    // Цикл чтения записей
    while (inFile.peek() != EOF && inFile.good()) {
        // Пропускаем начальные пустые строки между записями, если они есть
        while (inFile.peek() == '\n' || inFile.peek() == '\r') {
            if(!inFile.ignore()){ break; } // Пропустить символ
        }
        if (inFile.peek() == EOF || !inFile.good()) break; // Достигли конца или ошибка после пропуска

        attemptedToRead++;
        std::streampos pos_before_read = inFile.tellg(); // Запоминаем позицию перед чтением записи

        if (inFile >> tempRecord) { // Используем operator>> для ProviderRecord
            records_.push_back(tempRecord);
            successfullyRead++;
        } else {
            if (inFile.eof() && attemptedToRead == successfullyRead +1 && records_.empty() && pos_before_read == std::streampos(0) ) {
                // Это может быть случай, когда файл пустой или содержит только пробелы/комментарии.
                // Если файл просто пустой, inFile >> tempRecord сразу вернет false, eof может быть не сразу.
                // Если файл был пуст, successfullyRead останется 0, attemptedToRead будет 0 или 1.
                Logger::info("Database::loadFromFile: Достигнут EOF при попытке чтения записи #" + std::to_string(attemptedToRead) + ", возможно, файл пуст или содержит только пробелы.");
                // Не выводим ошибку парсинга, если это просто пустой файл.
                // Если attemptedToRead > 0 и successfullyRead == 0, значит, была попытка, но не удалась.
                if(attemptedToRead > 0 && successfullyRead == 0){ // Если первая же "запись" не считалась
                     // Это нормально для пустого файла, не логируем как ошибку парсинга
                }
                break; // Выход из цикла, если EOF после неудачного чтения
            }
            if (inFile.bad()){
                log_os_param << "Критическая ошибка чтения файла данных (IO error) во время разбора записи #" << attemptedToRead << " в \"" << filename << "\".\n";
                Logger::error("Database::loadFromFile: IO error при чтении записи #" + std::to_string(attemptedToRead) + " из " + filename);
                break; // Прерываем загрузку при IO ошибке
            }

            // Если не EOF и не BAD, значит ошибка формата
            // Не используем log_os_param для ошибок парсинга отдельных записей, т.к. это может быть многословно
            // Вместо этого используем Logger::warn или std::cerr, как было в ваших тестах
            Logger::warn("Database::loadFromFile: Ошибка формата данных (или EOF внутри записи): Запись #" +
                         std::to_string(attemptedToRead) + " в файле \"" + filename +
                         "\" не может быть полностью прочитана/разобрана. Запись пропущена.");
            // std::cerr << "Ошибка формата данных (или EOF): Запись #" << attemptedToRead ... (как в ваших тестах)

            inFile.clear(); // Сбрасываем флаги ошибок потока (failbit)
            // Пропускаем оставшуюся часть "сломанной" строки до конца или до следующей записи
            if (!std::getline(inFile, line_buffer_for_skip)) {
                 if (!inFile.eof()) {
                    log_os_param << "Критическая ошибка при попытке пропустить некорректную запись #" << attemptedToRead << " в файле \"" << filename << "\".\n";
                    Logger::error("Database::loadFromFile: Критическая ошибка при пропуске некорректной записи #" + std::to_string(attemptedToRead));
                 }
                 break;
            }
        }
    }
    inFile.close();

    log_os_param << "Загрузка из файла \"" << filename << "\" завершена сервером.\n";
    log_os_param << "Успешно загружено записей: " << successfullyRead << ".\n";
    if (attemptedToRead > successfullyRead) {
        unsigned long skipped_count = attemptedToRead - successfullyRead;
        // Если единственная попытка чтения не удалась (например, файл пуст, но не битый), это не "пропуск" из-за ошибки формата
        if (!(attemptedToRead == 1 && successfullyRead == 0 && skipped_count == 1 && records_.empty())) {
             log_os_param << "Пропущено из-за ошибок формата или неполных данных в конце файла: " << skipped_count << ".\n";
        }
    }
    Logger::info("Database::loadFromFile: Загружено " + std::to_string(successfullyRead) + " записей из " + filename +
                 ". Попыток чтения: " + std::to_string(attemptedToRead));

    if (successfullyRead > 0 || (attemptedToRead == 0 && records_.empty())) { // Успех, если что-то загружено или файл был валидно пуст
        try {
            currentFilename_ = std::filesystem::weakly_canonical(filename).string();
            Logger::info("Database: currentFilename_ обновлен на: " + currentFilename_);
        } catch (const std::filesystem::filesystem_error& fs_err) {
            Logger::error("Database::loadFromFile: Ошибка канонизации пути '" + filename + "' для currentFilename_: " + fs_err.what());
            currentFilename_ = filename; // Сохраняем как есть, если канонизация не удалась
        }
    } else if (!records_.empty()){ // Если что-то загружено, но successfullyRead == 0 (внутренняя ошибка логики)
        Logger::error("Database::loadFromFile: Внутренняя несогласованность: записи есть, но successfullyRead == 0.");
    } else { // Ничего не загружено и были попытки чтения (т.е. файл не был просто пустым)
        currentFilename_.clear(); // Сбрасываем, если загрузка была неуспешной и ничего не загружено
        Logger::warn("Database::loadFromFile: Ничего не было загружено из " + filename + ", currentFilename_ очищен.");
    }
    return true; // Возвращаем true, если файл был открыт, даже если не все записи валидны
}


bool Database::saveToFile(const std::string& filename_param, std::ostream& log_os_param) {
    std::string effective_filename = filename_param;
    if (effective_filename.empty()) { // Эта ветка не должна вызываться, если используется saveToFile(log_os_param)
        Logger::error("Database::saveToFile: Попытка сохранения с пустым filename_param, когда ожидался путь.");
        throw std::runtime_error("Внутренняя ошибка: saveToFile(filename, log) вызван с пустым filename.");
    }

    Logger::info("Database: Попытка сохранения в файл: " + effective_filename);
    std::ofstream outFile(effective_filename);
    if (!outFile.is_open()) {
        log_os_param << "Ошибка [БД]: Не удалось открыть файл данных \"" << effective_filename << "\" для сохранения.\n";
        Logger::error("Database::saveToFile: Не удалось открыть файл для записи: " + effective_filename);
        return false;
    }

    for (size_t i = 0; i < records_.size(); ++i) {
        outFile << records_[i];
        if (i < records_.size() - 1) { // Добавляем новую строку после каждой записи, КРОМЕ последней
            outFile << "\n";
        }
    }
    // Если records_ пуст, файл будет создан пустым.

    if (outFile.bad()) {
        log_os_param << "Ошибка [БД]: Произошла ошибка ввода-вывода при записи в файл \"" << effective_filename << "\".\n";
        Logger::error("Database::saveToFile: IO error во время записи в " + effective_filename);
        outFile.close();
        return false;
    }

    outFile.close();
    if (!outFile.good()) { // Проверяем состояние после закрытия
        log_os_param << "Ошибка [БД]: Не удалось корректно сохранить все данные и/или закрыть файл \"" << effective_filename << "\".\n";
        Logger::error("Database::saveToFile: Ошибка после закрытия файла " + effective_filename);
        return false;
    }

    log_os_param << "Успешно сохранено " << records_.size() << " записей сервером в \"" << effective_filename << "\".\n";
    Logger::info("Database::saveToFile: Успешно сохранено " + std::to_string(records_.size()) + " записей в " + effective_filename);
    try {
        currentFilename_ = std::filesystem::weakly_canonical(effective_filename).string();
        Logger::info("Database: currentFilename_ обновлен на: " + currentFilename_);
    } catch (const std::filesystem::filesystem_error& fs_err) {
        Logger::error("Database::saveToFile: Ошибка канонизации пути '" + effective_filename + "' для currentFilename_: " + fs_err.what());
        currentFilename_ = effective_filename; // Сохраняем как есть, если канонизация не удалась
    }
    return true;
}

bool Database::saveToFile(std::ostream& log_os_param) {
    if (currentFilename_.empty()) {
        log_os_param << "Ошибка [БД]: Имя файла для сохранения не было ранее установлено (через LOAD или SAVE с именем), и новое имя не предоставлено.\n";
        Logger::error("Database::saveToFile (без аргумента имени): currentFilename_ пуст.");
        // В соответствии с вашим `UserInterface::handleSave`, это должно бросать исключение,
        // которое будет поймано выше.
        throw std::runtime_error("Имя файла для SAVE не определено (не было LOAD или SAVE с именем).");
    }
    return saveToFile(currentFilename_, log_os_param);
}


void Database::addRecord(const ProviderRecord& record) {
    // Мьютекс должен быть взят вызывающим кодом (Server::handleClient)
    records_.push_back(record);
    Logger::debug("Database: Запись добавлена. Всего записей: " + std::to_string(records_.size()));
}

const ProviderRecord& Database::getRecordByIndex(size_t index) const {
    // Мьютекс для чтения (shared, если есть) должен быть взят вызывающим кодом
    if (index >= records_.size()) {
        std::string msg = "Индекс " + std::to_string(index) + " выходит за пределы. Размер: " + std::to_string(records_.size());
        Logger::error("Database::getRecordByIndex: " + msg);
        throw std::out_of_range(msg);
    }
    return records_.at(index);
}

ProviderRecord& Database::getRecordByIndexForEdit(size_t index) {
    // Мьютекс должен быть взят вызывающим кодом
    if (index >= records_.size()) {
        std::string msg = "Индекс " + std::to_string(index) + " выходит за пределы для редактирования. Размер: " + std::to_string(records_.size());
        Logger::error("Database::getRecordByIndexForEdit: " + msg);
        throw std::out_of_range(msg);
    }
    return records_.at(index);
}

void Database::editRecord(size_t index, const ProviderRecord& updatedRecord) {
    // Мьютекс должен быть взят вызывающим кодом
    if (index >= records_.size()) {
        std::string msg = "Индекс " + std::to_string(index) + " выходит за пределы для editRecord. Размер: " + std::to_string(records_.size());
        Logger::error("Database::editRecord: " + msg);
        throw std::out_of_range(msg);
    }
    records_[index] = updatedRecord;
    Logger::debug("Database: Запись по индексу " + std::to_string(index) + " отредактирована.");
}

std::vector<size_t> Database::findRecordsBySubscriberName(const std::string& name) const {
    // Мьютекс для чтения
    std::vector<size_t> indices;
    indices.reserve(records_.size() / 10 + 1); // Примерная пред-аллокация
    for (size_t i = 0; i < records_.size(); ++i) {
        if (records_[i].getName() == name) {
            indices.push_back(i);
        }
    }
    Logger::debug("Database::findRecordsBySubscriberName: Найдено " + std::to_string(indices.size()) + " записей для имени '" + name + "'.");
    return indices;
}

std::vector<size_t> Database::findRecordsByIpAddress(const IPAddress& ip) const {
    // Мьютекс для чтения
    std::vector<size_t> indices;
    indices.reserve(records_.size() / 10 + 1);
    for (size_t i = 0; i < records_.size(); ++i) {
        if (records_[i].getIpAddress() == ip) {
            indices.push_back(i);
        }
    }
    Logger::debug("Database::findRecordsByIpAddress: Найдено " + std::to_string(indices.size()) + " записей для IP " + ip.toString() + ".");
    return indices;
}

std::vector<size_t> Database::findRecordsByDate(const Date& date) const {
    // Мьютекс для чтения
    std::vector<size_t> indices;
    indices.reserve(records_.size() / 10 + 1);
    for (size_t i = 0; i < records_.size(); ++i) {
        if (records_[i].getDate() == date) {
            indices.push_back(i);
        }
    }
    Logger::debug("Database::findRecordsByDate: Найдено " + std::to_string(indices.size()) + " записей для даты " + date.toString() + ".");
    return indices;
}

std::vector<size_t> Database::findRecordsByCriteria(
    const std::string& name, bool useNameFilter,
    const IPAddress& ip, bool useIpFilter,
    const Date& recordDate, bool useDateFilter) const {
    // Мьютекс для чтения
    std::vector<size_t> indices;
    // Не пред-аллоцируем records_.size(), т.к. выборка может быть маленькой
    Logger::debug("Database::findRecordsByCriteria: Поиск...");
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
    Logger::debug("Database::findRecordsByCriteria: Найдено " + std::to_string(indices.size()) + " записей.");
    return indices;
}

// Методы deleteRecordsBy* не используются напрямую командами, используется deleteRecordsByIndices
size_t Database::deleteRecordsBySubscriberName(const std::string& name) {
    // Мьютекс
    size_t original_size = records_.size();
    records_.erase(std::remove_if(records_.begin(), records_.end(),
                                 [&](const ProviderRecord& rec) { return rec.getName() == name; }),
                  records_.end());
    size_t count_deleted = original_size - records_.size();
    Logger::info("Database::deleteRecordsBySubscriberName: Удалено " + std::to_string(count_deleted) + " записей для имени '" + name + "'.");
    return count_deleted;
}
// Аналогично для deleteRecordsByIpAddress и deleteRecordsByDate (с логированием)

size_t Database::deleteRecordsByIpAddress(const IPAddress& ip) {
    size_t original_size = records_.size();
    records_.erase(std::remove_if(records_.begin(), records_.end(),
                                 [&](const ProviderRecord& rec) { return rec.getIpAddress() == ip; }),
                  records_.end());
    size_t count_deleted = original_size - records_.size();
    Logger::info("Database::deleteRecordsByIpAddress: Удалено " + std::to_string(count_deleted) + " записей для IP " + ip.toString() + ".");
    return count_deleted;
}

size_t Database::deleteRecordsByDate(const Date& date) {
    size_t original_size = records_.size();
    records_.erase(std::remove_if(records_.begin(), records_.end(),
                                 [&](const ProviderRecord& rec) { return rec.getDate() == date; }),
                  records_.end());
    size_t count_deleted = original_size - records_.size();
    Logger::info("Database::deleteRecordsByDate: Удалено " + std::to_string(count_deleted) + " записей для даты " + date.toString() + ".");
    return count_deleted;
}


size_t Database::deleteRecordsByIndices(std::vector<size_t>& indices) {
    // Мьютекс
    if (indices.empty() || records_.empty()) {
        Logger::debug("Database::deleteRecordsByIndices: Список индексов пуст или база данных пуста. Удалено 0 записей.");
        return 0;
    }

    // Удаляем невалидные индексы (больше или равные размеру records_)
    indices.erase(std::remove_if(indices.begin(), indices.end(),
                                 [&](size_t idx){ return idx >= records_.size(); }),
                  indices.end());

    if (indices.empty()) {
        Logger::debug("Database::deleteRecordsByIndices: Все предоставленные индексы были невалидны. Удалено 0 записей.");
        return 0;
    }

    // Сортируем и удаляем дубликаты
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

    // Сортируем в обратном порядке для корректного удаления (сначала большие индексы)
    std::sort(indices.rbegin(), indices.rend());

    size_t count_deleted = 0;
    std::string deleted_indices_log = "";
    for (size_t index_to_delete : indices) {
        // Проверка уже сделана, но для паранойи можно оставить records_.at() в try-catch
        records_.erase(records_.begin() + static_cast<ptrdiff_t>(index_to_delete));
        count_deleted++;
        if (!deleted_indices_log.empty()) deleted_indices_log += ", ";
        deleted_indices_log += std::to_string(index_to_delete);
    }
    Logger::info("Database::deleteRecordsByIndices: Удалено " + std::to_string(count_deleted) + " записей по индексам: [" + deleted_indices_log + "].");
    return count_deleted;
}

double Database::calculateChargesForRecord(const ProviderRecord& record,
                                         const TariffPlan& plan,
                                         const Date& startDate,
                                         const Date& endDate) const {
    // Мьютекс для чтения (если record и plan не меняются)
    // Важно: эта функция const, она не должна менять состояние Database
    if (record.getDate() < startDate || record.getDate() > endDate) {
        Logger::debug("Database::calculateChargesForRecord: Запись " + record.getName() + " (" + record.getDate().toString() +
                      ") не попадает в период [" + startDate.toString() + " - " + endDate.toString() + "]. Начислено: 0.0");
        return 0.0;
    }

    double totalCharge = 0.0;
    const auto& trafficIn = record.getTrafficInByHour();
    const auto& trafficOut = record.getTrafficOutByHour();

    if (trafficIn.size() != static_cast<size_t>(HOURS_IN_DAY) || trafficOut.size() != static_cast<size_t>(HOURS_IN_DAY)) {
        Logger::error("Database::calculateChargesForRecord: Предупреждение: Запись для \"" + record.getName() + "\" от " + record.getDate().toString()
                      + " имеет некорректный размер данных о трафике (вх: " + std::to_string(trafficIn.size())
                      + ", исх: " + std::to_string(trafficOut.size()) + "). Расчет для этой записи не будет произведен корректно (вернет 0).");
        return 0.0; // Не можем корректно рассчитать
    }

    try {
        for (int hour = 0; hour < HOURS_IN_DAY; ++hour) {
            size_t hour_idx = static_cast<size_t>(hour);
            if (trafficIn[hour_idx] > 0.0) {
                 totalCharge += trafficIn[hour_idx] * plan.getCostInForHour(hour);
            }
            if (trafficOut[hour_idx] > 0.0) {
                 totalCharge += trafficOut[hour_idx] * plan.getCostOutForHour(hour);
            }
        }
    } catch (const std::out_of_range& e) {
        // Эта ошибка может прийти от plan.getCost*ForHour, если тарифы не загружены или час некорректен
        Logger::error("Database::calculateChargesForRecord: Ошибка при расчете платежей для \"" + record.getName() + "\" от " + record.getDate().toString()
                      + " (вероятно, проблема с тарифным планом или индексом часа): " + e.what());
        return 0.0; // Возвращаем 0 при ошибке тарифа
    }
    Logger::debug("Database::calculateChargesForRecord: Для " + record.getName() + " (" + record.getDate().toString() +
                  ") начислено: " + std::to_string(totalCharge) + " за период [" + startDate.toString() + " - " + endDate.toString() + "].");
    return totalCharge;
}

void Database::clearAllRecords() noexcept {
    // Мьютекс должен быть взят вызывающим кодом
    records_.clear();
    records_.shrink_to_fit(); // Освобождаем память
    currentFilename_.clear();
    Logger::info("Database: Все записи очищены, currentFilename сброшен.");
}
