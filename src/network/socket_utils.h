#ifndef SOCKET_UTILS_H
#define SOCKET_UTILS_H

#include <string>
#include <vector>
#include <stdexcept> 
#include <chrono>   // For std::chrono for sleep in non-blocking examples
#include <thread>   // For std::this_thread::sleep_for

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h> // For getaddrinfo, freeaddrinfo, gai_strerror
    #pragma comment(lib, "Ws2_32.lib")
    using socket_t = SOCKET;
    const socket_t INVALID_SOCKET_VAL = INVALID_SOCKET;
    using socklen_t_compat = int; // socklen_t is int on Windows for accept
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h> 
    #include <netdb.h>  
    #include <fcntl.h> 
    #include <cerrno>  
    using socket_t = int;
    const socket_t INVALID_SOCKET_VAL = -1;
    #define SD_BOTH SHUT_RDWR 
    inline int closesocket(socket_t s) { return close(s); }
    using socklen_t_compat = socklen_t;
#endif

class SocketException : public std::runtime_error {
public:
    SocketException(const std::string& message) : std::runtime_error(message) {}
};

namespace Network {

void init_networking(); 
void cleanup_networking(); 

class TCPSocket {
public:
    TCPSocket();
    ~TCPSocket();

    TCPSocket(const TCPSocket&) = delete;
    TCPSocket& operator=(const TCPSocket&) = delete;
    TCPSocket(TCPSocket&& other) noexcept;
    TCPSocket& operator=(TCPSocket&& other) noexcept;

    bool connect_to(const std::string& host, int port);
    bool listen_on(int port, int backlog = 10); 
    TCPSocket accept_connection(bool& success_flag); 

    int send_data(const std::string& data) const;
    std::string receive_framed_message() const; 

    void close_socket();
    bool is_valid() const;
    socket_t get_raw_socket() const { return sock_fd_; }
    void set_non_blocking(bool non_blocking);

private: 
    socket_t sock_fd_;
    bool is_listening_socket_ = false;

    explicit TCPSocket(socket_t fd); 
};

} 

#endif
