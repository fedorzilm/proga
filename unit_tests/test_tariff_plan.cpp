// unit_tests/test_tariff_plan.cpp
#include "gtest/gtest.h"
#include "tariff_plan.h"
#include "common_defs.h" 
#include "file_utils.h"  
#include "test_utils.h" // ИСПРАВЛЕНИЕ: Подключаем новый заголовочный файл
#include <fstream>
#include <vector>
#include <string>
#include <limits>   
#include <iomanip>  
#include <filesystem> 

// ИСПРАВЛЕНИЕ: Локальное определение create_test_tariff_file удалено.

class TariffPlanTest : public ::testing::Test {
protected:
    std::string test_dir_path;
    std::string test_tariff_filename;

    void SetUp() override {
        test_dir_path = "./test_tariffs_temp_TariffPlanTest_v2"; 
        std::filesystem::create_directories(test_dir_path); 
        test_tariff_filename = test_dir_path + "/temp_tariff.cfg";
    }

    void TearDown() override {
        if (std::filesystem::exists(test_dir_path)) {
             std::filesystem::remove_all(test_dir_path); 
        }
    }
    // Helper для генерации валидных строк для файла тарифов (без маркеров IN_RATES/OUT_RATES)
    std::vector<std::string> generate_valid_tariff_lines_for_test(double base_rate_in = 0.1, double base_rate_out = 0.05) {
        std::vector<std::string> lines;
        std::string line_in_str, line_out_str;
        for (int i = 0; i < HOURS_IN_DAY; ++i) {
            line_in_str += std::to_string(base_rate_in + static_cast<double>(i) * 0.01) + (i == HOURS_IN_DAY - 1 ? "" : " ");
            line_out_str += std::to_string(base_rate_out + static_cast<double>(i) * 0.005) + (i == HOURS_IN_DAY - 1 ? "" : " ");
        }
        lines.push_back(line_in_str); // Одна строка для IN_RATES
        lines.push_back(line_out_str); // Одна строка для OUT_RATES
        return lines;
    }
};


TEST_F(TariffPlanTest, DefaultConstructor) {
    TariffPlan tp;
    for (int i = 0; i < HOURS_IN_DAY; ++i) {
        EXPECT_DOUBLE_EQ(tp.getCostInForHour(i), 0.0);
        EXPECT_DOUBLE_EQ(tp.getCostOutForHour(i), 0.0);
    }
}

TEST_F(TariffPlanTest, LoadFromFile_ValidFile_AllOnOneLinePerDirection) {
    // Используем новый хелпер, который не добавляет "IN_RATES:"
    auto lines = generate_valid_tariff_lines_for_test(0.2, 0.1);
    create_test_tariff_file(test_tariff_filename, lines); // Эта функция из test_utils.h

    TariffPlan tp;
    EXPECT_TRUE(tp.loadFromFile(test_tariff_filename));
    for (int i = 0; i < HOURS_IN_DAY; ++i) {
        EXPECT_NEAR(tp.getCostInForHour(i), 0.2 + static_cast<double>(i) * 0.01, DOUBLE_EPSILON);
        EXPECT_NEAR(tp.getCostOutForHour(i), 0.1 + static_cast<double>(i) * 0.005, DOUBLE_EPSILON);
    }
}

TEST_F(TariffPlanTest, LoadFromFile_ValidFile_RatesOnSeparateLines) {
    std::vector<std::string> lines;
    // НЕ добавляем "IN_RATES:" / "OUT_RATES:"
    for (int i = 0; i < HOURS_IN_DAY; ++i) { lines.push_back(std::to_string(0.10 * (i + 1))); }
    for (int i = 0; i < HOURS_IN_DAY; ++i) { lines.push_back(std::to_string(0.05 * (i + 1))); }
    create_test_tariff_file(test_tariff_filename, lines);
    
    TariffPlan tp;
    EXPECT_TRUE(tp.loadFromFile(test_tariff_filename));
     for (int i = 0; i < HOURS_IN_DAY; ++i) {
        EXPECT_DOUBLE_EQ(tp.getCostInForHour(i), 0.10 * (i + 1));
        EXPECT_DOUBLE_EQ(tp.getCostOutForHour(i), 0.05 * (i + 1));
    }
}


