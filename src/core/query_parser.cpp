/*!
 * \file query_parser.cpp
 * \author Fedor Zilnitskiy
 * \brief Реализация класса QueryParser для разбора строковых запросов к базе данных.
 */
#include "query_parser.h"
#include "logger.h"     // Для логирования процесса парсинга и ошибок
#include <algorithm>    // Для std::transform (toUpperQP)
#include <stdexcept>    // Для std::runtime_error, std::invalid_argument, std::stod
#include <cctype>       // Для std::toupper, std::isspace

/*!
 * \brief Вспомогательная функция для преобразования строки в верхний регистр.
 * Используется для регистронезависимого сравнения ключевых слов команд и параметров.
 * \param s Исходная строка.
 * \return Строка в верхнем регистре.
 */
static std::string toUpperQP(std::string s) { // QP - Query Parser, чтобы избежать конфликтов имен
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return static_cast<char>(std::toupper(c)); });
    return s;
}

/*!
 * \brief Токенизация строки запроса.
 * \param queryString Строка запроса.
 * \return Вектор токенов.
 * \throw std::runtime_error Если незакрытая кавычка.
 */
std::vector<std::string> QueryParser::tokenize(const std::string& queryString) const {
    std::vector<std::string> tokens;
    std::string current_token_buffer;
    bool in_quote = false;
    char prev_char = 0; // Для обработки экранированных кавычек, если потребуется (пока не используется)

    for (char ch : queryString) {
        if (in_quote) {
            if (ch == '"') { 
                // Если бы поддерживались экранированные кавычки, здесь была бы проверка на prev_char == '\\'
                in_quote = false;
                // Содержимое кавычек - это один токен, даже если пустое (e.g., FIO "")
                tokens.push_back(current_token_buffer); 
                current_token_buffer.clear();
            } else {
                current_token_buffer += ch;
            }
        } else { // Вне кавычек
            if (ch == '"') {
                in_quote = true;
                // Если перед открывающей кавычкой был какой-то текст без пробелов, это отдельный токен
                if (!current_token_buffer.empty()) { 
                    tokens.push_back(current_token_buffer);
                    current_token_buffer.clear();
                }
                // Сама кавычка не является частью токена, она лишь меняет состояние парсера
            } else if (std::isspace(static_cast<unsigned char>(ch))) {
                if (!current_token_buffer.empty()) { // Если что-то накоплено в буфере, это токен
                    tokens.push_back(current_token_buffer);
                    current_token_buffer.clear();
                }
                // Пробельные символы между токенами игнорируются
            } else { // Любой другой символ добавляется к текущему токену
                current_token_buffer += ch;
            }
        }
        prev_char = ch;
    }

    if (in_quote) { // Если цикл завершился, а мы все еще внутри кавычки
        std::string error_msg = "Ошибка токенизации: незакрытая двойная кавычка в строке запроса. Начало содержимого в кавычках: \"" + current_token_buffer;
        Logger::error("QueryParser::tokenize: " + error_msg);
        throw std::runtime_error(error_msg);
    }
    if (!current_token_buffer.empty()) { // Добавляем последний токен, если он остался после цикла
        tokens.push_back(current_token_buffer);
    }
    return tokens;
}

/*!
 * \brief Парсинг блока значений трафика.
 * \param tokens Вектор токенов.
 * \param traffic_vector Выходной вектор для значений трафика.
 * \param currentIndex Текущий индекс в `tokens`.
 * \param commandName Имя команды для логов.
 * \param traffic_type_name Тип трафика ("TRAFFIC_IN" или "TRAFFIC_OUT") для логов.
 * \throw std::runtime_error При ошибках формата или количества.
 */
