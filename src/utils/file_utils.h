// Предполагаемый путь: src/utils/file_utils.h
#ifndef FILE_UTILS_H
#define FILE_UTILS_H

#include <string>
#include <filesystem> // C++17

/*!
 * \file file_utils.h
 * \brief Утилиты для работы с файловой системой, например, для определения путей проекта.
 */

/*!
 * \brief Пытается определить и вернуть абсолютный путь к корневой директории проекта.
 * \param argv0_or_path_within_project Аргумент argv[0] из функции main() или любой известный путь внутри проекта.
 * Используется для определения местоположения исполняемого файла или другого маркера.
 * \return std::filesystem::path, представляющий предполагаемый корень проекта.
 * В случае полной неудачи может вернуть текущую рабочую директорию.
 * \throw std::runtime_error если argv0_or_path_within_project пуст или невалиден.
 */
std::filesystem::path getProjectRootPath(const char* argv0_or_path_within_project);

/*!
 * \brief Формирует абсолютный путь к файлу или директории внутри поддиректории 'data' относительно корня проекта.
 * \param fileOrDirInDataDir Имя файла или поддиректории внутри 'data' (например, "tariff.cfg" или "logs/app.log").
 * \param argv0_or_project_root Аргумент argv[0] из функции main() или уже известный путь к корню проекта.
 * \return std::filesystem::path, представляющий полный путь к ресурсу в директории data.
 * \throw std::runtime_error если argv0_or_project_root пуст или невалиден.
 */
std::filesystem::path getProjectDataPath(const std::string& fileOrDirInDataDir, const char* argv0_or_project_root);

#endif // FILE_UTILS_H
