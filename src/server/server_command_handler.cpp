/*!
 * \file server_command_handler.cpp
 * \author Fedor Zilnitskiy
 * \brief Реализация класса ServerCommandHandler для обработки команд сервера.
 */
#include "server_command_handler.h"
#include "logger.h"         // Для логирования операций и ошибок
#include "common_defs.h"    // Для HOURS_IN_DAY, DOUBLE_EPSILON, DEFAULT_SERVER_DATA_SUBDIR
#include "file_utils.h"     // Для getProjectRootPath (используется в getSafeServerFilePath_SCH как fallback)
#include "provider_record.h"// Для работы с записями
#include "ip_address.h"     // Для работы с IP-адресами
#include "date.h"           // Для работы с датами
#include <filesystem>       // Для работы с путями (std::filesystem)
#include <iomanip>          // Для std::fixed, std::setprecision
#include <algorithm>        // Для std::sort, std::unique (если понадобится локально)
#include <cstdio>           // Для ::remove, если потребуется удаление файла (пока не используется)

#ifdef _WIN32
#include <direct.h> // Для _getcwd
#else
#include <unistd.h> // Для getcwd
#endif


/*!
 * \brief Формирует безопасный абсолютный путь к файлу данных на сервере.
 *
 * Принимает базовый путь (например, корень проекта или указанную директорию данных сервера)
 * и имя файла, запрошенное клиентом. Гарантирует, что итоговый путь будет находиться
 * внутри предопределенной поддиректории (`default_data_subdir`, например, "server_databases")
 * относительно базового пути, предотвращая выход за пределы "песочницы".
 * Создает директорию `default_data_subdir`, если она не существует.
 *
 * \param root_for_data_ops_str Базовый путь для файловых операций. Если пуст, функция
 * попытается определить корень проекта относительно текущей рабочей директории.
 * \param requested_filename_from_client Имя файла, запрошенное клиентом (может содержать путь, но будет взято только имя файла).
 * \param default_data_subdir Имя поддиректории, в которой должны храниться файлы БД.
 * \return Абсолютный, канонизированный и безопасный путь `std::filesystem::path` к файлу.
 * \throw std::runtime_error если имя файла недопустимо, содержит запрещенные символы,
 * слишком длинное, или если происходит попытка доступа к файлу вне разрешенной директории,
 * или при ошибках файловой системы.
 */
