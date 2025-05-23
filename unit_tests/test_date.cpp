#include "gtest/gtest.h"
#include "date.h"       
#include <sstream>      

class DateTest : public ::testing::Test {
protected:
};

TEST_F(DateTest, DefaultConstructor) {
    Date d;
    EXPECT_EQ(d.getDay(), 1);
    EXPECT_EQ(d.getMonth(), 1);
    EXPECT_EQ(d.getYear(), 1970);
}

TEST_F(DateTest, ParameterizedConstructor_ValidDates) {
    EXPECT_NO_THROW(Date(1, 1, 2000));
    Date d1(15, 10, 2023);
    EXPECT_EQ(d1.getDay(), 15);
    EXPECT_EQ(d1.getMonth(), 10);
    EXPECT_EQ(d1.getYear(), 2023);

    // Високосный год, 29 февраля (косвенно тестирует isLeap)
    EXPECT_NO_THROW(Date(29, 2, 2024));
    Date d_leap(29, 2, 2024);
    EXPECT_EQ(d_leap.getDay(), 29);
    EXPECT_EQ(d_leap.getMonth(), 2);
    EXPECT_EQ(d_leap.getYear(), 2024);

    // Невисокосный год, 28 февраля (косвенно тестирует isLeap)
    EXPECT_NO_THROW(Date(28, 2, 2023));
    Date d_non_leap(28, 2, 2023);
    EXPECT_EQ(d_non_leap.getDay(), 28);

    // Проверка граничных годов (если они валидны по определению в Date::validate)
    EXPECT_NO_THROW(Date(1, 1, 1900)); // 1900 не високосный
    EXPECT_NO_THROW(Date(28, 2, 1900));
    EXPECT_NO_THROW(Date(1, 1, 2100)); // 2100 не високосный
    EXPECT_NO_THROW(Date(28, 2, 2100));
    EXPECT_NO_THROW(Date(29, 2, 2000)); // 2000 високосный
    EXPECT_NO_THROW(Date(29, 2, 2096)); // 2096 високосный
}

TEST_F(DateTest, ParameterizedConstructor_InvalidDates_Throws) {
    // Неверный день
    EXPECT_THROW(Date(0, 1, 2000), std::invalid_argument);
    EXPECT_THROW(Date(32, 1, 2000), std::invalid_argument);
    EXPECT_THROW(Date(31, 4, 2000), std::invalid_argument); // 30 дней в апреле
    EXPECT_THROW(Date(29, 2, 2023), std::invalid_argument); // 2023 не високосный (косвенно isLeap)
    EXPECT_THROW(Date(29, 2, 1900), std::invalid_argument); // 1900 не високосный (косвенно isLeap)
    EXPECT_THROW(Date(30, 2, 2024), std::invalid_argument); // В феврале не бывает 30 дней, даже в високосный

    // Неверный месяц
    EXPECT_THROW(Date(1, 0, 2000), std::invalid_argument);
    EXPECT_THROW(Date(1, 13, 2000), std::invalid_argument);

    // Неверный год (согласно ограничениям MIN_YEAR=1900, MAX_YEAR=2100 в Date::validate)
    EXPECT_THROW(Date(1, 1, 1899), std::invalid_argument);
    EXPECT_THROW(Date(1, 1, 2101), std::invalid_argument);
}


TEST_F(DateTest, ToStringConversion) {
    Date d1(5, 3, 2021);
    EXPECT_EQ(d1.toString(), "05.03.2021");

    Date d2(25, 12, 1999);
    EXPECT_EQ(d2.toString(), "25.12.1999");

    Date d3(1, 1, 1970); // Значение по умолчанию
    EXPECT_EQ(d3.toString(), "01.01.1970");
}

