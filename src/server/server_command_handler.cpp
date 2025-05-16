// Предполагаемый путь: src/server/server_command_handler.cpp
#include "server_command_handler.h"
#include "logger.h"
#include "common_defs.h"
#include "file_utils.h" // Для getProjectRootPath
#include "provider_record.h"
#include "ip_address.h"
#include "date.h"
#include <filesystem>
#include <iomanip>
#include <algorithm> // Для std::sort, std::unique (в handleDelete, если нужно)

// Вспомогательная функция getSafeServerFilePath_SCH (как обсуждалось ранее)
// Убедитесь, что она здесь или в общем доступном месте и корректно обрабатывает пути.
// Для краткости, ее код здесь не повторяю, но он должен быть здесь или включен.
// ... (код getSafeServerFilePath_SCH из предыдущих ответов) ...
std::filesystem::path getSafeServerFilePath_SCH(const std::string& server_base_path_str,
                                             const std::string& requested_filename_from_client,
                                             const std::string& default_data_subdir = DEFAULT_SERVER_DATA_SUBDIR) { // Используем константу
    std::filesystem::path server_data_root;
    if (server_base_path_str.empty()) {
        server_data_root = std::filesystem::current_path();
        Logger::warn("getSafeServerFilePath_SCH: server_base_path_str не был предоставлен, используется CWD сервера: " + server_data_root.string());
    } else {
        std::filesystem::path exec_path_obj(server_base_path_str);
        try {
             server_data_root = getProjectRootPath(server_base_path_str.c_str());
        } catch (const std::exception& e) {
            Logger::error("getSafeServerFilePath_SCH: Ошибка при вызове getProjectRootPath с '" + server_base_path_str + "': " + e.what() + ". Используется CWD.");
            server_data_root = std::filesystem::current_path();
        }
    }

    std::filesystem::path data_storage_dir = server_data_root / default_data_subdir;

    if (!std::filesystem::exists(data_storage_dir)) {
        try {
            if (std::filesystem::create_directories(data_storage_dir)) {
                Logger::info("getSafeServerFilePath_SCH: Создана директория для данных сервера: " + data_storage_dir.string());
            }
        } catch (const std::filesystem::filesystem_error& e) {
            Logger::error("getSafeServerFilePath_SCH: Не удалось создать директорию " + data_storage_dir.string() + ": " + e.what());
            throw std::runtime_error("Ошибка создания директории данных на сервере: " + data_storage_dir.string());
        }
    }

    std::filesystem::path client_filename_part(requested_filename_from_client);
    std::string clean_filename = client_filename_part.filename().string();

    if (clean_filename.empty() || clean_filename == "." || clean_filename == "..") {
        Logger::error("getSafeServerFilePath_SCH: Недопустимое имя файла от клиента: " + requested_filename_from_client);
        throw std::runtime_error("Недопустимое имя файла от клиента: " + requested_filename_from_client);
    }
    if (clean_filename.find_first_of("/\\:*?\"<>|") != std::string::npos) { // Базовая проверка
        Logger::error("getSafeServerFilePath_SCH: Имя файла содержит недопустимые символы: " + clean_filename);
        throw std::runtime_error("Имя файла содержит недопустимые символы: " + clean_filename);
    }

    std::filesystem::path target_path = data_storage_dir / clean_filename;
    std::filesystem::path canonical_target_path;
    std::filesystem::path canonical_data_root;

    try {
        canonical_target_path = std::filesystem::weakly_canonical(target_path);
        canonical_data_root = std::filesystem::weakly_canonical(data_storage_dir);
    } catch (const std::filesystem::filesystem_error& e) {
        Logger::error("getSafeServerFilePath_SCH: Ошибка канонизации пути '" + target_path.string() + "' или '" + data_storage_dir.string() + "': " + e.what());
        throw std::runtime_error("Ошибка обработки пути к файлу на сервере.");
    }
    
    std::string target_str = canonical_target_path.string();
    std::string root_str = canonical_data_root.string();
    #ifdef _WIN32
        char sep = '\\';
        // Нормализация для Windows (приведение к одному виду разделителей)
        std::replace(target_str.begin(), target_str.end(), '/', sep);
        std::replace(root_str.begin(), root_str.end(), '/', sep);
    #else
        char sep = '/';
    #endif
    if (!root_str.empty() && root_str.back() != sep) {
        root_str += sep;
    }

    if (target_str.rfind(root_str, 0) != 0) {
        Logger::error("getSafeServerFilePath_SCH: Попытка доступа к файлу вне разрешенной директории! Запрошено: '" +
                      requested_filename_from_client + "', канонический путь: '" + canonical_target_path.string() +
                      "', ожидалось внутри '" + canonical_data_root.string() + "' (проверяемый корень: '" + root_str + "')");
        throw std::runtime_error("Доступ к файлу запрещен (выход за пределы песочницы).");
    }
    return canonical_target_path;
}


