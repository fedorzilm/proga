// unit_tests/test_query_parser.cpp
#include "gtest/gtest.h"
#include "query_parser.h" // Включает common_defs.h, ip_address.h, date.h
#include "logger.h"       // Для возможной отладки тестов

class QueryParserTest : public ::testing::Test {
protected:
    QueryParser parser;
    Query resultQuery;

    // Вспомогательная функция для создания вектора трафика для тестов
    std::vector<double> create_traffic_data(int count, double start_val = 1.0, double step = 0.1) {
        std::vector<double> data;
        for (int i = 0; i < count; ++i) {
            data.push_back(start_val + static_cast<double>(i) * step);
        }
        return data;
    }

    std::string traffic_vector_to_string(const std::vector<double>& traffic) {
        std::string s;
        for (size_t i = 0; i < traffic.size(); ++i) {
            s += std::to_string(traffic[i]);
            if (i < traffic.size() - 1) s += " ";
        }
        return s;
    }
    void SetUp() override {
        // Logger::init(LogLevel::DEBUG); // Для отладки, если парсер логирует что-то важное
    }
};

// Тесты токенизации
TEST_F(QueryParserTest, Tokenize_Simple) {
    std::string query = "ADD FIO \"Иванов Иван\" IP \"1.2.3.4\"";
    // Ожидаем: "ADD", "FIO", "Иванов Иван", "IP", "1.2.3.4"
    // Внутренний метод tokenize приватный, поэтому тестируем через parseQuery и проверяем originalQueryString или поля Query
    // Для прямого теста tokenize пришлось бы сделать его public или friend.
    // Здесь будем тестировать косвенно или предполагать, что он работает, если parseQuery работает.
    // Для примера, если бы tokenize был public:
    // auto tokens = parser.tokenize(query);
    // EXPECT_EQ(tokens.size(), 5);
    // EXPECT_EQ(tokens[2], "Иванов Иван");
    SUCCEED() << "Тестирование приватного tokenize требует его открытия или косвенной проверки через parseQuery.";
}

TEST_F(QueryParserTest, Tokenize_UnclosedQuote) {
    std::string query_unclosed = "ADD FIO \"Иванов Иван";
    EXPECT_THROW(parser.parseQuery(query_unclosed), std::runtime_error);
}

TEST_F(QueryParserTest, Tokenize_EmptyStringInQuotes) {
     std::string query = "SELECT FIO \"\" END"; // Пустое имя в кавычках
     EXPECT_NO_THROW(resultQuery = parser.parseQuery(query));
     EXPECT_EQ(resultQuery.type, QueryType::SELECT);
     EXPECT_TRUE(resultQuery.params.useNameFilter);
     EXPECT_EQ(resultQuery.params.criteriaName, "");
}

TEST_F(QueryParserTest, ParseQuery_EmptyQuery) {
    resultQuery = parser.parseQuery("");
    EXPECT_EQ(resultQuery.type, QueryType::UNKNOWN);
    resultQuery = parser.parseQuery("   \t  \n  ");
    EXPECT_EQ(resultQuery.type, QueryType::UNKNOWN);
}

TEST_F(QueryParserTest, ParseQuery_UnknownCommand) {
    EXPECT_THROW(parser.parseQuery("NONEXISTENT_COMMAND params"), std::runtime_error);
}

