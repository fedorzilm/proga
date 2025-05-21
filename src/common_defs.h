/*!
 * \file common_defs.h
 * \author Fedor Zilnitskiy
 * \brief Содержит общие определения, константы и стандартные заголовочные файлы, используемые в проекте "База Данных Интернет-Провайдера".
 *
 * Этот файл централизует подключение часто используемых стандартных библиотек C++
 * и определяет глобальные константы проекта, такие как количество часов в сутках,
 * эпсилон для сравнения чисел с плавающей запятой, максимальный размер сообщения,
 * имена поддиректорий и файлов логов по умолчанию, а также константы для сетевого протокола.
 */
#ifndef COMMON_DEFS_H
#define COMMON_DEFS_H

// Стандартные библиотеки C++
#include <string>       // Для использования std::string
#include <vector>       // Для использования std::vector
#include <array>        // Для использования std::array
#include <iostream>     // Для стандартных потоков ввода/вывода
#include <fstream>      // Для файловых потоков
#include <sstream>      // Для строковых потоков
#include <algorithm>    // Для стандартных алгоритмов
#include <stdexcept>    // Для стандартных исключений
#include <iomanip>      // Для манипуляторов потока
#include <limits>       // Для доступа к свойствам числовых типов
#include <random>       // Для генерации случайных чисел
#include <chrono>       // Для работы со временем
#include <map>          // Для использования std::map
#include <set>          // Для использования std::set
#include <cctype>       // Для функций классификации символов (например, std::toupper, std::isspace)
#include <cmath>        // Для математических функций (std::fabs, std::round)
#include <cstring>      // Для функций работы с C-строками (std::strlen, std::memcpy, std::memset)
#include <thread>       // Для std::thread (используется в сервере и пуле потоков)
#include <mutex>        // Для std::mutex, std::lock_guard, std::unique_lock
#include <shared_mutex> // Для std::shared_mutex, std::shared_lock (сервер, база данных)
#include <atomic>       // Для std::atomic (сервер, обработка сигналов, пул потоков)
#include <condition_variable> // Для std::condition_variable (пул потоков)
#include <functional>   // Для std::function (пул потоков)
#include <queue>        // Для std::queue (пул потоков)
#include <csignal>      // Для std::signal и связанных типов (обработка сигналов на сервере)
#include <filesystem>   // Для std::filesystem (C++17) (используется в file_utils, server_command_handler, server_config, server_main)
#include <cstdint>      // Для uint32_t и других целочисленных типов фиксированного размера (TCPSocket)
#include <optional>     // Для std::optional (может быть полезно в ServerConfig)

// Определяем константу для количества часов в сутках
constexpr int HOURS_IN_DAY = 24; /*!< Количество часов в сутках, используемое для данных о трафике. */

// Эпсилон для сравнения чисел с плавающей запятой
constexpr double DOUBLE_EPSILON = 1e-9; /*!< Небольшое значение для сравнения чисел double на приблизительное равенство. */

// Максимальный размер полезной нагрузки сообщения для протокола "длина + данные" (для безопасности)
// Это будет максимальный размер ОДНОГО чанка или ОДНОГО сообщения, если не чанковать.
// Уменьшено до 1MB для облегчения тестирования чанкования, если потребуется.
// Оригинальное значение было 20MB.
const uint32_t MAX_MESSAGE_PAYLOAD_SIZE = 1 * 1024 * 1024; /*!< Максимальный размер данных (в байтах) одного сообщения/чанка (1MB). */

// Имя поддиректории по умолчанию для файлов данных сервера (LOAD/SAVE)
const std::string DEFAULT_SERVER_DATA_SUBDIR = "server_databases"; /*!< Имя поддиректории по умолчанию для хранения файлов баз данных на сервере. */

// Таймаут по умолчанию для ожидания ответа от сервера на стороне клиента (в миллисекундах)
const int DEFAULT_CLIENT_RECEIVE_TIMEOUT_MS = 120000; /*!< Таймаут по умолчанию (в миллисекундах) для клиента при ожидании ответа от сервера (2 минуты). */