std::filesystem::path getSafeServerFilePath_SCH(
    const std::string& root_for_data_ops_str,
    const std::string& requested_filename_from_client,
    const std::string& default_data_subdir = DEFAULT_SERVER_DATA_SUBDIR) 
{
    std::filesystem::path server_data_search_root;

    if (!root_for_data_ops_str.empty()) {
        try {
            server_data_search_root = std::filesystem::weakly_canonical(std::filesystem::absolute(root_for_data_ops_str));
             Logger::debug("getSafeServerFilePath_SCH: Используется предоставленный базовый путь для данных: '" + server_data_search_root.string() + "'");
        } catch (const std::filesystem::filesystem_error& e) {
            Logger::error("getSafeServerFilePath_SCH: Ошибка при обработке предоставленного базового пути '" + root_for_data_ops_str + "': " + e.what() + ". Попытка использовать CWD.");
            server_data_search_root = std::filesystem::current_path();
        }
    } else {
        // Если базовый путь не предоставлен, пытаемся определить корень проекта от CWD
        char current_path_cstr[1024] = {0}; // Буфер для текущего пути
        #ifdef _WIN32
            if (_getcwd(current_path_cstr, sizeof(current_path_cstr) -1) == nullptr) { // -1 для нуль-терминатора
                throw std::runtime_error("getSafeServerFilePath_SCH: Критическая ошибка: не удалось получить текущую рабочую директорию (_getcwd).");
            }
        #else
            if (getcwd(current_path_cstr, sizeof(current_path_cstr) -1) == nullptr) {
                throw std::runtime_error("getSafeServerFilePath_SCH: Критическая ошибка: не удалось получить текущую рабочую директорию (getcwd).");
            }
        #endif
        try {
            server_data_search_root = getProjectRootPath(current_path_cstr);
            Logger::warn("getSafeServerFilePath_SCH: Базовый путь для данных не был предоставлен. Определен корень проекта от CWD: '" + server_data_search_root.string() + "'");
        } catch (const std::exception& e_gprp) {
            Logger::error("getSafeServerFilePath_SCH: Ошибка при авто-определении корня проекта от CWD: " + std::string(e_gprp.what()) + ". Используется CWD ('" + current_path_cstr + "') как корень данных.");
            server_data_search_root = std::filesystem::path(current_path_cstr);
        }
    }

    // Директория для хранения файлов БД всегда находится в default_data_subdir относительно server_data_search_root
    std::filesystem::path data_storage_dir = server_data_search_root / default_data_subdir;

    // Создаем директорию для хранения БД, если она не существует
    if (!std::filesystem::exists(data_storage_dir)) {
        try {
            if (std::filesystem::create_directories(data_storage_dir)) {
                Logger::info("getSafeServerFilePath_SCH: Успешно создана директория для данных сервера: '" + data_storage_dir.string() + "'");
            } else {
                // Эта ситуация маловероятна, если нет проблем с правами доступа
                 Logger::warn("getSafeServerFilePath_SCH: create_directories вернул false для '" + data_storage_dir.string() + "', возможно, директория уже была создана другим потоком.");
            }
        } catch (const std::filesystem::filesystem_error& e_create_dir) {
            Logger::error("getSafeServerFilePath_SCH: Не удалось создать директорию данных '" + data_storage_dir.string() + "': " + e_create_dir.what());
            throw std::runtime_error("Критическая ошибка сервера: не удалось создать директорию для хранения баз данных: " + data_storage_dir.string());
        }
    } else if (!std::filesystem::is_directory(data_storage_dir)){
        Logger::error("getSafeServerFilePath_SCH: Путь для хранения данных '" + data_storage_dir.string() + "' существует, но не является директорией!");
        throw std::runtime_error("Критическая ошибка сервера: путь для хранения баз данных не является директорией.");
    }


    std::filesystem::path client_filename_obj(requested_filename_from_client);
    // Извлекаем только имя файла, игнорируя любой путь, который мог передать клиент
    std::string clean_filename = client_filename_obj.filename().string(); 

    // Проверки на недопустимое имя файла
    if (clean_filename.empty() || clean_filename == "." || clean_filename == "..") {
        throw std::runtime_error("Недопустимое имя файла от клиента: '" + requested_filename_from_client + "' (пустое или состоит из точек).");
    }
    // Проверка на запрещенные символы (можно расширить список для разных ФС)
    if (clean_filename.find_first_of("/\\:*?\"<>|") != std::string::npos) { 
        throw std::runtime_error("Имя файла '" + clean_filename + "' содержит недопустимые символы (/\\:*?\"<>|).");
    }
    const size_t MAX_FILENAME_LEN = 250; // Ограничение на длину имени файла (разумное)
    if (clean_filename.length() > MAX_FILENAME_LEN) {
        throw std::runtime_error("Имя файла слишком длинное (макс. " + std::to_string(MAX_FILENAME_LEN) + " символов): '" + clean_filename + "'");
    }
    // Запрет на скрытые файлы (начинающиеся с точки), если это нежелательно
    // if (clean_filename.rfind(".", 0) == 0) { // rfind(str, pos) - ищет str начиная с pos. Здесь ищем "." в позиции 0.
    //     throw std::runtime_error("Имена файлов, начинающиеся с точки (скрытые файлы), не разрешены: '" + clean_filename + "'");
    // }


    std::filesystem::path target_path = data_storage_dir / clean_filename;
    std::filesystem::path canonical_target_path;
    std::filesystem::path canonical_data_root;

    try {
        // weakly_canonical разрешает несуществующие компоненты пути, кроме последнего, если он не пуст.
        // Для target_path, clean_filename не пуст. data_storage_dir мы создали, он должен существовать.
        canonical_target_path = std::filesystem::weakly_canonical(target_path);
        canonical_data_root = std::filesystem::weakly_canonical(data_storage_dir); // Должна быть уже канонической и существующей
    } catch (const std::filesystem::filesystem_error& e_canon) {
        Logger::error("getSafeServerFilePath_SCH: Ошибка канонизации пути '" + target_path.string() + "' или '" + data_storage_dir.string() + "': " + e_canon.what());
        throw std::runtime_error("Ошибка сервера при обработке пути к файлу.");
    }
    
    // Проверка "песочницы": убеждаемся, что канонический путь к файлу находится ВНУТРИ канонического пути к директории данных.
    // Используем lexically_normal для удаления ".." и "." перед сравнением строк.
    std::string target_str_norm = canonical_target_path.lexically_normal().string();
    std::string root_str_norm = canonical_data_root.lexically_normal().string();

    // Проверяем, что target_str_norm начинается с root_str_norm и не является самим root_str_norm
    // (т.е. файл должен быть ВНУТРИ директории, а не самой директорией)
    // и что после root_str_norm идет разделитель пути.
    if (target_str_norm.rfind(root_str_norm, 0) != 0 || // Не начинается с корня
        target_str_norm.length() <= root_str_norm.length() || // Путь короче или равен корню (т.е. это сам корень или выше)
        (target_str_norm.length() > root_str_norm.length() && 
         target_str_norm[root_str_norm.length()] != std::filesystem::path::preferred_separator) // После корня должен быть разделитель
       )
    {
        Logger::error("getSafeServerFilePath_SCH: Попытка доступа к файлу вне разрешенной директории (нарушение песочницы)! "
                      "Запрошено клиентом: '" + requested_filename_from_client + "', "
                      "Целевой путь (нормализованный): '" + target_str_norm + "', "
                      "Ожидалось внутри: '" + root_str_norm + "'.");
        throw std::runtime_error("Доступ к файлу запрещен (нарушение безопасности сервера).");
    }

    Logger::debug("getSafeServerFilePath_SCH: Безопасный абсолютный путь к файлу на сервере: '" + canonical_target_path.string() + "'");
    return canonical_target_path;
}

