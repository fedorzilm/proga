#include "gtest/gtest.h"
#include "query_parser.h" 
#include "provider_record.h" 
#include "common_defs.h"     // Для HOURS_IN_DAY (если нужно в тестах)
#include "date.h"            // Для создания объектов Date в тестах
#include "ip_address.h"      // Для создания объектов IpAddress в тестах

TEST(QueryParserTest, ParseEmptyQuery) {
    auto q_opt = QueryParser::parse_select("");
    ASSERT_TRUE(q_opt.has_value());
    EXPECT_TRUE(q_opt->wants_all_fields());
    ASSERT_EQ(q_opt->select_fields.size(), 1);
    EXPECT_EQ(q_opt->select_fields[0], "*");
    EXPECT_TRUE(q_opt->criteria.empty());
    EXPECT_EQ(q_opt->sort_by_field, "");
    EXPECT_EQ(q_opt->sort_order, SortOrder::NONE);
}

TEST(QueryParserTest, ParseSimpleSelectAll) {
    auto q_opt = QueryParser::parse_select("SELECT *");
    ASSERT_TRUE(q_opt.has_value());
    EXPECT_TRUE(q_opt->wants_all_fields());
    ASSERT_EQ(q_opt->select_fields.size(), 1);
    EXPECT_EQ(q_opt->select_fields[0], "*");
}

TEST(QueryParserTest, ParseSelectSpecificFields) {
    auto q_opt = QueryParser::parse_select("SELECT name, ip, date");
    ASSERT_TRUE(q_opt.has_value());
    ASSERT_EQ(q_opt->select_fields.size(), 3);
    EXPECT_EQ(q_opt->select_fields[0], "name");
    EXPECT_EQ(q_opt->select_fields[1], "ip");
    EXPECT_EQ(q_opt->select_fields[2], "date");
    EXPECT_FALSE(q_opt->wants_all_fields());
}

TEST(QueryParserTest, ParseSelectSpecificFieldsWithSpaces) {
    auto q_opt = QueryParser::parse_select("  SELECT   name  ,   ip ");
    ASSERT_TRUE(q_opt.has_value());
    ASSERT_EQ(q_opt->select_fields.size(), 2);
    EXPECT_EQ(q_opt->select_fields[0], "name");
    EXPECT_EQ(q_opt->select_fields[1], "ip");
}

TEST(QueryParserTest, ParseSelectWithMixedCaseKeywords) {
    auto q_opt = QueryParser::parse_select("select Name, iP frOM table wHeRe dAte = \"01/01/2020\" sOrT bY Name Asc");
    // Ваша реализация QueryParser::parse_select преобразует ключевые слова в нижний регистр.
    // Имена полей в SELECT list и SORT BY остаются как есть (но в criteria field_name_lower хранится в нижнем).
    ASSERT_TRUE(q_opt.has_value());
    EXPECT_EQ(q_opt->select_fields[0], "Name"); // Сохраняет регистр как в запросе
    EXPECT_EQ(q_opt->select_fields[1], "iP");
    // "frOM table" - это часть списка полей, если ваш парсер не знает "FROM"
    // Ваш парсер не знает "FROM", поэтому "frOM" и "table" будут полями.
    // Это ожидаемое поведение для вашего парсера.
    ASSERT_EQ(q_opt->select_fields.size(), 4); // Name, iP, frOM, table
    EXPECT_EQ(q_opt->select_fields[2], "frOM");
    EXPECT_EQ(q_opt->select_fields[3], "table");

    ASSERT_EQ(q_opt->criteria.size(), 1);
    EXPECT_EQ(q_opt->criteria[0].field_name_lower, "date"); // Имя поля в criteria в нижнем регистре
    EXPECT_EQ(q_opt->criteria[0].field_name_raw, "dAte");   // Сохраненное сырое имя
    EXPECT_EQ(q_opt->sort_by_field, "Name"); // Сохраняет регистр
    EXPECT_EQ(q_opt->sort_order, SortOrder::ASC);
}