void QueryParser::parseTrafficBlock(const std::vector<std::string>& tokens,
                                   std::vector<double>& traffic_vector, 
                                   size_t& currentIndex,               
                                   const std::string& commandName,      
                                   const std::string& traffic_type_name) const {
    traffic_vector.assign(HOURS_IN_DAY, 0.0); // Инициализируем нулями, будем перезаписывать

    // currentIndex уже указывает на ПЕРВОЕ значение трафика (токен ПОСЛЕ TRAFFIC_IN/OUT)
    for (int i = 0; i < HOURS_IN_DAY; ++i) {
        if (currentIndex >= tokens.size()) { // Недостаточно токенов
            std::string error_msg = commandName + " " + traffic_type_name + ": Недостаточно значений трафика. Ожидалось " +
                                     std::to_string(HOURS_IN_DAY) + ", найдено только " + std::to_string(i) + ".";
            Logger::error("QueryParser::parseTrafficBlock: " + error_msg);
            throw std::runtime_error(error_msg);
        }

        const std::string& token = tokens[currentIndex];
        // Проверка, не является ли текущий токен ключевым словом следующего параметра/команды
        // Это предотвращает "поедание" ключевых слов как значений трафика.
        std::string upper_token_check = toUpperQP(token);
        if (upper_token_check == "END" || upper_token_check == "FIO" || upper_token_check == "IP" || upper_token_check == "DATE" ||
            upper_token_check == "TRAFFIC_IN" || upper_token_check == "TRAFFIC_OUT" || // Если это следующая секция трафика
            upper_token_check == "SET" || upper_token_check == "START_DATE" || upper_token_check == "END_DATE") {
            std::string error_msg = commandName + " " + traffic_type_name + ": Недостаточно значений трафика. Ожидалось " +
                                     std::to_string(HOURS_IN_DAY) + ", найдено " + std::to_string(i) +
                                     " перед ключевым словом '" + token + "'.";
            Logger::error("QueryParser::parseTrafficBlock: " + error_msg);
            throw std::runtime_error(error_msg);
        }

        try {
            size_t processed_chars = 0;
            double val = std::stod(token, &processed_chars); // std::stod может выбросить invalid_argument или out_of_range
            
            // Проверяем, что вся строка токена была числом
            if (processed_chars != token.length()) { 
                 throw std::invalid_argument("Лишние символы '" + token.substr(processed_chars) + "' в числовом значении трафика: '" + token + "'");
            }
            if (val < -DOUBLE_EPSILON) { // Сравнение с небольшим отрицательным эпсилон
                throw std::invalid_argument("Значение трафика не может быть отрицательным: " + token + " (распарсено как " + std::to_string(val) + ")");
            }
            traffic_vector[static_cast<size_t>(i)] = (val < 0.0 && val > -DOUBLE_EPSILON) ? 0.0 : val; // Приводим очень маленькие отрицательные к 0
        } catch (const std::invalid_argument& e_stod) { // От std::stod (не число) или наша проверка
            std::string error_msg = commandName + " " + traffic_type_name + ": Некорректное числовое значение для часа " +
                                     std::to_string(i) + " (токен: '" + token + "'). Ошибка: " + e_stod.what();
            Logger::error("QueryParser::parseTrafficBlock: " + error_msg);
            throw std::runtime_error(error_msg);
        } catch (const std::out_of_range& e_oor) { // От std::stod (слишком большое/маленькое число)
            std::string error_msg = commandName + " " + traffic_type_name + ": Значение трафика для часа " +
                                     std::to_string(i) + " (токен: '" + token + "') выходит за пределы допустимого диапазона double. Ошибка: " + e_oor.what();
            Logger::error("QueryParser::parseTrafficBlock: " + error_msg);
            throw std::runtime_error(error_msg);
        }
        currentIndex++; // Переходим к следующему токену для следующего значения трафика
    }
}

/*!
 * \brief Парсинг параметров команды ADD.
 * \param tokens Вектор токенов.
 * \param params Выходная структура параметров.
 * \param currentIndex Текущий индекс в `tokens`.
 * \throw std::runtime_error При ошибках формата.
 */
