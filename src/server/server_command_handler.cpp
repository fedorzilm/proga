/*!
 * \file server_command_handler.cpp
 * \brief Реализация класса ServerCommandHandler для обработки команд сервера и отправки структурированных ответов.
 */
#include "server_command_handler.h"
#include "logger.h"
#include "file_utils.h"
#include "provider_record.h"
#include "ip_address.h"
#include "date.h"

#include <filesystem>
#include <iomanip>
#include <algorithm>
#include <vector>


static std::string schToUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return static_cast<char>(std::toupper(c)); });
    return s;
}


ServerCommandHandler::ServerCommandHandler(Database& db, TariffPlan& plan, const std::string& server_data_path_base)
    : db_(db), tariff_plan_(plan), server_data_base_path_(server_data_path_base) {
    Logger::debug("[Обработчик Команд Сервера] Конструктор: Инициализирован с server_data_base_path: '" + server_data_base_path_ + "'");
}

void ServerCommandHandler::formatRecordsToStream(std::ostringstream& oss,
                                               const std::vector<ProviderRecord>& records,
                                               size_t start_index,
                                               size_t count,
                                               bool add_display_indices,
                                               [[maybe_unused]] const std::map<size_t, size_t>* db_indices_map) const {
    if (start_index >= records.size()) {
        return;
    }
    size_t end_i = std::min(start_index + count, records.size());
    for (size_t i = start_index; i < end_i; ++i) {
        if (add_display_indices) {
            oss << "Запись (Отображаемый Индекс в текущем наборе #" << (i - start_index) << "):\n";
        }
        // Используем существующий operator<< для ProviderRecord
        oss << records[i];
        // Добавляем разделитель после каждой записи, кроме последней в чанке/выборке
        if (i < end_i - 1) {
            oss << "\n-----------------------------------------------------------------\n";
        }
    }
}


void ServerCommandHandler::sendSingleMessageResponsePart(std::shared_ptr<TCPSocket> client_socket, const ServerResponse& response) const {
    if (!client_socket || !client_socket->isValid()) {
        Logger::error("[SCH Отправка Одной Части] Попытка отправить ответ через невалидный сокет. Клиент мог отключиться.");
        return;
    }
    std::ostringstream header_oss;
    header_oss << SRV_HEADER_STATUS << ": " << response.statusCode << "\n"
               << SRV_HEADER_MESSAGE << ": " << (response.statusMessage.empty() ? (response.statusCode < SRV_STATUS_BAD_REQUEST ? "OK" : "Ошибка") : response.statusMessage) << "\n"
               << SRV_HEADER_RECORDS_IN_PAYLOAD << ": " << response.recordsInPayload << "\n"
               << SRV_HEADER_TOTAL_RECORDS << ": " << response.totalRecordsOverall << "\n"
               << SRV_HEADER_PAYLOAD_TYPE << ": " << response.payloadType << "\n"
               << SRV_HEADER_DATA_MARKER << "\n";

    std::string payload_str = response.payloadDataStream.str();
    std::string full_response_str = header_oss.str() + payload_str;

    std::string log_msg_prefix = "[SCH Отправка Одной Части] Отправка ";
    if(response.statusCode == SRV_STATUS_OK_MULTI_PART_BEGIN) log_msg_prefix += "первой части (BEGIN): ";
    else if(response.statusCode == SRV_STATUS_OK_MULTI_PART_CHUNK) log_msg_prefix += "следующей части (CHUNK): ";
    else if(response.statusCode == SRV_STATUS_OK_MULTI_PART_END) log_msg_prefix += "сообщения о конце частей (END): ";
    else log_msg_prefix += "ответа: ";

    Logger::debug(log_msg_prefix + "Статус=" + std::to_string(response.statusCode) +
                  ", Сообщ=\"" + response.statusMessage + "\"" +
                  ", ДлинаЗаголовка: " + std::to_string(header_oss.str().length()) +
                  ", ДлинаНагрузки: " + std::to_string(payload_str.length()) +
                  ", ОбщаяДлинаСообщ: " + std::to_string(full_response_str.length()));

    if (!client_socket->sendAllDataWithLengthPrefix(full_response_str)) {
        Logger::error(log_msg_prefix + "Не удалось отправить часть ответа клиенту. Ошибка сокета: " + std::to_string(client_socket->getLastSocketError()) + ". Клиент мог отключиться.");
    }
}

