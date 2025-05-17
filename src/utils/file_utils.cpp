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
            Logger::error("FileUtils::getProjectRootPath: Received null or empty path. Cannot determine project root.");
            throw std::runtime_error("Invalid initial path for getProjectRootPath.");
        }

        std::filesystem::path initial_path_obj;
        try { 
             initial_path_obj = std::filesystem::path(executable_path_or_any_path_within_project);
        } catch (const std::exception& e) {
            Logger::error(std::string("FileUtils::getProjectRootPath: Error constructing path from input string '") + executable_path_or_any_path_within_project + "': " + e.what());
            throw std::runtime_error("Failed to construct path from input for getProjectRootPath.");
        }

        if (initial_path_obj.empty() && !(executable_path_or_any_path_within_project[0] == '\0')) {
            Logger::warn(std::string("FileUtils::getProjectRootPath: Initial path string '") + executable_path_or_any_path_within_project + "' resulted in an empty std::filesystem::path object. Behavior might be unexpected.");
        }
        std::filesystem::path current_processing_path;

        try {
            if (initial_path_obj.is_absolute()) {
                current_processing_path = initial_path_obj;
            } else {
                current_processing_path = std::filesystem::absolute(initial_path_obj);
            }
            current_processing_path = std::filesystem::weakly_canonical(current_processing_path);
            Logger::debug("FileUtils::getProjectRootPath: Initial canonicalized path: " + current_processing_path.string());
        } catch (const std::filesystem::filesystem_error& e) {
            Logger::error(std::string("FileUtils::getProjectRootPath: Filesystem error while processing initial path '") +
                          executable_path_or_any_path_within_project + "': " + e.what() + ". Returning CWD as fallback.");
            try {
                return std::filesystem::current_path();
            } catch (const std::filesystem::filesystem_error& e_cwd) {
                 Logger::error(std::string("FileUtils::getProjectRootPath: CRITICAL: Failed to get current_path() as fallback: ") + e_cwd.what()); // ИСПРАВЛЕНО
                 throw std::runtime_error("Failed to determine project root and CWD."); 
            }
        }

        std::filesystem::path search_start_dir;
        std::error_code ec; 
        bool path_exists = std::filesystem::exists(current_processing_path, ec);
        if (ec) {
             Logger::warn(std::string("FileUtils::getProjectRootPath: Error checking existence of '") + current_processing_path.string() + "': " + ec.message() + ". Assuming non-existent for now.");
             path_exists = false;
        }

        if (path_exists) {
            bool is_dir = std::filesystem::is_directory(current_processing_path, ec);
            if(ec) { Logger::warn(std::string("FileUtils::getProjectRootPath: Error checking if '") + current_processing_path.string() + "' is directory: " + ec.message()); is_dir = false; }

            bool is_file = !is_dir && std::filesystem::is_regular_file(current_processing_path, ec);
             if(ec && !is_dir) { Logger::warn(std::string("FileUtils::getProjectRootPath: Error checking if '") + current_processing_path.string() + "' is regular file: " + ec.message()); is_file = false; }

            if (is_dir) {
                search_start_dir = current_processing_path;
            } else if (is_file && current_processing_path.has_parent_path()) {
                search_start_dir = current_processing_path.parent_path();
            } else if (current_processing_path.has_parent_path()){ 
                search_start_dir = current_processing_path.parent_path();
            } else { 
                try { search_start_dir = std::filesystem::current_path(); } 
                catch(const std::filesystem::filesystem_error& e_cwd) { throw std::runtime_error(std::string("Failed to get CWD for root search: ") + e_cwd.what());} // ИСПРАВЛЕНО
                Logger::warn(std::string("FileUtils::getProjectRootPath: Could not determine a valid directory to start search from '") +
                             current_processing_path.string() + "'. Using CWD: " + search_start_dir.string());
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
                catch(const std::filesystem::filesystem_error& e_cwd) {throw std::runtime_error(std::string("Failed to get CWD for root search: ") + e_cwd.what());} // ИСПРАВЛЕНО
                 Logger::warn(std::string("FileUtils::getProjectRootPath: Path '") + current_processing_path.string() + "' and its existing parents do not exist or error checking. Using CWD.");
             }
        }
         else { 
            try { search_start_dir = std::filesystem::current_path(); } 
            catch(const std::filesystem::filesystem_error& e_cwd) {throw std::runtime_error(std::string("Failed to get CWD for root search: ") + e_cwd.what());} // ИСПРАВЛЕНО
            Logger::warn(std::string("FileUtils::getProjectRootPath: Path '") + current_processing_path.string() + "' does not exist and has no parent. Using CWD.");
        }

        Logger::debug("FileUtils::getProjectRootPath: Starting root search from directory: " + search_start_dir.string());

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
                        Logger::info("FileUtils::getProjectRootPath: Project root found by '*/build_type/bin' structure: " + candidate_root.string());
                        return candidate_root;
                    }
                }
            }
            if (std::filesystem::exists(parent_of_bin / "CMakeLists.txt") ||
                std::filesystem::exists(parent_of_bin / ".git") ||
                std::filesystem::exists(parent_of_bin / "src")) {
                Logger::info("FileUtils::getProjectRootPath: Project root found by '*/bin' structure where '*' is root: " + parent_of_bin.string());
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
                    Logger::info("FileUtils::getProjectRootPath: Project root found by marker '" + marker + "' in: " + current_path_for_marker_search.string());
                    return std::filesystem::weakly_canonical(current_path_for_marker_search);
                }
            }
            if (!current_path_for_marker_search.has_parent_path() || current_path_for_marker_search.parent_path() == current_path_for_marker_search) {
                Logger::debug("FileUtils::getProjectRootPath: Reached filesystem root or no parent path during marker search.");
                break;
            }
            current_path_for_marker_search = current_path_for_marker_search.parent_path();
        }

        std::filesystem::path cwd;
        try {
            cwd = std::filesystem::current_path();
        } catch (const std::filesystem::filesystem_error& e_cwd){
            Logger::error(std::string("FileUtils::getProjectRootPath: CRITICAL: Failed to get current_path() as final fallback: ") + e_cwd.what()); // ИСПРАВЛЕНО
            throw std::runtime_error("Failed to determine project root and CWD could not be obtained.");
        }
        Logger::warn("FileUtils::getProjectRootPath: Project root was not definitively identified by markers or structure. Returning Current Working Directory as fallback: " + cwd.string());
        return cwd;
    }

    std::filesystem::path getProjectDataPath(const std::string& fileOrDirInDataSubdir, const char* executable_path_for_root_detection) {
        std::filesystem::path project_root;
        try {
            project_root = getProjectRootPath(executable_path_for_root_detection);
        } catch (const std::exception& e) { 
            Logger::error(std::string("FileUtils::getProjectDataPath: Error obtaining project root for data directory: ") + e.what());
            try {
                project_root = std::filesystem::current_path();
                Logger::warn("FileUtils::getProjectDataPath: Using CWD (" + project_root.string() + ") as project root for data directory due to previous error.");
            } catch (const std::filesystem::filesystem_error& e_cwd) {
                 Logger::error(std::string("FileUtils::getProjectDataPath: CRITICAL: Failed to get current_path() as fallback for project root: ") + e_cwd.what()); // ИСПРАВЛЕНО
                 throw std::runtime_error("Failed to determine project data path and CWD.");
            }
        }

        std::filesystem::path data_dir_path = project_root / "data";
        std::error_code ec;
        if (!std::filesystem::exists(data_dir_path, ec) && !ec) { 
            Logger::info("FileUtils::getProjectDataPath: Data directory '" + data_dir_path.string() + "' does not exist. Attempting to create it.");
            try {
                if (std::filesystem::create_directories(data_dir_path)) {
                    Logger::info("FileUtils::getProjectDataPath: Successfully created project data directory: " + data_dir_path.string());
                } else {
                    if (!std::filesystem::exists(data_dir_path)) { 
                        Logger::warn("FileUtils::getProjectDataPath: Failed to create data directory '" + data_dir_path.string() + "' and it still does not exist.");
                    }
                }
            } catch (const std::filesystem::filesystem_error& e_create_dir) {
                Logger::warn(std::string("FileUtils::getProjectDataPath: Filesystem error while trying to create data directory '") + data_dir_path.string() + "': " + e_create_dir.what());
            }
        } else if (ec) {
            Logger::warn(std::string("FileUtils::getProjectDataPath: Error checking existence of data directory '") + data_dir_path.string() + "': " + ec.message());
        } else if (std::filesystem::exists(data_dir_path) && !std::filesystem::is_directory(data_dir_path, ec) && !ec) { 
            Logger::error("FileUtils::getProjectDataPath: Path for data '" + data_dir_path.string() + "' exists but is not a directory!");
            throw std::runtime_error("Project data path is not a directory: " + data_dir_path.string());
        } else if (ec) { 
             Logger::warn(std::string("FileUtils::getProjectDataPath: Error checking if data path '") + data_dir_path.string() + "' is directory: " + ec.message());
        }

        std::filesystem::path final_path;
        if (fileOrDirInDataSubdir.empty()) {
            final_path = data_dir_path;
        } else {
            std::filesystem::path subdir_path_part(fileOrDirInDataSubdir);
            final_path = data_dir_path / subdir_path_part.filename(); 
        }

        Logger::debug("FileUtils::getProjectDataPath: Constructed data path: " + final_path.string());
        try {
            return std::filesystem::weakly_canonical(final_path);
        } catch (const std::filesystem::filesystem_error& e_canon) {
            Logger::warn(std::string("FileUtils::getProjectDataPath: Filesystem error during weakly_canonical for '") + final_path.string() + "': " + e_canon.what() + ". Returning non-canonicalized path.");
            return final_path;
        }
    }

    std::filesystem::path getSafeServerFilePath(
        const std::string& configured_server_data_root_str,
        const std::string& requested_filename_from_client,
        const std::string& data_subdir_name)
    {
        const std::string log_prefix = "[FileUtils::getSafeServerFilePath] ";
        Logger::debug(log_prefix + "Called with configured_root='" + configured_server_data_root_str +
                      "', requested_client_filename='" + requested_filename_from_client +
                      "', data_subdir='" + data_subdir_name + "'");

        std::filesystem::path server_data_search_base;

        if (!configured_server_data_root_str.empty()) {
            try {
                std::filesystem::path configured_root_path(configured_server_data_root_str);
                if (configured_root_path.is_absolute()){
                     server_data_search_base = std::filesystem::weakly_canonical(configured_root_path);
                } else {
                    Logger::warn(log_prefix + "Configured server data root ('" + configured_server_data_root_str + "') is relative. Resolving against CWD.");
                    server_data_search_base = std::filesystem::weakly_canonical(std::filesystem::absolute(configured_root_path));
                }
            } catch (const std::filesystem::filesystem_error& e) {
                Logger::error(log_prefix + "Filesystem error processing configured_server_data_root_str '" + configured_server_data_root_str + "': " + e.what() + ". Falling back to CWD-based root detection.");
                char current_path_cstr_fallback[2048] = {0}; 
                #ifdef _WIN32
                    if (_getcwd(current_path_cstr_fallback, sizeof(current_path_cstr_fallback) -1) == nullptr) { throw std::runtime_error("CRITICAL: _getcwd failed in fallback.");}
                #else
                    if (getcwd(current_path_cstr_fallback, sizeof(current_path_cstr_fallback) -1) == nullptr) { throw std::runtime_error("CRITICAL: getcwd failed in fallback. Errno: " + std::to_string(errno));}
                #endif
                try {
                    server_data_search_base = getProjectRootPath(current_path_cstr_fallback);
                } catch (const std::exception& e_fallback){
                     Logger::error(log_prefix + "Fallback to getProjectRootPath from CWD ('" + std::string(current_path_cstr_fallback) + "') also failed: " + e_fallback.what() + ". Using CWD directly as last resort.");
                     try { server_data_search_base = std::filesystem::current_path(); } 
                     catch(const std::filesystem::filesystem_error& e_cwd_final) {throw std::runtime_error(std::string("CRITICAL: CWD unavailable for safe path determination: ") + e_cwd_final.what());} // ИСПРАВЛЕНО
                }
            }
        } else {
            Logger::warn(log_prefix + "Configured_server_data_root_str is empty. Attempting to determine project root from CWD as base for server data.");
            char current_path_cstr[2048] = {0};
            #ifdef _WIN32
                if (_getcwd(current_path_cstr, sizeof(current_path_cstr) -1) == nullptr) {
                    Logger::error(log_prefix + "CRITICAL: Failed to get CWD via _getcwd for server data path. Errno-like: " + std::to_string(GetLastError())); 
                    throw std::runtime_error("Critical server error: Could not get CWD for safe file path determination (Windows).");
                }
            #else
                if (getcwd(current_path_cstr, sizeof(current_path_cstr) -1) == nullptr) {
                    Logger::error(log_prefix + "CRITICAL: Failed to get CWD via getcwd for server data path. Errno: " + std::to_string(errno));
                    throw std::runtime_error("Critical server error: Could not get CWD for safe file path determination (POSIX).");
                }
            #endif
            
            try {
                server_data_search_base = getProjectRootPath(current_path_cstr);
            } catch (const std::exception& e_gprp) {
                Logger::error(log_prefix + "Error auto-detecting project root from CWD ('" + std::string(current_path_cstr) + "') for server data: " + std::string(e_gprp.what()) + ". Using CWD itself as data search base.");
                try { server_data_search_base = std::filesystem::path(current_path_cstr); } 
                catch(const std::exception& e_path_cwd){throw std::runtime_error(std::string("CRITICAL: CWD path construction failed: ") + e_path_cwd.what());} // ИСПРАВЛЕНО
            }
        }
        Logger::debug(log_prefix + "Effective server data search base for LOAD/SAVE: '" + server_data_search_base.string() + "'");

        std::filesystem::path data_storage_root_dir = server_data_search_base / data_subdir_name;

        std::error_code ec_create;
        if (!std::filesystem::exists(data_storage_root_dir, ec_create) && !ec_create) {
            Logger::info(log_prefix + "Server data storage directory '" + data_storage_root_dir.string() + "' does not exist. Attempting to create it.");
            try {
                if (std::filesystem::create_directories(data_storage_root_dir)) {
                    Logger::info(log_prefix + "Successfully created server data storage directory: '" + data_storage_root_dir.string() + "'");
                } else {
                    if (!std::filesystem::exists(data_storage_root_dir)) { 
                        Logger::error(log_prefix + "Failed to create server data storage directory '" + data_storage_root_dir.string() + "' and it still does not exist after attempt.");
                        throw std::runtime_error("Critical server error: Could not create directory for database storage: " + data_storage_root_dir.string());
                    }
                }
            } catch (const std::filesystem::filesystem_error& e_create_dir) {
                Logger::error(log_prefix + "Filesystem error while trying to create server data storage directory '" + data_storage_root_dir.string() + "': " + e_create_dir.what());
                throw std::runtime_error("Critical server error: Could not create directory for database storage due to filesystem error: " + data_storage_root_dir.string());
            }
        } else if (ec_create) {
             Logger::error(log_prefix + "Error checking existence of server data storage directory '" + data_storage_root_dir.string() + "': " + ec_create.message());
             throw std::runtime_error("Error accessing server data storage directory: " + data_storage_root_dir.string());
        }
        else if (std::filesystem::exists(data_storage_root_dir) && !std::filesystem::is_directory(data_storage_root_dir, ec_create) && !ec_create){
            Logger::error(log_prefix + "Path designated for server data storage '" + data_storage_root_dir.string() + "' exists but is NOT a directory!");
            throw std::runtime_error("Critical server error: Path for database storage is not a directory.");
        } else if (ec_create) {
            Logger::error(log_prefix + "Error checking if server data storage path '" + data_storage_root_dir.string() + "' is a directory: " + ec_create.message());
            throw std::runtime_error("Error accessing server data storage directory type: " + data_storage_root_dir.string());
        }
        Logger::debug(log_prefix + "Ensured server data storage directory exists: '" + data_storage_root_dir.string() + "'");

        std::filesystem::path client_filename_path_obj(requested_filename_from_client);
        std::string cleaned_filename = client_filename_path_obj.filename().string(); 

        cleaned_filename.erase(std::remove_if(cleaned_filename.begin(), cleaned_filename.end(),
                                           [](unsigned char c) { return std::iscntrl(c); }), 
                               cleaned_filename.end());
        while (!cleaned_filename.empty() && cleaned_filename[0] == '.') {
            cleaned_filename.erase(0, 1);
        }

        if (cleaned_filename.empty() || cleaned_filename == "." || cleaned_filename == "..") { 
            Logger::warn(log_prefix + "Invalid filename from client after cleaning: '" + requested_filename_from_client + "' (resulted in empty or dot(s)).");
            throw std::runtime_error("Invalid/empty filename specified by client: '" + requested_filename_from_client + "'.");
        }
        if (cleaned_filename.find_first_of("/\\:*?\"<>|") != std::string::npos) {
            Logger::warn(log_prefix + "Filename '" + cleaned_filename + "' from client contains invalid characters.");
            throw std::runtime_error("Filename '" + cleaned_filename + "' contains forbidden characters (e.g., /\\:*?\"<>|).");
        }
        const size_t MAX_FILENAME_LENGTH = 250; 
        if (cleaned_filename.length() > MAX_FILENAME_LENGTH) {
            Logger::warn(log_prefix + "Filename from client is too long: '" + cleaned_filename + "' (max " + std::to_string(MAX_FILENAME_LENGTH) + "). Truncating for error message.");
            throw std::runtime_error("Filename is too long (max " + std::to_string(MAX_FILENAME_LENGTH) + " chars): '" + cleaned_filename.substr(0,50) + "...'");
        }

        std::filesystem::path target_file_path_final = data_storage_root_dir / cleaned_filename;
        std::filesystem::path canonical_target_path;
        std::filesystem::path canonical_data_storage_root;

        try {
            canonical_target_path = std::filesystem::weakly_canonical(target_file_path_final);
            canonical_data_storage_root = std::filesystem::canonical(data_storage_root_dir); 
        } catch (const std::filesystem::filesystem_error& e_canon) {
            Logger::error(log_prefix + "Filesystem error during path canonicalization. Target: '" + target_file_path_final.string() +
                          "', Storage Root: '" + data_storage_root_dir.string() + "'. Error: " + e_canon.what());
            throw std::runtime_error("Server error while processing file path for operation.");
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
                 Logger::error(log_prefix + "Sandbox Violation Attempt: Client requested filename maps to the root data directory itself or an invalid relative path. "
                               "Requested by client: '" + requested_filename_from_client + "', "
                               "Cleaned name: '" + cleaned_filename + "', "
                               "Normalized Target Path: '" + target_str_normalized + "', "
                               "Normalized Data Storage Root: '" + root_str_normalized + "'.");
             }
        }

        if (!path_is_within_sandbox) {
            Logger::error(log_prefix + "Sandbox Violation Attempt: Path is outside the allowed data directory! "
                          "Requested by client: '" + requested_filename_from_client + "', "
                          "Cleaned name: '" + cleaned_filename + "', "
                          "Normalized Target Path: '" + target_str_normalized + "', "
                          "Expected to be within Normalized Data Storage Root (prefix checked): '" + root_prefix_to_check + "'.");
            throw std::runtime_error("File access denied (sandbox violation on server).");
        }

        Logger::info(log_prefix + "Safe absolute path for server file operation determined: '" + canonical_target_path.string() + "'");
        return canonical_target_path;
    }

} // namespace FileUtils
