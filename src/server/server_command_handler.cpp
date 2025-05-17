/*!
 * \file server_command_handler.cpp
 * \author Fedor Zilnitskiy
 * \brief Реализация класса ServerCommandHandler для обработки команд сервера.
 */
#include "server_command_handler.h"
#include "logger.h"
#include "common_defs.h" 
#include "file_utils.h"  
#include "provider_record.h"
#include "ip_address.h"
#include "date.h"
#include <filesystem> 
#include <iomanip>    
#include <algorithm>  
#include <cstdio>     
#include <cctype>     

#ifdef _WIN32
#include <direct.h> 
#else
#include <unistd.h> 
#endif

static std::string toUpperSCH(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return static_cast<char>(std::toupper(c)); });
    return s;
}

std::filesystem::path getSafeServerFilePath_SCH(
    const std::string& root_for_data_ops_str, 
    const std::string& requested_filename_from_client,
    const std::string& default_data_subdir) 
{
    const std::string sch_log_prefix = "[SCH GetSafePath] ";
    // Logger::debug(sch_log_prefix + "Вызван с root_for_data_ops_str='" + root_for_data_ops_str +
    //               "', requested_filename_from_client='" + requested_filename_from_client +
    //               "', default_data_subdir='" + default_data_subdir + "'");

    std::filesystem::path server_data_search_root;

    if (!root_for_data_ops_str.empty()) {
        try {
            server_data_search_root = std::filesystem::weakly_canonical(std::filesystem::absolute(root_for_data_ops_str));
            // Logger::debug(sch_log_prefix + "Используется предоставленный базовый путь для данных (разрешенный): '" + server_data_search_root.string() + "'");
        } catch (const std::filesystem::filesystem_error& e) {
            Logger::error(sch_log_prefix + "Ошибка при обработке предоставленного базового пути '" + root_for_data_ops_str + "': " + e.what() + ". Попытка использовать CWD.");
            server_data_search_root = std::filesystem::current_path(); 
        }
    } else { 
        char current_path_cstr[1024] = {0};
        #ifdef _WIN32
            if (_getcwd(current_path_cstr, sizeof(current_path_cstr) -1) == nullptr) {
                Logger::error(sch_log_prefix + "Критическая ошибка: не удалось получить текущую рабочую директорию (_getcwd).");
                throw std::runtime_error("Критическая ошибка сервера: не удалось получить CWD (_getcwd).");
            }
        #else
            if (getcwd(current_path_cstr, sizeof(current_path_cstr) -1) == nullptr) {
                Logger::error(sch_log_prefix + "Критическая ошибка: не удалось получить текущую рабочую директорию (getcwd). Errno: " + std::to_string(errno));
                throw std::runtime_error("Критическая ошибка сервера: не удалось получить CWD (getcwd).");
            }
        #endif
        
        try {
            server_data_search_root = getProjectRootPath(current_path_cstr); 
            // Logger::warn(sch_log_prefix + "Базовый путь для данных не был предоставлен (root_for_data_ops_str пуст). Определен корень проекта от CWD: '" + server_data_search_root.string() + "'");
        } catch (const std::exception& e_gprp) {
            Logger::error(sch_log_prefix + "Ошибка при авто-определении корня проекта от CWD ('" + current_path_cstr + "'): " + std::string(e_gprp.what()) + ". Используется CWD как корень данных.");
            server_data_search_root = std::filesystem::path(current_path_cstr);
        }
    }

    std::filesystem::path data_storage_dir = server_data_search_root / default_data_subdir;

    if (!std::filesystem::exists(data_storage_dir)) {
        try {
            if (std::filesystem::create_directories(data_storage_dir)) {
                // Logger::info(sch_log_prefix + "Успешно создана директория для данных сервера: '" + data_storage_dir.string() + "'");
            } else {
                 if (!std::filesystem::exists(data_storage_dir)) {
                    Logger::error(sch_log_prefix + "Не удалось создать директорию данных '" + data_storage_dir.string() + "' и она не существует после попытки создания.");
                    throw std::runtime_error("Критическая ошибка сервера: не удалось создать директорию для хранения баз данных: " + data_storage_dir.string());
                 }
            }
        } catch (const std::filesystem::filesystem_error& e_create_dir) {
            Logger::error(sch_log_prefix + "Не удалось создать директорию данных '" + data_storage_dir.string() + "': " + e_create_dir.what());
            throw std::runtime_error("Критическая ошибка сервера: не удалось создать директорию для хранения баз данных: " + data_storage_dir.string());
        }
    } else if (!std::filesystem::is_directory(data_storage_dir)){ 
        Logger::error(sch_log_prefix + "Путь для хранения данных '" + data_storage_dir.string() + "' существует, но не является директорией!");
        throw std::runtime_error("Критическая ошибка сервера: путь для хранения баз данных не является директорией.");
    }

    std::filesystem::path client_filename_obj(requested_filename_from_client);
    std::string clean_filename = client_filename_obj.filename().string(); 

    if (clean_filename.empty() || clean_filename == "." || clean_filename == "..") {
        // Logger::warn(sch_log_prefix + "Недопустимое имя файла от клиента: '" + requested_filename_from_client + "' (пустое или состоит из точек).");
        throw std::runtime_error("Недопустимое имя файла от клиента: '" + requested_filename_from_client + "'.");
    }
    if (clean_filename.find_first_of("/\\:*?\"<>|") != std::string::npos) {
        // Logger::warn(sch_log_prefix + "Имя файла '" + clean_filename + "' от клиента содержит недопустимые символы.");
        throw std::runtime_error("Имя файла '" + clean_filename + "' содержит недопустимые символы (/\\:*?\"<>|).");
    }
    const size_t MAX_FILENAME_LEN = 250; 
    if (clean_filename.length() > MAX_FILENAME_LEN) {
        // Logger::warn(sch_log_prefix + "Имя файла от клиента слишком длинное: '" + clean_filename + "' (макс " + std::to_string(MAX_FILENAME_LEN) + ").");
        throw std::runtime_error("Имя файла слишком длинное (макс. " + std::to_string(MAX_FILENAME_LEN) + " символов): '" + clean_filename + "'");
    }

    std::filesystem::path target_path = data_storage_dir / clean_filename;
    std::filesystem::path canonical_target_path;
    std::filesystem::path canonical_data_root_resolved; 

    try {
        canonical_target_path = std::filesystem::weakly_canonical(target_path);
        canonical_data_root_resolved = std::filesystem::weakly_canonical(data_storage_dir); 
    } catch (const std::filesystem::filesystem_error& e_canon) {
        Logger::error(sch_log_prefix + "Ошибка канонизации пути '" + target_path.string() + "' или '" + data_storage_dir.string() + "': " + e_canon.what());
        throw std::runtime_error("Ошибка сервера при обработке пути к файлу.");
    }

    std::string target_str_norm = canonical_target_path.lexically_normal().string();
    std::string root_str_norm = canonical_data_root_resolved.lexically_normal().string();
    
    // Ensure root_str_norm ends with a preferred_separator if it's not empty and target_str_norm is longer
    // This handles cases where root_str_norm might be something like "/tmp/data" and target "/tmp/datafiles".
    std::string root_str_norm_with_sep = root_str_norm;
    if (!root_str_norm_with_sep.empty() && root_str_norm_with_sep.back() != std::filesystem::path::preferred_separator) {
        root_str_norm_with_sep += std::filesystem::path::preferred_separator;
    }
    
    // Check if target_str_norm starts with root_str_norm_with_sep (or is equal to root_str_norm if it's a directory itself - but we save files)
    // And ensure target is not just the root directory itself.
    bool path_is_safe = (target_str_norm.rfind(root_str_norm_with_sep, 0) == 0 && target_str_norm.length() > root_str_norm_with_sep.length());
    if (root_str_norm_with_sep.length() == 1 && root_str_norm_with_sep[0] == std::filesystem::path::preferred_separator) { // Root is just "/"
         path_is_safe = (target_str_norm.rfind(root_str_norm_with_sep, 0) == 0 && target_str_norm.length() > 1);
    }


    if (!path_is_safe)
    {
        Logger::error(sch_log_prefix + "Попытка доступа к файлу вне разрешенной директории (нарушение песочницы)! "
                      "Запрошено клиентом: '" + requested_filename_from_client + "', "
                      "Очищенное имя: '" + clean_filename + "', "
                      "Целевой путь (канонический, нормализованный): '" + target_str_norm + "', "
                      "Ожидалось внутри (канонический, нормализованный корень данных): '" + root_str_norm + "'.");
        throw std::runtime_error("Доступ к файлу запрещен (нарушение безопасности сервера).");
    }
    
    // Logger::debug(sch_log_prefix + "Безопасный абсолютный путь к файлу на сервере: '" + canonical_target_path.string() + "'");
    return canonical_target_path;
}


