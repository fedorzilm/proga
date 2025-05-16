// Предполагаемый путь: src/core/query_parser.cpp
#include "query_parser.h"
#include "logger.h" // Предполагаемый путь: src/utils/logger.h
#include <algorithm> // Для std::transform
#include <stdexcept> // Для std::runtime_error, std::invalid_argument, std::stod
#include <cctype>    // Для std::toupper, std::isspace

// Вспомогательная функция для преобразования строки в верхний регистр.
// Используется для регистронезависимого сравнения ключевых слов.
static std::string toUpperQP(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return static_cast<char>(std::toupper(c)); });
    return s;
}

std::vector<std::string> QueryParser::tokenize(const std::string& queryString) const {
    std::vector<std::string> tokens;
    std::string current_token_buffer;
    bool in_quote = false;

    for (char ch : queryString) {
        if (in_quote) {
            if (ch == '"') {
                in_quote = false;
                tokens.push_back(current_token_buffer); // Добавляем содержимое кавычек
                current_token_buffer.clear();
            } else {
                current_token_buffer += ch;
            }
        } else {
            if (ch == '"') {
                in_quote = true;
                if (!current_token_buffer.empty()) { // Токен перед открывающей кавычкой
                    tokens.push_back(current_token_buffer);
                    current_token_buffer.clear();
                }
                // Сама кавычка не добавляется как токен, но меняет состояние
            } else if (std::isspace(static_cast<unsigned char>(ch))) {
                if (!current_token_buffer.empty()) {
                    tokens.push_back(current_token_buffer);
                    current_token_buffer.clear();
                }
                // Пробелы между токенами игнорируются
            } else {
                current_token_buffer += ch;
            }
        }
    }

    if (in_quote) {
        std::string error_msg = "Ошибка токенизации: незакрытая кавычка в строке запроса, начиная с: \"" + current_token_buffer;
        Logger::error("QueryParser::tokenize: " + error_msg);
        throw std::runtime_error(error_msg);
    }
    if (!current_token_buffer.empty()) { // Добавляем последний токен, если он есть
        tokens.push_back(current_token_buffer);
    }
    return tokens;
}

void QueryParser::parseTrafficBlock(const std::vector<std::string>& tokens,
                                   std::vector<double>& traffic_vector, // Выходной параметр
                                   size_t& currentIndex,               // Текущий индекс токена (вход/выход)
                                   const std::string& commandName,      // "ADD" или "EDIT SET"
                                   const std::string& traffic_type_name) const { // "TRAFFIC_IN" или "TRAFFIC_OUT"
    traffic_vector.clear(); // Очищаем на случай повторного использования
    traffic_vector.reserve(HOURS_IN_DAY);

    // currentIndex УЖЕ указывает на ПЕРВОЕ значение трафика (токен ПОСЛЕ TRAFFIC_IN/OUT)
    for (int i = 0; i < HOURS_IN_DAY; ++i) {
        if (currentIndex >= tokens.size()) {
            std::string error_msg = commandName + " " + traffic_type_name + ": Недостаточно значений трафика. Ожидалось " +
                                     std::to_string(HOURS_IN_DAY) + ", найдено " + std::to_string(i) + ".";
            Logger::error("QueryParser::parseTrafficBlock: " + error_msg);
            throw std::runtime_error(error_msg);
        }

        const std::string& token = tokens[currentIndex];
        // Проверка, не является ли текущий токен ключевым словом следующего параметра/команды
        std::string upper_token = toUpperQP(token);
        if (upper_token == "END" || upper_token == "FIO" || upper_token == "IP" || upper_token == "DATE" ||
            upper_token == "TRAFFIC_IN" || upper_token == "TRAFFIC_OUT" ||
            upper_token == "SET" || upper_token == "START_DATE" || upper_token == "END_DATE") {
            std::string error_msg = commandName + " " + traffic_type_name + ": Недостаточно значений трафика. Ожидалось " +
                                     std::to_string(HOURS_IN_DAY) + ", найдено " + std::to_string(i) +
                                     " перед ключевым словом '" + token + "'.";
            Logger::error("QueryParser::parseTrafficBlock: " + error_msg);
            throw std::runtime_error(error_msg);
        }

        try {
            size_t processed_chars = 0;
            double val = std::stod(token, &processed_chars);
            if (processed_chars != token.length()) { // Проверяем, что вся строка была числом
                 throw std::invalid_argument("Лишние символы в числовом значении трафика: '" + token.substr(processed_chars) + "'");
            }
            if (val < 0.0) {
                throw std::invalid_argument("Значение трафика не может быть отрицательным: " + std::to_string(val));
            }
            traffic_vector.push_back(val);
        } catch (const std::invalid_argument& e) {
            std::string error_msg = commandName + " " + traffic_type_name + ": Некорректное числовое значение для часа " +
                                     std::to_string(i) + " ('" + token + "'): " + e.what();
            Logger::error("QueryParser::parseTrafficBlock: " + error_msg);
            throw std::runtime_error(error_msg);
        } catch (const std::out_of_range& ) { // От std::stod
            std::string error_msg = commandName + " " + traffic_type_name + ": Значение трафика для часа " +
                                     std::to_string(i) + " ('" + token + "') выходит за пределы допустимого диапазона double.";
            Logger::error("QueryParser::parseTrafficBlock: " + error_msg);
            throw std::runtime_error(error_msg);
        }
        currentIndex++; // Переходим к следующему токену для следующего значения трафика
    }
}