void ServerCommandHandler::sendRemainingChunks(std::shared_ptr<TCPSocket> client_socket, const ServerResponse& initialResponseContext) const {
    if (!client_socket || !client_socket->isValid()) {
        Logger::error("[SCH Отправка Оставшихся Частей] Невалидный сокет для отправки оставшихся частей. Клиент мог отключиться.");
        return;
    }

    const auto& records_to_chunk = initialResponseContext.recordsForChunking;
    size_t total_records_in_set = initialResponseContext.totalRecordsOverall;
    size_t records_sent_in_first_chunk = initialResponseContext.recordsInPayload;

    if (records_sent_in_first_chunk > total_records_in_set) {
        Logger::error("[SCH Отправка Оставшихся Частей] Несоответствие: records_sent_in_first_chunk (" + std::to_string(records_sent_in_first_chunk) +
                      ") > total_records_in_set (" + std::to_string(total_records_in_set) + "). Прерывание передачи по частям.");
        return;
    }

    size_t records_remaining_to_send = total_records_in_set - records_sent_in_first_chunk;
    size_t current_offset_in_recordsForChunking = records_sent_in_first_chunk;

    Logger::debug("[SCH Отправка Ост Частей] Начало отправки оставшихся частей. Всего в наборе: " + std::to_string(total_records_in_set) +
                  ", Отправлено в первой части (BEGIN): " + std::to_string(records_sent_in_first_chunk) + ", Осталось отправить: " + std::to_string(records_remaining_to_send));

    while (records_remaining_to_send > 0) {
        if (!client_socket->isValid()) {
            Logger::warn("[SCH Отправка Оставшихся Частей] Соединение с клиентом потеряно во время отправки последующих частей.");
            return;
        }
        ServerResponse chunk_response;
        chunk_response.statusCode = SRV_STATUS_OK_MULTI_PART_CHUNK;
        chunk_response.payloadType = SRV_PAYLOAD_TYPE_PROVIDER_RECORDS_LIST;

        size_t num_records_in_this_chunk = std::min(records_remaining_to_send, SRV_DEFAULT_CHUNK_RECORDS_COUNT);

        formatRecordsToStream(chunk_response.payloadDataStream,
                              records_to_chunk,
                              current_offset_in_recordsForChunking,
                              num_records_in_this_chunk,
                              true);

        chunk_response.recordsInPayload = num_records_in_this_chunk;
        chunk_response.totalRecordsOverall = 0; // В CHUNK частях это поле нерелевантно
        
        chunk_response.statusMessage = "Продолжение многочастного ответа.";


        sendSingleMessageResponsePart(client_socket, chunk_response);

        current_offset_in_recordsForChunking += num_records_in_this_chunk;
        records_remaining_to_send -= num_records_in_this_chunk;
        Logger::debug("[SCH Отправка Оставшихся Частей] Отправлена часть с " + std::to_string(num_records_in_this_chunk) + " записями. Текущее смещение в данных для чанкинга: " + std::to_string(current_offset_in_recordsForChunking) + ", Осталось отправить: " + std::to_string(records_remaining_to_send));
    }

    if (client_socket->isValid()) {
        ServerResponse end_response;
        end_response.statusCode = SRV_STATUS_OK_MULTI_PART_END;
        end_response.statusMessage = "Многочастная передача данных завершена.";
        end_response.payloadType = SRV_PAYLOAD_TYPE_NONE;
        end_response.totalRecordsOverall = 0; 
        end_response.recordsInPayload = 0;
        sendSingleMessageResponsePart(client_socket, end_response);
        Logger::debug("[SCH Отправка Оставшихся Частей] Отправлено сообщение о завершении многочастной передачи.");
    }
}