// Тесты для команды ADD
TEST_F(QueryParserTest, AddCommand_Full) {
    std::vector<double> traffic_in_data = create_traffic_data(HOURS_IN_DAY, 1.0);
    std::vector<double> traffic_out_data = create_traffic_data(HOURS_IN_DAY, 0.5);
    std::string query_str = "ADD FIO \"Тестовый Пользователь\" IP \"192.168.0.1\" DATE \"01.01.2023\" TRAFFIC_IN " +
                           traffic_vector_to_string(traffic_in_data) + " TRAFFIC_OUT " +
                           traffic_vector_to_string(traffic_out_data) + " END";

    EXPECT_NO_THROW(resultQuery = parser.parseQuery(query_str));
    EXPECT_EQ(resultQuery.type, QueryType::ADD);
    EXPECT_EQ(resultQuery.params.subscriberNameData, "Тестовый Пользователь");
    EXPECT_EQ(resultQuery.params.ipAddressData.toString(), "192.168.0.1");
    EXPECT_EQ(resultQuery.params.dateData.toString(), "01.01.2023");
    EXPECT_TRUE(resultQuery.params.hasTrafficInToSet);
    EXPECT_TRUE(resultQuery.params.hasTrafficOutToSet);
    ASSERT_EQ(resultQuery.params.trafficInData.size(), static_cast<size_t>(HOURS_IN_DAY));
    ASSERT_EQ(resultQuery.params.trafficOutData.size(), static_cast<size_t>(HOURS_IN_DAY));
    for(int i=0; i < HOURS_IN_DAY; ++i) {
        EXPECT_DOUBLE_EQ(resultQuery.params.trafficInData[static_cast<size_t>(i)], traffic_in_data[static_cast<size_t>(i)]);
        EXPECT_DOUBLE_EQ(resultQuery.params.trafficOutData[static_cast<size_t>(i)], traffic_out_data[static_cast<size_t>(i)]);
    }
}

TEST_F(QueryParserTest, AddCommand_Minimal) {
    std::string query_str = "ADD FIO \"Минимальный\" IP \"10.0.0.1\" DATE \"15.07.2024\""; // END опционален
    EXPECT_NO_THROW(resultQuery = parser.parseQuery(query_str));
    EXPECT_EQ(resultQuery.type, QueryType::ADD);
    EXPECT_EQ(resultQuery.params.subscriberNameData, "Минимальный");
    EXPECT_EQ(resultQuery.params.ipAddressData.toString(), "10.0.0.1");
    EXPECT_EQ(resultQuery.params.dateData.toString(), "15.07.2024");
    EXPECT_FALSE(resultQuery.params.hasTrafficInToSet); // Трафик не указан
    EXPECT_FALSE(resultQuery.params.hasTrafficOutToSet);
    EXPECT_TRUE(resultQuery.params.trafficInData.empty()); // Должен быть пустым, будет заполнен нулями в обработчике
    EXPECT_TRUE(resultQuery.params.trafficOutData.empty());
}

TEST_F(QueryParserTest, AddCommand_MissingRequiredParams) {
    EXPECT_THROW(parser.parseQuery("ADD IP \"1.1.1.1\" DATE \"01.01.2000\""), std::runtime_error); // Нет FIO
    EXPECT_THROW(parser.parseQuery("ADD FIO \"Имя\" DATE \"01.01.2000\""), std::runtime_error);    // Нет IP
    EXPECT_THROW(parser.parseQuery("ADD FIO \"Имя\" IP \"1.1.1.1\""), std::runtime_error);         // Нет DATE
}

TEST_F(QueryParserTest, AddCommand_InvalidTrafficCount) {
    std::string query_str = "ADD FIO \"Имя\" IP \"1.1.1.1\" DATE \"01.01.2000\" TRAFFIC_IN 1 2 3"; // Мало данных
    EXPECT_THROW(parser.parseQuery(query_str), std::runtime_error);
}

TEST_F(QueryParserTest, AddCommand_InvalidParamValue) {
    EXPECT_THROW(parser.parseQuery("ADD FIO \"Имя\" IP \"bad-ip\" DATE \"01.01.2000\""), std::runtime_error);
    EXPECT_THROW(parser.parseQuery("ADD FIO \"Имя\" IP \"1.1.1.1\" DATE \"bad-date\""), std::runtime_error);
    std::string traffic_str_bad_num = "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 not_a_number";
    EXPECT_THROW(parser.parseQuery("ADD FIO \"Имя\" IP \"1.1.1.1\" DATE \"01.01.2000\" TRAFFIC_IN " + traffic_str_bad_num), std::runtime_error);
}

