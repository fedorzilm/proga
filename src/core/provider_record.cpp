// Предполагаемый путь: src/core/provider_record.cpp
#include "provider_record.h" // Предполагаемый путь: src/core/provider_record.h
#include "logger.h"          // Предполагаемый путь: src/utils/logger.h
#include <iomanip>           // Для std::fixed, std::setprecision в operator<<
#include <sstream>           // Для std::ostringstream в validateTrafficVector
#include <cmath>             // Для std::fabs в operator==
#include <algorithm>         // Для std::all_of (можно использовать вместо цикла)

void ProviderRecord::validateTrafficVector(const std::vector<double>& traffic, const std::string& traffic_type_name) const {
    if (traffic.size() != static_cast<size_t>(HOURS_IN_DAY)) { // Явное приведение HOURS_IN_DAY к size_t для сравнения
        std::ostringstream ss;
        ss << "Данные о " << traffic_type_name << " трафике должны содержать ровно "
           << HOURS_IN_DAY << " почасовых значений. Предоставлено: " << traffic.size();
        // Logger::warn("ProviderRecord::validateTrafficVector: " + ss.str()); // Лог не всегда нужен для invalid_argument
        throw std::invalid_argument(ss.str());
    }
    // Проверка на отрицательные значения
    if (std::any_of(traffic.begin(), traffic.end(), [](double val){ return val < 0.0; })) {
         std::ostringstream ss;
        // Можно найти первое отрицательное значение для более детального сообщения
        double first_negative = 0.0;
        for(double val : traffic) { if(val < 0.0) { first_negative = val; break;}}
        ss << "Значение " << traffic_type_name << " трафика не может быть отрицательным. Найдено: " << first_negative;
        throw std::invalid_argument(ss.str());
    }
}

ProviderRecord::ProviderRecord() noexcept
    : subscriberName_(""),
      ipAddress_(),      // Конструктор IPAddress по умолчанию
      date_(),           // Конструктор Date по умолчанию
      trafficInByHour_(HOURS_IN_DAY, 0.0),
      trafficOutByHour_(HOURS_IN_DAY, 0.0) {
}

ProviderRecord::ProviderRecord(const std::string& name,
                               const IPAddress& ip,
                               const Date& recordDate,
                               const std::vector<double>& trafficIn,
                               const std::vector<double>& trafficOut)
    : subscriberName_(name),
      ipAddress_(ip),
      date_(recordDate)
      // trafficInByHour_ и trafficOutByHour_ будут инициализированы после валидации
{
    // Валидация и присвоение трафика. Бросит исключение, если невалидно.
    validateTrafficVector(trafficIn, "входящем");
    trafficInByHour_ = trafficIn;

    validateTrafficVector(trafficOut, "исходящем");
    trafficOutByHour_ = trafficOut;
}

void ProviderRecord::setTrafficInByHour(const std::vector<double>& trafficIn) {
    validateTrafficVector(trafficIn, "входящем");
    trafficInByHour_ = trafficIn;
}

void ProviderRecord::setTrafficOutByHour(const std::vector<double>& trafficOut) {
    validateTrafficVector(trafficOut, "исходящем");
    trafficOutByHour_ = trafficOut;
}

bool ProviderRecord::operator==(const ProviderRecord& other) const noexcept {
    if (subscriberName_ != other.subscriberName_ ||
        ipAddress_ != other.ipAddress_ ||
        date_ != other.date_ ||
        trafficInByHour_.size() != other.trafficInByHour_.size() || // Доп. проверка, хотя должна быть гарантирована
        trafficOutByHour_.size() != other.trafficOutByHour_.size()) {
        return false;
    }

    for (size_t i = 0; i < trafficInByHour_.size(); ++i) {
        if (std::fabs(trafficInByHour_[i] - other.trafficInByHour_[i]) > DOUBLE_EPSILON) {
            return false;
        }
    }
    for (size_t i = 0; i < trafficOutByHour_.size(); ++i) {
        if (std::fabs(trafficOutByHour_[i] - other.trafficOutByHour_[i]) > DOUBLE_EPSILON) {
            return false;
        }
    }
    return true;
}

bool ProviderRecord::operator!=(const ProviderRecord& other) const noexcept {
    return !(*this == other);
}