void QueryParser::parseAddParams(const std::vector<std::string>& tokens, QueryParameters& params, size_t& currentIndex) const {
    bool fio_set = false, ip_set = false, date_set = false; 
    // Флаги hasTrafficInToSet/hasTrafficOutToSet для ADD устанавливаются, если блоки есть
    // params.hasTrafficInToSet и params.hasTrafficOutToSet будут установлены в parseTrafficBlock, если он вызовется.
    // Но здесь они используются для EDIT, а для ADD они не так важны, т.к. трафик либо есть, либо будет нулевым по умолчанию.

    while (currentIndex < tokens.size()) {
        std::string keyword_upper = toUpperQP(tokens[currentIndex]);
        if (keyword_upper == "END") { // Опциональный END
            currentIndex++; 
            break; 
        }

        std::string current_keyword_token = tokens[currentIndex];
        currentIndex++; // Переходим к значению или к следующему ключевому слову

        if (keyword_upper == "FIO") {
            if (fio_set) throw std::runtime_error("ADD: Параметр FIO указан более одного раза.");
            if (currentIndex >= tokens.size()) throw std::runtime_error("ADD: Отсутствует значение для FIO после ключевого слова.");
            params.subscriberNameData = tokens[currentIndex++];
            fio_set = true;
        } else if (keyword_upper == "IP") {
            if (ip_set) throw std::runtime_error("ADD: Параметр IP указан более одного раза.");
            if (currentIndex >= tokens.size()) throw std::runtime_error("ADD: Отсутствует значение для IP после ключевого слова.");
            std::istringstream ip_ss(tokens[currentIndex]);
            if (!(ip_ss >> params.ipAddressData) || (ip_ss >> std::ws && !ip_ss.eof()) ) { // Проверка на лишние символы
                throw std::runtime_error("ADD: Некорректный формат IP-адреса: '" + tokens[currentIndex] + "'");
            }
            currentIndex++;
            ip_set = true;
        } else if (keyword_upper == "DATE") {
            if (date_set) throw std::runtime_error("ADD: Параметр DATE указан более одного раза.");
            if (currentIndex >= tokens.size()) throw std::runtime_error("ADD: Отсутствует значение для DATE после ключевого слова.");
            std::istringstream date_ss(tokens[currentIndex]);
            if (!(date_ss >> params.dateData) || (date_ss >> std::ws && !date_ss.eof())) {
                throw std::runtime_error("ADD: Некорректный формат даты: '" + tokens[currentIndex] + "'");
            }
            currentIndex++;
            date_set = true;
        } else if (keyword_upper == "TRAFFIC_IN") {
            if (params.hasTrafficInToSet) throw std::runtime_error("ADD: Блок TRAFFIC_IN указан более одного раза.");
            parseTrafficBlock(tokens, params.trafficInData, currentIndex, "ADD", "TRAFFIC_IN");
            params.hasTrafficInToSet = true; // Устанавливаем флаг, что блок был
        } else if (keyword_upper == "TRAFFIC_OUT") {
            if (params.hasTrafficOutToSet) throw std::runtime_error("ADD: Блок TRAFFIC_OUT указан более одного раза.");
            parseTrafficBlock(tokens, params.trafficOutData, currentIndex, "ADD", "TRAFFIC_OUT");
            params.hasTrafficOutToSet = true;
        } else {
            throw std::runtime_error("ADD: Неизвестное ключевое слово '" + current_keyword_token + "' или параметр не на своем месте.");
        }
    }
    // Для команды ADD, если ФИО, IP или DATE не были установлены, это ошибка, т.к. они обязательны для новой записи.
    if (!fio_set) throw std::runtime_error("ADD: Отсутствует обязательный параметр FIO.");
    if (!ip_set) throw std::runtime_error("ADD: Отсутствует обязательный параметр IP.");
    if (!date_set) throw std::runtime_error("ADD: Отсутствует обязательный параметр DATE.");
    
    // Если TRAFFIC_IN или TRAFFIC_OUT не были предоставлены, они останутся пустыми в params.
    // ServerCommandHandler::handleAdd затем заполнит их нулями.
}

/*!
 * \brief Парсинг критериев для SELECT, DELETE, EDIT.
 * \param tokens Вектор токенов.
 * \param params Выходная структура параметров.
 * \param currentIndex Текущий индекс в `tokens`.
 * \throw std::runtime_error При ошибках формата.
 */
