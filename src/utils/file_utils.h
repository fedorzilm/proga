/*!
 * \file file_utils.h
 * \author Fedor Zilnitskiy
 * \brief Объявляет утилиты для работы с файловой системой, такие как определение путей к проекту и данным,
 * а также для безопасного формирования путей к файлам на сервере.
 *
 * Эти функции помогают абстрагироваться от конкретного расположения исполняемого файла
 * и позволяют находить ресурсы проекта (например, файлы данных, конфигурационные файлы)
 * более надежным способом. Также предоставляется функция для безопасного формирования путей
 * к файлам данных сервера, используемая в ServerCommandHandler. Используется C++17 `<filesystem>`.
 */
#ifndef FILE_UTILS_H
#define FILE_UTILS_H

#include "common_defs.h" // Включает <filesystem>, <string>, <stdexcept>
#include <string>
#include <filesystem> // C++17
#include <stdexcept>  // Для std::runtime_error

/*!
 * \brief Пространство имен для утилит работы с файлами.
 */
namespace FileUtils {

    /*!
     * \brief Пытается определить и вернуть абсолютный путь к корневой директории проекта.
     *
     * Использует различные эвристики, такие как поиск маркерных файлов (`CMakeLists.txt`, `.git`)
     * или анализ структуры директорий (например, если исполняемый файл находится в `build/bin`).
     *
     * \param executable_path_or_any_path_within_project Абсолютный или относительный путь к исполняемому файлу (например, argv[0])
     * или любой другой известный путь внутри проекта. Используется как отправная точка для поиска.
     * Если передается относительный путь, он будет разрешен относительно текущей рабочей директории.
     * \return `std::filesystem::path`, представляющий предполагаемый корень проекта.
     * \throw std::runtime_error если `executable_path_or_any_path_within_project` пуст или невалиден,
     * или если возникают критические ошибки при работе с файловой системой.
     * \note В случае полной неудачи определения корня по эвристикам, может вернуть текущую рабочую директорию (CWD) с соответствующим предупреждением в лог.
     */
    std::filesystem::path getProjectRootPath(const char* executable_path_or_any_path_within_project);

    /*!
     * \brief Формирует абсолютный путь к файлу или директории внутри стандартной поддиректории для данных ('data') относительно корня проекта.
     * \param fileOrDirInDataSubdir Имя файла или поддиректории внутри 'data' (например, "tariff.cfg" или "logs/app.log").
     * \param executable_path_for_root_detection Абсолютный или относительный путь к исполняемому файлу или любой известный путь внутри проекта,
     * используемый для определения корня проекта через `getProjectRootPath`.
     * \return `std::filesystem::path`, представляющий полный путь к ресурсу в директории `data`.
     * \throw std::runtime_error если `executable_path_for_root_detection` пуст/невалиден или возникают ошибки при формировании пути.
     * \note Если `fileOrDirInDataSubdir` пуст, возвращает путь к самой директории `data`.
     */
    std::filesystem::path getProjectDataPath(const std::string& fileOrDirInDataSubdir, const char* executable_path_for_root_detection);

    /*!
     * \brief Формирует безопасный абсолютный путь к файлу данных на сервере для операций LOAD/SAVE.
     * Гарантирует, что путь находится внутри указанной корневой директории данных сервера и ее стандартной поддиректории.
     * Выполняет очистку имени файла от клиента (удаляет компоненты пути, проверяет на недопустимые символы и длину).
     * \param configured_server_data_root Разрешенный (обычно абсолютный) путь к корневой директории данных, настроенной для сервера.
     * Если пуст, функция попытается определить корень проекта (или CWD) как базу.
     * \param requested_filename_from_client Имя файла, запрошенное клиентом (может содержать попытки обхода директорий).
     * \param data_subdir_name Имя стандартной поддиректории внутри `configured_server_data_root`, где должны храниться файлы БД (например, значение `DEFAULT_SERVER_DATA_SUBDIR`).
     * \return Канонизированный, безопасный `std::filesystem::path` к файлу.
     * \throw std::runtime_error если имя файла от клиента недопустимо, или если результирующий путь выходит за пределы разрешенной "песочницы",
     * или если возникают ошибки файловой системы (например, невозможно создать директорию данных).
     */
    std::filesystem::path getSafeServerFilePath(
        const std::string& configured_server_data_root_str, // Изменено имя параметра для ясности
        const std::string& requested_filename_from_client,
        const std::string& data_subdir_name
    );

} // namespace FileUtils

#endif // FILE_UTILS_H
