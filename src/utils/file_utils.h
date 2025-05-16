/*!
 * \file file_utils.h
 * \author Fedor Zilnitskiy
 * \brief Объявляет утилиты для работы с файловой системой, такие как определение путей к проекту и данным.
 *
 * Эти функции помогают абстрагироваться от конкретного расположения исполняемого файла
 * и позволяют находить ресурсы проекта (например, файлы данных, конфигурационные файлы)
 * более надежным способом. Используется C++17 `<filesystem>`.
 */
#ifndef FILE_UTILS_H
#define FILE_UTILS_H

#include "common_defs.h" // Включает <filesystem>, <string>, <stdexcept>
#include <string>
#include <filesystem> // C++17
#include <stdexcept>  // Для std::runtime_error

/*!
 * \brief Пытается определить и вернуть абсолютный путь к корневой директории проекта.
 *
 * Использует различные эвристики, такие как поиск маркерных файлов (`CMakeLists.txt`, `.git`)
 * или анализ структуры директорий (например, если исполняемый файл находится в `build/bin`).
 *
 * \param executabl_path_or_any_path_within_project Абсолютный или относительный путь к исполняемому файлу (например, argv[0])
 * или любой другой известный путь внутри проекта. Используется как отправная точка для поиска.
 * Если передается относительный путь, он будет разрешен относительно текущей рабочей директории.
 * \return `std::filesystem::path`, представляющий предполагаемый корень проекта.
 * \throw std::runtime_error если `executabl_path_or_any_path_within_project` пуст или невалиден,
 * или если возникают критические ошибки при работе с файловой системой.
 * \note В случае полной неудачи определения корня по эвристикам, может вернуть текущую рабочую директорию (CWD) с соответствующим предупреждением в лог.
 */
std::filesystem::path getProjectRootPath(const char* executabl_path_or_any_path_within_project);

/*!
 * \brief Формирует абсолютный путь к файлу или директории внутри стандартной поддиректории для данных ('data') относительно корня проекта.
 * \param fileOrDirInDataSubdir Имя файла или поддиректории внутри 'data' (например, "tariff.cfg" или "logs/app.log").
 * \param executabl_path_for_root_detection Абсолютный или относительный путь к исполняемому файлу или любой известный путь внутри проекта,
 * используемый для определения корня проекта через `getProjectRootPath`.
 * \return `std::filesystem::path`, представляющий полный путь к ресурсу в директории `data`.
 * \throw std::runtime_error если `executabl_path_for_root_detection` пуст/невалиден или возникают ошибки при формировании пути.
 * \note Если `fileOrDirInDataSubdir` пуст, возвращает путь к самой директории `data`.
 */
std::filesystem::path getProjectDataPath(const std::string& fileOrDirInDataSubdir, const char* executabl_path_for_root_detection);

#endif // FILE_UTILS_H