// Тесты для команды SELECT
TEST_F(QueryParserTest, SelectCommand_NoCriteria) {
    // SELECT без критериев не поддерживается текущим QueryParser::parseCriteriaParams,
    // он ожидает либо критерий, либо END. Это соответствует PRINT_ALL.
    // Если SELECT без всего должен работать как PRINT_ALL, это должно быть доработано в парсере.
    // Текущая реализация parseCriteriaParams выбросит ошибку, если не найдет критерий или END/SET/START_DATE/END_DATE
    // или дойдет до конца токенов.
    EXPECT_THROW(parser.parseQuery("SELECT"), std::runtime_error); // "Неизвестное ключевое слово в критериях" или подобное
    
    // Если нужен PRINT_ALL, то это отдельная команда
    resultQuery = parser.parseQuery("PRINT_ALL END");
    EXPECT_EQ(resultQuery.type, QueryType::PRINT_ALL);
}

TEST_F(QueryParserTest, SelectCommand_WithCriteria) {
    std::string query_str = "SELECT FIO \"Поиск Имя\" IP \"10.20.30.40\" DATE \"25.12.2023\" END";
    EXPECT_NO_THROW(resultQuery = parser.parseQuery(query_str));
    EXPECT_EQ(resultQuery.type, QueryType::SELECT);
    EXPECT_TRUE(resultQuery.params.useNameFilter);
    EXPECT_EQ(resultQuery.params.criteriaName, "Поиск Имя");
    EXPECT_TRUE(resultQuery.params.useIpFilter);
    EXPECT_EQ(resultQuery.params.criteriaIpAddress.toString(), "10.20.30.40");
    EXPECT_TRUE(resultQuery.params.useDateFilter);
    EXPECT_EQ(resultQuery.params.criteriaDate.toString(), "25.12.2023");
}

TEST_F(QueryParserTest, SelectCommand_PartialCriteria) {
    std::string query_str = "SELECT IP \"172.16.0.1\" END";
    EXPECT_NO_THROW(resultQuery = parser.parseQuery(query_str));
    EXPECT_EQ(resultQuery.type, QueryType::SELECT);
    EXPECT_FALSE(resultQuery.params.useNameFilter);
    EXPECT_TRUE(resultQuery.params.useIpFilter);
    EXPECT_EQ(resultQuery.params.criteriaIpAddress.toString(), "172.16.0.1");
    EXPECT_FALSE(resultQuery.params.useDateFilter);
}

TEST_F(QueryParserTest, SelectCommand_InvalidCriteriaValue) {
    EXPECT_THROW(parser.parseQuery("SELECT IP \"not-an-ip\""), std::runtime_error);
    EXPECT_THROW(parser.parseQuery("SELECT DATE \"32.13.2000\""), std::runtime_error);
}

// Тесты для команды DELETE (аналогичны SELECT по части критериев)
TEST_F(QueryParserTest, DeleteCommand_WithCriteria) {
    std::string query_str = "DELETE FIO \"Удалить Этого\" DATE \"01.01.1970\"";
    EXPECT_NO_THROW(resultQuery = parser.parseQuery(query_str));
    EXPECT_EQ(resultQuery.type, QueryType::DELETE);
    EXPECT_TRUE(resultQuery.params.useNameFilter);
    EXPECT_EQ(resultQuery.params.criteriaName, "Удалить Этого");
    EXPECT_TRUE(resultQuery.params.useDateFilter);
    EXPECT_EQ(resultQuery.params.criteriaDate.toString(), "01.01.1970");
    EXPECT_FALSE(resultQuery.params.useIpFilter);
}

