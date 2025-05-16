// Предполагаемый путь: src/utils/file_utils.cpp
#include "file_utils.h"
#include "logger.h" // Для логирования
#include <vector>   // Для списка маркеров
#include <stdexcept>// Для std::runtime_error

// Проверка на доступность <filesystem> во время компиляции (если еще не сделана в common_defs.h или здесь)
#ifndef __cpp_lib_filesystem
    #error "C++17 std::filesystem is required for file_utils.cpp. Check your compiler and C++ standard settings."
#endif

std::filesystem::path getProjectRootPath(const char* argv0_or_path_within_project) {
    if (argv0_or_path_within_project == nullptr || argv0_or_path_within_project[0] == '\0') {
        Logger::error("getProjectRootPath: Получен пустой или нулевой путь (argv0). Невозможно определить корень проекта.");
        throw std::runtime_error("Невалидный argv0_or_path_within_project для getProjectRootPath.");
    }

    std::filesystem::path initial_path_obj(argv0_or_path_within_project);
    std::filesystem::path current_processing_path;

    try {
        if (initial_path_obj.is_absolute()) {
            current_processing_path = initial_path_obj;
        } else {
            // Если путь относительный, он считается относительно CWD.
            current_processing_path = std::filesystem::absolute(initial_path_obj);
        }
        current_processing_path = std::filesystem::weakly_canonical(current_processing_path);
        Logger::debug("getProjectRootPath: Начальный канонизированный путь: " + current_processing_path.string());
    } catch (const std::filesystem::filesystem_error& e) {
        Logger::error("getProjectRootPath: Ошибка файловой системы при обработке начального пути '" +
                      std::string(argv0_or_path_within_project) + "': " + e.what());
        // В качестве отката, если не удалось даже получить absolute, вернем CWD, но это плохой знак
        return std::filesystem::current_path();
    }

    // Определяем, является ли current_processing_path файлом или директорией
    std::filesystem::path search_start_dir;
    if (std::filesystem::is_directory(current_processing_path)) {
        search_start_dir = current_processing_path;
    } else if (std::filesystem::is_regular_file(current_processing_path) && current_processing_path.has_parent_path()) {
        search_start_dir = current_processing_path.parent_path();
    } else if (current_processing_path.has_parent_path()){ // Если это не существующий файл, но путь имеет родителя
        search_start_dir = current_processing_path.parent_path();
    }
     else {
        search_start_dir = std::filesystem::current_path(); // Откат, если путь совсем странный
        Logger::warn("getProjectRootPath: Не удалось определить директорию для начала поиска из '" +
                     current_processing_path.string() + "'. Используется CWD.");
    }

    Logger::debug("getProjectRootPath: Начало поиска корня из директории: " + search_start_dir.string());

    // Эвристики для определения корня проекта
    // 1. Если мы в build/bin или build/ Debug (Release) /bin
    std::filesystem::path p = search_start_dir;
    if (p.filename() == "bin" && p.has_parent_path()) {
        p = p.parent_path(); // p теперь build/ или build/Debug/
        if (p.filename() == "build" || p.filename() == "Debug" || p.filename() == "Release" || p.filename() == "RelWithDebInfo" || p.filename() == "MinSizeRel") {
            if (p.has_parent_path()) { // Убедимся, что есть родитель у build/*
                 // Проверяем наличие CMakeLists.txt или .git в родителе build/*
                std::filesystem::path candidate_root = std::filesystem::weakly_canonical(p.parent_path());
                 if (std::filesystem::exists(candidate_root / "CMakeLists.txt") ||
                    std::filesystem::exists(candidate_root / ".git")) {
                    Logger::info("getProjectRootPath: Корень проекта найден по структуре '*/build/bin' или '*/build/config/bin': " + candidate_root.string());
                    return candidate_root;
                }
            }
             // Если мы просто в some_dir/bin/ и родитель some_dir содержит маркеры
             if (std::filesystem::exists(p / "CMakeLists.txt") || std::filesystem::exists(p / ".git")) {
                 Logger::info("getProjectRootPath: Корень проекта найден по структуре '*/bin', где '*' это корень: " + p.string());
                 return p;
             }
        }
    }


    // 2. Поиск маркеров проекта (.git, CMakeLists.txt) вверх по иерархии от search_start_dir
    std::vector<std::string> project_markers = {"CMakeLists.txt", ".git"};
    std::filesystem::path current_path_for_marker_search = search_start_dir;

    for (int i = 0; i < 8; ++i) { // Ограничим глубину поиска (8 уровней вверх)
        for (const auto& marker : project_markers) {
            if (std::filesystem::exists(current_path_for_marker_search / marker)) {
                Logger::info("getProjectRootPath: Корень проекта найден по маркеру '" + marker + "' в: " + current_path_for_marker_search.string());
                return std::filesystem::weakly_canonical(current_path_for_marker_search);
            }
        }
        if (!current_path_for_marker_search.has_parent_path() || current_path_for_marker_search.parent_path() == current_path_for_marker_search) {
            Logger::debug("getProjectRootPath: Достигнут корень файловой системы или нет родителя при поиске маркера.");
            break; // Достигли корня ФС или "зациклились"
        }
        current_path_for_marker_search = current_path_for_marker_search.parent_path();
    }

    // 3. Если ничего не найдено, в качестве крайней меры возвращаем CWD
    std::filesystem::path cwd = std::filesystem::current_path();
    Logger::warn("getProjectRootPath: Корень проекта не был однозначно определен по маркерам или структуре. Возвращается текущая рабочая директория: " + cwd.string());
    return cwd;
}

std::filesystem::path getProjectDataPath(const std::string& fileOrDirInDataDir, const char* argv0_or_project_root) {
    if (fileOrDirInDataDir.empty()) {
        Logger::warn("getProjectDataPath: Запрошен пустой путь внутри 'data', возвращен путь к 'data'.");
        // Возвращаем путь к самой директории data
         try {
            return getProjectRootPath(argv0_or_project_root) / "data";
        } catch (const std::exception& e) {
            Logger::error("getProjectDataPath: Ошибка при получении корня проекта для data/: " + std::string(e.what()));
            // Откат к CWD/data в случае ошибки
            return std::filesystem::current_path() / "data";
        }
    }

    try {
        std::filesystem::path project_root = getProjectRootPath(argv0_or_project_root);
        std::filesystem::path data_path = project_root / "data";
        std::filesystem::path final_path = data_path / fileOrDirInDataDir;
        Logger::debug("getProjectDataPath: Сформирован путь: " + final_path.string());
        return final_path;
    } catch (const std::exception& e) {
        Logger::error("getProjectDataPath: Ошибка при формировании пути к '" + fileOrDirInDataDir + "': " + std::string(e.what()));
        // Откат к CWD/data/fileOrDirInDataDir
        return std::filesystem::current_path() / "data" / fileOrDirInDataDir;
    }
}
