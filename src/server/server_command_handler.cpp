/*!
 * \file server_command_handler.cpp
 * \author Fedor Zilnitskiy
 * \brief Реализация класса ServerCommandHandler для обработки команд сервера и отправки структурированных ответов.
 */
#include "server_command_handler.h"
#include "logger.h"
#include "file_utils.h"      // Включаем для FileUtils::getSafeServerFilePath
#include "provider_record.h" // ProviderRecord::operator<<
#include "ip_address.h"      // Для парсинга IP в handleEdit
#include "date.h"            // Для парсинга Date в handleEdit

#include <filesystem> // Для std::filesystem::path
#include <iomanip>    // Для std::fixed, std::setprecision
#include <algorithm>  // Для std::min, std::transform (для schToUpper)
#include <vector>     // Для std::vector


// Вспомогательная функция для преобразования строки в верхний регистр
// Помечена как static, чтобы ограничить область видимости этим файлом
static std::string schToUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return static_cast<char>(std::toupper(c)); });
    return s;
}


ServerCommandHandler::ServerCommandHandler(Database& db, TariffPlan& plan, const std::string& server_data_path_base)
    : db_(db), tariff_plan_(plan), server_data_base_path_(server_data_path_base) {
    Logger::debug("[ServerCommandHandler] Constructor: Initialized with server_data_base_path: '" + server_data_base_path_ + "'");
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
            // Выводим индекс относительно начала текущей переданной коллекции записей (чанка или полной выборки)
            oss << "Record (Display Index in current set #" << (i - start_index) << "):\n";
        }
        oss << records[i]; 
        if (i < end_i - 1) { 
            oss << "\n-----------------------------------------------------------------\n";
        }
    }
}

// Этот метод теперь public, как мы решили для обработки ошибок парсинга в Server::clientHandlerTask
void ServerCommandHandler::sendSingleMessageResponsePart(std::shared_ptr<TCPSocket> client_socket, const ServerResponse& response) const {
    if (!client_socket || !client_socket->isValid()) {
        Logger::error("[SCH SendSinglePart] Attempt to send response via invalid socket. Client may have disconnected.");
        return;
    }
    std::ostringstream header_oss;
    header_oss << SRV_HEADER_STATUS << ": " << response.statusCode << "\n"
               << SRV_HEADER_MESSAGE << ": " << (response.statusMessage.empty() ? (response.statusCode < SRV_STATUS_BAD_REQUEST ? "OK" : "Error") : response.statusMessage) << "\n"
               << SRV_HEADER_RECORDS_IN_PAYLOAD << ": " << response.recordsInPayload << "\n"
               << SRV_HEADER_TOTAL_RECORDS << ": " << response.totalRecordsOverall << "\n" 
               << SRV_HEADER_PAYLOAD_TYPE << ": " << response.payloadType << "\n"
               << SRV_HEADER_DATA_MARKER << "\n";

    std::string payload_str = response.payloadDataStream.str();
    std::string full_response_str = header_oss.str() + payload_str;

    std::string log_msg_prefix = "[SCH SendSinglePart] Sending ";
    if(response.statusCode == SRV_STATUS_OK_MULTI_PART_BEGIN) log_msg_prefix += "first chunk: ";
    else if(response.statusCode == SRV_STATUS_OK_MULTI_PART_CHUNK) log_msg_prefix += "next chunk: ";
    else if(response.statusCode == SRV_STATUS_OK_MULTI_PART_END) log_msg_prefix += "end chunk message: ";
    else log_msg_prefix += "response: ";

    Logger::debug(log_msg_prefix + "Status=" + std::to_string(response.statusCode) +
                  ", Msg=\"" + response.statusMessage + "\"" +
                  ", HeaderLen: " + std::to_string(header_oss.str().length()) +
                  ", PayloadLen: " + std::to_string(payload_str.length()) +
                  ", TotalMsgLen: " + std::to_string(full_response_str.length()));

    if (!client_socket->sendAllDataWithLengthPrefix(full_response_str)) {
        Logger::error(log_msg_prefix + "Failed to send response part to client. Socket error: " + std::to_string(client_socket->getLastSocketError()) + ". Client may have disconnected.");
    }
}

