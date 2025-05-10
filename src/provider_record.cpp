#include "provider_record.h"
#include "common_defs.h"
#include <sstream>
#include <iomanip>
#include <numeric>   
#include <algorithm> 
#include <stdexcept> 

ProviderRecord::ProviderRecord() : hourly_traffic(HOURS_IN_DAY) {} 

const std::vector<std::string>& ProviderRecord::get_all_field_names() {
    static const std::vector<std::string> field_names = {
        "name", "ip", "date", 
        "total_traffic", "total_incoming", "total_outgoing"
    };
    return field_names;
}

void ProviderRecord::print(std::ostream& os) const {
    os << "Name:    " << full_name << "\n"
       << "IP Addr: " << ip_address.to_string() << "\n"
       << "Date:    " << record_date.to_string() << "\n"
       << "Traffic (In/Out per hour):\n";
    for (size_t i = 0; i < hourly_traffic.size(); ++i) {
        os << "  Hour " << std::setfill('0') << std::setw(2) << i << ": " 
           << std::left << std::setw(12) << hourly_traffic[i].incoming 
           << "/ " << std::left << std::setw(12) << hourly_traffic[i].outgoing << "\n";
    }
    os << std::right; 
    os << "Total Daily Incoming: " << total_daily_incoming_traffic() << " Bytes\n";
    os << "Total Daily Outgoing: " << total_daily_outgoing_traffic() << " Bytes\n";
    os << "Total Daily Combined: " << total_daily_traffic() << " Bytes\n";
}

void ProviderRecord::print_selected_fields(std::ostream& os, const std::vector<std::string>& fields_to_print) const {
    bool first = true;
    if (fields_to_print.empty() || (fields_to_print.size() == 1 && fields_to_print[0] == "*")) {
        print(os); 
        return;
    }

    for (const auto& field : fields_to_print) {
        if (!first) {
            os << " | ";
        }
        std::string display_field_name = field;
        if (!display_field_name.empty()) { 
            display_field_name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(display_field_name[0])));
        }
        
        try {
            os << display_field_name << ": " << get_field_value_as_string(field);
        } catch(const std::out_of_range&) {
             os << display_field_name << ": [N/A]";
        }
        first = false;
    }
    os << "\n";
}

bool ProviderRecord::read(std::istream& is) {
    std::string line;

    if (!std::getline(is, full_name) || full_name.empty()) return false;

    std::string ip_str_val; 
    if (!std::getline(is, ip_str_val)) return false;
    auto parsed_ip = IpAddress::from_string(ip_str_val);
    if (!parsed_ip) return false;
    ip_address = *parsed_ip;

    std::string date_str_val; 
    if (!std::getline(is, date_str_val)) return false;
    auto parsed_date = Date::from_string(date_str_val);
    if (!parsed_date) return false;
    record_date = *parsed_date;

    if (!std::getline(is, line)) return false;
    std::stringstream ss_traffic(line);
    hourly_traffic.assign(HOURS_IN_DAY, TrafficReading{}); 
    
    long long in_val, out_val;
    size_t hour_count = 0;
    while (hour_count < HOURS_IN_DAY) {
        if (!(ss_traffic >> in_val >> out_val)) { 
            return false; 
        }
        if (in_val < 0 || out_val < 0) return false; 
        hourly_traffic[hour_count].incoming = in_val;
        hourly_traffic[hour_count].outgoing = out_val;
        hour_count++;
    }
    
    std::string remaining_traffic_data;
    if (ss_traffic >> remaining_traffic_data) {       
        return false; 
    }
    
    return hour_count == HOURS_IN_DAY;  
}

void ProviderRecord::write(std::ostream& os) const {
    os << full_name << "\n"
       << ip_address.to_string() << "\n"
       << record_date.to_string() << "\n";
    for (size_t i = 0; i < hourly_traffic.size(); ++i) {
        os << hourly_traffic[i].incoming << " " << hourly_traffic[i].outgoing;
        if (i < hourly_traffic.size() - 1) {
            os << " ";
        }
    }
    os << "\n";
}

