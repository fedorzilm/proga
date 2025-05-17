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
                std::cerr << "TCPSocket FATAL: WSAStartup failed. Error Code: " << result << std::endl;
                return false;
            }
            wsa_initialized_ = true;
            Logger::debug("TCPSocket: Winsock API (WSA) initialized successfully.");
        }
        wsa_ref_count_++; // Увеличиваем счетчик активных "пользователей" WSA
        return true;
    }

    /*!
     * \brief Очищает ресурсы Winsock API, если счетчик ссылок достигает нуля.
     * Потокобезопасен.
     */
    void TCPSocket::cleanup_wsa() {
        std::lock_guard<std::mutex> lock(wsa_mutex_);
        if (wsa_ref_count_ > 0) { // Предотвращение отрицательного счетчика
            wsa_ref_count_--;
            if (wsa_ref_count_ == 0 && wsa_initialized_) { // Очистка при последнем "пользователе"
                if (WSACleanup() == SOCKET_ERROR) {
                     std::cerr << "TCPSocket WARNING: WSACleanup failed. Error: " << WSAGetLastError() << std::endl;
                } else {
                    Logger::debug("TCPSocket: Winsock API (WSA) cleaned up successfully.");
                }
                wsa_initialized_ = false;
            }
        }
    }
#endif

// --- Конструкторы и Деструктор ---

/*!
 * \brief Конструктор по умолчанию.
 */
TCPSocket::TCPSocket() {
#ifdef _WIN32
    if (!initialize_wsa()) {
        throw std::runtime_error("TCPSocket Constructor: Failed to initialize Winsock API (WSA).");
    }
#endif
}

/*!
 * \brief Конструктор с существующим дескриптором.
 * \param socket_fd_param Дескриптор сокета.
 */
TCPSocket::TCPSocket(int socket_fd_param)
#ifdef _WIN32
    : socket_fd_(static_cast<SOCKET>(socket_fd_param))
#else
    : socket_fd_(socket_fd_param)
#endif
{
#ifdef _WIN32
    if (!initialize_wsa()) {
         throw std::runtime_error("TCPSocket Constructor(fd): Failed to initialize Winsock API (WSA).");
    }
#endif
    Logger::debug("TCPSocket: Constructed with existing fd " + std::to_string(getRawSocketDescriptor()) + ".");
}

/*!
 * \brief Деструктор.
 */
TCPSocket::~TCPSocket() {
    closeSocket();
#ifdef _WIN32
    cleanup_wsa();
#endif
}

// --- Перемещение ---

/*!
 * \brief Конструктор перемещения.
 * \param other R-value ссылка на другой TCPSocket.
 */
TCPSocket::TCPSocket(TCPSocket&& other) noexcept
#ifdef _WIN32
    : socket_fd_(other.socket_fd_)
#else
    : socket_fd_(other.socket_fd_)
#endif
{
#ifdef _WIN32
    if (socket_fd_ != INVALID_SOCKET) {
        if (!initialize_wsa()) {
            Logger::error("TCPSocket MoveCtor: initialize_wsa failed for a moved valid socket. WSA ref count might be incorrect.");
        }
    }
    other.socket_fd_ = INVALID_SOCKET;
#else
    other.socket_fd_ = -1;
#endif
}

/*!
 * \brief Оператор присваивания перемещением.
 * \param other R-value ссылка на другой TCPSocket.
 * \return Ссылка на текущий объект.
 */
TCPSocket& TCPSocket::operator=(TCPSocket&& other) noexcept {
    if (this != &other) {
        closeSocket();

#ifdef _WIN32
        socket_fd_ = other.socket_fd_;
        if (socket_fd_ != INVALID_SOCKET) {
            if(!initialize_wsa()){
                 Logger::error("TCPSocket MoveAssign: initialize_wsa failed for a moved valid socket. WSA ref count might be incorrect.");
            }
        }
        other.socket_fd_ = INVALID_SOCKET;
#else
        socket_fd_ = other.socket_fd_;
        other.socket_fd_ = -1;
#endif
    }
    return *this;
}


// --- Основные операции с сокетом ---

