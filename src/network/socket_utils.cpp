#include "socket_utils.h"
#include "network_protocol.h" 
#include <iostream>           
#include <cstring> // For strerror, memset, memcpy
#include <algorithm> // For std::min          
#include <vector>             
#include <thread> 
#include <chrono> 

namespace Network {

void init_networking() {
#ifdef _WIN32
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        throw SocketException("WSAStartup failed: " + std::to_string(result));
    }
#endif
} 

void cleanup_networking() {
#ifdef _WIN32
    WSACleanup();
#endif
} 

TCPSocket::TCPSocket() : sock_fd_(INVALID_SOCKET_VAL), is_listening_socket_(false) {}
TCPSocket::TCPSocket(socket_t fd) : sock_fd_(fd), is_listening_socket_(false) {}

TCPSocket::~TCPSocket() {
    close_socket();
}

TCPSocket::TCPSocket(TCPSocket&& other) noexcept : sock_fd_(other.sock_fd_), is_listening_socket_(other.is_listening_socket_) {
    other.sock_fd_ = INVALID_SOCKET_VAL;
    other.is_listening_socket_ = false;
}

TCPSocket& TCPSocket::operator=(TCPSocket&& other) noexcept {
    if (this != &other) {
        close_socket();
        sock_fd_ = other.sock_fd_;
        is_listening_socket_ = other.is_listening_socket_;
        other.sock_fd_ = INVALID_SOCKET_VAL;
        other.is_listening_socket_ = false;
    }
    return *this;
}

void TCPSocket::set_non_blocking(bool non_blocking) {
    if (!is_valid()) return;
#ifdef _WIN32
    u_long mode = non_blocking ? 1 : 0;
    if (ioctlsocket(sock_fd_, FIONBIO, &mode) != 0) {
        throw SocketException("ioctlsocket FIONBIO failed: " + std::to_string(WSAGetLastError()));
    }
#else
    int flags = fcntl(sock_fd_, F_GETFL, 0);
    if (flags == -1) {
        throw SocketException("fcntl F_GETFL failed: " + std::string(strerror(errno)));
    }
    flags = non_blocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (fcntl(sock_fd_, F_SETFL, flags) == -1) {
        throw SocketException("fcntl F_SETFL failed: " + std::string(strerror(errno)));
    }
#endif
}

bool TCPSocket::connect_to(const std::string& host, int port) {
    if (is_valid()) close_socket();

    addrinfo hints{};
    hints.ai_family = AF_INET; // Force IPv4, can be AF_UNSPEC for IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    
    addrinfo *result_addr_info = nullptr;
    int gai_status = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result_addr_info);
    if (gai_status != 0) {
        std::cerr << "getaddrinfo failed for " << host << ":" << port << " - " << gai_strerror(gai_status) << std::endl;
        return false;
    }

    bool connected = false;
    // Iterate through all returned addresses and try to connect
    for(addrinfo* rp = result_addr_info; rp != nullptr; rp = rp->ai_next) {
        sock_fd_ = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (!is_valid()) {
            std::cerr << "Socket creation failed: " << strerror(errno) << std::endl;
            continue; // Try next address
        }

        if (connect(sock_fd_, rp->ai_addr, static_cast<int>(rp->ai_addrlen)) == 0) {
            connected = true; // Success
            break; 
        } else {
            // std::cerr << "Connect attempt failed: " << strerror(errno) << std::endl; // Can be verbose
            closesocket(sock_fd_);
            sock_fd_ = INVALID_SOCKET_VAL;
        }
    }
    freeaddrinfo(result_addr_info); // Free the structure

    if (!connected) {
        std::cerr << "All connect attempts failed to " << host << ":" << port << "." << std::endl;
        return false;
    }
    is_listening_socket_ = false;
    return true;
}

bool TCPSocket::listen_on(int port, int backlog) {
    if (is_valid()) close_socket();

    sock_fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (!is_valid()) {
        std::cerr << "Socket creation failed for listening: " << strerror(errno) << std::endl;
        return false;
    }

    int opt = 1;
#ifdef _WIN32
    if (setsockopt(sock_fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt)) < 0) {
        std::cerr << "setsockopt(SO_REUSEADDR) failed on Windows: " << WSAGetLastError() << std::endl;
        // Non-fatal, try to continue
    }
#else
    if (setsockopt(sock_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "setsockopt(SO_REUSEADDR) failed: " << strerror(errno) << std::endl;
        // Non-fatal
    }
#endif

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // Listen on all available interfaces
    server_addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(sock_fd_, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        std::cerr << "Bind failed on port " << port << ": " << strerror(errno) << std::endl;
        closesocket(sock_fd_); sock_fd_ = INVALID_SOCKET_VAL;
        return false;
    }

    if (listen(sock_fd_, backlog) < 0) {
        std::cerr << "Listen failed: " << strerror(errno) << std::endl;
        closesocket(sock_fd_); sock_fd_ = INVALID_SOCKET_VAL;
        return false;
    }
    is_listening_socket_ = true;
    return true;
}