ServerCommandHandler::ServerCommandHandler(Database& db, TariffPlan& plan, const std::string& server_data_path_base)
    : db_(db), tariff_plan_(plan), server_data_base_path_(server_data_path_base) {
}

std::string ServerCommandHandler::processCommand(const Query& query) {
    std::ostringstream response_oss;
    const std::string sch_cmd_log_prefix = "[SCH CmdProc] ";

    try {
        // Logger::info(sch_cmd_log_prefix + "Обработка команды типа " + std::to_string(static_cast<int>(query.type)) +
        //              " (оригинал: \"" + query.originalQueryString + "\")");

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
                response_oss << "OK\nПоддерживаемые команды сервера: ADD, SELECT, DELETE, EDIT, CALCULATE_CHARGES, PRINT_ALL, LOAD, SAVE, EXIT.\n"
                             << "Для детального синтаксиса используйте локальную команду HELP на стороне клиента.\n";
                // Logger::info(sch_cmd_log_prefix + "Обработан запрос HELP от клиента.");
                break;
            case QueryType::EXIT:
                response_oss << "OK\nСервер подтверждает команду EXIT. Завершение сессии.\n";
                // Logger::info(sch_cmd_log_prefix + "Подтверждение команды EXIT для клиента. Клиент должен разорвать соединение.");
                break;
            case QueryType::UNKNOWN:
            default:
                response_oss << "ERROR\nОшибка [Сервер]: Неизвестная или некорректно сформированная команда получена сервером.\n"
                             << "Оригинальный запрос: \"" << query.originalQueryString << "\"\n";
                Logger::warn(sch_cmd_log_prefix + "Получен неизвестный или некорректный тип запроса (тип: "
                             + std::to_string(static_cast<int>(query.type)) + ", оригинал: '" + query.originalQueryString + "').");
                break;
        }
    } catch (const std::invalid_argument& iae) {
        response_oss.str(""); response_oss.clear(); 
        response_oss << "ERROR\nОшибка [Сервер]: Неверный аргумент в параметрах команды -> " << iae.what() << "\n"
                     << "Оригинальный запрос: \"" << query.originalQueryString << "\"\n";
        Logger::error(sch_cmd_log_prefix + "InvalidArgument при обработке '" + query.originalQueryString + "': " + iae.what());
    } catch (const std::out_of_range& oor) {
        response_oss.str(""); response_oss.clear();
        response_oss << "ERROR\nОшибка [Сервер]: Запрошенный элемент (например, по индексу) вне допустимого диапазона -> " << oor.what() << "\n"
                     << "Оригинальный запрос: \"" << query.originalQueryString << "\"\n";
        Logger::error(sch_cmd_log_prefix + "OutOfRange при обработке '" + query.originalQueryString + "': " + oor.what());
    } catch (const std::runtime_error& rte) {
        response_oss.str(""); response_oss.clear();
        response_oss << "ERROR\nОшибка выполнения на сервере -> " << rte.what() << "\n"
                     << "Оригинальный запрос: \"" << query.originalQueryString << "\"\n";
        Logger::error(sch_cmd_log_prefix + "RuntimeError при обработке '" + query.originalQueryString + "': " + rte.what());
    } catch (const std::exception& e) { 
        response_oss.str(""); response_oss.clear();
        response_oss << "ERROR\nНепредвиденная системная ошибка на сервере -> " << e.what() << "\n"
                     << "Оригинальный запрос: \"" << query.originalQueryString << "\"\n";
        Logger::error(sch_cmd_log_prefix + "StdException при обработке '" + query.originalQueryString + "': " + e.what());
    } catch (...) { 
        response_oss.str(""); response_oss.clear();
        response_oss << "ERROR\nНеизвестная критическая ошибка произошла на сервере при обработке вашего запроса.\n"
                     << "Оригинальный запрос: \"" << query.originalQueryString << "\"\n";
        Logger::error(sch_cmd_log_prefix + "Неизвестное исключение (...) при обработке '" + query.originalQueryString + "'.");
    }
    return response_oss.str();
}

