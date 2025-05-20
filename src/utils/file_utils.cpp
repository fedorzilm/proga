/*!
 * \file file_utils.cpp
 * \author Fedor Zilnitskiy
 * \brief Реализация утилит для работы с файловой системой.
 */
#include "file_utils.h"
#include "logger.h" // Для логирования операций и ошибок
#include <vector>   // Для списка маркеров проекта
#include <cstdio>   // Для _getcwd, getcwd (используется в getSafeServerFilePath, если root пуст)
#include <cctype>   // Для std::iscntrl (используется в getSafeServerFilePath для очистки имени файла)

#ifdef _WIN32
#include <direct.h> // Для _getcwd
#else
#include <unistd.h> // Для getcwd
#endif


// Проверка на доступность <filesystem> во время компиляции
#ifndef __cpp_lib_filesystem
    #error "C++17 std::filesystem is required for file_utils.cpp. Check your compiler and C++ standard settings."
#endif

namespace FileUtils {

    /*!
     * \brief Определяет корень проекта на основе пути к исполняемому файлу или другому файлу внутри проекта.
     * \param executable_path_or_any_path_within_project Путь, используемый как отправная точка.
     * \return Путь к корню проекта.
     */
    std::filesystem::path getProjectRootPath(const char* executable_path_or_any_path_within_project) {
        if (executable_path_or_any_path_within_project == nullptr || executable_path_or_any_path_within_project[0] == '\0') {
            Logger::error("FileUtils::getProjectRootPath: Получен нулевой или пустой путь. Невозможно определить корень проекта.");
            throw std::runtime_error("Неверный начальный путь для getProjectRootPath.");
        }

        std::filesystem::path initial_path_obj;
        try { 
             initial_path_obj = std::filesystem::path(executable_path_or_any_path_within_project);
        } catch (const std::exception& e) {
            Logger::error(std::string("FileUtils::getProjectRootPath: Ошибка создания пути из входной строки '") + executable_path_or_any_path_within_project + "': " + e.what());
            throw std::runtime_error("Не удалось создать путь из входных данных для getProjectRootPath.");
        }

        if (initial_path_obj.empty() && !(executable_path_or_any_path_within_project[0] == '\0')) {
            Logger::warn(std::string("FileUtils::getProjectRootPath: Начальная строка пути '") + executable_path_or_any_path_within_project + "' привела к пустому объекту std::filesystem::path. Поведение может быть неожиданным.");
        }
        std::filesystem::path current_processing_path;

        try {
            if (initial_path_obj.is_absolute()) {
                current_processing_path = initial_path_obj;
            } else {
                current_processing_path = std::filesystem::absolute(initial_path_obj);
            }
            current_processing_path = std::filesystem::weakly_canonical(current_processing_path);
            Logger::debug("FileUtils::getProjectRootPath: Начальный канонизированный путь: " + current_processing_path.string());
        } catch (const std::filesystem::filesystem_error& e) {
            Logger::error(std::string("FileUtils::getProjectRootPath: Ошибка файловой системы при обработке начального пути '") +
                          executable_path_or_any_path_within_project + "': " + e.what() + ". Возврат CWD в качестве запасного варианта.");
            try {
                return std::filesystem::current_path();
            } catch (const std::filesystem::filesystem_error& e_cwd) {
                 Logger::error(std::string("FileUtils::getProjectRootPath: КРИТИЧЕСКАЯ ОШИБКА: Не удалось получить current_path() в качестве запасного варианта: ") + e_cwd.what());
                 throw std::runtime_error("Не удалось определить корень проекта и CWD."); 
            }
        }

        std::filesystem::path search_start_dir;
        std::error_code ec; 
        bool path_exists = std::filesystem::exists(current_processing_path, ec);
        if (ec) {
             Logger::warn(std::string("FileUtils::getProjectRootPath: Ошибка проверки существования '") + current_processing_path.string() + "': " + ec.message() + ". Пока предполагается, что не существует.");
             path_exists = false;
        }

        if (path_exists) {
            bool is_dir = std::filesystem::is_directory(current_processing_path, ec);
            if(ec) { Logger::warn(std::string("FileUtils::getProjectRootPath: Ошибка проверки, является ли '") + current_processing_path.string() + "' директорией: " + ec.message()); is_dir = false; }

            bool is_file = !is_dir && std::filesystem::is_regular_file(current_processing_path, ec);
             if(ec && !is_dir) { Logger::warn(std::string("FileUtils::getProjectRootPath: Ошибка проверки, является ли '") + current_processing_path.string() + "' обычным файлом: " + ec.message()); is_file = false; }

            if (is_dir) {
                search_start_dir = current_processing_path;
            } else if (is_file && current_processing_path.has_parent_path()) {
                search_start_dir = current_processing_path.parent_path();
            } else if (current_processing_path.has_parent_path()){ 
                search_start_dir = current_processing_path.parent_path();
            } else { 
                try { search_start_dir = std::filesystem::current_path(); } 
                catch(const std::filesystem::filesystem_error& e_cwd) { throw std::runtime_error(std::string("Не удалось получить CWD для поиска корня: ") + e_cwd.what());}
                Logger::warn(std::string("FileUtils::getProjectRootPath: Не удалось определить действительную директорию для начала поиска из '") +
                             current_processing_path.string() + "'. Используется CWD: " + search_start_dir.string());
            }
        } else if (current_processing_path.has_parent_path()) { 
             search_start_dir = current_processing_path.parent_path();
             std::filesystem::path temp_parent = search_start_dir;
             while(!std::filesystem::exists(temp_parent, ec) && temp_parent.has_parent_path() && temp_parent.parent_path() != temp_parent){
                if(ec) { break; }
                temp_parent = temp_parent.parent_path();
             }
             if(std::filesystem::exists(temp_parent, ec) && !ec) { search_start_dir = temp_parent;}
             else { 
                try { search_start_dir = std::filesystem::current_path(); } 
                catch(const std::filesystem::filesystem_error& e_cwd) {throw std::runtime_error(std::string("Не удалось получить CWD для поиска корня: ") + e_cwd.what());}
                 Logger::warn(std::string("FileUtils::getProjectRootPath: Путь '") + current_processing_path.string() + "' и его существующие родители не существуют или ошибка проверки. Используется CWD.");
             }
        }
         else { 
            try { search_start_dir = std::filesystem::current_path(); } 
            catch(const std::filesystem::filesystem_error& e_cwd) {throw std::runtime_error(std::string("Не удалось получить CWD для поиска корня: ") + e_cwd.what());}
            Logger::warn(std::string("FileUtils::getProjectRootPath: Путь '") + current_processing_path.string() + "' не существует и не имеет родителя. Используется CWD.");
        }

        Logger::debug("FileUtils::getProjectRootPath: Начало поиска корня из директории: " + search_start_dir.string());

        std::filesystem::path p = search_start_dir;
        if (p.filename() == "bin" && p.has_parent_path()) { 
            std::filesystem::path parent_of_bin = p.parent_path();
            if (parent_of_bin.filename() == "build" || parent_of_bin.filename() == "Debug" || parent_of_bin.filename() == "Release" ||
                parent_of_bin.filename() == "RelWithDebInfo" || parent_of_bin.filename() == "MinSizeRel") {
                if (parent_of_bin.has_parent_path()) { 
                    std::filesystem::path candidate_root = std::filesystem::weakly_canonical(parent_of_bin.parent_path());
                    if (std::filesystem::exists(candidate_root / "CMakeLists.txt") ||
                        std::filesystem::exists(candidate_root / ".git") ||
                        std::filesystem::exists(candidate_root / "src")) {
                        Logger::info("FileUtils::getProjectRootPath: Корень проекта найден по структуре '*/тип_сборки/bin': " + candidate_root.string());
                        return candidate_root;
                    }
                }
            }
            if (std::filesystem::exists(parent_of_bin / "CMakeLists.txt") ||
                std::filesystem::exists(parent_of_bin / ".git") ||
                std::filesystem::exists(parent_of_bin / "src")) {
                Logger::info("FileUtils::getProjectRootPath: Корень проекта найден по структуре '*/bin', где '*' - корень: " + parent_of_bin.string());
                return std::filesystem::weakly_canonical(parent_of_bin);
            }
        }

        std::vector<std::string> project_markers = {"CMakeLists.txt", ".git", "src"}; 
        std::filesystem::path current_path_for_marker_search = search_start_dir;
        const int max_depth_search = 8; 

        for (int i = 0; i < max_depth_search; ++i) {
            bool current_search_path_is_dir = std::filesystem::is_directory(current_path_for_marker_search, ec);
            if(ec || !current_search_path_is_dir){
                 if (current_path_for_marker_search.has_parent_path() && current_path_for_marker_search.parent_path() != current_path_for_marker_search) {
                    current_path_for_marker_search = current_path_for_marker_search.parent_path();
                    continue;
                 } else {
                    break; 
                 }
            }

            for (const auto& marker : project_markers) {
                if (std::filesystem::exists(current_path_for_marker_search / marker)) {
                    Logger::info("FileUtils::getProjectRootPath: Корень проекта найден по маркеру '" + marker + "' в: " + current_path_for_marker_search.string());
                    return std::filesystem::weakly_canonical(current_path_for_marker_search);
                }
            }
            if (!current_path_for_marker_search.has_parent_path() || current_path_for_marker_search.parent_path() == current_path_for_marker_search) {
                Logger::debug("FileUtils::getProjectRootPath: Достигнут корень файловой системы или отсутствует родительский путь во время поиска маркера.");
                break;
            }
            current_path_for_marker_search = current_path_for_marker_search.parent_path();
        }

        std::filesystem::path cwd;
        try {
            cwd = std::filesystem::current_path();
        } catch (const std::filesystem::filesystem_error& e_cwd){
            Logger::error(std::string("FileUtils::getProjectRootPath: КРИТИЧЕСКАЯ ОШИБКА: Не удалось получить current_path() в качестве окончательного запасного варианта: ") + e_cwd.what());
            throw std::runtime_error("Не удалось определить корень проекта, и CWD не может быть получен.");
        }
        Logger::warn("FileUtils::getProjectRootPath: Корень проекта не был однозначно идентифицирован по маркерам или структуре. Возврат текущей рабочей директории в качестве запасного варианта: " + cwd.string());
        return cwd;
    }