TEST(QueryParserTest, ParseWhereClauseSimple) {
    auto q_opt = QueryParser::parse_select("SELECT * WHERE name = \"Test Name\"");
    ASSERT_TRUE(q_opt.has_value());
    ASSERT_EQ(q_opt->criteria.size(), 1);
    EXPECT_EQ(q_opt->criteria[0].field_name_lower, "name");
    EXPECT_EQ(q_opt->criteria[0].field_name_raw, "name");
    EXPECT_EQ(q_opt->criteria[0].condition, Condition::EQ);
    ASSERT_TRUE(std::holds_alternative<std::string>(q_opt->criteria[0].value));
    EXPECT_EQ(std::get<std::string>(q_opt->criteria[0].value), "Test Name");
}

TEST(QueryParserTest, ParseWhereClauseMultipleConditions) {
    auto q_opt = QueryParser::parse_select("SELECT * WHERE ip = \"1.2.3.4\" AND date >= \"01/01/2023\" AND name CONTAINS \"User\"");
    ASSERT_TRUE(q_opt.has_value());
    ASSERT_EQ(q_opt->criteria.size(), 3);

    EXPECT_EQ(q_opt->criteria[0].field_name_lower, "ip");
    EXPECT_EQ(q_opt->criteria[0].condition, Condition::EQ);
    ASSERT_TRUE(std::holds_alternative<IpAddress>(q_opt->criteria[0].value));
    EXPECT_EQ(std::get<IpAddress>(q_opt->criteria[0].value).to_string(), "1.2.3.4");

    EXPECT_EQ(q_opt->criteria[1].field_name_lower, "date");
    EXPECT_EQ(q_opt->criteria[1].condition, Condition::GE);
    ASSERT_TRUE(std::holds_alternative<Date>(q_opt->criteria[1].value));
    EXPECT_EQ(std::get<Date>(q_opt->criteria[1].value).to_string(), "01/01/2023");
    
    EXPECT_EQ(q_opt->criteria[2].field_name_lower, "name");
    EXPECT_EQ(q_opt->criteria[2].condition, Condition::CONTAINS);
    ASSERT_TRUE(std::holds_alternative<std::string>(q_opt->criteria[2].value));
    EXPECT_EQ(std::get<std::string>(q_opt->criteria[2].value), "User");
}

