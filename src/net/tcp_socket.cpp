/*!
 * \file tcp_socket.cpp
 * \author Fedor Zilnitskiy
 * \brief Реализация класса TCPSocket для кроссплатформенной работы с TCP сокетами.
 */
#include "tcp_socket.h"
#include "logger.h"     // Для логирования операций и ошибок
// Остальные необходимые заголовки уже включены через tcp_socket.h -> common_defs.h

// --- Статическая инициализация членов для WSA (только для Windows) ---
#ifdef _WIN32
    bool TCPSocket::wsa_initialized_ = false;
    int TCPSocket::wsa_ref_count_ = 0;
    std::mutex TCPSocket::wsa_mutex_;

    /*!
     * \brief Инициализирует Winsock API, если это еще не сделано.
     * Увеличивает счетчик ссылок. Потокобезопасен.
     * \return true в случае успеха.
     */
    bool TCPSocket::initialize_wsa() {
        std::lock_guard<std::mutex> lock(wsa_mutex_);
        if (wsa_ref_count_ == 0) { // Инициализация при первом вызове
            WSADATA wsaData;
            int result = WSAStartup(MAKEWORD(2, 2), &wsaData); // Запрашиваем Winsock версии 2.2
            if (result != 0) {
                // Используем std::cerr для критических ошибок до инициализации логгера или если логгер недоступен
                std::cerr << "TCPSocket FATAL: WSAStartup failed. Error Code: " << result << std::endl;
                return false;
            }
            wsa_initialized_ = true;
        }
        wsa_ref_count_++;
        return true;
    }

    /*!
     * \brief Очищает ресурсы Winsock API, если счетчик ссылок достигает нуля.
     * Потокобезопасен.
     */
    void TCPSocket::cleanup_wsa() {
        std::lock_guard<std::mutex> lock(wsa_mutex_);
        if (wsa_ref_count_ > 0) {
            wsa_ref_count_--;
            if (wsa_ref_count_ == 0 && wsa_initialized_) {
                if (WSACleanup() == SOCKET_ERROR) {
                     std::cerr << "TCPSocket WARNING: WSACleanup failed. Error: " << WSAGetLastError() << std::endl;
                }
                wsa_initialized_ = false;
            }
        }
    }
#endif

// --- Вспомогательные функции для кода ошибки ---
void TCPSocket::setLastSocketError() const noexcept {
#ifdef _WIN32
    last_error_code_ = WSAGetLastError();
#else
    last_error_code_ = errno;
#endif
}

void TCPSocket::clearLastSocketError() const noexcept {
    last_error_code_ = 0;
#ifndef _WIN32
    errno = 0; // Сбрасываем errno для POSIX систем
#endif
}

int TCPSocket::getLastSocketError() const noexcept {
    return last_error_code_;
}


// --- Конструкторы и Деструктор ---
TCPSocket::TCPSocket() {
    clearLastSocketError();
#ifdef _WIN32
    if (!initialize_wsa()) {
        // В конструкторе лучше выбрасывать исключение при критической ошибке
        throw std::runtime_error("TCPSocket Constructor: Failed to initialize Winsock API (WSA).");
    }
#endif
    // Logger::debug("TCPSocket: Default constructor called."); // Логирование создания объекта
}

TCPSocket::TCPSocket(int socket_fd_param)
#ifdef _WIN32
    : socket_fd_(static_cast<SOCKET>(socket_fd_param))
#else
    : socket_fd_(socket_fd_param)
#endif
{
    clearLastSocketError();
#ifdef _WIN32
    if (!initialize_wsa()) {
         throw std::runtime_error("TCPSocket Constructor(fd): Failed to initialize Winsock API (WSA).");
    }
#endif
    // Logger::debug("TCPSocket: Constructor with existing fd " + std::to_string(socket_fd_param) + " called.");
}

TCPSocket::~TCPSocket() {
    // Logger::debug("TCPSocket: Destructor called for socket fd (raw): " + std::to_string(getRawSocketDescriptor()));
    closeSocket(); // closeSocket уже логирует ошибки закрытия, если они есть
#ifdef _WIN32
    cleanup_wsa();
#endif
}

// --- Перемещение ---
TCPSocket::TCPSocket(TCPSocket&& other) noexcept
#ifdef _WIN32
    : socket_fd_(other.socket_fd_), last_error_code_(other.last_error_code_)
#else
    : socket_fd_(other.socket_fd_), last_error_code_(other.last_error_code_)
#endif
{
    // Logger::debug("TCPSocket: Move constructor called. Moving fd (raw): " + std::to_string(getRawSocketDescriptor()));
#ifdef _WIN32
    other.socket_fd_ = INVALID_SOCKET;
#else
    other.socket_fd_ = -1;
#endif
    other.last_error_code_ = 0;
}

