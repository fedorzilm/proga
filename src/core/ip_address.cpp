/*!
 * \file ip_address.cpp
 * \author Fedor Zilnitskiy
 * \brief Реализация класса IPAddress для работы с IPv4-адресами.
 */
#include "ip_address.h"
#include "logger.h"     // Для возможного логирования ошибок (хотя в основном используются исключения)
#include <sstream>      // Для std::ostringstream при конвертации в строку и std::istringstream при парсинге
// <array> уже включен через ip_address.h -> common_defs.h

/*!
 * \brief Проверяет валидность одного октета.
 * \param octet_val Значение октета.
 * \return true, если октет в диапазоне [0, 255].
 */
bool IPAddress::isValidOctet(int octet_val) const noexcept {
    return octet_val >= 0 && octet_val <= 255;
}

/*!
 * \brief Валидирует все четыре октета.
 * \param o1 Первый октет.
 * \param o2 Второй октет.
 * \param o3 Третий октет.
 * \param o4 Четвертый октет.
 * \throw std::invalid_argument Если хотя бы один октет невалиден.
 */
void IPAddress::validateOctets(int o1, int o2, int o3, int o4) {
    if (!isValidOctet(o1) || !isValidOctet(o2) || !isValidOctet(o3) || !isValidOctet(o4)) {
        std::ostringstream ss;
        ss << "Некорректные значения октетов IP-адреса: "
           << o1 << "." << o2 << "." << o3 << "." << o4
           << ". Каждый октет должен быть в диапазоне от 0 до 255.";
        // Logger::warn("IPAddress Validation: " + ss.str()); // Можно логировать, но исключение важнее
        throw std::invalid_argument(ss.str());
    }
}

/*!
 * \brief Конструктор по умолчанию. Инициализирует IP как 0.0.0.0.
 */
IPAddress::IPAddress() noexcept : octets_{{0, 0, 0, 0}} {
    // 0.0.0.0 - валидный IP-адрес по умолчанию.
}

/*!
 * \brief Конструктор с четырьмя октетами.
 * \param o1 Первый октет.
 * \param o2 Второй октет.
 * \param o3 Третий октет.
 * \param o4 Четвертый октет.
 * \throw std::invalid_argument Если октеты невалидны.
 */
IPAddress::IPAddress(int o1, int o2, int o3, int o4) {
    // Инициализируем нулями на случай, если validateOctets выбросит исключение до присвоения.
    // Хотя в C++ присвоение в теле конструктора после проверки - стандартная практика.
    octets_.fill(0); 
    validateOctets(o1, o2, o3, o4); // Выбрасывает исключение при ошибке
    octets_[0] = static_cast<unsigned char>(o1);
    octets_[1] = static_cast<unsigned char>(o2);
    octets_[2] = static_cast<unsigned char>(o3);
    octets_[3] = static_cast<unsigned char>(o4);
}

/*!
 * \brief Преобразует IP-адрес в строку.
 * \return Строковое представление IP-адреса.
 */
std::string IPAddress::toString() const {
    std::ostringstream oss;
    oss << static_cast<int>(octets_[0]) << "."
        << static_cast<int>(octets_[1]) << "."
        << static_cast<int>(octets_[2]) << "."
        << static_cast<int>(octets_[3]);
    return oss.str();
}

/*!
 * \brief Сравнивает два IP-адреса на равенство.
 * \param other Другой IP-адрес.
 * \return true, если адреса равны.
 */
bool IPAddress::operator==(const IPAddress& other) const noexcept {
    return octets_ == other.octets_;
}

/*!
 * \brief Сравнивает два IP-адреса на неравенство.
 * \param other Другой IP-адрес.
 * \return true, если адреса не равны.
 */
bool IPAddress::operator!=(const IPAddress& other) const noexcept {
    return !(*this == other);
}

/*!
 * \brief Сравнивает два IP-адреса (меньше).
 * \param other Другой IP-адрес.
 * \return true, если текущий адрес лексикографически меньше.
 */
bool IPAddress::operator<(const IPAddress& other) const noexcept {
    // Сравнение std::array уже выполняет лексикографическое сравнение
    return octets_ < other.octets_;
}

/*!
 * \brief Оператор вывода IP-адреса в поток.
 * \param os Выходной поток.
 * \param ip IP-адрес для вывода.
 * \return Ссылка на выходной поток.
 */
std::ostream& operator<<(std::ostream& os, const IPAddress& ip) {
    os << ip.toString();
    return os;
}

/*!
 * \brief Оператор ввода IP-адреса из потока.
 * Ожидает формат "o1.o2.o3.o4".
 * \param is Входной поток.
 * \param ip IP-адрес для считывания.
 * \return Ссылка на входной поток.
 */
std::istream& operator>>(std::istream& is, IPAddress& ip) {
    std::string s;
    // Читаем строку до пробельного символа. Предполагается, что IP-адрес - это одно "слово".
    if (!(is >> s)) { 
        return is; // Ошибка чтения или конец потока
    }

    if (s.empty()) { // Дополнительная проверка, хотя is >> s обычно не вернет пустую строку при успехе
        is.setstate(std::ios_base::failbit);
        return is;
    }

    int o1_in = 0, o2_in = 0, o3_in = 0, o4_in = 0;
    char dot1 = 0, dot2 = 0, dot3 = 0;
    // char extra_char_check = 0; // Для проверки лишних символов в конце строки

    std::istringstream ip_ss(s); // Используем строковый поток для разбора строки s
    
    // Пытаемся считать 4 числа и 3 точки между ними
    ip_ss >> o1_in >> dot1 >> o2_in >> dot2 >> o3_in >> dot3 >> o4_in;

    // Проверяем состояние потока ip_ss:
    // 1. ip_ss.fail(): была ли ошибка при чтении чисел (например, нечисловые символы)?
    // 2. dot1, dot2, dot3: являются ли разделители точками?
    // 3. ip_ss.get(extra_char_check) && !ip_ss.eof(): остались ли еще символы в строке s после o4_in?
    //    (ip_ss.eof() после успешного чтения последнего числа - это нормально)
    //    Более надежная проверка - считать пробелы и затем проверить на eof.
    
    ip_ss >> std::ws; // Потребляем оставшиеся пробельные символы

    if (ip_ss.fail() || dot1 != '.' || dot2 != '.' || dot3 != '.' || !ip_ss.eof()) {
        // Если после потребления пробелов поток не в состоянии eof, значит были лишние не-пробельные символы.
        // Если ip_ss.fail() был установлен ранее (например, при чтении чисел), это тоже ошибка.
        is.setstate(std::ios_base::failbit); // Ошибка формата
        return is;
    }

    try {
        // Валидация и присвоение через конструктор (или временный объект)
        IPAddress temp_ip(o1_in, o2_in, o3_in, o4_in);
        ip.octets_ = temp_ip.octets_; // Присваиваем, если конструктор не выбросил исключение
    } catch (const std::invalid_argument& /*e*/) {
        // Конструктор IPAddress выбросил исключение (невалидные октеты)
        is.setstate(std::ios_base::failbit); 
    }
    return is;
}
