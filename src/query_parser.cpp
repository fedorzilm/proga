#include "query_parser.h"
#include "provider_record.h" // Для QueryParser::matches, хотя в этом файле не используется напрямую
#include "common_defs.h"     // Для to_lower_util, get_current_timestamp, trim_string
#include <sstream>
#include <algorithm>
#include <iostream> // Для std::cerr

// Вспомогательная функция для получения следующего токена из потока
// (можно вынести в common_defs.h, если используется в других местах)
namespace { // Локальная для этого файла
    std::string get_next_token_qp(std::stringstream& ss) { // Переименована, чтобы избежать конфликтов, если есть глобальная
        std::string token;
        if (ss >> token) {
            return token;
        }
        return "";
    }
}

bool FieldCriterion::is_string_condition() const {
    return condition == Condition::EQ || condition == Condition::NE || condition == Condition::CONTAINS;
}

bool FieldCriterion::is_date_condition() const {
    // CONTAINS не применим к датам в текущей логике
    return condition != Condition::CONTAINS && condition != Condition::NONE;
}

bool FieldCriterion::is_ip_condition() const {
    // CONTAINS не применим к IP в текущей логике
    return condition != Condition::CONTAINS && condition != Condition::NONE;
}


std::optional<SelectQuery> QueryParser::parse_select(const std::string& query_string_input) {
    std::string query_string = trim_string(query_string_input);
    if (query_string.empty()) {
        SelectQuery q_all;
        q_all.select_fields.push_back("*");
        return q_all;
    }

    std::stringstream ss(query_string);
    std::string token;
    SelectQuery query;

    // 1. Parse SELECT
    token = get_next_token_qp(ss);
    if (to_lower_util(token) != "select") {
        std::cerr << get_current_timestamp() << " [QueryParser] Error: Query must start with 'SELECT'. Got: '" << token << "'" << std::endl;
        return std::nullopt;
    }

    // 2. Parse Field List
    ss >> std::ws;
    std::string field_list_buffer;
    std::streampos field_list_start_pos = ss.tellg();

    while (ss.good() && ss.peek() != EOF) {
        std::streampos current_word_pos = ss.tellg();
        // Читаем токен до пробела или ключевого слова
        std::string current_word;
        char peek_char = static_cast<char>(ss.peek());
        while(ss.good() && peek_char != EOF && !isspace(peek_char) && peek_char != ','){
            current_word += static_cast<char>(ss.get());
            peek_char = static_cast<char>(ss.peek());
        }
        if (current_word.empty()){ // если сразу пробел или запятая, токен будет пустым, попробуем обычное чтение
             current_word = get_next_token_qp(ss);
        }


        if (current_word.empty() && (ss.fail() || ss.eof())) break; 

        std::string lower_current_word = to_lower_util(current_word);
        if (lower_current_word == "where" || lower_current_word == "sort") {
            ss.clear(); 
            ss.seekg(current_word_pos); 
            break;
        }
        if (!field_list_buffer.empty()) field_list_buffer += " "; 
        field_list_buffer += current_word;

        ss >> std::ws; // пропустить пробелы
        if (ss.peek() == ',') {
            field_list_buffer += ","; // Добавляем запятую в буфер, если она есть
            ss.get(); // съедаем запятую
            ss >> std::ws; // пропустить пробелы после запятой
            if (ss.peek() == EOF || to_lower_util(std::string(1, static_cast<char>(ss.peek()))) == "w" || to_lower_util(std::string(1, static_cast<char>(ss.peek()))) == "s") {
                 // Запятая в конце списка полей или перед WHERE/SORT
                 if(ss.peek() == EOF || (ss.peek() != EOF && (to_lower_util(get_next_token_qp(ss))=="where" || to_lower_util(get_next_token_qp(ss))=="sort"))){
                    // Если после запятой сразу конец или ключевое слово, это ошибка
                    ss.seekg(current_word_pos); // Вернуть поток к началу последнего прочитанного слова
                    ss.seekg(field_list_buffer.length() - current_word.length(), std::ios_base::cur); // Попытка откатить и current_word
                    field_list_buffer = field_list_buffer.substr(0, field_list_buffer.length() - current_word.length() -1); // Удалить current_word и пробел
                 }
                 // Иначе, если это не конец, и не where/sort, значит просто список полей продолжается
            }
        }
    }
    ss.clear(); 

    field_list_buffer = trim_string(field_list_buffer);

    if (field_list_buffer.empty()) { 
        std::cerr << get_current_timestamp() << " [QueryParser] Error: Missing field list after 'SELECT'." << std::endl;
        return std::nullopt;
    }
    
    if (field_list_buffer == "*") {
        query.select_fields.push_back("*");
    } else {
        std::stringstream field_ss(field_list_buffer);
        std::string selected_field_segment;
        while (std::getline(field_ss, selected_field_segment, ',')) {
            selected_field_segment = trim_string(selected_field_segment);
            if (!selected_field_segment.empty()) {
                // Проверка на случай, если в сегменте несколько полей без запятой (например, "name ip")
                std::stringstream sub_segment_ss(selected_field_segment);
                std::string single_field;
                if (sub_segment_ss >> single_field) {
                    query.select_fields.push_back(single_field);
                    if (sub_segment_ss >> single_field) { // Если что-то еще осталось в сегменте
                        std::cerr << get_current_timestamp() << " [QueryParser] Error: Missing comma or invalid token '" << single_field << "' in SELECT list segment: '" << selected_field_segment << "'." << std::endl;
                        return std::nullopt;
                    }
                } else { // selected_field_segment был непустым, но из него ничего не прочиталось (маловероятно, но для полноты)
                     std::cerr << get_current_timestamp() << " [QueryParser] Error: Empty field name in SELECT list (after comma)." << std::endl;
                     return std::nullopt;
                }
            } else { 
                 std::cerr << get_current_timestamp() << " [QueryParser] Error: Empty field name in SELECT list (likely due to extra comma)." << std::endl;
                 return std::nullopt;
            }
        }
        if (query.select_fields.empty()) { 
            std::cerr << get_current_timestamp() << " [QueryParser] Error: Empty or invalid select field list specified." << std::endl;
            return std::nullopt;
        }
    }

    // 3. Parse WHERE (Optional)
    ss >> std::ws;
    std::streampos before_where_pos = ss.tellg();
    token = get_next_token_qp(ss);

    if (to_lower_util(token) == "where") {
        bool expect_condition = true; 
        while (ss.good()) {
            if (!expect_condition) { 
                std::streampos before_connector_pos = ss.tellg();
                std::string connector = get_next_token_qp(ss);
                if (to_lower_util(connector) == "and") {
                    expect_condition = true;
                    if ((ss >> std::ws).peek() == EOF) { // AND без последующего условия
                        std::cerr << get_current_timestamp() << " [QueryParser] Error: Expected condition after 'AND'." << std::endl;
                        return std::nullopt;
                    }
                } else { // Не AND, значит, закончили с WHERE
                    ss.clear();
                    ss.seekg(before_connector_pos); 
                    break; 
                }
            }
            if (!expect_condition) break; // Если не было AND, и мы не ожидаем условие, выходим

            std::string field_str = get_next_token_qp(ss);
            if (field_str.empty() && (ss.fail() || ss.eof())) {
                if (query.criteria.empty()) { 
                    std::cerr << get_current_timestamp() << " [QueryParser] Error: Expected condition after 'WHERE'." << std::endl;
                    return std::nullopt;
                }
                // Это может быть конец запроса, если последнее было валидное условие
                break; 
            }
            std::string lower_field_str_check = to_lower_util(field_str);
            if (lower_field_str_check == "sort" || lower_field_str_check == "and") {
                 std::cerr << get_current_timestamp() << " [QueryParser] Error: Unexpected keyword '" << field_str << "' as field name in WHERE clause." << std::endl;
                 return std::nullopt;
            }

            std::string condition_str = get_next_token_qp(ss);
            if (condition_str.empty()) {
                std::cerr << get_current_timestamp() << " [QueryParser] Error: Expected operator after field '" << field_str << "'." << std::endl;
                return std::nullopt;
            }

            std::string full_value_str;
            ss >> std::ws;
            if (ss.peek() == EOF) { // Ожидали значение, но получили конец файла/строки
                 std::cerr << get_current_timestamp() << " [QueryParser] Error: Expected value after operator '" << condition_str << "' for field '" << field_str << "'." << std::endl;
                 return std::nullopt;
            }
            if (ss.peek() == '"') {
                ss.get(); 
                char c_val;
                bool found_closing_quote = false;
                while(ss.get(c_val)) {
                    if (c_val == '"') {
                        found_closing_quote = true;
                        break; 
                    }
                    full_value_str += c_val;
                }
                if (!found_closing_quote) { 
                     std::cerr << get_current_timestamp() << " [QueryParser] Error: Unterminated quoted string for value after '" << field_str << " " << condition_str << "'." << std::endl;
                     return std::nullopt;
                }
            } else {
                full_value_str = get_next_token_qp(ss);
                if (full_value_str.empty() && (ss.fail() || ss.eof())) { 
                     std::cerr << get_current_timestamp() << " [QueryParser] Error: Expected value after operator '" << condition_str << "' for field '" << field_str << "'." << std::endl;
                     return std::nullopt;
                }
            }

            FieldCriterion criterion;
            criterion.field_name_raw = field_str;
            criterion.field_name_lower = to_lower_util(field_str);
            std::string lower_cond = to_lower_util(condition_str);

            if (lower_cond == "=" || lower_cond == "==") criterion.condition = Condition::EQ;
            else if (lower_cond == "!=" || lower_cond == "<>") criterion.condition = Condition::NE;
            else if (lower_cond == "<") criterion.condition = Condition::LT;
            else if (lower_cond == ">") criterion.condition = Condition::GT;
            else if (lower_cond == "<=") criterion.condition = Condition::LE;
            else if (lower_cond == ">=") criterion.condition = Condition::GE;
            else if (lower_cond == "contains") criterion.condition = Condition::CONTAINS;
            else {
                std::cerr << get_current_timestamp() << " [QueryParser] Error: Unknown or unsupported operator '" << condition_str << "'." << std::endl;
                return std::nullopt;
            }

            if (criterion.field_name_lower == "name") {
                if (!criterion.is_string_condition()) {
                    std::cerr << get_current_timestamp() << " [QueryParser] Error: Operator '" << condition_str << "' not valid for field 'name'." << std::endl; return std::nullopt;
                }
                criterion.value = full_value_str;
            } else if (criterion.field_name_lower == "ip") {
                if (!criterion.is_ip_condition()) { 
                     std::cerr << get_current_timestamp() << " [QueryParser] Error: Operator '" << condition_str << "' not valid for 'ip'." << std::endl; return std::nullopt;
                }
                auto ip_val = IpAddress::from_string(full_value_str);
                if (!ip_val) {
                    std::cerr << get_current_timestamp() << " [QueryParser] Error: Invalid IP value '" << full_value_str << "' for field 'ip'." << std::endl; return std::nullopt;
                }
                criterion.value = *ip_val;
            } else if (criterion.field_name_lower == "date") {
                 if (!criterion.is_date_condition()) {
                     std::cerr << get_current_timestamp() << " [QueryParser] Error: Operator '" << condition_str << "' not valid for 'date'." << std::endl; return std::nullopt;
                }
                auto date_val = Date::from_string(full_value_str);
                if (!date_val) {
                    std::cerr << get_current_timestamp() << " [QueryParser] Error: Invalid Date value '" << full_value_str << "' for field 'date'." << std::endl; return std::nullopt;
                }
                criterion.value = *date_val;
            } else { 
                std::cerr << get_current_timestamp() << " [QueryParser] Error: Unknown field name '" << field_str << "' in WHERE clause." << std::endl;
                return std::nullopt;
            }
            query.criteria.push_back(criterion);
            expect_condition = false; // После успешного условия ожидаем AND или конец WHERE

            ss >> std::ws;
            if (ss.peek() == EOF || ss.peek() == ';') break;
        }
         if (expect_condition && !query.criteria.empty()) { // Если ожидали условие после AND, но его не было
            std::cerr << get_current_timestamp() << " [QueryParser] Error: Trailing 'AND' without a condition." << std::endl;
            return std::nullopt;
        }

    } else { 
        ss.clear();
        ss.seekg(before_where_pos);
    }

    // 4. Parse SORT BY (Optional)
    ss >> std::ws;
    std::streampos before_sort_pos = ss.tellg();
    token = get_next_token_qp(ss);

    if (to_lower_util(token) == "sort") {
        std::string by_token = get_next_token_qp(ss);
        if (to_lower_util(by_token) != "by") {
            std::cerr << get_current_timestamp() << " [QueryParser] Error: Expected 'BY' after 'SORT'. Got: '" << by_token << "'" << std::endl;
            return std::nullopt;
        }
        query.sort_by_field = get_next_token_qp(ss);
        if (query.sort_by_field.empty() || to_lower_util(query.sort_by_field) == "asc" || to_lower_util(query.sort_by_field) == "desc") { 
            std::cerr << get_current_timestamp() << " [QueryParser] Error: Expected field name after 'SORT BY'. Got: '" << query.sort_by_field << "'" << std::endl;
            return std::nullopt;
        }
        
        // Проверка на допустимые имена полей для сортировки (можно расширить)
        std::string lower_sort_field = to_lower_util(query.sort_by_field);
        if (lower_sort_field != "name" && lower_sort_field != "ip" && lower_sort_field != "date" && 
            lower_sort_field != "total_traffic" && lower_sort_field != "total_incoming" && lower_sort_field != "total_outgoing") {
            // Если нужны и другие поля для сортировки, их нужно добавить сюда или в ProviderRecord::get_all_field_names() и проверять по нему.
            bool is_hourly_field = false;
            if (lower_sort_field.rfind("traffic_", 0) == 0 && lower_sort_field.length() > 8) { // traffic_XX, traffic_in_XX, traffic_out_XX
                is_hourly_field = true;
            }
            if(!is_hourly_field){
                std::cerr << get_current_timestamp() << " [QueryParser] Error: Invalid or unsupported field '" << query.sort_by_field << "' for SORT BY." << std::endl;
                // return std::nullopt; // Раскомментировать, если нужна строгая проверка полей сортировки здесь
            }
        }


        ss >> std::ws;
        std::string order_str;
        if (ss.peek() != EOF && ss.peek() != ';') {
            std::streampos before_order_pos = ss.tellg();
            order_str = get_next_token_qp(ss);
            std::string lower_order_str = to_lower_util(order_str);
            if (lower_order_str == "asc") {
                query.sort_order = SortOrder::ASC;
            } else if (lower_order_str == "desc") {
                query.sort_order = SortOrder::DESC;
            } else { 
                std::cerr << get_current_timestamp() << " [QueryParser] Error: Invalid sort order '" << order_str << "'. Expected ASC, DESC or nothing." << std::endl;
                return std::nullopt;
            }
        } else { 
            query.sort_order = SortOrder::ASC;
        }
    } else { 
        ss.clear();
        ss.seekg(before_sort_pos);
    }

    // 5. Check for trailing characters
    ss >> std::ws;
    if (ss.peek() == ';') ss.get(); 

    if (ss.peek() != EOF) { 
        std::string remaining_slop_token;
        ss >> remaining_slop_token; // Пробуем прочитать оставшийся токен
        if (!remaining_slop_token.empty()){ // Если что-то прочиталось
             std::cerr << get_current_timestamp() << " [QueryParser] Warning: Trailing characters at end of query: '" << remaining_slop_token << "'" << std::endl;
             // return std::nullopt; // Раскомментировать для более строгой ошибки при наличии "хвоста"
        }
    }
    return query;
}