    std::filesystem::path getProjectDataPath(const std::string& fileOrDirInDataSubdir, const char* executable_path_for_root_detection) {
        std::filesystem::path project_root;
        try {
            project_root = getProjectRootPath(executable_path_for_root_detection);
        } catch (const std::exception& e) { 
            Logger::error(std::string("FileUtils::getProjectDataPath: Ошибка получения корня проекта для директории данных: ") + e.what());
            try {
                project_root = std::filesystem::current_path();
                Logger::warn("FileUtils::getProjectDataPath: Используется CWD (" + project_root.string() + ") как корень проекта для директории данных из-за предыдущей ошибки.");
            } catch (const std::filesystem::filesystem_error& e_cwd) {
                 Logger::error(std::string("FileUtils::getProjectDataPath: КРИТИЧЕСКАЯ ОШИБКА: Не удалось получить current_path() в качестве запасного варианта для корня проекта: ") + e_cwd.what());
                 throw std::runtime_error("Не удалось определить путь к данным проекта и CWD.");
            }
        }

        std::filesystem::path data_dir_path = project_root / "data";
        std::error_code ec;
        if (!std::filesystem::exists(data_dir_path, ec) && !ec) { 
            Logger::info("FileUtils::getProjectDataPath: Директория данных '" + data_dir_path.string() + "' не существует. Попытка создать ее.");
            try {
                if (std::filesystem::create_directories(data_dir_path)) {
                    Logger::info("FileUtils::getProjectDataPath: Успешно создана директория данных проекта: " + data_dir_path.string());
                } else {
                    if (!std::filesystem::exists(data_dir_path)) { 
                        Logger::warn("FileUtils::getProjectDataPath: Не удалось создать директорию данных '" + data_dir_path.string() + "', и она все еще не существует.");
                    }
                }
            } catch (const std::filesystem::filesystem_error& e_create_dir) {
                Logger::warn(std::string("FileUtils::getProjectDataPath: Ошибка файловой системы при попытке создания директории данных '") + data_dir_path.string() + "': " + e_create_dir.what());
            }
        } else if (ec) {
            Logger::warn(std::string("FileUtils::getProjectDataPath: Ошибка проверки существования директории данных '") + data_dir_path.string() + "': " + ec.message());
        } else if (std::filesystem::exists(data_dir_path) && !std::filesystem::is_directory(data_dir_path, ec) && !ec) { 
            Logger::error("FileUtils::getProjectDataPath: Путь для данных '" + data_dir_path.string() + "' существует, но не является директорией!");
            throw std::runtime_error("Путь к данным проекта не является директорией: " + data_dir_path.string());
        } else if (ec) { 
             Logger::warn(std::string("FileUtils::getProjectDataPath: Ошибка проверки, является ли путь к данным '") + data_dir_path.string() + "' директорией: " + ec.message());
        }

        std::filesystem::path final_path;
        if (fileOrDirInDataSubdir.empty()) {
            final_path = data_dir_path;
        } else {
            std::filesystem::path subdir_path_part(fileOrDirInDataSubdir);
            final_path = data_dir_path / subdir_path_part.filename(); 
        }

        Logger::debug("FileUtils::getProjectDataPath: Сконструированный путь к данным: " + final_path.string());
        try {
            return std::filesystem::weakly_canonical(final_path);
        } catch (const std::filesystem::filesystem_error& e_canon) {
            Logger::warn(std::string("FileUtils::getProjectDataPath: Ошибка файловой системы во время weakly_canonical для '") + final_path.string() + "': " + e_canon.what() + ". Возврат неканонизированного пути.");
            return final_path;
        }
    }