void ServerCommandHandler::sendRemainingChunks(std::shared_ptr<TCPSocket> client_socket, const ServerResponse& initialResponseContext) const {
    if (!client_socket || !client_socket->isValid()) {
        Logger::error("[SCH SendRemChunks] Invalid socket for sending remaining chunks. Client may have disconnected.");
        return;
    }

    const auto& records_to_chunk = initialResponseContext.recordsForChunking;
    size_t total_records_in_set = initialResponseContext.totalRecordsOverall; 
    size_t records_sent_in_first_chunk = initialResponseContext.recordsInPayload; 

    if (records_sent_in_first_chunk > total_records_in_set) { 
        Logger::error("[SCH SendRemChunks] Inconsistency: records_sent_in_first_chunk (" + std::to_string(records_sent_in_first_chunk) +
                      ") > total_records_in_set (" + std::to_string(total_records_in_set) + "). Aborting chunking.");
        return;
    }

    size_t records_remaining_to_send = total_records_in_set - records_sent_in_first_chunk;
    size_t current_offset_in_recordsForChunking = records_sent_in_first_chunk; 

    Logger::debug("[SCH SendRemChunks] Starting to send remaining chunks. Total in set: " + std::to_string(total_records_in_set) +
                  ", Sent in first chunk: " + std::to_string(records_sent_in_first_chunk) + ", Remaining to send: " + std::to_string(records_remaining_to_send));

    while (records_remaining_to_send > 0) {
        if (!client_socket->isValid()) {
            Logger::warn("[SCH SendRemChunks] Client connection lost while sending subsequent chunks.");
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
        chunk_response.totalRecordsOverall = 0; 
        chunk_response.statusMessage = "Data chunk (records " + 
                                     std::to_string(current_offset_in_recordsForChunking + 1) + "-" + 
                                     std::to_string(current_offset_in_recordsForChunking + num_records_in_this_chunk) + 
                                     " of " + std::to_string(total_records_in_set) + ").";

        sendSingleMessageResponsePart(client_socket, chunk_response);

        current_offset_in_recordsForChunking += num_records_in_this_chunk;
        records_remaining_to_send -= num_records_in_this_chunk;
        Logger::debug("[SCH SendRemChunks] Sent chunk with " + std::to_string(num_records_in_this_chunk) + " records. Current offset in chunking data: " + std::to_string(current_offset_in_recordsForChunking) + ", Remaining to send: " + std::to_string(records_remaining_to_send));
    }

    if (client_socket->isValid()) {
        ServerResponse end_response;
        end_response.statusCode = SRV_STATUS_OK_MULTI_PART_END;
        end_response.statusMessage = "Multi-part data transfer complete. Total records sent: " + std::to_string(total_records_in_set);
        end_response.payloadType = SRV_PAYLOAD_TYPE_NONE;
        sendSingleMessageResponsePart(client_socket, end_response);
        Logger::debug("[SCH SendRemChunks] Sent multi-part end message.");
    }
}


void ServerCommandHandler::processAndSendCommandResponse(std::shared_ptr<TCPSocket> client_socket, const Query& query) {
    ServerResponse response; 
    const std::string sch_cmd_log_prefix = "[SCH ProcAndSend] ";
    Logger::debug(sch_cmd_log_prefix + "Processing command: " + query.originalQueryString);

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
        response.statusMessage = "Invalid argument in command: " + std::string(iae.what());
        response.payloadType = SRV_PAYLOAD_TYPE_ERROR_INFO;
        response.payloadDataStream << "Error Detail: Invalid argument provided to server.\n"
                                   << "Server Message: " << iae.what() << "\n"
                                   << "Original query: \"" << query.originalQueryString << "\"\n";
        Logger::error(sch_cmd_log_prefix + "InvalidArgument for '" + query.originalQueryString + "': " + iae.what());
    } catch (const std::out_of_range& oor) {
        response.reset();
        response.statusCode = SRV_STATUS_NOT_FOUND; 
        response.statusMessage = "Requested item not found or out of range: " + std::string(oor.what());
        response.payloadType = SRV_PAYLOAD_TYPE_ERROR_INFO;
        response.payloadDataStream << "Error Detail: Item not found or index out of range.\n"
                                   << "Server Message: " << oor.what() << "\n"
                                   << "Original query: \"" << query.originalQueryString << "\"\n";
        Logger::error(sch_cmd_log_prefix + "OutOfRange for '" + query.originalQueryString + "': " + oor.what());
    } catch (const std::filesystem::filesystem_error& fs_err) { 
        response.reset();
        response.statusCode = SRV_STATUS_SERVER_ERROR;
        response.statusMessage = "File system error on server: " + std::string(fs_err.what());
        response.payloadType = SRV_PAYLOAD_TYPE_ERROR_INFO;
        response.payloadDataStream << "Error Detail: A file system error occurred on the server.\n"
                                   << "Server Message: " << fs_err.what() << "\n" // Используем what() для filesystem_error
                                   << "Original query: \"" << query.originalQueryString << "\"\n";
        Logger::error(sch_cmd_log_prefix + "FilesystemError for '" + query.originalQueryString + "': " + fs_err.what());
    }
    catch (const std::runtime_error& rte) { // Ловим std::runtime_error после filesystem_error
        response.reset();
        response.statusCode = SRV_STATUS_SERVER_ERROR;
        response.statusMessage = "Server runtime error: " + std::string(rte.what());
        response.payloadType = SRV_PAYLOAD_TYPE_ERROR_INFO;
        response.payloadDataStream << "Error Detail: A runtime error occurred on the server.\n"
                                   << "Server Message: " << rte.what() << "\n"
                                   << "Original query: \"" << query.originalQueryString << "\"\n";
        Logger::error(sch_cmd_log_prefix + "RuntimeError for '" + query.originalQueryString + "': " + rte.what());
    } catch (const std::exception& e) { // Ловим все остальные std::exception
        response.reset();
        response.statusCode = SRV_STATUS_SERVER_ERROR;
        response.statusMessage = "Unexpected server error: " + std::string(e.what());
        response.payloadType = SRV_PAYLOAD_TYPE_ERROR_INFO;
        response.payloadDataStream << "Error Detail: An unexpected system error occurred on the server.\n"
                                   << "Server Message: " << e.what() << "\n"
                                   << "Original query: \"" << query.originalQueryString << "\"\n";
        Logger::error(sch_cmd_log_prefix + "StdException for '" + query.originalQueryString + "': " + e.what());
    } catch (...) {
        response.reset();
        response.statusCode = SRV_STATUS_SERVER_ERROR;
        response.statusMessage = "Unknown critical server error occurred.";
        response.payloadType = SRV_PAYLOAD_TYPE_ERROR_INFO;
        response.payloadDataStream << "Error Detail: An unknown critical error occurred on the server.\n"
                                   << "Original query: \"" << query.originalQueryString << "\"\n";
        Logger::error(sch_cmd_log_prefix + "Unknown exception (...) for '" + query.originalQueryString + "'.");
    }

    if (response.requiresChunking && response.statusCode == SRV_STATUS_OK_MULTI_PART_BEGIN) {
        // sendRemainingChunks сам отправит и первый чанк (из response) и остальные
        sendRemainingChunks(client_socket, response); 
    } else {
        sendSingleMessageResponsePart(client_socket, response);
    }
}


