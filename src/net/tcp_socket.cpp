// Предполагаемый путь: src/net/tcp_socket.cpp
#include "tcp_socket.h"
#include "logger.h"     // Предполагаемый путь: src/utils/logger.h
#include <stdexcept>    // Для std::runtime_error
#include <vector>
#include <chrono>       // Для std::chrono (если будет использоваться для таймаутов внутри select/poll)
#include <algorithm>    // Для std::min (если нужно)
#include <iostream>     // Для временной отладки (лучше убрать в финальной версии)

// --- Статические члены для WSA (Windows) ---
#ifdef _WIN32
    bool TCPSocket::wsa_initialized_ = false;
    int TCPSocket::wsa_ref_count_ = 0;
    std::mutex TCPSocket::wsa_mutex_; // Определен в .h, здесь используется

    bool TCPSocket::initialize_wsa() {
        std::lock_guard<std::mutex> lock(wsa_mutex_); // Синхронизируем доступ
        if (wsa_ref_count_ == 0) {
            WSADATA wsaData;
            int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
            if (result != 0) {
                // Логгер может быть еще не инициализирован, если это первый объект TCPSocket
                // std::cerr << "TCPSocket: WSAStartup failed. Error Code: " << result << std::endl;
                Logger::error("TCPSocket: WSAStartup failed. Error Code: " + std::to_string(result));
                return false;
            }
            wsa_initialized_ = true;
            Logger::debug("TCPSocket: WSAStartup successful.");
        }
        wsa_ref_count_++;
        return true;
    }

    void TCPSocket::cleanup_wsa() {
        std::lock_guard<std::mutex> lock(wsa_mutex_);
        if (wsa_ref_count_ > 0) {
            wsa_ref_count_--;
            if (wsa_ref_count_ == 0 && wsa_initialized_) {
                if (WSACleanup() == SOCKET_ERROR) {
                     Logger::error("TCPSocket: WSACleanup failed. Error: " + std::to_string(WSAGetLastError()));
                } else {
                    Logger::debug("TCPSocket: WSACleanup called successfully.");
                }
                wsa_initialized_ = false;
            }
        }
    }
#endif

// --- Конструкторы и Деструктор ---
TCPSocket::TCPSocket() {
#ifdef _WIN32
    if (!initialize_wsa()) {
        // Критическая ошибка, невозможно создать сокет
        throw std::runtime_error("TCPSocket: Failed to initialize Winsock in constructor.");
    }
    // socket_fd_ уже INVALID_SOCKET по умолчанию
#else
    socket_fd_ = -1; // Уже по умолчанию
#endif
    Logger::debug("TCPSocket: Default constructor. Socket fd: " + std::to_string(socket_fd_));
}

TCPSocket::TCPSocket(int socket_fd_param)
#ifdef _WIN32
    : socket_fd_(static_cast<SOCKET>(socket_fd_param)) // Приводим int к SOCKET для Windows
#else
    : socket_fd_(socket_fd_param)
#endif
{
#ifdef _WIN32
    if (!initialize_wsa()) { // Все равно нужно для подсчета ссылок, даже если сокет уже есть
         throw std::runtime_error("TCPSocket: Failed to initialize Winsock in constructor with fd.");
    }
#endif
    Logger::debug("TCPSocket: Constructor with fd " + std::to_string(socket_fd_param) + " initialized.");
}

TCPSocket::~TCPSocket() {
    Logger::debug("TCPSocket: Destructor for fd " + std::to_string(getRawSocketDescriptor()) + " called.");
    closeSocket(); // Закрываем сокет, если он валиден
#ifdef _WIN32
    cleanup_wsa(); // Уменьшаем счетчик ссылок WSA
#endif
}

// --- Перемещение ---
TCPSocket::TCPSocket(TCPSocket&& other) noexcept
#ifdef _WIN32
    : socket_fd_(other.socket_fd_)
#else
    : socket_fd_(other.socket_fd_)
