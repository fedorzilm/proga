#include "gtest/gtest.h"
#include "tcp_socket.h"
#include "logger.h"
#include <thread>
#include <vector>
#include <array>
#include <cstring>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>

#ifndef _WIN32
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#else
// Winsock уже подключен через tcp_socket.h
#endif

// RAII wrapper для std::thread
class ThreadGuard {
public:
    explicit ThreadGuard(std::thread& t) : t_(t) {}
    ~ThreadGuard() {
        if (t_.joinable()) {
            Logger::debug("ThreadGuard: Автоматическое присоединение потока в деструкторе.");
            try {
                t_.join();
            } catch (const std::system_error& e) {
                Logger::error("ThreadGuard: Исключение при join в деструкторе: " + std::string(e.what()));
            } catch (...) {
                Logger::error("ThreadGuard: Неизвестное исключение при join в деструкторе.");
            }
        }
    }
    ThreadGuard(const ThreadGuard&) = delete;
    ThreadGuard& operator=(const ThreadGuard&) = delete;
private:
    std::thread& t_;
};


const int LOOPBACK_TEST_PORT = 55555;
const std::string LOOPBACK_TEST_HOST = "127.0.0.1";

// Предварительные объявления для функций, используемых в потоках
void loopback_server_thread_echo_function(TCPSocket* listen_socket_ptr,
                                          const std::string& expected_msg,
                                          std::string* received_by_server_ptr,
                                          bool* server_success_ptr,
                                          std::atomic<bool>* server_finished_flag_ptr,
                                          std::condition_variable* ready_cv_ptr,
                                          std::mutex* ready_mtx_ptr,
                                          std::atomic<bool>* is_ready_flag_ptr);

void loopback_server_sends_bad_length_prefix(TCPSocket* listen_socket_ptr,
                                             std::atomic<bool>* server_finished_flag,
                                             std::condition_variable* ready_cv,
                                             std::mutex* ready_mtx,
                                             std::atomic<bool>* is_ready_flag);

void loopback_server_does_not_send(TCPSocket* listen_socket_ptr,
                                   std::atomic<bool>* server_finished_flag,
                                   std::condition_variable* ready_cv,
                                   std::mutex* ready_mtx,
                                   std::atomic<bool>* is_ready_flag);


class TCPSocketTest : public ::testing::Test {
protected:
    int create_raw_socket() {
        #ifdef _WIN32
            TCPSocket dummy_for_wsa_init; // Для инициализации WSA
            SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (s == INVALID_SOCKET) return -1;
            return static_cast<int>(reinterpret_cast<intptr_t>(s)); // Безопасное приведение
        #else
            return socket(AF_INET, SOCK_STREAM, 0);
        #endif
    }

    void close_raw_socket(int fd) {
        if (fd < 0) return;
        #ifdef _WIN32
            if (fd == static_cast<int>(reinterpret_cast<intptr_t>(INVALID_SOCKET))) return;
            closesocket(reinterpret_cast<SOCKET>(static_cast<intptr_t>(fd)));
        #else
            close(fd);
        #endif
    }

    std::mutex server_ready_mutex;
    std::condition_variable server_ready_cv;
    std::atomic<bool> is_server_ready_atomic{false};

    void SetUp() override {
        is_server_ready_atomic.store(false);
    }
};

