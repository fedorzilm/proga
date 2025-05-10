#include "gtest/gtest.h"
#include "ip_address.h" // Убедитесь, что путь правильный
#include <sstream>

TEST(IpAddressTest, DefaultConstructor) {
    IpAddress ip;
    EXPECT_EQ(ip.address_str, "");
    EXPECT_FALSE(ip.is_valid());
}

TEST(IpAddressTest, ValidConstructionAndToString) {
    IpAddress ip("192.168.1.1");
    ASSERT_TRUE(ip.is_valid());
    EXPECT_EQ(ip.to_string(), "192.168.1.1");
}

TEST(IpAddressTest, FromStringValid) {
    auto ip_opt = IpAddress::from_string("10.0.0.255");
    ASSERT_TRUE(ip_opt.has_value());
    EXPECT_EQ(ip_opt->to_string(), "10.0.0.255");
    EXPECT_TRUE(ip_opt->is_valid());

    auto ip_min = IpAddress::from_string("0.0.0.0");
    ASSERT_TRUE(ip_min.has_value());
    EXPECT_EQ(ip_min->to_string(), "0.0.0.0");

    auto ip_max = IpAddress::from_string("255.255.255.255");
    ASSERT_TRUE(ip_max.has_value());
    EXPECT_EQ(ip_max->to_string(), "255.255.255.255");
}

TEST(IpAddressTest, FromStringInvalidFormat) {
    EXPECT_FALSE(IpAddress::from_string("192.168.0").has_value());     // Меньше 4 октетов
    EXPECT_FALSE(IpAddress::from_string("192.168.0.1.5").has_value());// Больше 4 октетов
    EXPECT_FALSE(IpAddress::from_string("192..0.1").has_value());      // Пустой октет
    EXPECT_FALSE(IpAddress::from_string("a.b.c.d").has_value());       // Нечисловые значения
    EXPECT_FALSE(IpAddress::from_string("").has_value());
    EXPECT_FALSE(IpAddress::from_string("192.168.1.").has_value());    // Заканчивается точкой
    EXPECT_FALSE(IpAddress::from_string(".192.168.1.1").has_value());  // Начинается с точки
}

TEST(IpAddressTest, FromStringInvalidValues) {
    EXPECT_FALSE(IpAddress::from_string("192.168.0.256").has_value());// Октет > 255
    EXPECT_FALSE(IpAddress::from_string("192.168.-1.0").has_value()); // Отрицательное значение
    EXPECT_FALSE(IpAddress::from_string("192.168.001.1").has_value());// Ведущий ноль в многозначном октете
    EXPECT_FALSE(IpAddress::from_string("192.168.010.1").has_value());// Ведущий ноль
    EXPECT_TRUE(IpAddress::from_string("192.168.0.1").has_value()); // Одиночный 0 валиден
}

TEST(IpAddressTest, ToUint32Valid) {
    auto ip_opt1 = IpAddress::from_string("192.168.1.1");
    ASSERT_TRUE(ip_opt1.has_value());
    auto u_val1 = ip_opt1->to_uint32();
    ASSERT_TRUE(u_val1.has_value());
    EXPECT_EQ(*u_val1, (static_cast<uint32_t>(192) << 24) | (static_cast<uint32_t>(168) << 16) | (static_cast<uint32_t>(1) << 8) | static_cast<uint32_t>(1));

    auto ip_opt2 = IpAddress::from_string("10.0.0.255");
    ASSERT_TRUE(ip_opt2.has_value());
    auto u_val2 = ip_opt2->to_uint32();
    ASSERT_TRUE(u_val2.has_value());
    EXPECT_EQ(*u_val2, (static_cast<uint32_t>(10) << 24) | (static_cast<uint32_t>(0) << 16) | (static_cast<uint32_t>(0) << 8) | static_cast<uint32_t>(255));

    auto ip_min = IpAddress::from_string("0.0.0.0");
    ASSERT_TRUE(ip_min.has_value());
    auto u_min = ip_min->to_uint32();
    ASSERT_TRUE(u_min.has_value());
    EXPECT_EQ(*u_min, 0);

    auto ip_max = IpAddress::from_string("255.255.255.255");
    ASSERT_TRUE(ip_max.has_value());
    auto u_max = ip_max->to_uint32();
    ASSERT_TRUE(u_max.has_value());
    EXPECT_EQ(*u_max, 0xFFFFFFFF);
}

TEST(IpAddressTest, ToUint32Invalid) {
    IpAddress ip_invalid_str("invalid.ip"); // Не вызываем from_string, так как is_valid() вызывается в to_uint32
    EXPECT_FALSE(ip_invalid_str.to_uint32().has_value());

    IpAddress ip_invalid_val("192.168.0.256");
    EXPECT_FALSE(ip_invalid_val.to_uint32().has_value());
}


TEST(IpAddressTest, ComparisonOperators) {
    IpAddress ip1("10.0.0.1"), ip2("10.0.0.2"), ip3("192.168.0.1"), ip4("10.0.0.1");
    EXPECT_TRUE(ip1 < ip2);
    EXPECT_TRUE(ip1 < ip3);
    EXPECT_FALSE(ip2 < ip1);
    EXPECT_FALSE(ip3 < ip1);

    EXPECT_TRUE(ip1 == ip4);
    EXPECT_FALSE(ip1 == ip2);

    EXPECT_TRUE(ip1 != ip2);
    EXPECT_FALSE(ip1 != ip4);

    EXPECT_TRUE(ip2 > ip1);
    EXPECT_FALSE(ip1 > ip2);

    EXPECT_TRUE(ip1 <= ip2);
    EXPECT_TRUE(ip1 <= ip4);
    EXPECT_FALSE(ip2 <= ip1);

    EXPECT_TRUE(ip2 >= ip1);
    EXPECT_TRUE(ip4 >= ip1);
    EXPECT_FALSE(ip1 >= ip2);
}

TEST(IpAddressTest, ComparisonWithInvalid) {
    IpAddress valid_ip("1.1.1.1");
    IpAddress invalid_ip_empty("");
    IpAddress invalid_ip_format("abc");

    // Поведение при сравнении с невалидными IP зависит от вашей реализации
    // Если to_uint32() возвращает nullopt, и вы затем сравниваете address_str,
    // то результаты могут быть такими:
    EXPECT_FALSE(valid_ip == invalid_ip_empty);
    EXPECT_FALSE(valid_ip == invalid_ip_format);
    // Сравнение невалидных между собой
    // EXPECT_TRUE(invalid_ip_empty < invalid_ip_format); // " " < "abc"
}


TEST(IpAddressTest, StreamOperators) {
    IpAddress ip_orig("123.45.67.89");
    std::stringstream ss;
    ss << ip_orig;
    EXPECT_EQ(ss.str(), "123.45.67.89");

    IpAddress ip_read;
    ss.seekg(0); // Reset stream for reading
    ss >> ip_read;
    ASSERT_FALSE(ss.fail());
    EXPECT_EQ(ip_read, ip_orig);

    std::stringstream ss_invalid("invalid-ip");
    IpAddress ip_invalid_read;
    ss_invalid >> ip_invalid_read;
    EXPECT_TRUE(ss_invalid.fail());
}