void QueryParser::parseAddParams(const std::vector<std::string>& tokens, QueryParameters& params, size_t& currentIndex) const {
    // Флаги для проверки дублирования параметров
    bool fio_set = false, ip_set = false, date_set = false, traffic_in_set = false, traffic_out_set = false;

    while (currentIndex < tokens.size()) {
        std::string keyword_upper = toUpperQP(tokens[currentIndex]);
        if (keyword_upper == "END") {
            currentIndex++; // Пропускаем END
            break;
        }

        // Сохраняем токен ключевого слова для сообщений об ошибках
        std::string current_keyword_token = tokens[currentIndex];
        currentIndex++; // Переходим к значению или следующему ключевому слову

        if (keyword_upper == "FIO") {
            if (fio_set) throw std::runtime_error("ADD: Параметр FIO указан более одного раза.");
            if (currentIndex >= tokens.size()) throw std::runtime_error("ADD: Отсутствует значение для FIO.");
            params.subscriberNameData = tokens[currentIndex++];
            fio_set = true;
        } else if (keyword_upper == "IP") {
            if (ip_set) throw std::runtime_error("ADD: Параметр IP указан более одного раза.");
            if (currentIndex >= tokens.size()) throw std::runtime_error("ADD: Отсутствует значение для IP.");
            std::istringstream ip_ss(tokens[currentIndex]);
            if (!(ip_ss >> params.ipAddressData) || (ip_ss.rdbuf() && ip_ss.rdbuf()->in_avail() != 0) ) {
                throw std::runtime_error("ADD: Некорректный формат IP-адреса: '" + tokens[currentIndex] + "'");
            }
            currentIndex++;
            ip_set = true;
        } else if (keyword_upper == "DATE") {
            if (date_set) throw std::runtime_error("ADD: Параметр DATE указан более одного раза.");
            if (currentIndex >= tokens.size()) throw std::runtime_error("ADD: Отсутствует значение для DATE.");
            std::istringstream date_ss(tokens[currentIndex]);
            if (!(date_ss >> params.dateData) || (date_ss.rdbuf() && date_ss.rdbuf()->in_avail() != 0)) {
                throw std::runtime_error("ADD: Некорректный формат даты: '" + tokens[currentIndex] + "'");
            }
            currentIndex++;
            date_set = true;
        } else if (keyword_upper == "TRAFFIC_IN") {
            if (traffic_in_set) throw std::runtime_error("ADD: Блок TRAFFIC_IN указан более одного раза.");
            // parseTrafficBlock ожидает, что currentIndex указывает на ПЕРВОЕ значение трафика
            parseTrafficBlock(tokens, params.trafficInData, currentIndex, "ADD", "TRAFFIC_IN");
            traffic_in_set = true;
            params.hasTrafficInToSet = true; // Хотя для ADD это подразумевается, если блок есть
            // currentIndex уже обновлен parseTrafficBlock
        } else if (keyword_upper == "TRAFFIC_OUT") {
            if (traffic_out_set) throw std::runtime_error("ADD: Блок TRAFFIC_OUT указан более одного раза.");
            parseTrafficBlock(tokens, params.trafficOutData, currentIndex, "ADD", "TRAFFIC_OUT");
            traffic_out_set = true;
            params.hasTrafficOutToSet = true;
            // currentIndex уже обновлен parseTrafficBlock
        } else {
            throw std::runtime_error("ADD: Неизвестное ключевое слово или параметр не на своем месте: '" + current_keyword_token + "'");
        }
    }
    // Проверки на обязательные поля для ADD можно добавить здесь, если они есть
    // Например, если FIO, IP, DATE всегда обязательны.
    // if (!fio_set || !ip_set || !date_set) {
    //     throw std::runtime_error("ADD: Отсутствуют обязательные параметры (FIO, IP или DATE).");
    // }
}

