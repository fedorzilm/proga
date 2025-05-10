#include "database.h"
#include "common_defs.h"
#include "query_parser.h"
#include "provider_record.h"
#include <fstream>
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <stdexcept> // Для std::out_of_range, std::bad_variant_access в select_records

bool Database::load_from_file(const std::string& filename) {
    std::unique_lock<std::shared_mutex> lock(db_mutex_);

    if (!is_valid_cmd_argument_path(filename)) { 
        std::cerr << get_current_timestamp() << " [Database] Error: Invalid filepath for load: " << filename << std::endl;
        return false;
    }
    std::ifstream ifs(filename);
    if (!ifs) {
        std::cout << get_current_timestamp() << " [Database] Info: File '" << filename << "' not found or cannot be opened. Starting empty." << std::endl;
        records.clear();
        return true; // Считаем это успешным запуском с пустой базой
    }

    records.clear();
    int logical_record_idx = 0;

    while (ifs.peek() != EOF && ifs.good()) {
        logical_record_idx++;
        ProviderRecord temp_record;
        std::streampos record_start_stream_pos = ifs.tellg();

        if (!temp_record.read(ifs)) {
            if (ifs.eof()) {
                // Если достигнут конец файла и что-то было прочитано для этой записи (не пустая строка), это неполная запись
                if (ifs.tellg() != record_start_stream_pos && !ifs.bad()) { 
                     std::cerr << get_current_timestamp() << " [Database] Warning: Incomplete record at the end of file '" << filename
                               << "' (logical record " << logical_record_idx << ")." << std::endl;
                }
                break; // Конец файла, выходим из цикла
            }
            // Если не EOF, но read вернул false, значит, ошибка формата
            std::cerr << get_current_timestamp() << " [Database] Warning: Invalid record data format in file '" << filename
                      << "' for logical record " << logical_record_idx
                      << ". Skipping this entire logical record." << std::endl;
            
            ifs.clear(); // Очищаем флаги ошибок потока (например, failbit)

            // Пытаемся пропустить строки этой ошибочной записи, чтобы продолжить чтение файла
            // Каждая запись это 4 строки. Мы уже пытались прочитать первую.
            // Пропускаем до 3 оставшихся строк или до конца файла.
            // Это простая эвристика, может быть не идеальной для всех форматов ошибок.
            if (ifs.seekg(record_start_stream_pos)) { // Возвращаемся к началу ошибочной записи
                for (int i = 0; i < 4; ++i) { // Пропускаем 4 строки этой записи
                    std::string dummy_skipper;
                    if (!std::getline(ifs, dummy_skipper)) {
                        if (ifs.eof()) break; // Достигли конца файла при пропуске
                        // Если не EOF, но getline не удался, это может быть I/O ошибка
                        std::cerr << get_current_timestamp() << " [Database] Error while trying to skip lines of malformed record " << logical_record_idx << std::endl;
                        // Решаем, что делать дальше: прервать загрузку или попытаться продолжить.
                        // Для большей устойчивости, можно попытаться продолжить, но это рискованно.
                        // Пока что вернем false, если пропуск не удался.
                        return false; 
                    }
                }
            } else {
                 std::cerr << get_current_timestamp() << " [Database] Critical error: Could not seekg to reskip malformed record " << logical_record_idx << "." << std::endl;
                 return false; // Критическая ошибка, прерываем загрузку
            }
            continue; // Переходим к следующей итерации цикла while
        }

        // Проверяем валидность успешно прочитанной записи
        if (!temp_record.is_valid()) {
             std::cerr << get_current_timestamp() << " [Database] Warning: Record " << logical_record_idx
                       << " from file '" << filename << "' was read, but is_valid() returned false. Skipping." << std::endl;
        } else {
            records.push_back(std::move(temp_record));
        }
    }

    if (ifs.bad()) { // Проверяем на I/O ошибки после цикла (например, если диск отвалился)
        std::cerr << get_current_timestamp() << " [Database] Error: Critical I/O error reading '" << filename << "'." << std::endl;
        return false;
    }

    std::cout << get_current_timestamp() << " [Database] Info: Loaded " << records.size() << " valid records from '" << filename << "'." << std::endl;
    return true;
}