TEST(QueryParserTest, ParseSortClause) {
    auto q_opt_date_desc = QueryParser::parse_select("SELECT * SORT BY date DESC");
    ASSERT_TRUE(q_opt_date_desc.has_value());
    EXPECT_EQ(q_opt_date_desc->sort_by_field, "date");
    EXPECT_EQ(q_opt_date_desc->sort_order, SortOrder::DESC);

    auto q_opt_name_asc = QueryParser::parse_select("SELECT name, ip SORT BY name ASC");
    ASSERT_TRUE(q_opt_name_asc.has_value());
    EXPECT_EQ(q_opt_name_asc->sort_by_field, "name");
    EXPECT_EQ(q_opt_name_asc->sort_order, SortOrder::ASC);

    auto q_opt_total_default_asc = QueryParser::parse_select("SELECT total_traffic SORT BY total_traffic");
    ASSERT_TRUE(q_opt_total_default_asc.has_value());
    EXPECT_EQ(q_opt_total_default_asc->sort_by_field, "total_traffic");
    EXPECT_EQ(q_opt_total_default_asc->sort_order, SortOrder::ASC); // По умолчанию ASC

    auto q_opt_sort_ip = QueryParser::parse_select("SELECT * SORT BY ip");
    ASSERT_TRUE(q_opt_sort_ip.has_value());
    EXPECT_EQ(q_opt_sort_ip->sort_by_field, "ip");
    EXPECT_EQ(q_opt_sort_ip->sort_order, SortOrder::ASC);

    auto q_opt_sort_total_incoming = QueryParser::parse_select("SELECT * SORT BY total_incoming DESC");
    ASSERT_TRUE(q_opt_sort_total_incoming.has_value());
    EXPECT_EQ(q_opt_sort_total_incoming->sort_by_field, "total_incoming");
    EXPECT_EQ(q_opt_sort_total_incoming->sort_order, SortOrder::DESC);
    
    auto q_opt_sort_total_outgoing = QueryParser::parse_select("SELECT * SORT BY total_outgoing");
    ASSERT_TRUE(q_opt_sort_total_outgoing.has_value());
    EXPECT_EQ(q_opt_sort_total_outgoing->sort_by_field, "total_outgoing");
    EXPECT_EQ(q_opt_sort_total_outgoing->sort_order, SortOrder::ASC);
    
    auto q_opt_sort_traffic_hour = QueryParser::parse_select("SELECT * SORT BY traffic_05 ASC");
    ASSERT_TRUE(q_opt_sort_traffic_hour.has_value());
    EXPECT_EQ(q_opt_sort_traffic_hour->sort_by_field, "traffic_05");
    EXPECT_EQ(q_opt_sort_traffic_hour->sort_order, SortOrder::ASC);

    auto q_opt_sort_traffic_in_hour = QueryParser::parse_select("SELECT * SORT BY traffic_in_10 DESC");
    ASSERT_TRUE(q_opt_sort_traffic_in_hour.has_value());
    EXPECT_EQ(q_opt_sort_traffic_in_hour->sort_by_field, "traffic_in_10");
    EXPECT_EQ(q_opt_sort_traffic_in_hour->sort_order, SortOrder::DESC);
    
    auto q_opt_sort_traffic_out_hour = QueryParser::parse_select("SELECT * SORT BY traffic_out_23");
    ASSERT_TRUE(q_opt_sort_traffic_out_hour.has_value());
    EXPECT_EQ(q_opt_sort_traffic_out_hour->sort_by_field, "traffic_out_23");
    EXPECT_EQ(q_opt_sort_traffic_out_hour->sort_order, SortOrder::ASC);
}

TEST(QueryParserTest, ParseFullQuery) {
    auto q_opt = QueryParser::parse_select("SELECT name, date WHERE ip != \"10.0.0.1\" AND date < \"10/10/2025\" SORT BY name ASC");
    ASSERT_TRUE(q_opt.has_value());
    ASSERT_EQ(q_opt->select_fields.size(), 2);
    EXPECT_EQ(q_opt->select_fields[0], "name");
    EXPECT_EQ(q_opt->select_fields[1], "date");
    ASSERT_EQ(q_opt->criteria.size(), 2);
    EXPECT_EQ(q_opt->criteria[0].field_name_lower, "ip");
    EXPECT_EQ(q_opt->criteria[0].condition, Condition::NE);
    EXPECT_EQ(q_opt->criteria[1].field_name_lower, "date");
    EXPECT_EQ(q_opt->criteria[1].condition, Condition::LT);
    EXPECT_EQ(q_opt->sort_by_field, "name");
    EXPECT_EQ(q_opt->sort_order, SortOrder::ASC);
}

TEST(QueryParserTest, ParseInvalidQueriesSyntax) {
    EXPECT_FALSE(QueryParser::parse_select("SELEC *").has_value());                     
    EXPECT_FALSE(QueryParser::parse_select("SELECT name ip").has_value()); // Нет запятой между полями в середине
    EXPECT_FALSE(QueryParser::parse_select("SELECT name, ip date").has_value()); // Нет запятой
    EXPECT_FALSE(QueryParser::parse_select("SELECT * WHERE name = ").has_value());      
    EXPECT_FALSE(QueryParser::parse_select("SELECT * WHERE name EQ \"val\"").has_value());
    EXPECT_FALSE(QueryParser::parse_select("SELECT * SORT date").has_value());          
    EXPECT_FALSE(QueryParser::parse_select("SELECT * WHERE name = \"Test\" AND").has_value());
    EXPECT_FALSE(QueryParser::parse_select("SELECT * WHERE name = \"MissingQuote").has_value());
    EXPECT_FALSE(QueryParser::parse_select("SELECT * SORT BY name WRONG_ORDER").has_value());
    EXPECT_FALSE(QueryParser::parse_select("SELECT name,").has_value()); // Запятая в конце списка полей
    EXPECT_FALSE(QueryParser::parse_select("SELECT name, WHERE date = \"01/01/2020\"").has_value()); // Запятая перед WHERE
    EXPECT_FALSE(QueryParser::parse_select("SELECT name, , ip").has_value()); // Две запятые подряд
    EXPECT_FALSE(QueryParser::parse_select("SELECT").has_value()); // Только SELECT
    EXPECT_FALSE(QueryParser::parse_select("SELECT * WHERE").has_value()); // WHERE без условий
    EXPECT_FALSE(QueryParser::parse_select("SELECT * SORT BY").has_value()); // SORT BY без поля
    EXPECT_FALSE(QueryParser::parse_select("SELECT * SORT BY name error").has_value()); // Не ASC/DESC после имени поля
}