void QueryParser::parseCriteriaParams(const std::vector<std::string>& tokens, QueryParameters& params, size_t& currentIndex) const {
    while (currentIndex < tokens.size()) {
        std::string keyword_upper = toUpperQP(tokens[currentIndex]);

        // Ключевые слова, завершающие секцию критериев для разных команд
        if (keyword_upper == "END" || keyword_upper == "SET" || // Для EDIT
            keyword_upper == "START_DATE" || keyword_upper == "END_DATE") { // Для CALCULATE_CHARGES
            break;
        }

        std::string current_keyword_token = tokens[currentIndex];
        currentIndex++; // На значение

        if (currentIndex >= tokens.size()) {
            throw std::runtime_error("Отсутствует значение для критерия '" + current_keyword_token + "'.");
        }

        if (keyword_upper == "FIO") {
            if (params.useNameFilter) throw std::runtime_error("Критерий FIO указан более одного раза.");
            params.criteriaName = tokens[currentIndex++];
            params.useNameFilter = true;
        } else if (keyword_upper == "IP") {
            if (params.useIpFilter) throw std::runtime_error("Критерий IP указан более одного раза.");
            std::istringstream ip_ss(tokens[currentIndex]);
            if (!(ip_ss >> params.criteriaIpAddress) || (ip_ss.rdbuf() && ip_ss.rdbuf()->in_avail() != 0)) {
                throw std::runtime_error("Некорректный формат IP-адреса для критерия IP: '" + tokens[currentIndex] + "'");
            }
            currentIndex++;
            params.useIpFilter = true;
        } else if (keyword_upper == "DATE") {
            if (params.useDateFilter) throw std::runtime_error("Критерий DATE указан более одного раза.");
            std::istringstream date_ss(tokens[currentIndex]);
            if (!(date_ss >> params.criteriaDate) || (date_ss.rdbuf() && date_ss.rdbuf()->in_avail() != 0)) {
                throw std::runtime_error("Некорректный формат даты для критерия DATE: '" + tokens[currentIndex] + "'");
            }
            currentIndex++;
            params.useDateFilter = true;
        } else {
            // Если это не END и не SET и не START/END_DATE, то это неизвестное слово в критериях
            throw std::runtime_error("Неизвестное ключевое слово в критериях: '" + current_keyword_token + "'");
        }
    }
}