TCPSocket& TCPSocket::operator=(TCPSocket&& other) noexcept {
    if (this != &other) {
        // Logger::debug("TCPSocket: Move assignment operator. Closing current fd (raw): " + std::to_string(getRawSocketDescriptor()) +
        //               " and moving fd (raw): " + std::to_string(other.getRawSocketDescriptor()));
        closeSocket(); // Закрываем текущий сокет перед присвоением
#ifdef _WIN32
        socket_fd_ = other.socket_fd_;
        other.socket_fd_ = INVALID_SOCKET;
#else
        socket_fd_ = other.socket_fd_;
        other.socket_fd_ = -1;
#endif
        last_error_code_ = other.last_error_code_;
        other.last_error_code_ = 0;
    }
    return *this;
}


// --- Основные операции с сокетом ---
bool TCPSocket::isValid() const {
#ifdef _WIN32
    return socket_fd_ != INVALID_SOCKET;
#else
    return socket_fd_ >= 0;
#endif
}

void TCPSocket::closeSocket() {
    if (isValid()) {
        // Logger::debug("TCPSocket: Attempting to close socket fd (raw): " + std::to_string(getRawSocketDescriptor()));
        clearLastSocketError();
#ifdef _WIN32
        if (::shutdown(socket_fd_, SD_BOTH) == SOCKET_ERROR) {
            // Ошибку shutdown можно проигнорировать или залогировать как WARN,
            // т.к. closesocket все равно будет вызван.
            // setLastSocketError();
            // Logger::warn("TCPSocket::closeSocket: shutdown() call failed. WSAError: " + std::to_string(WSAGetLastError()) +
            //             " for fd (raw): " + std::to_string(getRawSocketDescriptor()));
        }
        if (::closesocket(socket_fd_) == SOCKET_ERROR) {
            setLastSocketError();
            Logger::error("TCPSocket::closeSocket: closesocket() call failed. WSAError: " + std::to_string(getLastSocketError()) +
                         " for fd (raw): " + std::to_string(getRawSocketDescriptor()));
        } else {
            // Logger::debug("TCPSocket::closeSocket: Successfully closed socket fd (raw): " + std::to_string(getRawSocketDescriptor()));
            clearLastSocketError(); // Успешное закрытие
        }
        socket_fd_ = INVALID_SOCKET;
#else // POSIX
        if (::shutdown(socket_fd_, SHUT_RDWR) < 0) {
            // setLastSocketError();
            // Logger::warn("TCPSocket::closeSocket: shutdown() call failed. Errno(" + std::to_string(errno) + "): " + std::strerror(errno) +
            //             " for fd: " + std::to_string(socket_fd_));
        }
        if (::close(socket_fd_) < 0) {
            setLastSocketError();
            Logger::error("TCPSocket::closeSocket: close() call failed. Errno(" + std::to_string(getLastSocketError()) + "): " + std::strerror(getLastSocketError()) +
                         " for fd: " + std::to_string(socket_fd_));
        } else {
            // Logger::debug("TCPSocket::closeSocket: Successfully closed socket fd: " + std::to_string(socket_fd_));
            clearLastSocketError();
        }
        socket_fd_ = -1;
#endif
    } else {
        clearLastSocketError(); // Если сокет уже невалиден, ошибок нет
    }
}

int TCPSocket::getRawSocketDescriptor() const {
#ifdef _WIN32
    return static_cast<int>(reinterpret_cast<intptr_t>(socket_fd_));
#else
    return socket_fd_;
#endif
}

bool TCPSocket::connectSocket(const std::string& host, int port) {
    clearLastSocketError();
    if (isValid()) { 
        Logger::warn("TCPSocket::connectSocket: Socket was already valid. Closing before reconnecting. FD(raw): " + std::to_string(getRawSocketDescriptor()));
        closeSocket();
        clearLastSocketError(); 
    }

#ifdef _WIN32
    socket_fd_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_fd_ == INVALID_SOCKET) {
        setLastSocketError();
        Logger::error("TCPSocket::connectSocket: socket() creation failed. WSAError: " + std::to_string(getLastSocketError()));
        return false;
    }
#else
    socket_fd_ = ::socket(AF_INET, SOCK_STREAM, 0); 
    if (socket_fd_ < 0) {
        setLastSocketError();
        Logger::error("TCPSocket::connectSocket: socket() creation failed. Errno(" + std::to_string(getLastSocketError()) + "): " + std::strerror(getLastSocketError()));
        return false;
    }