void ServerCommandHandler::handleAdd(const QueryParameters& params, std::ostringstream& oss) {
    std::vector<double> trafficInToAdd = params.trafficInData;
    std::vector<double> trafficOutToAdd = params.trafficOutData;

    if (trafficInToAdd.empty() && params.hasTrafficInToSet) { 
        throw std::runtime_error("ADD: Блок TRAFFIC_IN был указан, но не содержит данных (ошибка парсера).");
    }
    if (trafficOutToAdd.empty() && params.hasTrafficOutToSet) {
         throw std::runtime_error("ADD: Блок TRAFFIC_OUT был указан, но не содержит данных (ошибка парсера).");
    }

    if (!params.hasTrafficInToSet) { 
        trafficInToAdd.assign(HOURS_IN_DAY, 0.0);
    }
    if (!params.hasTrafficOutToSet) { 
        trafficOutToAdd.assign(HOURS_IN_DAY, 0.0);
    }
    
    if (trafficInToAdd.size() != static_cast<size_t>(HOURS_IN_DAY) && params.hasTrafficInToSet) {
        throw std::runtime_error("ADD: TRAFFIC_IN должен содержать " + std::to_string(HOURS_IN_DAY) + " значений, получено " + std::to_string(trafficInToAdd.size()));
    }
     if (trafficInToAdd.size() != static_cast<size_t>(HOURS_IN_DAY) && !params.hasTrafficInToSet && !trafficInToAdd.empty()) { // Если не было hasTrafficInToSet, но вектор не пуст (не должен быть)
        throw std::logic_error("ADD: Внутренняя ошибка - TRAFFIC_IN не был SET, но вектор не пуст и неверного размера.");
    }
    if (trafficOutToAdd.size() != static_cast<size_t>(HOURS_IN_DAY) && params.hasTrafficOutToSet) {
         throw std::runtime_error("ADD: TRAFFIC_OUT должен содержать " + std::to_string(HOURS_IN_DAY) + " значений, получено " + std::to_string(trafficOutToAdd.size()));
    }
    if (trafficOutToAdd.size() != static_cast<size_t>(HOURS_IN_DAY) && !params.hasTrafficOutToSet && !trafficOutToAdd.empty()) {
        throw std::logic_error("ADD: Внутренняя ошибка - TRAFFIC_OUT не был SET, но вектор не пуст и неверного размера.");
    }

    ProviderRecord newRecord(
        params.subscriberNameData,
        params.ipAddressData,
        params.dateData,
        trafficInToAdd,  
        trafficOutToAdd
    );
    db_.addRecord(newRecord);
    oss << "OK\nЗапись для абонента '" << params.subscriberNameData << "' успешно добавлена на сервере.\n";
}

