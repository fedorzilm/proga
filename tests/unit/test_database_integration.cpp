#include "gtest/gtest.h"
#include "database.h"       
#include "query_parser.h"   
#include "tariff_plan.h"    
#include "provider_record.h"
#include "common_defs.h"    
#include "date.h"           // Для from_string
#include "ip_address.h"     // Для from_string
#include <fstream>
#include <cstdio>           // для std::remove
#include <vector>
#include <string>
#include <numeric>          // for std::accumulate
#include <filesystem>       // Для работы с временными файлами

// Вспомогательная функция для создания файла данных ProviderRecord
void create_provider_test_file_for_db_int_test(const std::string& filename, const std::vector<std::string>& record_lines_flat) {
    std::ofstream ofs(filename);
    if (!ofs.is_open()) {
        throw std::runtime_error("Failed to open for writing: " + filename);
    }
    for(size_t i = 0; i < record_lines_flat.size(); i += 4) {
        if (i + 3 >= record_lines_flat.size()) {
            throw std::runtime_error("Insufficient data for a complete record at index " + std::to_string(i) + " in create_provider_test_file_for_db_int_test");
        }
        ofs << record_lines_flat[i] << std::endl;   
        ofs << record_lines_flat[i+1] << std::endl; 
        ofs << record_lines_flat[i+2] << std::endl; 
        ofs << record_lines_flat[i+3] << std::endl; 
    }
    ofs.close();
}

// Вспомогательная функция для создания тестового файла тарифов
void create_tariff_test_file_for_db_int_test(const std::string& filename, const std::vector<std::string>& lines) {
    std::ofstream ofs(filename);
     if (!ofs.is_open()) {
        throw std::runtime_error("Failed to open for writing: " + filename);
    }
    for (const auto& line : lines) {
        ofs << line << std::endl;
    }
    ofs.close();
}


class DatabaseIntegrationTest : public ::testing::Test {
protected:
    Database db;
    TariffPlan tariff; 
    TariffPlan alt_tariff; 
    
    std::filesystem::path temp_test_dir_db_int; // Базовая временная директория для этого набора тестов
    std::string test_db_filename_db_int;
    std::string test_tariff_filename_db_int;
    std::string test_alt_tariff_filename_db_int;

    const std::vector<std::string> small_db_content_db_int = { // _db_int суффикс
        "Alice Wonderland", "192.168.0.10", "01/01/2024", "10 5 10 5 10 5 10 5 10 5 10 5 10 5 10 5 10 5 10 5 10 5 10 5 10 5 10 5 10 5 10 5 10 5 10 5 100 50 100 50 100 50 100 50 100 50 100 50",
        "Bob The Builder", "10.0.0.2", "01/01/2024", "20 10 20 10 20 10 20 10 20 10 20 10 20 10 20 10 20 10 20 10 20 10 20 10 20 10 20 10 20 10 20 10 20 10 20 10 200 100 200 100 200 100 200 100 200 100 200 100",
        "Alice Wonderland", "192.168.0.10", "02/01/2024", "5 2 5 2 5 2 5 2 5 2 5 2 5 2 5 2 5 2 5 2 5 2 5 2 5 2 5 2 5 2 5 2 5 2 5 2 50 20 50 20 50 20 50 20 50 20 50 20",
        "Charlie Chaplin", "192.168.0.11", "01/02/2024", "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0"
    };
    const std::vector<std::string> default_tariff_content_db_int = { // _db_int суффикс
        "0.5","0.5","0.5","0.5","0.5","0.5","0.5","0.5", 
        "1.0","1.0","1.0","1.0","1.0","1.0","1.0","1.0","1.0","1.0", 
        "0.75","0.75","0.75","0.75","0.75","0.75" 
    };
    const std::vector<std::string> alternative_tariff_content_db_int = { // _db_int суффикс
        "0.1","0.1","0.1","0.1","0.1","0.1","0.1","0.1",
        "0.1","0.1","0.1","0.1","0.1","0.1","0.1","0.1","0.1","0.1",
        "0.1","0.1","0.1","0.1","0.1","0.1"
    };