TCPSocket TCPSocket::accept_connection(bool& success_flag) {
    success_flag = false;
    if (!is_listening_socket_ || !is_valid()) {
        std::cerr << "Accept Error: Not a valid listening socket or socket is invalid." << std::endl;
        return TCPSocket(INVALID_SOCKET_VAL);
    }
    sockaddr_in client_addr{};
    socklen_t_compat client_addr_len = sizeof(client_addr); // Use compatible type
    socket_t client_sock = accept(sock_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_addr_len);

    if (client_sock == INVALID_SOCKET_VAL) {
        // This can happen if socket is closed (e.g. by stop()) or non-blocking and no connection.
        // Only log unexpected errors for blocking sockets.
        #ifdef _WIN32
            // int error = WSAGetLastError(); if (error != WSAEWOULDBLOCK && error != WSAEINTR && error != WSAECONNABORTED && error != WSAENOTSOCK) { std::cerr << "accept failed: " << error << std::endl; }
        #else
            // if (errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR && errno != ECONNABORTED && errno != ENOTSOCK) { std::cerr << "accept failed: " << strerror(errno) << std::endl; }
        #endif
        return TCPSocket(INVALID_SOCKET_VAL);
    }
    success_flag = true;
    return TCPSocket(client_sock);
}

int TCPSocket::send_data(const std::string& data) const {
    if (!is_valid() || is_listening_socket_) return -1;
    
    int total_bytes_sent = 0;
    int data_len = static_cast<int>(data.length());
    const char* data_ptr = data.c_str();

    while(total_bytes_sent < data_len) {
        int bytes_sent_this_call = send(sock_fd_, data_ptr + total_bytes_sent, data_len - total_bytes_sent, 0);
        if (bytes_sent_this_call < 0) {
            #ifdef _WIN32
                 // if (WSAGetLastError() == WSAEWOULDBLOCK) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); continue;}
            #else
                 // if (errno == EAGAIN || errno == EWOULDBLOCK) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); continue;}
            #endif
            // std::cerr << "Send failed: " << strerror(errno) << std::endl; // Can be noisy
            return -1; 
        }
        if (bytes_sent_this_call == 0) { // Should not typically happen with TCP if connection is valid and len > 0
            // std::cerr << "Send returned 0, connection may be gracefully closing or closed by peer." << std::endl;
            return total_bytes_sent; // Return what was sent, or -1 to indicate error.
        }
        total_bytes_sent += bytes_sent_this_call;
    }
    return total_bytes_sent;
}

std::string TCPSocket::receive_framed_message() const {
    if (!is_valid() || is_listening_socket_) return "";

    std::vector<char> length_buffer_vec(LENGTH_PREFIX_DIGITS);
    int total_bytes_read_for_prefix = 0;
    
    while(total_bytes_read_for_prefix < LENGTH_PREFIX_DIGITS) {
        int bytes_read = recv(sock_fd_, length_buffer_vec.data() + total_bytes_read_for_prefix, LENGTH_PREFIX_DIGITS - total_bytes_read_for_prefix, 0);
        if (bytes_read < 0) {
            #ifdef _WIN32
                // if (WSAGetLastError() == WSAEWOULDBLOCK) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); continue;}
            #else
                // if (errno == EAGAIN || errno == EWOULDBLOCK) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); continue;}
            #endif
            // std::cerr << "Recv failed while reading length prefix: " << strerror(errno) << std::endl;
            return ""; 
        }
        if (bytes_read == 0) { /* std::cerr << "Connection closed by peer while reading length prefix." << std::endl; */ return ""; }
        total_bytes_read_for_prefix += bytes_read;
    }
    
    int payload_length;
    try {
        payload_length = std::stoi(std::string(length_buffer_vec.data(), LENGTH_PREFIX_DIGITS));
    } catch (const std::exception& e) {
        std::cerr << "Error converting length prefix '" << std::string(length_buffer_vec.data(), LENGTH_PREFIX_DIGITS) << "': " << e.what() << std::endl;
        return "";
    }

    if (payload_length < 0 || payload_length > MAX_MSG_LEN * 100) { 
        std::cerr << "Invalid or excessive payload length received: " << payload_length << std::endl;
        return "";
    }
    if (payload_length == 0) return ""; // Valid empty payload

    std::string payload_data;
    payload_data.resize(payload_length);
    int total_bytes_read_for_payload = 0;
    while (total_bytes_read_for_payload < payload_length) {
        int bytes_read = recv(sock_fd_, &payload_data[total_bytes_read_for_payload], payload_length - total_bytes_read_for_payload, 0);
        if (bytes_read < 0) {
             #ifdef _WIN32
                // if (WSAGetLastError() == WSAEWOULDBLOCK) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); continue;}
            #else
                // if (errno == EAGAIN || errno == EWOULDBLOCK) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); continue;}
            #endif
            // std::cerr << "Recv failed while reading payload: " << strerror(errno) << std::endl;
            return ""; 
        }
        if (bytes_read == 0) { /* std::cerr << "Connection closed by peer while reading payload." << std::endl; */ return ""; }
        total_bytes_read_for_payload += bytes_read;
    }
    return payload_data;
}

void TCPSocket::close_socket() {
    if (is_valid()) {
        shutdown(sock_fd_, SD_BOTH); 
        closesocket(sock_fd_);
        sock_fd_ = INVALID_SOCKET_VAL;
    }
    is_listening_socket_ = false;
}

bool TCPSocket::is_valid() const {
    return sock_fd_ != INVALID_SOCKET_VAL;
}

} // namespace Network