void ServerCommandHandler::handleSelect(const QueryParameters& params, std::ostringstream& oss) {
    std::vector<size_t> indices = db_.findRecordsByCriteria(
        params.criteriaName, params.useNameFilter,
        params.criteriaIpAddress, params.useIpFilter,
        params.criteriaDate, params.useDateFilter
    );

    oss << "OK\n"; // Всегда начинаем с OK, даже если ничего не найдено
    if (indices.empty()) {
        oss << "Записи, соответствующие критериям, не найдены на сервере.\n";
    } else {
        oss << "Найдено " << indices.size() << " записей, соответствующих критериям на сервере:\n";
        oss << "-----------------------------------------------------------------\n";
        for (size_t i = 0; i < indices.size(); ++i) {
             try {
                const ProviderRecord& rec = db_.getRecordByIndex(indices[i]);
                oss << "Запись (Индекс в БД #" << indices[i] << "):\n";
                oss << rec; 
                oss << "\n-----------------------------------------------------------------\n";
            } catch (const std::out_of_range& e) {
                oss << "ПРЕДУПРЕЖДЕНИЕ [Сервер]: Не удалось получить доступ к записи по индексу " << indices[i]
                    << " (возможно, была удалена): " << e.what() << "\n";
                Logger::warn("[SCH Select] Ошибка доступа к записи " + std::to_string(indices[i]) + " при выводе результатов: " + e.what());
            }
        }
    }
}