// Тесты для команды EDIT
TEST_F(QueryParserTest, EditCommand_WithCriteriaAndSet) {
    std::vector<double> traffic = create_traffic_data(HOURS_IN_DAY, 3.0);
    std::string query_str = "EDIT FIO \"Старое Имя\" IP \"1.1.1.1\" SET FIO \"Новое Имя\" DATE \"02.02.2022\" TRAFFIC_IN " +
                            traffic_vector_to_string(traffic) + " END";
    EXPECT_NO_THROW(resultQuery = parser.parseQuery(query_str));
    EXPECT_EQ(resultQuery.type, QueryType::EDIT);
    // Критерии
    EXPECT_TRUE(resultQuery.params.useNameFilter);
    EXPECT_EQ(resultQuery.params.criteriaName, "Старое Имя");
    EXPECT_TRUE(resultQuery.params.useIpFilter);
    EXPECT_EQ(resultQuery.params.criteriaIpAddress.toString(), "1.1.1.1");
    // Данные для SET
    EXPECT_EQ(resultQuery.params.setData.at("FIO"), "Новое Имя");
    EXPECT_EQ(resultQuery.params.setData.at("DATE"), "02.02.2022");
    EXPECT_TRUE(resultQuery.params.hasTrafficInToSet);
    ASSERT_EQ(resultQuery.params.trafficInData.size(), static_cast<size_t>(HOURS_IN_DAY));
    EXPECT_DOUBLE_EQ(resultQuery.params.trafficInData[0], 3.0);
    EXPECT_FALSE(resultQuery.params.hasTrafficOutToSet);
}

TEST_F(QueryParserTest, EditCommand_NoCriteria_OnlySet) {
    std::string query_str = "EDIT SET IP \"8.8.8.8\""; // Должен быть SET после EDIT, если нет критериев
    // Текущая реализация parseCriteriaParams может не справиться с этим, если она ожидает хотя бы один критерий или END.
    // parseQuery вызывает parseCriteriaParams, затем parseEditSetParams.
    // Если parseCriteriaParams видит "SET", он должен остановиться.
    EXPECT_NO_THROW(resultQuery = parser.parseQuery(query_str));
    EXPECT_EQ(resultQuery.type, QueryType::EDIT);
    EXPECT_FALSE(resultQuery.params.useNameFilter);
    EXPECT_FALSE(resultQuery.params.useIpFilter);
    EXPECT_FALSE(resultQuery.params.useDateFilter);
    EXPECT_EQ(resultQuery.params.setData.at("IP"), "8.8.8.8");
}

TEST_F(QueryParserTest, EditCommand_EmptySetClause) {
    EXPECT_THROW(parser.parseQuery("EDIT FIO \"Имя\" SET END"), std::runtime_error);
    EXPECT_THROW(parser.parseQuery("EDIT FIO \"Имя\" SET"), std::runtime_error); // Пустой SET
}

TEST_F(QueryParserTest, EditCommand_InvalidSetField) {
    EXPECT_THROW(parser.parseQuery("EDIT FIO \"Имя\" SET NONEXISTENT_FIELD \"value\""), std::runtime_error);
}

// Тесты для CALCULATE_CHARGES
TEST_F(QueryParserTest, CalculateCharges_Full) {
    std::string query_str = "CALCULATE_CHARGES FIO \"Плательщик\" IP \"123.123.123.123\" DATE \"10.03.2024\" START_DATE \"01.03.2024\" END_DATE \"31.03.2024\" END";
    EXPECT_NO_THROW(resultQuery = parser.parseQuery(query_str));
    EXPECT_EQ(resultQuery.type, QueryType::CALCULATE_CHARGES);
    EXPECT_TRUE(resultQuery.params.useNameFilter);
    EXPECT_EQ(resultQuery.params.criteriaName, "Плательщик");
    EXPECT_TRUE(resultQuery.params.useIpFilter);
    EXPECT_EQ(resultQuery.params.criteriaIpAddress.toString(), "123.123.123.123");
    EXPECT_TRUE(resultQuery.params.useDateFilter); // Это DATE самой записи, не периода
    EXPECT_EQ(resultQuery.params.criteriaDate.toString(), "10.03.2024");
    EXPECT_TRUE(resultQuery.params.useStartDateFilter);
    EXPECT_EQ(resultQuery.params.criteriaStartDate.toString(), "01.03.2024");
    EXPECT_TRUE(resultQuery.params.useEndDateFilter);
    EXPECT_EQ(resultQuery.params.criteriaEndDate.toString(), "31.03.2024");
}