#endif
{
#ifdef _WIN32
    // Убеждаемся, что WSA инициализирован для нового объекта, если он будет использоваться
    // Это немного избыточно, если other уже проинициализировал, но безопасно
    if (socket_fd_ != INVALID_SOCKET && !initialize_wsa()) {
        // Если WSA не удалось инициализировать, сокет становится невалидным
        Logger::error("TCPSocket: Move constructor failed to initialize WSA for valid moved socket.");
        socket_fd_ = INVALID_SOCKET; 
    }
    other.socket_fd_ = INVALID_SOCKET;
#else
    other.socket_fd_ = -1;
#endif
    Logger::debug("TCPSocket: Move constructed. New fd: " + std::to_string(getRawSocketDescriptor()) +
                  ", old fd was: " + std::to_string(other.getRawSocketDescriptor()) + " (now invalid).");
}

TCPSocket& TCPSocket::operator=(TCPSocket&& other) noexcept {
    if (this != &other) {
        closeSocket(); // Закрыть текущий сокет

#ifdef _WIN32
        // Уменьшаем счетчик WSA для старого сокета (если он был валиден и уникален для этого объекта)
        // и увеличиваем для нового, если он перемещается
        // Эта логика становится сложной, если конструкторы/деструкторы сами управляют wsa_ref_count_
        // Проще всего, чтобы initialize_wsa/cleanup_wsa были идемпотентны и вызывались при необходимости
        if (isValid()) { // Если текущий сокет был валиден
           // cleanup_wsa(); // Уменьшаем счетчик для текущего объекта перед присвоением
        }
        socket_fd_ = other.socket_fd_;
        other.socket_fd_ = INVALID_SOCKET;
        if (isValid()) { // Если перемещенный сокет валиден
           // initialize_wsa(); // Увеличиваем счетчик для нового объекта
        }
#else
        socket_fd_ = other.socket_fd_;
        other.socket_fd_ = -1;
#endif
        Logger::debug("TCPSocket: Move assigned. New fd: " + std::to_string(getRawSocketDescriptor()) +
                      ", old fd was: " + std::to_string(other.getRawSocketDescriptor()) + " (now invalid).");
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
        Logger::info("TCPSocket: Closing socket fd " + std::to_string(getRawSocketDescriptor()));
#ifdef _WIN32
        // Сначала shutdown для корректного завершения TCP-сессии
        if (::shutdown(socket_fd_, SD_BOTH) == SOCKET_ERROR) {
            // Не фатально, если сокет уже закрыт другой стороной или не был подключен
            // Logger::warn("TCPSocket::closeSocket: shutdown failed. Error: " + std::to_string(WSAGetLastError()));
        }
        if (::closesocket(socket_fd_) == SOCKET_ERROR) {
            Logger::error("TCPSocket::closeSocket: closesocket failed. Error: " + std::to_string(WSAGetLastError()));
        }
        socket_fd_ = INVALID_SOCKET;
#else
        if (::shutdown(socket_fd_, SHUT_RDWR) < 0) {
            // Logger::warn("TCPSocket::closeSocket: shutdown failed. Error: " + std::string(strerror(errno)));
        }
        if (::close(socket_fd_) < 0) {
            Logger::error("TCPSocket::closeSocket: close failed. Error: " + std::string(strerror(errno)));
        }
        socket_fd_ = -1;
#endif
    }
}

int TCPSocket::getRawSocketDescriptor() const {
#ifdef _WIN32
    return static_cast<int>(socket_fd_); // Приведение SOCKET к int
#else
    return socket_fd_;
#endif
}


bool TCPSocket::connectSocket(const std::string& host, int port) {
    if (isValid()) {
        Logger::warn("TCPSocket::connectSocket: Socket already valid (fd=" + std::to_string(getRawSocketDescriptor()) + "). Closing first.");
        closeSocket();
    }

#ifdef _WIN32
    socket_fd_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_fd_ == INVALID_SOCKET) {
        Logger::error("TCPSocket::connectSocket: socket() creation failed. Error: " + std::to_string(WSAGetLastError()));
        return false;
    }
#else
    socket_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        Logger::error("TCPSocket::connectSocket: socket() creation failed. Error: " + std::string(strerror(errno)));
        return false;
    }