void ServerCommandHandler::handleDelete(const QueryParameters& params, std::ostringstream& oss) {
    std::vector<size_t> indices_to_delete = db_.findRecordsByCriteria(
        params.criteriaName, params.useNameFilter,
        params.criteriaIpAddress, params.useIpFilter,
        params.criteriaDate, params.useDateFilter
    );
    oss << "OK\n";
    if (indices_to_delete.empty()) {
        oss << "Не найдено записей, соответствующих критериям для удаления на сервере.\n";
        return;
    }

    size_t num_deleted = db_.deleteRecordsByIndices(indices_to_delete);
    oss << "Успешно удалено " << num_deleted << " записей сервером.\n";
}

void ServerCommandHandler::handleEdit(const QueryParameters& params, std::ostringstream& oss) {
    if (params.setData.empty() && !params.hasTrafficInToSet && !params.hasTrafficOutToSet) {
        throw std::runtime_error("EDIT: Отсутствует секция SET или она не содержит полей для изменения.");
    }
    
    std::vector<size_t> indices_to_edit = db_.findRecordsByCriteria(
        params.criteriaName, params.useNameFilter,
        params.criteriaIpAddress, params.useIpFilter,
        params.criteriaDate, params.useDateFilter
    );
    oss << "OK\n";
    if (indices_to_edit.empty()) {
        oss << "Не найдено записей, соответствующих критериям для редактирования.\n";
        Logger::info("[SCH Edit] Не найдено записей для редактирования по критериям.");
        return;
    }
    
    size_t target_db_index = indices_to_edit[0];
    if (indices_to_edit.size() > 1) {
        oss << "EDIT Предупреждение [Сервер]: Критерии соответствуют " << indices_to_edit.size()
            << " записям. Будет отредактирована только первая найденная запись (Индекс в БД: " << target_db_index << ").\n";
        Logger::warn("[SCH Edit] Найдено " + std::to_string(indices_to_edit.size()) +
                     " записей для редактирования, будет обработана только первая с индексом " + std::to_string(target_db_index));
    }

    ProviderRecord record_to_edit = db_.getRecordByIndex(target_db_index);
    ProviderRecord original_record_for_comparison = record_to_edit; // Для проверки, были ли реальные изменения
    bool changed_applied = false;
    // std::ostringstream changes_log_details_ss; // Не используется в текущей реализации для вывода

     for (const auto& pair : params.setData) {
        const std::string& field_key_upper = toUpperSCH(pair.first);
        const std::string& value_str = pair.second;
        if (field_key_upper == "FIO") {
            record_to_edit.setName(value_str);
        } else if (field_key_upper == "IP") {
            IPAddress new_ip;
            std::istringstream ip_ss(value_str);
            if (!(ip_ss >> new_ip) || (ip_ss >> std::ws && !ip_ss.eof())) {
                throw std::invalid_argument("EDIT SET: Некорректный формат IP-адреса '" + value_str + "' в секции SET.");
            }
            record_to_edit.setIpAddress(new_ip);
        } else if (field_key_upper == "DATE") {
            Date new_date;
            std::istringstream date_ss(value_str);
            if (!(date_ss >> new_date) || (date_ss >> std::ws && !date_ss.eof())) {
                 throw std::invalid_argument("EDIT SET: Некорректный формат даты '" + value_str + "' в секции SET.");
            }
            record_to_edit.setDate(new_date);
        } else {
             // QueryParser должен был отфильтровать неизвестные поля, но на всякий случай.
             Logger::warn("[SCH Edit] Обнаружено неизвестное поле '" + pair.first + "' в params.setData при попытке редактирования. Пропущено.");
        }
    }
    if (params.hasTrafficInToSet) {
        if(params.trafficInData.size() != static_cast<size_t>(HOURS_IN_DAY)) throw std::runtime_error("EDIT SET: TRAFFIC_IN должен содержать " + std::to_string(HOURS_IN_DAY) + " значений.");
        record_to_edit.setTrafficInByHour(params.trafficInData);
    }
    if (params.hasTrafficOutToSet) {
        if(params.trafficOutData.size() != static_cast<size_t>(HOURS_IN_DAY)) throw std::runtime_error("EDIT SET: TRAFFIC_OUT должен содержать " + std::to_string(HOURS_IN_DAY) + " значений.");
        record_to_edit.setTrafficOutByHour(params.trafficOutData);
    }

    changed_applied = (record_to_edit != original_record_for_comparison);

    if (changed_applied) {
        db_.editRecord(target_db_index, record_to_edit);
        oss << "Запись с Индексом в БД " << target_db_index << " успешно отредактирована сервером.\n";
        oss << "Новые данные:\n-----------------------------------------------------------------\n";
        oss << "Запись (Индекс в БД: " << target_db_index << "):\n" << db_.getRecordByIndex(target_db_index);
        oss << "\n-----------------------------------------------------------------\n";
    } else {
        oss << "EDIT Информация [Сервер]: Не было применено никаких изменений к записи (новые данные идентичны существующим или поля в SET не привели к изменениям).\n";
    }
}

