#include "gtest/gtest.h"
#include "provider_record.h"
#include "common_defs.h" 
#include "date.h"        // Для from_string в тестах
#include "ip_address.h"  // Для from_string в тестах
#include <sstream>
#include <string>
#include <vector>
#include <numeric>   
#include <algorithm> 
#include <stdexcept> 
#include <limits>     // Для std::numeric_limits

TEST(ProviderRecordTest, DefaultConstructorIsValid) { 
    ProviderRecord rec;
    EXPECT_FALSE(rec.is_valid()); // Поля по умолчанию не делают запись валидной
    EXPECT_EQ(rec.hourly_traffic.size(), HOURS_IN_DAY); 
    for(const auto& tr_val : rec.hourly_traffic) { // tr -> tr_val
        EXPECT_EQ(tr_val.incoming, 0);
        EXPECT_EQ(tr_val.outgoing, 0);
    }
}

TEST(ProviderRecordTest, ValidRecord) {
    ProviderRecord rec;
    rec.full_name = "Test User";
    rec.ip_address = IpAddress::from_string("1.2.3.4").value();
    rec.record_date = Date::from_string("01/01/2024").value();
    for(size_t i = 0; i < HOURS_IN_DAY; ++i) {
        rec.hourly_traffic[i] = {static_cast<long long>(i*10), static_cast<long long>(i*5)};
    }
    ASSERT_TRUE(rec.is_valid());
    // Сумма арифметической прогрессии S_n = n/2 * (a_1 + a_n)
    // Для i*10: a_0 = 0, a_23 = 230. Сумма = 24/2 * (0 + 230) = 12 * 230 = 2760
    EXPECT_EQ(rec.total_daily_incoming_traffic(), 2760); 
    // Для i*5: a_0 = 0, a_23 = 115. Сумма = 24/2 * (0 + 115) = 12 * 115 = 1380
    EXPECT_EQ(rec.total_daily_outgoing_traffic(), 1380); 
    EXPECT_EQ(rec.total_daily_traffic(), 4140); // 2760 + 1380
}

TEST(ProviderRecordTest, InvalidRecordFields) { 
    ProviderRecord rec_base; // Базовая валидная запись
    rec_base.full_name = "Test Base";
    rec_base.ip_address = IpAddress::from_string("1.1.1.1").value();
    rec_base.record_date = Date::from_string("01/01/2024").value();
    for(size_t i_val = 0; i_val < HOURS_IN_DAY; ++i_val) rec_base.hourly_traffic[i_val] = {0,0}; // i->i_val
    ASSERT_TRUE(rec_base.is_valid()); 

    ProviderRecord rec_test_val; // rec_test -> rec_test_val

    rec_test_val = rec_base; 
    rec_test_val.full_name = ""; 
    EXPECT_FALSE(rec_test_val.is_valid()) << "Empty name should be invalid";

    rec_test_val = rec_base;
    rec_test_val.ip_address = IpAddress(); 
    EXPECT_FALSE(rec_test_val.is_valid()) << "Default (invalid) IP should be invalid";

    rec_test_val = rec_base;
    rec_test_val.ip_address = IpAddress::from_string("not-an-ip").value_or(IpAddress()); // Гарантируем невалидный IP
    EXPECT_FALSE(rec_test_val.is_valid()) << "Malformed IP string should result in invalid record";


    rec_test_val = rec_base;
    rec_test_val.record_date = Date(); 
    EXPECT_FALSE(rec_test_val.is_valid()) << "Default (invalid) Date should be invalid";
    
    rec_test_val = rec_base;
    rec_test_val.record_date = Date::from_string("not-a-date").value_or(Date());
    EXPECT_FALSE(rec_test_val.is_valid()) << "Malformed Date string should result in invalid record";

    rec_test_val = rec_base;
    rec_test_val.hourly_traffic[0].incoming = -10; 
    EXPECT_FALSE(rec_test_val.is_valid()) << "Negative incoming traffic should be invalid";

    rec_test_val = rec_base;
    rec_test_val.hourly_traffic[HOURS_IN_DAY-1].outgoing = -1; 
    EXPECT_FALSE(rec_test_val.is_valid()) << "Negative outgoing traffic should be invalid";

    rec_test_val = rec_base;
    rec_test_val.hourly_traffic.resize(HOURS_IN_DAY - 1); 
    EXPECT_FALSE(rec_test_val.is_valid()) << "Incorrect traffic vector size (too small) should be invalid";

    rec_test_val = rec_base;
    rec_test_val.hourly_traffic.resize(HOURS_IN_DAY + 1); 
    rec_test_val.hourly_traffic[HOURS_IN_DAY] = {0,0}; // Добавляем элемент, чтобы размер стал HOURS_IN_DAY + 1
    EXPECT_FALSE(rec_test_val.is_valid()) << "Incorrect traffic vector size (too large) should be invalid";
}