/*!
 * \brief Конструктор ServerCommandHandler.
 * \param db Ссылка на базу данных.
 * \param plan Ссылка на тарифный план.
 * \param server_data_path_base Базовый путь для файловых операций на сервере.
 */
ServerCommandHandler::ServerCommandHandler(Database& db, TariffPlan& plan, const std::string& server_data_path_base)
    : db_(db), tariff_plan_(plan), server_data_base_path_(server_data_path_base) {
    Logger::info("ServerCommandHandler: Инициализирован. Базовый путь для данных сервера: '" + 
                  (server_data_base_path_.empty() ? "[Не указан, будет использовано автоопределение/CWD]" : server_data_base_path_) + "'");
}

/*!
 * \brief Обрабатывает входящий запрос.
 * \param query Разобранный запрос.
 * \return Строка ответа для клиента.
 */
std::string ServerCommandHandler::processCommand(const Query& query) {
    std::ostringstream response_oss; // Поток для формирования ответа клиенту
    // Блокировка мьютекса db_shared_mutex_ (unique_lock или shared_lock)
    // осуществляется в Server::clientHandlerTask перед вызовом этого метода.

    try {
        switch (query.type) {
            case QueryType::ADD:               handleAdd(query.params, response_oss); break;
            case QueryType::SELECT:            handleSelect(query.params, response_oss); break;
            case QueryType::DELETE:            handleDelete(query.params, response_oss); break;
            case QueryType::EDIT:              handleEdit(query.params, response_oss); break;
            case QueryType::CALCULATE_CHARGES: handleCalculateCharges(query.params, response_oss); break;
            case QueryType::PRINT_ALL:         handlePrintAll(response_oss); break;
            case QueryType::LOAD:              handleLoad(query.params, response_oss); break;
            case QueryType::SAVE:              handleSave(query.params, response_oss); break;
            case QueryType::HELP: // Команда HELP, если дошла до сервера
                response_oss << "Сервер поддерживает команды: ADD, SELECT, DELETE, EDIT, CALCULATE_CHARGES, PRINT_ALL, LOAD, SAVE, EXIT.\n"
                             << "Для детального синтаксиса используйте команду HELP на стороне клиента.\n";
                Logger::info("ServerCommandHandler: Обработан запрос HELP от клиента (ответ-заглушка).");
                break;
            case QueryType::EXIT: // Команда EXIT от клиента (не EXIT_CLIENT_SESSION)
                response_oss << "Сервер подтверждает команду EXIT. Сессия будет завершена клиентом после получения этого ответа.\n";
                Logger::info("ServerCommandHandler: Подтверждение команды EXIT для клиента. Клиент должен разорвать соединение.");
                // Фактическое завершение сессии (разрыв цикла в clientHandlerTask) произойдет после отправки этого ответа.
                break;
            case QueryType::UNKNOWN:
            default:
                response_oss << "Ошибка [Сервер]: Неизвестная или некорректно сформированная команда получена сервером.\n"
                             << "Оригинальный запрос: \"" << query.originalQueryString << "\"\n";
                Logger::warn("ServerCommandHandler: Получен неизвестный или некорректный тип запроса (тип: " 
                             + std::to_string(static_cast<int>(query.type)) + ", оригинал: '" + query.originalQueryString + "').");
                break;
        }
    } catch (const std::invalid_argument& iae) {
        response_oss.str(""); response_oss.clear(); // Очищаем поток перед записью новой ошибки
        response_oss << "Ошибка [Сервер]: Неверный аргумент в параметрах команды -> " << iae.what() << "\n"
                     << "Оригинальный запрос: \"" << query.originalQueryString << "\"\n";
        Logger::error("ServerCommandHandler: InvalidArgument при обработке '" + query.originalQueryString + "': " + iae.what());
    } catch (const std::out_of_range& oor) {
        response_oss.str(""); response_oss.clear();
        response_oss << "Ошибка [Сервер]: Запрошенный элемент (например, по индексу) вне допустимого диапазона -> " << oor.what() << "\n"
                     << "Оригинальный запрос: \"" << query.originalQueryString << "\"\n";
        Logger::error("ServerCommandHandler: OutOfRange при обработке '" + query.originalQueryString + "': " + oor.what());
    } catch (const std::runtime_error& rte) { // Общие ошибки времени выполнения (включая ошибки от getSafeServerFilePath_SCH)
        response_oss.str(""); response_oss.clear();
        response_oss << "Ошибка выполнения на сервере -> " << rte.what() << "\n"
                     << "Оригинальный запрос: \"" << query.originalQueryString << "\"\n";
        Logger::error("ServerCommandHandler: RuntimeError при обработке '" + query.originalQueryString + "': " + rte.what());
    } catch (const std::exception& e) { // Другие стандартные исключения
        response_oss.str(""); response_oss.clear();
        response_oss << "Непредвиденная системная ошибка на сервере -> " << e.what() << "\n"
                     << "Оригинальный запрос: \"" << query.originalQueryString << "\"\n";
        Logger::error("ServerCommandHandler: StdException при обработке '" + query.originalQueryString + "': " + e.what());
    } catch (...) { // Все остальные (нестандартные) исключения
        response_oss.str(""); response_oss.clear();
        response_oss << "Неизвестная критическая ошибка произошла на сервере при обработке вашего запроса.\n"
                     << "Оригинальный запрос: \"" << query.originalQueryString << "\"\n";
        Logger::error("ServerCommandHandler: Неизвестное исключение (...) при обработке '" + query.originalQueryString + "'.");
    }
    return response_oss.str();
}