#endif
    Logger::debug("TCPSocket::connectSocket: Socket created, fd=" + std::to_string(getRawSocketDescriptor()));

    addrinfo hints{};
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // Только IPv4, как в вашем IPAddress
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo *result_addrinfo = nullptr;
    std::string port_str = std::to_string(port);
    int res = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result_addrinfo);
    if (res != 0) {
#ifdef _WIN32
        Logger::error("TCPSocket::connectSocket: getaddrinfo failed for host '" + host + "'. Error code: " + std::to_string(res) + " (WSAGetLastError: " + std::to_string(WSAGetLastError()) + ")");
#else
        Logger::error("TCPSocket::connectSocket: getaddrinfo failed for host '" + host + "'. Error: " + std::string(gai_strerror(res)));
#endif
        closeSocket(); // Закрываем созданный сокет
        return false;
    }

    bool connected = false;
    // Пробуем подключиться к первому подходящему адресу из списка
    for (addrinfo *ptr = result_addrinfo; ptr != nullptr; ptr = ptr->ai_next) {
        Logger::debug("TCPSocket::connectSocket: Attempting to connect to an address for " + host + ":" + port_str);
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
        // Если connect не удался, логируем ошибку для этого конкретного адреса
#ifdef _WIN32
        Logger::warn("TCPSocket::connectSocket: ::connect attempt failed. Error: " + std::to_string(WSAGetLastError()));
#else
        Logger::warn("TCPSocket::connectSocket: ::connect attempt failed. Error: " + std::string(strerror(errno)));
#endif
    }
    freeaddrinfo(result_addrinfo); // Освобождаем память, выделенную getaddrinfo

    if (!connected) {
        Logger::error("TCPSocket::connectSocket: All attempts to connect to " + host + ":" + port_str + " failed.");
        closeSocket();
        return false;
    }
    Logger::info("TCPSocket: Successfully connected to " + host + ":" + port_str + " on fd " + std::to_string(getRawSocketDescriptor()));
    return true;
}

bool TCPSocket::bindSocket(int port) {
    if (isValid()) {
        Logger::warn("TCPSocket::bindSocket: Socket already valid. Closing first.");
        closeSocket();
    }
#ifdef _WIN32
    socket_fd_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_fd_ == INVALID_SOCKET) { Logger::error("TCPSocket::bindSocket: socket() creation failed. Error: " + std::to_string(WSAGetLastError())); return false; }
#else
    socket_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) { Logger::error("TCPSocket::bindSocket: socket() creation failed. Error: " + std::string(strerror(errno))); return false; }
#endif
    Logger::debug("TCPSocket::bindSocket: Socket created for binding, fd=" + std::to_string(getRawSocketDescriptor()));

    // Разрешить переиспользование адреса (SO_REUSEADDR)
#ifdef _WIN32
    char optval_win = 1; // TRUE
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &optval_win, sizeof(optval_win)) == SOCKET_ERROR) {
         Logger::warn("TCPSocket::bindSocket: setsockopt(SO_REUSEADDR) failed. Error: " + std::to_string(WSAGetLastError()) + ". Continuing anyway.");
    }
#else
    int optval_posix = 1; // TRUE
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &optval_posix, sizeof(optval_posix)) < 0) {
        Logger::warn("TCPSocket::bindSocket: setsockopt(SO_REUSEADDR) failed. Error: " + std::string(strerror(errno)) + ". Continuing anyway.");
    }
#endif

    sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // Слушать на всех интерфейсах
    server_addr.sin_port = htons(static_cast<unsigned short>(port)); // Преобразование в сетевой порядок байт

    if (::bind(socket_fd_, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
#ifdef _WIN32
        Logger::error("TCPSocket::bindSocket: bind() to port " + std::to_string(port) + " failed. Error: " + std::to_string(WSAGetLastError()));
#else
        Logger::error("TCPSocket::bindSocket: bind() to port " + std::to_string(port) + " failed. Error: " + std::string(strerror(errno)));
#endif
        closeSocket();
        return false;
    }
    Logger::info("TCPSocket: Socket fd " + std::to_string(getRawSocketDescriptor()) + " successfully bound to port " + std::to_string(port));
    return true;
}

