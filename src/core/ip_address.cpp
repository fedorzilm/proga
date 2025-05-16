// Предполагаемый путь: src/core/ip_address.cpp
#include "ip_address.h" // Предполагаемый путь: src/core/ip_address.h
#include "logger.h"     // Предполагаемый путь: src/utils/logger.h
#include <sstream>      // Для std::ostringstream, std::istringstream
#include <array>        // Уже в ip_address.h

bool IPAddress::isValidOctet(int octet_val) const noexcept {
    return octet_val >= 0 && octet_val <= 255;
}

void IPAddress::validateOctets(int o1, int o2, int o3, int o4) {
    if (!isValidOctet(o1) || !isValidOctet(o2) || !isValidOctet(o3) || !isValidOctet(o4)) {
        std::ostringstream ss;
        ss << "Некорректные значения октетов IP-адреса: "
           << o1 << "." << o2 << "." << o3 << "." << o4
           << ". Каждый октет должен быть в диапазоне от 0 до 255.";
        // Logger::warn("IPAddress::validateOctets: " + ss.str()); // Лог не всегда нужен для invalid_argument
        throw std::invalid_argument(ss.str());
    }
}

IPAddress::IPAddress() noexcept : octets_{{0, 0, 0, 0}} {
    // 0.0.0.0 - валидный IP по умолчанию
}

IPAddress::IPAddress(int o1, int o2, int o3, int o4) {
    validateOctets(o1, o2, o3, o4);
    octets_[0] = static_cast<unsigned char>(o1);
    octets_[1] = static_cast<unsigned char>(o2);
    octets_[2] = static_cast<unsigned char>(o3);
    octets_[3] = static_cast<unsigned char>(o4);
}

std::string IPAddress::toString() const {
    std::ostringstream oss;
    oss << static_cast<int>(octets_[0]) << "."
        << static_cast<int>(octets_[1]) << "."
        << static_cast<int>(octets_[2]) << "."
        << static_cast<int>(octets_[3]);
    return oss.str();
}

bool IPAddress::operator==(const IPAddress& other) const noexcept {
    return octets_ == other.octets_;
}

bool IPAddress::operator!=(const IPAddress& other) const noexcept {
    return !(*this == other);
}

bool IPAddress::operator<(const IPAddress& other) const noexcept {
    return octets_ < other.octets_;
}

std::ostream& operator<<(std::ostream& os, const IPAddress& ip) {
    os << ip.toString();
    return os;
}

std::istream& operator>>(std::istream& is, IPAddress& ip) {
    std::string s;
    if (!(is >> s)) { // Читаем "слово"
        return is;
    }

    if (s.empty()) {
        is.setstate(std::ios_base::failbit);
        return is;
    }

    int o1_in = 0, o2_in = 0, o3_in = 0, o4_in = 0;
    char dot1 = 0, dot2 = 0, dot3 = 0;
    char extra_char = 0; // Для проверки лишних символов

    std::istringstream ip_ss(s);
    // Пытаемся считать 4 числа и 3 точки между ними
    ip_ss >> o1_in >> dot1 >> o2_in >> dot2 >> o3_in >> dot3 >> o4_in;

    // Проверяем состояние потока ip_ss (не было ли ошибок при чтении чисел)
    // Проверяем, что разделители - это точки
    // Проверяем, что после чтения последнего октета не осталось других символов в строке s
    if (ip_ss.fail() || dot1 != '.' || dot2 != '.' || dot3 != '.' || (ip_ss.get(extra_char) && !ip_ss.eof())) {
        is.setstate(std::ios_base::failbit);
        return is;
    }

    try {
        // Валидация и присвоение через конструктор
        IPAddress temp_ip(o1_in, o2_in, o3_in, o4_in);
        ip.octets_ = temp_ip.octets_; // Присваиваем, если конструктор не выбросил исключение
    } catch (const std::invalid_argument& /*e*/) {
        is.setstate(std::ios_base::failbit); // Ошибка валидации октетов
    }
    return is;
}