    void SetUp() override {
        temp_test_dir_db_int = std::filesystem::temp_directory_path() / "ip_db_db_integration_tests";
        std::filesystem::remove_all(temp_test_dir_db_int); // Очищаем от предыдущих запусков
        std::filesystem::create_directories(temp_test_dir_db_int);

        test_db_filename_db_int = (temp_test_dir_db_int / "temp_db_integration.txt").string();
        test_tariff_filename_db_int = (temp_test_dir_db_int / "temp_tariff_integration.cfg").string();
        test_alt_tariff_filename_db_int = (temp_test_dir_db_int / "temp_alt_tariff_integration.cfg").string();

        ASSERT_NO_THROW(create_provider_test_file_for_db_int_test(test_db_filename_db_int, small_db_content_db_int));
        ASSERT_TRUE(db.load_from_file(test_db_filename_db_int)) << "Setup failed: Could not load DB file: " << test_db_filename_db_int;
        
        ASSERT_NO_THROW(create_tariff_test_file_for_db_int_test(test_tariff_filename_db_int, default_tariff_content_db_int));
        ASSERT_TRUE(tariff.load_from_file(test_tariff_filename_db_int)) << "Setup failed: Could not load Tariff file: " << test_tariff_filename_db_int;
        
        ASSERT_NO_THROW(create_tariff_test_file_for_db_int_test(test_alt_tariff_filename_db_int, alternative_tariff_content_db_int));
        ASSERT_TRUE(alt_tariff.load_from_file(test_alt_tariff_filename_db_int)) << "Setup failed: Could not load Alt Tariff file: " << test_alt_tariff_filename_db_int;
    }

    void TearDown() override {
         try {
            if (std::filesystem::exists(temp_test_dir_db_int)) {
                std::filesystem::remove_all(temp_test_dir_db_int);
            }
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Warning: TearDown failed to remove temp_test_dir_db_int: " << temp_test_dir_db_int.string() << ". Error: " << e.what() << std::endl;
        }
    }
};

TEST_F(DatabaseIntegrationTest, InitialRecordCount) {
    EXPECT_EQ(db.record_count(), 4);
}

TEST_F(DatabaseIntegrationTest, AddAndSelect) {
    ProviderRecord newUser; // newUser -> newUserAddSelect
    newUser.full_name = "Diana Prince";
    newUser.ip_address = IpAddress::from_string("4.3.2.1").value();
    newUser.record_date = Date::from_string("03/03/2023").value();
    for(size_t i=0; i<HOURS_IN_DAY; ++i) newUser.hourly_traffic[i] = {static_cast<long long>(i), static_cast<long long>(i)};
    ASSERT_TRUE(newUser.is_valid()); // Убедимся, что сама запись валидна перед добавлением
    ASSERT_TRUE(db.add_record(newUser));
    EXPECT_EQ(db.record_count(), 5);

    auto pq_opt_addsel = QueryParser::parse_select("SELECT * WHERE name = \"Diana Prince\""); // pq_opt -> pq_opt_addsel
    ASSERT_TRUE(pq_opt_addsel.has_value());
    auto results_addsel = db.get_formatted_select_results(*pq_opt_addsel); // results -> results_addsel
    ASSERT_EQ(results_addsel.size(), 2); 
    ASSERT_GE(results_addsel[1].size(), 1U); // Проверяем, что есть хотя бы одна колонка в данных
    EXPECT_EQ(results_addsel[1][0], "Diana Prince"); // Предполагаем, что 'name' - первая колонка при SELECT *
}

TEST_F(DatabaseIntegrationTest, AddDuplicateRecord) {
    ProviderRecord existingUser_dup; // existingUser -> existingUser_dup
    existingUser_dup.full_name = "Alice Wonderland";
    existingUser_dup.ip_address = IpAddress::from_string("192.168.0.10").value();
    existingUser_dup.record_date = Date::from_string("01/01/2024").value();
    for(size_t i=0; i<HOURS_IN_DAY; ++i) existingUser_dup.hourly_traffic[i] = {1,1}; // Трафик не важен
    ASSERT_TRUE(existingUser_dup.is_valid());
    
    EXPECT_FALSE(db.add_record(existingUser_dup)); 
    EXPECT_EQ(db.record_count(), 4); 
}