void QueryParser::parseEditSetParams(const std::vector<std::string>& tokens, QueryParameters& params, size_t& currentIndex) const {
    if (currentIndex >= tokens.size() || toUpperQP(tokens[currentIndex]) != "SET") {
        // Эта ошибка может быть избыточной, если критерии были, но SET отсутствует.
        // Вызывающий код parseQuery должен решить, является ли отсутствие SET ошибкой для EDIT.
        // Пока считаем, что если мы здесь, то SET должен быть.
        throw std::runtime_error("EDIT: Ожидалось ключевое слово SET после критериев.");
    }
    currentIndex++; // Пропускаем "SET"

    if (currentIndex >= tokens.size() || toUpperQP(tokens[currentIndex]) == "END") { // Пустой SET ... END
        Logger::debug("QueryParser::parseEditSetParams: Обнаружена пустая секция SET (SET END или SET в конце).");
        // Пустой SET может быть валидным (нет полей для изменения), либо ошибкой в зависимости от требований.
        // Ваши тесты (`test_query_parser.cpp`, `ParseEditEmptySetClauseThrowsError`) ожидают ошибку для `EDIT SET END` или `EDIT SET`.
        // Поэтому здесь бросим исключение, если после SET нет параметров или сразу END.
        throw std::runtime_error("EDIT: Секция SET пуста или отсутствует после ключевого слова SET.");
    }

    bool set_param_found_in_clause = false;
    while (currentIndex < tokens.size()) {
        std::string fieldToSet_upper = toUpperQP(tokens[currentIndex]);
        if (fieldToSet_upper == "END") {
            currentIndex++; // Пропускаем END
            break;
        }

        std::string current_field_token = tokens[currentIndex];
        currentIndex++; // На значение или на первый элемент трафика

        if (fieldToSet_upper == "FIO") {
            if (params.setData.count("FIO")) throw std::runtime_error("EDIT SET: Поле FIO указано для изменения более одного раза.");
            if (currentIndex >= tokens.size()) throw std::runtime_error("EDIT SET: Отсутствует значение для FIO.");
            params.setData["FIO"] = tokens[currentIndex++];
            set_param_found_in_clause = true;
        } else if (fieldToSet_upper == "IP") {
            if (params.setData.count("IP")) throw std::runtime_error("EDIT SET: Поле IP указано для изменения более одного раза.");
            if (currentIndex >= tokens.size()) throw std::runtime_error("EDIT SET: Отсутствует значение для IP.");
            // Валидация IP будет позже, здесь сохраняем как строку
            params.setData["IP"] = tokens[currentIndex++];
            set_param_found_in_clause = true;
        } else if (fieldToSet_upper == "DATE") {
            if (params.setData.count("DATE")) throw std::runtime_error("EDIT SET: Поле DATE указано для изменения более одного раза.");
            if (currentIndex >= tokens.size()) throw std::runtime_error("EDIT SET: Отсутствует значение для DATE.");
            params.setData["DATE"] = tokens[currentIndex++];
            set_param_found_in_clause = true;
        } else if (fieldToSet_upper == "TRAFFIC_IN") {
            if (params.hasTrafficInToSet) throw std::runtime_error("EDIT SET: Блок TRAFFIC_IN указан для изменения более одного раза.");
            parseTrafficBlock(tokens, params.trafficInData, currentIndex, "EDIT SET", "TRAFFIC_IN");
            params.hasTrafficInToSet = true;
            set_param_found_in_clause = true;
            // currentIndex уже обновлен
        } else if (fieldToSet_upper == "TRAFFIC_OUT") {
            if (params.hasTrafficOutToSet) throw std::runtime_error("EDIT SET: Блок TRAFFIC_OUT указан для изменения более одного раза.");
            parseTrafficBlock(tokens, params.trafficOutData, currentIndex, "EDIT SET", "TRAFFIC_OUT");
            params.hasTrafficOutToSet = true;
            set_param_found_in_clause = true;
            // currentIndex уже обновлен
        } else {
            throw std::runtime_error("EDIT SET: Неизвестное поле для изменения '" + current_field_token + "'.");
        }
    }

    if (!set_param_found_in_clause) {
        // Эта проверка срабатывает, если цикл while не нашел ни одного ВАЛИДНОГО параметра SET.
        // Если после SET сразу шел END, мы уже выбросили исключение выше.
        // Если после SET было что-то, но это не валидное поле, то исключение будет в 'else' выше.
        // Эта ветка может быть достигнута, если после SET было что-то, что не является END,
        // но и не является валидным полем (например, SET UNKNOWN_TOKEN END).
        // Но это уже должно было быть поймано.
        // Сохраним на всякий случай, но возможно, она избыточна с учетом предыдущей проверки на пустой SET.
         throw std::runtime_error("EDIT: Секция SET не содержит корректных полей для изменения.");
    }
}


