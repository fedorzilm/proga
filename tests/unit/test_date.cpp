#include "gtest/gtest.h"
#include "date.h" // Убедитесь, что путь к вашему date.h правильный

TEST(DateTest, DefaultConstructor) {
    Date d;
    EXPECT_EQ(d.year, 0); // Или какие у вас значения по умолчанию
    EXPECT_EQ(d.month, 0);
    EXPECT_EQ(d.day, 0);
    EXPECT_FALSE(d.is_valid()); // Дата по умолчанию обычно невалидна
}

TEST(DateTest, ValidConstruction) {
    Date d(2024, 5, 9);
    ASSERT_TRUE(d.is_valid());
    EXPECT_EQ(d.year, 2024);
    EXPECT_EQ(d.month, 5);
    EXPECT_EQ(d.day, 9);
}

TEST(DateTest, InvalidConstructionValues) {
    Date d1(2024, 13, 1); // Неверный месяц
    EXPECT_FALSE(d1.is_valid());
    Date d2(2024, 2, 30); // Неверный день для февраля не високосного года (если бы 2024 не был)
                          // Для 2024 (високосный), 29 валидно, 30 невалидно
    EXPECT_FALSE(d2.is_valid());
    Date d3(2023, 2, 29); // 2023 не високосный
    EXPECT_FALSE(d3.is_valid());
    Date d4(0, 1, 1);    // Невалидный год
    EXPECT_FALSE(d4.is_valid());
}

TEST(DateTest, FromStringValid) {
    auto d_opt = Date::from_string("09/05/2024");
    ASSERT_TRUE(d_opt.has_value());
    EXPECT_EQ(d_opt->year, 2024);
    EXPECT_EQ(d_opt->month, 5);
    EXPECT_EQ(d_opt->day, 9);
    EXPECT_TRUE(d_opt->is_valid());

    auto d_leap = Date::from_string("29/02/2024"); // Високосный
    ASSERT_TRUE(d_leap.has_value());
    EXPECT_EQ(d_leap->day, 29);
    EXPECT_TRUE(d_leap->is_valid());

    auto d_end_of_month = Date::from_string("31/01/2023");
    ASSERT_TRUE(d_end_of_month.has_value());
    EXPECT_EQ(d_end_of_month->day, 31);
    EXPECT_TRUE(d_end_of_month->is_valid());
}

TEST(DateTest, FromStringInvalidFormat) {
    EXPECT_FALSE(Date::from_string("09-05-2024").has_value()); // Неверный разделитель
    EXPECT_FALSE(Date::from_string("9/5/2024").has_value());   // Неполные компоненты
    EXPECT_FALSE(Date::from_string("09/05/24").has_value());   // Год не 4 цифры
    EXPECT_FALSE(Date::from_string("09/May/2024").has_value());// Месяц не числом
    EXPECT_FALSE(Date::from_string("").has_value());
    EXPECT_FALSE(Date::from_string("01/02/20231").has_value());
    EXPECT_FALSE(Date::from_string("aa/bb/cccc").has_value());
}

TEST(DateTest, FromStringInvalidValues) {
    EXPECT_FALSE(Date::from_string("32/01/2024").has_value()); // Неверный день
    EXPECT_FALSE(Date::from_string("00/01/2024").has_value()); // Неверный день (0)
    EXPECT_FALSE(Date::from_string("01/13/2024").has_value()); // Неверный месяц
    EXPECT_FALSE(Date::from_string("01/00/2024").has_value()); // Неверный месяц (0)
    EXPECT_FALSE(Date::from_string("29/02/2023").has_value()); // Февраль не високосного
    EXPECT_FALSE(Date::from_string("31/04/2024").has_value()); // 31 день в апреле
    EXPECT_FALSE(Date::from_string("01/01/0000").has_value()); // Невалидный год
}


TEST(DateTest, ToString) {
    Date d(2024, 5, 9);
    EXPECT_EQ(d.to_string(), "09/05/2024");
    Date d2(2023, 12, 31);
    EXPECT_EQ(d2.to_string(), "31/12/2023");
    Date d3(2025, 1, 1);
    EXPECT_EQ(d3.to_string(), "01/01/2025");
}

TEST(DateTest, ComparisonOperators) {
    Date d1(2024, 1, 1), d2(2024, 1, 2), d3(2023, 12, 31), d4(2024, 1, 1);
    EXPECT_TRUE(d1 < d2);
    EXPECT_FALSE(d2 < d1);
    EXPECT_TRUE(d3 < d1);
    EXPECT_FALSE(d1 < d3);

    EXPECT_TRUE(d1 == d4);
    EXPECT_FALSE(d1 == d2);

    EXPECT_TRUE(d1 != d2);
    EXPECT_FALSE(d1 != d4);

    EXPECT_TRUE(d2 > d1);
    EXPECT_FALSE(d1 > d2);

    EXPECT_TRUE(d1 <= d2);
    EXPECT_TRUE(d1 <= d4);
    EXPECT_FALSE(d2 <= d1);

    EXPECT_TRUE(d2 >= d1);
    EXPECT_TRUE(d4 >= d1);
    EXPECT_FALSE(d1 >= d2);
}

TEST(DateTest, DaysInMonthAndLeapYearStatic) {
    EXPECT_EQ(Date::days_in_month(2024, 2), 29);
    EXPECT_TRUE(Date::is_leap(2024));

    EXPECT_EQ(Date::days_in_month(2023, 2), 28);
    EXPECT_FALSE(Date::is_leap(2023));

    EXPECT_EQ(Date::days_in_month(2000, 2), 29); // Високосный кратный 400
    EXPECT_TRUE(Date::is_leap(2000));

    EXPECT_EQ(Date::days_in_month(1900, 2), 28); // Невисокосный кратный 100
    EXPECT_FALSE(Date::is_leap(1900));

    EXPECT_EQ(Date::days_in_month(2023, 1), 31);
    EXPECT_EQ(Date::days_in_month(2023, 4), 30);
    EXPECT_EQ(Date::days_in_month(2023, 12), 31);

    EXPECT_EQ(Date::days_in_month(2023, 0), 0); // Невалидный месяц
    EXPECT_EQ(Date::days_in_month(2023, 13), 0); // Невалидный месяц
    EXPECT_EQ(Date::days_in_month(0, 1), 0); // Невалидный год
}

TEST(DateTest, StreamOperators) {
    Date d_orig(2025, 7, 19);
    std::stringstream ss;
    ss << d_orig;
    EXPECT_EQ(ss.str(), "19/07/2025");

    Date d_read;
    ss.seekg(0); // Reset stream for reading
    ss >> d_read;
    ASSERT_FALSE(ss.fail()); // Check if read was successful
    EXPECT_EQ(d_read, d_orig);

    std::stringstream ss_invalid("invalid-date");
    Date d_invalid_read;
    ss_invalid >> d_invalid_read;
    EXPECT_TRUE(ss_invalid.fail()); // Read should fail
}
