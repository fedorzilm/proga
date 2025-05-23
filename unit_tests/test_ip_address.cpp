#include "gtest/gtest.h"
#include "ip_address.h" 
#include <sstream>      

class IPAddressTest : public ::testing::Test {
};

TEST_F(IPAddressTest, DefaultConstructor) {
    IPAddress ip;
    EXPECT_EQ(ip.toString(), "0.0.0.0");
}

TEST_F(IPAddressTest, ParameterizedConstructor_ValidIPs) {
    EXPECT_NO_THROW(IPAddress(192, 168, 1, 1));
    IPAddress ip1(10, 0, 0, 255);
    EXPECT_EQ(ip1.toString(), "10.0.0.255");
    IPAddress ip2(0, 0, 0, 0);
    EXPECT_EQ(ip2.toString(), "0.0.0.0");
    IPAddress ip3(255, 255, 255, 255);
    EXPECT_EQ(ip3.toString(), "255.255.255.255");
}

TEST_F(IPAddressTest, ParameterizedConstructor_InvalidOctets_Throws) {
    EXPECT_THROW(IPAddress(-1, 0, 0, 0), std::invalid_argument);
    EXPECT_THROW(IPAddress(0, 256, 0, 0), std::invalid_argument);
    EXPECT_THROW(IPAddress(0, 0, -10, 0), std::invalid_argument);
    EXPECT_THROW(IPAddress(0, 0, 0, 1000), std::invalid_argument);
}

TEST_F(IPAddressTest, ToStringConversion) {
    IPAddress ip(127, 0, 0, 1);
    EXPECT_EQ(ip.toString(), "127.0.0.1");
}

TEST_F(IPAddressTest, ComparisonOperators) {
    IPAddress ip_a(192, 168, 0, 1);    
    IPAddress ip_b(192, 168, 0, 2);    // A < B
    IPAddress ip_c(192, 168, 1, 1);    // A < C, B < C
    IPAddress ip_d(10, 255, 255, 255); // D < A, D < B, D < C
    IPAddress ip_e(192, 168, 0, 1);    // A == E

    // 1. ==
    EXPECT_TRUE(ip_a == ip_e);
    EXPECT_FALSE(ip_a == ip_b);

    // 2. !=
    EXPECT_TRUE(ip_a != ip_b);
    EXPECT_FALSE(ip_a != ip_e);

    // 3. <
    EXPECT_TRUE(ip_a < ip_b);    
    EXPECT_TRUE(ip_a < ip_c);    
    EXPECT_TRUE(ip_d < ip_a);    
    EXPECT_FALSE(ip_b < ip_a);   
    EXPECT_FALSE(ip_a < ip_e);   
    EXPECT_FALSE(ip_c < ip_a);   

    // 4. > ( a > b  <=>  b < a )
    EXPECT_TRUE(ip_b > ip_a);    
    EXPECT_TRUE(ip_c > ip_a);    
    EXPECT_TRUE(ip_a > ip_d);    
    EXPECT_FALSE(ip_a > ip_b);   
    EXPECT_FALSE(ip_e > ip_a); 
    EXPECT_FALSE(ip_d > ip_a);   

    // 5. <= ( a <= b  <=>  !(b < a) )
    // ip_a <= ip_e  (A <= E) -> true.  !(E < A) -> !(false) -> true.
    EXPECT_TRUE(!(ip_e < ip_a));   
    // ip_a <= ip_b  (A <= B) -> true.  !(B < A) -> !(false) -> true.
    EXPECT_TRUE(!(ip_b < ip_a)); 
    // ip_d <= ip_a  (D <= A) -> true.  !(A < D) -> !(false) -> true.
    EXPECT_TRUE(!(ip_a < ip_d));   
    // ip_b <= ip_a  (B <= A) -> false. !(A < B) -> !(true) -> false.
    EXPECT_FALSE(!(ip_a < ip_b));  
    // ip_c <= ip_a  (C <= A) -> false. !(A < C) -> !(true) -> false.
    EXPECT_FALSE(!(ip_a < ip_c));  
    
    // 6. >= ( a >= b  <=>  !(a < b) )
    // ip_a >= ip_e  (A >= E) -> true.  !(A < E) -> !(false) -> true.
    EXPECT_TRUE(!(ip_a < ip_e));   
    // ip_b >= ip_a  (B >= A) -> true.  !(B < A) -> !(false) -> true.
    EXPECT_TRUE(!(ip_b < ip_a));   
    // ip_a >= ip_d  (A >= D) -> true.  !(A < D) -> !(false) -> true.
    EXPECT_TRUE(!(ip_a < ip_d));  
    // ip_a >= ip_b  (A >= B) -> false. !(A < B) -> !(true) -> false.
    EXPECT_FALSE(!(ip_a < ip_b));  
    // ip_a >= ip_c  (A >= C) -> false. !(A < C) -> !(true) -> false.
    EXPECT_FALSE(!(ip_a < ip_c)); 
}