void ServerCommandHandler::handleCalculateCharges(const QueryParameters& params, std::ostringstream& oss) {
    if (!params.useStartDateFilter || !params.useEndDateFilter) {
        throw std::runtime_error("CALCULATE_CHARGES: Команда требует обязательного указания START_DATE и END_DATE.");
    }
    if (params.criteriaStartDate > params.criteriaEndDate) {
         throw std::runtime_error("CALCULATE_CHARGES: START_DATE (" + params.criteriaStartDate.toString() + ") не может быть позже END_DATE (" + params.criteriaEndDate.toString() + ").");
    }

    std::vector<ProviderRecord> records_to_process;
     if (!params.useNameFilter && !params.useIpFilter && !params.useDateFilter) {
        records_to_process = db_.getAllRecords();
    } else {
        std::vector<size_t> record_indices = db_.findRecordsByCriteria(
            params.criteriaName, params.useNameFilter,
            params.criteriaIpAddress, params.useIpFilter,
            params.criteriaDate, params.useDateFilter);
        if (record_indices.empty()){
            oss << "OK\nНе найдено записей по указанным критериям фильтрации для расчета начислений.\n";
            return;
        }
        records_to_process.reserve(record_indices.size());
        for(size_t index : record_indices) {
            try { records_to_process.push_back(db_.getRecordByIndex(index)); }
            catch (const std::out_of_range&) { }
        }
    }
    oss << "OK\n";
     if (records_to_process.empty()){ 
        oss << "В базе данных (или по указанным критериям) нет записей для расчета начислений.\n";
        return;
    }

    double grandTotalCharges = 0.0;
    oss << "Отчет по расчету стоимости за период (" << params.criteriaStartDate.toString() << " - " << params.criteriaEndDate.toString() << ") на сервере:\n";
    oss << "-----------------------------------------------------------------\n";
    oss << std::fixed << std::setprecision(2); 
    bool charges_calculated_for_at_least_one = false;

    for (const auto& record : records_to_process) {
        if (record.getDate() >= params.criteriaStartDate && record.getDate() <= params.criteriaEndDate) {
            double charge = 0.0;
            try {
                charge = db_.calculateChargesForRecord(record, tariff_plan_, params.criteriaStartDate, params.criteriaEndDate);
                oss << "Абонент: " << record.getName() << " (IP: " << record.getIpAddress().toString()
                    << ", Дата записи: " << record.getDate().toString() << ") | Начислено: " << charge << "\n";
                charges_calculated_for_at_least_one = true;
                grandTotalCharges += charge;
            }
            catch (const std::exception& e) { 
                oss << "Абонент: " << record.getName() << " | Ошибка расчета: " << e.what() << "\n";
                Logger::error("[SCH Calc] Ошибка расчета для " + record.getName() + ": " + e.what());
            }
        }
    }
    if (!charges_calculated_for_at_least_one && !records_to_process.empty()) { 
        oss << "Для выбранных записей начисления отсутствуют (возможно, все записи вне периода расчета или тарифы нулевые/ошибочны).\n";
    } 

    oss << "-----------------------------------------------------------------\n";
    oss << "ИТОГО начислено для выборки на сервере: " << grandTotalCharges << "\n";
    oss << "-----------------------------------------------------------------\n";
}