// --- Реализации методов handle* ---

/*! \brief Обработка команды ADD. */
void ServerCommandHandler::handleAdd(const QueryParameters& params, std::ostringstream& oss) {
    std::vector<double> trafficInToAdd = params.trafficInData;
    std::vector<double> trafficOutToAdd = params.trafficOutData;

    // Если TRAFFIC_IN или TRAFFIC_OUT не были предоставлены в запросе ADD,
    // QueryParser оставит params.trafficInData/OutData пустыми, и hasTrafficIn/OutToSet будут false.
    // В этом случае ProviderRecord должен быть создан с нулевым трафиком.
    if (!params.hasTrafficInToSet) { // Если блок TRAFFIC_IN не был в запросе
        trafficInToAdd.assign(HOURS_IN_DAY, 0.0);
        Logger::debug("ADD: Блок TRAFFIC_IN не предоставлен, используется трафик по нулям.");
    }
    if (!params.hasTrafficOutToSet) { // Если блок TRAFFIC_OUT не был в запросе
        trafficOutToAdd.assign(HOURS_IN_DAY, 0.0);
        Logger::debug("ADD: Блок TRAFFIC_OUT не предоставлен, используется трафик по нулям.");
    }
    // Валидация на количество элементов и неотрицательность уже должна быть сделана в QueryParser::parseTrafficBlock
    // и затем в конструкторе/сеттерах ProviderRecord.

    // Конструктор ProviderRecord может выбросить std::invalid_argument, если что-то не так с данными
    // (например, если QueryParser пропустил невалидные данные трафика, хотя не должен).
    ProviderRecord newRecord(
        params.subscriberNameData, // Обязательно по логике QueryParser::parseAddParams
        params.ipAddressData,      // Обязательно
        params.dateData,           // Обязательно
        trafficInToAdd,
        trafficOutToAdd
    );
    db_.addRecord(newRecord); 
    oss << "Запись для абонента '" << params.subscriberNameData << "' успешно добавлена на сервере.\n";
    Logger::info("ADD: Запись для ФИО='" + params.subscriberNameData + "', IP=" + params.ipAddressData.toString() +
                 ", Дата=" + params.dateData.toString() + " успешно добавлена.");
}

/*! \brief Обработка команды SELECT. */
void ServerCommandHandler::handleSelect(const QueryParameters& params, std::ostringstream& oss) {
    // ... (реализация без изменений, но с учетом того, что getRecordByIndex может выбросить out_of_range,
    //      если запись была удалена другим потоком между find и get - это уже обрабатывается)
    std::string criteria_log = "FIO: " + (params.useNameFilter ? "'" + params.criteriaName + "'" : "any") +
                              ", IP: " + (params.useIpFilter ? params.criteriaIpAddress.toString() : "any") +
                              ", Date: " + (params.useDateFilter ? params.criteriaDate.toString() : "any");
    Logger::info("SELECT: Поиск по критериям: " + criteria_log);

    std::vector<size_t> indices = db_.findRecordsByCriteria(
        params.criteriaName, params.useNameFilter,
        params.criteriaIpAddress, params.useIpFilter,
        params.criteriaDate, params.useDateFilter
    ); 

    if (indices.empty()) {
        oss << "Записи, соответствующие критериям, не найдены на сервере.\n";
    } else {
        oss << "Найдено " << indices.size() << " записей, соответствующих критериям на сервере:\n";
        oss << "-----------------------------------------------------------------\n";
        for (size_t i = 0; i < indices.size(); ++i) {
            try {
                const ProviderRecord& rec = db_.getRecordByIndex(indices[i]); 
                oss << "Запись (Индекс в БД #" << indices[i] << "):\n"; // Используем номер из вектора indices
                oss << rec; 
                oss << "\n-----------------------------------------------------------------\n";
            } catch (const std::out_of_range& e) {
                oss << "Ошибка [Сервер]: Не удалось получить доступ к записи по индексу " << indices[i] 
                    << " (возможно, была удалена): " << e.what() << "\n";
                Logger::warn("SELECT: Ошибка доступа к записи " + std::to_string(indices[i]) + " при выводе результатов: " + e.what());
            }
        }
    }
    Logger::info("SELECT: Завершено. Найдено записей: " + std::to_string(indices.size()) + ".");
}