// --- Реализации handleXXX методов ---

void ServerCommandHandler::handleAdd(const QueryParameters& params, ServerResponse& response) {
    std::vector<double> trafficInToAdd = params.trafficInData;
    std::vector<double> trafficOutToAdd = params.trafficOutData; 

    if (trafficInToAdd.empty() && !params.hasTrafficInToSet) {
        trafficInToAdd.assign(HOURS_IN_DAY, 0.0);
    } else if (params.hasTrafficInToSet && trafficInToAdd.size() != static_cast<size_t>(HOURS_IN_DAY)) {
        throw std::invalid_argument("ADD: TRAFFIC_IN must contain " + std::to_string(HOURS_IN_DAY) + " values, received " + std::to_string(trafficInToAdd.size()));
    }

    if (trafficOutToAdd.empty() && !params.hasTrafficOutToSet) {
        trafficOutToAdd.assign(HOURS_IN_DAY, 0.0);
    } else if (params.hasTrafficOutToSet && trafficOutToAdd.size() != static_cast<size_t>(HOURS_IN_DAY)) {
         throw std::invalid_argument("ADD: TRAFFIC_OUT must contain " + std::to_string(HOURS_IN_DAY) + " values, received " + std::to_string(trafficOutToAdd.size()));
    }
    
    ProviderRecord newRecord(
        params.subscriberNameData,
        params.ipAddressData,
        params.dateData,
        trafficInToAdd,
        trafficOutToAdd
    ); 

    db_.addRecord(newRecord);

    response.statusCode = SRV_STATUS_OK;
    response.statusMessage = "Record added successfully.";
    response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
    response.payloadDataStream << "Record for subscriber '" << params.subscriberNameData << "' was successfully added to the database.";
}

