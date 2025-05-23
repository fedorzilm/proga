#include "gtest/gtest.h"
#include "provider_record.h" // Включает common_defs.h, ip_address.h, date.h
#include <sstream>
#include <vector>
#include <string>
#include <iomanip> // для std::fixed, std::setprecision в тестах вывода

class ProviderRecordTest : public ::testing::Test {
protected:
    Date defaultDate; // 01.01.1970
    IPAddress defaultIp; // 0.0.0.0
    std::vector<double> defaultTraffic;

    ProviderRecordTest() : defaultTraffic(HOURS_IN_DAY, 0.0) {}

    std::vector<double> create_valid_traffic_vector(double val = 1.0) {
        return std::vector<double>(HOURS_IN_DAY, val);
    }
};

TEST_F(ProviderRecordTest, DefaultConstructor) {
    ProviderRecord pr;
    EXPECT_EQ(pr.getName(), "");
    EXPECT_EQ(pr.getIpAddress(), defaultIp);
    EXPECT_EQ(pr.getDate(), defaultDate);
    ASSERT_EQ(pr.getTrafficInByHour().size(), static_cast<size_t>(HOURS_IN_DAY));
    ASSERT_EQ(pr.getTrafficOutByHour().size(), static_cast<size_t>(HOURS_IN_DAY));
    for (double val : pr.getTrafficInByHour()) {
        EXPECT_DOUBLE_EQ(val, 0.0);
    }
    for (double val : pr.getTrafficOutByHour()) {
        EXPECT_DOUBLE_EQ(val, 0.0);
    }
}

TEST_F(ProviderRecordTest, ParameterizedConstructor_Valid) {
    std::string name = "Тест Тестович";
    IPAddress ip(192, 168, 1, 1);
    Date date(15, 5, 2024);
    std::vector<double> trafficIn = create_valid_traffic_vector(1.5);
    std::vector<double> trafficOut = create_valid_traffic_vector(2.5);

    EXPECT_NO_THROW({
        ProviderRecord pr(name, ip, date, trafficIn, trafficOut);
        EXPECT_EQ(pr.getName(), name);
        EXPECT_EQ(pr.getIpAddress(), ip);
        EXPECT_EQ(pr.getDate(), date);
        EXPECT_EQ(pr.getTrafficInByHour(), trafficIn);
        EXPECT_EQ(pr.getTrafficOutByHour(), trafficOut);
    });
}

TEST_F(ProviderRecordTest, ParameterizedConstructor_InvalidTrafficInSize) {
    std::vector<double> invalidTrafficIn(HOURS_IN_DAY - 1, 1.0);
    std::vector<double> validTrafficOut = create_valid_traffic_vector();
    EXPECT_THROW(ProviderRecord("Тест", defaultIp, defaultDate, invalidTrafficIn, validTrafficOut), std::invalid_argument);
}

TEST_F(ProviderRecordTest, ParameterizedConstructor_InvalidTrafficOutSize) {
    std::vector<double> validTrafficIn = create_valid_traffic_vector();
    std::vector<double> invalidTrafficOut(HOURS_IN_DAY + 1, 1.0);
    EXPECT_THROW(ProviderRecord("Тест", defaultIp, defaultDate, validTrafficIn, invalidTrafficOut), std::invalid_argument);
}

TEST_F(ProviderRecordTest, ParameterizedConstructor_NegativeTrafficIn) {
    std::vector<double> trafficInWithNegative = create_valid_traffic_vector();
    trafficInWithNegative[0] = -0.1;
    std::vector<double> validTrafficOut = create_valid_traffic_vector();
    EXPECT_THROW(ProviderRecord("Тест", defaultIp, defaultDate, trafficInWithNegative, validTrafficOut), std::invalid_argument);
}

TEST_F(ProviderRecordTest, ParameterizedConstructor_NegativeTrafficOut) {
    std::vector<double> validTrafficIn = create_valid_traffic_vector();
    std::vector<double> trafficOutWithNegative = create_valid_traffic_vector();
    trafficOutWithNegative[HOURS_IN_DAY - 1] = -10.0;
    EXPECT_THROW(ProviderRecord("Тест", defaultIp, defaultDate, validTrafficIn, trafficOutWithNegative), std::invalid_argument);
}