TEST_F(TariffPlanTest, LoadFromFile_FileNotExists) {
    TariffPlan tp;
    EXPECT_THROW(tp.loadFromFile("non_existent_tariff.cfg"), std::runtime_error);
}

TEST_F(TariffPlanTest, LoadFromFile_TooFewRates) {
    create_test_tariff_file(test_tariff_filename, {"0.1 0.2", "0.05"}); // Без маркеров
    TariffPlan tp;
    EXPECT_THROW(tp.loadFromFile(test_tariff_filename), std::invalid_argument);
}

TEST_F(TariffPlanTest, LoadFromFile_TooManyRates) {
    std::string in_rates;
    for (int i = 0; i < HOURS_IN_DAY + 1; ++i) in_rates += "0.1 "; 
    std::string out_rates;
     for (int i = 0; i < HOURS_IN_DAY; ++i) out_rates += "0.05 "; 
    create_test_tariff_file(test_tariff_filename, {in_rates, out_rates}); // Без маркеров
    TariffPlan tp;
    EXPECT_THROW(tp.loadFromFile(test_tariff_filename), std::invalid_argument);
}

TEST_F(TariffPlanTest, LoadFromFile_NonNumericRate) {
    std::vector<std::string> lines = generate_valid_tariff_lines_for_test();
    lines[0] = "0.1 abc 0.3 " + lines[0].substr(lines[0].find(' ', lines[0].find(' ') + 1) + 1);
    create_test_tariff_file(test_tariff_filename, lines); // Без маркеров
    TariffPlan tp;
    EXPECT_THROW(tp.loadFromFile(test_tariff_filename), std::invalid_argument);
}

TEST_F(TariffPlanTest, LoadFromFile_NegativeRate) {
    std::vector<std::string> lines;
    for(int i=0; i<HOURS_IN_DAY -1; ++i) lines.push_back("0.1"); // IN rates
    lines.push_back("-0.1"); // Negative rate in IN
    for(int i=0; i<HOURS_IN_DAY; ++i) lines.push_back("0.05"); // OUT rates
    create_test_tariff_file(test_tariff_filename, lines);
    TariffPlan tp;
    EXPECT_THROW(tp.loadFromFile(test_tariff_filename), std::invalid_argument);
}

TEST_F(TariffPlanTest, LoadFromFile_WithCommentsAndBlankLines) {
    std::vector<std::string> data_lines_in, data_lines_out;
    for(int i = 0; i < 2; ++i) { // Два блока по 12 чисел
        std::string temp_in, temp_out;
        for(int j=0; j < 12; ++j) {
            temp_in += std::to_string(0.10 + (i*12+j)*0.01) + (j==11 ? "" : " ");
            temp_out += std::to_string(0.05 + (i*12+j)*0.005) + (j==11 ? "" : " ");
        }
        data_lines_in.push_back(temp_in);
        data_lines_out.push_back(temp_out);
    }
    std::vector<std::string> file_content = {
        "#This is a comment",
        data_lines_in[0], 
        "", 
        data_lines_in[1] + " #inline comment",
        "# another comment",
        data_lines_out[0],
        data_lines_out[1]
    };
    create_test_tariff_file(test_tariff_filename, file_content);
    TariffPlan tp;
    EXPECT_TRUE(tp.loadFromFile(test_tariff_filename));
    EXPECT_DOUBLE_EQ(tp.getCostInForHour(0), 0.10);
    EXPECT_DOUBLE_EQ(tp.getCostOutForHour(23), 0.05 + (23)*0.005);
}

TEST_F(TariffPlanTest, LoadFromFile_ExtraTextOnRateLine) {
    std::vector<std::string> lines = generate_valid_tariff_lines_for_test();
    lines[0] += " extra_text_should_fail"; 
    create_test_tariff_file(test_tariff_filename, lines);
    TariffPlan tp;
    EXPECT_THROW(tp.loadFromFile(test_tariff_filename), std::invalid_argument);
}

TEST_F(TariffPlanTest, GetCostForHour_InvalidHour) {
    TariffPlan tp;
    EXPECT_THROW(tp.getCostInForHour(-1), std::out_of_range);
    EXPECT_THROW(tp.getCostOutForHour(HOURS_IN_DAY), std::out_of_range);
}