TEST_F(IPAddressTest, StreamOutput) {
    IPAddress ip(172, 16, 32, 1);
    std::ostringstream oss;
    oss << ip;
    EXPECT_EQ(oss.str(), "172.16.32.1");
}

TEST_F(IPAddressTest, StreamInput_Valid) {
    std::istringstream iss_valid("192.168.10.20");
    IPAddress ip_valid;
    iss_valid >> ip_valid;
    EXPECT_FALSE(iss_valid.fail());
    EXPECT_EQ(ip_valid.toString(), "192.168.10.20");

    std::istringstream iss_spacing("  10.0.5.1 \t"); 
    IPAddress ip_spacing;
    iss_spacing >> ip_spacing;
    EXPECT_FALSE(iss_spacing.fail());
    EXPECT_EQ(ip_spacing.toString(), "10.0.5.1");
}

TEST_F(IPAddressTest, StreamInput_InvalidFormat) {
    IPAddress ip_invalid;
    std::istringstream iss_no_dots("19216811");
    iss_no_dots >> ip_invalid;
    EXPECT_TRUE(iss_no_dots.fail());
    ip_invalid = IPAddress(); 
    std::istringstream iss_wrong_sep("10,0,0,1");
    iss_wrong_sep >> ip_invalid;
    EXPECT_TRUE(iss_wrong_sep.fail());
    ip_invalid = IPAddress();
    std::istringstream iss_too_few_parts("192.168.1");
    iss_too_few_parts >> ip_invalid;
    EXPECT_TRUE(iss_too_few_parts.fail());
    ip_invalid = IPAddress();
    std::istringstream iss_too_many_parts("192.168.1.1.1");
    iss_too_many_parts >> ip_invalid;
    EXPECT_TRUE(iss_too_many_parts.fail());
    ip_invalid = IPAddress();
    std::istringstream iss_non_numeric("192.168.one.1");
    iss_non_numeric >> ip_invalid;
    EXPECT_TRUE(iss_non_numeric.fail());
    ip_invalid = IPAddress();
    std::istringstream iss_extra_chars("1.2.3.4extra");
    iss_extra_chars >> ip_invalid;
    EXPECT_TRUE(iss_extra_chars.fail());
    ip_invalid = IPAddress();
    std::istringstream iss_leading_dot(".1.2.3.4");
    iss_leading_dot >> ip_invalid;
    EXPECT_TRUE(iss_leading_dot.fail());
    ip_invalid = IPAddress();
    std::istringstream iss_trailing_dot("1.2.3.4.");
    iss_trailing_dot >> ip_invalid;
    EXPECT_TRUE(iss_trailing_dot.fail());
}

TEST_F(IPAddressTest, StreamInput_InvalidOctetValues) {
    IPAddress ip_invalid_val;
    std::istringstream iss_octet_too_large("192.168.300.1");
    iss_octet_too_large >> ip_invalid_val;
    EXPECT_TRUE(iss_octet_too_large.fail());
    ip_invalid_val = IPAddress();
    std::istringstream iss_octet_negative("192.168.-5.1");
    iss_octet_negative >> ip_invalid_val;
    EXPECT_TRUE(iss_octet_negative.fail());
}

TEST_F(IPAddressTest, StreamInput_EmptyStream) {
    IPAddress ip;
    std::istringstream empty_ss("");
    empty_ss >> ip;
    EXPECT_TRUE(empty_ss.fail());
}