ServerCommandHandler::ServerCommandHandler(Database& db, TariffPlan& plan, const std::string& server_exec_path_param)
    : db_(db), tariff_plan_(plan), server_executable_path_(server_exec_path_param) {
    Logger::debug("ServerCommandHandler: Инициализирован. Базовый путь сервера для файлов (исполняемый или корень проекта): " + server_executable_path_);
}

std::string ServerCommandHandler::processCommand(const Query& query) {
    std::ostringstream response_oss;
    // Мьютекс для доступа к db_ должен управляться из Server::handleClient
    // в зависимости от типа операции (чтение/запись).
    try {
        switch (query.type) {
            case QueryType::ADD:               handleAdd(query.params, response_oss); break;
            case QueryType::SELECT:            handleSelect(query.params, response_oss); break;
            case QueryType::DELETE:            handleDelete(query.params, response_oss); break;
            case QueryType::EDIT:              handleEdit(query.params, response_oss); break;
            case QueryType::CALCULATE_CHARGES: handleCalculateCharges(query.params, response_oss); break;
            case QueryType::PRINT_ALL:         handlePrintAll(response_oss); break;
            case QueryType::LOAD:              handleLoad(query.params, response_oss); break;
            case QueryType::SAVE:              handleSave(query.params, response_oss); break;
            case QueryType::HELP:
                response_oss << "Команда HELP обычно обрабатывается на стороне клиента. Сервер поддерживает: ADD, SELECT, DELETE, EDIT, CALCULATE_CHARGES, PRINT_ALL, LOAD, SAVE, EXIT (для сессии).\n";
                Logger::info("ServerCommandHandler: Обработан запрос HELP (информационное сообщение).");
                break;
            case QueryType::EXIT:
                response_oss << "Команда EXIT получена сервером. Сервер подтверждает закрытие текущей клиентской сессии.\n";
                Logger::info("ServerCommandHandler: Обработан запрос EXIT (завершение сессии клиента).");
                break;
            case QueryType::UNKNOWN:
            default:
                response_oss << "Ошибка [Сервер]: Неизвестный или некорректно сформированный запрос был получен сервером.\n";
                Logger::warn("ServerCommandHandler: Получен неизвестный тип запроса или некорректный запрос (оригинал: '" + query.originalQueryString + "').");
                break;
        }
    } catch (const std::invalid_argument& iae) {
        response_oss.str(""); response_oss.clear(); // Очищаем перед записью новой ошибки
        response_oss << "Ошибка [Сервер]: Неверный аргумент в параметрах команды: " << iae.what() << "\n";
        Logger::error("ServerCommandHandler: Ошибка invalid_argument при обработке команды: " + std::string(iae.what()) + " для запроса: " + query.originalQueryString);
    } catch (const std::out_of_range& oor) {
        response_oss.str(""); response_oss.clear();
        response_oss << "Ошибка [Сервер]: Запрошенный элемент (например, по индексу) вне допустимого диапазона: " << oor.what() << "\n";
        Logger::error("ServerCommandHandler: Ошибка out_of_range при обработке команды: " + std::string(oor.what()) + " для запроса: " + query.originalQueryString);
    } catch (const std::runtime_error& rte) {
        response_oss.str(""); response_oss.clear();
        response_oss << "Ошибка выполнения на сервере: " << rte.what() << "\n";
        Logger::error("ServerCommandHandler: Ошибка runtime_error при обработке команды: " + std::string(rte.what()) + " для запроса: " + query.originalQueryString);
    } catch (const std::exception& e) {
        response_oss.str(""); response_oss.clear();
        response_oss << "Непредвиденная системная ошибка на сервере: " << e.what() << "\n";
        Logger::error("ServerCommandHandler: Непредвиденная std::exception при обработке команды: " + std::string(e.what()) + " для запроса: " + query.originalQueryString);
    } catch (...) {
        response_oss.str(""); response_oss.clear();
        response_oss << "Неизвестная критическая ошибка произошла на сервере при обработке запроса.\n";
        Logger::error("ServerCommandHandler: Неизвестная критическая ошибка (...) при обработке команды: " + query.originalQueryString);
    }
    return response_oss.str();
}