void QueryParser::parseCalculateChargesParams(const std::vector<std::string>& tokens, QueryParameters& params, size_t& currentIndex) const {
    // Сначала разбираем необязательные критерии FIO, IP, DATE самой записи
    parseCriteriaParams(tokens, params, currentIndex); // currentIndex будет указывать на токен после критериев

    bool startDateFound = false;
    bool endDateFound = false;

    while(currentIndex < tokens.size()){
        std::string keyword_upper = toUpperQP(tokens[currentIndex]);
        if (keyword_upper == "END") {
            currentIndex++; // Пропускаем END
            break;
        }

        std::string current_keyword_token = tokens[currentIndex];
        currentIndex++; // На значение

        if (currentIndex >= tokens.size()) {
             throw std::runtime_error("CALCULATE_CHARGES: Отсутствует значение для параметра '" + current_keyword_token + "'.");
        }

        if(keyword_upper == "START_DATE"){
            if (startDateFound) throw std::runtime_error("CALCULATE_CHARGES: Параметр START_DATE указан более одного раза.");
            std::istringstream date_ss(tokens[currentIndex]);
            if (!(date_ss >> params.criteriaStartDate) || (date_ss.rdbuf() && date_ss.rdbuf()->in_avail() != 0)) {
                throw std::runtime_error("CALCULATE_CHARGES: Некорректный формат даты для START_DATE: '" + tokens[currentIndex] + "'");
            }
            currentIndex++;
            params.useStartDateFilter = true;
            startDateFound = true;
        } else if (keyword_upper == "END_DATE"){
            if (endDateFound) throw std::runtime_error("CALCULATE_CHARGES: Параметр END_DATE указан более одного раза.");
            std::istringstream date_ss(tokens[currentIndex]);
            if (!(date_ss >> params.criteriaEndDate) || (date_ss.rdbuf() && date_ss.rdbuf()->in_avail() != 0)) {
                throw std::runtime_error("CALCULATE_CHARGES: Некорректный формат даты для END_DATE: '" + tokens[currentIndex] + "'");
            }
            currentIndex++;
            params.useEndDateFilter = true;
            endDateFound = true;
        } else {
            throw std::runtime_error("CALCULATE_CHARGES: Неожиданное ключевое слово '" + current_keyword_token +
                                     "'. Ожидались START_DATE, END_DATE или END после критериев.");
        }
    }

    if (!startDateFound || !endDateFound) {
        throw std::runtime_error("CALCULATE_CHARGES: Запрос требует обязательного наличия START_DATE и END_DATE.");
    }
}