void ServerCommandHandler::processAndSendCommandResponse(std::shared_ptr<TCPSocket> client_socket, const Query& query) {
    ServerResponse response;
    const std::string sch_cmd_log_prefix = "[SCH Обработка И Отправка] ";
    Logger::debug(sch_cmd_log_prefix + "Обработка команды: " + query.originalQueryString);

    try {
        switch (query.type) {
            case QueryType::ADD:               handleAdd(query.params, response); break;
            case QueryType::SELECT:            handleSelect(query.params, response); break;
            case QueryType::DELETE:            handleDelete(query.params, response); break;
            case QueryType::EDIT:              handleEdit(query.params, response); break;
            case QueryType::CALCULATE_CHARGES: handleCalculateCharges(query.params, response); break;
            case QueryType::PRINT_ALL:         handlePrintAll(response); break;
            case QueryType::LOAD:              handleLoad(query.params, response); break;
            case QueryType::SAVE:              handleSave(query.params, response); break;
            case QueryType::HELP:              handleHelp(response); break;
            case QueryType::EXIT:              handleExit(response); break;
            case QueryType::UNKNOWN:
            default:                           handleUnknown(query, response); break;
        }
    } catch (const std::invalid_argument& iae) {
        response.reset();
        response.statusCode = SRV_STATUS_BAD_REQUEST;
        response.statusMessage = "Неверный аргумент в команде: " + std::string(iae.what());
        response.payloadType = SRV_PAYLOAD_TYPE_ERROR_INFO;
        response.payloadDataStream << "Детали ошибки: Серверу предоставлен неверный аргумент.\n"
                                   << "Сообщение сервера: " << iae.what() << "\n"
                                   << "Оригинальный запрос: \"" << query.originalQueryString << "\"\n";
        Logger::error(sch_cmd_log_prefix + "InvalidArgument для '" + query.originalQueryString + "': " + iae.what());
    } catch (const std::out_of_range& oor) {
        response.reset();
        response.statusCode = SRV_STATUS_NOT_FOUND;
        response.statusMessage = "Запрошенный элемент не найден или вне диапазона: " + std::string(oor.what());
        response.payloadType = SRV_PAYLOAD_TYPE_ERROR_INFO;
        response.payloadDataStream << "Детали ошибки: Элемент не найден или индекс вне диапазона.\n"
                                   << "Сообщение сервера: " << oor.what() << "\n"
                                   << "Оригинальный запрос: \"" << query.originalQueryString << "\"\n";
        Logger::error(sch_cmd_log_prefix + "OutOfRange для '" + query.originalQueryString + "': " + oor.what());
    } catch (const std::filesystem::filesystem_error& fs_err) {
        response.reset();
        response.statusCode = SRV_STATUS_SERVER_ERROR;
        response.statusMessage = "Ошибка файловой системы на сервере: " + std::string(fs_err.what());
        response.payloadType = SRV_PAYLOAD_TYPE_ERROR_INFO;
        response.payloadDataStream << "Детали ошибки: На сервере произошла ошибка файловой системы.\n"
                                   << "Сообщение сервера: " << fs_err.what() << "\n"
                                   << "Оригинальный запрос: \"" << query.originalQueryString << "\"\n";
        Logger::error(sch_cmd_log_prefix + "FilesystemError для '" + query.originalQueryString + "': " + fs_err.what());
    }
    catch (const std::runtime_error& rte) { 
        response.reset();
        response.statusCode = SRV_STATUS_SERVER_ERROR;
        response.statusMessage = "Ошибка выполнения на сервере: " + std::string(rte.what());
        response.payloadType = SRV_PAYLOAD_TYPE_ERROR_INFO;
        response.payloadDataStream << "Детали ошибки: На сервере произошла ошибка выполнения.\n"
                                   << "Сообщение сервера: " << rte.what() << "\n"
                                   << "Оригинальный запрос: \"" << query.originalQueryString << "\"\n";
        Logger::error(sch_cmd_log_prefix + "RuntimeError для '" + query.originalQueryString + "': " + rte.what());
    } catch (const std::exception& e) { 
        response.reset();
        response.statusCode = SRV_STATUS_SERVER_ERROR;
        response.statusMessage = "Неожиданная ошибка сервера: " + std::string(e.what());
        response.payloadType = SRV_PAYLOAD_TYPE_ERROR_INFO;
        response.payloadDataStream << "Детали ошибки: На сервере произошла неожиданная системная ошибка.\n"
                                   << "Сообщение сервера: " << e.what() << "\n"
                                   << "Оригинальный запрос: \"" << query.originalQueryString << "\"\n";
        Logger::error(sch_cmd_log_prefix + "StdException для '" + query.originalQueryString + "': " + e.what());
    } catch (...) {
        response.reset();
        response.statusCode = SRV_STATUS_SERVER_ERROR;
        response.statusMessage = "Произошла неизвестная критическая ошибка сервера.";
        response.payloadType = SRV_PAYLOAD_TYPE_ERROR_INFO;
        response.payloadDataStream << "Детали ошибки: На сервере произошла неизвестная критическая ошибка.\n"
                                   << "Оригинальный запрос: \"" << query.originalQueryString << "\"\n";
        Logger::error(sch_cmd_log_prefix + "Неизвестное исключение (...) для '" + query.originalQueryString + "'.");
    }

    sendSingleMessageResponsePart(client_socket, response); 
    if (response.requiresChunking && response.statusCode == SRV_STATUS_OK_MULTI_PART_BEGIN) {
        sendRemainingChunks(client_socket, response);
    }
}