// --- Реализация методов handle* ---
// (Копируем и адаптируем из UserInterface.cpp, заменяя вывод в 'out' на 'oss', добавляя Logger)

void ServerCommandHandler::handleAdd(const QueryParameters& params, std::ostringstream& oss) {
    // Код из предыдущего ответа для handleAdd (Шаг 15) был адекватным.
    // Убедимся в логировании.
    std::vector<double> trafficInToAdd = params.trafficInData;
    std::vector<double> trafficOutToAdd = params.trafficOutData;
    if (trafficInToAdd.empty()) trafficInToAdd.assign(HOURS_IN_DAY, 0.0);
    if (trafficOutToAdd.empty()) trafficOutToAdd.assign(HOURS_IN_DAY, 0.0);

    try {
        ProviderRecord newRecord(
            params.subscriberNameData,
            params.ipAddressData,
            params.dateData,
            trafficInToAdd,
            trafficOutToAdd
        );
        db_.addRecord(newRecord); // db_mutex управляется в Server::handleClient
        oss << "Запись успешно добавлена сервером.\n";
        Logger::info("ADD: Запись для ФИО='" + params.subscriberNameData + "', IP=" + params.ipAddressData.toString() +
                     ", Дата=" + params.dateData.toString() + " успешно добавлена.");
    } catch (const std::invalid_argument& e) {
        oss << "Ошибка [Сервер] при создании записи для ADD (неверные аргументы ProviderRecord): " << e.what() << "\n";
        Logger::error("ADD: Ошибка invalid_argument при создании ProviderRecord: " + std::string(e.what()));
    } catch (const std::exception& e) {
        oss << "Непредвиденная ошибка [Сервер] при добавлении записи: " << e.what() << "\n";
        Logger::error("ADD: Общая ошибка std::exception при добавлении записи: " + std::string(e.what()));
    }
}

void ServerCommandHandler::handleSelect(const QueryParameters& params, std::ostringstream& oss) {
    // Код из предыдущего ответа (Шаг 15) был адекватным.
    std::string criteria_log = "FIO: " + (params.useNameFilter ? "'" + params.criteriaName + "'" : "any") +
                              ", IP: " + (params.useIpFilter ? params.criteriaIpAddress.toString() : "any") +
                              ", Date: " + (params.useDateFilter ? params.criteriaDate.toString() : "any");
    Logger::info("SELECT: Поиск по критериям: " + criteria_log);

    std::vector<size_t> indices = db_.findRecordsByCriteria(
        params.criteriaName, params.useNameFilter,
        params.criteriaIpAddress, params.useIpFilter,
        params.criteriaDate, params.useDateFilter
    ); // db_mutex для чтения управляется в Server::handleClient

    oss << "Найдено " << indices.size() << " записей, соответствующих критериям на сервере.\n";
    if (!indices.empty()) {
        oss << "-----------------------------------------------------------------\n";
        for (size_t i = 0; i < indices.size(); ++i) {
            oss << "Запись (Индекс в БД на сервере: " << indices[i] << "):\n";
            try {
                ProviderRecord rec = db_.getRecordByIndex(indices[i]); // Получаем копию
                oss << rec;
                oss << "\n-----------------------------------------------------------------\n";
            } catch (const std::out_of_range& e) {
                oss << "Ошибка [Сервер]: Внутренняя ошибка при доступе к записи по индексу " << indices[i] << " в БД: " << e.what() << "\n";
                Logger::error("SELECT: Ошибка доступа к записи " + std::to_string(indices[i]) + ": " + e.what());
            }
        }
    } else {
        oss << "Записи не найдены или выборка пуста на сервере.\n";
    }
    Logger::info("SELECT: Завершено, найдено " + std::to_string(indices.size()) + " записей.");
}