/*! \brief Проверяет валидность сокета. */
bool TCPSocket::isValid() const {
#ifdef _WIN32
    return socket_fd_ != INVALID_SOCKET;
#else
    return socket_fd_ >= 0;
#endif
}

/*! \brief Закрывает сокет. */
void TCPSocket::closeSocket() {
    if (isValid()) {
        Logger::info("TCPSocket: Closing socket fd " + std::to_string(getRawSocketDescriptor()));
#ifdef _WIN32
        if (::shutdown(socket_fd_, SD_BOTH) == SOCKET_ERROR) {
        }
        if (::closesocket(socket_fd_) == SOCKET_ERROR) {
            Logger::error("TCPSocket::closeSocket: closesocket call failed. Error: " + std::to_string(WSAGetLastError()));
        }
        socket_fd_ = INVALID_SOCKET;
#else // POSIX
        if (::shutdown(socket_fd_, SHUT_RDWR) < 0) {
        }
        if (::close(socket_fd_) < 0) {
            Logger::error("TCPSocket::closeSocket: close call failed. Error ("+ std::to_string(errno) +"): " + std::strerror(errno));
        }
        socket_fd_ = -1;
#endif
    }
}

/*! \brief Возвращает сырой дескриптор сокета. */
int TCPSocket::getRawSocketDescriptor() const {
#ifdef _WIN32
    return static_cast<int>(socket_fd_);
#else
    return socket_fd_;
#endif
}

/*!
 * \brief Клиентское подключение к серверу.
 * \param host Имя хоста или IP.
 * \param port Порт.
 * \return true при успехе.
 */
bool TCPSocket::connectSocket(const std::string& host, int port) {
    if (isValid()) {
        Logger::warn("TCPSocket::connectSocket: Socket fd " + std::to_string(getRawSocketDescriptor()) + " is already valid. Closing it first.");
        closeSocket();
    }

#ifdef _WIN32
    socket_fd_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_fd_ == INVALID_SOCKET) {
        Logger::error("TCPSocket::connectSocket: socket() creation failed. WSAError: " + std::to_string(WSAGetLastError()));
        return false;
    }
#else
    socket_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        Logger::error("TCPSocket::connectSocket: socket() creation failed. Errno(" + std::to_string(errno) +"): " + std::strerror(errno));
        return false;
    }
#endif
    Logger::debug("TCPSocket::connectSocket: System socket created, fd=" + std::to_string(getRawSocketDescriptor()));

    addrinfo hints{};
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo *result_addrinfo = nullptr;
    std::string port_str = std::to_string(port);

    int gai_res = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result_addrinfo);
    if (gai_res != 0) {
#ifdef _WIN32
        Logger::error("TCPSocket::connectSocket: getaddrinfo failed for host '" + host + "'. Error code: " + std::to_string(gai_res) + " (WSAGetLastError: " + std::to_string(WSAGetLastError()) + ")");
#else
        Logger::error("TCPSocket::connectSocket: getaddrinfo failed for host '" + host + "'. Error: " + std::string(gai_strerror(gai_res)));
#endif
        closeSocket();
        return false;
    }

    bool connected = false;
    for (addrinfo *ptr = result_addrinfo; ptr != nullptr; ptr = ptr->ai_next) {
#ifdef _WIN32
        if (::connect(socket_fd_, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen)) != SOCKET_ERROR) {
            connected = true;
            break;
        }
#else
        if (::connect(socket_fd_, ptr->ai_addr, ptr->ai_addrlen) != -1) {
            connected = true;
            break;
        }
#endif
    }
    freeaddrinfo(result_addrinfo);

    if (!connected) {
        Logger::error("TCPSocket::connectSocket: All attempts to connect to " + host + ":" + port_str + " failed.");
        closeSocket();
        return false;
    }
    Logger::info("TCPSocket: Successfully connected to " + host + ":" + port_str + " on fd " + std::to_string(getRawSocketDescriptor()));
    return true;
}

/*!
 * \brief Серверная привязка сокета к порту.
 * \param port Порт.
 * \return true при успехе.
 */