#endif
    // Logger::debug("TCPSocket::connectSocket: Socket created successfully. FD(raw): " + std::to_string(getRawSocketDescriptor()));

    addrinfo hints{}; 
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;       
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo *result_addrinfo = nullptr;
    std::string port_str = std::to_string(port);

#ifdef _WIN32 
    WSASetLastError(0);
#else
    errno = 0;
#endif
    int gai_res = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result_addrinfo);
    if (gai_res != 0) {
#ifdef _WIN32
        Logger::error("TCPSocket::connectSocket: getaddrinfo failed for host '" + host + "'. GAI_Error: " + std::to_string(gai_res) + " (" + gai_strerrorA(gai_res) + ")");
#else
        Logger::error("TCPSocket::connectSocket: getaddrinfo failed for host '" + host + "'. Error: " + std::string(gai_strerror(gai_res)));
#endif
        closeSocket(); 
        if (result_addrinfo) freeaddrinfo(result_addrinfo);
        return false;
    }

    bool connected = false;
    addrinfo *ptr = nullptr; 
    for (ptr = result_addrinfo; ptr != nullptr; ptr = ptr->ai_next) {
        clearLastSocketError(); 
#ifdef _WIN32
        if (::connect(socket_fd_, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen)) != SOCKET_ERROR) {
            connected = true;
            break; 
        } else {
            setLastSocketError(); 
        }
#else
        if (::connect(socket_fd_, ptr->ai_addr, ptr->ai_addrlen) != -1) {
            connected = true;
            break; 
        } else {
            setLastSocketError(); 
        }
#endif
    }
    freeaddrinfo(result_addrinfo); 

    if (!connected) {
        Logger::error("TCPSocket::connectSocket: All attempts to connect to " + host + ":" + port_str + " failed. Last connect error code: " + std::to_string(getLastSocketError()));
        closeSocket(); 
        return false;
    }
    clearLastSocketError(); 
    // Logger::info("TCPSocket::connectSocket: Successfully connected to " + host + ":" + port_str + ". FD(raw): " + std::to_string(getRawSocketDescriptor()));
    return true;
}

bool TCPSocket::bindSocket(int port) {
    clearLastSocketError();
    if (isValid()) {
        Logger::warn("TCPSocket::bindSocket: Socket was already valid. Closing before rebinding. FD(raw): " + std::to_string(getRawSocketDescriptor()));
        closeSocket();
        clearLastSocketError();
    }

#ifdef _WIN32
    socket_fd_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_fd_ == INVALID_SOCKET) { setLastSocketError(); Logger::error("TCPSocket::bindSocket: socket() creation failed. WSAError: " + std::to_string(getLastSocketError())); return false; }
#else
    socket_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) { setLastSocketError(); Logger::error("TCPSocket::bindSocket: socket() creation failed. Errno(" + std::to_string(getLastSocketError()) + "): " + std::strerror(getLastSocketError())); return false; }
#endif
    // Logger::debug("TCPSocket::bindSocket: Socket created for binding. FD(raw): " + std::to_string(getRawSocketDescriptor()));

#ifdef _WIN32
    char optval_win = 1; 
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &optval_win, sizeof(optval_win)) == SOCKET_ERROR) {
         setLastSocketError();
         Logger::warn("TCPSocket::bindSocket: setsockopt(SO_REUSEADDR) failed. WSAError: " + std::to_string(getLastSocketError()) + ". Continuing anyway.");
    }
#else
    int optval_posix = 1;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &optval_posix, sizeof(optval_posix)) < 0) {
        setLastSocketError();
        Logger::warn("TCPSocket::bindSocket: setsockopt(SO_REUSEADDR) failed. Errno(" + std::to_string(getLastSocketError()) + "): " + std::strerror(getLastSocketError()) + ". Continuing anyway.");
    }
#endif

    sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; 
    server_addr.sin_port = htons(static_cast<unsigned short>(port)); 

    clearLastSocketError();
    if (::bind(socket_fd_, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) { // C-style cast for POSIX, reinterpret_cast for type safety
        setLastSocketError();
#ifdef _WIN32
        Logger::error("TCPSocket::bindSocket: bind() to port " + std::to_string(port) + " failed. WSAError: " + std::to_string(getLastSocketError()));
#else
        Logger::error("TCPSocket::bindSocket: bind() to port " + std::to_string(port) + " failed. Errno(" + std::to_string(getLastSocketError()) + "): " + std::strerror(getLastSocketError()));
#endif
        closeSocket(); 
        return false;
    }
    // Logger::info("TCPSocket::bindSocket: Successfully bound to port " + std::to_string(port) + ". FD(raw): " + std::to_string(getRawSocketDescriptor()));
    return true;
}