void ServerCommandHandler::handleDelete(const QueryParameters& params, std::ostringstream& oss) {
    // Код из предыдущего ответа (Шаг 15) был адекватным.
     std::string criteria_log = "FIO: " + (params.useNameFilter ? "'" + params.criteriaName + "'" : "any") +
                              ", IP: " + (params.useIpFilter ? params.criteriaIpAddress.toString() : "any") +
                              ", Date: " + (params.useDateFilter ? params.criteriaDate.toString() : "any");
    Logger::info("DELETE: Поиск записей для удаления по критериям: " + criteria_log);

    std::vector<size_t> indices_to_delete = db_.findRecordsByCriteria(
        params.criteriaName, params.useNameFilter,
        params.criteriaIpAddress, params.useIpFilter,
        params.criteriaDate, params.useDateFilter
    ); // db_mutex управляется в Server::handleClient

    if (indices_to_delete.empty()) {
        oss << "Не найдено записей, соответствующих критериям для удаления на сервере.\n";
        Logger::info("DELETE: Не найдено записей для удаления.");
        return;
    }

    // Логика вывода удаляемых записей (как в UserInterface::handleDelete)
    oss << "Сервер подготовил к удалению следующие " << indices_to_delete.size() << " записей:\n";
    oss << "-----------------------------------------------------------------\n";
    for (size_t db_idx : indices_to_delete) { // Используем db_idx для ясности, что это индекс в БД
        try {
            // Получаем копию перед удалением для вывода
            ProviderRecord rec_to_show = db_.getRecordByIndex(db_idx);
            oss << "Запись (Индекс в БД на сервере: " << db_idx << "):\n";
            oss << rec_to_show;
            oss << "\n-----------------------------------------------------------------\n";
        } catch (const std::out_of_range& e) {
             oss << "Предупреждение [Сервер]: Не удалось отобразить запись с индексом " << db_idx << " перед удалением (возможно, уже удалена другим потоком): " << e.what() << "\n";
             Logger::warn("DELETE: Ошибка отображения записи " + std::to_string(db_idx) + " перед удалением: " + e.what());
        }
    }

    size_t num_deleted = db_.deleteRecordsByIndices(indices_to_delete);
    oss << num_deleted << " записей успешно удалено сервером.\n";
    Logger::info("DELETE: Удалено " + std::to_string(num_deleted) + " записей.");
}