void ServerCommandHandler::handleSelect(const QueryParameters& params, ServerResponse& response) {
    std::vector<size_t> original_indices = db_.findRecordsByCriteria(
        params.criteriaName, params.useNameFilter,
        params.criteriaIpAddress, params.useIpFilter,
        params.criteriaDate, params.useDateFilter
    );

    if (original_indices.empty()) {
        response.statusCode = SRV_STATUS_OK; 
        response.statusMessage = "No records matching criteria found.";
        response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
        response.payloadDataStream << "No records matching the specified criteria were found on the server.";
        return;
    }
    
    std::vector<ProviderRecord> found_records_copies;
    found_records_copies.reserve(original_indices.size());
    for (size_t db_idx : original_indices) { 
        try {
            found_records_copies.push_back(db_.getRecordByIndex(db_idx));
        } catch (const std::out_of_range& e) {
            Logger::warn("[SCH Select] Record with original DB index " + std::to_string(db_idx) + " not found during collection: " + e.what());
        }
    }
    
    if (found_records_copies.empty() && !original_indices.empty()){
        response.statusCode = SRV_STATUS_NOT_FOUND;
        response.statusMessage = "Records found by criteria were no longer accessible.";
        response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
        response.payloadDataStream << "Records initially found by criteria could not be retrieved (e.g., deleted concurrently).";
        return;
    }

    response.statusMessage = std::to_string(found_records_copies.size()) + " record(s) selected successfully.";
    response.payloadType = SRV_PAYLOAD_TYPE_PROVIDER_RECORDS_LIST;
    response.totalRecordsOverall = found_records_copies.size(); 

    if (found_records_copies.size() > SRV_CHUNKING_THRESHOLD_RECORDS) {
        response.statusCode = SRV_STATUS_OK_MULTI_PART_BEGIN;
        response.requiresChunking = true;
        response.recordsForChunking = std::move(found_records_copies);
        response.recordsInPayload = std::min(response.totalRecordsOverall, SRV_DEFAULT_CHUNK_RECORDS_COUNT); 
        formatRecordsToStream(response.payloadDataStream, response.recordsForChunking, 0, response.recordsInPayload, true); 
        Logger::info("[SCH Select] Found " + std::to_string(response.totalRecordsOverall) + " records. Using chunking. First chunk has " + std::to_string(response.recordsInPayload) + " records.");
    } else {
        response.statusCode = SRV_STATUS_OK;
        response.requiresChunking = false;
        formatRecordsToStream(response.payloadDataStream, found_records_copies, 0, found_records_copies.size(), true);
        response.recordsInPayload = found_records_copies.size();
        Logger::info("[SCH Select] Found " + std::to_string(found_records_copies.size()) + " records. Sending as single part.");
    }
}