TEST_F(TariffPlanTest, GetCostForHour_AfterSuccessfulLoad) {
    auto lines = generate_valid_tariff_lines_for_test(0.123, 0.067);
    create_test_tariff_file(test_tariff_filename, lines);
    TariffPlan tp;
    tp.loadFromFile(test_tariff_filename);
    EXPECT_NEAR(tp.getCostInForHour(0), 0.123, DOUBLE_EPSILON);
    EXPECT_NEAR(tp.getCostOutForHour(HOURS_IN_DAY - 1), 0.067 + (HOURS_IN_DAY - 1) * 0.005, DOUBLE_EPSILON);
}

TEST_F(TariffPlanTest, LoadFromFile_RatesAlmostZero) {
    // "Облегченная" версия: Тестируем только очень малые ПОЛОЖИТЕЛЬНЫЕ тарифы.
    std::ostringstream in_rates_line_oss, out_rates_line_oss;
    std::vector<double> expected_in_values, expected_out_values;

    for (int i = 0; i < HOURS_IN_DAY; ++i) {
        double positive_small_in = 0.0000000001 * (i + 1.0); 
        double positive_small_out = 0.0000000002 * (i + 1.0);
        expected_in_values.push_back(positive_small_in);
        expected_out_values.push_back(positive_small_out);
        in_rates_line_oss << std::fixed << std::setprecision(12) << positive_small_in << (i == HOURS_IN_DAY - 1 ? "" : " ");
        out_rates_line_oss << std::fixed << std::setprecision(12) << positive_small_out << (i == HOURS_IN_DAY - 1 ? "" : " ");
    }
    create_test_tariff_file(test_tariff_filename, {in_rates_line_oss.str(), out_rates_line_oss.str()});
    
    TariffPlan tp;
    // Ожидаем, что загрузка малых ПОЛОЖИТЕЛЬНЫХ тарифов НЕ вызовет исключения.
    EXPECT_NO_THROW(tp.loadFromFile(test_tariff_filename)) 
        << "Загрузка малых положительных тарифов не должна вызывать исключений.";

    // Дополнительная проверка, что значения загружены корректно
    TariffPlan tp_verify; 
    bool load_ok = false;
    try {
        load_ok = tp_verify.loadFromFile(test_tariff_filename);
    } catch (const std::exception& e) {
        FAIL() << "Неожиданное исключение при повторной загрузке для проверки: " << e.what();
    }
    ASSERT_TRUE(load_ok) << "TariffPlan::loadFromFile вернул false для корректных малых положительных тарифов.";

    for (int i = 0; i < HOURS_IN_DAY; ++i) {
        EXPECT_DOUBLE_EQ(tp_verify.getCostInForHour(i), expected_in_values[static_cast<size_t>(i)]);
        EXPECT_DOUBLE_EQ(tp_verify.getCostOutForHour(i), expected_out_values[static_cast<size_t>(i)]);
    }
}

TEST_F(TariffPlanTest, LoadFromFile_MaxDoubleValue) {
    std::ostringstream in_rates_line_oss, out_rates_line_oss;
    double max_val_scaled = std::numeric_limits<double>::max() / (HOURS_IN_DAY + 100.0); // Масштабируем, чтобы избежать проблем с суммой или строковым представлением
     if (max_val_scaled > 1e100) max_val_scaled = 1e100; // Дополнительное ограничение для разумности строк

    for (int i = 0; i < HOURS_IN_DAY; ++i) {
        in_rates_line_oss << std::scientific << std::setprecision(std::numeric_limits<double>::digits10 + 1) 
                          << max_val_scaled << (i == HOURS_IN_DAY - 1 ? "" : " ");
        out_rates_line_oss << std::scientific << std::setprecision(std::numeric_limits<double>::digits10 + 1) 
                           << max_val_scaled << (i == HOURS_IN_DAY - 1 ? "" : " ");
    }
    create_test_tariff_file(test_tariff_filename, {in_rates_line_oss.str(), out_rates_line_oss.str()});
    
    TariffPlan tp;
    EXPECT_TRUE(tp.loadFromFile(test_tariff_filename)); // Ожидаем, что загрузка пройдет успешно
    EXPECT_NEAR(tp.getCostInForHour(0), max_val_scaled, max_val_scaled * 1e-9); // Допуск для больших чисел
}
