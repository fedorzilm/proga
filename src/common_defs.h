#ifndef COMMON_DEFS_H
#define COMMON_DEFS_H

#include <limits>
#include <string>
#include <vector>
#include <optional>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstdio>
#include <cstdlib>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

const size_t HOURS_IN_DAY = 24;

struct TrafficReading {
    long long incoming = 0;
    long long outgoing = 0;
    long long total() const { return incoming + outgoing; }
};

using TrafficData = std::vector<TrafficReading>;

enum class Condition {
    NONE, EQ, NE, LT, GT, LE, GE, CONTAINS
};

enum class SortOrder {
    NONE, ASC, DESC
};

inline void clear_cin_buffer() {
    std::cin.clear();
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

// Для строгой проверки символов в ПРОСТОМ имени файла (без пути)
inline bool is_valid_simple_filename_char(unsigned char c) {
    return std::isalnum(c) || c == '_' || c == '-' || c == '.';
}

// Для валидации ПРОСТЫХ имен файлов (без пути), например, пользовательский ввод простого имени.
inline bool is_valid_simple_filename(const std::string& filename) {
    if (filename.empty() || filename.length() > 255) {
        return false;
    }
    if (filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos ||
        filename.find("..") != std::string::npos) {
        return false;
    }
    std::string forbidden_chars = "|><;&$#!*()[]{}'\"` "; // Пробел здесь явно запрещен
    for (char fc : forbidden_chars) {
        if (filename.find(fc) != std::string::npos) return false;
    }
    return std::all_of(filename.begin(), filename.end(), is_valid_simple_filename_char);
}

// Для валидации ПОЛНЫХ ПУТЕЙ (абсолютных или относительных), особенно из argv.
inline bool is_valid_cmd_argument_path(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    if (path.find('\0') != std::string::npos) {
        return false;
    }
    if (path.length() > 4096) { // PATH_MAX
         return false;
    }
    // Проверка на управляющие символы C0 (0x01-0x1F, кроме стандартных пробельных) и символ DEL (0x7F).
    for (unsigned char c : path) {
        // Разрешены: \t (9), \n (10), \r (13), \f (12), \v (11) - хотя они редки в путях
        // Запрещены: 0x00 (уже проверено), 0x01-0x08, 0x0E-0x1F, 0x7F (DEL)
        if (c == 127 || (c > 0 && c < 32 && c != '\t' && c != '\n' && c != '\r' && c != '\f' && c != '\v')) {
            // std::cerr << "[Debug common_defs] Path contains control character: " << static_cast<int>(c) << std::endl;
            return false;
        }
    }
    return true;
}

inline std::string trim_string(const std::string& str) {
    const char* whitespace_chars = " \t\n\r\f\v\xC2\xA0";
    const auto str_begin = str.find_first_not_of(whitespace_chars);
    if (str_begin == std::string::npos) return "";
    const auto str_end = str.find_last_not_of(whitespace_chars);
    const auto str_range = str_end - str_begin + 1;
    return str.substr(str_begin, str_range);
}

inline std::string to_lower_util(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return s;
}

inline void ensure_directory_exists_util(const std::string& path_str) {
    if (path_str.empty()) return;
    std::string path_to_check = path_str;
    size_t last_slash_idx = path_str.find_last_of("/\\");

    if (last_slash_idx != std::string::npos) {
        path_to_check = path_str.substr(0, last_slash_idx);
        if (path_to_check.empty() && last_slash_idx == 0) {
            return;
        }
         if (path_to_check.empty()) {
             return;
         }
    } else {
        return;
    }
    
    if (path_to_check.empty()) return;

    #if defined(_WIN32) || defined(_WIN64)
        DWORD ftyp = GetFileAttributesA(path_to_check.c_str());
        if (ftyp == INVALID_FILE_ATTRIBUTES) {
            if (CreateDirectoryA(path_to_check.c_str(), NULL) == 0) {
                // std::cerr << "Warning: Failed to create directory (CreateDirectoryA): " << path_to_check << " Error: " << GetLastError() << std::endl;
            }
        } else if (!(ftyp & FILE_ATTRIBUTE_DIRECTORY)) {
            // std::cerr << "Warning: Path exists but is not a directory (Windows): " << path_to_check << std::endl;
        }
    #else // POSIX
        std::string command = "mkdir -p \"" + path_to_check + "\"";
        int ret = system(command.c_str());
        if (ret != 0) {
            // std::cerr << "Warning: mkdir -p command failed for path (POSIX): " << path_to_check << " with code " << ret << std::endl;
        }
    #endif
}

inline std::string get_current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm{};
#ifdef _WIN32
    localtime_s(&now_tm, &now_c);
#else
    localtime_r(&now_c, &now_tm);
#endif
    std::stringstream ss;
    ss << std::put_time(&now_tm, "%Y-%m-%d %H:%M:%S");
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

#endif // COMMON_DEFS_H

