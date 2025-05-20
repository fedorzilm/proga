/*!
 * \file logger.h
 * \author Fedor Zilnitskiy
 * \brief Определяет статический класс Logger для логирования сообщений различного уровня важности в проекте.
 *
 * Logger предоставляет простой интерфейс для вывода отладочной информации, сообщений о ходе выполнения,
 * предупреждений и ошибок. Поддерживает несколько уровней логирования, вывод в консоль (stdout/stderr)
 * и, опционально, в файл. Формат лога включает временную метку, уровень, ID потока и модуль.
 * Класс потокобезопасен благодаря использованию std::mutex.
 */
#ifndef LOGGER_H
#define LOGGER_H

#include "common_defs.h" // Включает <string>, <mutex>, <chrono>, <iomanip>, <thread>, <fstream>, <sstream>, <iostream>

#include <string>
#include <fstream>
#include <sstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <thread>
#include <iostream>

/*!
 * \enum LogLevel
 * \brief Определяет уровни важности для логируемых сообщений.
 */
enum class LogLevel {
    DEBUG = 0, /*!< Детальная отладочная информация, полезная для разработчиков. */
    INFO  = 1, /*!< Информационные сообщения о ходе выполнения программы. */
    WARN  = 2, /*!< Предупреждения о потенциальных проблемах или некритических ошибках. */
    ERROR = 3, /*!< Сообщения об ошибках, которые могут повлиять на работу программы. */
    NONE  = 4  /*!< Специальный уровень для полного отключения логирования (кроме сообщений инициализации и изменения уровня). */
};

/*!
 * \class Logger
 * \brief Статический класс для логирования сообщений.
 *
 * Предоставляет методы `init`, `setLevel`, `debug`, `info`, `warn`, `error`.
 * Не предназначен для создания экземпляров.
 */
class Logger {
public:
    // Запрещаем создание экземпляров и копирование/присваивание
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    /*!
     * \brief Инициализирует логгер с указанным уровнем и, опционально, файлом для вывода.
     * Если файл указан, логи дублируются в файл и консоль (ERROR/WARN всегда в stderr, INFO/DEBUG в stdout).
     * Если файл не указан, вывод осуществляется только в консоль.
     * Повторный вызов `init` переинициализирует логгер (например, для изменения файла или уровня).
     * \param initial_level Начальный уровень логирования. По умолчанию LogLevel::INFO.
     * \param log_file_path Путь к файлу лога. Если пустой, вывод только в консоль. По умолчанию пуст.
     */
    static void init(LogLevel initial_level = LogLevel::INFO, const std::string& log_file_path = "");

    /*!
     * \brief Устанавливает текущий уровень логирования.
     * Сообщения с уровнем ниже установленного не будут выводиться (кроме специальных случаев).
     * \param level Новый уровень логирования.
     */
    static void setLevel(LogLevel level);

    /*!
     * \brief Получает текущий установленный уровень логирования.
     * \return Текущий LogLevel.
     */
    static LogLevel getLevel() noexcept;

    /*!
     * \brief Логирует сообщение с уровнем DEBUG.
     * \param message Текст сообщения.
     * \param module Опциональное имя модуля/компонента, откуда пришло сообщение.
     */
    static void debug(const std::string& message, const std::string& module = "");

    /*!
     * \brief Логирует сообщение с уровнем INFO.
     * \param message Текст сообщения.
     * \param module Опциональное имя модуля/компонента, откуда пришло сообщение.
     */
    static void info(const std::string& message, const std::string& module = "");

    /*!
     * \brief Логирует сообщение с уровнем WARN.
     * \param message Текст сообщения.
     * \param module Опциональное имя модуля/компонента, откуда пришло сообщение.
     */
    static void warn(const std::string& message, const std::string& module = "");

    /*!
     * \brief Логирует сообщение с уровнем ERROR.
     * \param message Текст сообщения.
     * \param module Опциональное имя модуля/компонента, откуда пришло сообщение.
     */
    static void error(const std::string& message, const std::string& module = "");

    /*!
     * \brief Получает строковое представление идентификатора текущего потока.
     * \return Строка с ID потока.
     */
    static std::string get_thread_id_str();

private:
    Logger() = default; /*!< Приватный конструктор, чтобы предотвратить создание экземпляров. */
    ~Logger();          /*!< Приватный деструктор (для статического класса обычно не вызывается явно, но для полноты). */

    static LogLevel current_level_;             /*!< Текущий уровень логирования. */
    static std::mutex log_mutex_;               /*!< Мьютекс для синхронизации доступа к общим ресурсам логгера. */
    static std::ofstream log_file_stream_;      /*!< Поток для вывода логов в файл. */
    static bool use_file_;                      /*!< Флаг, указывающий, используется ли вывод в файл. */
    static bool initialized_;                   /*!< Флаг, указывающий, был ли логгер инициализирован. */

    /*!
     * \brief Внутренний метод для формирования и вывода лог-сообщения.
     * Вызывается публичными методами логирования под мьютексом.
     * \param level Уровень сообщения.
     * \param level_str Строковое представление уровня (DEBUG, INFO и т.д.).
     * \param module Опциональное имя модуля.
     * \param message Текст сообщения.
     */
    static void log_internal(LogLevel level, const std::string& level_str, const std::string& module, const std::string& message);

    /*!
     * \brief Генерирует временную метку для лог-сообщения.
     * \return Строка с временной меткой в формате "ГГГГ-ММ-ДД ЧЧ:ММ:СС.мс".
     */
    static std::string get_timestamp();
};

#endif // LOGGER_H