bool TCPSocket::bindSocket(int port) {
    if (isValid()) {
        Logger::warn("TCPSocket::bindSocket: Socket fd " + std::to_string(getRawSocketDescriptor()) + " is already valid. Closing it first.");
        closeSocket();
    }
#ifdef _WIN32
    socket_fd_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_fd_ == INVALID_SOCKET) { Logger::error("TCPSocket::bindSocket: socket() creation failed. WSAError: " + std::to_string(WSAGetLastError())); return false; }
#else
    socket_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) { Logger::error("TCPSocket::bindSocket: socket() creation failed. Errno(" + std::to_string(errno) +"): " + std::strerror(errno)); return false; }
#endif
    Logger::debug("TCPSocket::bindSocket: System socket created for binding, fd=" + std::to_string(getRawSocketDescriptor()));

#ifdef _WIN32
    char optval_win = 1;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &optval_win, sizeof(optval_win)) == SOCKET_ERROR) {
         Logger::warn("TCPSocket::bindSocket: setsockopt(SO_REUSEADDR) failed. WSAError: " + std::to_string(WSAGetLastError()) + ". Continuing anyway.");
    }
#else
    int optval_posix = 1;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &optval_posix, sizeof(optval_posix)) < 0) {
        Logger::warn("TCPSocket::bindSocket: setsockopt(SO_REUSEADDR) failed. Errno(" + std::to_string(errno) +"): " + std::strerror(errno) + ". Continuing anyway.");
    }
#endif

    sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(static_cast<unsigned short>(port));

    if (::bind(socket_fd_, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
#ifdef _WIN32
        Logger::error("TCPSocket::bindSocket: bind() to port " + std::to_string(port) + " failed. WSAError: " + std::to_string(WSAGetLastError()));
#else
        Logger::error("TCPSocket::bindSocket: bind() to port " + std::to_string(port) + " failed. Errno(" + std::to_string(errno) +"): " + std::strerror(errno));
#endif
        closeSocket();
        return false;
    }
    Logger::info("TCPSocket: Socket fd " + std::to_string(getRawSocketDescriptor()) + " successfully bound to port " + std::to_string(port));
    return true;
}

/*!
 * \brief Перевод сокета в режим прослушивания.
 * \param backlog Макс. очередь ожидающих соединений.
 * \return true при успехе.
 */
bool TCPSocket::listenSocket(int backlog) {
    if (!isValid()) {
        Logger::error("TCPSocket::listenSocket: Called on an invalid (not bound or closed) socket.");
        return false;
    }
    if (::listen(socket_fd_, backlog) < 0) {
#ifdef _WIN32
        Logger::error("TCPSocket::listenSocket: listen() call failed. WSAError: " + std::to_string(WSAGetLastError()));
#else
        Logger::error("TCPSocket::listenSocket: listen() call failed. Errno(" + std::to_string(errno) +"): " + std::strerror(errno));
#endif
        return false;
    }
    Logger::info("TCPSocket: Socket fd " + std::to_string(getRawSocketDescriptor()) + " is now listening with backlog " + std::to_string(backlog));
    return true;
}

/*!
 * \brief Прием входящего соединения.
 * \param client_ip Указатель для IP клиента (опционально).
 * \param client_port Указатель для порта клиента (опционально).
 * \return Новый TCPSocket для клиента или невалидный TCPSocket при ошибке.
 */
