#ifndef QUERY_PARSER_H
#define QUERY_PARSER_H

#include "common_defs.h" 
#include "date.h"        
#include "ip_address.h"  
#include <string>
#include <vector>
#include <variant>
#include <optional>

struct FieldCriterion {
    std::string field_name_raw;
    std::string field_name_lower;
    Condition condition = Condition::NONE;
    std::variant<std::string, Date, IpAddress, long long> value; 

    bool is_string_condition() const;
    bool is_date_condition() const;
    bool is_ip_condition() const; 
};

struct SelectQuery {
    std::vector<FieldCriterion> criteria;      
    std::vector<std::string> select_fields;    
    std::string sort_by_field;                 
    SortOrder sort_order = SortOrder::NONE;    

    bool wants_all_fields() const {
        return select_fields.empty() || (select_fields.size() == 1 && select_fields[0] == "*");
    }
};

struct ProviderRecord; 

class QueryParser {
public:
    static std::optional<SelectQuery> parse_select(const std::string& query_string);
    static bool matches(const ProviderRecord& record, const SelectQuery& query); 
}; 

#endif // QUERY_PARSER_H

