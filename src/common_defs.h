/*!
 * \file common_defs.h
 * \author Fedor Zilnitskiy
 * \brief Содержит общие определения, константы и стандартные заголовочные файлы, используемые в проекте "База Данных Интернет-Провайдера".
 *
 * Этот файл централизует подключение часто используемых стандартных библиотек C++
 * и определяет глобальные константы проекта, такие как количество часов в сутках,
 * эпсилон для сравнения чисел с плавающей запятой, максимальный размер сообщения,
 * имена поддиректорий и файлов логов по умолчанию.
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
const uint32_t MAX_MESSAGE_PAYLOAD_SIZE = 20 * 1024 * 1024; /*!< Максимальный размер данных (в байтах), которые могут быть переданы в одном сообщении после префикса длины (20MB). */

// Имя поддиректории по умолчанию для файлов данных сервера (LOAD/SAVE)
// Это имя будет использоваться относительно базовой директории данных сервера.
const std::string DEFAULT_SERVER_DATA_SUBDIR = "server_databases"; /*!< Имя поддиректории по умолчанию для хранения файлов баз данных на сервере. */

// Таймаут по умолчанию для ожидания ответа от сервера на стороне клиента (в миллисекундах)
const int DEFAULT_CLIENT_RECEIVE_TIMEOUT_MS = 120000; /*!< Таймаут по умолчанию (в миллисекундах) для клиента при ожидании ответа от сервера (2 минуты). */

// Имена файлов логов по умолчанию
const std::string DEFAULT_SERVER_LOG_FILE = "server.log";       /*!< Имя файла лога сервера по умолчанию. */
const std::string DEFAULT_CLIENT_LOG_FILE = "client.log";       /*!< Имя файла лога клиента по умолчанию. */
const std::string DEFAULT_GENERATOR_LOG_FILE = "generator.log"; /*!< Имя файла лога генератора данных по умолчанию. */

#endif // COMMON_DEFS_H