TEST_F(QueryParserTest, CalculateCharges_OnlyDates) {
    std::string query_str = "CALCULATE_CHARGES START_DATE \"01.01.2023\" END_DATE \"31.01.2023\"";
    EXPECT_NO_THROW(resultQuery = parser.parseQuery(query_str));
    EXPECT_EQ(resultQuery.type, QueryType::CALCULATE_CHARGES);
    EXPECT_FALSE(resultQuery.params.useNameFilter);
    EXPECT_TRUE(resultQuery.params.useStartDateFilter);
    EXPECT_EQ(resultQuery.params.criteriaStartDate.toString(), "01.01.2023");
    EXPECT_TRUE(resultQuery.params.useEndDateFilter);
    EXPECT_EQ(resultQuery.params.criteriaEndDate.toString(), "31.01.2023");
}

TEST_F(QueryParserTest, CalculateCharges_MissingDates) {
    EXPECT_THROW(parser.parseQuery("CALCULATE_CHARGES FIO \"Имя\" END_DATE \"01.01.2000\""), std::runtime_error); // Нет START_DATE
    EXPECT_THROW(parser.parseQuery("CALCULATE_CHARGES START_DATE \"01.01.2000\""), std::runtime_error);          // Нет END_DATE
}

// Тесты для LOAD/SAVE
TEST_F(QueryParserTest, LoadCommand) {
    std::string query_str = "LOAD \"mydata.db\" END";
    EXPECT_NO_THROW(resultQuery = parser.parseQuery(query_str));
    EXPECT_EQ(resultQuery.type, QueryType::LOAD);
    EXPECT_EQ(resultQuery.params.filename, "mydata.db");
}

TEST_F(QueryParserTest, LoadCommand_NoFilename) {
    EXPECT_THROW(parser.parseQuery("LOAD"), std::runtime_error);
    EXPECT_THROW(parser.parseQuery("LOAD END"), std::runtime_error);
}

TEST_F(QueryParserTest, SaveCommand_WithFilename) {
    std::string query_str = "SAVE \"backup.db\"";
    EXPECT_NO_THROW(resultQuery = parser.parseQuery(query_str));
    EXPECT_EQ(resultQuery.type, QueryType::SAVE);
    EXPECT_EQ(resultQuery.params.filename, "backup.db");
}

TEST_F(QueryParserTest, SaveCommand_NoFilename) {
    std::string query_str = "SAVE END";
    EXPECT_NO_THROW(resultQuery = parser.parseQuery(query_str));
    EXPECT_EQ(resultQuery.type, QueryType::SAVE);
    EXPECT_TRUE(resultQuery.params.filename.empty()); // Имя файла пустое, обработчик должен использовать currentFilename
}

// Тесты для EXIT, HELP, PRINT_ALL (простые)
TEST_F(QueryParserTest, ExitCommand) {
    resultQuery = parser.parseQuery("EXIT");
    EXPECT_EQ(resultQuery.type, QueryType::EXIT);
    resultQuery = parser.parseQuery("EXIT END");
    EXPECT_EQ(resultQuery.type, QueryType::EXIT);
}

TEST_F(QueryParserTest, HelpCommand) {
    resultQuery = parser.parseQuery("HELP");
    EXPECT_EQ(resultQuery.type, QueryType::HELP);
    resultQuery = parser.parseQuery("HELP END");
    EXPECT_EQ(resultQuery.type, QueryType::HELP);
}

TEST_F(QueryParserTest, PrintAllCommand) {
    resultQuery = parser.parseQuery("PRINT_ALL");
    EXPECT_EQ(resultQuery.type, QueryType::PRINT_ALL);
    resultQuery = parser.parseQuery("PRINT_ALL END");
    EXPECT_EQ(resultQuery.type, QueryType::PRINT_ALL);
}