TCPSocket TCPSocket::acceptSocket(std::string* client_ip, int* client_port) {
    if (!isValid()) {
        Logger::warn("TCPSocket::acceptSocket: Called on an invalid listening socket (fd: " + std::to_string(getRawSocketDescriptor()) + ").");
        return TCPSocket();
    }

    sockaddr_storage client_addr_storage;
    socklen_t client_addr_len = sizeof(client_addr_storage);

#ifdef _WIN32
    SOCKET accepted_socket_raw = ::accept(socket_fd_, reinterpret_cast<sockaddr*>(&client_addr_storage), &client_addr_len);
    if (accepted_socket_raw == INVALID_SOCKET) {
        int error_code = WSAGetLastError();
        if (error_code == WSAEINTR ||
            error_code == WSAECONNABORTED ||
            error_code == WSAEWOULDBLOCK ||
            error_code == WSAESHUTDOWN ||
            error_code == WSAENOTSOCK) {
            Logger::debug("TCPSocket::acceptSocket: accept() returned non-fatal error or interruption. WSAError: " + std::to_string(error_code));
        } else {
             Logger::warn("TCPSocket::acceptSocket: accept() failed. WSAError: " + std::to_string(error_code));
        }
        return TCPSocket();
    }
#else // POSIX
    int accepted_socket_raw = ::accept(socket_fd_, reinterpret_cast<sockaddr*>(&client_addr_storage), &client_addr_len);
    if (accepted_socket_raw < 0) {
        int error_code = errno;
        if (error_code == EINTR || error_code == ECONNABORTED ||
            error_code == EWOULDBLOCK || error_code == EAGAIN ||
            error_code == ENOTSOCK ) {
             Logger::debug("TCPSocket::acceptSocket: accept() returned non-fatal error or interruption. Errno(" + std::to_string(error_code) +"): " + std::strerror(error_code));
        } else {
            Logger::warn("TCPSocket::acceptSocket: accept() failed. Errno(" + std::to_string(error_code) +"): " + std::strerror(error_code));
        }
        return TCPSocket();
    }
#endif

    if (client_ip || client_port) {
        char host_str[NI_MAXHOST] = {0};
        char port_str[NI_MAXSERV] = {0};

        if (getnameinfo(reinterpret_cast<sockaddr*>(&client_addr_storage), client_addr_len,
                        host_str, NI_MAXHOST, port_str, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
            if (client_ip) *client_ip = host_str;
            if (client_port) {
                try {
                    *client_port = std::stoi(port_str);
                } catch (const std::exception& e_stoi) {
                    Logger::error("TCPSocket::acceptSocket: Ошибка конвертации порта клиента '" + std::string(port_str) + "' в число: " + e_stoi.what());
                    *client_port = 0;
                }
            }
            Logger::info("TCPSocket: Accepted connection from " + std::string(host_str) + ":" + std::string(port_str) +
                         " on new fd " + std::to_string(static_cast<int>(accepted_socket_raw)));
        } else {
#ifdef _WIN32
            Logger::warn("TCPSocket::acceptSocket: getnameinfo failed for accepted client. WSAError: " + std::to_string(WSAGetLastError()));
#else
            Logger::warn("TCPSocket::acceptSocket: getnameinfo failed for accepted client. Errno(" + std::to_string(errno) +"): " + std::strerror(errno));
#endif
            if(client_ip) client_ip->clear();
            if(client_port) *client_port = 0;
        }
    }
    return TCPSocket(static_cast<int>(accepted_socket_raw));
}

/*!
 * \brief Отправка всех данных из буфера.
 * \param buffer Указатель на буфер.
 * \param length Длина данных.
 * \return Количество отправленных байт или -1 при ошибке.
 */
int TCPSocket::sendAllData(const char* buffer, size_t length) const {
    if (!isValid()) { Logger::error("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + ")::sendAllData: Invalid socket."); return -1; }
    if (buffer == nullptr && length > 0) { Logger::error("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + ")::sendAllData: Buffer is null with non-zero length ("+ std::to_string(length) +")."); return -1; }
    if (length == 0) return 0;

    size_t total_sent = 0;
    while (total_sent < length) {
        ssize_t bytes_sent_this_call;
#ifdef _WIN32
        bytes_sent_this_call = ::send(socket_fd_, buffer + total_sent, static_cast<int>(length - total_sent), 0);
        if (bytes_sent_this_call == SOCKET_ERROR) {
            int error_code = WSAGetLastError();
            if (error_code == WSAEWOULDBLOCK) {
                Logger::debug("TCPSocket (fd " + std::to_string(socket_fd_) + ")::sendAllData: send WSAEWOULDBLOCK. Sent " + std::to_string(total_sent) + "/" + std::to_string(length) + " bytes so far.");
                return static_cast<int>(total_sent);
            }
            Logger::error("TCPSocket (fd " + std::to_string(socket_fd_) + ")::sendAllData: send failed. WSAError: " + std::to_string(error_code));
            return -1;
        }
#else // POSIX
        bytes_sent_this_call = ::send(socket_fd_, buffer + total_sent, length - total_sent, MSG_NOSIGNAL);
        if (bytes_sent_this_call < 0) {
            int error_code = errno;
            if (error_code == EINTR) { Logger::debug("TCPSocket (fd " + std::to_string(socket_fd_) + ")::sendAllData: send interrupted by EINTR, retrying."); continue; }
            if (error_code == EAGAIN
#if defined(EAGAIN) && defined(EWOULDBLOCK) && EAGAIN != EWOULDBLOCK
                || error_code == EWOULDBLOCK
#endif
            ) {
                Logger::debug("TCPSocket (fd " + std::to_string(socket_fd_) + ")::sendAllData: send would block (EAGAIN/EWOULDBLOCK). Sent " + std::to_string(total_sent) + "/" + std::to_string(length) + " bytes so far.");
                return static_cast<int>(total_sent);
            }
            Logger::error("TCPSocket (fd " + std::to_string(socket_fd_) + ")::sendAllData: send failed. Errno(" + std::to_string(error_code) +"): " + std::strerror(error_code));
            return -1;
        }
#endif
        if (bytes_sent_this_call == 0) {
            Logger::warn("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + ")::sendAllData: send returned 0. Peer might have closed connection or send buffer is full. Sent " + std::to_string(total_sent) + "/" + std::to_string(length) + " bytes.");
            return static_cast<int>(total_sent);
        }
        total_sent += static_cast<size_t>(bytes_sent_this_call);
    }
    return static_cast<int>(total_sent);
}

/*!
 * \brief Отправка данных с префиксом длины.
 * \param data Строка для отправки.
 * \return true при успехе.
 */
bool TCPSocket::sendAllDataWithLengthPrefix(const std::string& data) const {
    if (!isValid()) { Logger::error("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + ")::sendAllDataWithLengthPrefix: Invalid socket."); return false; }

    uint32_t data_len_host = static_cast<uint32_t>(data.length());
    if (data_len_host > MAX_MESSAGE_PAYLOAD_SIZE) {
        Logger::error("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + ")::sendAllDataWithLengthPrefix: Data size (" + std::to_string(data_len_host) + " bytes) exceeds MAX_MESSAGE_PAYLOAD_SIZE (" + std::to_string(MAX_MESSAGE_PAYLOAD_SIZE) + "). Message not sent.");
        return false;
    }
    uint32_t data_len_net = htonl(data_len_host);

    if (sendAllData(reinterpret_cast<const char*>(&data_len_net), sizeof(data_len_net)) != static_cast<int>(sizeof(data_len_net))) {
        Logger::error("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + ")::sendAllDataWithLengthPrefix: Failed to send length prefix (" + std::to_string(sizeof(data_len_net)) + " bytes).");
        return false;
    }

    if (data_len_host > 0) {
        if (sendAllData(data.c_str(), data.length()) != static_cast<int>(data.length())) {
            Logger::error("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + ")::sendAllDataWithLengthPrefix: Failed to send data payload of size " + std::to_string(data_len_host) + " bytes.");
            return false;
        }
    }
    return true;
}

/*!
 * \brief Получение всех данных указанной длины.
 * \param buffer Указатель на буфер.
 * \param length_to_receive Длина данных для получения.
 * \return Количество полученных байт или -1/-2 при ошибке/таймауте.
 */
int TCPSocket::receiveAllData(char* buffer, size_t length_to_receive) const {
    if (!isValid()) { Logger::error("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + ")::receiveAllData: Invalid socket."); return -1; }
    if (buffer == nullptr && length_to_receive > 0) { Logger::error("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + ")::receiveAllData: Buffer is null with non-zero length (" + std::to_string(length_to_receive) + ")."); return -1; }
    if (length_to_receive == 0) return 0;

    size_t total_received = 0;
    while (total_received < length_to_receive) {
        ssize_t bytes_received_this_call;
#ifdef _WIN32
        bytes_received_this_call = ::recv(socket_fd_, buffer + total_received, static_cast<int>(length_to_receive - total_received), 0);
        if (bytes_received_this_call == SOCKET_ERROR) {
            int error_code = WSAGetLastError();
            if (error_code == WSAEWOULDBLOCK || error_code == WSAETIMEDOUT) {
                 Logger::debug("TCPSocket (fd " + std::to_string(socket_fd_) + ")::receiveAllData: recv WSAEWOULDBLOCK/WSAETIMEDOUT. Received " + std::to_string(total_received) + "/" + std::to_string(length_to_receive) + " bytes so far.");
                 return static_cast<int>(total_received);
            }
            Logger::error("TCPSocket (fd " + std::to_string(socket_fd_) + ")::receiveAllData: recv failed. WSAError: " + std::to_string(error_code));
            return -1;
        }
#else // POSIX
        bytes_received_this_call = ::recv(socket_fd_, buffer + total_received, length_to_receive - total_received, 0);
        if (bytes_received_this_call < 0) {
            int error_code = errno;
            if (error_code == EINTR) { Logger::debug("TCPSocket (fd " + std::to_string(socket_fd_) + ")::receiveAllData: recv interrupted by EINTR, retrying."); continue; }
            if (error_code == EAGAIN
#if defined(EAGAIN) && defined(EWOULDBLOCK) && EAGAIN != EWOULDBLOCK
                || error_code == EWOULDBLOCK
#endif
            ) {
                Logger::debug("TCPSocket (fd " + std::to_string(socket_fd_) + ")::receiveAllData: recv EAGAIN/EWOULDBLOCK. Received " + std::to_string(total_received) + "/" + std::to_string(length_to_receive) + " bytes so far.");
                return static_cast<int>(total_received);
            }
            Logger::error("TCPSocket (fd " + std::to_string(socket_fd_) + ")::receiveAllData: recv failed. Errno(" + std::to_string(error_code) +"): " + std::strerror(error_code));
            return -1;
        }
#endif
        if (bytes_received_this_call == 0) {
            Logger::info("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + ")::receiveAllData: Connection closed by peer. Received " + std::to_string(total_received) + "/" + std::to_string(length_to_receive) + " bytes before close.");
            return static_cast<int>(total_received);
        }
        total_received += static_cast<size_t>(bytes_received_this_call);
    }
    return static_cast<int>(total_received);
}

/*!
 * \brief Получение данных с префиксом длины.
 * \param success Выходной флаг успеха.
 * \param timeout_ms Таймаут в мс.
 * \return Строка с данными или пустая строка при ошибке/таймауте.
 */
std::string TCPSocket::receiveAllDataWithLengthPrefix(bool& success, int timeout_ms) {
    success = false;
    if (!isValid()) {
        Logger::error("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + ")::receiveAllDataWithLengthPrefix: Invalid socket.");
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
        if (getsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, (char*)&original_timeout_val_win, &optlen_win) == 0) {
            original_timeout_fetched_win = true;
        } else {
            Logger::warn("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + ")::receiveAllDataWithLengthPrefix: getsockopt SO_RCVTIMEO failed. WSAError: " + std::to_string(WSAGetLastError()));
        }
        DWORD new_timeout_win = static_cast<DWORD>(timeout_ms == 0 ? 1 : timeout_ms);
        if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&new_timeout_win, sizeof(new_timeout_win)) == 0) {
            temporary_timeout_was_set = true;
            Logger::debug("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + ")::receiveAllDataWithLengthPrefix: Set temporary SO_RCVTIMEO to " + std::to_string(new_timeout_win) + "ms.");
        } else {
            Logger::error("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + ")::receiveAllDataWithLengthPrefix: setsockopt SO_RCVTIMEO to " + std::to_string(new_timeout_win) + "ms failed. WSAError: " + std::to_string(WSAGetLastError()));
        }
#else // POSIX
        if (getsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &original_timeout_val_posix, &optlen_posix) == 0) {
            original_timeout_fetched_posix = true;
        } else {
            Logger::warn("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + ")::receiveAllDataWithLengthPrefix: getsockopt SO_RCVTIMEO failed. Errno(" + std::to_string(errno) + "): " + std::strerror(errno));
        }
        timeval new_timeout_posix;
        if (timeout_ms == 0) {
            new_timeout_posix.tv_sec = 0;
            new_timeout_posix.tv_usec = 1000;
        } else {
            new_timeout_posix.tv_sec = timeout_ms / 1000;
            new_timeout_posix.tv_usec = (timeout_ms % 1000) * 1000;
        }
        if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &new_timeout_posix, sizeof(new_timeout_posix)) == 0) {
            temporary_timeout_was_set = true;
            Logger::debug("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + ")::receiveAllDataWithLengthPrefix: Set temporary SO_RCVTIMEO to " + std::to_string(timeout_ms) + "ms (sec=" + std::to_string(new_timeout_posix.tv_sec) + ", usec=" + std::to_string(new_timeout_posix.tv_usec) + ").");
        } else {
            Logger::error("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + ")::receiveAllDataWithLengthPrefix: setsockopt SO_RCVTIMEO to " + std::to_string(timeout_ms) + "ms failed. Errno(" + std::to_string(errno) + "): " + std::strerror(errno));
        }
#endif
    }

    auto restore_timeout_finalizer = std::shared_ptr<void>(nullptr,
        [&](void*){
            if (temporary_timeout_was_set && isValid()) {
            #ifdef _WIN32
                if (original_timeout_fetched_win) {
                    if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&original_timeout_val_win, sizeof(original_timeout_val_win)) != 0) {
                        Logger::warn("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + ")::receiveAllDataWithLengthPrefix: Failed to restore SO_RCVTIMEO. WSAError: " + std::to_string(WSAGetLastError()));
                    }
                }
            #else // POSIX
                if (original_timeout_fetched_posix) {
                    if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &original_timeout_val_posix, sizeof(original_timeout_val_posix)) != 0) {
                        Logger::warn("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + ")::receiveAllDataWithLengthPrefix: Failed to restore SO_RCVTIMEO. Errno(" + std::to_string(errno) + "): " + std::strerror(errno));
                    }
                }
            #endif
            }
        }
    );

    char len_buffer[sizeof(uint32_t)];
    int bytes_len_received = receiveAllData(len_buffer, sizeof(uint32_t));

    if (bytes_len_received != static_cast<int>(sizeof(uint32_t))) {
        if (bytes_len_received == 0 && isValid()) {
             Logger::info("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + ")::receiveAllDataWithLengthPrefix: Connection closed by peer while receiving length prefix.");
        } else if (bytes_len_received > 0) {
            Logger::warn("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + ")::receiveAllDataWithLengthPrefix: Received incomplete length prefix (" + std::to_string(bytes_len_received) + " bytes). Probable timeout or error.");
        } else if (bytes_len_received < 0 && isValid()) {
             Logger::error("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + ")::receiveAllDataWithLengthPrefix: Socket error while receiving length prefix.");
        }
        return "";
    }

    uint32_t data_len_net;
    std::memcpy(&data_len_net, len_buffer, sizeof(uint32_t));
    uint32_t data_len_host = ntohl(data_len_net);

    if (data_len_host == 0) {
        success = true;
        Logger::debug("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + ")::receiveAllDataWithLengthPrefix: Received empty message (length prefix was 0).");
        return "";
    }

    if (data_len_host > MAX_MESSAGE_PAYLOAD_SIZE) {
        Logger::error("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + ")::receiveAllDataWithLengthPrefix: Declared message size (" + std::to_string(data_len_host) + " bytes) exceeds MAX_MESSAGE_PAYLOAD_SIZE (" + std::to_string(MAX_MESSAGE_PAYLOAD_SIZE) + "). Possible DoS or protocol error. Closing socket.");
        closeSocket();
        return "";
    }

    std::vector<char> data_buffer_vec(data_len_host);
    int bytes_payload_received = receiveAllData(data_buffer_vec.data(), data_len_host);

    if (bytes_payload_received != static_cast<int>(data_len_host)) {
        if (bytes_payload_received == 0 && isValid() && data_len_host > 0) {
            Logger::info("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + ")::receiveAllDataWithLengthPrefix: Connection closed by peer while receiving payload ("+ std::to_string(bytes_payload_received) + "/" + std::to_string(data_len_host) +").");
        } else if (bytes_payload_received > 0) {
            Logger::warn("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + ")::receiveAllDataWithLengthPrefix: Received incomplete payload (" + std::to_string(bytes_payload_received) + "/" + std::to_string(data_len_host) + " bytes). Probable timeout or error.");
        } else if (bytes_payload_received < 0 && isValid()) {
            Logger::error("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + ")::receiveAllDataWithLengthPrefix: Socket error while receiving payload.");
        }
        return "";
    }

    success = true;
    return std::string(data_buffer_vec.data(), data_len_host);
}