bool QueryParser::matches(const ProviderRecord& record, const SelectQuery& query) {
    if (query.criteria.empty()) {
        return true;
    }

    for (const auto& crit : query.criteria) {
        bool criterion_match = false;
        try {
            if (crit.field_name_lower == "name") {
                if (auto* val_ptr = std::get_if<std::string>(&crit.value)) {
                    const std::string& crit_val = *val_ptr;
                    switch (crit.condition) {
                        case Condition::EQ: criterion_match = (record.full_name == crit_val); break;
                        case Condition::NE: criterion_match = (record.full_name != crit_val); break;
                        case Condition::CONTAINS: criterion_match = (record.full_name.find(crit_val) != std::string::npos); break;
                        default: /* Should not happen if parser validates operators */ break;
                    }
                }
            } else if (crit.field_name_lower == "ip") {
                if (auto* val_ptr = std::get_if<IpAddress>(&crit.value)) {
                    const IpAddress& crit_val = *val_ptr;
                    switch (crit.condition) {
                        case Condition::EQ: criterion_match = (record.ip_address == crit_val); break;
                        case Condition::NE: criterion_match = (record.ip_address != crit_val); break;
                        case Condition::LT: criterion_match = (record.ip_address < crit_val); break;
                        case Condition::GT: criterion_match = (record.ip_address > crit_val); break;
                        case Condition::LE: criterion_match = (record.ip_address <= crit_val); break;
                        case Condition::GE: criterion_match = (record.ip_address >= crit_val); break;
                        default: /* Should not happen */ break;
                    }
                }
            } else if (crit.field_name_lower == "date") {
                if (auto* val_ptr = std::get_if<Date>(&crit.value)) {
                    const Date& crit_val = *val_ptr;
                    switch (crit.condition) {
                        case Condition::EQ: criterion_match = (record.record_date == crit_val); break;
                        case Condition::NE: criterion_match = (record.record_date != crit_val); break;
                        case Condition::LT: criterion_match = (record.record_date < crit_val); break;
                        case Condition::GT: criterion_match = (record.record_date > crit_val); break;
                        case Condition::LE: criterion_match = (record.record_date <= crit_val); break;
                        case Condition::GE: criterion_match = (record.record_date >= crit_val); break;
                        default: /* Should not happen */ break;
                    }
                }
            }
            // Добавьте сюда обработку других полей, если они поддерживаются в WHERE
        } catch (const std::bad_variant_access& bva) {
            std::cerr << get_current_timestamp() << " [QueryParser] Internal error: Bad variant access in matches for field "
                      << crit.field_name_lower << ". " << bva.what() << std::endl;
            return false; // Ошибка при доступе к значению критерия
        }
        if (!criterion_match) return false; // Если хоть один критерий не совпал, вся запись не подходит
    }
    return true; // Все критерии совпали
}