TEST_F(DateTest, ComparisonOperators) {
    Date d1(1, 1, 2023);
    Date d2(2, 1, 2023);
    Date d3(1, 2, 2023);
    Date d4(1, 1, 2024);
    Date d5(1, 1, 2023); // Равна d1

    // ==
    EXPECT_TRUE(d1 == d5);
    EXPECT_FALSE(d1 == d2);

    // !=
    EXPECT_TRUE(d1 != d2);
    EXPECT_FALSE(d1 != d5);

    // <
    EXPECT_TRUE(d1 < d2); // День меньше
    EXPECT_TRUE(d1 < d3); // Месяц меньше
    EXPECT_TRUE(d1 < d4); // Год меньше
    EXPECT_FALSE(d2 < d1);
    EXPECT_FALSE(d1 < d1);

    // >
    EXPECT_TRUE(d2 > d1);
    EXPECT_TRUE(d3 > d1);
    EXPECT_TRUE(d4 > d1);
    EXPECT_FALSE(d1 > d2);
    EXPECT_FALSE(d1 > d1);

    // <=
    EXPECT_TRUE(d1 <= d5);
    EXPECT_TRUE(d1 <= d2);
    EXPECT_FALSE(d2 <= d1);

    // >=
    EXPECT_TRUE(d1 >= d5);
    EXPECT_TRUE(d2 >= d1);
    EXPECT_FALSE(d1 >= d2);
}

TEST_F(DateTest, StreamOutput) {
    Date d(7, 8, 2022);
    std::ostringstream oss;
    oss << d;
    EXPECT_EQ(oss.str(), "07.08.2022");
}

TEST_F(DateTest, StreamInput_Valid) {
    std::istringstream iss_valid("09.11.2023");
    Date d_valid;
    iss_valid >> d_valid;
    EXPECT_FALSE(iss_valid.fail());
    EXPECT_EQ(d_valid.getDay(), 9);
    EXPECT_EQ(d_valid.getMonth(), 11);
    EXPECT_EQ(d_valid.getYear(), 2023);

    std::istringstream iss_spacing("  25.01.1995  "); // С пробелами
    Date d_spacing;
    iss_spacing >> d_spacing;
    EXPECT_FALSE(iss_spacing.fail());
    EXPECT_EQ(d_spacing.getDay(), 25);
    EXPECT_EQ(d_spacing.getMonth(), 1);
    EXPECT_EQ(d_spacing.getYear(), 1995);
}

TEST_F(DateTest, StreamInput_InvalidFormat) {
    Date d_invalid;
    std::istringstream iss_bad_format1("09-11-2023"); // Неверный разделитель
    iss_bad_format1 >> d_invalid;
    EXPECT_TRUE(iss_bad_format1.fail());

    d_invalid = Date(); // Сброс
    std::istringstream iss_bad_format2("9.11.2023"); // День не двузначный, но парсер должен справиться
    iss_bad_format2 >> d_invalid;
    EXPECT_FALSE(iss_bad_format2.fail());
    EXPECT_EQ(d_invalid.getDay(), 9);


    d_invalid = Date();
    std::istringstream iss_not_enough_parts("09.11");
    iss_not_enough_parts >> d_invalid;
    EXPECT_TRUE(iss_not_enough_parts.fail());

    d_invalid = Date();
    std::istringstream iss_non_numeric("aa.bb.cccc");
    iss_non_numeric >> d_invalid;
    EXPECT_TRUE(iss_non_numeric.fail());

    d_invalid = Date();
    std::istringstream iss_extra_chars("01.01.2000extra");
    iss_extra_chars >> d_invalid;
    EXPECT_TRUE(iss_extra_chars.fail());
}

TEST_F(DateTest, StreamInput_InvalidDateValue) {
    Date d_invalid_val;
    std::istringstream iss_bad_day("32.01.2023");
    iss_bad_day >> d_invalid_val;
    EXPECT_TRUE(iss_bad_day.fail());

    d_invalid_val = Date();
    std::istringstream iss_bad_month("01.13.2023");
    iss_bad_month >> d_invalid_val;
    EXPECT_TRUE(iss_bad_month.fail());

    d_invalid_val = Date();
    std::istringstream iss_bad_year("01.01.1800");
    iss_bad_year >> d_invalid_val;
    EXPECT_TRUE(iss_bad_year.fail());

    d_invalid_val = Date();
    std::istringstream iss_bad_leap("29.02.2023"); // 2023 не високосный
    iss_bad_leap >> d_invalid_val;
    EXPECT_TRUE(iss_bad_leap.fail());
}

TEST_F(DateTest, StreamInput_EmptyStream) {
    Date d;
    std::istringstream empty_ss("");
    empty_ss >> d;
    EXPECT_TRUE(empty_ss.fail());
}