TEST(ProviderRecordTest, ReadWrite) {
    ProviderRecord rec_orig;
    rec_orig.full_name = "John Doe";
    rec_orig.ip_address = IpAddress::from_string("10.20.30.40").value();
    rec_orig.record_date = Date::from_string("15/03/2024").value();
    for(size_t i = 0; i < HOURS_IN_DAY; ++i) {
        rec_orig.hourly_traffic[i] = {static_cast<long long>(i + 100), static_cast<long long>(i + 50)};
    }
    ASSERT_TRUE(rec_orig.is_valid()); // Убедимся, что исходная запись валидна

    std::stringstream ss_read_write; // ss -> ss_read_write
    rec_orig.write(ss_read_write);

    ProviderRecord rec_read;
    ss_read_write.seekg(0); 
    ASSERT_TRUE(rec_read.read(ss_read_write));
    ASSERT_TRUE(rec_read.is_valid()); 

    EXPECT_EQ(rec_read.full_name, rec_orig.full_name);
    EXPECT_EQ(rec_read.ip_address, rec_orig.ip_address);
    EXPECT_EQ(rec_read.record_date, rec_orig.record_date);
    ASSERT_EQ(rec_read.hourly_traffic.size(), rec_orig.hourly_traffic.size());
    for(size_t i = 0; i < HOURS_IN_DAY; ++i) {
        EXPECT_EQ(rec_read.hourly_traffic[i].incoming, rec_orig.hourly_traffic[i].incoming);
        EXPECT_EQ(rec_read.hourly_traffic[i].outgoing, rec_orig.hourly_traffic[i].outgoing);
    }
}

TEST(ProviderRecordTest, ReadInvalidTrafficString) {
    std::string name_val_rits = "Bad Traffic User"; // name -> name_val_rits
    std::string ip_val_rits = "5.5.5.5";         // ip -> ip_val_rits
    std::string date_val_rits = "05/05/2024";       // date -> date_val_rits
    
    std::string valid_traffic_part_rits; // valid_traffic_part -> valid_traffic_part_rits
    for(size_t i = 0; i < HOURS_IN_DAY - 1; ++i) valid_traffic_part_rits += "0 0 ";

    std::stringstream ss_not_enough_rits; // ss_not_enough -> ss_not_enough_rits
    ss_not_enough_rits << name_val_rits << "\n" << ip_val_rits << "\n" << date_val_rits << "\n" << valid_traffic_part_rits << "10\n"; 
    ProviderRecord rec_ne_rits; // rec_ne -> rec_ne_rits
    EXPECT_FALSE(rec_ne_rits.read(ss_not_enough_rits));

    std::stringstream ss_negative_rits; // ss_negative -> ss_negative_rits
    ss_negative_rits << name_val_rits << "\n" << ip_val_rits << "\n" << date_val_rits << "\n" << valid_traffic_part_rits << "-5 5\n";
    ProviderRecord rec_neg_rits; // rec_neg -> rec_neg_rits
    EXPECT_FALSE(rec_neg_rits.read(ss_negative_rits));

    std::stringstream ss_non_numeric_rits; // ss_non_numeric -> ss_non_numeric_rits
    ss_non_numeric_rits << name_val_rits << "\n" << ip_val_rits << "\n" << date_val_rits << "\n" << valid_traffic_part_rits << "abc 5\n";
    ProviderRecord rec_nn_rits; // rec_nn -> rec_nn_rits
    EXPECT_FALSE(rec_nn_rits.read(ss_non_numeric_rits));

    std::stringstream ss_too_many_rits; // ss_too_many -> ss_too_many_rits
    std::string full_valid_traffic_rits; // full_valid_traffic -> full_valid_traffic_rits
     for(size_t i = 0; i < HOURS_IN_DAY; ++i) full_valid_traffic_rits += "0 0" + std::string(i < HOURS_IN_DAY -1 ? " " : "");
    ss_too_many_rits << name_val_rits << "\n" << ip_val_rits << "\n" << date_val_rits << "\n" << full_valid_traffic_rits << " 123\n"; 
    ProviderRecord rec_tm_rits; // rec_tm -> rec_tm_rits
    EXPECT_FALSE(rec_tm_rits.read(ss_too_many_rits));

    std::stringstream ss_empty_traffic_rits; // ss_empty_traffic -> ss_empty_traffic_rits
    ss_empty_traffic_rits << name_val_rits << "\n" << ip_val_rits << "\n" << date_val_rits << "\n" << "\n"; // Пустая строка трафика
    ProviderRecord rec_et_rits; // rec_et -> rec_et_rits
    EXPECT_FALSE(rec_et_rits.read(ss_empty_traffic_rits));
}