/*!
 * \brief Установка неблокирующего режима.
 * \param non_blocking true для неблокирующего, false для блокирующего.
 * \return true при успехе.
 */
bool TCPSocket::setNonBlocking(bool non_blocking) {
    if (!isValid()) { Logger::error("TCPSocket::setNonBlocking: Called on an invalid socket."); return false; }
#ifdef _WIN32
    u_long mode = non_blocking ? 1 : 0;
    if (ioctlsocket(socket_fd_, FIONBIO, &mode) != 0) {
        Logger::error("TCPSocket::setNonBlocking: ioctlsocket(FIONBIO) failed. WSAError: " + std::to_string(WSAGetLastError()));
        return false;
    }
#else // POSIX
    int flags = fcntl(socket_fd_, F_GETFL, 0);
    if (flags == -1) {
        Logger::error("TCPSocket::setNonBlocking: fcntl(F_GETFL) failed. Errno(" + std::to_string(errno) + "): " + std::strerror(errno));
        return false;
    }
    flags = non_blocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (fcntl(socket_fd_, F_SETFL, flags) == -1) {
        Logger::error("TCPSocket::setNonBlocking: fcntl(F_SETFL) failed. Errno(" + std::to_string(errno) + "): " + std::strerror(errno));
        return false;
    }
#endif
    Logger::debug("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + "): Non-blocking mode " + (non_blocking ? "enabled." : "disabled."));
    return true;
}