void ServerCommandHandler::handleAdd(const QueryParameters& params, ServerResponse& response) {
    std::vector<double> trafficInToAdd = params.trafficInData;
    std::vector<double> trafficOutToAdd = params.trafficOutData;

    if (trafficInToAdd.empty() && !params.hasTrafficInToSet) {
        trafficInToAdd.assign(HOURS_IN_DAY, 0.0);
    } else if (params.hasTrafficInToSet && trafficInToAdd.size() != static_cast<size_t>(HOURS_IN_DAY)) {
        throw std::invalid_argument("ADD: TRAFFIC_IN должен содержать " + std::to_string(HOURS_IN_DAY) + " значений, получено " + std::to_string(trafficInToAdd.size()));
    }

    if (trafficOutToAdd.empty() && !params.hasTrafficOutToSet) {
        trafficOutToAdd.assign(HOURS_IN_DAY, 0.0);
    } else if (params.hasTrafficOutToSet && trafficOutToAdd.size() != static_cast<size_t>(HOURS_IN_DAY)) {
         throw std::invalid_argument("ADD: TRAFFIC_OUT должен содержать " + std::to_string(HOURS_IN_DAY) + " значений, получено " + std::to_string(trafficOutToAdd.size()));
    }

    ProviderRecord newRecord(
        params.subscriberNameData,
        params.ipAddressData,
        params.dateData,
        trafficInToAdd,
        trafficOutToAdd
    );

    db_.addRecord(newRecord);
    Logger::debug("[Database Добавление Записи] Запись для '" + params.subscriberNameData + "' добавлена. Всего записей: " + std::to_string(db_.getRecordCount()));

    response.statusCode = SRV_STATUS_OK;
    response.statusMessage = "Запись успешно добавлена.";
    response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE; 
    response.payloadDataStream << "Запись для абонента '" << params.subscriberNameData << "' была успешно добавлена в базу данных.";
}

void ServerCommandHandler::handleSelect(const QueryParameters& params, ServerResponse& response) {
    std::vector<size_t> original_indices = db_.findRecordsByCriteria(
        params.criteriaName, params.useNameFilter,
        params.criteriaIpAddress, params.useIpFilter,
        params.criteriaDate, params.useDateFilter
    );
    Logger::debug("[Database Поиск По Критериям] Найдено " + std::to_string(original_indices.size()) + " записей по заданным критериям.");

    if (original_indices.empty()) {
        response.statusCode = SRV_STATUS_OK; // Или SRV_STATUS_NOT_FOUND если так требует семантика
        response.statusMessage = "Не найдено записей, соответствующих критериям.";
        response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
        response.payloadDataStream << "На сервере не найдено записей, соответствующих указанным критериям.";
        return;
    }

    std::vector<ProviderRecord> found_records_copies;
    found_records_copies.reserve(original_indices.size());
    for (size_t db_idx : original_indices) {
        try {
            found_records_copies.push_back(db_.getRecordByIndex(db_idx));
        } catch (const std::out_of_range& e) {
            Logger::warn("[SCH Выборка] Запись с оригинальным индексом БД " + std::to_string(db_idx) + " не найдена во время сбора: " + e.what());
        }
    }

    if (found_records_copies.empty() && !original_indices.empty()){
        response.statusCode = SRV_STATUS_NOT_FOUND;
        response.statusMessage = "Записи, найденные по критериям, более недоступны.";
        response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
        response.payloadDataStream << "Записи, первоначально найденные по критериям, не удалось извлечь (например, удалены параллельно).";
        return;
    }
    
    response.statusMessage = std::to_string(found_records_copies.size()) + " записей успешно выбрано.";
    response.payloadType = SRV_PAYLOAD_TYPE_PROVIDER_RECORDS_LIST;
    response.totalRecordsOverall = found_records_copies.size();

    if (found_records_copies.size() >= SRV_CHUNKING_THRESHOLD_RECORDS) { // ИЗМЕНЕНО: >= вместо > для большей точности с порогом
        response.statusCode = SRV_STATUS_OK_MULTI_PART_BEGIN;
        response.statusMessage = "Начало многочастного ответа.";
        response.requiresChunking = true;
        response.recordsForChunking = std::move(found_records_copies);
        response.recordsInPayload = std::min(response.totalRecordsOverall, SRV_DEFAULT_CHUNK_RECORDS_COUNT);
        formatRecordsToStream(response.payloadDataStream, response.recordsForChunking, 0, response.recordsInPayload, true);
        Logger::info("[SCH Выборка] Найдено " + std::to_string(response.totalRecordsOverall) + " записей. Используется передача по частям. Первая часть содержит " + std::to_string(response.recordsInPayload) + " записей.");
    } else {
        response.statusCode = SRV_STATUS_OK;
        response.requiresChunking = false;
        formatRecordsToStream(response.payloadDataStream, found_records_copies, 0, found_records_copies.size(), true);
        response.recordsInPayload = found_records_copies.size();
        Logger::info("[SCH Выборка] Найдено " + std::to_string(found_records_copies.size()) + " записей. Отправка одной частью.");
    }
}