bool Database::save_to_file(const std::string& filename) const {
    std::shared_lock<std::shared_mutex> lock(db_mutex_); // shared_lock для const метода

    if (!is_valid_cmd_argument_path(filename)) { 
        std::cerr << get_current_timestamp() << " [Database] Error: Invalid filepath for save: " << filename << std::endl;
        return false;
    }
    ensure_directory_exists_util(filename); // Убедимся, что директория существует
    
    std::ofstream ofs(filename);
    if (!ofs) {
        std::cerr << get_current_timestamp() << " [Database] Error: Cannot open file '" << filename << "' for writing." << std::endl;
        return false;
    }

    for (const auto& record : records) {
        record.write(ofs);
        if (!ofs.good()) { // Проверяем после каждой записи
            std::cerr << get_current_timestamp() << " [Database] Error: I/O error while writing record to '" << filename << "'." << std::endl;
            ofs.close(); // Пытаемся закрыть файл
            return false;
        }
    }
    ofs.close(); // Закрываем файл после успешной записи всех данных
    if (ofs.bad()){ // Проверяем состояние потока после закрытия
         std::cerr << get_current_timestamp() << " [Database] Error: I/O error after closing file '" << filename << "'." << std::endl;
         return false;
    }
    return true;
}

bool Database::add_record(ProviderRecord record) {
    std::unique_lock<std::shared_mutex> lock(db_mutex_);

    if (!record.is_valid()) {
        std::cerr << get_current_timestamp() << " [Database] Error: Attempted to add invalid record." << std::endl;
        return false;
    }
    // Проверка на дубликат по ключам (ФИО, IP, Дата)
    for(const auto& existing_record : records) {
        if(existing_record.full_name == record.full_name &&
           existing_record.ip_address == record.ip_address &&
           existing_record.record_date == record.record_date) {
            std::cerr << get_current_timestamp() << " [Database] Error: Duplicate record (name, IP, date match) already exists. Cannot add." << std::endl;
            return false;
        }
    }
    records.push_back(std::move(record));
    return true;
}

size_t Database::delete_records(const SelectQuery& query) {
    std::unique_lock<std::shared_mutex> lock(db_mutex_);
    size_t initial_size = records.size();
    records.erase(
        std::remove_if(records.begin(), records.end(),
                       [&](const ProviderRecord& rec) {
                           return QueryParser::matches(rec, query); // QueryParser::matches теперь static
                       }),
        records.end()
    );
    return initial_size - records.size();
}

ProviderRecord* Database::find_exact_record_for_edit(const std::string& name, const IpAddress& ip, const Date& date) {
    std::unique_lock<std::shared_mutex> lock(db_mutex_); 
    for (auto& record : records) {
        if (record.full_name == name && record.ip_address == ip && record.record_date == date) {
            return &record;
        }
    }
    return nullptr;
}

int Database::update_record(const std::string& original_key_name, 
                            const IpAddress& original_key_ip, 
                            const Date& original_key_date, 
                            const ProviderRecord& new_record_data) {
    std::unique_lock<std::shared_mutex> lock(db_mutex_);

    if (!new_record_data.is_valid()) {
        std::cerr << get_current_timestamp() << " [Database] Error: Attempted to update with invalid new record data." << std::endl;
        return -3; // Обновленная запись невалидна
    }

    auto it = std::find_if(records.begin(), records.end(), [&](const ProviderRecord& rec) {
        return rec.full_name == original_key_name &&
               rec.ip_address == original_key_ip &&
               rec.record_date == original_key_date;
    });

    if (it == records.end()) {
        std::cerr << get_current_timestamp() << " [Database] Error: Record to update not found with original keys (" 
                  << original_key_name << ", " << original_key_ip.to_string() << ", " << original_key_date.to_string() << ")." << std::endl;
        return -1; // Запись для редактирования не найдена
    }

    // Проверяем, изменились ли идентификационные поля
    bool id_fields_changed = (new_record_data.full_name != original_key_name ||
                              new_record_data.ip_address != original_key_ip ||
                              new_record_data.record_date != original_key_date);

    if (id_fields_changed) {
        // Идентификационные поля изменились. Проверяем, не конфликтуют ли новые ключи с ДРУГОЙ существующей записью.
        for (const auto& existing_record : records) {
            if (&existing_record == &(*it)) { 
                continue;
            }
            if (existing_record.full_name == new_record_data.full_name &&
                existing_record.ip_address == new_record_data.ip_address &&
                existing_record.record_date == new_record_data.record_date) {
                std::cerr << get_current_timestamp() << " [Database] Error: New identifying fields for update ("
                          << new_record_data.full_name << ", " << new_record_data.ip_address.to_string() << ", " << new_record_data.record_date.to_string()
                          << ") conflict with another existing record." << std::endl;
                return -2; // Конфликт с другой существующей записью
            }
        }
    }
    *it = new_record_data;
    return 0; // Успех
}