// Серверная функция для loopback_ConnectBindListenAcceptSendReceive
void loopback_server_thread_echo_function(TCPSocket* listen_socket_ptr,
                                          const std::string& expected_msg,
                                          std::string* received_by_server_ptr,
                                          bool* server_success_ptr,
                                          std::atomic<bool>* server_finished_flag_ptr,
                                          std::condition_variable* ready_cv_ptr,
                                          std::mutex* ready_mtx_ptr,
                                          std::atomic<bool>* is_ready_flag_ptr) {
    if(server_success_ptr) *server_success_ptr = false;
    try {
        {
            std::lock_guard<std::mutex> lk(*ready_mtx_ptr);
            if(is_ready_flag_ptr) is_ready_flag_ptr->store(true);
            Logger::debug("Серверный поток: готов к accept(), is_ready_flag установлен.");
        }
        if(ready_cv_ptr) ready_cv_ptr->notify_one();

        Logger::debug("Серверный поток: ожидание accept()...");
        TCPSocket client_handler_socket = listen_socket_ptr->acceptSocket();

        if (!client_handler_socket.isValid()) {
            Logger::error("Серверный поток: accept() вернул невалидный сокет. Ошибка: " + std::to_string(listen_socket_ptr->getLastSocketError()));
            if (server_finished_flag_ptr) server_finished_flag_ptr->store(true);
            return;
        }
        Logger::debug("Серверный поток: соединение принято, ожидание данных от клиента...");

        bool client_recv_success = false;
        std::string msg_from_client = client_handler_socket.receiveAllDataWithLengthPrefix(client_recv_success, 5000);
        if (received_by_server_ptr) *received_by_server_ptr = msg_from_client;

        if (client_recv_success) {
            Logger::debug("Серверный поток: получено от клиента: " + msg_from_client);
            if (msg_from_client == expected_msg) {
                Logger::debug("Серверный поток: сообщение совпало, отправка обратно...");
                if (client_handler_socket.sendAllDataWithLengthPrefix(msg_from_client)) {
                    if(server_success_ptr) *server_success_ptr = true;
                    Logger::debug("Серверный поток: эхо успешно отправлено.");
                } else {
                    Logger::error("Серверный поток: ошибка отправки эхо. Ошибка сокета: " + std::to_string(client_handler_socket.getLastSocketError()));
                }
            } else {
                 Logger::error("Серверный поток: полученное сообщение не совпало с ожидаемым. Ожидалось: '" + expected_msg + "', получено: '" + msg_from_client + "'");
            }
        } else {
             Logger::error("Серверный поток: ошибка получения данных от клиента. Ошибка сокета: " + std::to_string(client_handler_socket.getLastSocketError()));
        }
        client_handler_socket.closeSocket();
    } catch (const std::exception& e) {
        Logger::error("Серверный поток: поймано исключение std::exception: " + std::string(e.what()));
        std::cerr << "Серверный поток: поймано исключение std::exception: " << e.what() << std::endl;
    } catch (...) {
        Logger::error("Серверный поток: поймано неизвестное исключение.");
        std::cerr << "Серверный поток: поймано неизвестное исключение." << std::endl;
    }
    if (server_finished_flag_ptr) server_finished_flag_ptr->store(true);
    Logger::debug("Серверный поток: завершение.");
}

TEST_F(TCPSocketTest, DefaultConstructorAndIsValid) {
    TCPSocket sock;
    EXPECT_FALSE(sock.isValid());
    #ifdef _WIN32
        EXPECT_EQ(sock.getRawSocketDescriptor(), static_cast<int>(reinterpret_cast<intptr_t>(INVALID_SOCKET)));
    #else
        EXPECT_EQ(sock.getRawSocketDescriptor(), -1);
    #endif
    EXPECT_EQ(sock.getLastSocketError(), 0);
}

TEST_F(TCPSocketTest, ConstructorWithValidFd) {
    int raw_fd = create_raw_socket();
    if (raw_fd < 0 && (std::getenv("CI") == nullptr)) { // Не считать ошибкой в CI, если сокеты запрещены
         GTEST_SKIP() << "Не удалось создать сырой сокет для ConstructorWithValidFd. Код: " << raw_fd;
    }
    ASSERT_NE(raw_fd, -1) << "Ошибка создания сырого сокета: " << strerror(errno);
    { TCPSocket sock(raw_fd); EXPECT_TRUE(sock.isValid()); EXPECT_EQ(sock.getRawSocketDescriptor(), raw_fd); } // Деструктор sock закроет raw_fd
}

TEST_F(TCPSocketTest, ConstructorWithInvalidFd) {
    TCPSocket sock(-1); // Передаем невалидный дескриптор
    EXPECT_FALSE(sock.isValid());
}

