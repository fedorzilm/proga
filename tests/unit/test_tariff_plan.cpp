// В файле test_tariff_plan.cpp
#include "gtest/gtest.h"
#include "tariff_plan.h" // Путь к вашему tariff_plan.h
#include "common_defs.h" // Для HOURS_IN_DAY
#include <fstream>
#include <vector>
#include <string>
#include <cstdio> // для std::remove
#include <filesystem> // Для работы с временными файлами (C++17)

// Вспомогательная функция для создания тестового файла тарифов
// Использует filesystem для создания файла во временной директории системы
std::string create_temp_tariff_file_tp(const std::vector<std::string>& lines, const std::string& base_name = "test_tariff_") {
    // Создаем уникальное имя файла во временной директории
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path();
    std::filesystem::path file_path;
    
    // Простой способ генерации уникального имени (можно улучшить)
    // Для тестов обычно достаточно фиксированных имен, если они удаляются в TearDown фикстуры.
    // Но так как здесь нет фикстуры, сделаем так.
    // Или можно передавать полное имя файла в функцию.
    // Для простоты GTest, где каждый TEST - независим, будем использовать фиксированные имена с последующим удалением.
    // Эта функция была перенесена из предыдущих примеров, где она была в фикстуре.
    // Здесь она будет создавать файл в текущей директории.
    
    static int file_counter = 0; // Статический счетчик для уникальности имен при нескольких вызовах в одном тестовом приложении
    std::string filename = base_name + std::to_string(file_counter++) + ".cfg";


    std::ofstream ofs(filename);
    if (!ofs.is_open()) { // Используем ASSERT_... только внутри макросов TEST...
        // Вспомогательная функция не должна использовать ASSERT, лучше бросить исключение или вернуть bool
        throw std::runtime_error("Failed to create test file: " + filename);
    }
    for (const auto& line : lines) {
        ofs << line << std::endl;
    }
    ofs.close();
    return filename; // Возвращаем имя созданного файла
}


TEST(TariffPlanTest, LoadValidFile) {
    std::vector<std::string> lines;
    for (int i = 0; i < HOURS_IN_DAY; ++i) lines.push_back(std::to_string(0.1 * i));
    std::string filename = create_temp_tariff_file_tp(lines, "valid_tariff_");

    TariffPlan tp;
    ASSERT_TRUE(tp.load_from_file(filename));
    for (int i = 0; i < HOURS_IN_DAY; ++i) {
        EXPECT_DOUBLE_EQ(tp.get_rate(i), 0.1 * i);
    }
    std::remove(filename.c_str());
}

TEST(TariffPlanTest, LoadInvalidCountNotEnough) { 
    std::vector<std::string> lines; 
    for (int i = 0; i < HOURS_IN_DAY - 1; ++i) lines.push_back("1.0");
    std::string filename = create_temp_tariff_file_tp(lines, "invalid_count_not_enough_");

    TariffPlan tp;
    EXPECT_FALSE(tp.load_from_file(filename)); 
    std::remove(filename.c_str());
}

TEST(TariffPlanTest, LoadFileWithTooManyRates) {
    std::vector<std::string> lines;
    for (int i = 0; i < HOURS_IN_DAY + 1; ++i) { 
        lines.push_back(std::to_string(0.1 * i));
    }
    std::string filename = create_temp_tariff_file_tp(lines, "too_many_rates_");

    TariffPlan tp;
    EXPECT_FALSE(tp.load_from_file(filename)); 
    std::remove(filename.c_str());
}


TEST(TariffPlanTest, LoadInvalidFormat) {
    std::vector<std::string> lines;
    for (int i = 0; i < HOURS_IN_DAY -1; ++i) lines.push_back("1.0");
    lines.push_back("not_a_number"); 
    std::string filename = create_temp_tariff_file_tp(lines, "invalid_format_");

    TariffPlan tp;
    EXPECT_FALSE(tp.load_from_file(filename));
    std::remove(filename.c_str());
}