void ServerCommandHandler::handlePrintAll(std::ostringstream& oss) {
    const size_t count = db_.getRecordCount();
    oss << "OK\n"; 
    if (count == 0) {
        oss << "База данных на сервере пуста.\n";
    } else {
        oss << "Содержимое базы данных на сервере (" << count << " записей):\n";
        oss << "-----------------------------------------------------------------\n";
        for (size_t i = 0; i < count; ++i) {
            try {
                const ProviderRecord& rec = db_.getRecordByIndex(i);
                oss << "Запись (Индекс в БД #" << i << "):\n";
                oss << rec; 
                oss << "\n-----------------------------------------------------------------\n";
            } catch(const std::out_of_range& e) {
                Logger::error("[SCH PrintAll] Критическая ошибка доступа к записи " + std::to_string(i) + ": " + e.what());
                oss << "ОШИБКА [Сервер]: Внутренняя ошибка при доступе к записи по индексу " << i << ": " << e.what() << "\n";
                oss << "-----------------------------------------------------------------\n";
            }
        }
    }
}

void ServerCommandHandler::handleLoad(const QueryParameters& params, std::ostringstream& oss) {
    if (params.filename.empty()) {
        throw std::runtime_error("LOAD: Команда требует имя файла.");
    }
    std::filesystem::path target_file_path = getSafeServerFilePath_SCH(server_data_base_path_, params.filename, DEFAULT_SERVER_DATA_SUBDIR);

    FileOperationResult load_res = db_.loadFromFile(target_file_path.string());

    if (load_res.success) {
        oss << "OK\n" << load_res.user_message << "\n";
    } else {
        oss << "ERROR\n" << load_res.user_message << "\n";
    }
}

void ServerCommandHandler::handleSave(const QueryParameters& params, std::ostringstream& oss) {
    std::string filename_to_save_on_server_final;
    FileOperationResult save_res;

    if (params.filename.empty()) { 
        filename_to_save_on_server_final = db_.getCurrentFilename();
        if (filename_to_save_on_server_final.empty()) {
            throw std::runtime_error("SAVE: Имя файла для сохранения не указано и не было установлено ранее (через LOAD или SAVE с именем). Некуда сохранять.");
        }
        save_res = db_.saveToFile(); 
    } else { 
        std::filesystem::path target_file_path = getSafeServerFilePath_SCH(server_data_base_path_, params.filename, DEFAULT_SERVER_DATA_SUBDIR);
        filename_to_save_on_server_final = target_file_path.string();
        save_res = db_.saveToFile(filename_to_save_on_server_final);
    }

    if (save_res.success) {
        oss << "OK\n" << save_res.user_message << "\n";
    } else {
        oss << "ERROR\n" << save_res.user_message << "\n";
    }
}
