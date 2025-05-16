// Предполагаемый путь: src/common_defs.h
#ifndef COMMON_DEFS_H
#define COMMON_DEFS_H

/*!
 * \file common_defs.h
 * \brief Содержит общие определения, константы и стандартные заголовочные файлы, используемые в проекте.
 */

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
#include <cctype>       // Для функций классификации символов
#include <cmath>        // Для математических функций (std::fabs, std::round)
#include <cstring>      // Для std::strlen, std::memcpy, std::memset (используется в TCPSocket и др.)
#include <thread>       // Для std::thread (сервер)
#include <mutex>        // Для std::mutex, std::lock_guard (сервер, логгер)
#include <atomic>       // Для std::atomic (сервер, обработка сигналов)
#include <csignal>      // Для std::signal (обработка сигналов)
#include <filesystem>   // Для std::filesystem (C++17) (file_utils, server_command_handler)
#include <cstdint>      // Для uint32_t и других целочисленных типов фиксированного размера (TCPSocket)

// Определяем константу для количества часов в сутках
constexpr int HOURS_IN_DAY = 24; /*!< Количество часов в сутках, используемое для данных о трафике. */

// Эпсилон для сравнения чисел с плавающей запятой
constexpr double DOUBLE_EPSILON = 1e-9; /*!< Небольшое значение для сравнения чисел double на приблизительное равенство. */

// Максимальный размер сообщения для протокола "длина + данные" (для безопасности)
const uint32_t MAX_MESSAGE_PAYLOAD_SIZE = 20 * 1024 * 1024; // 20MB, например

// Имя поддиректории по умолчанию для файлов данных сервера (LOAD/SAVE)
const std::string DEFAULT_SERVER_DATA_SUBDIR = "server_databases";

#endif // COMMON_DEFS_H