/*! \brief Обработка команды DELETE. */
void ServerCommandHandler::handleDelete(const QueryParameters& params, std::ostringstream& oss) {
    // ... (реализация без существенных изменений)
     std::string criteria_log = "FIO: " + (params.useNameFilter ? "'" + params.criteriaName + "'" : "any") +
                              ", IP: " + (params.useIpFilter ? params.criteriaIpAddress.toString() : "any") +
                              ", Date: " + (params.useDateFilter ? params.criteriaDate.toString() : "any");
    Logger::info("DELETE: Поиск записей для удаления по критериям: " + criteria_log);

    std::vector<size_t> indices_to_delete = db_.findRecordsByCriteria(
        params.criteriaName, params.useNameFilter,
        params.criteriaIpAddress, params.useIpFilter,
        params.criteriaDate, params.useDateFilter
    ); 

    if (indices_to_delete.empty()) {
        oss << "Не найдено записей, соответствующих критериям для удаления на сервере.\n";
        Logger::info("DELETE: Не найдено записей для удаления по указанным критериям.");
        return;
    }
    
    // Логика вывода информации об удаляемых записях (опционально, для отладки или если требуется подтверждение от клиента, что не предусмотрено)
    // oss << "Сервер подготовил к удалению следующие " << indices_to_delete.size() << " записей (индексы в БД): ";
    // for(size_t i=0; i < indices_to_delete.size(); ++i) { oss << indices_to_delete[i] << (i == indices_to_delete.size()-1 ? "" : ", ");}
    // oss << "\n";

    size_t num_deleted = db_.deleteRecordsByIndices(indices_to_delete); // indices_to_delete будет изменен (отсортирован) внутри
    oss << "Успешно удалено " << num_deleted << " записей сервером.\n";
    Logger::info("DELETE: Удалено " + std::to_string(num_deleted) + " записей.");
}