void QueryParser::parseCriteriaParams(const std::vector<std::string>& tokens, QueryParameters& params, size_t& currentIndex) const {
    while (currentIndex < tokens.size()) {
        std::string keyword_upper = toUpperQP(tokens[currentIndex]);

        // Ключевые слова, завершающие секцию критериев для разных команд
        if (keyword_upper == "END" || keyword_upper == "SET" || // Для EDIT
            keyword_upper == "START_DATE" || keyword_upper == "END_DATE") { // Для CALCULATE_CHARGES
            break; // Завершаем разбор критериев
        }

        std::string current_keyword_token = tokens[currentIndex];
        currentIndex++; // Переходим к значению

        if (currentIndex >= tokens.size()) { // После ключевого слова критерия должно быть значение
            throw std::runtime_error("Отсутствует значение для критерия '" + current_keyword_token + "'.");
        }

        if (keyword_upper == "FIO") {
            if (params.useNameFilter) throw std::runtime_error("Критерий FIO указан более одного раза.");
            params.criteriaName = tokens[currentIndex++];
            params.useNameFilter = true;
        } else if (keyword_upper == "IP") {
            if (params.useIpFilter) throw std::runtime_error("Критерий IP указан более одного раза.");
            std::istringstream ip_ss(tokens[currentIndex]);
            if (!(ip_ss >> params.criteriaIpAddress) || (ip_ss >> std::ws && !ip_ss.eof())) {
                throw std::runtime_error("Некорректный формат IP-адреса для критерия IP: '" + tokens[currentIndex] + "'");
            }
            currentIndex++;
            params.useIpFilter = true;
        } else if (keyword_upper == "DATE") {
            if (params.useDateFilter) throw std::runtime_error("Критерий DATE указан более одного раза.");
            std::istringstream date_ss(tokens[currentIndex]);
            if (!(date_ss >> params.criteriaDate) || (date_ss >> std::ws && !date_ss.eof())) {
                throw std::runtime_error("Некорректный формат даты для критерия DATE: '" + tokens[currentIndex] + "'");
            }
            currentIndex++;
            params.useDateFilter = true;
        } else {
            // Если это не END и не другие терминальные слова, то это неизвестное слово в критериях
            throw std::runtime_error("Неизвестное ключевое слово в критериях: '" + current_keyword_token + "' или критерий не на своем месте.");
        }
    }
}

/*!
 * \brief Парсинг секции SET команды EDIT.
 * \param tokens Вектор токенов.
 * \param params Выходная структура параметров.
 * \param currentIndex Текущий индекс в `tokens`.
 * \throw std::runtime_error При ошибках формата.
 */
void QueryParser::parseEditSetParams(const std::vector<std::string>& tokens, QueryParameters& params, size_t& currentIndex) const {
    // currentIndex должен указывать на токен SET при вызове
    if (currentIndex >= tokens.size() || toUpperQP(tokens[currentIndex]) != "SET") {
        throw std::runtime_error("EDIT: Ожидалось ключевое слово SET после критериев (или в начале, если критериев нет).");
    }
    currentIndex++; // Пропускаем "SET"

    if (currentIndex >= tokens.size() || toUpperQP(tokens[currentIndex]) == "END") { // Пустой SET ... END или SET в конце строки
        throw std::runtime_error("EDIT: Секция SET не может быть пустой (должна содержать хотя бы одно поле для изменения).");
    }

    bool set_param_found_in_clause = false; // Флаг, что хотя бы одно поле было в SET
    while (currentIndex < tokens.size()) {
        std::string fieldToSet_upper = toUpperQP(tokens[currentIndex]);
        if (fieldToSet_upper == "END") { // Опциональный END
            currentIndex++; 
            break;
        }

        std::string current_field_token = tokens[currentIndex];
        currentIndex++; // Переходим к значению или к первому элементу трафика

        if (fieldToSet_upper == "FIO") {
            if (params.setData.count("FIO")) throw std::runtime_error("EDIT SET: Поле FIO указано для изменения более одного раза.");
            if (currentIndex >= tokens.size()) throw std::runtime_error("EDIT SET: Отсутствует значение для FIO после ключевого слова.");
            params.setData["FIO"] = tokens[currentIndex++];
            set_param_found_in_clause = true;
        } else if (fieldToSet_upper == "IP") {
            if (params.setData.count("IP")) throw std::runtime_error("EDIT SET: Поле IP указано для изменения более одного раза.");
            if (currentIndex >= tokens.size()) throw std::runtime_error("EDIT SET: Отсутствует значение для IP после ключевого слова.");
            // Валидация формата IP будет позже, в ServerCommandHandler, здесь сохраняем как строку
            params.setData["IP"] = tokens[currentIndex++];
            set_param_found_in_clause = true;
        } else if (fieldToSet_upper == "DATE") {
            if (params.setData.count("DATE")) throw std::runtime_error("EDIT SET: Поле DATE указано для изменения более одного раза.");
            if (currentIndex >= tokens.size()) throw std::runtime_error("EDIT SET: Отсутствует значение для DATE после ключевого слова.");
            params.setData["DATE"] = tokens[currentIndex++];
            set_param_found_in_clause = true;
        } else if (fieldToSet_upper == "TRAFFIC_IN") {
            if (params.hasTrafficInToSet) throw std::runtime_error("EDIT SET: Блок TRAFFIC_IN указан для изменения более одного раза.");
            parseTrafficBlock(tokens, params.trafficInData, currentIndex, "EDIT SET", "TRAFFIC_IN");
            params.hasTrafficInToSet = true;
            set_param_found_in_clause = true;
        } else if (fieldToSet_upper == "TRAFFIC_OUT") {
            if (params.hasTrafficOutToSet) throw std::runtime_error("EDIT SET: Блок TRAFFIC_OUT указан для изменения более одного раза.");
            parseTrafficBlock(tokens, params.trafficOutData, currentIndex, "EDIT SET", "TRAFFIC_OUT");
            params.hasTrafficOutToSet = true;
            set_param_found_in_clause = true;
        } else {
            throw std::runtime_error("EDIT SET: Неизвестное поле для изменения '" + current_field_token + "' или параметр не на своем месте.");
        }
    }

    if (!set_param_found_in_clause) {
        // Эта ошибка должна была быть поймана ранее проверкой на пустой SET
        throw std::runtime_error("EDIT: Секция SET не содержит корректных полей для изменения (внутренняя ошибка парсера).");
    }
}