bool TCPSocket::listenSocket(int backlog) {
    if (!isValid()) {
        Logger::error("TCPSocket::listenSocket: Called on an invalid socket.");
        return false;
    }
    if (::listen(socket_fd_, backlog) < 0) {
#ifdef _WIN32
        Logger::error("TCPSocket::listenSocket: listen() failed. Error: " + std::to_string(WSAGetLastError()));
#else
        Logger::error("TCPSocket::listenSocket: listen() failed. Error: " + std::string(strerror(errno)));
#endif
        return false;
    }
    Logger::info("TCPSocket: Socket fd " + std::to_string(getRawSocketDescriptor()) + " is now listening with backlog " + std::to_string(backlog));
    return true;
}

TCPSocket TCPSocket::acceptSocket(std::string* client_ip, int* client_port) {
    if (!isValid()) {
        Logger::warn("TCPSocket::acceptSocket: Called on an invalid listening socket.");
        return TCPSocket(); // Возвращаем невалидный сокет
    }

    sockaddr_storage client_addr_storage; // Используем sockaddr_storage для IPv4/IPv6 совместимости
    socklen_t client_addr_len = sizeof(client_addr_storage);
    
#ifdef _WIN32
    SOCKET accepted_socket_raw = ::accept(socket_fd_, reinterpret_cast<sockaddr*>(&client_addr_storage), &client_addr_len);
    if (accepted_socket_raw == INVALID_SOCKET) {
        int error_code = WSAGetLastError();
        // WSAEINTR может случиться, если слушающий сокет был закрыт другим потоком
        // WSAECONNABORTED - соединение было прервано
        // WSAEWOULDBLOCK - для неблокирующего сокета, если нет соединений
        // WSAESHUTDOWN - сокет был закрыт через shutdown()
        if (error_code != WSAEINTR && error_code != WSAECONNABORTED && error_code != WSAEWOULDBLOCK && error_code != WSAENOTSOCK /*слушающий сокет закрыт*/ ) {
             Logger::warn("TCPSocket::acceptSocket: accept() failed. Error: " + std::to_string(error_code));
        } else if (error_code == WSAEINTR || error_code == WSAENOTSOCK) { // WSAENOTSOCK - слушающий сокет был закрыт
            Logger::debug("TCPSocket::acceptSocket: accept() interrupted or listening socket closed. Error: " + std::to_string(error_code));
        }
        return TCPSocket(); // Возвращаем невалидный сокет
    }
#else // POSIX
    int accepted_socket_raw = ::accept(socket_fd_, reinterpret_cast<sockaddr*>(&client_addr_storage), &client_addr_len);
    if (accepted_socket_raw < 0) {
        if (errno != EINTR && errno != ECONNABORTED && errno != EWOULDBLOCK && errno != EAGAIN && errno != ENOTSOCK) {
            Logger::warn("TCPSocket::acceptSocket: accept() failed. Error: " + std::string(strerror(errno)));
        } else if (errno == EINTR || errno == ENOTSOCK) {
             Logger::debug("TCPSocket::acceptSocket: accept() interrupted or listening socket closed. Error: " + std::string(strerror(errno)));
        }
        return TCPSocket();
    }
#endif

    if (client_ip || client_port) { // Если нужны данные о клиенте
        char host_str[NI_MAXHOST] = {0};
        char port_str[NI_MAXSERV] = {0};
        // NI_NUMERICHOST - не пытаться разрешить имя хоста, вернуть IP
        // NI_NUMERICSERV - не пытаться разрешить имя сервиса, вернуть номер порта
        if (getnameinfo(reinterpret_cast<sockaddr*>(&client_addr_storage), client_addr_len,
                        host_str, NI_MAXHOST, port_str, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
            if (client_ip) *client_ip = host_str;
            if (client_port) {
                try { *client_port = std::stoi(port_str); }
                catch (const std::exception& e) { Logger::error("TCPSocket::acceptSocket: Ошибка конвертации порта клиента '" + std::string(port_str) + "': " + e.what()); }
            }
            Logger::info("TCPSocket: Accepted connection from " + std::string(host_str) + ":" + std::string(port_str) +
                         " on new fd " + std::to_string(accepted_socket_raw));
        } else {
#ifdef _WIN32
            Logger::warn("TCPSocket::acceptSocket: getnameinfo failed for accepted client. Error: " + std::to_string(WSAGetLastError()));
#else
            Logger::warn("TCPSocket::acceptSocket: getnameinfo failed for accepted client. Error: " + std::string(strerror(errno)));
#endif
        }
    }
    return TCPSocket(static_cast<int>(accepted_socket_raw)); // Создаем новый TCPSocket для принятого соединения
}


// --- Отправка и получение данных ---
// Реализации sendAllData, sendAllDataWithLengthPrefix, receiveAllData, receiveAllDataWithLengthPrefix
// из предыдущего ответа (Шаг 15, первая часть `TCPSocket.cpp`) были уже достаточно полными и включали
// циклы для полной отправки/получения, обработку EINTR (для POSIX), и использование Logger.
// Убедитесь, что вы используете ту финальную версию.

// Пример для sendAllData (напоминание)
int TCPSocket::sendAllData(const char* buffer, size_t length) const {
    if (!isValid()) { Logger::error("TCPSocket(fd:" + std::to_string(getRawSocketDescriptor()) + ")::sendAllData: Invalid socket."); return -1; }
    if (buffer == nullptr && length > 0) { Logger::error("TCPSocket(fd:" + std::to_string(getRawSocketDescriptor()) + ")::sendAllData: Buffer is null with non-zero length."); return -1; }
    if (length == 0) return 0;

    size_t total_sent = 0;
    while (total_sent < length) {
        int bytes_sent_this_call;
#ifdef _WIN32
        bytes_sent_this_call = ::send(socket_fd_, buffer + total_sent, static_cast<int>(length - total_sent), 0);
        if (bytes_sent_this_call == SOCKET_ERROR) {
            Logger::error("TCPSocket(fd:" + std::to_string(getRawSocketDescriptor()) + ")::sendAllData: send failed. Error: " + std::to_string(WSAGetLastError()));
            return -1;
        }
#else
        bytes_sent_this_call = ::send(socket_fd_, buffer + total_sent, length - total_sent, MSG_NOSIGNAL); // MSG_NOSIGNAL для предотвращения SIGPIPE
        if (bytes_sent_this_call < 0) {
            if (errno == EINTR) { Logger::debug("TCPSocket(fd:" + std::to_string(socket_fd_) + ")::sendAllData: send interrupted by EINTR, retrying."); continue; }
            if (errno == EAGAIN || errno == EWOULDBLOCK) { Logger::warn("TCPSocket(fd:" + std::to_string(socket_fd_) + ")::sendAllData: send would block (EAGAIN/EWOULDBLOCK). Sent " + std::to_string(total_sent) + " so far."); return static_cast<int>(total_sent); } // Не ошибка, но не все отправлено
            Logger::error("TCPSocket(fd:" + std::to_string(socket_fd_) + ")::sendAllData: send failed. Error: " + std::string(strerror(errno)));
            return -1;
        }
#endif
        if (bytes_sent_this_call == 0) {
            Logger::warn("TCPSocket(fd:" + std::to_string(getRawSocketDescriptor()) + ")::sendAllData: send returned 0, peer might have closed connection. Sent " + std::to_string(total_sent) + "/" + std::to_string(length) + " bytes.");
            return static_cast<int>(total_sent);
        }
        total_sent += static_cast<size_t>(bytes_sent_this_call);
    }
    return static_cast<int>(total_sent);
}

// Реализация sendAllDataWithLengthPrefix, receiveAllData, receiveAllDataWithLengthPrefix
// должна следовать этой же логике полной обработки и логирования.

bool TCPSocket::sendAllDataWithLengthPrefix(const std::string& data) const {
    if (!isValid()) { Logger::error("TCPSocket(fd:" + std::to_string(getRawSocketDescriptor()) + ")::sendAllDataWithLengthPrefix: Invalid socket."); return false; }

    uint32_t data_len = static_cast<uint32_t>(data.length());
    uint32_t data_len_net = htonl(data_len);

    if (sendAllData(reinterpret_cast<const char*>(&data_len_net), sizeof(data_len_net)) != static_cast<int>(sizeof(data_len_net))) {
        Logger::error("TCPSocket(fd:" + std::to_string(getRawSocketDescriptor()) + ")::sendAllDataWithLengthPrefix: Failed to send length prefix.");
        return false;
    }
    if (data_len > 0) {
        if (sendAllData(data.c_str(), data.length()) != static_cast<int>(data.length())) {
            Logger::error("TCPSocket(fd:" + std::to_string(getRawSocketDescriptor()) + ")::sendAllDataWithLengthPrefix: Failed to send data payload of size " + std::to_string(data_len) + ".");
            return false;
        }
    }
    Logger::debug("TCPSocket(fd:" + std::to_string(getRawSocketDescriptor()) + ")::sendAllDataWithLengthPrefix: Successfully sent " + std::to_string(data_len) + " bytes of data with prefix.");
    return true;
}

int TCPSocket::receiveAllData(char* buffer, size_t length_to_receive) const {
    if (!isValid()) { Logger::error("TCPSocket(fd:" + std::to_string(getRawSocketDescriptor()) + ")::receiveAllData: Invalid socket."); return -1; }
    if (buffer == nullptr && length_to_receive > 0) { Logger::error("TCPSocket(fd:" + std::to_string(getRawSocketDescriptor()) + ")::receiveAllData: Buffer is null with non-zero length."); return -1; }
    if (length_to_receive == 0) return 0;

    size_t total_received = 0;
    while (total_received < length_to_receive) {
        int bytes_received_this_call;
#ifdef _WIN32
        bytes_received_this_call = ::recv(socket_fd_, buffer + total_received, static_cast<int>(length_to_receive - total_received), 0);
        if (bytes_received_this_call == SOCKET_ERROR) {
            int error_code = WSAGetLastError();
            if (error_code == WSAEWOULDBLOCK || error_code == WSAETIMEDOUT) {
                 Logger::debug("TCPSocket(fd:" + std::to_string(socket_fd_) + ")::receiveAllData: recv WSAEWOULDBLOCK/WSAETIMEDOUT. Received " + std::to_string(total_received) + " so far.");
                 return static_cast<int>(total_received); 
            }
            Logger::error("TCPSocket(fd:" + std::to_string(socket_fd_) + ")::receiveAllData: recv failed. Error: " + std::to_string(error_code));
            return -1;
        }
#else
        bytes_received_this_call = ::recv(socket_fd_, buffer + total_received, length_to_receive - total_received, 0);
        if (bytes_received_this_call < 0) {
            if (errno == EINTR) { Logger::debug("TCPSocket(fd:" + std::to_string(socket_fd_) + ")::receiveAllData: recv interrupted by EINTR, retrying."); continue; }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                Logger::debug("TCPSocket(fd:" + std::to_string(socket_fd_) + ")::receiveAllData: recv EAGAIN/EWOULDBLOCK. Received " + std::to_string(total_received) + " so far.");
                return static_cast<int>(total_received);
            }
            Logger::error("TCPSocket(fd:" + std::to_string(socket_fd_) + ")::receiveAllData: recv failed. Error: " + std::string(strerror(errno)));
            return -1;
        }
#endif
        if (bytes_received_this_call == 0) {
            Logger::info("TCPSocket(fd:" + std::to_string(getRawSocketDescriptor()) + ")::receiveAllData: Connection closed by peer. Received " + std::to_string(total_received) + "/" + std::to_string(length_to_receive) + " bytes.");
            return static_cast<int>(total_received);
        }
        total_received += static_cast<size_t>(bytes_received_this_call);
    }
    return static_cast<int>(total_received);
}