/*! \brief Обработка команды EDIT. */
void ServerCommandHandler::handleEdit(const QueryParameters& params, std::ostringstream& oss) {
    // ... (реализация с более строгой обработкой ошибок при парсинге значений из params.setData)
    std::string criteria_log = "FIO: " + (params.useNameFilter ? "'" + params.criteriaName + "'" : "any") +
                              ", IP: " + (params.useIpFilter ? params.criteriaIpAddress.toString() : "any") +
                              ", Date: " + (params.useDateFilter ? params.criteriaDate.toString() : "any");
    Logger::info("EDIT: Поиск записи для редактирования по критериям: " + criteria_log);

    std::vector<size_t> indices_to_edit = db_.findRecordsByCriteria(
        params.criteriaName, params.useNameFilter,
        params.criteriaIpAddress, params.useIpFilter,
        params.criteriaDate, params.useDateFilter
    );

    if (indices_to_edit.empty()) {
        // Вместо прямого вывода в oss, выбрасываем исключение, чтобы оно было поймано и отформатировано в processCommand
        throw std::runtime_error("Не найдено записей, соответствующих критериям для редактирования.");
    }
    // QueryParser уже должен был проверить, что SET не пуст, если команда EDIT
    if (params.setData.empty() && !params.hasTrafficInToSet && !params.hasTrafficOutToSet) {
        throw std::runtime_error("Отсутствует секция SET с указанием полей для изменения в команде EDIT.");
    }

    size_t target_db_index = indices_to_edit[0]; 
    if (indices_to_edit.size() > 1) {
        oss << "EDIT Предупреждение [Сервер]: Критерии соответствуют " << indices_to_edit.size()
            << " записям. Будет отредактирована только первая найденная запись (Индекс в БД: " << target_db_index << ").\n";
        Logger::warn("EDIT: Найдено " + std::to_string(indices_to_edit.size()) +
                     " записей для редактирования, будет обработана только первая с индексом " + std::to_string(target_db_index));
    }
    
    ProviderRecord record_to_edit = db_.getRecordByIndex(target_db_index); // Получаем копию
    bool changed_applied = false;
    std::ostringstream changes_log_details_ss; // Используем ostringstream для лога изменений
    changes_log_details_ss << "EDIT: Применяемые изменения для записи с индексом " << target_db_index << ": ";

    // Обработка полей из setData (FIO, IP, DATE)
    for (const auto& pair : params.setData) {
        const std::string& field_key_upper = toUpperSC(pair.first); // toUpperSC определен в server_config.cpp, можно вынести в common_utils
        const std::string& value_str = pair.second;
        changed_applied = true; // Предполагаем, что если поле есть в setData, оно должно быть изменено

        if (field_key_upper == "FIO") {
            record_to_edit.setName(value_str);
            changes_log_details_ss << "FIO->'" << value_str << "'; ";
        } else if (field_key_upper == "IP") {
            IPAddress new_ip; 
            std::istringstream ip_ss(value_str);
            if (!(ip_ss >> new_ip) || (ip_ss >> std::ws && !ip_ss.eof())) {
                throw std::invalid_argument("Некорректный формат IP-адреса '" + value_str + "' в секции SET.");
            }
            record_to_edit.setIpAddress(new_ip); 
            changes_log_details_ss << "IP->" + new_ip.toString() + "; "; 
        } else if (field_key_upper == "DATE") {
            Date new_date; 
            std::istringstream date_ss(value_str);
            if (!(date_ss >> new_date) || (date_ss >> std::ws && !date_ss.eof())) {
                 throw std::invalid_argument("Некорректный формат даты '" + value_str + "' в секции SET.");
            }
            record_to_edit.setDate(new_date); 
            changes_log_details_ss << "DATE->" + new_date.toString() + "; "; 
        } else {
             // Эта ситуация не должна возникать, если QueryParser отработал корректно
             Logger::warn("EDIT: Обнаружено неизвестное поле '" + pair.first + "' в params.setData, которое должно было быть отфильтровано QueryParser'ом.");
             changed_applied = false; // Не считаем это изменением
        }
    }

    // Обработка TRAFFIC_IN и TRAFFIC_OUT
    if (params.hasTrafficInToSet) {
        // QueryParser уже должен был провалидировать params.trafficInData на размер и неотрицательность
        record_to_edit.setTrafficInByHour(params.trafficInData); 
        changes_log_details_ss << "TRAFFIC_IN обновлен (количество элементов: " << params.trafficInData.size() << "); "; 
        changed_applied = true; 
    }
    if (params.hasTrafficOutToSet) {
        record_to_edit.setTrafficOutByHour(params.trafficOutData); 
        changes_log_details_ss << "TRAFFIC_OUT обновлен (количество элементов: " << params.trafficOutData.size() << "); "; 
        changed_applied = true; 
    }

    if (changed_applied) {
        db_.editRecord(target_db_index, record_to_edit); 
        oss << "Запись с Индексом в БД " << target_db_index << " успешно отредактирована сервером.\n";
        oss << "Новые данные:\n-----------------------------------------------------------------\n";
        // Выводим измененную запись из БД для подтверждения
        oss << "Запись (Индекс в БД: " << target_db_index << "):\n" << db_.getRecordByIndex(target_db_index); 
        oss << "\n-----------------------------------------------------------------\n";
        Logger::info(changes_log_details_ss.str());
    } else {
        // Это может случиться, если SET был, но не содержал валидных полей для изменения,
        // или если QueryParser не установил hasTrafficIn/OutToSet корректно.
        oss << "EDIT Информация [Сервер]: Не было применено никаких изменений к записи (возможно, поля в SET неверны или отсутствуют).\n";
        Logger::info("EDIT: Не было применено фактических изменений к записи " + std::to_string(target_db_index) + ".");
    }
}