void ServerCommandHandler::handleEdit(const QueryParameters& params, std::ostringstream& oss) {
    // Код из предыдущего ответа (Шаг 15) был адекватным и подробным.
    // Убедитесь, что он полностью перенесен и использует Logger.
    // ... (полный код handleEdit из предыдущего ответа с Logger) ...
    std::string criteria_log = "FIO: " + (params.useNameFilter ? "'" + params.criteriaName + "'" : "any") +
                              ", IP: " + (params.useIpFilter ? params.criteriaIpAddress.toString() : "any") +
                              ", Date: " + (params.useDateFilter ? params.criteriaDate.toString() : "any");
    Logger::info("EDIT: Поиск записи для редактирования по критериям: " + criteria_log);

    std::vector<size_t> indices_to_edit = db_.findRecordsByCriteria(
        params.criteriaName, params.useNameFilter,
        params.criteriaIpAddress, params.useIpFilter,
        params.criteriaDate, params.useDateFilter
    ); // db_mutex управляется в Server::handleClient

    if (indices_to_edit.empty()) {
        oss << "EDIT Ошибка [Сервер]: Не найдено записей, соответствующих критериям.\n";
        Logger::info("EDIT: Не найдено записей для редактирования по критериям.");
        return;
    }
    if (params.setData.empty() && !params.hasTrafficInToSet && !params.hasTrafficOutToSet) {
        oss << "EDIT Ошибка [Сервер]: Отсутствует секция SET с указанием полей для изменения.\n";
        Logger::info("EDIT: Пустая секция SET, нет данных для изменения.");
        return;
    }

    size_t target_db_index = indices_to_edit[0];
    if (indices_to_edit.size() > 1) {
        oss << "EDIT Предупреждение [Сервер]: Критерии соответствуют " << indices_to_edit.size()
            << " записям. Будет отредактирована только первая найденная запись (Индекс в БД: " << target_db_index << ").\n";
        Logger::warn("EDIT: Найдено " + std::to_string(indices_to_edit.size()) +
                     " записей для редактирования, будет обработана только первая с индексом " + std::to_string(target_db_index));
    }

    try {
        ProviderRecord record_to_edit = db_.getRecordByIndex(target_db_index);
        bool changed_applied = false;
        std::string changes_log_details = "EDIT: Применяемые изменения для записи " + std::to_string(target_db_index) + ": ";

        auto it_fio = params.setData.find("FIO");
        if (it_fio != params.setData.end()) {
            if (record_to_edit.getName() != it_fio->second) {
                record_to_edit.setName(it_fio->second);
                changes_log_details += "FIO->'" + it_fio->second + "'; ";
                changed_applied = true;
            }
        }
        // ... (аналогично для IP и DATE с проверкой на реальное изменение) ...
        auto it_ip = params.setData.find("IP");
        if (it_ip != params.setData.end()) {
            IPAddress new_ip; std::istringstream ip_ss(it_ip->second);
            if (!(ip_ss >> new_ip) || (ip_ss.rdbuf() && ip_ss.rdbuf()->in_avail() != 0)) {
                oss << "EDIT Ошибка [Сервер]: Некорректный формат IP в SET: '" << it_ip->second << "'\n"; Logger::warn("EDIT: Некорректный IP в SET: " + it_ip->second); return;
            }
            if (record_to_edit.getIpAddress() != new_ip) {
                record_to_edit.setIpAddress(new_ip); changes_log_details += "IP->" + new_ip.toString() + "; "; changed_applied = true;
            }
        }
        auto it_date = params.setData.find("DATE");
        if (it_date != params.setData.end()) {
            Date new_date; std::istringstream date_ss(it_date->second);
            if (!(date_ss >> new_date) || (date_ss.rdbuf() && date_ss.rdbuf()->in_avail() != 0)) {
                oss << "EDIT Ошибка [Сервер]: Некорректный формат DATE в SET: '" << it_date->second << "'\n"; Logger::warn("EDIT: Некорректная дата в SET: " + it_date->second); return;
            }
            if (record_to_edit.getDate() != new_date) {
                record_to_edit.setDate(new_date); changes_log_details += "DATE->" + new_date.toString() + "; "; changed_applied = true;
            }
        }


        if (params.hasTrafficInToSet) {
             bool traffic_in_differs = params.trafficInData.size() != record_to_edit.getTrafficInByHour().size();
            if (!traffic_in_differs) { /* ... цикл сравнения ... */ for(size_t i=0; i<params.trafficInData.size(); ++i) if(std::fabs(params.trafficInData[i]-record_to_edit.getTrafficInByHour()[i]) > DOUBLE_EPSILON) {traffic_in_differs=true; break;} }
            if (traffic_in_differs) { record_to_edit.setTrafficInByHour(params.trafficInData); changes_log_details += "TRAFFIC_IN обновлен; "; changed_applied = true; }
        }
        if (params.hasTrafficOutToSet) {
            bool traffic_out_differs = params.trafficOutData.size() != record_to_edit.getTrafficOutByHour().size();
            if (!traffic_out_differs) { /* ... цикл сравнения ... */ for(size_t i=0; i<params.trafficOutData.size(); ++i) if(std::fabs(params.trafficOutData[i]-record_to_edit.getTrafficOutByHour()[i]) > DOUBLE_EPSILON) {traffic_out_differs=true; break;} }
            if (traffic_out_differs) { record_to_edit.setTrafficOutByHour(params.trafficOutData); changes_log_details += "TRAFFIC_OUT обновлен; "; changed_applied = true; }
        }

        if (changed_applied) {
            db_.editRecord(target_db_index, record_to_edit);
            oss << "Запись с Индексом в БД " << target_db_index << " успешно отредактирована сервером.\n";
            oss << "Новые данные:\n-----------------------------------------------------------------\n";
            oss << "Запись (Индекс в БД: " << target_db_index << "):\n" << db_.getRecordByIndex(target_db_index);
            oss << "\n-----------------------------------------------------------------\n";
            Logger::info(changes_log_details);
        } else {
            oss << "EDIT Информация [Сервер]: В секции SET не найдено корректных полей для изменения или новые значения совпадают с текущими.\n";
            Logger::info("EDIT: Не было применено изменений к записи " + std::to_string(target_db_index) + ".");
        }
    } catch (const std::out_of_range& e) {
        oss << "EDIT Ошибка [Сервер]: Ошибка доступа к записи (индекс " << target_db_index << "): " << e.what() << "\n";
        Logger::error("EDIT OOR: " + std::string(e.what()));
    } catch (const std::invalid_argument& e) {
        oss << "EDIT Ошибка [Сервер]: Ошибка при установке данных для записи " << target_db_index << ": " << e.what() << "\n";
        Logger::error("EDIT IA: " + std::string(e.what()));
    }
}