Query QueryParser::parseQuery(const std::string& queryString) const {
    Logger::debug("QueryParser: Начало разбора запроса: \"" + queryString + "\"");
    Query resultQuery;
    resultQuery.originalQueryString = queryString; // Сохраняем оригинальную строку
    resultQuery.params.reset();

    std::vector<std::string> tokens;
    try {
        tokens = tokenize(queryString);
    } catch (const std::runtime_error& e) {
        Logger::error("QueryParser: Ошибка токенизации: " + std::string(e.what()));
        throw; // Перебрасываем ошибку токенизации
    }

    if (tokens.empty()) {
        Logger::info("QueryParser: Получен пустой запрос.");
        resultQuery.type = QueryType::UNKNOWN; // Или можно определить тип EMPTY_QUERY
        return resultQuery;
    }

    std::string command_upper = toUpperQP(tokens[0]);
    size_t currentIndex = 1; // Начинаем разбор параметров со следующего токена

    try {
        if (command_upper == "ADD") {
            resultQuery.type = QueryType::ADD;
            parseAddParams(tokens, resultQuery.params, currentIndex);
        } else if (command_upper == "SELECT") {
            resultQuery.type = QueryType::SELECT;
            parseCriteriaParams(tokens, resultQuery.params, currentIndex);
        } else if (command_upper == "DELETE") {
            resultQuery.type = QueryType::DELETE;
            parseCriteriaParams(tokens, resultQuery.params, currentIndex);
        } else if (command_upper == "EDIT") {
            resultQuery.type = QueryType::EDIT;
            parseCriteriaParams(tokens, resultQuery.params, currentIndex); // Критерии
            // После критериев должен идти SET
            if (currentIndex < tokens.size() && toUpperQP(tokens[currentIndex]) == "SET") {
                parseEditSetParams(tokens, resultQuery.params, currentIndex); // SET-часть
            } else {
                 // Если SET отсутствует, но критерии были, это ошибка для EDIT с изменением данных
                 // Ваши тесты показывают, что "EDIT FIO ..." без SET - это ошибка.
                throw std::runtime_error("EDIT: Отсутствует ключевое слово SET после критериев.");
            }
        } else if (command_upper == "CALCULATE_CHARGES") {
            resultQuery.type = QueryType::CALCULATE_CHARGES;
            parseCalculateChargesParams(tokens, resultQuery.params, currentIndex);
        } else if (command_upper == "PRINT_ALL") {
            resultQuery.type = QueryType::PRINT_ALL;
            // Параметров нет, кроме опционального END
        } else if (command_upper == "LOAD") {
            resultQuery.type = QueryType::LOAD;
            if (currentIndex < tokens.size() && toUpperQP(tokens[currentIndex]) != "END") {
                resultQuery.params.filename = tokens[currentIndex++];
            } else {
                throw std::runtime_error("LOAD: Запрос требует указания имени файла.");
            }
        } else if (command_upper == "SAVE") {
            resultQuery.type = QueryType::SAVE;
            if (currentIndex < tokens.size() && toUpperQP(tokens[currentIndex]) != "END") {
                resultQuery.params.filename = tokens[currentIndex++];
            }
            // Имя файла для SAVE опционально
        } else if (command_upper == "EXIT") {
            resultQuery.type = QueryType::EXIT;
        } else if (command_upper == "HELP") {
            resultQuery.type = QueryType::HELP;
        } else {
            Logger::warn("QueryParser: Неизвестная команда: '" + tokens[0] + "'");
            throw std::runtime_error("Неизвестная команда: '" + tokens[0] + "'");
        }

        // Проверка на оставшиеся токены после основной части команды и ее параметров
        if (currentIndex < tokens.size()) {
            if (toUpperQP(tokens[currentIndex]) == "END") {
                currentIndex++;
                if (currentIndex < tokens.size()) {
                    std::string error_msg = "Неожиданные токены после ключевого слова END, начиная с: '" + tokens[currentIndex] + "'";
                    Logger::error("QueryParser: " + error_msg);
                    throw std::runtime_error(error_msg);
                }
            } else {
                std::string error_msg = "Неожиданные завершающие токены в запросе, начиная с: '" + tokens[currentIndex] + "'";
                Logger::error("QueryParser: " + error_msg);
                throw std::runtime_error(error_msg);
            }
        }
    } catch (const std::runtime_error& e) {
        Logger::error("QueryParser: Ошибка при разборе команды '" + tokens[0] + "': " + e.what());
        throw; // Перебрасываем ошибку парсинга конкретной команды
    }
    Logger::debug("QueryParser: Запрос успешно разобран. Тип: " + std::to_string(static_cast<int>(resultQuery.type)));
    return resultQuery;
}
