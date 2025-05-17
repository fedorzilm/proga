/*!
 * \file file_utils.cpp
 * \author Fedor Zilnitskiy
 * \brief Реализация утилит для работы с файловой системой.
 */
#include "file_utils.h"
#include "logger.h" // Для логирования операций и ошибок
#include <vector>   // Для списка маркеров проекта

// Проверка на доступность <filesystem> во время компиляции
#ifndef __cpp_lib_filesystem
    #error "C++17 std::filesystem is required for file_utils.cpp. Check your compiler and C++ standard settings."
#endif

/*!
 * \brief Определяет корень проекта на основе пути к исполняемому файлу или другому файлу внутри проекта.
 * \param executabl_path_or_any_path_within_project Путь, используемый как отправная точка.
 * \return Путь к корню проекта.
 */
std::filesystem::path getProjectRootPath(const char* executabl_path_or_any_path_within_project) {
    if (executabl_path_or_any_path_within_project == nullptr || executabl_path_or_any_path_within_project[0] == '\0') {
        Logger::error("getProjectRootPath: Получен пустой или нулевой путь. Невозможно определить корень проекта.");
        throw std::runtime_error("Невалидный начальный путь для getProjectRootPath.");
    }

    std::filesystem::path initial_path_obj(executabl_path_or_any_path_within_project);
    if (initial_path_obj.empty() && !(executabl_path_or_any_path_within_project[0] == '\0')) {
         Logger::warn("getProjectRootPath: Начальный путь '" + std::string(executabl_path_or_any_path_within_project) + "' привел к пустому std::filesystem::path. Поведение может быть неожиданным.");
    }
    std::filesystem::path current_processing_path;

    try {
        // Получаем абсолютный канонический путь для начала работы
        if (initial_path_obj.is_absolute()) {
            current_processing_path = initial_path_obj;
        } else {
            current_processing_path = std::filesystem::absolute(initial_path_obj);
        }
        current_processing_path = std::filesystem::weakly_canonical(current_processing_path);
        Logger::debug("getProjectRootPath: Начальный канонизированный путь: " + current_processing_path.string());
    } catch (const std::filesystem::filesystem_error& e) {
        Logger::error("getProjectRootPath: Ошибка файловой системы при обработке начального пути '" +
                      std::string(executabl_path_or_any_path_within_project) + "': " + e.what());
        return std::filesystem::current_path();
    }

    std::filesystem::path search_start_dir;
    if (std::filesystem::is_directory(current_processing_path)) {
        search_start_dir = current_processing_path;
    } else if (std::filesystem::is_regular_file(current_processing_path) && current_processing_path.has_parent_path()) {
        search_start_dir = current_processing_path.parent_path();
    } else if (current_processing_path.has_parent_path()){
        search_start_dir = current_processing_path.parent_path();
    } else {
        search_start_dir = std::filesystem::current_path();
        Logger::warn("getProjectRootPath: Не удалось определить директорию для начала поиска из '" +
                     current_processing_path.string() + "'. Используется CWD: " + search_start_dir.string());
    }
    Logger::debug("getProjectRootPath: Начало поиска корня из директории: " + search_start_dir.string());

    std::filesystem::path p = search_start_dir;
    if (p.filename() == "bin" && p.has_parent_path()) {
        p = p.parent_path();
        if (p.filename() == "build" || p.filename() == "Debug" || p.filename() == "Release" ||
            p.filename() == "RelWithDebInfo" || p.filename() == "MinSizeRel") {
            if (p.has_parent_path()) {
                std::filesystem::path candidate_root = std::filesystem::weakly_canonical(p.parent_path());
                if (std::filesystem::exists(candidate_root / "CMakeLists.txt") ||
                    std::filesystem::exists(candidate_root / ".git") ||
                    std::filesystem::exists(candidate_root / "src")) {
                    Logger::info("getProjectRootPath: Корень проекта найден по структуре '*/build/bin' или '*/build/config/bin': " + candidate_root.string());
                    return candidate_root;
                }
            }
        }
        if (std::filesystem::exists(p / "CMakeLists.txt") ||
            std::filesystem::exists(p / ".git") ||
            std::filesystem::exists(p / "src")) {
            Logger::info("getProjectRootPath: Корень проекта найден по структуре '*/bin', где '*' это корень: " + p.string());
            return p;
        }
    }

    std::vector<std::string> project_markers = {"CMakeLists.txt", ".git", "src"};
    std::filesystem::path current_path_for_marker_search = search_start_dir;
    const int max_depth_search = 8;

    for (int i = 0; i < max_depth_search; ++i) {
        for (const auto& marker : project_markers) {
            if (std::filesystem::exists(current_path_for_marker_search / marker)) {
                Logger::info("getProjectRootPath: Корень проекта найден по маркеру '" + marker + "' в: " + current_path_for_marker_search.string());
                return std::filesystem::weakly_canonical(current_path_for_marker_search);
            }
        }
        if (!current_path_for_marker_search.has_parent_path() || current_path_for_marker_search.parent_path() == current_path_for_marker_search) {
            Logger::debug("getProjectRootPath: Достигнут корень файловой системы или нет родителя при поиске маркера.");
            break;
        }
        current_path_for_marker_search = current_path_for_marker_search.parent_path();
    }

    std::filesystem::path cwd = std::filesystem::current_path();
    Logger::warn("getProjectRootPath: Корень проекта не был однозначно определен по маркерам или структуре. Возвращается текущая рабочая директория: " + cwd.string());
    return cwd;
}

/*!
 * \brief Формирует путь к ресурсу в поддиректории 'data' проекта.
 * \param fileOrDirInDataSubdir Имя файла/директории в 'data'.
 * \param executabl_path_for_root_detection Путь для определения корня проекта.
 * \return Полный путь к ресурсу.
 */
std::filesystem::path getProjectDataPath(const std::string& fileOrDirInDataSubdir, const char* executabl_path_for_root_detection) {
    std::filesystem::path project_root;
    try {
        project_root = getProjectRootPath(executabl_path_for_root_detection);
    } catch (const std::exception& e) {
        Logger::error("getProjectDataPath: Ошибка при получении корня проекта для data/: " + std::string(e.what()));
        project_root = std::filesystem::current_path();
        Logger::warn("getProjectDataPath: Используется CWD (" + project_root.string() + ") как корень для директории data.");
    }

    std::filesystem::path data_dir_path = project_root / "data";

    if (!std::filesystem::exists(data_dir_path)) {
        try {
            if (std::filesystem::create_directories(data_dir_path)) {
                Logger::info("getProjectDataPath: Создана директория данных проекта: " + data_dir_path.string());
            }
        } catch (const std::filesystem::filesystem_error& e) {
            Logger::warn("getProjectDataPath: Не удалось создать директорию " + data_dir_path.string() + ": " + e.what());
        }
    }

    std::filesystem::path final_path;
    if (fileOrDirInDataSubdir.empty()) {
        final_path = data_dir_path;
    } else {
        final_path = data_dir_path / fileOrDirInDataSubdir;
    }

    Logger::debug("getProjectDataPath: Сформирован путь к данным: " + final_path.string());
    return std::filesystem::weakly_canonical(final_path);
}