TEST_F(ProviderRecordTest, Setters_Valid) {
    ProviderRecord pr;
    std::string newName = "Новое Имя";
    IPAddress newIp(10, 20, 30, 40);
    Date newDate(10, 10, 2020);
    std::vector<double> newTrafficIn = create_valid_traffic_vector(5.0);
    std::vector<double> newTrafficOut = create_valid_traffic_vector(6.0);

    pr.setName(newName);
    EXPECT_EQ(pr.getName(), newName);

    pr.setIpAddress(newIp);
    EXPECT_EQ(pr.getIpAddress(), newIp);

    pr.setDate(newDate);
    EXPECT_EQ(pr.getDate(), newDate);

    EXPECT_NO_THROW(pr.setTrafficInByHour(newTrafficIn));
    EXPECT_EQ(pr.getTrafficInByHour(), newTrafficIn);

    EXPECT_NO_THROW(pr.setTrafficOutByHour(newTrafficOut));
    EXPECT_EQ(pr.getTrafficOutByHour(), newTrafficOut);
}

TEST_F(ProviderRecordTest, SetTraffic_InvalidSize) {
    ProviderRecord pr;
    std::vector<double> invalidTraffic(10, 1.0); // Неверный размер
    EXPECT_THROW(pr.setTrafficInByHour(invalidTraffic), std::invalid_argument);
    EXPECT_THROW(pr.setTrafficOutByHour(invalidTraffic), std::invalid_argument);
}

TEST_F(ProviderRecordTest, SetTraffic_NegativeValues) {
    ProviderRecord pr;
    std::vector<double> trafficWithNegative = create_valid_traffic_vector();
    trafficWithNegative[5] = -5.5;
    EXPECT_THROW(pr.setTrafficInByHour(trafficWithNegative), std::invalid_argument);
    EXPECT_THROW(pr.setTrafficOutByHour(trafficWithNegative), std::invalid_argument);
}

TEST_F(ProviderRecordTest, EqualityOperators) {
    std::vector<double> traffic1 = create_valid_traffic_vector(1.0);
    std::vector<double> traffic2 = create_valid_traffic_vector(2.0);

    ProviderRecord pr1("Имя1", IPAddress(1,1,1,1), Date(1,1,2020), traffic1, traffic1);
    ProviderRecord pr2("Имя1", IPAddress(1,1,1,1), Date(1,1,2020), traffic1, traffic1); // Такой же
    ProviderRecord pr3("Имя2", IPAddress(1,1,1,1), Date(1,1,2020), traffic1, traffic1); // Другое имя
    ProviderRecord pr4("Имя1", IPAddress(2,2,2,2), Date(1,1,2020), traffic1, traffic1); // Другой IP
    ProviderRecord pr5("Имя1", IPAddress(1,1,1,1), Date(2,1,2020), traffic1, traffic1); // Другая дата
    ProviderRecord pr6("Имя1", IPAddress(1,1,1,1), Date(1,1,2020), traffic2, traffic1); // Другой TrafficIn
    ProviderRecord pr7("Имя1", IPAddress(1,1,1,1), Date(1,1,2020), traffic1, traffic2); // Другой TrafficOut

    EXPECT_TRUE(pr1 == pr2);
    EXPECT_FALSE(pr1 == pr3);
    EXPECT_FALSE(pr1 == pr4);
    EXPECT_FALSE(pr1 == pr5);
    EXPECT_FALSE(pr1 == pr6);
    EXPECT_FALSE(pr1 == pr7);

    EXPECT_FALSE(pr1 != pr2);
    EXPECT_TRUE(pr1 != pr3);
}