TEST_F(DatabaseIntegrationTest, DeleteAndVerify) {
    auto pq_opt_del_dv = QueryParser::parse_select("SELECT * WHERE name = \"Bob The Builder\""); // pq_opt_del -> pq_opt_del_dv
    ASSERT_TRUE(pq_opt_del_dv.has_value());
    size_t deleted_count_dv = db.delete_records(*pq_opt_del_dv); // deleted -> deleted_count_dv
    EXPECT_EQ(deleted_count_dv, 1);
    EXPECT_EQ(db.record_count(), 3);

    auto pq_opt_sel_dv = QueryParser::parse_select("SELECT * WHERE name = \"Bob The Builder\""); // pq_opt_sel -> pq_opt_sel_dv
    ASSERT_TRUE(pq_opt_sel_dv.has_value());
    auto results_sel_dv = db.get_formatted_select_results(*pq_opt_sel_dv); // results -> results_sel_dv
    // Если get_formatted_select_results возвращает пустой вектор, если нет данных (кроме заголовка)
    // или вектор с одним элементом (заголовок). Ваша реализация добавляет заголовки.
    ASSERT_EQ(results_sel_dv.size(), 1); // Только заголовок
    ASSERT_FALSE(results_sel_dv[0].empty()); 
}

TEST_F(DatabaseIntegrationTest, DeleteByMultipleCriteria) {
    auto pq_opt_del_mc = QueryParser::parse_select("SELECT * WHERE name = \"Alice Wonderland\" AND date = \"01/01/2024\""); // pq_opt_del -> pq_opt_del_mc
    ASSERT_TRUE(pq_opt_del_mc.has_value());
    size_t deleted_mc = db.delete_records(*pq_opt_del_mc); // deleted -> deleted_mc
    EXPECT_EQ(deleted_mc, 1);
    EXPECT_EQ(db.record_count(), 3);

    auto pq_opt_sel_rem_mc = QueryParser::parse_select("SELECT date WHERE name = \"Alice Wonderland\""); // pq_opt_sel_remaining -> pq_opt_sel_rem_mc
    ASSERT_TRUE(pq_opt_sel_rem_mc.has_value());
    auto results_rem_mc = db.get_formatted_select_results(*pq_opt_sel_rem_mc); // results_remaining -> results_rem_mc
    ASSERT_EQ(results_rem_mc.size(), 2); 
    ASSERT_GE(results_rem_mc[1].size(), 1U);
    EXPECT_EQ(results_rem_mc[1][0], "02/01/2024"); 
}

TEST_F(DatabaseIntegrationTest, DeleteNonExistentRecords) {
    auto pq_opt_del_ne = QueryParser::parse_select("SELECT * WHERE name = \"NonExistent User XYZ\""); // pq_opt_del -> pq_opt_del_ne
    ASSERT_TRUE(pq_opt_del_ne.has_value());
    size_t deleted_ne = db.delete_records(*pq_opt_del_ne); // deleted -> deleted_ne
    EXPECT_EQ(deleted_ne, 0);
    EXPECT_EQ(db.record_count(), 4); 
}


TEST_F(DatabaseIntegrationTest, CalculateBillAliceJan1) {
    auto pq_opt_cb = QueryParser::parse_select("SELECT * WHERE name = \"Alice Wonderland\""); // pq_opt -> pq_opt_cb
    ASSERT_TRUE(pq_opt_cb.has_value());
    Date start_date_cb = Date::from_string("01/01/2024").value(); // start_date -> start_date_cb
    Date end_date_cb = Date::from_string("01/01/2024").value();   // end_date -> end_date_cb

    auto bill_opt_cb = db.calculate_bill(*pq_opt_cb, start_date_cb, end_date_cb, tariff); // bill_opt -> bill_opt_cb
    ASSERT_TRUE(bill_opt_cb.has_value());
    EXPECT_DOUBLE_EQ(*bill_opt_cb, 885.00);
}

TEST_F(DatabaseIntegrationTest, CalculateBillEmptySelection) {
    auto pq_opt_cb_es = QueryParser::parse_select("SELECT * WHERE name = \"NonExistent User XYZ\""); // pq_opt -> pq_opt_cb_es
    ASSERT_TRUE(pq_opt_cb_es.has_value());
    Date start_date_cb_es = Date::from_string("01/01/2024").value(); // start_date -> start_date_cb_es
    Date end_date_cb_es = Date::from_string("01/01/2024").value();   // end_date -> end_date_cb_es

    auto bill_opt_cb_es = db.calculate_bill(*pq_opt_cb_es, start_date_cb_es, end_date_cb_es, tariff); // bill_opt -> bill_opt_cb_es
    ASSERT_TRUE(bill_opt_cb_es.has_value());
    EXPECT_DOUBLE_EQ(*bill_opt_cb_es, 0.0); 
}