TEST_F(TCPSocketTest, MoveConstructor) {
    int raw_fd = create_raw_socket();
     if (raw_fd < 0 && (std::getenv("CI") == nullptr)) {
         GTEST_SKIP() << "Не удалось создать сырой сокет для MoveConstructor. Код: " << raw_fd;
    }
    ASSERT_NE(raw_fd, -1);
    TCPSocket sock1(raw_fd);
    ASSERT_TRUE(sock1.isValid());
    int original_fd_val = sock1.getRawSocketDescriptor();
    TCPSocket sock2(std::move(sock1)); // Перемещающий конструктор
    EXPECT_TRUE(sock2.isValid());
    EXPECT_EQ(sock2.getRawSocketDescriptor(), original_fd_val); // sock2 теперь владеет дескриптором
    EXPECT_FALSE(sock1.isValid()); // sock1 больше не валиден
    #ifdef _WIN32
        EXPECT_EQ(sock1.getRawSocketDescriptor(), static_cast<int>(reinterpret_cast<intptr_t>(INVALID_SOCKET)));
    #else
        EXPECT_EQ(sock1.getRawSocketDescriptor(), -1);
    #endif
}

TEST_F(TCPSocketTest, MoveAssignmentOperator) {
    int raw_fd1 = create_raw_socket();
    int raw_fd2 = create_raw_socket();
    if ((raw_fd1 < 0 || raw_fd2 < 0) && (std::getenv("CI") == nullptr)) {
         GTEST_SKIP() << "Не удалось создать сырые сокеты для MoveAssignmentOperator.";
    }
    ASSERT_NE(raw_fd1, -1);
    ASSERT_NE(raw_fd2, -1);
    TCPSocket sock1(raw_fd1);
    TCPSocket sock2(raw_fd2);
    ASSERT_TRUE(sock1.isValid());
    ASSERT_TRUE(sock2.isValid());
    sock1 = std::move(sock2); // Перемещающее присваивание
    EXPECT_TRUE(sock1.isValid());
    EXPECT_EQ(sock1.getRawSocketDescriptor(), raw_fd2); // sock1 теперь владеет дескриптором от sock2
    EXPECT_FALSE(sock2.isValid()); // sock2 больше не валиден
    TCPSocket& sock_ref = sock1; // Тест на самоприсваивание через ссылку
    sock1 = std::move(sock_ref); // Самоприсваивание
    EXPECT_TRUE(sock1.isValid()); // Должен оставаться валидным
    EXPECT_EQ(sock1.getRawSocketDescriptor(), raw_fd2); // И владеть тем же дескриптором
}

TEST_F(TCPSocketTest, CloseSocketMakesInvalid) {
    int raw_fd = create_raw_socket();
     if (raw_fd < 0 && (std::getenv("CI") == nullptr)) {
         GTEST_SKIP() << "Не удалось создать сырой сокет для CloseSocketMakesInvalid.";
    }
    ASSERT_NE(raw_fd, -1);
    TCPSocket sock(raw_fd);
    ASSERT_TRUE(sock.isValid());
    sock.closeSocket();
    EXPECT_FALSE(sock.isValid());
}

TEST_F(TCPSocketTest, CloseSocket_Idempotent) {
    TCPSocket sock; // Невалидный сокет
    EXPECT_NO_THROW(sock.closeSocket()); // Закрытие невалидного сокета не должно вызывать исключений
    EXPECT_FALSE(sock.isValid());
    int raw_fd = create_raw_socket();
    if (raw_fd < 0 && (std::getenv("CI") == nullptr)) {
         GTEST_SKIP() << "Не удалось создать сырой сокет для CloseSocket_Idempotent.";
    }
    ASSERT_NE(raw_fd, -1);
    TCPSocket sock2(raw_fd);
    sock2.closeSocket(); // Первое закрытие
    EXPECT_NO_THROW(sock2.closeSocket()); // Повторное закрытие
    EXPECT_FALSE(sock2.isValid());
}

TEST_F(TCPSocketTest, SocketOptions_InvalidSocket) {
    TCPSocket sock; // Невалидный сокет
    EXPECT_FALSE(sock.setNonBlocking(true));
    EXPECT_FALSE(sock.setRecvTimeout(100));
    EXPECT_FALSE(sock.setSendTimeout(100));
}