bool TCPSocket::listenSocket(int backlog) {
    clearLastSocketError();
    if (!isValid()) {
        Logger::error("TCPSocket::listenSocket: Called on an invalid (not bound or closed) socket.");
        return false;
    }
    if (::listen(socket_fd_, backlog) < 0) {
        setLastSocketError();
#ifdef _WIN32
        Logger::error("TCPSocket::listenSocket: listen() call failed. WSAError: " + std::to_string(getLastSocketError()));
#else
        Logger::error("TCPSocket::listenSocket: listen() call failed. Errno(" + std::to_string(getLastSocketError()) + "): " + std::strerror(getLastSocketError()));
#endif
        return false; 
    }
    // Logger::info("TCPSocket::listenSocket: Now listening on port with backlog " + std::to_string(backlog) + ". FD(raw): " + std::to_string(getRawSocketDescriptor()));
    return true;
}

TCPSocket TCPSocket::acceptSocket(std::string* client_ip, int* client_port) {
    clearLastSocketError();
    if (!isValid()) {
        return TCPSocket(); 
    }

    sockaddr_storage client_addr_storage; 
    socklen_t client_addr_len = sizeof(client_addr_storage);
    std::memset(&client_addr_storage, 0, sizeof(client_addr_storage));

    int accepted_socket_fd_raw_int; 

#ifdef _WIN32
    SOCKET accepted_socket_raw_win;
    accepted_socket_raw_win = ::accept(socket_fd_, reinterpret_cast<sockaddr*>(&client_addr_storage), &client_addr_len);
    if (accepted_socket_raw_win == INVALID_SOCKET) {
        setLastSocketError();
        int error_code = getLastSocketError();
        if (error_code != WSAEINTR && error_code != WSAECONNABORTED &&
            error_code != WSAEWOULDBLOCK && error_code != WSAESHUTDOWN && 
            error_code != WSAENOTSOCK && 
            error_code != WSAEINPROGRESS 
            ) {
            Logger::warn("TCPSocket::acceptSocket: accept() failed. WSAError: " + std::to_string(error_code));
        }
        return TCPSocket(); 
    }
    accepted_socket_fd_raw_int = static_cast<int>(reinterpret_cast<intptr_t>(accepted_socket_raw_win));
#else // POSIX
    int accepted_fd_posix;
    accepted_fd_posix = ::accept(socket_fd_, reinterpret_cast<sockaddr*>(&client_addr_storage), &client_addr_len);
    if (accepted_fd_posix < 0) {
        setLastSocketError();
        int error_code = getLastSocketError();
        if (error_code != EINTR && error_code != ECONNABORTED &&
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
            error_code != EWOULDBLOCK &&
#endif
            error_code != EAGAIN && 
            error_code != ENOTSOCK && 
            error_code != EBADF 
            ) { // Логируем только "неожиданные" ошибки
            Logger::warn("TCPSocket::acceptSocket: accept() failed. Errno(" + std::to_string(error_code) + "): " + std::strerror(error_code));
        }
        return TCPSocket(); 
    }
    accepted_socket_fd_raw_int = accepted_fd_posix;
#endif

    if (client_ip || client_port) {
        char host_str[NI_MAXHOST] = {0}; 
        char port_str[NI_MAXSERV] = {0}; 

#ifdef _WIN32 
        WSASetLastError(0);
#else
        errno = 0;
#endif
        if (getnameinfo(reinterpret_cast<sockaddr*>(&client_addr_storage), client_addr_len,
                        host_str, NI_MAXHOST, port_str, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
            if (client_ip) *client_ip = host_str;
            if (client_port) {
                try {
                    *client_port = std::stoi(port_str);
                } catch (const std::exception& e_stoi) {
                    Logger::error("TCPSocket::acceptSocket: Error converting client port string '" + std::string(port_str) + "' to int: " + e_stoi.what());
                    if (client_port) *client_port = 0; 
                }
            }
        } else {
#ifdef _WIN32
            Logger::warn("TCPSocket::acceptSocket: getnameinfo failed for accepted client. Potential WSAError: " + std::to_string(WSAGetLastError()));
#else
            Logger::warn("TCPSocket::acceptSocket: getnameinfo failed for accepted client. Errno(" + std::to_string(errno) + "): " + std::strerror(errno));
#endif
            if(client_ip) client_ip->clear(); 
            if(client_port) *client_port = 0;
        }
    }
    TCPSocket clientSock(accepted_socket_fd_raw_int);
    clientSock.clearLastSocketError(); 
    // Logger::info("TCPSocket::acceptSocket: Accepted new connection. Client FD(raw): " + std::to_string(clientSock.getRawSocketDescriptor()));
    return clientSock; 
}

int TCPSocket::sendAllData(const char* buffer, size_t length) const {
    clearLastSocketError();
    if (!isValid()) { Logger::error("TCPSocket (fd_raw " + std::to_string(getRawSocketDescriptor()) + ")::sendAllData: Invalid socket."); return -1; }
    if (buffer == nullptr && length > 0) { Logger::error("TCPSocket (fd_raw " + std::to_string(getRawSocketDescriptor()) + ")::sendAllData: Buffer is null with non-zero length ("+ std::to_string(length) +")."); return -1; }
    if (length == 0) {
        return 0; 
    }

    size_t total_sent = 0;
    while (total_sent < length) {
        ssize_t bytes_sent_this_call; 
        clearLastSocketError(); 
#ifdef _WIN32
        bytes_sent_this_call = ::send(socket_fd_, buffer + total_sent, static_cast<int>(length - total_sent), 0);
        if (bytes_sent_this_call == SOCKET_ERROR) {
            setLastSocketError();
            int error_code = getLastSocketError();
            if (error_code == WSAEWOULDBLOCK) { 
                return static_cast<int>(total_sent); 
            }
            Logger::error("TCPSocket (fd_raw " + std::to_string(getRawSocketDescriptor()) + ")::sendAllData: send failed. WSAError: " + std::to_string(error_code));
            return -1; 
        }
#else // POSIX
        bytes_sent_this_call = ::send(socket_fd_, buffer + total_sent, length - total_sent, MSG_NOSIGNAL); 
        if (bytes_sent_this_call < 0) {
            setLastSocketError();
            int error_code = getLastSocketError();
            if (error_code == EINTR) { 
                continue;
            }
            bool would_block = (error_code == EAGAIN);
            #if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN // Check if EWOULDBLOCK is defined and different from EAGAIN
            if (error_code == EWOULDBLOCK) {
                would_block = true;
            }
            #endif
            if (would_block) {
                return static_cast<int>(total_sent); 
            }
            Logger::error("TCPSocket (fd " + std::to_string(socket_fd_) + ")::sendAllData: send failed. Errno(" + std::to_string(error_code) + "): " + std::strerror(error_code));
            return -1; 
        }
#endif
        if (bytes_sent_this_call == 0) { 
            return static_cast<int>(total_sent); 
        }
        total_sent += static_cast<size_t>(bytes_sent_this_call);
    }
    return static_cast<int>(total_sent); 
}

bool TCPSocket::sendAllDataWithLengthPrefix(const std::string& data) const {
    clearLastSocketError();
    if (!isValid()) { Logger::error("TCPSocket (fd_raw " + std::to_string(getRawSocketDescriptor()) + ")::sendAllDataWithLengthPrefix: Invalid socket."); return false; }

    uint32_t data_len_host = static_cast<uint32_t>(data.length());
    if (data_len_host > MAX_MESSAGE_PAYLOAD_SIZE) { 
        Logger::error("TCPSocket (fd_raw " + std::to_string(getRawSocketDescriptor()) + ")::sendAllDataWithLengthPrefix: Data size (" + std::to_string(data_len_host) + " bytes) exceeds MAX_MESSAGE_PAYLOAD_SIZE (" + std::to_string(MAX_MESSAGE_PAYLOAD_SIZE) + "). Message not sent.");
        return false;
    }
    uint32_t data_len_net = htonl(data_len_host); 

    Logger::debug("TCPSocket (fd_raw " + std::to_string(getRawSocketDescriptor()) +
                  ") SendLenPrefix: Sending length prefix value: " + std::to_string(data_len_host) +
                  " (" + std::to_string(sizeof(data_len_net)) + " bytes as network order).");
    if (sendAllData(reinterpret_cast<const char*>(&data_len_net), sizeof(data_len_net)) != static_cast<int>(sizeof(data_len_net))) {
        Logger::error("TCPSocket (fd_raw " + std::to_string(getRawSocketDescriptor()) + ")::sendAllDataWithLengthPrefix: Failed to send length prefix (" + std::to_string(sizeof(data_len_net)) + " bytes). Socket error: " + std::to_string(getLastSocketError()));
        return false;
    }

    if (data_len_host > 0) {
        Logger::debug("TCPSocket (fd_raw " + std::to_string(getRawSocketDescriptor()) +
                      ") SendLenPrefix: Sending payload data: " + std::to_string(data.length()) + " bytes.");
        if (sendAllData(data.c_str(), data.length()) != static_cast<int>(data.length())) {
            Logger::error("TCPSocket (fd_raw " + std::to_string(getRawSocketDescriptor()) + ")::sendAllDataWithLengthPrefix: Failed to send data payload of size " + std::to_string(data_len_host) + " bytes. Socket error: " + std::to_string(getLastSocketError()));
            return false;
        }
        Logger::debug("TCPSocket (fd_raw " + std::to_string(getRawSocketDescriptor()) +
                      ") SendLenPrefix: Payload data sent successfully.");
    } else {
         Logger::debug("TCPSocket (fd_raw " + std::to_string(getRawSocketDescriptor()) +
                      ") SendLenPrefix: Payload data is empty (length 0), not sending payload.");
    }
    return true;
}

int TCPSocket::receiveAllData(char* buffer, size_t length_to_receive) const {
    clearLastSocketError();
    if (!isValid()) { Logger::error("TCPSocket (fd_raw " + std::to_string(getRawSocketDescriptor()) + ")::receiveAllData: Invalid socket."); return -1; }
    if (buffer == nullptr && length_to_receive > 0) { Logger::error("TCPSocket (fd_raw " + std::to_string(getRawSocketDescriptor()) + ")::receiveAllData: Buffer is null with non-zero length (" + std::to_string(length_to_receive) + ")."); return -1; }
    if (length_to_receive == 0) {
        return 0; 
    }

    size_t total_received = 0;
    while (total_received < length_to_receive) {
        ssize_t bytes_received_this_call;
        clearLastSocketError(); 
#ifdef _WIN32
        bytes_received_this_call = ::recv(socket_fd_, buffer + total_received, static_cast<int>(length_to_receive - total_received), 0);
        if (bytes_received_this_call == SOCKET_ERROR) {
            setLastSocketError();
            int error_code = getLastSocketError();
            if (error_code == WSAEWOULDBLOCK || error_code == WSAETIMEDOUT) { 
                return static_cast<int>(total_received); 
            }
            Logger::error("TCPSocket (fd_raw " + std::to_string(getRawSocketDescriptor()) + ")::receiveAllData: recv failed. WSAError: " + std::to_string(error_code));
            return -1; 
        }
#else // POSIX
        bytes_received_this_call = ::recv(socket_fd_, buffer + total_received, length_to_receive - total_received, 0);
        if (bytes_received_this_call < 0) {
            setLastSocketError();
            int error_code = getLastSocketError();
            if (error_code == EINTR) { 
                continue;
            }
            // Исправляем предупреждение [-Wlogical-op]
            bool would_block = (error_code == EAGAIN);
            #if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN // Check if EWOULDBLOCK is defined and different from EAGAIN
            if (error_code == EWOULDBLOCK) {
                would_block = true;
            }
            #endif
            if (would_block) {
                return static_cast<int>(total_received); 
            }
            Logger::error("TCPSocket (fd " + std::to_string(socket_fd_) + ")::receiveAllData: recv failed. Errno(" + std::to_string(error_code) + "): " + std::strerror(error_code));
            return -1; 
        }
#endif
        if (bytes_received_this_call == 0) { 
            clearLastSocketError(); 
            return static_cast<int>(total_received); 
        }
        total_received += static_cast<size_t>(bytes_received_this_call);
    }
    return static_cast<int>(total_received); 
}

std::string TCPSocket::receiveAllDataWithLengthPrefix(bool& success, int timeout_ms) {
    success = false; 
    clearLastSocketError();
    if (!isValid()) {
        Logger::error("TCPSocket (fd_raw " + std::to_string(getRawSocketDescriptor()) + ")::receiveAllDataWithLengthPrefix: Invalid socket.");
        return "";
    }

    bool temporary_timeout_was_set = false;
#ifdef _WIN32
    DWORD original_timeout_val_win = 0;
    int optlen_win = sizeof(original_timeout_val_win);
    bool original_timeout_fetched_win = false;
#else // POSIX
    timeval original_timeout_val_posix = {0,0}; 
    socklen_t optlen_posix = sizeof(original_timeout_val_posix);
    bool original_timeout_fetched_posix = false;
#endif

    if (timeout_ms >= 0) {
#ifdef _WIN32
        clearLastSocketError();
        if (getsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&original_timeout_val_win), &optlen_win) == 0) {
            original_timeout_fetched_win = true;
        } else {
            setLastSocketError(); 
        }
        DWORD new_timeout_win = static_cast<DWORD>(timeout_ms == 0 ? 1 : timeout_ms); 
        clearLastSocketError();
        if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&new_timeout_win), sizeof(new_timeout_win)) == 0) {
            temporary_timeout_was_set = true;
        } else {
            setLastSocketError();
            Logger::error("TCPSocket (fd_raw " + std::to_string(getRawSocketDescriptor()) + ")::receiveAllDataWithLengthPrefix: setsockopt SO_RCVTIMEO to " + std::to_string(new_timeout_win) + "ms failed. WSAError: " + std::to_string(getLastSocketError()));
            return ""; 
        }
