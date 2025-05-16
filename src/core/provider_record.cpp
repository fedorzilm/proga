/*!
 * \file provider_record.cpp
 * \author Fedor Zilnitskiy
 * \brief Реализация класса ProviderRecord для представления записи о данных интернет-провайдера.
 */
#include "provider_record.h"
#include "logger.h"          // Для возможного логирования (хотя в основном используются исключения)
#include <iomanip>           // Для std::fixed, std::setprecision в operator<<
#include <sstream>           // Для std::ostringstream в validateTrafficVector
#include <cmath>             // Для std::fabs в operator==
#include <algorithm>         // Для std::any_of (проверка отрицательного трафика)

/*!
 * \brief Внутренний метод валидации вектора трафика.
 * \param traffic Вектор трафика.
 * \param traffic_type_name Описание типа трафика для сообщения об ошибке.
 * \throw std::invalid_argument Если вектор не соответствует требованиям.
 */
void ProviderRecord::validateTrafficVector(const std::vector<double>& traffic, const std::string& traffic_type_name) const {
    if (traffic.size() != static_cast<size_t>(HOURS_IN_DAY)) {
        std::ostringstream ss;
        ss << "Данные о " << traffic_type_name << " трафике должны содержать ровно "
           << HOURS_IN_DAY << " почасовых значений. Предоставлено: " << traffic.size() << " значений.";
        throw std::invalid_argument(ss.str());
    }
    // Проверка на отрицательные значения
    if (std::any_of(traffic.begin(), traffic.end(), [](double val){ return val < -DOUBLE_EPSILON; })) { // Разрешаем очень маленькие отрицательные из-за погрешностей double, но не явно отрицательные
         std::ostringstream ss;
        // Поиск первого отрицательного значения для более информативного сообщения
        double first_negative_val = 0.0;
        bool found = false;
        for(double val : traffic) { 
            if(val < -DOUBLE_EPSILON) { 
                first_negative_val = val; 
                found = true;
                break;
            }
        }
        if (found) {
            ss << "Значение " << traffic_type_name << " трафика не может быть отрицательным. Найдено: " << std::fixed << std::setprecision(2) << first_negative_val << ".";
            throw std::invalid_argument(ss.str());
        }
    }
}

/*!
 * \brief Конструктор по умолчанию.
 */
ProviderRecord::ProviderRecord() noexcept
    : subscriberName_(""),                 // Пустое имя
      ipAddress_(),                       // IPAddress по умолчанию (0.0.0.0)
      date_(),                            // Date по умолчанию (01.01.1970)
      trafficInByHour_(HOURS_IN_DAY, 0.0),  // Вектор нулей
      trafficOutByHour_(HOURS_IN_DAY, 0.0) { // Вектор нулей
    // Logger::debug("ProviderRecord: Default constructor called.");
}

/*!
 * \brief Конструктор с параметрами.
 * \param name Имя абонента.
 * \param ip IP-адрес.
 * \param recordDate Дата записи.
 * \param trafficIn Вектор входящего трафика.
 * \param trafficOut Вектор исходящего трафика.
 * \throw std::invalid_argument Если трафик некорректен.
 */
ProviderRecord::ProviderRecord(const std::string& name,
                               const IPAddress& ip,
                               const Date& recordDate,
                               const std::vector<double>& trafficIn,
                               const std::vector<double>& trafficOut)
    : subscriberName_(name),
      ipAddress_(ip),
      date_(recordDate)
      // trafficInByHour_ и trafficOutByHour_ будут инициализированы после валидации ниже
{
    // Logger::debug("ProviderRecord: Parameterized constructor for '" + name + "'.");
    // Валидация и присвоение трафика. Выбросит исключение, если невалидно.
    validateTrafficVector(trafficIn, "входящем");
    trafficInByHour_ = trafficIn;

    validateTrafficVector(trafficOut, "исходящем");
    trafficOutByHour_ = trafficOut;
}

/*!
 * \brief Устанавливает вектор входящего трафика.
 * \param trafficIn Новый вектор трафика.
 * \throw std::invalid_argument Если трафик некорректен.
 */
void ProviderRecord::setTrafficInByHour(const std::vector<double>& trafficIn) {
    validateTrafficVector(trafficIn, "входящем");
    trafficInByHour_ = trafficIn;
    // Logger::debug("ProviderRecord: TrafficIn set for '" + subscriberName_ + "'.");
}

/*!
 * \brief Устанавливает вектор исходящего трафика.
 * \param trafficOut Новый вектор трафика.
 * \throw std::invalid_argument Если трафик некорректен.
 */
void ProviderRecord::setTrafficOutByHour(const std::vector<double>& trafficOut) {
    validateTrafficVector(trafficOut, "исходящем");
    trafficOutByHour_ = trafficOut;
    // Logger::debug("ProviderRecord: TrafficOut set for '" + subscriberName_ + "'.");
}

/*!
 * \brief Оператор сравнения на равенство.
 * \param other Другая запись.
 * \return true, если записи равны.
 */
