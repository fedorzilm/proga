#ifndef IP_ADDRESS_H
#define IP_ADDRESS_H

#include <string>
#include <iostream>
#include <optional>
#include <cstdint> 

class IpAddress {
public:
    std::string address_str;

    IpAddress(const std::string& addr = "");

    bool is_valid() const;
    static std::optional<IpAddress> from_string(const std::string& ip_str);

    std::string to_string() const;
    std::optional<uint32_t> to_uint32() const; 

    bool operator<(const IpAddress& other) const;
    bool operator>(const IpAddress& other) const;
    bool operator<=(const IpAddress& other) const;
    bool operator>=(const IpAddress& other) const;
    bool operator==(const IpAddress& other) const;
    bool operator!=(const IpAddress& other) const;
};

std::ostream& operator<<(std::ostream& os, const IpAddress& ip);
std::istream& operator>>(std::istream& is, IpAddress& ip);

#endif // IP_ADDRESS_H