std::vector<ProviderRecord> Database::select_records(const SelectQuery& query) const {
    std::shared_lock<std::shared_mutex> lock(db_mutex_);
    std::vector<ProviderRecord> results_copies;
    results_copies.reserve(records.size()); // Оптимизация: резервируем память
    for (const auto& record : records) {
        if (QueryParser::matches(record, query)) { // QueryParser::matches теперь static
            results_copies.push_back(record);
        }
    }

    if (query.sort_order != SortOrder::NONE && !query.sort_by_field.empty() && !results_copies.empty()) {
        std::sort(results_copies.begin(), results_copies.end(),
                  [&](const ProviderRecord& a, const ProviderRecord& b) {
            try {
                auto val_a_var = a.get_field_value(query.sort_by_field);
                auto val_b_var = b.get_field_value(query.sort_by_field);
                bool less_than = false;

                // Используем std::visit для сравнения вариантов
                less_than = std::visit([&](auto&& arg_a, auto&& arg_b) -> bool {
                    using TypeA = std::decay_t<decltype(arg_a)>;
                    using TypeB = std::decay_t<decltype(arg_b)>;

                    if constexpr (std::is_same_v<TypeA, TypeB>) {
                        if constexpr (std::is_same_v<TypeA, TrafficReading>) {
                            return arg_a.total() < arg_b.total();
                        } else {
                             // Для std::string, Date, IpAddress, long long оператор < уже определен
                            return arg_a < arg_b;
                        }
                    } else {
                        // Попытка сравнить разные типы - это ошибка в логике get_field_value или запроса
                        // Для сортировки типы должны быть одинаковыми.
                        // Можно вернуть false или бросить исключение.
                        // В ProviderRecord::get_field_value мы всегда возвращаем один тип для одного имени поля.
                        // Эта ветка не должна достигаться при корректной работе.
                        std::cerr << get_current_timestamp() << " [Database] Warning: Type mismatch during sort for field '" 
                                  << query.sort_by_field << "'. Comparing different types." << std::endl;
                        return false; 
                    }
                }, val_a_var, val_b_var);
                
                return query.sort_order == SortOrder::ASC ? less_than : !less_than;

            } catch (const std::out_of_range& oor) { // от get_field_value
                std::cerr << get_current_timestamp() << " [Database] Warning: Invalid field '" << query.sort_by_field << "' for sorting: " << oor.what() << std::endl;
                return false; // или не менять порядок элементов, если поле неверно
            } catch (const std::bad_variant_access& bva) { // от std::get, если мы бы его использовали напрямую
                std::cerr << get_current_timestamp() << " [Database] Warning: Type mismatch (bad_variant_access) for field '" << query.sort_by_field << "' during sorting: " << bva.what() << std::endl;
                return false;
            }
        });
    }
    return results_copies;
}