#else // POSIX
        clearLastSocketError();
        if (getsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &original_timeout_val_posix, &optlen_posix) == 0) {
            original_timeout_fetched_posix = true;
        } else {
            setLastSocketError();
        }
        timeval new_timeout_posix;
        if (timeout_ms == 0) { 
            new_timeout_posix.tv_sec = 0;
            new_timeout_posix.tv_usec = 1000; 
        } else {
            new_timeout_posix.tv_sec = timeout_ms / 1000;
            new_timeout_posix.tv_usec = (timeout_ms % 1000) * 1000;
        }
        clearLastSocketError();
        if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &new_timeout_posix, sizeof(new_timeout_posix)) == 0) {
            temporary_timeout_was_set = true;
        } else {
            setLastSocketError();
            Logger::error("TCPSocket (fd_raw " + std::to_string(getRawSocketDescriptor()) + ")::receiveAllDataWithLengthPrefix: setsockopt SO_RCVTIMEO to " + std::to_string(timeout_ms) + "ms failed. Errno(" + std::to_string(getLastSocketError()) + "): " + std::strerror(getLastSocketError()));
            return ""; 
        }
#endif
    }

    auto restore_timeout_finalizer = std::shared_ptr<void>(nullptr,
        [&]([[maybe_unused]] void* p_unused){ 
            if (temporary_timeout_was_set && isValid()) { 
            #ifdef _WIN32
                if (original_timeout_fetched_win) {
                    clearLastSocketError();
                    if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&original_timeout_val_win), sizeof(original_timeout_val_win)) != 0) {
                        setLastSocketError(); 
                    }
                }
            #else // POSIX
                if (original_timeout_fetched_posix) {
                    clearLastSocketError();
                    if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &original_timeout_val_posix, sizeof(original_timeout_val_posix)) != 0) {
                        setLastSocketError();
                    }
                }
            #endif
            }
        }
    ); 

    char len_buffer[sizeof(uint32_t)];
    int bytes_len_received = receiveAllData(len_buffer, sizeof(uint32_t));

    if (bytes_len_received != static_cast<int>(sizeof(uint32_t))) {
        return ""; 
    }

    uint32_t data_len_net;
    std::memcpy(&data_len_net, len_buffer, sizeof(uint32_t));
    uint32_t data_len_host = ntohl(data_len_net); 

    Logger::debug("TCPSocket (fd_raw " + std::to_string(getRawSocketDescriptor()) +
                  ") RecvLenPrefix: Received length prefix value: " + std::to_string(data_len_host) +
                  " (" + std::to_string(bytes_len_received) + " bytes as network order).");

    if (data_len_host > MAX_MESSAGE_PAYLOAD_SIZE) { 
        Logger::error("TCPSocket (fd_raw " + std::to_string(getRawSocketDescriptor()) + ")::receiveAllDataWithLengthPrefix: Declared message size (" + std::to_string(data_len_host) + " bytes) exceeds MAX_MESSAGE_PAYLOAD_SIZE (" + std::to_string(MAX_MESSAGE_PAYLOAD_SIZE) + "). Potential data corruption or attack.");
        // Не закрываем сокет здесь, но устанавливаем ошибку. Вызывающая сторона решит.
        last_error_code_ = -1; // Условный код ошибки протокола
        return ""; 
    }

    if (data_len_host == 0) { 
        Logger::debug("TCPSocket (fd_raw " + std::to_string(getRawSocketDescriptor()) +
                      ") RecvLenPrefix: Payload data is empty (length 0) as per prefix.");
        success = true;
        return ""; 
    }

    std::vector<char> data_buffer_vec(data_len_host);
    Logger::debug("TCPSocket (fd_raw " + std::to_string(getRawSocketDescriptor()) +
                  ") RecvLenPrefix: Attempting to receive payload data: " + std::to_string(data_len_host) + " bytes.");

    int bytes_payload_received = receiveAllData(data_buffer_vec.data(), data_len_host);

    if (bytes_payload_received != static_cast<int>(data_len_host)) {
        Logger::warn("TCPSocket (fd_raw " + std::to_string(getRawSocketDescriptor()) +
                      ") RecvLenPrefix: Received less payload data than expected. Expected: " + std::to_string(data_len_host) +
                      ", received: " + std::to_string(bytes_payload_received) + ". Socket error: " + std::to_string(getLastSocketError()));
        return ""; 
    }
    Logger::debug("TCPSocket (fd_raw " + std::to_string(getRawSocketDescriptor()) +
                  ") RecvLenPrefix: Successfully received payload data: " + std::to_string(bytes_payload_received) + " bytes.");

    success = true; 
    return std::string(data_buffer_vec.data(), data_len_host);
}