TEST_F(DatabaseIntegrationTest, CalculateBillPeriodNoRecords) {
    auto pq_opt_cb_pnr = QueryParser::parse_select("SELECT * WHERE name = \"Alice Wonderland\""); // pq_opt -> pq_opt_cb_pnr
    ASSERT_TRUE(pq_opt_cb_pnr.has_value());
    Date start_date_cb_pnr = Date::from_string("01/03/2024").value(); // start_date -> start_date_cb_pnr
    Date end_date_cb_pnr = Date::from_string("31/03/2024").value();   // end_date -> end_date_cb_pnr

    auto bill_opt_cb_pnr = db.calculate_bill(*pq_opt_cb_pnr, start_date_cb_pnr, end_date_cb_pnr, tariff); // bill_opt -> bill_opt_cb_pnr
    ASSERT_TRUE(bill_opt_cb_pnr.has_value());
    EXPECT_DOUBLE_EQ(*bill_opt_cb_pnr, 0.0); 
}

TEST_F(DatabaseIntegrationTest, CalculateBillWithAlternativeTariff) {
    auto pq_opt_cb_at = QueryParser::parse_select("SELECT * WHERE name = \"Alice Wonderland\""); // pq_opt -> pq_opt_cb_at
    ASSERT_TRUE(pq_opt_cb_at.has_value());
    Date start_date_cb_at = Date::from_string("01/01/2024").value(); // start_date -> start_date_cb_at
    Date end_date_cb_at = Date::from_string("01/01/2024").value();   // end_date -> end_date_cb_at
    
    auto bill_opt_cb_at = db.calculate_bill(*pq_opt_cb_at, start_date_cb_at, end_date_cb_at, alt_tariff); // bill_opt -> bill_opt_cb_at
    ASSERT_TRUE(bill_opt_cb_at.has_value());
    EXPECT_DOUBLE_EQ(*bill_opt_cb_at, 117.00); // Трафик 1170 * 0.1 (из alt_tariff)
}


TEST_F(DatabaseIntegrationTest, SaveAndLoadConsistency) {
    ProviderRecord newUser_slc; // newUser -> newUser_slc
    newUser_slc.full_name = "Save Test User";
    newUser_slc.ip_address = IpAddress::from_string("9.8.7.6").value();
    newUser_slc.record_date = Date::from_string("01/06/2024").value();
    for(size_t i=0; i<HOURS_IN_DAY; ++i) newUser_slc.hourly_traffic[i] = {5,5};
    ASSERT_TRUE(newUser_slc.is_valid());
    db.add_record(newUser_slc);
    size_t count_before_save_slc = db.record_count(); // count_before_save -> count_before_save_slc
    EXPECT_EQ(count_before_save_slc, 5); 

    const std::string temp_save_file_slc = (temp_test_dir_db_int / "temp_db_save_load.txt").string(); // temp_save_file -> temp_save_file_slc
    ASSERT_TRUE(db.save_to_file(temp_save_file_slc));

    Database db_loaded_slc; // db_loaded -> db_loaded_slc
    ASSERT_TRUE(db_loaded_slc.load_from_file(temp_save_file_slc));
    EXPECT_EQ(db_loaded_slc.record_count(), count_before_save_slc);

    auto pq_opt_slc = QueryParser::parse_select("SELECT * WHERE name = \"Save Test User\""); // pq_opt -> pq_opt_slc
    ASSERT_TRUE(pq_opt_slc.has_value());
    auto results_slc = db_loaded_slc.get_formatted_select_results(*pq_opt_slc); // results -> results_slc
    ASSERT_EQ(results_slc.size(), 2); 
    ASSERT_GE(results_slc[1].size(), 1U);
    EXPECT_EQ(results_slc[1][0], "Save Test User");

    // std::remove(temp_save_file_slc.c_str()); // TearDown позаботится об этом через remove_all
}