// Имена файлов логов по умолчанию
const std::string DEFAULT_SERVER_LOG_FILE = "server.log";       /*!< Имя файла лога сервера по умолчанию. */
const std::string DEFAULT_CLIENT_LOG_FILE = "client.log";       /*!< Имя файла лога клиента по умолчанию. */
const std::string DEFAULT_GENERATOR_LOG_FILE = "generator.log"; /*!< Имя файла лога генератора данных по умолчанию. */

// --- Константы для Сетевого Протокола Ответа Сервера ---

// Коды статуса ответа сервера
constexpr int SRV_STATUS_OK = 200;                          /*!< Успешное выполнение, одночастный ответ. */
constexpr int SRV_STATUS_OK_MULTI_PART_BEGIN = 201;         /*!< Успешное выполнение, начинается многочастный ответ. */
constexpr int SRV_STATUS_OK_MULTI_PART_CHUNK = 202;         /*!< Успешное выполнение, это очередной чанк данных (не первый и не последний). */
constexpr int SRV_STATUS_OK_MULTI_PART_END = 203;           /*!< Успешное выполнение, это последний чанк данных многочастного ответа (или просто уведомление о конце). */

constexpr int SRV_STATUS_BAD_REQUEST = 400;                 /*!< Ошибка в запросе клиента (например, синтаксис, неверные параметры). */
constexpr int SRV_STATUS_NOT_FOUND = 404;                   /*!< Запрошенный ресурс/запись не найден(а). */
constexpr int SRV_STATUS_SERVER_ERROR = 500;                /*!< Внутренняя ошибка сервера. */
// constexpr int SRV_STATUS_SERVER_UNAVAILABLE = 503;       // Зарезервировано для будущего использования

// Типы полезной нагрузки в ответе сервера
const std::string SRV_PAYLOAD_TYPE_PROVIDER_RECORDS_LIST = "PROVIDER_RECORDS_LIST"; /*!< Список записей ProviderRecord. */
const std::string SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE = "SIMPLE_MESSAGE";         /*!< Простое текстовое сообщение (например, подтверждение, информация). */
const std::string SRV_PAYLOAD_TYPE_ERROR_INFO = "ERROR_INFO";                 /*!< Информация об ошибке. */
const std::string SRV_PAYLOAD_TYPE_NONE = "NONE";                             /*!< Полезная нагрузка отсутствует (например, в SRV_STATUS_OK_MULTI_PART_END). */

// Параметры чанкования
const size_t SRV_DEFAULT_CHUNK_RECORDS_COUNT = 50; /*!< Количество записей ProviderRecord в одном чанке по умолчанию. */
// Порог для включения чанкования (в количестве записей). Если записей больше, используется чанкование.
const size_t SRV_CHUNKING_THRESHOLD_RECORDS = SRV_DEFAULT_CHUNK_RECORDS_COUNT + 10; /*!< Порог в записях для активации чанкования. */


// Ключи для заголовков в ответе сервера (для парсинга)
const std::string SRV_HEADER_STATUS = "STATUS";                         /*!< Ключ заголовка: Код статуса. */
const std::string SRV_HEADER_MESSAGE = "MESSAGE";                       /*!< Ключ заголовка: Текстовое сообщение статуса. */
const std::string SRV_HEADER_RECORDS_IN_PAYLOAD = "RECORDS_IN_PAYLOAD"; /*!< Ключ заголовка: Количество записей в текущей полезной нагрузке. */
const std::string SRV_HEADER_TOTAL_RECORDS = "TOTAL_RECORDS";           /*!< Ключ заголовка: Общее количество записей (для первого сообщения многочастного ответа). */
const std::string SRV_HEADER_PAYLOAD_TYPE = "PAYLOAD_TYPE";             /*!< Ключ заголовка: Тип полезной нагрузки. */
const std::string SRV_HEADER_DATA_MARKER = "--DATA_BEGIN--";            /*!< Маркер начала блока данных полезной нагрузки. */

#endif // COMMON_DEFS_H