TEST_F(QueryParserTest, ExtraTokensAfterEnd) {
    EXPECT_THROW(parser.parseQuery("PRINT_ALL END some_extra_token"), std::runtime_error);
}

TEST_F(QueryParserTest, CaseInsensitiveCommands) {
    resultQuery = parser.parseQuery("add FIO \"Имя\" IP \"1.1.1.1\" DATE \"01.01.2000\"");
    EXPECT_EQ(resultQuery.type, QueryType::ADD);
    resultQuery = parser.parseQuery("SeLeCt IP \"2.2.2.2\"");
    EXPECT_EQ(resultQuery.type, QueryType::SELECT);
    EXPECT_EQ(resultQuery.params.criteriaIpAddress.toString(), "2.2.2.2");
}

TEST_F(QueryParserTest, CaseInsensitiveKeywords) {
    std::string query_str = "SELECT fIo \"Критерий Имя\" ip \"3.3.3.3\" dAtE \"02.02.2002\" eNd";
    EXPECT_NO_THROW(resultQuery = parser.parseQuery(query_str));
    EXPECT_EQ(resultQuery.type, QueryType::SELECT);
    EXPECT_TRUE(resultQuery.params.useNameFilter);
    EXPECT_EQ(resultQuery.params.criteriaName, "Критерий Имя");
    EXPECT_TRUE(resultQuery.params.useIpFilter);
    EXPECT_EQ(resultQuery.params.criteriaIpAddress.toString(), "3.3.3.3");
    EXPECT_TRUE(resultQuery.params.useDateFilter);
    EXPECT_EQ(resultQuery.params.criteriaDate.toString(), "02.02.2002");

    std::vector<double> traffic = create_traffic_data(HOURS_IN_DAY, 1.0);
    query_str = "EDIT FIO \"Критерий\" sEt FiO \"Новое\" tRaFfIc_In " + traffic_vector_to_string(traffic);
    EXPECT_NO_THROW(resultQuery = parser.parseQuery(query_str));
    EXPECT_EQ(resultQuery.type, QueryType::EDIT);
    EXPECT_EQ(resultQuery.params.setData.at("FIO"), "Новое");
    EXPECT_TRUE(resultQuery.params.hasTrafficInToSet);
    EXPECT_EQ(resultQuery.params.trafficInData.size(), static_cast<size_t>(HOURS_IN_DAY));
}

TEST_F(QueryParserTest, AddCommand_QuotedValuesWithSpaces) {
    std::string query_str = "ADD FIO \"Иванов Иван Иванович\" IP \"192.168.1.1\" DATE \"01.01.2024\"";
    EXPECT_NO_THROW(resultQuery = parser.parseQuery(query_str));
    EXPECT_EQ(resultQuery.type, QueryType::ADD);
    EXPECT_EQ(resultQuery.params.subscriberNameData, "Иванов Иван Иванович");
}

TEST_F(QueryParserTest, AddCommand_TrafficBlock_TerminatedByNextKeyword) {
    // Проверяем, что parseTrafficBlock останавливается перед следующим ключевым словом, если введено меньше HOURS_IN_DAY значений
    std::string query_str = "ADD FIO \"Test\" IP \"1.1.1.1\" DATE \"01.01.2024\" TRAFFIC_IN 1.0 2.0 TRAFFIC_OUT 0.5 0.5";
    // Ожидаем ошибку, так как для TRAFFIC_IN предоставлено только 2 значения, а затем идет TRAFFIC_OUT
    EXPECT_THROW(parser.parseQuery(query_str), std::runtime_error);

    std::string query_str2 = "ADD FIO \"Test\" IP \"1.1.1.1\" DATE \"01.01.2024\" TRAFFIC_IN 1.0 2.0 END";
    EXPECT_THROW(parser.parseQuery(query_str2), std::runtime_error); // Ожидаем ошибку из-за нехватки данных в TRAFFIC_IN
}