void ServerCommandHandler::handlePrintAll(ServerResponse& response) {
    std::vector<ProviderRecord> all_records_copy = db_.getAllRecords();

    if (all_records_copy.empty()) {
        response.statusCode = SRV_STATUS_OK;
        response.statusMessage = "База данных пуста.";
        response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
        response.payloadDataStream << "База данных на сервере в настоящее время пуста.";
        return;
    }
    
    response.statusMessage = "Все " + std::to_string(all_records_copy.size()) + " записей успешно извлечены.";
    response.payloadType = SRV_PAYLOAD_TYPE_PROVIDER_RECORDS_LIST;
    response.totalRecordsOverall = all_records_copy.size();

    if (all_records_copy.size() >= SRV_CHUNKING_THRESHOLD_RECORDS) { // ИЗМЕНЕНО: >= вместо >
        response.statusCode = SRV_STATUS_OK_MULTI_PART_BEGIN;
        response.statusMessage = "Начало многочастного ответа.";
        response.requiresChunking = true;
        response.recordsForChunking = std::move(all_records_copy);
        response.recordsInPayload = std::min(response.totalRecordsOverall, SRV_DEFAULT_CHUNK_RECORDS_COUNT);
        formatRecordsToStream(response.payloadDataStream, response.recordsForChunking, 0, response.recordsInPayload, true);
        Logger::info("[SCH Печать Всех] База данных содержит " + std::to_string(response.totalRecordsOverall) + " записей. Используется передача по частям. Первая часть содержит " + std::to_string(response.recordsInPayload) + " записей.");
    } else {
        response.statusCode = SRV_STATUS_OK;
        response.requiresChunking = false;
        formatRecordsToStream(response.payloadDataStream, all_records_copy, 0, all_records_copy.size(), true);
        response.recordsInPayload = all_records_copy.size();
        Logger::info("[SCH Печать Всех] База данных содержит " + std::to_string(all_records_copy.size()) + " записей. Отправка одной частью.");
    }
}


void ServerCommandHandler::handleDelete(const QueryParameters& params, ServerResponse& response) {
    std::vector<size_t> indices_to_delete = db_.findRecordsByCriteria(
        params.criteriaName, params.useNameFilter,
        params.criteriaIpAddress, params.useIpFilter,
        params.criteriaDate, params.useDateFilter
    );
    Logger::debug("[Database Поиск По Критериям] Найдено " + std::to_string(indices_to_delete.size()) + " записей для удаления.");


    if (indices_to_delete.empty()) {
        response.statusCode = SRV_STATUS_OK; // Или SRV_STATUS_NOT_FOUND
        response.statusMessage = "Не найдено записей, соответствующих критериям для удаления.";
        response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
        response.payloadDataStream << "На сервере не найдено записей, соответствующих указанным критериям для удаления.";
        return;
    }

    size_t num_deleted = db_.deleteRecordsByIndices(indices_to_delete);
    Logger::info("[Database Удаление По Индексам] Удалено " + std::to_string(num_deleted) + " записей.");
    response.statusCode = SRV_STATUS_OK;
    response.statusMessage = std::to_string(num_deleted) + " записей успешно удалено.";
    response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
    response.payloadDataStream << "Успешно удалено " << num_deleted << " записей из базы данных сервера.";
    response.recordsInPayload = num_deleted;
}