TEST(ProviderRecordTest, GetFieldValueAsString) {
    ProviderRecord rec_gfas; // rec -> rec_gfas
    rec_gfas.full_name = "Jane Ray";
    rec_gfas.ip_address = IpAddress::from_string("123.123.123.123").value();
    rec_gfas.record_date = Date::from_string("10/10/2020").value();
    
    rec_gfas.hourly_traffic[0] = {10, 5};   
    rec_gfas.hourly_traffic[5] = {100, 50}; 
    rec_gfas.hourly_traffic[HOURS_IN_DAY - 1] = {20, 30}; // Час 23

    long long expected_total_in_gfas = 10 + 100 + 20; // expected_total_in -> expected_total_in_gfas
    long long expected_total_out_gfas = 5 + 50 + 30; // expected_total_out -> expected_total_out_gfas

    EXPECT_EQ(rec_gfas.get_field_value_as_string("name"), "Jane Ray");
    EXPECT_EQ(rec_gfas.get_field_value_as_string("ip"), "123.123.123.123");
    EXPECT_EQ(rec_gfas.get_field_value_as_string("date"), "10/10/2020");
    EXPECT_EQ(rec_gfas.get_field_value_as_string("total_incoming"), std::to_string(expected_total_in_gfas));
    EXPECT_EQ(rec_gfas.get_field_value_as_string("total_outgoing"), std::to_string(expected_total_out_gfas));
    EXPECT_EQ(rec_gfas.get_field_value_as_string("total_traffic"), std::to_string(expected_total_in_gfas + expected_total_out_gfas));

    EXPECT_EQ(rec_gfas.get_field_value_as_string("traffic_00"), "10/5"); 
    EXPECT_EQ(rec_gfas.get_field_value_as_string("traffic_in_00"), "10");
    EXPECT_EQ(rec_gfas.get_field_value_as_string("traffic_out_00"), "5");

    EXPECT_EQ(rec_gfas.get_field_value_as_string("traffic_05"), "100/50"); 
    EXPECT_EQ(rec_gfas.get_field_value_as_string("traffic_in_05"), "100");
    EXPECT_EQ(rec_gfas.get_field_value_as_string("traffic_out_05"), "50");
    
    EXPECT_EQ(rec_gfas.get_field_value_as_string("traffic_23"), "20/30");
    EXPECT_EQ(rec_gfas.get_field_value_as_string("traffic_in_23"), "20");
    EXPECT_EQ(rec_gfas.get_field_value_as_string("traffic_out_23"), "30");

    EXPECT_EQ(rec_gfas.get_field_value_as_string("traffic_01"), "0/0"); // Час с нулевым трафиком
    EXPECT_EQ(rec_gfas.get_field_value_as_string("traffic_in_01"), "0");
    EXPECT_EQ(rec_gfas.get_field_value_as_string("traffic_out_01"), "0");
    
    EXPECT_THROW(rec_gfas.get_field_value_as_string("non_existent_field"), std::out_of_range);
    EXPECT_THROW(rec_gfas.get_field_value_as_string("traffic_24"), std::out_of_range); 
    EXPECT_THROW(rec_gfas.get_field_value_as_string("traffic_in_abc"), std::out_of_range); 
    EXPECT_THROW(rec_gfas.get_field_value_as_string("traffic_in_99"), std::out_of_range); 
    EXPECT_THROW(rec_gfas.get_field_value_as_string("traffic_out_24"), std::out_of_range);
}