std::optional<double> Database::calculate_bill(const SelectQuery& query, const Date& start_date, const Date& end_date, const TariffPlan& tariff) const {
    std::shared_lock<std::shared_mutex> lock(db_mutex_);

    if (!start_date.is_valid() || !end_date.is_valid() || end_date < start_date) {
        std::cerr << get_current_timestamp() << " [Database] Error: Invalid date range for billing (" 
                  << start_date.to_string() << " - " << end_date.to_string() << ")." << std::endl;
        return std::nullopt;
    }
    double total_bill = 0.0;
    bool records_in_period_found = false;
    for (const auto& record : records) {
        if (QueryParser::matches(record, query)) { // QueryParser::matches теперь static
            if (record.record_date >= start_date && record.record_date <= end_date) {
                records_in_period_found = true;
                for (size_t hour = 0; hour < record.hourly_traffic.size() && hour < HOURS_IN_DAY; ++hour) {
                    total_bill += static_cast<double>(record.hourly_traffic[hour].total()) * tariff.get_rate(static_cast<int>(hour));
                }
            }
        }
    }
    // Если мы хотим различать "0.0 из-за отсутствия записей" от "0.0 из-за нулевого трафика/тарифа",
    // можно вернуть std::nullopt, если records_in_period_found == false и total_bill == 0.0.
    // Но текущая логика вернет 0.0 в обоих случаях, что тоже приемлемо.
    return total_bill;
}

size_t Database::record_count() const {
    std::shared_lock<std::shared_mutex> lock(db_mutex_);
    return records.size();
}

void Database::print_all_records_to_stream(std::ostream& os, size_t limit) const {
    std::shared_lock<std::shared_mutex> lock(db_mutex_);
    size_t count = 0;
    if (records.empty()) {
        os << "Database is empty.\n";
        return;
    }
    for (const auto& record : records) {
        if (limit > 0 && count >= limit) {
            os << "... (output limited to " << limit << " records)\n";
            break;
        }
        record.print(os); // ProviderRecord::print выводит разделитель сам или нет?
                           // Судя по вашему коду, ProviderRecord::print не выводит разделитель,
                           // это делает UserInterface::handle_print_all. Оставляем так.
        os << "------------------------------------\n"; // Добавим разделитель здесь для консистентности, если print() его не ставит
        count++;
    }
}

std::vector<std::vector<std::string>> Database::get_formatted_select_results(const SelectQuery& query) const {
    // Эта функция не берет блокировку сама, т.к. вызывает select_records, который уже это делает.
    std::vector<ProviderRecord> selected_records = select_records(query); // select_records берет shared_lock
    std::vector<std::vector<std::string>> formatted_results;

    std::vector<std::string> fields_to_display;
    if (query.wants_all_fields()) {
        fields_to_display = ProviderRecord::get_all_field_names(); // static метод
    } else {
        fields_to_display = query.select_fields;
    }

    // Добавляем заголовки, только если есть что отображать или есть записи
    if (!fields_to_display.empty() && !(fields_to_display.size() == 1 && fields_to_display[0].empty())) {
        formatted_results.push_back(fields_to_display);
    } else if (!selected_records.empty()) { 
        // Если поля не указаны, но есть записи, используем все поля по умолчанию
        fields_to_display = ProviderRecord::get_all_field_names();
         if (!fields_to_display.empty()) { // Проверка, что get_all_field_names не пуст
             formatted_results.push_back(fields_to_display);
         }
    }
    // Если fields_to_display все еще пуст (например, get_all_field_names вернул пустоту) 
    // и нет записей, то formatted_results останется пустым.

    for (const auto& record : selected_records) {
        std::vector<std::string> row;
        // Резервируем место, если fields_to_display не пуст
        if (!fields_to_display.empty()) row.reserve(fields_to_display.size());
        
        bool field_found_in_row_for_this_record = false;
        for (const auto& field_name : fields_to_display) {
            if (field_name.empty()) continue; // Пропускаем пустые имена полей в запросе
            try {
                row.push_back(record.get_field_value_as_string(field_name));
                field_found_in_row_for_this_record = true;
            } catch (const std::out_of_range&) {
                // Если поле не найдено в ProviderRecord::get_field_value_as_string,
                // это может быть ошибка в запросе (неверное имя поля) или в get_all_field_names.
                row.push_back("[N/A:" + field_name + "]"); 
            }
        }
        // Добавляем строку, только если она не пуста или если это была единственная запрошенная колонка, которая вернула N/A
        if (field_found_in_row_for_this_record || (!row.empty() && !fields_to_display.empty()) ) {
             formatted_results.push_back(row);
        }
    }
    return formatted_results;
}