/*!
 * \brief Парсинг параметров команды CALCULATE_CHARGES.
 * \param tokens Вектор токенов.
 * \param params Выходная структура параметров.
 * \param currentIndex Текущий индекс в `tokens`.
 * \throw std::runtime_error При ошибках формата.
 */
void QueryParser::parseCalculateChargesParams(const std::vector<std::string>& tokens, QueryParameters& params, size_t& currentIndex) const {
    // Сначала разбираем опциональные критерии фильтрации записей (FIO, IP, DATE самой записи)
    parseCriteriaParams(tokens, params, currentIndex); // currentIndex будет указывать на токен после критериев

    bool startDateFound = false;
    bool endDateFound = false;

    while(currentIndex < tokens.size()){
        std::string keyword_upper = toUpperQP(tokens[currentIndex]);
        if (keyword_upper == "END") { // Опциональный END
            currentIndex++; 
            break;
        }

        std::string current_keyword_token = tokens[currentIndex];
        currentIndex++; // Переходим к значению

        if (currentIndex >= tokens.size()) { // После ключевого слова (START_DATE/END_DATE) должно быть значение
             throw std::runtime_error("CALCULATE_CHARGES: Отсутствует значение для параметра '" + current_keyword_token + "'.");
        }

        if(keyword_upper == "START_DATE"){
            if (startDateFound) throw std::runtime_error("CALCULATE_CHARGES: Параметр START_DATE указан более одного раза.");
            std::istringstream date_ss(tokens[currentIndex]);
            if (!(date_ss >> params.criteriaStartDate) || (date_ss >> std::ws && !date_ss.eof())) {
                throw std::runtime_error("CALCULATE_CHARGES: Некорректный формат даты для START_DATE: '" + tokens[currentIndex] + "'");
            }
            currentIndex++;
            params.useStartDateFilter = true; // Устанавливаем флаг
            startDateFound = true;
        } else if (keyword_upper == "END_DATE"){
            if (endDateFound) throw std::runtime_error("CALCULATE_CHARGES: Параметр END_DATE указан более одного раза.");
            std::istringstream date_ss(tokens[currentIndex]);
            if (!(date_ss >> params.criteriaEndDate) || (date_ss >> std::ws && !date_ss.eof())) {
                throw std::runtime_error("CALCULATE_CHARGES: Некорректный формат даты для END_DATE: '" + tokens[currentIndex] + "'");
            }
            currentIndex++;
            params.useEndDateFilter = true; // Устанавливаем флаг
            endDateFound = true;
        } else {
            throw std::runtime_error("CALCULATE_CHARGES: Неожиданное ключевое слово '" + current_keyword_token +
                                     "'. Ожидались START_DATE, END_DATE или END после критериев фильтрации записей.");
        }
    }

    // START_DATE и END_DATE обязательны для CALCULATE_CHARGES
    if (!startDateFound) {
        throw std::runtime_error("CALCULATE_CHARGES: Запрос требует обязательного наличия параметра START_DATE.");
    }
    if (!endDateFound) {
        throw std::runtime_error("CALCULATE_CHARGES: Запрос требует обязательного наличия параметра END_DATE.");
    }
}