TEST_F(ProviderRecordTest, StreamOutputFormat) {
    std::string name = "Абонент Пример Примерович";
    IPAddress ip(192, 168, 10, 100);
    Date date(5, 7, 2024);
    std::vector<double> trafficIn = std::vector<double>(HOURS_IN_DAY);
    std::vector<double> trafficOut = std::vector<double>(HOURS_IN_DAY);
    for(int i=0; i<HOURS_IN_DAY; ++i) {
        trafficIn[static_cast<size_t>(i)] = 1.0 + static_cast<double>(i) * 0.1;   // 1.0, 1.1, ...
        trafficOut[static_cast<size_t>(i)] = 0.5 + static_cast<double>(i) * 0.05; // 0.50, 0.55, ...
    }
    ProviderRecord pr(name, ip, date, trafficIn, trafficOut);

    std::ostringstream oss;
    oss << pr;
    std::string output = oss.str();

    std::string expected_output_start = name + "\n" + ip.toString() + "\n" + date.toString() + "\n";
    EXPECT_NE(output.find(expected_output_start), std::string::npos);

    size_t traffic_in_line_pos = output.find(date.toString()) + date.toString().length() + 1;
    std::string traffic_in_line = output.substr(traffic_in_line_pos, output.find('\n', traffic_in_line_pos) - traffic_in_line_pos);
    
    std::istringstream iss_in(traffic_in_line);
    double val_in;
    int count_in = 0;
    while(iss_in >> val_in) count_in++;
    EXPECT_EQ(count_in, HOURS_IN_DAY);

    EXPECT_NE(output.find("1.00"), std::string::npos);
    EXPECT_NE(output.find("1.10"), std::string::npos);
    EXPECT_NE(output.find("0.50"), std::string::npos);
    EXPECT_NE(output.find("0.55"), std::string::npos);
}


TEST_F(ProviderRecordTest, StreamInput_Valid) {
    std::string name = "Ввод Тест";
    IPAddress ip(10,0,1,2);
    Date date(28,2,2024); // Високосный
    std::vector<double> trafficIn(HOURS_IN_DAY);
    std::vector<double> trafficOut(HOURS_IN_DAY);

    std::ostringstream oss_input_data;
    oss_input_data << name << "\n";
    oss_input_data << ip.toString() << "\n";
    oss_input_data << date.toString() << "\n";
    oss_input_data << std::fixed << std::setprecision(2);
    for(int i=0; i<HOURS_IN_DAY; ++i) {
        trafficIn[static_cast<size_t>(i)] = static_cast<double>(i);
        trafficOut[static_cast<size_t>(i)] = static_cast<double>(i) * 0.5;
        oss_input_data << trafficIn[static_cast<size_t>(i)] << (i == HOURS_IN_DAY - 1 ? "" : " ");
    }
    oss_input_data << "\n";
    for(int i=0; i<HOURS_IN_DAY; ++i) {
        oss_input_data << trafficOut[static_cast<size_t>(i)] << (i == HOURS_IN_DAY - 1 ? "" : " ");
    }

    std::istringstream iss(oss_input_data.str());
    ProviderRecord pr;
    iss >> pr;

    EXPECT_FALSE(iss.fail()) << "Поток ввода не должен быть в состоянии ошибки";
    EXPECT_EQ(pr.getName(), name);
    EXPECT_EQ(pr.getIpAddress(), ip);
    EXPECT_EQ(pr.getDate(), date);
    ASSERT_EQ(pr.getTrafficInByHour().size(), static_cast<size_t>(HOURS_IN_DAY));
    ASSERT_EQ(pr.getTrafficOutByHour().size(), static_cast<size_t>(HOURS_IN_DAY));

    for(int i=0; i<HOURS_IN_DAY; ++i) {
        EXPECT_NEAR(pr.getTrafficInByHour()[static_cast<size_t>(i)], trafficIn[static_cast<size_t>(i)], DOUBLE_EPSILON);
        EXPECT_NEAR(pr.getTrafficOutByHour()[static_cast<size_t>(i)], trafficOut[static_cast<size_t>(i)], DOUBLE_EPSILON);
    }
}

TEST_F(ProviderRecordTest, StreamInput_InvalidFormat_MissingParts) {
    ProviderRecord pr;
    std::istringstream iss1("Имя\n1.1.1.1"); // Не хватает даты и трафика
    iss1 >> pr;
    EXPECT_TRUE(iss1.fail());

    pr = ProviderRecord();
    std::istringstream iss2("Имя\n1.1.1.1\n01.01.2000\n1 2 3"); // Не хватает трафика
    iss2 >> pr;
    EXPECT_TRUE(iss2.fail());
}