TEST(QueryParserTest, ParseInvalidQueriesSemantics) {
    // Парсер проверяет имена полей в WHERE clause.
    EXPECT_FALSE(QueryParser::parse_select("SELECT * WHERE invalid_field = \"test\"").has_value()); 
    // Парсер проверяет допустимость операторов для типов полей.
    EXPECT_FALSE(QueryParser::parse_select("SELECT * WHERE name < \"test\"").has_value()); // '<' не для строк
    EXPECT_FALSE(QueryParser::parse_select("SELECT * WHERE ip CONTAINS \"1.2\"").has_value()); // 'CONTAINS' не для IP
    EXPECT_FALSE(QueryParser::parse_select("SELECT * WHERE date = \"not_a_date\"").has_value());
    EXPECT_FALSE(QueryParser::parse_select("SELECT * WHERE ip = \"not_an_ip\"").has_value());   
    // Имя поля в SORT BY на этапе парсинга не проверяется по семантике, только синтаксис.
    // Проверка существования поля для сортировки - задача Database::select_records.
    // EXPECT_FALSE(QueryParser::parse_select("SELECT * SORT BY non_existent_field").has_value()); // Это должно парситься успешно
    auto q_sort_invalid = QueryParser::parse_select("SELECT * SORT BY non_existent_field");
    ASSERT_TRUE(q_sort_invalid.has_value());
    EXPECT_EQ(q_sort_invalid->sort_by_field, "non_existent_field");

}

