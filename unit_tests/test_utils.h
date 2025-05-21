#ifndef UNIT_TESTS_TEST_UTILS_H
#define UNIT_TESTS_TEST_UTILS_H

#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>    // For std::runtime_error
#include "provider_record.h" // For ProviderRecord in create_records_file_for_db

// Helper functions marked inline as they are defined in a header file

inline void create_test_tariff_file(const std::string& filename, const std::vector<std::string>& lines) {
    std::filesystem::path file_path(filename);
    if (file_path.has_parent_path()) {
        std::filesystem::create_directories(file_path.parent_path());
    }
    std::ofstream outfile(filename);
    if (!outfile.is_open()) {
        throw std::runtime_error("Test Util: Could not open file for writing: " + filename);
    }
    for (const auto& line : lines) {
        outfile << line << std::endl;
    }
    outfile.close();
}

// Renamed to avoid conflict if test_database.cpp also had a similar one
inline void create_records_file_for_db_test(const std::string& filename, const std::vector<ProviderRecord>& records) {
    std::filesystem::path file_path(filename);
    if (file_path.has_parent_path()) {
        std::filesystem::create_directories(file_path.parent_path());
    }
    std::ofstream outfile(filename);
     if (!outfile.is_open()) {
        throw std::runtime_error("Test Util: Could not open file for writing: " + filename);
    }
    for (size_t i = 0; i < records.size(); ++i) {
        outfile << records[i];
        if (i < records.size() - 1) {
            outfile << "\n"; 
        }
    }
    outfile.close();
}

inline void create_text_file_for_db_test(const std::string& filename, const std::string& content) {
    std::filesystem::path file_path(filename);
    if (file_path.has_parent_path()) {
        std::filesystem::create_directories(file_path.parent_path());
    }
    std::ofstream outfile(filename);
    if (!outfile.is_open()) {
        throw std::runtime_error("Test Util: Could not open file for writing: " + filename);
    }
    outfile << content;
    outfile.close();
}

#endif // UNIT_TESTS_TEST_UTILS_H