TEST(ProviderRecordTest, TrafficWithMaxLongLongValues) {
    ProviderRecord rec_max; // rec -> rec_max
    rec_max.full_name = "Max Traffic User";
    rec_max.ip_address = IpAddress::from_string("255.255.255.254").value();
    rec_max.record_date = Date::from_string("31/12/2025").value();

    for(size_t i_val = 0; i_val < HOURS_IN_DAY; ++i_val) { // i -> i_val
        rec_max.hourly_traffic[i_val] = {0, 0};
    }

    const long long max_ll_val = std::numeric_limits<long long>::max(); // max_ll -> max_ll_val

    // Случай 1: одно поле max, другое маленькое
    rec_max.hourly_traffic[0] = {max_ll_val, 1}; 
    rec_max.hourly_traffic[1] = {2, max_ll_val}; 
    ASSERT_TRUE(rec_max.is_valid()) << "Record should be valid with LLONG_MAX traffic values.";

    // Проверяем суммы отдельных потоков. Если max_ll_val + (1 или 2) переполняет long long,
    // то это будет UB, и тест может упасть. Но сложение LLONG_MAX + 1 обычно приводит к LLONG_MIN.
    // В C++ переполнение знаковых целых - это UB. Поэтому мы не должны этого допускать в тестах,
    // если только мы не тестируем само поведение при переполнении.
    // Вместо этого, проверим, что если сумма НЕ переполняется, она считается верно.
    // Или проверим, что если одно поле LLONG_MAX, а остальные нули, то сумма равна LLONG_MAX.
    ProviderRecord rec_single_max;
    rec_single_max.full_name = "Single Max";
    rec_single_max.ip_address = IpAddress::from_string("1.1.1.1").value();
    rec_single_max.record_date = Date::from_string("01/01/2001").value();
    for(size_t i=0; i<HOURS_IN_DAY; ++i) rec_single_max.hourly_traffic[i] = {0,0};
    rec_single_max.hourly_traffic[0].incoming = max_ll_val;
    ASSERT_TRUE(rec_single_max.is_valid());
    EXPECT_EQ(rec_single_max.total_daily_incoming_traffic(), max_ll_val);
    EXPECT_EQ(rec_single_max.total_daily_outgoing_traffic(), 0);
    EXPECT_EQ(rec_single_max.total_daily_traffic(), max_ll_val); // Т.к. outgoing = 0

    // Случай 2: два поля с max_ll / 2, чтобы сумма total() для одного часа не переполнялась
    rec_max.hourly_traffic[0] = {max_ll_val / 2, max_ll_val / 2 -1}; 
    rec_max.hourly_traffic[1] = {10, 20}; // Остальные часы с небольшим трафиком
    for(size_t i_val = 2; i_val < HOURS_IN_DAY; ++i_val) { // i -> i_val
        rec_max.hourly_traffic[i_val] = {0,0};
    }
    ASSERT_TRUE(rec_max.is_valid());
    EXPECT_EQ(rec_max.hourly_traffic[0].total(), (max_ll_val / 2) + (max_ll_val / 2 - 1));
    EXPECT_EQ(rec_max.total_daily_incoming_traffic(), max_ll_val / 2 + 10);
    EXPECT_EQ(rec_max.total_daily_outgoing_traffic(), max_ll_val / 2 - 1 + 20);
    EXPECT_EQ(rec_max.total_daily_traffic(), (max_ll_val / 2 + max_ll_val / 2 - 1) + (10 + 20) );
}