TEST(QueryParserTest, MatchesFunctionality) {
    ProviderRecord rec;
    rec.full_name = "John Doe";
    rec.ip_address = IpAddress::from_string("192.168.1.100").value();
    rec.record_date = Date::from_string("20/05/2024").value();
    // Для теста matches поля трафика не используются в условиях WHERE текущего парсера

    // Test EQ
    auto q_name_eq = QueryParser::parse_select("SELECT * WHERE name = \"John Doe\"");
    ASSERT_TRUE(q_name_eq.has_value());
    EXPECT_TRUE(QueryParser::matches(rec, *q_name_eq));

    auto q_name_neq_val = QueryParser::parse_select("SELECT * WHERE name = \"Jane Doe\"");
    ASSERT_TRUE(q_name_neq_val.has_value());
    EXPECT_FALSE(QueryParser::matches(rec, *q_name_neq_val));

    // Test CONTAINS
    auto q_name_contains = QueryParser::parse_select("SELECT * WHERE name CONTAINS \"Doe\"");
    ASSERT_TRUE(q_name_contains.has_value());
    EXPECT_TRUE(QueryParser::matches(rec, *q_name_contains));
    
    auto q_name_not_contains = QueryParser::parse_select("SELECT * WHERE name CONTAINS \"XYZ\"");
    ASSERT_TRUE(q_name_not_contains.has_value());
    EXPECT_FALSE(QueryParser::matches(rec, *q_name_not_contains));

    // Test date GE
    auto q_date_ge = QueryParser::parse_select("SELECT * WHERE date >= \"20/05/2024\"");
    ASSERT_TRUE(q_date_ge.has_value());
    EXPECT_TRUE(QueryParser::matches(rec, *q_date_ge));

    // Test IP LT
    auto q_ip_lt = QueryParser::parse_select("SELECT * WHERE ip < \"192.168.1.101\"");
    ASSERT_TRUE(q_ip_lt.has_value());
    EXPECT_TRUE(QueryParser::matches(rec, *q_ip_lt));

    // Test multiple criteria - all match
    auto q_multi_match = QueryParser::parse_select("SELECT * WHERE name CONTAINS \"John\" AND ip = \"192.168.1.100\" AND date <= \"20/05/2024\"");
    ASSERT_TRUE(q_multi_match.has_value());
    EXPECT_TRUE(QueryParser::matches(rec, *q_multi_match));

    // Test multiple criteria - one does not match
    auto q_multi_no_match = QueryParser::parse_select("SELECT * WHERE name CONTAINS \"John\" AND ip = \"1.1.1.1\"");
    ASSERT_TRUE(q_multi_no_match.has_value());
    EXPECT_FALSE(QueryParser::matches(rec, *q_multi_no_match));

    // Проверка, что порядок AND не влияет
    auto q_multi_match_reordered = QueryParser::parse_select("SELECT * WHERE date <= \"20/05/2024\" AND name CONTAINS \"John\" AND ip = \"192.168.1.100\"");
    ASSERT_TRUE(q_multi_match_reordered.has_value());
    EXPECT_TRUE(QueryParser::matches(rec, *q_multi_match_reordered));

    // Диапазонное условие для даты
    auto q_date_range = QueryParser::parse_select("SELECT * WHERE date >= \"01/05/2024\" AND date <= \"30/05/2024\"");
    ASSERT_TRUE(q_date_range.has_value());
    EXPECT_TRUE(QueryParser::matches(rec, *q_date_range)); 

    auto q_date_range_miss_low = QueryParser::parse_select("SELECT * WHERE date >= \"21/05/2024\" AND date <= \"30/05/2024\"");
    ASSERT_TRUE(q_date_range_miss_low.has_value());
    EXPECT_FALSE(QueryParser::matches(rec, *q_date_range_miss_low));

    auto q_date_range_miss_high = QueryParser::parse_select("SELECT * WHERE date >= \"01/05/2024\" AND date <= \"19/05/2024\"");
    ASSERT_TRUE(q_date_range_miss_high.has_value());
    EXPECT_FALSE(QueryParser::matches(rec, *q_date_range_miss_high));

    ProviderRecord rec2; // Другая запись для проверки несовпадений
    rec2.full_name = "Jane Smith";
    rec2.ip_address = IpAddress::from_string("10.0.0.1").value();
    rec2.record_date = Date::from_string("01/01/2023").value();

    EXPECT_FALSE(QueryParser::matches(rec2, *q_name_eq)); 
    EXPECT_FALSE(QueryParser::matches(rec2, *q_date_ge)); 
}

TEST(QueryParserTest, ParseSelectWithTrailingSemicolon) {
    auto q_opt = QueryParser::parse_select("SELECT * WHERE name = \"Test\";");
    ASSERT_TRUE(q_opt.has_value());
    ASSERT_EQ(q_opt->criteria.size(), 1);
    EXPECT_EQ(q_opt->criteria[0].field_name_lower, "name");

    auto q_opt2 = QueryParser::parse_select("SELECT *;");
    ASSERT_TRUE(q_opt2.has_value());
    EXPECT_TRUE(q_opt2->criteria.empty());
}

TEST(QueryParserTest, ParseSelectWithExtraSpacesAroundKeywords) {
    auto q_opt = QueryParser::parse_select("  SELECT   * WHERE   name   =   \"Test\"   SORT   BY   ip   ASC  ");
    ASSERT_TRUE(q_opt.has_value());
    EXPECT_TRUE(q_opt->wants_all_fields());
    ASSERT_EQ(q_opt->criteria.size(), 1);
    EXPECT_EQ(q_opt->criteria[0].field_name_lower, "name");
    EXPECT_EQ(q_opt->sort_by_field, "ip");
    EXPECT_EQ(q_opt->sort_order, SortOrder::ASC);
}