TEST_F(QueryParserTest, EditCommand_SetTraffic_OnlyOneBlock) {
    std::vector<double> traffic_out_data = create_traffic_data(HOURS_IN_DAY, 0.5);
    std::string query_str = "EDIT FIO \"Some User\" SET TRAFFIC_OUT " + traffic_vector_to_string(traffic_out_data);
    EXPECT_NO_THROW(resultQuery = parser.parseQuery(query_str));
    EXPECT_EQ(resultQuery.type, QueryType::EDIT);
    EXPECT_FALSE(resultQuery.params.hasTrafficInToSet);
    EXPECT_TRUE(resultQuery.params.hasTrafficOutToSet);
    ASSERT_EQ(resultQuery.params.trafficOutData.size(), static_cast<size_t>(HOURS_IN_DAY));
    EXPECT_DOUBLE_EQ(resultQuery.params.trafficOutData[0], 0.5);
}

TEST_F(QueryParserTest, EditCommand_InvalidTrafficValueInSet) {
    std::string traffic_str_bad_num = "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 negative_val";
    std::string query_str = "EDIT FIO \"User\" SET TRAFFIC_IN " + traffic_str_bad_num;
    EXPECT_THROW(parser.parseQuery(query_str), std::runtime_error);
}


TEST_F(QueryParserTest, QueryWithMultipleSpacesBetweenTokens) {
    std::string query_str = "SELECT  FIO   \"Test  Name\"  IP  \"1.2.3.4\"   END";
    EXPECT_NO_THROW(resultQuery = parser.parseQuery(query_str));
    EXPECT_EQ(resultQuery.type, QueryType::SELECT);
    EXPECT_TRUE(resultQuery.params.useNameFilter);
    EXPECT_EQ(resultQuery.params.criteriaName, "Test  Name"); // Пробелы внутри кавычек сохраняются
    EXPECT_TRUE(resultQuery.params.useIpFilter);
    EXPECT_EQ(resultQuery.params.criteriaIpAddress.toString(), "1.2.3.4");
}

// Завершение теста для QueryParser
TEST_F(QueryParserTest, ExtraTokensAfterValidCommandAndEnd) {
    std::string query_str = "PRINT_ALL END unexpected_token";
    EXPECT_THROW(parser.parseQuery(query_str), std::runtime_error);
}

TEST_F(QueryParserTest, LoadCommand_FilenameWithSpaces) {
    std::string query_str = "LOAD \"my data file with spaces.db\" END";
    EXPECT_NO_THROW(resultQuery = parser.parseQuery(query_str));
    EXPECT_EQ(resultQuery.type, QueryType::LOAD);
    EXPECT_EQ(resultQuery.params.filename, "my data file with spaces.db");
}

TEST_F(QueryParserTest, ParseAddParams_DuplicateFields) {
    std::string query_str = "ADD FIO \"A\" IP \"1.1.1.1\" DATE \"01.01.2001\" FIO \"B\"";
    EXPECT_THROW(parser.parseQuery(query_str), std::runtime_error);
}

TEST_F(QueryParserTest, ParseCriteriaParams_DuplicateFields) {
    std::string query_str = "SELECT FIO \"A\" IP \"1.1.1.1\" FIO \"B\" END";
    EXPECT_THROW(parser.parseQuery(query_str), std::runtime_error);
}

TEST_F(QueryParserTest, ParseEditSetParams_DuplicateFields) {
    std::string query_str = "EDIT FIO \"A\" SET IP \"1.1.1.1\" DATE \"01.01.2001\" IP \"2.2.2.2\" END";
    EXPECT_THROW(parser.parseQuery(query_str), std::runtime_error);
}

TEST_F(QueryParserTest, CalculateChargesParams_DuplicateDateFields) {
    std::string query_str = "CALCULATE_CHARGES START_DATE \"01.01.2001\" END_DATE \"01.02.2001\" START_DATE \"03.03.2003\"";
    EXPECT_THROW(parser.parseQuery(query_str), std::runtime_error);
}