/*! \brief Обработка команды CALCULATE_CHARGES. */
void ServerCommandHandler::handleCalculateCharges(const QueryParameters& params, std::ostringstream& oss) {
    // ... (реализация без существенных изменений, но с учетом более строгой обработки ошибок от calculateChargesForRecord)
     if (!params.useStartDateFilter || !params.useEndDateFilter) {
        throw std::runtime_error("Команда CALCULATE_CHARGES требует обязательного указания START_DATE и END_DATE.");
    }
    if (params.criteriaStartDate > params.criteriaEndDate) {
         throw std::runtime_error("START_DATE (" + params.criteriaStartDate.toString() + ") не может быть позже END_DATE (" + params.criteriaEndDate.toString() + ") в CALCULATE_CHARGES.");
    }
    // ... (остальная логика как раньше, но при вызове db_.calculateChargesForRecord, если он вернет ошибку или 0 из-за ошибки тарифа, это нужно учесть)
    // В текущей реализации Database::calculateChargesForRecord логирует ошибку тарифа и возвращает 0.
    // ServerCommandHandler просто выведет 0 для такой записи.
    // Это приемлемо, если не требуется прерывать весь расчет из-за проблемы с тарифом для одной записи.

    std::string criteria_log_calc = "Период: " + params.criteriaStartDate.toString() + " - " + params.criteriaEndDate.toString();
    if(params.useNameFilter) criteria_log_calc += ", FIO: '" + params.criteriaName + "'";
    if(params.useIpFilter) criteria_log_calc += ", IP: " + params.criteriaIpAddress.toString();
    if(params.useDateFilter) criteria_log_calc += ", DateRec: " + params.criteriaDate.toString();
    Logger::info("CALCULATE_CHARGES: Расчет для: " + criteria_log_calc);

    std::vector<ProviderRecord> records_to_process;
    // ... (логика выбора records_to_process как и ранее) ...
    if (!params.useNameFilter && !params.useIpFilter && !params.useDateFilter) {
        records_to_process = db_.getAllRecords();
    } else {
        std::vector<size_t> record_indices = db_.findRecordsByCriteria(
            params.criteriaName, params.useNameFilter, 
            params.criteriaIpAddress, params.useIpFilter, 
            params.criteriaDate, params.useDateFilter);
        if (record_indices.empty()){ 
            oss << "Не найдено записей по указанным критериям для расчета начислений.\n"; 
            Logger::info("CALCULATE_CHARGES: Записи по критериям не найдены.");
            return;
        }
        records_to_process.reserve(record_indices.size());
        for(size_t index : record_indices) { 
            try { records_to_process.push_back(db_.getRecordByIndex(index)); } 
            catch (const std::out_of_range&) { /* Пропускаем, если запись уже удалена */ } 
        }
    }
     if (records_to_process.empty() && (params.useNameFilter || params.useIpFilter || params.useDateFilter) ){ 
        oss << "Не найдено записей по указанным критериям для расчета начислений.\n"; 
        Logger::info("CALCULATE_CHARGES: Записи по критериям не найдены (после попытки получить их).");
        return;
    }
    if (records_to_process.empty()){ // Если вообще нет записей
        oss << "В базе данных нет записей для расчета.\n"; 
        Logger::info("CALCULATE_CHARGES: База данных пуста.");
        return;
    }


    double grandTotalCharges = 0.0;
    oss << "Отчет по расчету стоимости за период (" << params.criteriaStartDate.toString() << " - " << params.criteriaEndDate.toString() << ") на сервере:\n";
    oss << "-----------------------------------------------------------------\n";
    oss << std::fixed << std::setprecision(2);
    bool charges_calculated_for_at_least_one = false;

    for (const auto& record : records_to_process) {
        if (record.getDate() >= params.criteriaStartDate && record.getDate() <= params.criteriaEndDate) {
            double charge = 0.0;
            try {
                charge = db_.calculateChargesForRecord(record, tariff_plan_, params.criteriaStartDate, params.criteriaEndDate);
                oss << "Абонент: " << record.getName() << " (IP: " << record.getIpAddress().toString()
                    << ", Дата записи: " << record.getDate().toString() << ") | Начислено: " << charge << "\n";
                charges_calculated_for_at_least_one = true; 
                grandTotalCharges += charge;
            } 
            // Database::calculateChargesForRecord уже логирует ошибки тарифа и возвращает 0,
            // поэтому здесь ловить std::out_of_range от тарифа не обязательно, если мы согласны с 0.
            catch (const std::exception& e) { // Другие неожиданные ошибки
                oss << "Абонент: " << record.getName() << " | Ошибка расчета: " << e.what() << "\n";
                Logger::error("CALCULATE_CHARGES: Неожиданная ошибка расчета для " + record.getName() + ": " + e.what());
            }
        }
    }
    if (!charges_calculated_for_at_least_one) { 
        oss << "Для выбранных записей начисления отсутствуют (возможно, все записи вне периода расчета или тарифы нулевые/ошибочны).\n"; 
    }
    oss << "-----------------------------------------------------------------\n";
    oss << "ИТОГО начислено для выборки на сервере: " << grandTotalCharges << "\n";
    oss << "-----------------------------------------------------------------\n";
    Logger::info("CALCULATE_CHARGES: Расчет завершен. Итого начислено: " + std::to_string(grandTotalCharges));
}