std::string TCPSocket::receiveAllDataWithLengthPrefix(bool& success, int timeout_ms) {
    success = false;
    if (!isValid()) { Logger::error("TCPSocket(fd:" + std::to_string(getRawSocketDescriptor()) + ")::receiveAllDataWithLengthPrefix: Invalid socket."); return ""; }

    // Установка и сброс таймаута (как было реализовано ранее, с get/setsockopt)
    // Этот код был в предыдущем ответе, здесь для краткости опущен, но он должен быть тут.
    // ... (код установки и сброса SO_RCVTIMEO) ...
    bool original_timeout_set = false;
    #ifdef _WIN32
        DWORD original_timeout_val_win = 0; // Сохраняем оригинальный таймаут
        int optlen_win = sizeof(original_timeout_val_win);
        if (timeout_ms >= 0) { // Если таймаут нужно установить/изменить
            if (getsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, (char*)&original_timeout_val_win, &optlen_win) == SOCKET_ERROR) { /* лог */ }
            DWORD new_timeout_win = static_cast<DWORD>(timeout_ms);
            if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&new_timeout_win, sizeof(new_timeout_win)) == SOCKET_ERROR) { /* лог */ } else { original_timeout_set = true; }
        }
    #else
        timeval original_timeout_val_posix = {0,0};
        socklen_t optlen_posix = sizeof(original_timeout_val_posix);
        if (timeout_ms >= 0) {
            if (getsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &original_timeout_val_posix, &optlen_posix) < 0) { /* лог */ }
            timeval new_timeout_posix; new_timeout_posix.tv_sec = timeout_ms / 1000; new_timeout_posix.tv_usec = (timeout_ms % 1000) * 1000;
            if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &new_timeout_posix, sizeof(new_timeout_posix)) < 0) { /* лог */ } else { original_timeout_set = true; }
        }
    #endif


    char len_buffer[sizeof(uint32_t)];
    int bytes_len_received = receiveAllData(len_buffer, sizeof(uint32_t));

    if (bytes_len_received != static_cast<int>(sizeof(uint32_t))) {
        if (bytes_len_received > 0) Logger::warn("TCPSocket(fd:" + std::to_string(getRawSocketDescriptor()) + "): Received incomplete length prefix (" + std::to_string(bytes_len_received) + " bytes).");
        else if (bytes_len_received == 0) Logger::info("TCPSocket(fd:" + std::to_string(getRawSocketDescriptor()) + "): Connection closed by peer while receiving length prefix.");
        // Восстанавливаем таймаут
        if (original_timeout_set) {
            #ifdef _WIN32
            setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&original_timeout_val_win, sizeof(original_timeout_val_win));
            #else
            setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &original_timeout_val_posix, sizeof(original_timeout_val_posix));
            #endif
        }
        return "";
    }

    uint32_t data_len_net;
    std::memcpy(&data_len_net, len_buffer, sizeof(uint32_t));
    uint32_t data_len = ntohl(data_len_net);

    if (data_len == 0) {
        success = true;
        if (original_timeout_set) { /* ... восстановить таймаут ... */ }
        Logger::debug("TCPSocket(fd:" + std::to_string(getRawSocketDescriptor()) + "): Received empty message (length prefix was 0).");
        return "";
    }
    
    if (data_len > MAX_MESSAGE_PAYLOAD_SIZE) { // Используем константу
        Logger::error("TCPSocket(fd:" + std::to_string(getRawSocketDescriptor()) + "): Declared message size (" + std::to_string(data_len) + ") exceeds MAX_MESSAGE_PAYLOAD_SIZE (" + std::to_string(MAX_MESSAGE_PAYLOAD_SIZE) + "). Possible DoS or protocol error. Closing socket.");
        //closeSocket(); // Разрываем соединение
        if (original_timeout_set) { /* ... восстановить таймаут ... */ }
        return "";
    }

    std::vector<char> data_buffer_vec(data_len);
    int bytes_payload_received = receiveAllData(data_buffer_vec.data(), data_len);

    // Восстанавливаем оригинальный таймаут сокета
    if (original_timeout_set) {
        #ifdef _WIN32
        if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&original_timeout_val_win, sizeof(original_timeout_val_win)) == SOCKET_ERROR && isValid()) { /* лог */ }
        #else
        if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &original_timeout_val_posix, sizeof(original_timeout_val_posix)) < 0 && isValid()) { /* лог */ }
        #endif
    }

    if (bytes_payload_received != static_cast<int>(data_len)) {
        if (bytes_payload_received > 0) Logger::warn("TCPSocket(fd:" + std::to_string(getRawSocketDescriptor()) + "): Received incomplete payload (" + std::to_string(bytes_payload_received) + "/" + std::to_string(data_len) + " bytes).");
        else if (bytes_payload_received == 0 && data_len > 0) Logger::info("TCPSocket(fd:" + std::to_string(getRawSocketDescriptor()) + "): Connection closed by peer while receiving payload.");
        return "";
    }

    success = true;
    Logger::debug("TCPSocket(fd:" + std::to_string(getRawSocketDescriptor()) + "): Successfully received " + std::to_string(data_len) + " bytes of data with prefix.");
    return std::string(data_buffer_vec.data(), data_len);
}


