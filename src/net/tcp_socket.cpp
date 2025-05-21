/*!
 * \file tcp_socket.cpp
 * \author Fedor Zilnitskiy
 * \brief Реализация класса TCPSocket для кросс-платформенной работы с TCP сокетами.
 */
#include "tcp_socket.h"
#include "logger.h" 
#include <string>
#include <vector>
#include <algorithm> 
#include <stdexcept> 
#include <iostream> 

#ifdef _WIN32
#include <ws2tcpip.h> 
#pragma comment(lib, "Ws2_32.lib")
#else
#include <netinet/in.h>
#include <arpa/inet.h>  
#include <unistd.h>     
#include <fcntl.h>      
#include <cerrno>       
#include <cstring>      
#endif

#ifdef _WIN32
std::atomic<int> TCPSocket::wsa_init_count_{0};
std::mutex TCPSocket::wsa_mutex_;

bool TCPSocket::initializeWSA() {
    std::lock_guard<std::mutex> lock(wsa_mutex_);
    if (wsa_init_count_.load() == 0) {
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            std::cerr << "TCPSocket: WSAStartup failed with error: " << result << std::endl;
            return false;
        }
    }
    wsa_init_count_++;
    return true;
}

void TCPSocket::cleanupWSA() {
    std::lock_guard<std::mutex> lock(wsa_mutex_);
    if (wsa_init_count_.load() > 0) { 
      wsa_init_count_--;
      if (wsa_init_count_.load() == 0) {
          WSACleanup();
      }
    }
}
#endif

TCPSocket::TCPSocket() noexcept : socket_fd_(PLATFORM_INVALID_SOCKET_FD), last_error_code_(0) {
#ifdef _WIN32
    initializeWSA(); 
#endif
}

TCPSocket::TCPSocket(int existing_fd) noexcept : socket_fd_(static_cast<intptr_t>(existing_fd)), last_error_code_(0) {
#ifdef _WIN32
    if (existing_fd != static_cast<int>(PLATFORM_INVALID_SOCKET_FD)) { // Only init if FD is potentially valid
        initializeWSA();
    }
#endif
}

TCPSocket::~TCPSocket() noexcept {
    closeSocket(); 
#ifdef _WIN32
     cleanupWSA(); 
#endif
}

TCPSocket::TCPSocket(TCPSocket&& other) noexcept
    : socket_fd_(other.socket_fd_), 
      last_error_code_(other.last_error_code_) {
    other.socket_fd_ = PLATFORM_INVALID_SOCKET_FD;
    other.last_error_code_ = 0;
    // WSA init/cleanup is handled by constructor/destructor, count should remain consistent
}

TCPSocket& TCPSocket::operator=(TCPSocket&& other) noexcept {
    if (this != &other) {
        closeSocket(); // Закрываем текущий сокет, если он был открыт
        socket_fd_ = other.socket_fd_;
        last_error_code_ = other.last_error_code_;
        other.socket_fd_ = PLATFORM_INVALID_SOCKET_FD;
        other.last_error_code_ = 0;
    }
    return *this;
}

bool TCPSocket::isValid() const noexcept {
    return socket_fd_ != PLATFORM_INVALID_SOCKET_FD;
}

int TCPSocket::getRawSocketDescriptor() const noexcept {
    return static_cast<int>(socket_fd_); 
}

int TCPSocket::getLastSocketError() const noexcept {
    return last_error_code_;
}

std::string TCPSocket::getLastSocketErrorString() const {
#ifdef _WIN32
    if (last_error_code_ == 0) return "No error.";
    char* s = nullptr;
    DWORD msg_len = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(last_error_code_),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&s), 0, nullptr);
    if (msg_len == 0 || s == nullptr) {
        return "Windows Sockets Error code " + std::to_string(last_error_code_) + " (FormatMessage failed)";
    }
    std::string error_string(s);
    LocalFree(s);
    // Удаляем \r\n с конца
    while (!error_string.empty() && (error_string.back() == '\n' || error_string.back() == '\r')) {
        error_string.pop_back();
    }
    return error_string;