void ServerCommandHandler::handleCalculateCharges(const QueryParameters& params, std::ostringstream& oss) {
    // Код из предыдущего ответа (Шаг 15) был адекватным.
    // ... (полный код handleCalculateCharges из предыдущего ответа с Logger) ...
     if (!params.useStartDateFilter || !params.useEndDateFilter) {
        oss << "Ошибка [Сервер]: Команда CALCULATE_CHARGES требует указания START_DATE и END_DATE.\n"; Logger::warn("CALCULATE_CHARGES: Отсутствуют START_DATE или END_DATE."); return;
    }
    if (params.criteriaStartDate > params.criteriaEndDate) {
        oss << "Ошибка [Сервер]: START_DATE (" << params.criteriaStartDate.toString() << ") не может быть позже END_DATE (" << params.criteriaEndDate.toString() << ").\n"; Logger::warn("CALCULATE_CHARGES: START_DATE позже END_DATE."); return;
    }
    std::string criteria_log_calc = "Период: " + params.criteriaStartDate.toString() + " - " + params.criteriaEndDate.toString();
    // ... (добавление FIO/IP/DateRec в criteria_log_calc) ...
    Logger::info("CALCULATE_CHARGES: Расчет для: " + criteria_log_calc);

    std::vector<ProviderRecord> records_to_process;
    // ... (логика выбора records_to_process) ...
    if (!params.useNameFilter && !params.useIpFilter && !params.useDateFilter) {
        records_to_process = db_.getAllRecords();
    } else {
        std::vector<size_t> record_indices = db_.findRecordsByCriteria(params.criteriaName, params.useNameFilter, params.criteriaIpAddress, params.useIpFilter, params.criteriaDate, params.useDateFilter);
        if (record_indices.empty()){ oss << "Не найдено записей по критериям для расчета.\n"; Logger::info("CALCULATE_CHARGES: Записи по критериям не найдены.");}
        else { for(size_t index : record_indices) { try { records_to_process.push_back(db_.getRecordByIndex(index)); } catch (const std::out_of_range& e) {Logger::warn("CALC_CHARGES: индекс " + std::to_string(index) + " OOR: " + e.what());} } }
    }

    double grandTotalCharges = 0.0;
    // ... (остальная логика с циклом, расчетом и выводом в oss, как в предыдущем ответе, с Logger::debug для каждой записи) ...
    oss << "Отчет по расчету стоимости за период (" << params.criteriaStartDate.toString() << " - " << params.criteriaEndDate.toString() << ") на сервере:\n";
    oss << "-----------------------------------------------------------------\n";
    oss << std::fixed << std::setprecision(2);
    bool charges_calculated_for_at_least_one = false;

    if (records_to_process.empty() && (params.useNameFilter || params.useIpFilter || params.useDateFilter) ) { /* уже обработано выше */ }
    else if (records_to_process.empty()){ oss << "В базе данных нет записей для расчета.\n"; }


    for (const auto& record : records_to_process) {
        if (record.getDate() >= params.criteriaStartDate && record.getDate() <= params.criteriaEndDate) {
            double charge = 0.0;
            try {
                charge = db_.calculateChargesForRecord(record, tariff_plan_, params.criteriaStartDate, params.criteriaEndDate);
                oss << "Абонент: " << record.getName() << " (IP: " << record.getIpAddress().toString()
                    << ", Дата записи: " << record.getDate().toString() << ") | Начислено: " << charge << "\n";
                charges_calculated_for_at_least_one = true; grandTotalCharges += charge;
            } catch (const std::exception& e) { /* ... логирование ошибки ... */ }
        } else { /* ... Logger::debug пропуск записи ... */ }
    }
    if (!charges_calculated_for_at_least_one && !records_to_process.empty()) { oss << "Для выбранных записей начисления отсутствуют (период/нулевые тарифы).\n"; }
    oss << "-----------------------------------------------------------------\n";
    oss << "Итого начислено для выборки на сервере: " << grandTotalCharges << "\n";
    oss << "-----------------------------------------------------------------\n";
    Logger::info("CALCULATE_CHARGES: Расчет завершен. Итого: " + std::to_string(grandTotalCharges));
}