/*! \brief Обработка команды PRINT_ALL. */
void ServerCommandHandler::handlePrintAll(std::ostringstream& oss) {
    // ... (реализация без существенных изменений, но итерация по индексам db_.getRecordCount() надежнее)
    Logger::info("PRINT_ALL: Запрос на вывод всех записей.");
    const size_t count = db_.getRecordCount();
    
    if (count == 0) {
        oss << "База данных на сервере пуста.\n";
        Logger::info("PRINT_ALL: База данных пуста.");
    } else {
        oss << "Содержимое базы данных на сервере (" << count << " записей):\n";
        Logger::info("PRINT_ALL: Вывод " + std::to_string(count) + " записей.");
        oss << "-----------------------------------------------------------------\n";
        for (size_t i = 0; i < count; ++i) { // Итерация по индексам
            try {
                const ProviderRecord& rec = db_.getRecordByIndex(i); 
                oss << "Запись (Индекс в БД #" << i << "):\n";
                oss << rec; 
                oss << "\n-----------------------------------------------------------------\n";
            } catch(const std::out_of_range& e) {
                // Этого не должно случиться при итерации до getRecordCount()
                oss << "Ошибка [Сервер]: Внутренняя ошибка при доступе к записи по индексу " << i << " при PRINT_ALL: " << e.what() << "\n";
                Logger::error("PRINT_ALL: Критическая ошибка доступа к записи " + std::to_string(i) + ": " + e.what());
                // Можно прервать вывод, если это критично
                break;
            }
        }
    }
}

/*! \brief Обработка команды LOAD. */
void ServerCommandHandler::handleLoad(const QueryParameters& params, std::ostringstream& oss) {
    if (params.filename.empty()) { // QueryParser должен был это проверить
        throw std::runtime_error("Команда LOAD требует имя файла.");
    }
    
    // getSafeServerFilePath_SCH может выбросить runtime_error, будет поймано в processCommand
    std::filesystem::path target_file_path = getSafeServerFilePath_SCH(server_data_base_path_, params.filename);
    
    Logger::info("LOAD: Попытка загрузки данных с сервера из файла: '" + target_file_path.string() + "'");
    FileOperationResult load_res = db_.loadFromFile(target_file_path.string()); 
    
    oss << load_res.user_message << "\n"; // Отправляем сообщение от Database клиенту
    
    if (!load_res.success) {
        // Если Database::loadFromFile вернул success=false, это может быть из-за IO ошибки или если все записи были ошибочны.
        // user_message уже должен содержать информацию об ошибке.
        // Дополнительно логируем детали, если они есть.
        Logger::warn("LOAD: Операция загрузки файла '" + target_file_path.string() + "' завершилась с ошибкой. "
                     "Сообщение для клиента: \"" + load_res.user_message + "\"" +
                     (load_res.error_details.empty() ? "" : " Детали сервера: " + load_res.error_details));
        // Не бросаем здесь исключение, так как ответ клиенту уже сформирован.
        // Если бы load_res.user_message не содержал информации об ошибке, можно было бы бросить исключение.
    } else {
        Logger::info("LOAD: Операция загрузки файла '" + target_file_path.string() + "' завершена. "
                     "Сообщение для клиента: \"" + load_res.user_message + "\"");
    }
}

/*! \brief Обработка команды SAVE. */
void ServerCommandHandler::handleSave(const QueryParameters& params, std::ostringstream& oss) {
    std::string filename_to_save_on_server_final;
    FileOperationResult save_res;

    if (params.filename.empty()) { // SAVE без указания имени файла
        filename_to_save_on_server_final = db_.getCurrentFilename();
        if (filename_to_save_on_server_final.empty()) {
            throw std::runtime_error("Имя файла для SAVE не указано и не было установлено ранее (через LOAD или SAVE с именем). Некуда сохранять.");
        }
        Logger::info("SAVE: Используется текущее имя файла из БД для сохранения: '" + filename_to_save_on_server_final + "'");
        // currentFilename_ из БД уже должен быть безопасным абсолютным путем.
        // Передаем его напрямую в Database::saveToFile() без параметра, он использует свой currentFilename_.
        save_res = db_.saveToFile(); 
    } else { // SAVE с указанием имени файла
        // getSafeServerFilePath_SCH может выбросить runtime_error
        std::filesystem::path target_file_path = getSafeServerFilePath_SCH(server_data_base_path_, params.filename);
        filename_to_save_on_server_final = target_file_path.string();
        Logger::info("SAVE: Используется указанное клиентом имя файла, разрешенное на сервере в: '" + filename_to_save_on_server_final + "'");
        save_res = db_.saveToFile(filename_to_save_on_server_final);
    }
    
    oss << save_res.user_message << "\n"; // Отправляем сообщение от Database клиенту

    if (!save_res.success) {
        Logger::warn("SAVE: Операция сохранения в файл '" + filename_to_save_on_server_final + "' завершилась с ошибкой. "
                     "Сообщение для клиента: \"" + save_res.user_message + "\"" +
                     (save_res.error_details.empty() ? "" : " Детали сервера: " + save_res.error_details));
    } else {
         Logger::info("SAVE: Операция сохранения в файл '" + filename_to_save_on_server_final + "' завершена. "
                     "Сообщение для клиента: \"" + save_res.user_message + "\"");
    }
}