#else
    if (last_error_code_ == 0) return "No error.";
    char err_buf[256]; 
    #if (_POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600) && ! _GNU_SOURCE // XSI-compliant strerror_r
        if (strerror_r(last_error_code_, err_buf, sizeof(err_buf)) == 0) {
            return std::string(err_buf);
        }
        return "POSIX error " + std::to_string(last_error_code_) + " (XSI strerror_r failed or returned error)";
    #else // GNU-specific strerror_r
        const char* err_msg_ptr = strerror_r(last_error_code_, err_buf, sizeof(err_buf)); 
        if (err_msg_ptr != nullptr) { 
            return std::string(err_msg_ptr);
        }
        return "Unknown POSIX error " + std::to_string(last_error_code_) + " (strerror_r GNU returned null)";
    #endif
#endif
}

void TCPSocket::closeSocket() noexcept {
    if (isValid()) {
        Logger::debug("TCPSocket: Closing socket: " + std::to_string(static_cast<int>(socket_fd_)));
#ifdef _WIN32
        ::closesocket(reinterpret_cast<SOCKET>(socket_fd_));
#else
        ::close(static_cast<int>(socket_fd_));
#endif
        socket_fd_ = PLATFORM_INVALID_SOCKET_FD;
    }
    last_error_code_ = 0; // Сбрасываем ошибку при закрытии
}

bool TCPSocket::createSocket() {
    if (isValid()) {
        Logger::warn("TCPSocket: Попытка создать сокет, когда он уже существует (валиден).");
        return true; 
    }
#ifdef _WIN32
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        last_error_code_ = WSAGetLastError();
        Logger::error("TCPSocket: ::socket() failed. Error: " + std::to_string(last_error_code_) + " (" + getLastSocketErrorString() + ")");
        return false;
    }
    socket_fd_ = reinterpret_cast<intptr_t>(s);
#else
    socket_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) { 
        last_error_code_ = errno;
        socket_fd_ = PLATFORM_INVALID_SOCKET_FD; 
        Logger::error("TCPSocket: ::socket() failed. Error: " + std::to_string(last_error_code_) + " (" + getLastSocketErrorString() + ")");
        return false;
    }
#endif
    Logger::debug("TCPSocket: Сокет успешно создан. FD: " + std::to_string(static_cast<int>(socket_fd_)));
    return true;
}

