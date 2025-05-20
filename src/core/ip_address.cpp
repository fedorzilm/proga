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
 * \brief Сравнивает два IP-адреса (больше).
 * \param other Другой IP-адрес.
 * \return true, если текущий адрес лексикографически больше.
 */
bool IPAddress::operator>(const IPAddress& other) const noexcept { // <-- ДОБАВЛЕНА РЕАЛИЗАЦИЯ
    return other < *this;
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
    if (!(is >> s)) { // Читаем одно слово, разделенное пробелами
        return is;    // Ошибка чтения или конец потока, состояние потока уже установлено
    }

    if (s.empty() && !is.eof()) {
        is.setstate(std::ios_base::failbit);
        return is;
    }
     // Если s пустое потому что достигнут конец файла сразу, просто выходим
    if (s.empty() && is.eof()){
        return is;
    }

    std::istringstream ip_ss(s);
    int o1_in = 0, o2_in = 0, o3_in = 0, o4_in = 0;
    char dot1 = 0, dot2 = 0, dot3 = 0;

    // Пытаемся считать компоненты IP и разделители-точки
    if (!(ip_ss >> o1_in)) { is.setstate(std::ios_base::failbit); return is; }
    if (!(ip_ss >> dot1) || dot1 != '.') { is.setstate(std::ios_base::failbit); return is; }
    if (!(ip_ss >> o2_in)) { is.setstate(std::ios_base::failbit); return is; }
    if (!(ip_ss >> dot2) || dot2 != '.') { is.setstate(std::ios_base::failbit); return is; }
    if (!(ip_ss >> o3_in)) { is.setstate(std::ios_base::failbit); return is; }
    if (!(ip_ss >> dot3) || dot3 != '.') { is.setstate(std::ios_base::failbit); return is; }
    if (!(ip_ss >> o4_in)) { is.setstate(std::ios_base::failbit); return is; }

    // Проверяем, остались ли в строке `s` какие-либо не пробельные символы после IP
    ip_ss >> std::ws; // Потребляем любые пробелы в конце токена
    if (!ip_ss.eof()) {
        is.setstate(std::ios_base::failbit); // Лишние символы в токене
        return is;
    }

    // Если все компоненты успешно считаны и формат строки `s` корректен,
    // пытаемся создать объект IPAddress (что вызовет валидацию диапазонов).
    try {
        IPAddress temp_ip(o1_in, o2_in, o3_in, o4_in); // Валидация происходит в конструкторе
        ip.octets_ = temp_ip.octets_; // Присваиваем, если валидация прошла успешно
    } catch (const std::invalid_argument&) {
        // Конструктор IPAddress выбросил исключение (невалидные октеты)
        is.setstate(std::ios_base::failbit);
    }
    return is;
}