void ServerCommandHandler::handleEdit(const QueryParameters& params, ServerResponse& response) {
    if (params.setData.empty() && !params.hasTrafficInToSet && !params.hasTrafficOutToSet) {
        throw std::invalid_argument("EDIT: Секция SET отсутствует или не содержит полей для изменения.");
    }

    std::vector<size_t> indices_to_edit = db_.findRecordsByCriteria(
        params.criteriaName, params.useNameFilter,
        params.criteriaIpAddress, params.useIpFilter,
        params.criteriaDate, params.useDateFilter
    );
     Logger::debug("[Database Поиск По Критериям] Найдено " + std::to_string(indices_to_edit.size()) + " записей для редактирования.");


    if (indices_to_edit.empty()) {
        response.statusCode = SRV_STATUS_NOT_FOUND;
        response.statusMessage = "Не найдено записей, соответствующих критериям для редактирования.";
        response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
        response.payloadDataStream << "Не найдено записей, соответствующих указанным критериям для редактирования.";
        Logger::info("[SCH Редактирование] Не найдено записей для редактирования по критериям.");
        return;
    }

    size_t target_db_index = indices_to_edit[0];
    std::string edit_prelude_message_str;

    if (indices_to_edit.size() > 1) {
        std::ostringstream warn_oss;
        warn_oss << "EDIT Предупреждение: Критерии совпали с " << indices_to_edit.size()
                 << " записями. Будет отредактирована только первая найденная запись (Оригинальный индекс в БД: " << target_db_index << ").\n";
        edit_prelude_message_str = warn_oss.str();
        Logger::warn("[SCH Редактирование] Найдено " + std::to_string(indices_to_edit.size()) +
                     " записей для редактирования, будет обработана только первая (оригинальный индекс БД " + std::to_string(target_db_index) + ")");
    }

    ProviderRecord record_to_edit = db_.getRecordByIndex(target_db_index);
    ProviderRecord original_record_for_comparison = record_to_edit;

    for (const auto& pair : params.setData) {
        const std::string& field_key_upper = schToUpper(pair.first);
        const std::string& value_str = pair.second;
        if (field_key_upper == "FIO") { record_to_edit.setName(value_str); }
        else if (field_key_upper == "IP") {
            IPAddress new_ip; std::istringstream ip_ss(value_str);
            if (!(ip_ss >> new_ip) || (ip_ss.peek() != EOF && !(ip_ss >> std::ws).eof() ) ) throw std::invalid_argument("EDIT SET: Неверный формат IP '" + value_str + "' для поля IP.");
            record_to_edit.setIpAddress(new_ip);
        } else if (field_key_upper == "DATE") {
            Date new_date; std::istringstream date_ss(value_str);
            if (!(date_ss >> new_date) || (date_ss.peek() != EOF && !(date_ss >> std::ws).eof() ) ) throw std::invalid_argument("EDIT SET: Неверный формат даты '" + value_str + "' для поля DATE.");
            record_to_edit.setDate(new_date);
        } else {
             Logger::warn("[SCH Редактирование] Неизвестное поле '" + pair.first + "' в данных SET. Пропущено.");
        }
    }
    if (params.hasTrafficInToSet) {
        if(params.trafficInData.size() != static_cast<size_t>(HOURS_IN_DAY)) throw std::invalid_argument("EDIT SET: Блок TRAFFIC_IN должен содержать " + std::to_string(HOURS_IN_DAY) + " значений.");
        record_to_edit.setTrafficInByHour(params.trafficInData);
    }
    if (params.hasTrafficOutToSet) {
        if(params.trafficOutData.size() != static_cast<size_t>(HOURS_IN_DAY)) throw std::invalid_argument("EDIT SET: Блок TRAFFIC_OUT должен содержать " + std::to_string(HOURS_IN_DAY) + " значений.");
        record_to_edit.setTrafficOutByHour(params.trafficOutData);
    }

    bool changed_applied = (record_to_edit != original_record_for_comparison);

    if (changed_applied) {
        db_.editRecord(target_db_index, record_to_edit);
         Logger::debug("[Database Редактирование Записи] Запись по индексу " + std::to_string(target_db_index) + " (абонент: '" + record_to_edit.getName() + "') отредактирована.");
        response.statusCode = SRV_STATUS_OK;
        response.statusMessage = "1 запись успешно изменена.";
        response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
        response.payloadDataStream << edit_prelude_message_str
                                   << "Успешно изменена 1 запись.\n";
        response.recordsInPayload = 1;
    } else {
        response.statusCode = SRV_STATUS_OK;
        response.statusMessage = "Изменения к записи не применены (новые данные идентичны или не привели к эффективным изменениям).";
        response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
        response.payloadDataStream << edit_prelude_message_str
                                   << "EDIT Информация: Изменения к записи не применены, так как новые данные были идентичны существующим, или поля SET не привели к эффективным изменениям.";
        response.recordsInPayload = 0;
    }
}