void ServerCommandHandler::handlePrintAll(ServerResponse& response) {
    std::vector<ProviderRecord> all_records_copy = db_.getAllRecords(); 

    if (all_records_copy.empty()) {
        response.statusCode = SRV_STATUS_OK;
        response.statusMessage = "Database is empty.";
        response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
        response.payloadDataStream << "The database on the server is currently empty.";
        return;
    }

    response.statusMessage = "All " + std::to_string(all_records_copy.size()) + " records retrieved successfully.";
    response.payloadType = SRV_PAYLOAD_TYPE_PROVIDER_RECORDS_LIST;
    response.totalRecordsOverall = all_records_copy.size();

    if (all_records_copy.size() > SRV_CHUNKING_THRESHOLD_RECORDS) {
        response.statusCode = SRV_STATUS_OK_MULTI_PART_BEGIN;
        response.requiresChunking = true;
        response.recordsForChunking = std::move(all_records_copy);
        response.recordsInPayload = std::min(response.totalRecordsOverall, SRV_DEFAULT_CHUNK_RECORDS_COUNT);
        formatRecordsToStream(response.payloadDataStream, response.recordsForChunking, 0, response.recordsInPayload, true);
        Logger::info("[SCH PrintAll] Database contains " + std::to_string(response.totalRecordsOverall) + " records. Using chunking. First chunk has " + std::to_string(response.recordsInPayload) + " records.");
    } else {
        response.statusCode = SRV_STATUS_OK;
        response.requiresChunking = false;
        formatRecordsToStream(response.payloadDataStream, all_records_copy, 0, all_records_copy.size(), true);
        response.recordsInPayload = all_records_copy.size();
        Logger::info("[SCH PrintAll] Database contains " + std::to_string(all_records_copy.size()) + " records. Sending as single part.");
    }
}


void ServerCommandHandler::handleDelete(const QueryParameters& params, ServerResponse& response) {
    std::vector<size_t> indices_to_delete = db_.findRecordsByCriteria(
        params.criteriaName, params.useNameFilter,
        params.criteriaIpAddress, params.useIpFilter,
        params.criteriaDate, params.useDateFilter
    );

    if (indices_to_delete.empty()) {
        response.statusCode = SRV_STATUS_OK; 
        response.statusMessage = "No records found matching criteria for deletion.";
        response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
        response.payloadDataStream << "No records were found matching the specified criteria for deletion on the server.";
        return;
    }

    size_t num_deleted = db_.deleteRecordsByIndices(indices_to_delete); 
    response.statusCode = SRV_STATUS_OK;
    response.statusMessage = std::to_string(num_deleted) + " record(s) deleted successfully.";
    response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
    response.payloadDataStream << "Successfully deleted " << num_deleted << " record(s) from the server database.";
    response.recordsInPayload = num_deleted; 
}

