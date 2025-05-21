// unit_tests/test_client_request_processing.cpp
#include "gtest/gtest.h"
#include <gmock/gmock-matchers.h> 
#include "tcp_socket.h"
#include "common_defs.h" 
#include "logger.h"
// Включаем client_processing_logic.h, предполагая, что он создан
// и содержит объявление process_single_request_to_server
// #include "client_processing_logic.h" 
// Если файла нет, то для компиляции тестов нужна хотя бы заглушка объявления:
bool process_single_request_to_server(
    TCPSocket& socket,
    const std::string& query,
    std::ostream& out_stream_for_response,
    const std::string& client_id_log_prefix,
    int receive_timeout_ms
);


#include <sstream>
#include <vector>
#include <string>
#include <functional> 
#include <atomic>     
#include <thread>       
#include <future>       
#include <chrono>       
#include <cstring>     
#include <iostream> 
#include <iomanip> 
// #include <regex> // Больше не нужен, если escapeForRegex удален

// Функция escapeForRegex УДАЛЕНА, так как не используется

class ThreadGuardForClientTest {
public:
    explicit ThreadGuardForClientTest(std::thread& t) : t_(t) {}
    ~ThreadGuardForClientTest() {
        if (t_.joinable()) {
            try {
                t_.join();
            } catch (...) { /* Подавить исключения в деструкторе */ }
        }
    }
    ThreadGuardForClientTest(const ThreadGuardForClientTest&) = delete;
    ThreadGuardForClientTest& operator=(const ThreadGuardForClientTest&) = delete;
private:
    std::thread& t_;
};


const int CLIENT_TEST_LISTENER_PORT = 56797; 
const std::string CLIENT_TEST_LISTENER_HOST = "127.0.0.1";

struct CapturedClientTestData {
    std::string data_sent_by_client_query; 
    bool client_send_attempted = false;
    bool connection_accepted = false;
    std::string error_message_from_stub; 
};

class ClientRequestProcessingTest : public ::testing::Test {
protected:
    std::ostringstream response_stream_from_client_logic; 
    std::string log_prefix = "[TestClientProcessing] ";
    int default_timeout = 3000; 

    TCPSocket stub_listener_socket;
    std::unique_ptr<std::thread> stub_server_thread;
    std::promise<CapturedClientTestData> server_feedback_promise;
    std::atomic<bool> stub_server_should_stop;

    static void SetUpTestSuite() {
        #ifdef _WIN32
            TCPSocket::initializeSockets();
        #endif
        Logger::init(LogLevel::DEBUG, "test_client_processing.log");
    }

    static void TearDownTestSuite() {
        #ifdef _WIN32
            TCPSocket::cleanupSockets();
        #endif
        Logger::init(LogLevel::NONE);
    }

    void SetUp() override {
        response_stream_from_client_logic.str("");
        stub_server_should_stop.store(false);
        server_feedback_promise = std::promise<CapturedClientTestData>(); 
        TCPSocket check_port_socket;
        if (check_port_socket.bindSocket(CLIENT_TEST_LISTENER_PORT)) {
            check_port_socket.closeSocket();
        } else {
            Logger::warn("[ClientTestSetUp] Порт " + std::to_string(CLIENT_TEST_LISTENER_PORT) + " может быть занят. Попытка продолжить...");
        }
    }

    void TearDown() override {
        stub_server_should_stop.store(true);
        TCPSocket dummy_connector_to_wakeup_accept;
        if(stub_listener_socket.isValid()) { 
            dummy_connector_to_wakeup_accept.connectSocket(CLIENT_TEST_LISTENER_HOST, CLIENT_TEST_LISTENER_PORT);
            if(dummy_connector_to_wakeup_accept.isValid()) dummy_connector_to_wakeup_accept.closeSocket();
        }

        if (stub_server_thread && stub_server_thread->joinable()) {
            stub_server_thread->join();
        }
        if(stub_listener_socket.isValid()) stub_listener_socket.closeSocket();
    }

    static bool string_starts_with(const std::string& str, const std::string& prefix) {
        return str.rfind(prefix, 0) == 0;
    }