std::ostream& operator<<(std::ostream& os, const ProviderRecord& record) {
    // Формат вывода как в ваших тестах и требованиях
    os << record.subscriberName_ << "\n";
    os << record.ipAddress_ << "\n"; // IPAddress::operator<<
    os << record.date_ << "\n";      // Date::operator<<

    // Вывод трафика с заданной точностью
    std::ios_base::fmtflags original_flags = os.flags(); // Сохраняем текущие флаги форматирования
    os << std::fixed << std::setprecision(2);

    for (size_t i = 0; i < record.trafficInByHour_.size(); ++i) {
        os << record.trafficInByHour_[i] << (i == record.trafficInByHour_.size() - 1 ? "" : " ");
    }
    os << "\n";

    for (size_t i = 0; i < record.trafficOutByHour_.size(); ++i) {
        os << record.trafficOutByHour_[i] << (i == record.trafficOutByHour_.size() - 1 ? "" : " ");
    }
    // Не добавляем \n после исходящего трафика, если это формат одной записи,
    // а Database::saveToFile добавит \n между записями.
    // Но для PRINT_ALL и вывода отдельных записей в UserInterface это может быть нужно.
    // В текущей версии UserInterface.cpp \n добавляется после всей записи. Сохраним это поведение.

    os.flags(original_flags); // Восстанавливаем флаги
    return os;
}

std::istream& operator>>(std::istream& is, ProviderRecord& record) {
    std::string nameLine;
    // Читаем имя абонента (может содержать пробелы, поэтому getline)
    if (!std::getline(is, nameLine)) {
        // Logger::debug("ProviderRecord<<: Failed to read name line or EOF.");
        return is; // Ошибка или конец файла
    }
    // Пропускаем пустые строки, если они могут быть между записями
    while(nameLine.empty() && is.peek() != EOF && is.good()){
        if (!std::getline(is, nameLine)) return is;
    }
    if(nameLine.empty() && (is.eof() || !is.good())) return is; // Если после пустых строк все равно конец

    record.subscriberName_ = nameLine;

    IPAddress tempIp;
    if (!(is >> tempIp)) { // Читаем IP
        // Logger::debug("ProviderRecord<<: Failed to read IP for " + nameLine);
        is.setstate(std::ios_base::failbit); return is;
    }
    record.ipAddress_ = tempIp;

    Date tempDate;
    if (!(is >> tempDate)) { // Читаем дату
        // Logger::debug("ProviderRecord<<: Failed to read Date for " + nameLine);
        is.setstate(std::ios_base::failbit); return is;
    }
    record.date_ = tempDate;

    // Читаем входящий трафик (24 значения double)
    std::vector<double> tempTrafficIn(HOURS_IN_DAY);
    for (int i = 0; i < HOURS_IN_DAY; ++i) {
        if (!(is >> tempTrafficIn[static_cast<size_t>(i)])) {
            // Logger::debug("ProviderRecord<<: Failed to read traffic_in value #" + std::to_string(i) + " for " + nameLine);
            is.setstate(std::ios_base::failbit); return is;
        }
        if (tempTrafficIn[static_cast<size_t>(i)] < 0.0) {
            // Logger::warn("ProviderRecord<<: Negative traffic_in value #" + std::to_string(i) + " for " + nameLine);
            is.setstate(std::ios_base::failbit); return is;
        }
    }
    // Валидация размера не нужна, если мы всегда читаем HOURS_IN_DAY
    record.trafficInByHour_ = tempTrafficIn;


    // Читаем исходящий трафик (24 значения double)
    std::vector<double> tempTrafficOut(HOURS_IN_DAY);
    for (int i = 0; i < HOURS_IN_DAY; ++i) {
        if (!(is >> tempTrafficOut[static_cast<size_t>(i)])) {
            // Logger::debug("ProviderRecord<<: Failed to read traffic_out value #" + std::to_string(i) + " for " + nameLine);
            is.setstate(std::ios_base::failbit); return is;
        }
        if (tempTrafficOut[static_cast<size_t>(i)] < 0.0) {
            // Logger::warn("ProviderRecord<<: Negative traffic_out value #" + std::to_string(i) + " for " + nameLine);
            is.setstate(std::ios_base::failbit); return is;
        }
    }
    record.trafficOutByHour_ = tempTrafficOut;

    // Пропускаем оставшиеся пробельные символы до конца строки или до следующей непустой строки
    // Это важно, если записи разделены одной или несколькими новыми строками.
    // is >> double сам пропускает пробелы перед числом.
    // После чтения последнего числа исходящего трафика, курсор может быть перед \n.
    // Следующий std::getline для имени новой записи должен корректно считать следующую строку.
    // Если файл заканчивается без \n после последней записи, is.peek() == EOF.
    // Если есть \n, то is.peek() покажет его. std::getline съест его.
    // Если есть несколько \n, std::getline для имени съест первый, и имя будет пустым.
    // Поэтому цикл while(nameLine.empty() ...) в начале важен.

    return is;
}