TEST_F(ProviderRecordTest, StreamInput_InvalidTrafficValueInStream) {
    ProviderRecord pr;
    std::string data_str = "Имя\n1.1.1.1\n01.01.2000\n";
    for(int i=0; i<HOURS_IN_DAY -1; ++i) data_str += "1.0 ";
    data_str += "bad_value\n"; // Неверное значение трафика
    for(int i=0; i<HOURS_IN_DAY; ++i) data_str += "1.0" + std::string(i == HOURS_IN_DAY -1 ? "" : " ");
    
    std::istringstream iss(data_str);
    iss >> pr;
    EXPECT_TRUE(iss.fail());
}

TEST_F(ProviderRecordTest, StreamInput_NegativeTrafficInStream) {
    ProviderRecord pr;
    std::string data_str = "Имя\n1.1.1.1\n01.01.2000\n";
    for(int i=0; i<HOURS_IN_DAY -1; ++i) data_str += "1.0 ";
    data_str += "-1.0\n"; // Отрицательное значение трафика
    for(int i=0; i<HOURS_IN_DAY; ++i) data_str += "1.0" + std::string(i == HOURS_IN_DAY -1 ? "" : " ");
    
    std::istringstream iss(data_str);
    iss >> pr;
    EXPECT_TRUE(iss.fail());
}

TEST_F(ProviderRecordTest, StreamInput_EmptyLinesBetweenRecordsSimulated) {
    std::string record1_data_str;
    std::ostringstream oss_rec1;
    oss_rec1 << "Имя1\n1.1.1.1\n01.01.2020\n";
    for(int i=0; i<HOURS_IN_DAY; ++i) oss_rec1 << "1.0" << (i == HOURS_IN_DAY -1 ? "" : " ");
    oss_rec1 << "\n";
    for(int i=0; i<HOURS_IN_DAY; ++i) oss_rec1 << "2.0" << (i == HOURS_IN_DAY -1 ? "" : " ");
    record1_data_str = oss_rec1.str();

    std::string full_stream_data = "\n\n" + record1_data_str + "\n\n" + record1_data_str;
    std::istringstream iss(full_stream_data);
    ProviderRecord pr1, pr2;

    char next_char;
    while (iss.good() && ((next_char = static_cast<char>(iss.peek())) == '\n' || next_char == '\r')) {
        iss.ignore();
    }
    iss >> pr1;
    EXPECT_FALSE(iss.fail()) << "Чтение первой записи не удалось";

    while (iss.good() && ((next_char = static_cast<char>(iss.peek())) == '\n' || next_char == '\r')) {
        iss.ignore();
    }
    iss >> pr2;
    EXPECT_FALSE(iss.fail()) << "Чтение второй записи не удалось";

    EXPECT_EQ(pr1.getName(), "Имя1");
    EXPECT_EQ(pr2.getName(), "Имя1");
}

TEST_F(ProviderRecordTest, StreamInput_HandlesMissingNewlineAtEndOfTrafficOut) {
    std::string name = "NoNewlineTest";
    IPAddress ip(1,2,3,4); 
    Date date(1,1,2021);
    std::ostringstream oss;
    oss << name << "\n" << ip << "\n" << date << "\n";
    for (int i = 0; i < HOURS_IN_DAY; ++i) oss << (i > 0 ? " " : "") << "1.0";
    oss << "\n";
    for (int i = 0; i < HOURS_IN_DAY; ++i) oss << (i > 0 ? " " : "") << "0.5"; // Нет \n в конце этой строки

    ProviderRecord pr;
    std::istringstream iss(oss.str());
    iss >> pr;
    EXPECT_FALSE(iss.fail());
    EXPECT_EQ(pr.getName(), name);
    EXPECT_EQ(pr.getIpAddress(), ip);
    EXPECT_EQ(pr.getDate(), date);
    ASSERT_FALSE(pr.getTrafficInByHour().empty());
    EXPECT_DOUBLE_EQ(pr.getTrafficInByHour()[0], 1.0);
    ASSERT_FALSE(pr.getTrafficOutByHour().empty());
    EXPECT_DOUBLE_EQ(pr.getTrafficOutByHour()[0], 0.5);
}