void ServerCommandHandler::handleEdit(const QueryParameters& params, ServerResponse& response) {
    if (params.setData.empty() && !params.hasTrafficInToSet && !params.hasTrafficOutToSet) {
        throw std::invalid_argument("EDIT: SET section is missing or does not contain any fields to modify.");
    }

    std::vector<size_t> indices_to_edit = db_.findRecordsByCriteria(
        params.criteriaName, params.useNameFilter,
        params.criteriaIpAddress, params.useIpFilter,
        params.criteriaDate, params.useDateFilter
    );

    if (indices_to_edit.empty()) {
        response.statusCode = SRV_STATUS_NOT_FOUND;
        response.statusMessage = "No records found matching criteria for editing.";
        response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
        response.payloadDataStream << "No records were found matching the specified criteria for editing.";
        Logger::info("[SCH Edit] No records found for editing based on criteria.");
        return;
    }

    size_t target_db_index = indices_to_edit[0]; 
    std::string edit_prelude_message_str;

    if (indices_to_edit.size() > 1) {
        std::ostringstream warn_oss;
        warn_oss << "EDIT Warning: Criteria matched " << indices_to_edit.size()
                 << " records. Only the first found record (Original DB Index: " << target_db_index << ") will be edited.\n";
        edit_prelude_message_str = warn_oss.str();
        Logger::warn("[SCH Edit] Found " + std::to_string(indices_to_edit.size()) +
                     " records for editing, will process only the first (original DB index " + std::to_string(target_db_index) + ")");
    }

    ProviderRecord record_to_edit = db_.getRecordByIndex(target_db_index); 
    ProviderRecord original_record_for_comparison = record_to_edit;
    
    for (const auto& pair : params.setData) {
        const std::string& field_key_upper = schToUpper(pair.first); 
        const std::string& value_str = pair.second;
        if (field_key_upper == "FIO") { record_to_edit.setName(value_str); }
        else if (field_key_upper == "IP") {
            IPAddress new_ip; std::istringstream ip_ss(value_str);
            if (!(ip_ss >> new_ip) || (ip_ss.peek() != EOF && !(ip_ss >> std::ws).eof() ) ) throw std::invalid_argument("EDIT SET: Invalid IP format '" + value_str + "' for IP field.");
            record_to_edit.setIpAddress(new_ip);
        } else if (field_key_upper == "DATE") {
            Date new_date; std::istringstream date_ss(value_str);
            if (!(date_ss >> new_date) || (date_ss.peek() != EOF && !(date_ss >> std::ws).eof() ) ) throw std::invalid_argument("EDIT SET: Invalid Date format '" + value_str + "' for DATE field.");
            record_to_edit.setDate(new_date);
        } else {
             Logger::warn("[SCH Edit] Unknown field '" + pair.first + "' in SET data. Skipped.");
        }
    }
    if (params.hasTrafficInToSet) {
        if(params.trafficInData.size() != static_cast<size_t>(HOURS_IN_DAY)) throw std::invalid_argument("EDIT SET: TRAFFIC_IN block must contain " + std::to_string(HOURS_IN_DAY) + " values.");
        record_to_edit.setTrafficInByHour(params.trafficInData);
    }
    if (params.hasTrafficOutToSet) {
        if(params.trafficOutData.size() != static_cast<size_t>(HOURS_IN_DAY)) throw std::invalid_argument("EDIT SET: TRAFFIC_OUT block must contain " + std::to_string(HOURS_IN_DAY) + " values.");
        record_to_edit.setTrafficOutByHour(params.trafficOutData);
    }
    
    bool changed_applied = (record_to_edit != original_record_for_comparison);

    if (changed_applied) {
        db_.editRecord(target_db_index, record_to_edit);
        response.statusCode = SRV_STATUS_OK;
        response.statusMessage = "Record edited successfully.";
        response.payloadType = SRV_PAYLOAD_TYPE_PROVIDER_RECORDS_LIST; 
        response.payloadDataStream << edit_prelude_message_str
                                   << "Record with Original DB Index " << target_db_index << " was successfully edited.\n"
                                   << "New data for the record:\n-----------------------------------------------------------------\n";
        const ProviderRecord& final_record_state = db_.getRecordByIndex(target_db_index);
        std::vector<ProviderRecord> temp_records_vec; temp_records_vec.push_back(final_record_state);
        formatRecordsToStream(response.payloadDataStream, temp_records_vec, 0, 1, false); 
        response.payloadDataStream << "\n-----------------------------------------------------------------";
        response.recordsInPayload = 1;
    } else {
        response.statusCode = SRV_STATUS_OK;
        response.statusMessage = "No changes applied to the record (new data was identical or no effective changes made).";
        response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
        response.payloadDataStream << edit_prelude_message_str
                                   << "EDIT Information: No changes were applied to the record as the new data was identical to the existing data, or the SET fields did not result in any effective changes.";
    }
}