TEST_F(DatabaseIntegrationTest, UpdateRecordSuccessNonKeyFields) {
    // Получаем первую запись Alice для модификации
    auto records_vec = db.select_records(
        *QueryParser::parse_select("SELECT * WHERE name = \"Alice Wonderland\" AND date = \"01/01/2024\"")
    );
    ASSERT_FALSE(records_vec.empty());
    ProviderRecord updated_data_urnkf = records_vec[0]; // updated_data -> updated_data_urnkf
    
    updated_data_urnkf.hourly_traffic[0] = {999, 888}; 

    int status_urnkf = db.update_record("Alice Wonderland", IpAddress::from_string("192.168.0.10").value(), Date::from_string("01/01/2024").value(), updated_data_urnkf); // status -> status_urnkf
    EXPECT_EQ(status_urnkf, 0); 

    auto results_urnkf = db.select_records(*QueryParser::parse_select("SELECT * WHERE name = \"Alice Wonderland\" AND date = \"01/01/2024\"")); // results -> results_urnkf
    ASSERT_EQ(results_urnkf.size(), 1);
    EXPECT_EQ(results_urnkf[0].hourly_traffic[0].incoming, 999);
    EXPECT_EQ(results_urnkf[0].hourly_traffic[0].outgoing, 888);
}

TEST_F(DatabaseIntegrationTest, UpdateRecordSuccessKeyFieldsNoConflict) {
    auto records_vec_urskf = db.select_records( // records_vec -> records_vec_urskf
        *QueryParser::parse_select("SELECT * WHERE name = \"Alice Wonderland\" AND date = \"01/01/2024\"")
    );
    ASSERT_FALSE(records_vec_urskf.empty());
    ProviderRecord updated_data_urskf = records_vec_urskf[0]; // updated_data -> updated_data_urskf
    
    updated_data_urskf.full_name = "Alicia Keys"; 
    // IP и дата остаются теми же, что и у оригинала, или меняются на уникальные

    int status_urskf = db.update_record("Alice Wonderland", IpAddress::from_string("192.168.0.10").value(), Date::from_string("01/01/2024").value(), updated_data_urskf); // status -> status_urskf
    EXPECT_EQ(status_urskf, 0); 

    auto old_results_urskf = db.select_records(*QueryParser::parse_select("SELECT * WHERE name = \"Alice Wonderland\" AND date = \"01/01/2024\"")); // old_results -> old_results_urskf
    EXPECT_TRUE(old_results_urskf.empty());

    auto new_results_urskf = db.select_records(*QueryParser::parse_select("SELECT * WHERE name = \"Alicia Keys\"")); // new_results -> new_results_urskf
    ASSERT_EQ(new_results_urskf.size(), 1);
    EXPECT_EQ(new_results_urskf[0].ip_address.to_string(), "192.168.0.10");
    EXPECT_EQ(new_results_urskf[0].record_date.to_string(), "01/01/2024");
}

TEST_F(DatabaseIntegrationTest, UpdateRecordNotFound) {
    ProviderRecord dummy_data_urnf; // dummy_data -> dummy_data_urnf
    dummy_data_urnf.full_name = "NonExistent"; 
    dummy_data_urnf.ip_address = IpAddress::from_string("0.0.0.0").value();
    dummy_data_urnf.record_date = Date::from_string("01/01/1990").value();
    for(size_t i=0; i<HOURS_IN_DAY; ++i) dummy_data_urnf.hourly_traffic[i] = {0,0};
    ASSERT_TRUE(dummy_data_urnf.is_valid());

    int status_urnf = db.update_record("NonExistent", IpAddress::from_string("0.0.0.0").value(), Date::from_string("01/01/1990").value(), dummy_data_urnf); // status -> status_urnf
    EXPECT_EQ(status_urnf, -1); 
}