TEST(TariffPlanTest, LoadNegativeRate) {
    std::vector<std::string> lines;
    for (int i = 0; i < HOURS_IN_DAY -1; ++i) lines.push_back("1.0");
    lines.push_back("-0.5"); 
    std::string filename = create_temp_tariff_file_tp(lines, "negative_rate_");

    TariffPlan tp;
    EXPECT_FALSE(tp.load_from_file(filename));
    std::remove(filename.c_str());
}

TEST(TariffPlanTest, LoadFileWithEmptyAndWhitespaceLinesAndComments) {
    std::vector<std::string> lines_tp_complex; // Переименована для ясности
    int valid_rates_added_count = 0; // Переименована
    for (int i = 0; i < HOURS_IN_DAY; ++i) {
        if (i % 5 == 0 && i < 10) lines_tp_complex.push_back(""); // Пустая строка (добавим несколько, но не слишком много, чтобы не раздувать файл)
        if (i % 3 == 0 && i < 10) lines_tp_complex.push_back("   "); // Строка с пробелами
        if (i % 4 == 0 && i < 10) lines_tp_complex.push_back("  # comment only line"); // Строка только с комментарием
        
        lines_tp_complex.push_back(std::to_string(0.2 * i) + " # rate for hour " + std::to_string(i));
        valid_rates_added_count++;
    }
    ASSERT_EQ(valid_rates_added_count, HOURS_IN_DAY); // Убедимся, что мы сформировали нужное количество валидных ставок.

    std::string filename = create_temp_tariff_file_tp(lines_tp_complex, "empty_whitespace_lines_");

    TariffPlan tp;
    ASSERT_TRUE(tp.load_from_file(filename)) << "Failed to load tariff file with empty/whitespace/comment lines. File content:\n---\n"
                                            // Можно добавить вывод содержимого файла для отладки, если тест падает
                                            // std::ifstream ifs(filename); std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>()); ifs.close();
                                            // << content << "\n---";
                                            << "(File content not printed here for brevity)";


    for (int i = 0; i < HOURS_IN_DAY; ++i) {
         EXPECT_DOUBLE_EQ(tp.get_rate(i), 0.2 * i);
    }
    std::remove(filename.c_str());
}


TEST(TariffPlanTest, GetRateInvalidHour) {
    TariffPlan tp; // Загружен по умолчанию нулями (согласно конструктору TariffPlan())
    EXPECT_DOUBLE_EQ(tp.get_rate(-1), 0.0);
    EXPECT_DOUBLE_EQ(tp.get_rate(HOURS_IN_DAY), 0.0);
    EXPECT_DOUBLE_EQ(tp.get_rate(HOURS_IN_DAY + 100), 0.0);
}

TEST(TariffPlanTest, LoadFileWithTrailingCharactersAfterRate) {
    std::vector<std::string> lines;
    for (int i = 0; i < HOURS_IN_DAY - 1; ++i) lines.push_back(std::to_string(0.1 * i));
    lines.push_back("1.5 extra_text"); // Лишние символы после числа (не комментарий)
    std::string filename = create_temp_tariff_file_tp(lines, "trailing_chars_");
                                      
    TariffPlan tp;
    // Ваша реализация TariffPlan::load_from_file проверяет: `!(ss_line >> rate_value) || !(ss_line >> std::ws).eof()`
    // Это означает, что если после числа и пробелов что-то есть, eof() не будет true, и загрузка вернет false.
    EXPECT_FALSE(tp.load_from_file(filename)); 
    std::remove(filename.c_str());
}

TEST(TariffPlanTest, LoadFileNonExistent) {
    TariffPlan tp;
    EXPECT_FALSE(tp.load_from_file("non_existent_tariff_file_qwerty.cfg"));
    // Проверяем, что тарифы остались дефолтными (нулевыми)
    for (int i = 0; i < HOURS_IN_DAY; ++i) {
        EXPECT_DOUBLE_EQ(tp.get_rate(i), 0.0);
    }
}