    std::filesystem::path getSafeServerFilePath(
        const std::string& configured_server_data_root_str,
        const std::string& requested_filename_from_client,
        const std::string& data_subdir_name)
    {
        const std::string log_prefix = "[FileUtils::getSafeServerFilePath] ";
        Logger::debug(log_prefix + "Вызван с configured_root='" + configured_server_data_root_str +
                      "', requested_client_filename='" + requested_filename_from_client +
                      "', data_subdir='" + data_subdir_name + "'");

        std::filesystem::path server_data_search_base;

        if (!configured_server_data_root_str.empty()) {
            try {
                std::filesystem::path configured_root_path(configured_server_data_root_str);
                if (configured_root_path.is_absolute()){
                     server_data_search_base = std::filesystem::weakly_canonical(configured_root_path);
                } else {
                    Logger::warn(log_prefix + "Указанный корень данных сервера ('" + configured_server_data_root_str + "') является относительным. Разрешение относительно CWD.");
                    server_data_search_base = std::filesystem::weakly_canonical(std::filesystem::absolute(configured_root_path));
                }
            } catch (const std::filesystem::filesystem_error& e) {
                Logger::error(log_prefix + "Ошибка файловой системы при обработке configured_server_data_root_str '" + configured_server_data_root_str + "': " + e.what() + ". Откат к определению корня на основе CWD.");
                char current_path_cstr_fallback[2048] = {0}; 
                #ifdef _WIN32
                    if (_getcwd(current_path_cstr_fallback, sizeof(current_path_cstr_fallback) -1) == nullptr) { throw std::runtime_error("КРИТИЧЕСКАЯ ОШИБКА: _getcwd не удалось при откате.");}
                #else
                    if (getcwd(current_path_cstr_fallback, sizeof(current_path_cstr_fallback) -1) == nullptr) { throw std::runtime_error("КРИТИЧЕСКАЯ ОШИБКА: getcwd не удалось при откате. Errno: " + std::to_string(errno));}
                #endif
                try {
                    server_data_search_base = getProjectRootPath(current_path_cstr_fallback);
                } catch (const std::exception& e_fallback){
                     Logger::error(log_prefix + "Откат к getProjectRootPath из CWD ('" + std::string(current_path_cstr_fallback) + "') также не удался: " + e_fallback.what() + ". Использование CWD напрямую как крайняя мера.");
                     try { server_data_search_base = std::filesystem::current_path(); } 
                     catch(const std::filesystem::filesystem_error& e_cwd_final) {throw std::runtime_error(std::string("КРИТИЧЕСКАЯ ОШИБКА: CWD недоступен для определения безопасного пути: ") + e_cwd_final.what());}
                }
            }
        } else {
            Logger::warn(log_prefix + "Configured_server_data_root_str пуст. Попытка определить корень проекта из CWD как базу для данных сервера.");
            char current_path_cstr[2048] = {0};
            #ifdef _WIN32
                if (_getcwd(current_path_cstr, sizeof(current_path_cstr) -1) == nullptr) {
                    Logger::error(log_prefix + "КРИТИЧЕСКАЯ ОШИБКА: Не удалось получить CWD через _getcwd для пути данных сервера. Код ошибки (GetLastError): " + std::to_string(GetLastError())); 
                    throw std::runtime_error("Критическая ошибка сервера: Не удалось получить CWD для определения безопасного пути к файлу (Windows).");
                }
            #else
                if (getcwd(current_path_cstr, sizeof(current_path_cstr) -1) == nullptr) {
                    Logger::error(log_prefix + "КРИТИЧЕСКАЯ ОШИБКА: Не удалось получить CWD через getcwd для пути данных сервера. Errno: " + std::to_string(errno));
                    throw std::runtime_error("Критическая ошибка сервера: Не удалось получить CWD для определения безопасного пути к файлу (POSIX).");
                }
            #endif
            
            try {
                server_data_search_base = getProjectRootPath(current_path_cstr);
            } catch (const std::exception& e_gprp) {
                Logger::error(log_prefix + "Ошибка автоопределения корня проекта из CWD ('" + std::string(current_path_cstr) + "') для данных сервера: " + std::string(e_gprp.what()) + ". Использование самого CWD как базы поиска данных.");
                try { server_data_search_base = std::filesystem::path(current_path_cstr); } 
                catch(const std::exception& e_path_cwd){throw std::runtime_error(std::string("КРИТИЧЕСКАЯ ОШИБКА: Создание пути CWD не удалось: ") + e_path_cwd.what());}
            }
        }
        Logger::debug(log_prefix + "Эффективная база поиска данных сервера для LOAD/SAVE: '" + server_data_search_base.string() + "'");

        std::filesystem::path data_storage_root_dir = server_data_search_base / data_subdir_name;

        std::error_code ec_create;
        if (!std::filesystem::exists(data_storage_root_dir, ec_create) && !ec_create) {
            Logger::info(log_prefix + "Директория хранения данных сервера '" + data_storage_root_dir.string() + "' не существует. Попытка создать ее.");
            try {
                if (std::filesystem::create_directories(data_storage_root_dir)) {
                    Logger::info(log_prefix + "Успешно создана директория хранения данных сервера: '" + data_storage_root_dir.string() + "'");
                } else {
                    if (!std::filesystem::exists(data_storage_root_dir)) { 
                        Logger::error(log_prefix + "Не удалось создать директорию хранения данных сервера '" + data_storage_root_dir.string() + "', и она все еще не существует после попытки.");
                        throw std::runtime_error("Критическая ошибка сервера: Не удалось создать директорию для хранения базы данных: " + data_storage_root_dir.string());
                    }
                }
            } catch (const std::filesystem::filesystem_error& e_create_dir) {
                Logger::error(log_prefix + "Ошибка файловой системы при попытке создания директории хранения данных сервера '" + data_storage_root_dir.string() + "': " + e_create_dir.what());
                throw std::runtime_error("Критическая ошибка сервера: Не удалось создать директорию для хранения базы данных из-за ошибки файловой системы: " + data_storage_root_dir.string());
            }
        } else if (ec_create) {
             Logger::error(log_prefix + "Ошибка проверки существования директории хранения данных сервера '" + data_storage_root_dir.string() + "': " + ec_create.message());
             throw std::runtime_error("Ошибка доступа к директории хранения данных сервера: " + data_storage_root_dir.string());
        }
        else if (std::filesystem::exists(data_storage_root_dir) && !std::filesystem::is_directory(data_storage_root_dir, ec_create) && !ec_create){
            Logger::error(log_prefix + "Путь, предназначенный для хранения данных сервера '" + data_storage_root_dir.string() + "', существует, но НЕ является директорией!");
            throw std::runtime_error("Критическая ошибка сервера: Путь для хранения базы данных не является директорией.");
        } else if (ec_create) {
            Logger::error(log_prefix + "Ошибка проверки, является ли путь хранения данных сервера '" + data_storage_root_dir.string() + "' директорией: " + ec_create.message());
            throw std::runtime_error("Ошибка доступа к типу директории хранения данных сервера: " + data_storage_root_dir.string());
        }
        Logger::debug(log_prefix + "Гарантировано существование директории хранения данных сервера: '" + data_storage_root_dir.string() + "'");

        std::filesystem::path client_filename_path_obj(requested_filename_from_client);
        std::string cleaned_filename = client_filename_path_obj.filename().string(); 

        cleaned_filename.erase(std::remove_if(cleaned_filename.begin(), cleaned_filename.end(),
                                           [](unsigned char c) { return std::iscntrl(c); }), 
                               cleaned_filename.end());
        while (!cleaned_filename.empty() && cleaned_filename[0] == '.') {
            cleaned_filename.erase(0, 1);
        }

        if (cleaned_filename.empty() || cleaned_filename == "." || cleaned_filename == "..") { 
            Logger::warn(log_prefix + "Недопустимое имя файла от клиента после очистки: '" + requested_filename_from_client + "' (результат: пусто или точки).");
            throw std::runtime_error("Указано недопустимое/пустое имя файла клиентом: '" + requested_filename_from_client + "'.");
        }
        if (cleaned_filename.find_first_of("/\\:*?\"<>|") != std::string::npos) {
            Logger::warn(log_prefix + "Имя файла '" + cleaned_filename + "' от клиента содержит недопустимые символы.");
            throw std::runtime_error("Имя файла '" + cleaned_filename + "' содержит запрещенные символы (например, /\\:*?\"<>|).");
        }
        const size_t MAX_FILENAME_LENGTH = 250; 
        if (cleaned_filename.length() > MAX_FILENAME_LENGTH) {
            Logger::warn(log_prefix + "Имя файла от клиента слишком длинное: '" + cleaned_filename + "' (макс. " + std::to_string(MAX_FILENAME_LENGTH) + "). Обрезка для сообщения об ошибке.");
            throw std::runtime_error("Имя файла слишком длинное (макс. " + std::to_string(MAX_FILENAME_LENGTH) + " симв.): '" + cleaned_filename.substr(0,50) + "...'");
        }

        std::filesystem::path target_file_path_final = data_storage_root_dir / cleaned_filename;
        std::filesystem::path canonical_target_path;
        std::filesystem::path canonical_data_storage_root;

        try {
            canonical_target_path = std::filesystem::weakly_canonical(target_file_path_final);
            canonical_data_storage_root = std::filesystem::canonical(data_storage_root_dir); 
        } catch (const std::filesystem::filesystem_error& e_canon) {
            Logger::error(log_prefix + "Ошибка файловой системы во время канонизации пути. Цель: '" + target_file_path_final.string() +
                          "', Корень хранения: '" + data_storage_root_dir.string() + "'. Ошибка: " + e_canon.what());
            throw std::runtime_error("Ошибка сервера при обработке пути к файлу для операции.");
        }

        std::string target_str_normalized = canonical_target_path.lexically_normal().string();
        std::string root_str_normalized = canonical_data_storage_root.lexically_normal().string();

        std::string root_prefix_to_check = root_str_normalized;
        if (!root_prefix_to_check.empty() && root_prefix_to_check.back() != std::filesystem::path::preferred_separator) {
            root_prefix_to_check += std::filesystem::path::preferred_separator;
        }
        
        bool path_is_within_sandbox = (target_str_normalized.rfind(root_prefix_to_check, 0) == 0);
        
        if(path_is_within_sandbox && target_str_normalized.length() <= root_prefix_to_check.length() && target_str_normalized == root_prefix_to_check.substr(0, target_str_normalized.length())) {
             if (!cleaned_filename.empty() && cleaned_filename != "." && cleaned_filename != ".."){
                 // Okay
             } else { 
                 path_is_within_sandbox = false; 
                 Logger::error(log_prefix + "Попытка нарушения песочницы: Запрошенное клиентом имя файла сопоставляется с самой корневой директорией данных или недопустимым относительным путем. "
                               "Запрошено клиентом: '" + requested_filename_from_client + "', "
                               "Очищенное имя: '" + cleaned_filename + "', "
                               "Нормализованный целевой путь: '" + target_str_normalized + "', "
                               "Нормализованный корень хранения данных: '" + root_str_normalized + "'.");
             }
        }

        if (!path_is_within_sandbox) {
            Logger::error(log_prefix + "Попытка нарушения песочницы: Путь находится за пределами разрешенной директории данных! "
                          "Запрошено клиентом: '" + requested_filename_from_client + "', "
                          "Очищенное имя: '" + cleaned_filename + "', "
                          "Нормализованный целевой путь: '" + target_str_normalized + "', "
                          "Ожидается нахождение внутри Нормализованного корня хранения данных (проверенный префикс): '" + root_prefix_to_check + "'.");
            throw std::runtime_error("Доступ к файлу запрещен (нарушение песочницы на сервере).");
        }

        Logger::info(log_prefix + "Определен безопасный абсолютный путь для файловой операции сервера: '" + canonical_target_path.string() + "'");
        return canonical_target_path;
    }

} // namespace FileUtils