TEST_F(TCPSocketTest, Loopback_ConnectBindListenAcceptSendReceive) {
    Logger::init(LogLevel::DEBUG); // Включить DEBUG для этого теста
    TCPSocket listen_sock;
    ASSERT_TRUE(listen_sock.bindSocket(LOOPBACK_TEST_PORT)) << "Bind не удался. Ошибка: " << listen_sock.getLastSocketError();
    ASSERT_TRUE(listen_sock.listenSocket(1)); // Бэклог 1 для простоты
    Logger::debug("Тест: Слушающий сокет создан и прослушивает порт " + std::to_string(LOOPBACK_TEST_PORT));

    std::string test_message = "Привет, мир сокетов!";
    std::string message_received_by_server;
    bool server_op_success = false;
    std::atomic<bool> server_thread_finished(false);

    std::thread server_thread(loopback_server_thread_echo_function,
                              &listen_sock,
                              test_message,
                              &message_received_by_server,
                              &server_op_success,
                              &server_thread_finished,
                              &server_ready_cv, &server_ready_mutex, &is_server_ready_atomic);
    ThreadGuard tg(server_thread); // Гарантирует join
    Logger::debug("Тест: Серверный поток запущен.");

    bool server_was_ready = false;
    TCPSocket client_sock;

    {
        std::unique_lock<std::mutex> lk(server_ready_mutex);
        Logger::debug("Тест: Ожидание готовности сервера к accept (is_server_ready_atomic)...");
        server_was_ready = server_ready_cv.wait_for(lk, std::chrono::seconds(2), [&]{ return is_server_ready_atomic.load(); }); // Уменьшено время ожидания
        EXPECT_TRUE(server_was_ready)
            << "Сервер не просигнализировал о готовности к accept вовремя.";
    }

    if (server_was_ready) {
        Logger::debug("Тест: Сервер готов к accept. Попытка подключения клиента...");
        ASSERT_TRUE(client_sock.connectSocket(LOOPBACK_TEST_HOST, LOOPBACK_TEST_PORT))
            << "Connect не удался. Ошибка: " << client_sock.getLastSocketError();
        Logger::debug("Тест: Клиент подключен. Отправка данных...");

        ASSERT_TRUE(client_sock.sendAllDataWithLengthPrefix(test_message));
        Logger::debug("Тест: Данные отправлены. Ожидание эхо...");

        bool client_recv_success = false;
        std::string echoed_message = client_sock.receiveAllDataWithLengthPrefix(client_recv_success, 5000);
        Logger::debug("Тест: Эхо получено. Успех: " + std::string(client_recv_success ? "true" : "false") + ", Сообщение: " + echoed_message);

        EXPECT_TRUE(client_recv_success);
        EXPECT_EQ(echoed_message, test_message);

        client_sock.closeSocket();
        Logger::debug("Тест: Клиентский сокет закрыт.");
    } else {
        Logger::error("Тест: Сервер не был готов, клиентская часть теста не будет выполнена.");
    }

    listen_sock.closeSocket();
    Logger::debug("Тест: Слушающий сокет закрыт.");

    if (server_was_ready) {
        for(int i=0; i<50 && !server_thread_finished.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));

        EXPECT_TRUE(server_op_success) << "Серверная часть операции завершилась неудачно.";
        if(server_op_success) { 
            EXPECT_EQ(message_received_by_server, test_message);
        }
        EXPECT_TRUE(server_thread_finished.load()) << "Серверный поток не установил флаг завершения.";
    }
    Logger::init(LogLevel::NONE); // Возвращаем на уровень по умолчанию или NONE
}


