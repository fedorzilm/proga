/*!
 * \file tcp_socket.h
 * \brief Заголовочный файл для класса TCPSocket, представляющего TCP сокет.
 */
#ifndef TCP_SOCKET_H
#define TCP_SOCKET_H

#include "common_defs.h" 
#include <string>
#include <vector>
#include <atomic>       
#include <mutex>        

// Включаем системные заголовки для SOMAXCONN и других определений сокетов
#ifdef _WIN32
    #include <winsock2.h> // Для SOMAXCONN и других определений Windows Sockets
    // Ws2tcpip.h для inet_pton и др. обычно включается в tcp_socket.cpp или common_defs.h
#else
    #include <sys/socket.h> // Для SOMAXCONN и других определений POSIX Sockets
#endif


/*!
 * \brief Класс для кросс-платформенной работы с TCP сокетами.
 */
class TCPSocket final {
public:
    TCPSocket() noexcept;
    explicit TCPSocket(int socket_fd) noexcept;
    ~TCPSocket() noexcept;

    TCPSocket(const TCPSocket&) = delete;
    TCPSocket& operator=(const TCPSocket&) = delete;

    TCPSocket(TCPSocket&& other) noexcept;
    TCPSocket& operator=(TCPSocket&& other) noexcept;
    
    bool createSocket();
    bool bindSocket(int port);
    bool listenSocket(int backlog = SOMAXCONN); 
    
    TCPSocket acceptSocket(std::string* client_ip = nullptr, int* client_port = nullptr);

    bool connectSocket(const std::string& host, int port);
    void closeSocket() noexcept;

    int sendData(const char* buffer, size_t length);
    int receiveData(char* buffer, size_t length);

    bool sendAllData(const char* buffer, size_t length) const;
    int receiveAllData(char* buffer, size_t length_to_receive) const;
    bool sendAllDataWithLengthPrefix(const std::string& data) const;
    std::string receiveAllDataWithLengthPrefix(bool& success_flag, int timeout_ms = 0) const;

    bool setNonBlocking(bool non_blocking_mode);
    bool setRecvTimeout(int milliseconds);
    bool setSendTimeout(int milliseconds);

    bool isValid() const noexcept;
    int getRawSocketDescriptor() const noexcept;
    int getLastSocketError() const noexcept;
    std::string getLastSocketErrorString() const;

private:
    intptr_t socket_fd_; 
    mutable int last_error_code_;   

#ifdef _WIN32
    static std::atomic<int> wsa_init_count_;
    static std::mutex wsa_mutex_;
    static bool initializeWSA();
    static void cleanupWSA();
#endif
    
    static constexpr intptr_t PLATFORM_INVALID_SOCKET_FD = 
#ifdef _WIN32
        reinterpret_cast<intptr_t>(INVALID_SOCKET);
#else
        static_cast<intptr_t>(-1);
#endif
};

#endif // TCP_SOCKET_H
