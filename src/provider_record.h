#ifndef PROVIDER_RECORD_H
#define PROVIDER_RECORD_H

#include "common_defs.h"
#include "date.h"
#include "ip_address.h"
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <map>     
#include <variant> 

struct ProviderRecord {
    std::string full_name;
    IpAddress ip_address;
    Date record_date;
    TrafficData hourly_traffic; 

    ProviderRecord(); 

    void print(std::ostream& os = std::cout) const;
    void print_selected_fields(std::ostream& os, const std::vector<std::string>& fields) const;

    bool read(std::istream& is); 
    void write(std::ostream& os) const; 

    bool is_valid() const;
    long long total_daily_traffic() const; 
    long long total_daily_incoming_traffic() const;
    long long total_daily_outgoing_traffic() const;

    std::variant<std::string, Date, IpAddress, long long, TrafficReading> get_field_value(const std::string& field_name) const;
    std::string get_field_value_as_string(const std::string& field_name) const;

    static const std::vector<std::string>& get_all_field_names(); 
};

#endif // PROVIDER_RECORD_H