void ServerCommandHandler::handleCalculateCharges(const QueryParameters& params, ServerResponse& response) {
    if (!params.useStartDateFilter || !params.useEndDateFilter) {
        throw std::invalid_argument("CALCULATE_CHARGES: Command requires both START_DATE and END_DATE parameters to be specified.");
    }
    if (params.criteriaStartDate > params.criteriaEndDate) {
         throw std::invalid_argument("CALCULATE_CHARGES: START_DATE (" + params.criteriaStartDate.toString() + ") cannot be later than END_DATE (" + params.criteriaEndDate.toString() + ").");
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
            response.statusCode = SRV_STATUS_OK; 
            response.statusMessage = "No records found matching filter criteria for charge calculation.";
            response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
            response.payloadDataStream << "No records found based on the specified filter criteria for charge calculation.";
            return;
        }
        records_to_process_copies.reserve(record_indices.size());
        for(size_t index : record_indices) {
            try { records_to_process_copies.push_back(db_.getRecordByIndex(index)); } 
            catch (const std::out_of_range&) { /* Игнорируем, если удалена */ }
        }
    }
    
    if (records_to_process_copies.empty()){
        response.statusCode = SRV_STATUS_OK;
        response.statusMessage = "No records available (or matching filter) to calculate charges for.";
        response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
        response.payloadDataStream << "No records are available in the database (or match filter criteria) for charge calculation.";
        return;
    }
    
    double grandTotalCharges = 0.0;
    response.payloadDataStream << "Charge calculation report for period (" << params.criteriaStartDate.toString() << " - " << params.criteriaEndDate.toString() << "):\n";
    response.payloadDataStream << "-----------------------------------------------------------------\n";
    response.payloadDataStream << std::fixed << std::setprecision(2);
    bool charges_calculated_for_at_least_one_record = false;

    for (const auto& record_copy : records_to_process_copies) {
        double charge_for_record = 0.0;
        try {
            charge_for_record = db_.calculateChargesForRecord(record_copy, tariff_plan_, params.criteriaStartDate, params.criteriaEndDate);
            if (record_copy.getDate() >= params.criteriaStartDate && record_copy.getDate() <= params.criteriaEndDate) {
                 if (charge_for_record > DOUBLE_EPSILON || charge_for_record < -DOUBLE_EPSILON || records_to_process_copies.size() == 1) { 
                    response.payloadDataStream << "Subscriber: " << record_copy.getName()
                                            << " (IP: " << record_copy.getIpAddress().toString()
                                            << ", Record Date: " << record_copy.getDate().toString()
                                            << ") | Calculated Charge: " << charge_for_record << "\n";
                    charges_calculated_for_at_least_one_record = true;
                 }
                grandTotalCharges += charge_for_record;
            }
        }
        catch (const std::exception& e) { 
            response.payloadDataStream << "Subscriber: " << record_copy.getName() << " | Error during charge calculation: " << e.what() << "\n";
            Logger::error("[SCH Calc] Charge calculation error for subscriber " + record_copy.getName() + ": " + e.what());
        }
    }

    if (!charges_calculated_for_at_least_one_record && !records_to_process_copies.empty()) {
        response.payloadDataStream << "No charges were applicable for the selected records within the specified period (or tariffs are zero/erroneous).\n";
    }
    response.payloadDataStream << "-----------------------------------------------------------------\n";
    response.payloadDataStream << "GRAND TOTAL calculated charges for selection: " << grandTotalCharges << "\n";
    response.payloadDataStream << "-----------------------------------------------------------------\n";

    response.statusCode = SRV_STATUS_OK;
    response.statusMessage = "Charge calculation completed.";
    response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE; 
}


void ServerCommandHandler::handleLoad(const QueryParameters& params, ServerResponse& response) {
    if (params.filename.empty()) {
        throw std::invalid_argument("LOAD: Command requires a filename parameter.");
    }
    std::filesystem::path target_file_path;
    try {
        // ИСПРАВЛЕНО: Используем FileUtils::getSafeServerFilePath
        target_file_path = FileUtils::getSafeServerFilePath(server_data_base_path_, params.filename, DEFAULT_SERVER_DATA_SUBDIR); 
        Logger::info("[SCH Load] Attempting to load from resolved safe path: '" + target_file_path.string() + "'");
    } catch (const std::exception& e) { 
        throw std::runtime_error("LOAD: Error resolving safe file path for '" + params.filename + "': " + std::string(e.what()));
    }

    FileOperationResult load_res = db_.loadFromFile(target_file_path.string()); 

    response.statusCode = load_res.success ? SRV_STATUS_OK : SRV_STATUS_SERVER_ERROR; 
    response.statusMessage = load_res.success ? "Data loaded successfully from file '" + params.filename + "'." : "Data loading from file '" + params.filename + "' failed.";
    response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
    response.payloadDataStream << load_res.user_message; 
    response.recordsInPayload = load_res.records_processed; 
    if (!load_res.success && !load_res.error_details.empty()){
        response.payloadDataStream << "\nAdditional server error details: " << load_res.error_details;
    }
}

