#include "ip_address.h"
#include <sstream>
#include <vector>
#include <charconv>  
#include <array>   

IpAddress::IpAddress(const std::string& addr) : address_str(addr) {}

bool IpAddress::is_valid() const {
    if (address_str.empty()) return false;

    std::stringstream ss(address_str);
    std::string segment;
    int segment_count = 0;
    
    if (address_str.back() == '.') return false;

    while (std::getline(ss, segment, '.')) {
        segment_count++;
        if (segment.empty() || segment.length() > 3) return false;
        if (segment.length() > 1 && segment[0] == '0') return false; 

        int value;
        for (char c : segment) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        }
        
        auto result = std::from_chars(segment.data(), segment.data() + segment.length(), value);
        if (result.ec != std::errc() || result.ptr != segment.data() + segment.length()) {
            return false; 
        }
        if (value < 0 || value > 255) return false; 
    }
    return segment_count == 4; 
}


std::optional<IpAddress> IpAddress::from_string(const std::string& ip_str) {
    IpAddress ip(ip_str);
    if (ip.is_valid()) {
        return ip;
    } else {
        return std::nullopt;
    }
}

std::string IpAddress::to_string() const {
    return address_str;
}

std::optional<uint32_t> IpAddress::to_uint32() const {
    if (!is_valid()) { 
        return std::nullopt;
    }
    uint32_t result = 0;
    std::stringstream ss(address_str);
    std::string segment;
    int val_segment;
    int shift = 24; 

    for (int i = 0; i < 4; ++i) {
        if (!std::getline(ss, segment, '.')) return std::nullopt; 
        if (segment.empty() || segment.length() > 3) return std::nullopt;
        if (segment.length() > 1 && segment[0] == '0') return std::nullopt;
        for (char c : segment) { if (!std::isdigit(static_cast<unsigned char>(c))) return std::nullopt; }

        auto conv_result = std::from_chars(segment.data(), segment.data() + segment.length(), val_segment);
        if (conv_result.ec != std::errc() || conv_result.ptr != segment.data() + segment.length() || val_segment < 0 || val_segment > 255) {
            return std::nullopt; 
        }
        result |= (static_cast<uint32_t>(val_segment) << shift);
        shift -= 8;
    }
    return result;
}

bool IpAddress::operator<(const IpAddress& other) const {
    auto this_val = to_uint32();
    auto other_val = other.to_uint32();
    if (this_val && other_val) {
        return *this_val < *other_val;
    }
    if (this_val && !other_val) return false; 
    if (!this_val && other_val) return true;  
    return address_str < other.address_str; 
}

bool IpAddress::operator>(const IpAddress& other) const {
    return other < *this;
}

bool IpAddress::operator<=(const IpAddress& other) const {
    return !(other < *this); // or !(*this > other)
}

bool IpAddress::operator>=(const IpAddress& other) const {
    return !(*this < other);
}

bool IpAddress::operator==(const IpAddress& other) const {
    auto this_val = to_uint32();
    auto other_val = other.to_uint32();
    if (this_val && other_val) {
        return *this_val == *other_val;
    }
    return (this_val.has_value() == other_val.has_value()) && (address_str == other.address_str);
}

bool IpAddress::operator!=(const IpAddress& other) const {
    return !(*this == other);
}

std::ostream& operator<<(std::ostream& os, const IpAddress& ip) {
    os << ip.to_string();
    return os;
}

std::istream& operator>>(std::istream& is, IpAddress& ip) {
    std::string ip_str_input;
    if (is >> ip_str_input) {
        auto parsed_ip = IpAddress::from_string(ip_str_input);
        if (parsed_ip) {
            ip = *parsed_ip;
        } else {
            is.setstate(std::ios::failbit);
        }
    }
    return is;
}