// --- Установка опций сокета ---
bool TCPSocket::setNonBlocking(bool non_blocking) {
    clearLastSocketError();
    if (!isValid()) { Logger::error("TCPSocket::setNonBlocking: Called on an invalid socket."); return false; }
#ifdef _WIN32
    u_long mode = non_blocking ? 1 : 0;
    if (ioctlsocket(socket_fd_, FIONBIO, &mode) != 0) {
        setLastSocketError();
        Logger::error("TCPSocket::setNonBlocking: ioctlsocket(FIONBIO) failed. WSAError: " + std::to_string(getLastSocketError()));
        return false;
    }
#else // POSIX
    int flags = fcntl(socket_fd_, F_GETFL, 0);
    if (flags == -1) {
        setLastSocketError();
        Logger::error("TCPSocket::setNonBlocking: fcntl(F_GETFL) failed. Errno(" + std::to_string(getLastSocketError()) + "): " + std::strerror(getLastSocketError()));
        return false;
    }
    flags = non_blocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (fcntl(socket_fd_, F_SETFL, flags) == -1) {
        setLastSocketError();
        Logger::error("TCPSocket::setNonBlocking: fcntl(F_SETFL) failed. Errno(" + std::to_string(getLastSocketError()) + "): " + std::strerror(getLastSocketError()));
        return false;
    }
#endif
    // Logger::debug("TCPSocket: Non-blocking mode " + std::string(non_blocking ? "enabled" : "disabled") + " for fd(raw): " + std::to_string(getRawSocketDescriptor()));
    return true;
}