bool ProviderRecord::is_valid() const {
    if (full_name.empty() || !ip_address.is_valid() || !record_date.is_valid()) {
        return false;
    }
    if (hourly_traffic.size() != HOURS_IN_DAY) {
        return false;
    }
    for(const auto& reading : hourly_traffic) {
        if (reading.incoming < 0 || reading.outgoing < 0) {
            return false;
        }
    }
    return true;
}

long long ProviderRecord::total_daily_traffic() const {
    long long total = 0;
    for(const auto& reading : hourly_traffic) {
        total += reading.total(); 
    }
    return total;
}

long long ProviderRecord::total_daily_incoming_traffic() const {
    long long total = 0;
    for(const auto& reading : hourly_traffic) {
        total += reading.incoming;
    }
    return total;
}

long long ProviderRecord::total_daily_outgoing_traffic() const {
    long long total = 0;
    for(const auto& reading : hourly_traffic) {
        total += reading.outgoing;
    }
    return total;
}

std::variant<std::string, Date, IpAddress, long long, TrafficReading> ProviderRecord::get_field_value(const std::string& field_name_input) const {
    std::string field_name = to_lower_util(field_name_input);

    if (field_name == "name") return full_name;
    if (field_name == "ip") return ip_address;
    if (field_name == "date") return record_date;
    if (field_name == "total_traffic") return total_daily_traffic();
    if (field_name == "total_incoming") return total_daily_incoming_traffic();
    if (field_name == "total_outgoing") return total_daily_outgoing_traffic();
    
    if (field_name.rfind("traffic_", 0) == 0) { 
        try {
             if (field_name.rfind("traffic_in_", 0) == 0 && field_name.length() == 11 + 2) { 
                 std::string hour_str = field_name.substr(11);
                 int hour_idx = std::stoi(hour_str);
                  if (hour_idx >= 0 && static_cast<size_t>(hour_idx) < hourly_traffic.size()) {
                     return hourly_traffic[static_cast<size_t>(hour_idx)].incoming;
                 }
             } else if (field_name.rfind("traffic_out_", 0) == 0 && field_name.length() == 12 + 2) { 
                 std::string hour_str = field_name.substr(12);
                 int hour_idx = std::stoi(hour_str);
                  if (hour_idx >= 0 && static_cast<size_t>(hour_idx) < hourly_traffic.size()) {
                     return hourly_traffic[static_cast<size_t>(hour_idx)].outgoing;
                 }
             } else if (field_name.length() == 8 + 2) { 
                 std::string hour_str = field_name.substr(8);
                 int hour_idx = std::stoi(hour_str);
                 if (hour_idx >= 0 && static_cast<size_t>(hour_idx) < hourly_traffic.size()) {
                     return hourly_traffic[static_cast<size_t>(hour_idx)]; 
                 }
             }
        } catch (const std::invalid_argument&) { 
             throw std::out_of_range("Invalid hour format in field name: " + field_name_input);
        } catch (const std::out_of_range&) { 
             throw std::out_of_range("Hour index out of range in field name: " + field_name_input);
        }
    }
    throw std::out_of_range("Unknown or malformed field name for get_field_value: " + field_name_input);
}

std::string ProviderRecord::get_field_value_as_string(const std::string& field_name) const {
    auto value_variant = get_field_value(field_name); 
    std::ostringstream oss;
    std::visit([&oss](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::string>) {
            oss << arg;
        } else if constexpr (std::is_same_v<T, IpAddress>) {
            oss << arg.to_string();
        } else if constexpr (std::is_same_v<T, Date>) {
            oss << arg.to_string();
        } else if constexpr (std::is_same_v<T, long long>) {
            oss << arg;
        } else if constexpr (std::is_same_v<T, TrafficReading>) {
            oss << arg.incoming << "/" << arg.outgoing;
        }
    }, value_variant);
    return oss.str();
}