bool TCPSocket::bindSocket(int port) {
    if (!isValid() && !createSocket()) { 
        return false;
    }

    // Установка опции SO_REUSEADDR
#ifdef _WIN32
    BOOL bOptVal = TRUE;
    int bOptLen = sizeof(BOOL);
    if (setsockopt(reinterpret_cast<SOCKET>(socket_fd_), SOL_SOCKET, SO_REUSEADDR, (char*)&bOptVal, bOptLen) == SOCKET_ERROR) {
        last_error_code_ = WSAGetLastError();
        Logger::warn("TCPSocket: setsockopt(SO_REUSEADDR) failed for Win. Error: " + std::to_string(last_error_code_) + " (" + getLastSocketErrorString() + ")");
        // Не делаем это фатальной ошибкой, bind может все еще сработать
    } else {
        Logger::debug("TCPSocket: setsockopt(SO_REUSEADDR) successful for Win.");
    }
#else
    int optval = 1;
    if (setsockopt(static_cast<int>(socket_fd_), SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        last_error_code_ = errno;
        Logger::warn("TCPSocket: setsockopt(SO_REUSEADDR) failed for POSIX. Error: " + std::to_string(last_error_code_) + " (" + getLastSocketErrorString() + ")");
        // Не делаем это фатальной ошибкой
    } else {
        Logger::debug("TCPSocket: setsockopt(SO_REUSEADDR) successful for POSIX.");
    }
#endif


    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); 
    server_addr.sin_port = htons(static_cast<unsigned short>(port));

#ifdef _WIN32
    SOCKET current_sock_fd_winsock = reinterpret_cast<SOCKET>(socket_fd_);
    if (::bind(current_sock_fd_winsock, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == SOCKET_ERROR) {
        last_error_code_ = WSAGetLastError();
        Logger::error("TCPSocket: ::bind() failed on port " + std::to_string(port) + ". Error: " + std::to_string(last_error_code_) + " (" + getLastSocketErrorString() + ")");
        return false;
    }
#else
    if (::bind(static_cast<int>(socket_fd_), reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        last_error_code_ = errno;
        Logger::error("TCPSocket: ::bind() failed on port " + std::to_string(port) + ". Error: " + std::to_string(last_error_code_) + " (" + getLastSocketErrorString() + ")");
        return false;
    }
#endif
    Logger::info("TCPSocket: Сокет успешно привязан к порту " + std::to_string(port));
    return true;
}

// ... (остальная часть файла TCPSocket.cpp без изменений)...
bool TCPSocket::listenSocket(int backlog) {
    if (!isValid()) {
        Logger::error("TCPSocket: listenSocket() вызван на невалидном сокете.");
        return false;
    }
#ifdef _WIN32
    SOCKET current_sock_fd_winsock = reinterpret_cast<SOCKET>(socket_fd_);
    if (::listen(current_sock_fd_winsock, backlog) == SOCKET_ERROR) {
        last_error_code_ = WSAGetLastError();
        Logger::error("TCPSocket: ::listen() failed. Error: " + std::to_string(last_error_code_) + " (" + getLastSocketErrorString() + ")");
        return false;
    }
#else
    if (::listen(static_cast<int>(socket_fd_), backlog) < 0) {
        last_error_code_ = errno;
        Logger::error("TCPSocket: ::listen() failed. Error: " + std::to_string(last_error_code_) + " (" + getLastSocketErrorString() + ")");
        return false;
    }
#endif
    Logger::info("TCPSocket: Сокет переведен в режим прослушивания. Backlog: " + std::to_string(backlog));
    return true;
}

TCPSocket TCPSocket::acceptSocket(std::string* client_ip_str_ptr, int* client_port_ptr) {
    if (!isValid()) {
        Logger::error("TCPSocket: acceptSocket() вызван на невалидном (слушающем) сокете.");
        return TCPSocket(); 
    }
    
    Logger::debug("TCPSocket::acceptSocket: Ожидание входящего соединения на FD: " + std::to_string(static_cast<int>(socket_fd_)));
    
    sockaddr_in client_addr{};
    socklen_t client_addr_len = sizeof(client_addr);
    intptr_t accepted_fd_ptr = PLATFORM_INVALID_SOCKET_FD; 

#ifdef _WIN32
    SOCKET current_sock_fd_winsock = reinterpret_cast<SOCKET>(socket_fd_);
    SOCKET accepted_sock_winsock = ::accept(current_sock_fd_winsock, reinterpret_cast<sockaddr*>(&client_addr), &client_addr_len);
    accepted_fd_ptr = reinterpret_cast<intptr_t>(accepted_sock_winsock);
    
    // Logger::debug("TCPSocket::acceptSocket: ::accept (Win) вернул: " + std::to_string(static_cast<int>(accepted_fd_ptr)));
    if (accepted_sock_winsock == INVALID_SOCKET) {
        last_error_code_ = WSAGetLastError(); 
        Logger::error("TCPSocket: ::accept() (Win) failed. Error: " + std::to_string(last_error_code_) + " (" + getLastSocketErrorString() + ")");
        return TCPSocket(); 
    }
#else
    int accepted_sock_posix = ::accept(static_cast<int>(socket_fd_), reinterpret_cast<sockaddr*>(&client_addr), &client_addr_len);
    accepted_fd_ptr = static_cast<intptr_t>(accepted_sock_posix);

    // Logger::debug("TCPSocket::acceptSocket: ::accept (POSIX) вернул: " + std::to_string(accepted_sock_posix));
    if (accepted_sock_posix < 0) {
        last_error_code_ = errno; 
        Logger::error("TCPSocket: ::accept() (POSIX) failed. Error: " + std::to_string(last_error_code_) + " (" + getLastSocketErrorString() + ")");
        return TCPSocket(); 
    }
#endif

    if (client_ip_str_ptr || client_port_ptr) {
        char client_ip_buffer[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &client_addr.sin_addr, client_ip_buffer, INET_ADDRSTRLEN) != nullptr) {
            if(client_ip_str_ptr) *client_ip_str_ptr = client_ip_buffer;
        } else {
            if(client_ip_str_ptr) *client_ip_str_ptr = "N/A";
        }
        if(client_port_ptr) *client_port_ptr = ntohs(client_addr.sin_port);
        Logger::info("TCPSocket: Принято новое соединение от " + (client_ip_str_ptr ? *client_ip_str_ptr : "N/A") + ":" +
                     (client_port_ptr ? std::to_string(*client_port_ptr) : "N/A") +
                     ". Новый FD: " + std::to_string(static_cast<int>(accepted_fd_ptr)));
    } else {
        Logger::info("TCPSocket: Принято новое соединение. Новый FD: " + std::to_string(static_cast<int>(accepted_fd_ptr)));
    }
    
    return TCPSocket(static_cast<int>(accepted_fd_ptr)); 
}

bool TCPSocket::connectSocket(const std::string& host, int port) {
    if (!isValid() && !createSocket()) {
        return false;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(static_cast<unsigned short>(port));

    int pton_res = inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr);
    if (pton_res <= 0) {
        Logger::error("TCPSocket: inet_pton: неверный формат адреса или ошибка системы для хоста: " + host);
        if (pton_res < 0) { 
            #ifdef _WIN32
                last_error_code_ = WSAGetLastError();
            #else
                last_error_code_ = errno;
            #endif
            Logger::error("TCPSocket: inet_pton system error: " + std::to_string(last_error_code_) + " (" + getLastSocketErrorString() + ")");
        }
        return false;
    }
    Logger::debug("TCPSocket: Попытка подключения к " + host + ":" + std::to_string(port));
#ifdef _WIN32
    SOCKET current_sock_fd_winsock = reinterpret_cast<SOCKET>(socket_fd_);
    if (::connect(current_sock_fd_winsock, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == SOCKET_ERROR) {
        last_error_code_ = WSAGetLastError();
        Logger::error("TCPSocket: ::connect() failed. Error: " + std::to_string(last_error_code_) + " (" + getLastSocketErrorString() + ")");
        return false;
    }
#else
    if (::connect(static_cast<int>(socket_fd_), reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        last_error_code_ = errno;
        Logger::error("TCPSocket: ::connect() failed. Error: " + std::to_string(last_error_code_) + " (" + getLastSocketErrorString() + ")");
        return false;
    }
#endif
    Logger::info("TCPSocket: Успешное подключение к " + host + ":" + std::to_string(port));
    return true;
}


int TCPSocket::sendData(const char* buffer, size_t length) { 
    if (!isValid()) { last_error_code_ = EBADF; return -1; } 
    last_error_code_ = 0;
#ifdef _WIN32
    SOCKET current_sock_fd_winsock = reinterpret_cast<SOCKET>(socket_fd_);
    int len_int = (length > static_cast<size_t>((std::numeric_limits<int>::max)())) ? (std::numeric_limits<int>::max)() : static_cast<int>(length);
    int bytes_sent = ::send(current_sock_fd_winsock, buffer, len_int, 0);
    if (bytes_sent == SOCKET_ERROR) {
        last_error_code_ = WSAGetLastError();
        return -1;
    }
#else
    ssize_t bytes_sent = ::send(static_cast<int>(socket_fd_), buffer, length, 0); 
    if (bytes_sent < 0) {
        last_error_code_ = errno;
        return -1;
    }
#endif
    return static_cast<int>(bytes_sent);
}

int TCPSocket::receiveData(char* buffer, size_t length) { 
    if (!isValid()) { last_error_code_ = EBADF; return -1; }
    last_error_code_ = 0;
#ifdef _WIN32
    SOCKET current_sock_fd_winsock = reinterpret_cast<SOCKET>(socket_fd_);
    int len_int = (length > static_cast<size_t>((std::numeric_limits<int>::max)())) ? (std::numeric_limits<int>::max)() : static_cast<int>(length);
    int bytes_received = ::recv(current_sock_fd_winsock, buffer, len_int, 0);
    if (bytes_received == SOCKET_ERROR) {
        last_error_code_ = WSAGetLastError();
        return -1;
    }
#else
    ssize_t bytes_received = ::recv(static_cast<int>(socket_fd_), buffer, length, 0);
    if (bytes_received < 0) {
        last_error_code_ = errno;
        return -1;
    }
#endif
    return static_cast<int>(bytes_received);
}


bool TCPSocket::setNonBlocking(bool non_blocking_mode) {
    if (!isValid()) { last_error_code_ = EBADF; return false; }
    last_error_code_ = 0;
#ifdef _WIN32
    u_long mode = non_blocking_mode ? 1 : 0;
    SOCKET current_sock_fd_winsock = reinterpret_cast<SOCKET>(socket_fd_);
    if (ioctlsocket(current_sock_fd_winsock, FIONBIO, &mode) != 0) {
        last_error_code_ = WSAGetLastError();
        Logger::error("TCPSocket: ioctlsocket(FIONBIO) failed. Error: " + std::to_string(last_error_code_) + " (" + getLastSocketErrorString() + ")");
        return false;
    }
#else
    int flags = fcntl(static_cast<int>(socket_fd_), F_GETFL, 0);
    if (flags == -1) {
        last_error_code_ = errno;
        Logger::error("TCPSocket: fcntl(F_GETFL) failed. Error: " + std::to_string(last_error_code_) + " (" + getLastSocketErrorString() + ")");
        return false;
    }
    flags = non_blocking_mode ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (fcntl(static_cast<int>(socket_fd_), F_SETFL, flags) == -1) {
        last_error_code_ = errno;
        Logger::error("TCPSocket: fcntl(F_SETFL) failed. Error: " + std::to_string(last_error_code_) + " (" + getLastSocketErrorString() + ")");
        return false;
    }
#endif
    Logger::debug("TCPSocket: Non-blocking mode " + std::string(non_blocking_mode ? "enabled" : "disabled") + " for FD: " + std::to_string(static_cast<int>(socket_fd_)));
    return true;
}

bool TCPSocket::setRecvTimeout(int milliseconds) {
    if (!isValid()) { last_error_code_ = EBADF; return false;}
    last_error_code_ = 0;
#ifdef _WIN32
    DWORD timeout = static_cast<DWORD>(milliseconds);
    SOCKET current_sock_fd_winsock = reinterpret_cast<SOCKET>(socket_fd_);
    if (setsockopt(current_sock_fd_winsock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) < 0) {
        last_error_code_ = WSAGetLastError();
        Logger::error("TCPSocket: setsockopt(SO_RCVTIMEO) failed. Error: " + std::to_string(last_error_code_) + " (" + getLastSocketErrorString() + ")");
        return false;
    }
#else
    struct timeval timeout {};
    timeout.tv_sec = milliseconds / 1000;
    timeout.tv_usec = (milliseconds % 1000) * 1000;
    if (setsockopt(static_cast<int>(socket_fd_), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        last_error_code_ = errno;
        Logger::error("TCPSocket: setsockopt(SO_RCVTIMEO) failed. Error: " + std::to_string(last_error_code_) + " (" + getLastSocketErrorString() + ")");
        return false;
    }
#endif
    Logger::debug("TCPSocket: Receive timeout set to " + std::to_string(milliseconds) + "ms for FD: " + std::to_string(static_cast<int>(socket_fd_)));
    return true;
}

bool TCPSocket::setSendTimeout(int milliseconds) {
    if (!isValid()) { last_error_code_ = EBADF; return false; }
    last_error_code_ = 0;
#ifdef _WIN32
    DWORD timeout = static_cast<DWORD>(milliseconds);
    SOCKET current_sock_fd_winsock = reinterpret_cast<SOCKET>(socket_fd_);
    if (setsockopt(current_sock_fd_winsock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) < 0) {
        last_error_code_ = WSAGetLastError();
        Logger::error("TCPSocket: setsockopt(SO_SNDTIMEO) failed. Error: " + std::to_string(last_error_code_) + " (" + getLastSocketErrorString() + ")");
        return false;
    }
#else
    struct timeval timeout {};
    timeout.tv_sec = milliseconds / 1000;
    timeout.tv_usec = (milliseconds % 1000) * 1000;
    if (setsockopt(static_cast<int>(socket_fd_), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        last_error_code_ = errno;
        Logger::error("TCPSocket: setsockopt(SO_SNDTIMEO) failed. Error: " + std::to_string(last_error_code_) + " (" + getLastSocketErrorString() + ")");
        return false;
    }
#endif
    Logger::debug("TCPSocket: Send timeout set to " + std::to_string(milliseconds) + "ms for FD: " + std::to_string(static_cast<int>(socket_fd_)));
    return true;
}

bool TCPSocket::sendAllData(const char* data, size_t length) const {
    if (!isValid()) { Logger::warn("TCPSocket::sendAllData - Socket is not valid."); if (data != nullptr) const_cast<TCPSocket*>(this)->last_error_code_ = EBADF; return false; }
    if (data == nullptr && length > 0) { Logger::warn("TCPSocket::sendAllData - Data is null but length > 0."); const_cast<TCPSocket*>(this)->last_error_code_ = EINVAL; return false;}
    if (length == 0) return true;

    size_t total_sent = 0;
    while (total_sent < length) {
        int bytes_sent_this_call = const_cast<TCPSocket*>(this)->sendData(data + total_sent, length - total_sent);
        if (bytes_sent_this_call <= 0) {
            Logger::error("TCPSocket::sendAllData - Failed to send. Error: " + std::to_string(last_error_code_) + " ("+ getLastSocketErrorString() +"). Sent " + std::to_string(total_sent) + "/" + std::to_string(length));
            return false;
        }
        total_sent += static_cast<size_t>(bytes_sent_this_call);
    }
    return true;
}

int TCPSocket::receiveAllData(char* buffer, size_t length_to_receive) const {
    if (!isValid()) { Logger::warn("TCPSocket::receiveAllData - Socket is not valid."); if (buffer != nullptr) const_cast<TCPSocket*>(this)->last_error_code_ = EBADF; return -1; }
    if (buffer == nullptr && length_to_receive > 0) { Logger::warn("TCPSocket::receiveAllData - Buffer is null but length_to_receive > 0."); const_cast<TCPSocket*>(this)->last_error_code_ = EINVAL; return -1;}
    if (length_to_receive == 0) return 0;

    size_t total_received = 0;
    while (total_received < length_to_receive) {
        int bytes_received_this_call = const_cast<TCPSocket*>(this)->receiveData(buffer + total_received, length_to_receive - total_received);
        if (bytes_received_this_call < 0) {
            return total_received > 0 ? static_cast<int>(total_received) : -1;
        }
        if (bytes_received_this_call == 0) { 
            break;
        }
        total_received += static_cast<size_t>(bytes_received_this_call);
    }
    return static_cast<int>(total_received);
}

bool TCPSocket::sendAllDataWithLengthPrefix(const std::string& data_str) const {
    if (!isValid()) { Logger::warn("TCPSocket::sendAllDataWithLengthPrefix - Socket is not valid."); const_cast<TCPSocket*>(this)->last_error_code_ = EBADF; return false; }
    if (data_str.length() > MAX_MESSAGE_PAYLOAD_SIZE) { 
        Logger::error("TCPSocket::sendAllDataWithLengthPrefix - Data size (" + std::to_string(data_str.length()) +
                      ") exceeds MAX_MESSAGE_PAYLOAD_SIZE (" + std::to_string(MAX_MESSAGE_PAYLOAD_SIZE) + ").");
        const_cast<TCPSocket*>(this)->last_error_code_ = EMSGSIZE; 
        return false;
    }

    uint32_t len_host = static_cast<uint32_t>(data_str.length());
    uint32_t len_net = htonl(len_host);

    if (!sendAllData(reinterpret_cast<const char*>(&len_net), sizeof(len_net))) {
        Logger::error("TCPSocket::sendAllDataWithLengthPrefix - Failed to send length prefix.");
        return false;
    }

    if (len_host > 0) { // Отправляем данные, только если они есть
        if (!sendAllData(data_str.data(), len_host)) {
            Logger::error("TCPSocket::sendAllDataWithLengthPrefix - Failed to send data payload.");
            return false;
        }
    }
    return true;
}

std::string TCPSocket::receiveAllDataWithLengthPrefix(bool& success_flag, int timeout_ms) const {
    success_flag = false;
    if (!isValid()) { Logger::warn("TCPSocket::receiveAllDataWithLengthPrefix - Socket is not valid."); const_cast<TCPSocket*>(this)->last_error_code_ = EBADF; return ""; }

    TCPSocket* non_const_this = const_cast<TCPSocket*>(this);

    if (timeout_ms > 0) {
        if (!non_const_this->setRecvTimeout(timeout_ms)) {
            Logger::warn("TCPSocket::receiveAllDataWithLengthPrefix - Failed to set recv timeout " + std::to_string(timeout_ms) + "ms.");
            // Продолжаем, т.к. это может быть не фатально, если таймаут уже был установлен или не нужен.
        }
    }

    uint32_t len_net = 0;
    int len_bytes_received = receiveAllData(reinterpret_cast<char*>(&len_net), sizeof(len_net));

    if (len_bytes_received != static_cast<int>(sizeof(len_net))) {
        if (len_bytes_received == 0 && sizeof(len_net) > 0) {
            Logger::info("TCPSocket::receiveAllDataWithLengthPrefix - Connection closed by peer while reading length prefix.");
        } else if (len_bytes_received > 0) { // Частично прочитана длина - это ошибка протокола
             Logger::error("TCPSocket::receiveAllDataWithLengthPrefix - Partially read length prefix. Expected " +
                          std::to_string(sizeof(len_net)) + ", got " + std::to_string(len_bytes_received) +
                          ". Error: " + std::to_string(last_error_code_) + " (" + getLastSocketErrorString() + ")");
        } // Если < 0, last_error_code_ уже установлен
        return ""; // Возвращаем пустую строку при любой ошибке чтения длины
    }

    uint32_t len_host = ntohl(len_net);

    if (len_host > MAX_MESSAGE_PAYLOAD_SIZE) {
        Logger::error("TCPSocket::receiveAllDataWithLengthPrefix - Declared payload size (" + std::to_string(len_host) +
                      ") exceeds MAX_MESSAGE_PAYLOAD_SIZE (" + std::to_string(MAX_MESSAGE_PAYLOAD_SIZE) + "). Closing connection.");
        non_const_this->closeSocket(); // Разрываем соединение при ошибке протокола
        non_const_this->last_error_code_ = EMSGSIZE;
        return "";
    }

    if (len_host == 0) { // Корректное получение пустого сообщения
        success_flag = true;
        return "";
    }

    std::vector<char> data_buffer(len_host);
    int data_bytes_received = receiveAllData(data_buffer.data(), len_host);

    if (data_bytes_received != static_cast<int>(len_host)) {
         if (data_bytes_received == 0 && len_host > 0) {
            Logger::warn("TCPSocket::receiveAllDataWithLengthPrefix - Connection closed by peer while reading data payload. Expected " +
                          std::to_string(len_host) + ", got " + std::to_string(data_bytes_received) + " before close.");
        } else if (data_bytes_received > 0) { // Частично прочитаны данные
            Logger::error("TCPSocket::receiveAllDataWithLengthPrefix - Partially read data payload. Expected " +
                          std::to_string(len_host) + ", got " + std::to_string(data_bytes_received) +
                          ". Error: " + std::to_string(last_error_code_) + " (" + getLastSocketErrorString() + ")");
        }
        return ""; // Возвращаем пустую строку, т.к. не все данные получены
    }

    success_flag = true;
    return std::string(data_buffer.data(), data_buffer.size());
}