bool ProviderRecord::operator==(const ProviderRecord& other) const noexcept {
    if (subscriberName_ != other.subscriberName_ ||
        ipAddress_ != other.ipAddress_ ||
        date_ != other.date_ ||
        trafficInByHour_.size() != other.trafficInByHour_.size() || 
        trafficOutByHour_.size() != other.trafficOutByHour_.size()) {
        return false;
    }

    // Сравнение векторов трафика с учетом DOUBLE_EPSILON
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

/*!
 * \brief Оператор сравнения на неравенство.
 * \param other Другая запись.
 * \return true, если записи не равны.
 */
bool ProviderRecord::operator!=(const ProviderRecord& other) const noexcept {
    return !(*this == other);
}

/*!
 * \brief Оператор вывода ProviderRecord в поток.
 * \param os Выходной поток.
 * \param record Запись для вывода.
 * \return Ссылка на выходной поток.
 */
std::ostream& operator<<(std::ostream& os, const ProviderRecord& record) {
    os << record.subscriberName_ << "\n";
    os << record.ipAddress_ << "\n"; 
    os << record.date_ << "\n";      

    std::ios_base::fmtflags original_flags = os.flags(); 
    os << std::fixed << std::setprecision(2); // Устанавливаем точность для трафика

    for (size_t i = 0; i < record.trafficInByHour_.size(); ++i) {
        os << record.trafficInByHour_[i] << (i == record.trafficInByHour_.size() - 1 ? "" : " ");
    }
    os << "\n";

    for (size_t i = 0; i < record.trafficOutByHour_.size(); ++i) {
        os << record.trafficOutByHour_[i] << (i == record.trafficOutByHour_.size() - 1 ? "" : " ");
    }
    // Не добавляем \n после исходящего трафика здесь,
    // Database::saveToFile добавит \n между записями, если это не последняя запись.
    // Для вывода одной записи (например, в SELECT), вызывающий код может добавить \n, если нужно.

    os.flags(original_flags); // Восстанавливаем исходные флаги форматирования потока
    return os;
}

/*!
 * \brief Оператор ввода ProviderRecord из потока.
 * \param is Входной поток.
 * \param record Запись для считывания.
 * \return Ссылка на входной поток.
 */
std::istream& operator>>(std::istream& is, ProviderRecord& record) {
    std::string nameLine;
    
    // Пропускаем пустые строки перед началом чтения записи
    while (is.peek() == '\n' || is.peek() == '\r') {
        if (!is.ignore()) { // Если ignore не удался (например, EOF)
            is.setstate(std::ios_base::failbit); // Устанавливаем ошибку, если нечего игнорировать
            return is;
        }
    }
    if (!is.good()) return is; // Если после пропуска пробелов поток не в порядке

    if (!std::getline(is, nameLine)) {
        // Не устанавливаем failbit здесь, если это просто EOF и ничего не прочитано.
        // Но если что-то ожидалось, getline установит failbit/eofbit.
        return is; 
    }
    // Если прочитали пустую строку, но поток еще не EOF (например, файл из нескольких \n),
    // это может быть ошибкой формата, если ожидалась непустая строка.
    // Однако, если это конец файла после последней записи, getline вернет пустую строку и установит eof.
    if (nameLine.empty() && !is.eof()) { // Если пустая строка, но не конец файла - возможно, ошибка формата
        // Для простоты, если имя пустое - считаем ошибкой, если не конец файла.
        // is.setstate(std::ios_base::failbit);
        // return is;
        // Более мягкая логика: пустая строка имени допустима, если это не конец файла.
        // Но если после пустой строки ничего нет, то это конец.
    }
     if (nameLine.empty() && is.eof() && is.gcount() == 0) { // Прочитан EOF без символов
        return is; // Это просто конец файла, не ошибка
    }


    record.subscriberName_ = nameLine;

    IPAddress tempIp;
    if (!(is >> tempIp)) { 
        is.setstate(std::ios_base::failbit); return is;
    }
    record.ipAddress_ = tempIp;

    Date tempDate;
    if (!(is >> tempDate)) { 
        is.setstate(std::ios_base::failbit); return is;
    }
    record.date_ = tempDate;

    std::vector<double> tempTrafficIn(HOURS_IN_DAY);
    for (int i = 0; i < HOURS_IN_DAY; ++i) {
        if (!(is >> tempTrafficIn[static_cast<size_t>(i)])) {
            is.setstate(std::ios_base::failbit); return is;
        }
        if (tempTrafficIn[static_cast<size_t>(i)] < -DOUBLE_EPSILON) { // Проверка на отрицательные значения
            // Logger::warn("ProviderRecord istream: Negative traffic_in value " + std::to_string(tempTrafficIn[static_cast<size_t>(i)]));
            is.setstate(std::ios_base::failbit); return is;
        }
    }
    record.trafficInByHour_ = tempTrafficIn;


    std::vector<double> tempTrafficOut(HOURS_IN_DAY);
    for (int i = 0; i < HOURS_IN_DAY; ++i) {
        if (!(is >> tempTrafficOut[static_cast<size_t>(i)])) {
            is.setstate(std::ios_base::failbit); return is;
        }
        if (tempTrafficOut[static_cast<size_t>(i)] < -DOUBLE_EPSILON) {
            // Logger::warn("ProviderRecord istream: Negative traffic_out value " + std::to_string(tempTrafficOut[static_cast<size_t>(i)]));
            is.setstate(std::ios_base::failbit); return is;
        }
    }
    record.trafficOutByHour_ = tempTrafficOut;

    // После чтения последнего числа исходящего трафика, курсор может быть перед \n или EOF.
    // Следующий вызов `is.peek()` в `Database::loadFromFile` обработает это.
    // Или, если это последний вызов `>>` для файла, `is.eof()` будет true.
    // Пропускать здесь `std::ws` или `is.ignore(std::numeric_limits<std::streamsize>::max(), '\n')`
    // может быть излишним и нарушить логику чтения следующей записи в `Database`.
    
    return is;
}