bool TCPSocket::setRecvTimeout(int timeout_ms) {
    clearLastSocketError();
    if (!isValid()) { Logger::error("TCPSocket::setRecvTimeout: Called on an invalid socket."); return false; }
    if (timeout_ms < 0) { Logger::warn("TCPSocket::setRecvTimeout: Negative timeout value (" + std::to_string(timeout_ms) + "ms) is invalid. Not setting timeout."); return false;}

#ifdef _WIN32
    DWORD timeout_val_dword = static_cast<DWORD>(timeout_ms); 
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_val_dword), sizeof(timeout_val_dword)) == SOCKET_ERROR) {
        setLastSocketError();
        Logger::error("TCPSocket::setRecvTimeout (Win): setsockopt failed. WSAError: " + std::to_string(getLastSocketError()));
        return false;
    }
#else // POSIX
    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000; 
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        setLastSocketError();
        Logger::error("TCPSocket::setRecvTimeout (POSIX): setsockopt failed. Errno(" + std::to_string(getLastSocketError()) + "): " + std::strerror(getLastSocketError()));
        return false;
    }
#endif
    // Logger::debug("TCPSocket: Receive timeout set to " + std::to_string(timeout_ms) + "ms for fd(raw): " + std::to_string(getRawSocketDescriptor()));
    return true;
}

bool TCPSocket::setSendTimeout(int timeout_ms) {
    clearLastSocketError();
    if (!isValid()) { Logger::error("TCPSocket::setSendTimeout: Called on an invalid socket."); return false; }
    if (timeout_ms < 0) { Logger::warn("TCPSocket::setSendTimeout: Negative timeout value (" + std::to_string(timeout_ms) + "ms) is invalid. Not setting timeout."); return false;}

#ifdef _WIN32
    DWORD timeout_val_dword = static_cast<DWORD>(timeout_ms);
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout_val_dword), sizeof(timeout_val_dword)) == SOCKET_ERROR) {
        setLastSocketError();
        Logger::error("TCPSocket::setSendTimeout (Win): setsockopt failed. WSAError: " + std::to_string(getLastSocketError()));
        return false;
    }
#else // POSIX
    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        setLastSocketError();
        Logger::error("TCPSocket::setSendTimeout (POSIX): setsockopt failed. Errno(" + std::to_string(getLastSocketError()) + "): " + std::strerror(getLastSocketError()));
        return false;
    }
#endif
    // Logger::debug("TCPSocket: Send timeout set to " + std::to_string(timeout_ms) + "ms for fd(raw): " + std::to_string(getRawSocketDescriptor()));
    return true;
}