    static void StubServerForClientTestWorker(TCPSocket listener_socket,
                                             std::promise<CapturedClientTestData>& feedback_promise,
                                             std::atomic<bool>& should_stop,
                                             const std::vector<std::string>& responses_to_send_by_stub,
                                             int client_expected_timeout_for_stub_wait) {
        CapturedClientTestData feedback;
        TCPSocket client_connection_socket;
        Logger::debug("[StubServerClientTest] Заглушка: поток запущен. FD слушателя: " + std::to_string(listener_socket.getRawSocketDescriptor()));

        if(!listener_socket.isValid()){
            feedback.error_message_from_stub = "StubServerClientTest: Слушающий сокет невалиден при старте.";
            try { feedback_promise.set_value(std::move(feedback)); } catch(...) {}
            return;
        }
        
        listener_socket.setRecvTimeout(1000); 

        while(!should_stop.load() && !client_connection_socket.isValid()){
            client_connection_socket = listener_socket.acceptSocket();
            if (should_stop.load()) { 
                 Logger::debug("[StubServerClientTest] Заглушка: получен сигнал should_stop во время/после accept.");
                 break;
            }
             if (!client_connection_socket.isValid() && listener_socket.getLastSocketError() != 0 
                #ifndef _WIN32 
                 && listener_socket.getLastSocketError() != EAGAIN && listener_socket.getLastSocketError() != EWOULDBLOCK
                #else
                 && listener_socket.getLastSocketError() != WSAETIMEDOUT
                #endif
                ) {
                Logger::warn("[StubServerClientTest] Заглушка: acceptSocket вернул ошибку: " + listener_socket.getLastSocketErrorString());
            }
        }
        
        if(listener_socket.isValid()) listener_socket.closeSocket(); 
        Logger::debug("[StubServerClientTest] Заглушка: слушающий сокет закрыт.");

        if (should_stop.load() && !client_connection_socket.isValid()) {
             Logger::debug("[StubServerClientTest] Заглушка: остановка до принятия соединения (или accept не удался и should_stop установлен).");
             try { feedback_promise.set_value(std::move(feedback)); } catch(...) {}
             return;
        }

        if (!client_connection_socket.isValid()) {
            feedback.error_message_from_stub = "StubServerClientTest: Не удалось принять соединение. Ошибка listener_socket: " + listener_socket.getLastSocketErrorString();
            Logger::error("[StubServerClientTest] " + feedback.error_message_from_stub);
            try { feedback_promise.set_value(std::move(feedback)); } catch(...) {}
            return;
        }
        feedback.connection_accepted = true;
        Logger::info("[StubServerClientTest] Заглушка: соединение принято. FD клиента: " + std::to_string(client_connection_socket.getRawSocketDescriptor()));

        bool special_action_taken = false;
        if (!responses_to_send_by_stub.empty()) {
            const std::string& first_command = responses_to_send_by_stub[0];
            if (string_starts_with(first_command, "NO_RECEIVE_THEN_CLOSE")) {
                Logger::debug("[StubServerClientTest] Заглушка: сценарий NO_RECEIVE_THEN_CLOSE.");
                bool client_sent_data_successfully = false;
                client_connection_socket.setRecvTimeout(100); 
                feedback.data_sent_by_client_query = client_connection_socket.receiveAllDataWithLengthPrefix(client_sent_data_successfully, 100);
                feedback.client_send_attempted = true;
                if(client_sent_data_successfully) Logger::debug("[StubServerClientTest] Заглушка (NO_RECEIVE_THEN_CLOSE): получен запрос: " + feedback.data_sent_by_client_query);
                else Logger::debug("[StubServerClientTest] Заглушка (NO_RECEIVE_THEN_CLOSE): клиент ничего не отправил или ошибка чтения.");
                
                Logger::info("[StubServerClientTest] Заглушка (NO_RECEIVE_THEN_CLOSE): Закрытие сокета клиента.");
                client_connection_socket.closeSocket();
                special_action_taken = true;
            } else if (string_starts_with(first_command, "SEND_BAD_HEADER")) {
                Logger::debug("[StubServerClientTest] Заглушка: сценарий SEND_BAD_HEADER.");
                bool client_sent_data_successfully = false;
                client_connection_socket.setRecvTimeout(1000);
                feedback.data_sent_by_client_query = client_connection_socket.receiveAllDataWithLengthPrefix(client_sent_data_successfully, 1000);
                feedback.client_send_attempted = true;
                if(client_sent_data_successfully) Logger::debug("[StubServerClientTest] Заглушка (SEND_BAD_HEADER): получен запрос: " + feedback.data_sent_by_client_query);

                const char* bad_header_data = "This is not a valid L+P message"; 
                if (client_connection_socket.isValid() && client_connection_socket.sendAllData(bad_header_data, strlen(bad_header_data))) {
                    Logger::info("[StubServerClientTest] Заглушка: отправлен 'мусор' (SEND_BAD_HEADER).");
                } else if (client_connection_socket.isValid()) {
                    feedback.error_message_from_stub = "StubServerClientTest: Ошибка отправки 'мусора' (SEND_BAD_HEADER).";
                    Logger::error("[StubServerClientTest] " + feedback.error_message_from_stub);
                }
                client_connection_socket.closeSocket();
                special_action_taken = true;
            }
        }

        if (!special_action_taken) {
            bool client_sent_data_successfully = false;
            if (client_connection_socket.isValid() && !should_stop.load()) {
                 client_connection_socket.setRecvTimeout(1000); 
                 feedback.data_sent_by_client_query = client_connection_socket.receiveAllDataWithLengthPrefix(client_sent_data_successfully, 1000);
                 feedback.client_send_attempted = true;
                 if (client_sent_data_successfully) {
                     Logger::info("[StubServerClientTest] Заглушка: получен запрос от клиента: " + feedback.data_sent_by_client_query);
                 } else {
                     Logger::warn("[StubServerClientTest] Заглушка: не удалось получить запрос от клиента или клиент не отправил (ожидаемо для некоторых тестов). Ошибка: " + client_connection_socket.getLastSocketErrorString());
                 }
            }

            if (client_connection_socket.isValid() && !should_stop.load()) {
                if (responses_to_send_by_stub.empty()) { 
                    Logger::debug("[StubServerClientTest] Заглушка: нет ответов для отправки (сценарий таймаута на клиенте). Удержание соединения.");
                    auto start_wait = std::chrono::steady_clock::now();
                    while(!should_stop.load() && 
                          (std::chrono::steady_clock::now() - start_wait < std::chrono::milliseconds(client_expected_timeout_for_stub_wait + 500)) ) {
                        if (!client_connection_socket.isValid()) {
                             Logger::debug("[StubServerClientTest] Заглушка (таймаут): клиент закрыл соединение во время ожидания заглушки.");
                            break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                    Logger::debug("[StubServerClientTest] Заглушка: завершение ожидания для сценария таймаута.");
                } else { 
                    for (const auto& response_part : responses_to_send_by_stub) {
                        if (should_stop.load() || !client_connection_socket.isValid()) break;
                        if (string_starts_with(response_part, "NO_RECEIVE_THEN_CLOSE") || string_starts_with(response_part, "SEND_BAD_HEADER")) {
                            continue; 
                        }
                        if (!client_connection_socket.sendAllDataWithLengthPrefix(response_part)) {
                            feedback.error_message_from_stub = "StubServerClientTest: Ошибка отправки ответа клиенту (часть: '" + response_part.substr(0, 50) + "...'). Ошибка сокета: " + client_connection_socket.getLastSocketErrorString();
                            Logger::error("[StubServerClientTest] " + feedback.error_message_from_stub);
                            break; 
                        }
                        Logger::info("[StubServerClientTest] Заглушка: УСПЕШНО отправлена часть ответа, длина " + std::to_string(response_part.length()));
                    }
                }
            }
        }

        if(client_connection_socket.isValid()) client_connection_socket.closeSocket();
        Logger::info("[StubServerClientTest] Заглушка: Закрытие сокета клиента (если еще валиден). Установка promise.");
        try { feedback_promise.set_value(std::move(feedback)); } catch(const std::future_error& e) {
            Logger::warn(std::string("[StubServerClientTest] Заглушка: Исключение при установке promise (вероятно, уже установлено): ") + e.what());
        }
        Logger::info("[StubServerClientTest] Заглушка: поток завершен.");
    }

    void StartStubServer(const std::vector<std::string>& responses_to_send_to_client, int client_timeout_for_stub_wait = 3000) { 
        if (!stub_listener_socket.bindSocket(CLIENT_TEST_LISTENER_PORT)) {
            std::string err_msg = "Порт " + std::to_string(CLIENT_TEST_LISTENER_PORT) + " занят (bind). Ошибка: " + stub_listener_socket.getLastSocketErrorString();
            Logger::error("[StartStubServer] " + err_msg);
            GTEST_FAIL() << err_msg; 
            return;
        }
        ASSERT_TRUE(stub_listener_socket.listenSocket(1)) << "listen на порту " << CLIENT_TEST_LISTENER_PORT << " не удался.";
        
        std::vector<std::string> responses_for_thread = responses_to_send_to_client;

        stub_server_thread = std::make_unique<std::thread>([this, responses = std::move(responses_for_thread), client_timeout_for_stub_wait]() mutable { 
            TCPSocket moved_listener = std::move(this->stub_listener_socket); 
            StubServerForClientTestWorker(std::move(moved_listener), this->server_feedback_promise, this->stub_server_should_stop, responses, client_timeout_for_stub_wait); 
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); 
    }

    std::string formatServerResponseForTest(int status, const std::string& message,
                                     const std::string& payload_type,
                                     size_t records_in_payload,
                                     size_t total_records,
                                     const std::string& payload_data) {
        std::ostringstream oss;
        oss << SRV_HEADER_STATUS << ": " << status << "\n"
            << SRV_HEADER_MESSAGE << ": " << message << "\n"
            << SRV_HEADER_RECORDS_IN_PAYLOAD << ": " << records_in_payload << "\n"
            << SRV_HEADER_TOTAL_RECORDS << ": " << total_records << "\n"
            << SRV_HEADER_PAYLOAD_TYPE << ": " << payload_type << "\n"
            << SRV_HEADER_DATA_MARKER << "\n"
            << payload_data;
        return oss.str();
    }
};

// --- ТЕСТЫ ---

TEST_F(ClientRequestProcessingTest, ProcessSingleRequest_EmptyQuery) {
    TCPSocket dummy_socket; 
    bool result = process_single_request_to_server(dummy_socket, "", response_stream_from_client_logic, log_prefix, default_timeout);
    EXPECT_TRUE(result);
    EXPECT_TRUE(response_stream_from_client_logic.str().empty()) << "Вывод должен быть пустым для пустого запроса.";
}

TEST_F(ClientRequestProcessingTest, ProcessSingleRequest_OK_SimpleMessage) {
    std::string server_payload = "Сервер: Команда успешно выполнена.";
    std::string response_from_stub = formatServerResponseForTest(
        SRV_STATUS_OK, "OK Test Message", SRV_PAYLOAD_TYPE_SIMPLE_MESSAGE, 0, 0, server_payload
    );
    StartStubServer({response_from_stub}); 

    TCPSocket client_socket;
    ASSERT_TRUE(client_socket.connectSocket(CLIENT_TEST_LISTENER_HOST, CLIENT_TEST_LISTENER_PORT)) << "Клиент не смог подключиться к заглушке.";

    std::string query = "SOME_COMMAND END";
    bool success = process_single_request_to_server(client_socket, query, response_stream_from_client_logic, log_prefix, default_timeout);
    
    client_socket.closeSocket(); 
    
    auto future_feedback = server_feedback_promise.get_future();
    ASSERT_EQ(future_feedback.wait_for(std::chrono::seconds(5)), std::future_status::ready) << "Таймаут ожидания ответа от заглушки"; 
    auto feedback = future_feedback.get();
    EXPECT_TRUE(feedback.connection_accepted) << "Заглушка не приняла соединение. Сообщение заглушки: " << feedback.error_message_from_stub;
    EXPECT_TRUE(feedback.client_send_attempted) << "Заглушка не попыталась прочитать запрос клиента.";
    EXPECT_EQ(feedback.data_sent_by_client_query, query) << "Заглушка получила неверный запрос. Сообщение заглушки: " << feedback.error_message_from_stub;
    EXPECT_TRUE(feedback.error_message_from_stub.empty()) << "Заглушка сообщила об ошибке: " << feedback.error_message_from_stub;

    ASSERT_TRUE(success) << "process_single_request_to_server вернул false. Вывод клиента: " << response_stream_from_client_logic.str();
    std::string output = response_stream_from_client_logic.str();
    // std::cout << "DEBUG Output for OK_SimpleMessage:\n---\n" << output << "\n---" << std::endl;
    EXPECT_THAT(output, testing::HasSubstr("Сервер: OK Test Message")); 
    EXPECT_THAT(output, testing::HasSubstr(server_payload)); 
}

TEST_F(ClientRequestProcessingTest, ProcessSingleRequest_ServerErrorResponse) {
    std::string server_error_details = "Детали ошибки от сервера: что-то пошло не так.";
    std::string response_from_stub = formatServerResponseForTest(
        SRV_STATUS_SERVER_ERROR, "Internal Server Error Test", SRV_PAYLOAD_TYPE_ERROR_INFO, 0, 0, server_error_details
    );
    StartStubServer({response_from_stub});

    TCPSocket client_socket;
    ASSERT_TRUE(client_socket.connectSocket(CLIENT_TEST_LISTENER_HOST, CLIENT_TEST_LISTENER_PORT));

    std::string query = "TRIGGER_SERVER_ERROR";
    bool success = process_single_request_to_server(client_socket, query, response_stream_from_client_logic, log_prefix, default_timeout);
    
    client_socket.closeSocket();
    auto future_feedback = server_feedback_promise.get_future();
    ASSERT_EQ(future_feedback.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    auto feedback = future_feedback.get();
    EXPECT_TRUE(feedback.connection_accepted) << feedback.error_message_from_stub;
    EXPECT_EQ(feedback.data_sent_by_client_query, query) << feedback.error_message_from_stub;
    EXPECT_TRUE(feedback.error_message_from_stub.empty()) << feedback.error_message_from_stub;

    ASSERT_TRUE(success) << "process_single_request_to_server вернул false. Вывод клиента: " << response_stream_from_client_logic.str(); 
    std::string output = response_stream_from_client_logic.str();
    // std::cout << "DEBUG Output for ServerErrorResponse:\n---\n" << output << "\n---" << std::endl;
    EXPECT_THAT(output, testing::HasSubstr("Сервер: Internal Server Error Test"));
    EXPECT_THAT(output, testing::HasSubstr(server_error_details));
}


TEST_F(ClientRequestProcessingTest, ProcessSingleRequest_SendFails) {
    StartStubServer({"NO_RECEIVE_THEN_CLOSE"}); 

    TCPSocket client_socket;
    ASSERT_TRUE(client_socket.connectSocket(CLIENT_TEST_LISTENER_HOST, CLIENT_TEST_LISTENER_PORT));

    std::string query = "SOME_COMMAND END";
    bool success = process_single_request_to_server(client_socket, query, response_stream_from_client_logic, log_prefix, default_timeout);
    
    auto future_feedback = server_feedback_promise.get_future();
    ASSERT_EQ(future_feedback.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    auto feedback = future_feedback.get();
    EXPECT_TRUE(feedback.connection_accepted) << feedback.error_message_from_stub;
    
    ASSERT_FALSE(success) << "process_single_request_to_server должен вернуть false.";
    std::string output = response_stream_from_client_logic.str();
    // std::cout << "DEBUG Output for SendFails:\n---\n" << output << "\n---" << std::endl;
    EXPECT_THAT(output, testing::MatchesRegex(".*КЛИЕНТ: ОШИБКА (ОТПРАВКИ|ПОЛУЧЕНИЯ).*"));
}


TEST_F(ClientRequestProcessingTest, ProcessSingleRequest_ReceiveFails_Timeout) {
    int client_actual_timeout_ms = 250; 
    StartStubServer({}, client_actual_timeout_ms); 

    TCPSocket client_socket;
    ASSERT_TRUE(client_socket.connectSocket(CLIENT_TEST_LISTENER_HOST, CLIENT_TEST_LISTENER_PORT));

    std::string query = "REQUEST_EXPECTING_TIMEOUT";
    bool success = process_single_request_to_server(client_socket, query, response_stream_from_client_logic, log_prefix, client_actual_timeout_ms);
    
    client_socket.closeSocket();
    auto future_feedback = server_feedback_promise.get_future();
    ASSERT_EQ(future_feedback.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    auto feedback = future_feedback.get();
    EXPECT_TRUE(feedback.connection_accepted) << feedback.error_message_from_stub;
    EXPECT_TRUE(feedback.client_send_attempted); 
    EXPECT_EQ(feedback.data_sent_by_client_query, query) << feedback.error_message_from_stub;
    EXPECT_TRUE(feedback.error_message_from_stub.empty()) << feedback.error_message_from_stub;

    ASSERT_FALSE(success) << "process_single_request_to_server должен вернуть false при таймауте получения.";
    std::string output = response_stream_from_client_logic.str();
    // std::cout << "DEBUG Output for ReceiveFails_Timeout:\n---\n" << output << "\n---" << std::endl;
    EXPECT_THAT(output, testing::MatchesRegex(".*КЛИЕНТ: ОШИБКА ПОЛУЧЕНИЯ: Таймаут ответа от сервера.*"));
}


TEST_F(ClientRequestProcessingTest, ProcessSingleRequest_ProtocolError_BadHeader) {
    StartStubServer({"SEND_BAD_HEADER"}); 

    TCPSocket client_socket;
    ASSERT_TRUE(client_socket.connectSocket(CLIENT_TEST_LISTENER_HOST, CLIENT_TEST_LISTENER_PORT));

    std::string query = "REQUEST_EXPECTING_BAD_RESPONSE";
    bool success = process_single_request_to_server(client_socket, query, response_stream_from_client_logic, log_prefix, default_timeout);
    
    client_socket.closeSocket();
    auto future_feedback = server_feedback_promise.get_future();
    ASSERT_EQ(future_feedback.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    auto feedback = future_feedback.get();
    EXPECT_TRUE(feedback.connection_accepted) << feedback.error_message_from_stub;
    
    ASSERT_FALSE(success) << "process_single_request_to_server должен был вернуть false"; 
    std::string output = response_stream_from_client_logic.str();
    // std::cout << "DEBUG Output for ProtocolError_BadHeader:\n---\n" << output << "\n---" << std::endl;
    EXPECT_THAT(output, testing::MatchesRegex(".*КЛИЕНТ: ОШИБКА ПОЛУЧЕНИЯ.*")); 
}


TEST_F(ClientRequestProcessingTest, ProcessSingleRequest_MultiPartResponse) {
    std::string rec_payload1 = "Запись (Отображаемый Индекс в текущем наборе #0):\nRec1\n1.1.1.1\n01.01.2020\n1.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00\n1.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00";
    std::string rec_payload2 = "Запись (Отображаемый Индекс в текущем наборе #0):\nRec2\n2.2.2.2\n02.01.2020\n2.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00\n2.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00";
    
    std::vector<std::string> server_responses;
    server_responses.push_back(formatServerResponseForTest(SRV_STATUS_OK_MULTI_PART_BEGIN, "Начало", SRV_PAYLOAD_TYPE_PROVIDER_RECORDS_LIST, 1, 2, rec_payload1 + "\n"));
    server_responses.push_back(formatServerResponseForTest(SRV_STATUS_OK_MULTI_PART_CHUNK, "Продолжение", SRV_PAYLOAD_TYPE_PROVIDER_RECORDS_LIST, 1, 0, rec_payload2 + "\n"));
    server_responses.push_back(formatServerResponseForTest(SRV_STATUS_OK_MULTI_PART_END, "Конец", SRV_PAYLOAD_TYPE_NONE, 0, 0, ""));

    StartStubServer(server_responses);

    TCPSocket client_socket;
    ASSERT_TRUE(client_socket.connectSocket(CLIENT_TEST_LISTENER_HOST, CLIENT_TEST_LISTENER_PORT));

    std::string query = "PRINT_ALL";
    bool success = process_single_request_to_server(client_socket, query, response_stream_from_client_logic, log_prefix, default_timeout);
    
    client_socket.closeSocket();
    auto future_feedback = server_feedback_promise.get_future();
    ASSERT_EQ(future_feedback.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    auto feedback = future_feedback.get();
    EXPECT_TRUE(feedback.connection_accepted) << feedback.error_message_from_stub;
    EXPECT_EQ(feedback.data_sent_by_client_query, query) << feedback.error_message_from_stub;
    EXPECT_TRUE(feedback.error_message_from_stub.empty()) << "Заглушка сообщила об ошибке: " << feedback.error_message_from_stub;

    ASSERT_TRUE(success) << "process_single_request_to_server вернул false. Вывод клиента: " << response_stream_from_client_logic.str();
    std::string output = response_stream_from_client_logic.str();
    // std::cout << "DEBUG Output for MultiPartResponse:\n---\n" << output << "\n---" << std::endl; 
        
    std::string expected_begin_msg_actual_format = "Сервер: Начало Всего записей: 2. Записей в этой части: 1.\n";
    ASSERT_GE(output.length(), expected_begin_msg_actual_format.length()) << "Output короче, чем ожидаемое начальное сообщение.";
    EXPECT_EQ(output.substr(0, expected_begin_msg_actual_format.length()), expected_begin_msg_actual_format)
        << "Output не начинается с ожидаемого начального сообщения.\nExpected prefix:\n'" << expected_begin_msg_actual_format 
        << "'\nActual output starts with:\n'" << output.substr(0, std::min(output.length(), expected_begin_msg_actual_format.length() + 10)) << "...'";
    
    EXPECT_THAT(output, testing::HasSubstr(rec_payload1 + "\n")); 
    
    std::string expected_chunk_msg_user_part = "Сервер: Продолжение Осталось записей: 1. Записей в этой части: 1.\n";
    EXPECT_THAT(output, testing::HasSubstr(expected_chunk_msg_user_part));

    EXPECT_THAT(output, testing::HasSubstr(rec_payload2 + "\n")); 
    EXPECT_THAT(output, testing::HasSubstr("Сервер: Конец\n"));

    EXPECT_THAT(output, testing::Not(testing::HasSubstr("КЛИЕНТ ПРЕДУПРЕЖДЕНИЕ: Количество обработанных записей")));
}


TEST_F(ClientRequestProcessingTest, ProcessSingleRequest_MultiPart_MismatchRecordCount) {
    std::string rec_payload1 = "Rec1 Data";
    
    std::vector<std::string> server_responses;
    server_responses.push_back(formatServerResponseForTest(SRV_STATUS_OK_MULTI_PART_BEGIN, "Начало", SRV_PAYLOAD_TYPE_PROVIDER_RECORDS_LIST, 1, 2, rec_payload1 + "\n"));
    server_responses.push_back(formatServerResponseForTest(SRV_STATUS_OK_MULTI_PART_END, "Конец", SRV_PAYLOAD_TYPE_NONE, 0, 0, "")); 

    StartStubServer(server_responses);

    TCPSocket client_socket;
    ASSERT_TRUE(client_socket.connectSocket(CLIENT_TEST_LISTENER_HOST, CLIENT_TEST_LISTENER_PORT));

    std::string query = "PRINT_ALL_MISMATCH";
    bool success = process_single_request_to_server(client_socket, query, response_stream_from_client_logic, log_prefix, default_timeout);
    
    client_socket.closeSocket();
    auto future_feedback = server_feedback_promise.get_future();
    ASSERT_EQ(future_feedback.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    auto feedback = future_feedback.get();
    EXPECT_TRUE(feedback.connection_accepted) << feedback.error_message_from_stub;
    EXPECT_EQ(feedback.data_sent_by_client_query, query) << feedback.error_message_from_stub;
    EXPECT_TRUE(feedback.error_message_from_stub.empty()) << "Заглушка сообщила об ошибке: " << feedback.error_message_from_stub;

    ASSERT_TRUE(success) << "process_single_request_to_server вернул false. Вывод клиента: " << response_stream_from_client_logic.str();
    std::string output = response_stream_from_client_logic.str();
    // std::cout << "DEBUG Output for MultiPart_MismatchRecordCount:\n---\n" << output << "\n---" << std::endl;
    
    std::string expected_begin_msg_actual_format = "Сервер: Начало Всего записей: 2. Записей в этой части: 1.\n";
    ASSERT_GE(output.length(), expected_begin_msg_actual_format.length()) << "Output короче, чем ожидаемое начальное сообщение.";
    EXPECT_EQ(output.substr(0, expected_begin_msg_actual_format.length()), expected_begin_msg_actual_format)
        << "Output не начинается с ожидаемого начального сообщения.\nExpected prefix:\n'" << expected_begin_msg_actual_format 
        << "'\nActual output starts with:\n'" << output.substr(0, std::min(output.length(), expected_begin_msg_actual_format.length() + 10)) << "...'";
        
    EXPECT_THAT(output, testing::HasSubstr(rec_payload1 + "\n"));
    
    std::string expected_warning_regex = ".*КЛИЕНТ ПРЕДУПРЕЖДЕНИЕ: Количество обработанных записей \\(1\\) не совпадает с общим ожидаемым сервером \\(2\\).*";
    EXPECT_THAT(output, testing::MatchesRegex(expected_warning_regex));

    EXPECT_THAT(output, testing::HasSubstr("Сервер: Конец\n"));
}