TEST_F(TCPSocketTest, SendReceive_EmptyString) {
    Logger::init(LogLevel::DEBUG);
    TCPSocket listen_sock;
    ASSERT_TRUE(listen_sock.bindSocket(LOOPBACK_TEST_PORT +1)); // Другой порт
    ASSERT_TRUE(listen_sock.listenSocket(1));

    std::string test_message = ""; // Пустая строка
    bool server_op_success = false;
    std::atomic<bool> server_thread_finished(false);
    std::string msg_recv_by_server;
    is_server_ready_atomic.store(false);


    std::thread server_thread([&]() { // Лямбда для потока сервера
        try {
            { std::lock_guard<std::mutex> lk(server_ready_mutex); is_server_ready_atomic.store(true); }
            server_ready_cv.notify_one();

            TCPSocket client_handler = listen_sock.acceptSocket();
            if (!client_handler.isValid()) { server_thread_finished.store(true); return; }

            bool recv_ok = false;
            std::string client_msg = client_handler.receiveAllDataWithLengthPrefix(recv_ok, 2000);
            msg_recv_by_server = client_msg;
            if (recv_ok && client_msg == test_message) {
                if (client_handler.sendAllDataWithLengthPrefix(client_msg)) {
                    server_op_success = true;
                }
            }
            client_handler.closeSocket();
        } catch (...) { /* Ignore in test thread for simplicity, check flags */ }
        server_thread_finished.store(true);
    });
    ThreadGuard tg(server_thread);

    bool server_was_ready = false;
    {
        std::unique_lock<std::mutex> lk(server_ready_mutex);
        server_was_ready = server_ready_cv.wait_for(lk, std::chrono::seconds(2), [&]{ return is_server_ready_atomic.load(); });
        EXPECT_TRUE(server_was_ready) << "Таймаут ожидания готовности сервера для SendReceive_EmptyString.";
    }

    if (server_was_ready) {
        TCPSocket client_sock;
        ASSERT_TRUE(client_sock.connectSocket(LOOPBACK_TEST_HOST, LOOPBACK_TEST_PORT + 1));
        ASSERT_TRUE(client_sock.sendAllDataWithLengthPrefix(test_message));

        bool client_recv_success = false;
        std::string echoed_message = client_sock.receiveAllDataWithLengthPrefix(client_recv_success, 2000);

        EXPECT_TRUE(client_recv_success);
        EXPECT_EQ(echoed_message, test_message);
        client_sock.closeSocket();
    } else {
         Logger::error("Тест SendReceive_EmptyString: Сервер не стал готов, клиентская часть теста пропускается.");
    }

    listen_sock.closeSocket();

    if (server_was_ready) {
        for(int i=0; i<50 && !server_thread_finished.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        EXPECT_TRUE(server_op_success);
        EXPECT_EQ(msg_recv_by_server, test_message);
        EXPECT_TRUE(server_thread_finished.load());
    }
    Logger::init(LogLevel::NONE);
}


TEST_F(TCPSocketTest, Send_ExceedsMaxLength_ReturnsFalse) {
    TCPSocket sock; // Невалидный сокет, но sendAllDataWithLengthPrefix проверяет длину до попытки отправки
    std::string too_long_data(MAX_MESSAGE_PAYLOAD_SIZE + 10, 'X');
    EXPECT_FALSE(sock.sendAllDataWithLengthPrefix(too_long_data));
}

void loopback_server_sends_bad_length_prefix(TCPSocket* listen_socket_ptr, std::atomic<bool>* server_finished_flag, std::condition_variable* ready_cv, std::mutex* ready_mtx, std::atomic<bool>* is_ready_flag) {
     try {
        { std::lock_guard<std::mutex> lk(*ready_mtx); is_ready_flag->store(true); }
        ready_cv->notify_one();

        TCPSocket client_handler = listen_socket_ptr->acceptSocket();
        if (!client_handler.isValid()) { if (server_finished_flag) server_finished_flag->store(true); return; }

        uint32_t bad_len_host = MAX_MESSAGE_PAYLOAD_SIZE + 1; // Длина больше максимальной
        uint32_t bad_len_net = htonl(bad_len_host);
        // Отправляем только некорректную длину, без данных
        client_handler.sendAllData(reinterpret_cast<const char*>(&bad_len_net), sizeof(bad_len_net));
        client_handler.closeSocket();
    } catch (...) {}
    if (server_finished_flag) server_finished_flag->store(true);
}

TEST_F(TCPSocketTest, Receive_ServerDeclaresTooLongPayload) {
    TCPSocket listen_sock;
    ASSERT_TRUE(listen_sock.bindSocket(LOOPBACK_TEST_PORT + 2));
    ASSERT_TRUE(listen_sock.listenSocket(1));
    std::atomic<bool> server_thread_finished(false);
    is_server_ready_atomic.store(false);

    std::thread server_thread(loopback_server_sends_bad_length_prefix, &listen_sock, &server_thread_finished, &server_ready_cv, &server_ready_mutex, &is_server_ready_atomic);
    ThreadGuard tg(server_thread);

    bool server_was_ready = false;
    {
        std::unique_lock<std::mutex> lk(server_ready_mutex);
        server_was_ready = server_ready_cv.wait_for(lk, std::chrono::seconds(2), [&]{ return is_server_ready_atomic.load(); });
        EXPECT_TRUE(server_was_ready) << "Таймаут ожидания готовности сервера для Receive_ServerDeclaresTooLongPayload.";
    }

    if (server_was_ready) {
        TCPSocket client_sock;
        ASSERT_TRUE(client_sock.connectSocket(LOOPBACK_TEST_HOST, LOOPBACK_TEST_PORT + 2));

        bool success = true; // receiveAllDataWithLengthPrefix должен вернуть false
        std::string data = client_sock.receiveAllDataWithLengthPrefix(success, 2000);

        EXPECT_FALSE(success); // Ожидаем ошибку, т.к. объявленная длина слишком велика
        EXPECT_TRUE(data.empty()); // Данные не должны быть получены
        client_sock.closeSocket();
    } else {
        Logger::error("Тест Receive_ServerDeclaresTooLongPayload: Сервер не стал готов, клиентская часть теста пропускается.");
    }

    listen_sock.closeSocket();
    if (server_was_ready) {
      for(int i=0; i<50 && !server_thread_finished.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
      EXPECT_TRUE(server_thread_finished.load());
    }
}

void loopback_server_does_not_send(TCPSocket* listen_socket_ptr, std::atomic<bool>* server_finished_flag, std::condition_variable* ready_cv, std::mutex* ready_mtx, std::atomic<bool>* is_ready_flag) {
    try {
        { std::lock_guard<std::mutex> lk(*ready_mtx); is_ready_flag->store(true); }
        ready_cv->notify_one();

        TCPSocket client_handler = listen_socket_ptr->acceptSocket();
        if (!client_handler.isValid()) { if (server_finished_flag) server_finished_flag->store(true); return; }
        // Сервер ничего не отправляет, просто ждет и закрывает
        std::this_thread::sleep_for(std::chrono::milliseconds(300)); // Даем клиенту время попытаться прочитать
        client_handler.closeSocket();
    } catch (...) {}
    if (server_finished_flag) server_finished_flag->store(true);
}

TEST_F(TCPSocketTest, ReceiveAllData_Timeout) {
    TCPSocket listen_sock;
    ASSERT_TRUE(listen_sock.bindSocket(LOOPBACK_TEST_PORT + 3));
    ASSERT_TRUE(listen_sock.listenSocket(1));
    std::atomic<bool> server_thread_finished(false);
    is_server_ready_atomic.store(false);

    std::thread server_thread(loopback_server_does_not_send, &listen_sock, &server_thread_finished, &server_ready_cv, &server_ready_mutex, &is_server_ready_atomic);
    ThreadGuard tg(server_thread);

    bool server_was_ready = false;
    {
        std::unique_lock<std::mutex> lk(server_ready_mutex);
        server_was_ready = server_ready_cv.wait_for(lk, std::chrono::seconds(2), [&]{ return is_server_ready_atomic.load(); });
        EXPECT_TRUE(server_was_ready) << "Таймаут ожидания готовности сервера для ReceiveAllData_Timeout.";
    }

    if (server_was_ready) {
        TCPSocket client_sock;
        ASSERT_TRUE(client_sock.connectSocket(LOOPBACK_TEST_HOST, LOOPBACK_TEST_PORT + 3));

        ASSERT_TRUE(client_sock.setRecvTimeout(150)); // Устанавливаем таймаут 150 мс

        bool success_flag = true; 
        std::string data = client_sock.receiveAllDataWithLengthPrefix(success_flag, 150);

        EXPECT_FALSE(success_flag); // Ожидаем неудачу из-за таймаута

        int err = client_sock.getLastSocketError();
        #ifdef _WIN32
            EXPECT_EQ(err, WSAETIMEDOUT) << "Ожидалась ошибка WSAETIMEDOUT, получено: " << err;
        #else
            EXPECT_TRUE(err == EAGAIN || err == EWOULDBLOCK) << "Ожидалась ошибка EAGAIN/EWOULDBLOCK, получено: " << err << " (" << strerror(err) << ")";
        #endif
        client_sock.closeSocket();
    } else {
         Logger::error("Тест ReceiveAllData_Timeout: Сервер не стал готов, клиентская часть теста пропускается.");
    }
    listen_sock.closeSocket();
    if (server_was_ready) {
        for(int i=0; i<50 && !server_thread_finished.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        EXPECT_TRUE(server_thread_finished.load());
    }
    // Logger::init(LogLevel::NONE);
}