void ServerCommandHandler::handleSave(const QueryParameters& params, ServerResponse& response) {
    FileOperationResult save_res;
    std::string effective_filename_for_msg_and_save;

    if (params.filename.empty()) {
        effective_filename_for_msg_and_save = db_.getCurrentFilename();
        if (effective_filename_for_msg_and_save.empty()){
             throw std::invalid_argument("SAVE: Filename not specified and no previous file context is available on the server to save to.");
        }
        Logger::info("[SCH Save] Attempting to save to current DB file context: '" + effective_filename_for_msg_and_save + "'");
        save_res = db_.saveToFile(); 
    } else {
        std::filesystem::path target_file_path;
        try {
             // ИСПРАВЛЕНО: Используем FileUtils::getSafeServerFilePath
             target_file_path = FileUtils::getSafeServerFilePath(server_data_base_path_, params.filename, DEFAULT_SERVER_DATA_SUBDIR); 
        } catch (const std::exception& e) {
            throw std::runtime_error("SAVE: Error resolving safe file path for '" + params.filename + "': " + std::string(e.what()));
        }
        effective_filename_for_msg_and_save = target_file_path.string();
        Logger::info("[SCH Save] Attempting to save to specified file: '" + effective_filename_for_msg_and_save + "'");
        save_res = db_.saveToFile(effective_filename_for_msg_and_save);
    }

    response.statusCode = save_res.success ? SRV_STATUS_OK : SRV_STATUS_SERVER_ERROR;
    response.statusMessage = save_res.success ? "Data saved successfully to file '" + effective_filename_for_msg_and_save + "'." : "Data saving to file '" + effective_filename_for_msg_and_save + "' failed.";
    response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
    response.payloadDataStream << save_res.user_message;
    response.recordsInPayload = save_res.records_processed;
     if (!save_res.success && !save_res.error_details.empty()){
        response.payloadDataStream << "\nAdditional server error details: " << save_res.error_details;
    }
}

void ServerCommandHandler::handleHelp(ServerResponse& response) {
    response.statusCode = SRV_STATUS_OK;
    response.statusMessage = "Server help information provided.";
    response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
    response.payloadDataStream << "Supported server commands: ADD, SELECT, DELETE, EDIT, CALCULATE_CHARGES, PRINT_ALL, LOAD, SAVE, EXIT.\n"
                               << "For detailed command syntax and parameters, please refer to the client-side HELP documentation or project specifications.\n";
}

void ServerCommandHandler::handleExit(ServerResponse& response) {
    response.statusCode = SRV_STATUS_OK;
    response.statusMessage = "Session termination acknowledged by server.";
    response.payloadType = SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE;
    response.payloadDataStream << "Server acknowledges EXIT command. The client should now close the connection session.";
}

void ServerCommandHandler::handleUnknown(const Query& query, ServerResponse& response) {
    response.statusCode = SRV_STATUS_BAD_REQUEST;
    response.statusMessage = "Unknown or malformed command received by server.";
    response.payloadType = SRV_PAYLOAD_TYPE_ERROR_INFO;
    response.payloadDataStream << "Error: The server did not understand the command or its format.\n"
                               << "Original query received by server: \"" << query.originalQueryString << "\"\n"
                               << "Please use client-side HELP for a list of valid commands and their correct syntax.\n";
    Logger::warn("[SCH Unknown] Received unknown/malformed query (parsed type: "
                 + std::to_string(static_cast<int>(query.type)) + ", original string: '" + query.originalQueryString + "').");
}