void ServerCommandHandler::handleCalculateCharges(const QueryParameters& params, ServerResponse& response) {
    if (!params.useStartDateFilter || !params.useEndDateFilter) {
        throw std::invalid_argument("CALCULATE_CHARGES: Команда требует указания параметров START_DATE и END_DATE.");
    }
    if (params.criteriaStartDate > params.criteriaEndDate) {
         throw std::invalid_argument("CALCULATE_CHARGES: START_DATE (" + params.criteriaStartDate.toString() + ") не может быть позже END_DATE (" + params.criteriaEndDate.toString() + ").");
    }

    std::vector<ProviderRecord> records_to_process_copies;
    if (!params.useNameFilter && !params.useIpFilter && !params.useDateFilter) {
        records_to_process_copies = db_.getAllRecords();
    } else {
        std::vector<size_t> record_indices = db_.findRecordsByCriteria(
            params.criteriaName, params.useNameFilter,
            params.criteriaIpAddress, params.useIpFilter,
            params.criteriaDate, params.useDateFilter
        );
        if (record_indices.empty()){
            response.statusCode = SRV_STATUS_OK; // Или SRV_STATUS_NOT_FOUND
            response.statusMessage = "Не найдено записей, соответствующих критериям фильтра для расчета начислений.";
            response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
            response.payloadDataStream << "Не найдено записей по указанным критериям фильтра для расчета начислений.";
            return;
        }
        records_to_process_copies.reserve(record_indices.size());
        for(size_t index : record_indices) {
            try { records_to_process_copies.push_back(db_.getRecordByIndex(index)); }
            catch (const std::out_of_range&) { /* Игнорируем, если удалена */ }
        }
    }

    if (records_to_process_copies.empty()){
        response.statusCode = SRV_STATUS_OK; // Или SRV_STATUS_NOT_FOUND
        response.statusMessage = "Нет доступных записей (или соответствующих фильтру) для расчета начислений.";
        response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
        response.payloadDataStream << "В базе данных нет доступных записей (или соответствующих критериям фильтра) для расчета начислений.";
        return;
    }

    double grandTotalCharges = 0.0;
    size_t records_charged_count = 0;
    std::ostringstream report_oss;
    report_oss << "Отчет о расчете начислений за период (" << params.criteriaStartDate.toString() << " - " << params.criteriaEndDate.toString() << "):\n";
    report_oss << "-----------------------------------------------------------------\n";
    report_oss << std::fixed << std::setprecision(2);

    for (const auto& record_copy : records_to_process_copies) {
        double charge_for_record = 0.0;
        try {
            charge_for_record = db_.calculateChargesForRecord(record_copy, tariff_plan_, params.criteriaStartDate, params.criteriaEndDate);
            if (record_copy.getDate() >= params.criteriaStartDate && record_copy.getDate() <= params.criteriaEndDate) {
                report_oss << "Абонент: " << record_copy.getName()
                           << " (IP: " << record_copy.getIpAddress().toString()
                           << ", Дата записи: " << record_copy.getDate().toString()
                           << ") | Рассчитанные начисления: " << charge_for_record << "\n";
                grandTotalCharges += charge_for_record;
                records_charged_count++;
                Logger::debug("[Database Расчет Начислений] Для '" + record_copy.getName() + "' (" + record_copy.getDate().toString() + ") начислено: " + std::to_string(charge_for_record) + " за период [" + params.criteriaStartDate.toString() + " - " + params.criteriaEndDate.toString() + "].");
            }
        }
        catch (const std::exception& e) {
            report_oss << "Абонент: " << record_copy.getName() << " | Ошибка при расчете начислений: " << e.what() << "\n";
            Logger::error("[SCH Расчет Начислений] Ошибка расчета начислений для абонента " + record_copy.getName() + ": " + e.what());
        }
    }

    if (records_charged_count == 0 && !records_to_process_copies.empty()) {
        report_oss << "Начисления для выбранных записей в указанный период отсутствуют (или тарифы нулевые/ошибочные).\n";
    }
    report_oss << "-----------------------------------------------------------------\n";
    report_oss << "ОБЩАЯ СУММА рассчитанных начислений для выборки: " << grandTotalCharges << "\n";
    report_oss << "-----------------------------------------------------------------\n";

    response.payloadDataStream.str(report_oss.str()); 
    response.statusCode = SRV_STATUS_OK;
    // ИЗМЕНЕНО: убрано (ей|и|ь)
    response.statusMessage = "Расчет успешно выполнен для " + std::to_string(records_charged_count) + " записей.";
    response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
    response.recordsInPayload = records_charged_count;
}