/*!
 * \brief Главный метод парсинга строки запроса.
 * \param queryString Строка запроса.
 * \return Объект Query.
 * \throw std::runtime_error При ошибках парсинга.
 */
Query QueryParser::parseQuery(const std::string& queryString) const {
    Logger::debug("QueryParser: Начало разбора запроса: \"" + queryString + "\"");
    Query resultQuery;
    resultQuery.originalQueryString = queryString; 
    resultQuery.params.reset(); // Сбрасываем параметры перед новым разбором

    std::vector<std::string> tokens;
    try {
        tokens = tokenize(queryString);
    } catch (const std::runtime_error& e_tokenize) {
        // Ошибка токенизации уже залогирована в tokenize
        throw; // Перебрасываем ошибку токенизации (например, незакрытая кавычка)
    }

    if (tokens.empty()) {
        Logger::info("QueryParser: Получен пустой запрос (или запрос только из пробелов).");
        resultQuery.type = QueryType::UNKNOWN; // Пустой запрос считаем неизвестным
        return resultQuery;
    }

    std::string command_upper = toUpperQP(tokens[0]);
    size_t currentIndex = 1; // Начинаем разбор параметров со следующего токена (индекс 1)

    try {
        if (command_upper == "ADD") {
            resultQuery.type = QueryType::ADD;
            parseAddParams(tokens, resultQuery.params, currentIndex);
        } else if (command_upper == "SELECT") {
            resultQuery.type = QueryType::SELECT;
            parseCriteriaParams(tokens, resultQuery.params, currentIndex);
            // После критериев может быть опциональный END
        } else if (command_upper == "DELETE") {
            resultQuery.type = QueryType::DELETE;
            parseCriteriaParams(tokens, resultQuery.params, currentIndex);
        } else if (command_upper == "EDIT") {
            resultQuery.type = QueryType::EDIT;
            // Сначала критерии (могут отсутствовать, тогда currentIndex не изменится)
            parseCriteriaParams(tokens, resultQuery.params, currentIndex); 
            // Затем обязательно секция SET
            parseEditSetParams(tokens, resultQuery.params, currentIndex); 
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
                throw std::runtime_error("LOAD: Запрос требует указания имени файла после команды LOAD.");
            }
        } else if (command_upper == "SAVE") {
            resultQuery.type = QueryType::SAVE;
            // Имя файла для SAVE опционально (если не указано, используется текущее имя файла из БД)
            if (currentIndex < tokens.size() && toUpperQP(tokens[currentIndex]) != "END") {
                resultQuery.params.filename = tokens[currentIndex++];
            }
        } else if (command_upper == "EXIT") {
            resultQuery.type = QueryType::EXIT;
        } else if (command_upper == "HELP") {
            resultQuery.type = QueryType::HELP;
        } else {
            throw std::runtime_error("Неизвестная команда: '" + tokens[0] + "'.");
        }

        // Проверка на оставшиеся токены после основной части команды и ее параметров
        // Ожидаем либо конец токенов, либо ключевое слово END (если оно не было уже обработано)
        if (currentIndex < tokens.size()) {
            if (toUpperQP(tokens[currentIndex]) == "END") {
                currentIndex++; // Потребляем END
                if (currentIndex < tokens.size()) { // После END не должно быть ничего
                    std::string error_msg = "Неожиданные токены после ключевого слова END, начиная с: '" + tokens[currentIndex] + "'";
                    Logger::error("QueryParser: " + error_msg);
                    throw std::runtime_error(error_msg);
                }
            } else { // Если это не END, а какие-то другие токены
                std::string error_msg = "Неожиданные завершающие токены в запросе, начиная с: '" + tokens[currentIndex] + "'. Возможно, отсутствует END или команда завершена некорректно.";
                Logger::error("QueryParser: " + error_msg);
                throw std::runtime_error(error_msg);
            }
        }
    } catch (const std::runtime_error& e_parse_cmd) {
        // Ошибки из parseAddParams, parseCriteriaParams и т.д. уже залогированы там.
        // Здесь можно добавить общий лог ошибки парсинга, если нужно.
        Logger::error("QueryParser: Ошибка при разборе команды '" + tokens[0] + "': " + e_parse_cmd.what());
        throw; // Перебрасываем ошибку парсинга конкретной команды
    }
    Logger::debug("QueryParser: Запрос успешно разобран. Тип: " + std::to_string(static_cast<int>(resultQuery.type)));
    return resultQuery;
}