TEST_F(DatabaseIntegrationTest, UpdateRecordConflictWithAnother) {
    auto records_vec_urcwa = db.select_records( // records_vec -> records_vec_urcwa
        *QueryParser::parse_select("SELECT * WHERE name = \"Charlie Chaplin\"") // Берем Чарли
    );
    ASSERT_FALSE(records_vec_urcwa.empty());
    ProviderRecord updated_charlie_data_urcwa = records_vec_urcwa[0]; // updated_charlie_data -> updated_charlie_data_urcwa

    // Пытаемся изменить Чарли так, чтобы его ключи совпали с существующим Бобом
    updated_charlie_data_urcwa.full_name = "Bob The Builder"; 
    updated_charlie_data_urcwa.ip_address = IpAddress::from_string("10.0.0.2").value(); 
    updated_charlie_data_urcwa.record_date = Date::from_string("01/01/2024").value(); 
                                                                                
    int status_urcwa = db.update_record( // status -> status_urcwa
        "Charlie Chaplin", 
        IpAddress::from_string("192.168.0.11").value(), 
        Date::from_string("01/02/2024").value(), 
        updated_charlie_data_urcwa
    );
    EXPECT_EQ(status_urcwa, -2); 
    EXPECT_EQ(db.record_count(), 4); 
}

TEST_F(DatabaseIntegrationTest, UpdateRecordToInvalidData) {
    auto records_vec_urtid = db.select_records( // records_vec -> records_vec_urtid
        *QueryParser::parse_select("SELECT * WHERE name = \"Alice Wonderland\" AND date = \"01/01/2024\"")
    );
    ASSERT_FALSE(records_vec_urtid.empty());
    ProviderRecord updated_data_urtid = records_vec_urtid[0]; // updated_data -> updated_data_urtid
    
    updated_data_urtid.full_name = ""; // Делаем запись невалидной

    int status_urtid = db.update_record("Alice Wonderland", IpAddress::from_string("192.168.0.10").value(), Date::from_string("01/01/2024").value(), updated_data_urtid); // status -> status_urtid
    EXPECT_EQ(status_urtid, -3); 
    EXPECT_EQ(db.record_count(), 4); 
}


TEST_F(DatabaseIntegrationTest, LoadInvalidRecordFile) {
    const std::string invalid_db_file_lirf = (temp_test_dir_db_int / "temp_invalid_db_integration.txt").string(); // invalid_db_file -> invalid_db_file_lirf
    std::string full_zero_traffic_lirf; // full_zero_traffic -> full_zero_traffic_lirf
    for(size_t i = 0; i < HOURS_IN_DAY; ++i) {
        full_zero_traffic_lirf += "0 0";
        if (i < HOURS_IN_DAY - 1) full_zero_traffic_lirf += " ";
    }
    std::vector<std::string> data_with_bad_traffic_line_lirf = { // data_with_bad_traffic_line -> data_with_bad_traffic_line_lirf
        "User A", "1.1.1.1", "01/01/2024", full_zero_traffic_lirf,
        "User B", "2.2.2.2", "02/01/2024", "not enough traffic data here", 
        "User C", "3.3.3.3", "03/01/2024", full_zero_traffic_lirf
    };
    ASSERT_NO_THROW(create_provider_test_file_for_db_int_test(invalid_db_file_lirf, data_with_bad_traffic_line_lirf));
    
    Database db_test_load_lirf; // db_test_load -> db_test_load_lirf
    EXPECT_TRUE(db_test_load_lirf.load_from_file(invalid_db_file_lirf)); // Ожидаем true, т.к. сам файл открывается, но не все записи валидны
    EXPECT_EQ(db_test_load_lirf.record_count(), 2); 
    
    auto pq_A_lirf = QueryParser::parse_select("SELECT name WHERE name = \"User A\""); // pq_A -> pq_A_lirf
    ASSERT_TRUE(pq_A_lirf.has_value());
    EXPECT_EQ(db_test_load_lirf.get_formatted_select_results(*pq_A_lirf).size(), 2U); 

    auto pq_C_lirf = QueryParser::parse_select("SELECT name WHERE name = \"User C\""); // pq_C -> pq_C_lirf
    ASSERT_TRUE(pq_C_lirf.has_value());
    EXPECT_EQ(db_test_load_lirf.get_formatted_select_results(*pq_C_lirf).size(), 2U); 
    
    auto pq_B_lirf = QueryParser::parse_select("SELECT name WHERE name = \"User B\""); // pq_B -> pq_B_lirf
    ASSERT_TRUE(pq_B_lirf.has_value());
    EXPECT_EQ(db_test_load_lirf.get_formatted_select_results(*pq_B_lirf).size(), 1U); // Только заголовок

    // std::remove(invalid_db_file_lirf.c_str()); // TearDown позаботится
}