void ServerCommandHandler::handleLoad(const QueryParameters& params, ServerResponse& response) {
    if (params.filename.empty()) {
        throw std::invalid_argument("LOAD: Команда требует параметр имени файла.");
    }
    std::filesystem::path target_file_path;
    try {
        target_file_path = FileUtils::getSafeServerFilePath(server_data_base_path_, params.filename, DEFAULT_SERVER_DATA_SUBDIR);
        Logger::info("[SCH Загрузка] Попытка загрузки из разрешенного безопасного пути: '" + target_file_path.string() + "'");
    } catch (const std::exception& e) {
        throw std::runtime_error("LOAD: Ошибка определения безопасного пути к файлу для '" + params.filename + "': " + std::string(e.what()));
    }

    FileOperationResult load_res = db_.loadFromFile(target_file_path.string());

    response.statusCode = load_res.success ? SRV_STATUS_OK : SRV_STATUS_SERVER_ERROR;
    response.statusMessage = load_res.success ?
        "Данные успешно загружены из файла '" + params.filename + "'. Загружено " + std::to_string(load_res.records_processed) + " записей." :
        "Загрузка данных из файла '" + params.filename + "' не удалась.";
    response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
    response.payloadDataStream << load_res.user_message;
    response.recordsInPayload = load_res.records_processed;
    if (!load_res.success && !load_res.error_details.empty()){
        response.payloadDataStream << "\nДополнительные сведения об ошибке сервера: " << load_res.error_details;
    }
}

void ServerCommandHandler::handleSave(const QueryParameters& params, ServerResponse& response) {
    FileOperationResult save_res;
    std::string effective_filename_for_msg_and_save;

    if (params.filename.empty()) {
        effective_filename_for_msg_and_save = db_.getCurrentFilename();
        if (effective_filename_for_msg_and_save.empty()){
             throw std::invalid_argument("SAVE: Имя файла не указано, и на сервере отсутствует предыдущий файловый контекст для сохранения.");
        }
        Logger::info("[SCH Сохранение] Попытка сохранения в текущий файловый контекст БД: '" + effective_filename_for_msg_and_save + "'");
        save_res = db_.saveToFile();
    } else {
        std::filesystem::path target_file_path;
        try {
             target_file_path = FileUtils::getSafeServerFilePath(server_data_base_path_, params.filename, DEFAULT_SERVER_DATA_SUBDIR);
        } catch (const std::exception& e) {
            throw std::runtime_error("SAVE: Ошибка определения безопасного пути к файлу для '" + params.filename + "': " + std::string(e.what()));
        }
        effective_filename_for_msg_and_save = params.filename; 
        Logger::info("[SCH Сохранение] Попытка сохранения в указанный файл (разрешен как): '" + target_file_path.string() + "'");
        save_res = db_.saveToFile(target_file_path.string());
    }

    response.statusCode = save_res.success ? SRV_STATUS_OK : SRV_STATUS_SERVER_ERROR;
    response.statusMessage = save_res.success ?
        "Данные успешно сохранены в файл '" + effective_filename_for_msg_and_save + "'. Сохранено " + std::to_string(save_res.records_processed) + " записей." :
        "Сохранение данных в файл '" + effective_filename_for_msg_and_save + "' не удалось.";
    response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
    response.payloadDataStream << save_res.user_message;
    response.recordsInPayload = save_res.records_processed;
     if (!save_res.success && !save_res.error_details.empty()){
        response.payloadDataStream << "\nДополнительные сведения об ошибке сервера: " << save_res.error_details;
    }
}

void ServerCommandHandler::handleHelp(ServerResponse& response) {
    response.statusCode = SRV_STATUS_OK;
    response.statusMessage = "Список доступных команд:";
    response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
    response.payloadDataStream << "Поддерживаемые команды сервера: ADD, SELECT, DELETE, EDIT, CALCULATE_CHARGES, PRINT_ALL, LOAD, SAVE, HELP, EXIT.\n"
                               << "Для детального синтаксиса команд и параметров, пожалуйста, обратитесь к документации клиента или спецификациям проекта.\n";
}

void ServerCommandHandler::handleExit(ServerResponse& response) {
    response.statusCode = SRV_STATUS_OK;
    response.statusMessage = "Завершение сессии подтверждено сервером.";
    response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
    response.payloadDataStream << "Сервер подтверждает команду EXIT. Клиент теперь должен закрыть сессию соединения.";
}

void ServerCommandHandler::handleUnknown(const Query& query, ServerResponse& response) {
    response.statusCode = SRV_STATUS_BAD_REQUEST;
    response.statusMessage = "Неизвестный тип запроса получен сервером.";
    response.payloadType = SRV_PAYLOAD_TYPE_ERROR_INFO;
    response.payloadDataStream << "Ошибка: Сервер не понял команду или ее формат.\n"
                               << "Оригинальный запрос, полученный сервером: \"" << query.originalQueryString << "\"\n"
                               << "Пожалуйста, используйте HELP на стороне клиента для получения списка допустимых команд и их правильного синтаксиса.\n";
    Logger::warn("[SCH Неизвестная Команда] Получен неизвестный/некорректный запрос (разобранный тип: "
                 + std::to_string(static_cast<int>(query.type)) + ", оригинальная строка: '" + query.originalQueryString + "').");
}