/*!
 * \brief Установка таймаута на получение.
 * \param timeout_ms Таймаут в мс.
 * \return true при успехе.
 */
bool TCPSocket::setRecvTimeout(int timeout_ms) {
    if (!isValid()) { Logger::error("TCPSocket::setRecvTimeout: Called on an invalid socket."); return false; }
    if (timeout_ms < 0) { Logger::warn("TCPSocket::setRecvTimeout: Negative timeout value (" + std::to_string(timeout_ms) + "ms) is invalid. Not setting timeout."); return false;}
    Logger::debug("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + "): Setting SO_RCVTIMEO to " + std::to_string(timeout_ms) + " ms.");
#ifdef _WIN32
    DWORD timeout_val_dword = static_cast<DWORD>(timeout_ms);
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_val_dword, sizeof(timeout_val_dword)) == SOCKET_ERROR) {
        Logger::error("TCPSocket::setRecvTimeout (Win): setsockopt failed. WSAError: " + std::to_string(WSAGetLastError()));
        return false;
    }
#else // POSIX
    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        Logger::error("TCPSocket::setRecvTimeout (POSIX): setsockopt failed. Errno(" + std::to_string(errno) + "): " + std::strerror(errno));
        return false;
    }
#endif
    return true;
}

/*!
 * \brief Установка таймаута на отправку.
 * \param timeout_ms Таймаут в мс.
 * \return true при успехе.
 */
bool TCPSocket::setSendTimeout(int timeout_ms) {
    if (!isValid()) { Logger::error("TCPSocket::setSendTimeout: Called on an invalid socket."); return false; }
    if (timeout_ms < 0) { Logger::warn("TCPSocket::setSendTimeout: Negative timeout value (" + std::to_string(timeout_ms) + "ms) is invalid. Not setting timeout."); return false;}
    Logger::debug("TCPSocket (fd " + std::to_string(getRawSocketDescriptor()) + "): Setting SO_SNDTIMEO to " + std::to_string(timeout_ms) + " ms.");
#ifdef _WIN32
    DWORD timeout_val_dword = static_cast<DWORD>(timeout_ms);
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout_val_dword, sizeof(timeout_val_dword)) == SOCKET_ERROR) {
        Logger::error("TCPSocket::setSendTimeout (Win): setsockopt failed. WSAError: " + std::to_string(WSAGetLastError()));
        return false;
    }
#else // POSIX
    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        Logger::error("TCPSocket::setSendTimeout (POSIX): setsockopt failed. Errno(" + std::to_string(errno) + "): " + std::strerror(errno));
        return false;
    }
#endif
    return true;
}