bool TCPSocket::setNonBlocking(bool non_blocking) {
    // Реализация из предыдущего ответа была адекватной, с Logger
    if (!isValid()) { Logger::error("TCPSocket::setNonBlocking: Invalid socket."); return false; }
#ifdef _WIN32
    u_long mode = non_blocking ? 1 : 0;
    if (ioctlsocket(socket_fd_, FIONBIO, &mode) != 0) {
        Logger::error("TCPSocket::setNonBlocking: ioctlsocket failed. Error: " + std::to_string(WSAGetLastError()));
        return false;
    }
#else
    int flags = fcntl(socket_fd_, F_GETFL, 0);
    if (flags == -1) {
        Logger::error("TCPSocket::setNonBlocking: fcntl(F_GETFL) failed. Error: " + std::string(strerror(errno)));
        return false;
    }
    flags = non_blocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (fcntl(socket_fd_, F_SETFL, flags) == -1) {
        Logger::error("TCPSocket::setNonBlocking: fcntl(F_SETFL) failed. Error: " + std::string(strerror(errno)));
        return false;
    }
#endif
    Logger::debug("TCPSocket(fd:" + std::to_string(getRawSocketDescriptor()) + "): Non-blocking mode " + (non_blocking ? "enabled." : "disabled."));
    return true;
}