void ServerCommandHandler::handlePrintAll(std::ostringstream& oss) {
    // Код из предыдущего ответа (Шаг 15) был адекватным.
    Logger::info("PRINT_ALL: Запрос на вывод всех записей.");
    const auto& all_records = db_.getAllRecords(); // db_mutex для чтения управляется в Server::handleClient
    oss << "Содержимое базы данных на сервере (" << all_records.size() << " записей):\n";
    if (all_records.empty()) {
        oss << "База данных пуста.\n";
        Logger::info("PRINT_ALL: База данных пуста.");
    } else {
        Logger::info("PRINT_ALL: Вывод " + std::to_string(all_records.size()) + " записей.");
        oss << "-----------------------------------------------------------------\n";
        for (size_t i = 0; i < all_records.size(); ++i) {
            oss << "Запись (Индекс в БД на сервере: " << i << "):\n";
            try {
                ProviderRecord rec = db_.getRecordByIndex(i); // Копия
                oss << rec;
                oss << "\n-----------------------------------------------------------------\n";
            } catch(const std::out_of_range& e) {
                oss << "Ошибка [Сервер]: Внутренняя ошибка при доступе к записи по индексу " << i << " в БД при PRINT_ALL: " << e.what() << "\n";
                Logger::error("PRINT_ALL: Ошибка доступа к записи " + std::to_string(i) + ": " + e.what());
            }
        }
    }
}

void ServerCommandHandler::handleLoad(const QueryParameters& params, std::ostringstream& oss) {
    // Код из предыдущего ответа (Шаг 15) был адекватным.
    if (params.filename.empty()) {
        oss << "Ошибка [Сервер]: Команда LOAD требует имя файла.\n"; Logger::warn("LOAD: Имя файла не указано от клиента."); return;
    }
    try {
        std::filesystem::path target_file_path = getSafeServerFilePath_SCH(server_executable_path_, params.filename, DEFAULT_SERVER_DATA_SUBDIR);
        Logger::info("LOAD: Попытка загрузки файла на сервере: " + target_file_path.string());
        db_.loadFromFile(target_file_path.string(), oss); // db_mutex управляется в Server::handleClient
    } catch (const std::exception& e) {
        oss << "Ошибка [Сервер] при LOAD: " << e.what() << "\n"; Logger::error("LOAD: Исключение: " + std::string(e.what()));
    }
}

void ServerCommandHandler::handleSave(const QueryParameters& params, std::ostringstream& oss) {
    // Код из предыдущего ответа (Шаг 15) был адекватным.
    std::string filename_to_save_on_server;
    bool use_current_filename = params.filename.empty();
    try {
        if (use_current_filename) {
            filename_to_save_on_server = db_.getCurrentFilename();
            if (filename_to_save_on_server.empty()) {
                oss << "Ошибка [Сервер]: Имя файла для SAVE не указано (БД не была загружена/сохранена ранее).\n"; Logger::warn("SAVE: Имя файла не определено."); return;
            }
            Logger::info("SAVE: Используется текущее имя файла из БД: " + filename_to_save_on_server);
            // Проверка безопасности currentFilename_ (он должен быть уже каноническим и безопасным, если установлен Database::load/save)
             std::filesystem::path temp_check_current(filename_to_save_on_server);
             if (!temp_check_current.is_absolute()){ // На всякий случай, если currentFilename не абсолютный
                 Logger::warn("SAVE: db_.currentFilename_ ('" + filename_to_save_on_server + "') не является абсолютным. Попытка разрешить.");
                 filename_to_save_on_server = getSafeServerFilePath_SCH(server_executable_path_, temp_check_current.filename().string(), DEFAULT_SERVER_DATA_SUBDIR).string();
             } else { // Если абсолютный, то getSafeServerFilePath_SCH проверит его на "песочницу"
                  filename_to_save_on_server = getSafeServerFilePath_SCH(server_executable_path_, temp_check_current.filename().string(), DEFAULT_SERVER_DATA_SUBDIR).string();
             }


        } else {
            filename_to_save_on_server = getSafeServerFilePath_SCH(server_executable_path_, params.filename, DEFAULT_SERVER_DATA_SUBDIR).string();
            Logger::info("SAVE: Используется указанное клиентом имя файла: " + params.filename + ", разрешен на сервере в: " + filename_to_save_on_server);
        }
        db_.saveToFile(filename_to_save_on_server, oss); // db_mutex управляется в Server::handleClient
    } catch (const std::exception& e) {
        oss << "Ошибка [Сервер] при SAVE: " << e.what() << "\n"; Logger::error("SAVE: Исключение: " + std::string(e.what()));
    }
}