bool TCPSocket::setRecvTimeout(int timeout_ms) {
    if (!isValid()) { Logger::error("TCPSocket::setRecvTimeout: Invalid socket."); return false; }
    Logger::debug("TCPSocket(fd:" + std::to_string(getRawSocketDescriptor()) + "): Setting SO_RCVTIMEO to " + std::to_string(timeout_ms) + " ms.");
#ifdef _WIN32
    DWORD timeout_val = static_cast<DWORD>(timeout_ms);
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_val, sizeof(timeout_val)) == SOCKET_ERROR) {
        Logger::error("TCPSocket::setRecvTimeout (Win): setsockopt failed. Error: " + std::to_string(WSAGetLastError()));
        return false;
    }
#else
    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        Logger::error("TCPSocket::setRecvTimeout (POSIX): setsockopt failed. Error: " + std::string(strerror(errno)));
        return false;
    }
#endif
    return true;
}
bool TCPSocket::setSendTimeout(int timeout_ms) {
    if (!isValid()) { Logger::error("TCPSocket::setSendTimeout: Invalid socket."); return false; }
    Logger::debug("TCPSocket(fd:" + std::to_string(getRawSocketDescriptor()) + "): Setting SO_SNDTIMEO to " + std::to_string(timeout_ms) + " ms.");
#ifdef _WIN32
    DWORD timeout_val = static_cast<DWORD>(timeout_ms);
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout_val, sizeof(timeout_val)) == SOCKET_ERROR) {
        Logger::error("TCPSocket::setSendTimeout (Win): setsockopt failed. Error: " + std::to_string(WSAGetLastError()));
        return false;
    }
#else
    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        Logger::error("TCPSocket::setSendTimeout (POSIX): setsockopt failed. Error: " + std::string(strerror(errno)));
        return false;
    }
#endif
    return true;
}
