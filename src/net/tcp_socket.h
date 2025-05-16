// Предполагаемый путь: src/net/tcp_socket.h
#ifndef TCP_SOCKET_H
#define TCP_SOCKET_H

#include "common_defs.h" // Для std::string, std::vector, uint32_t, std::mutex
                         // common_defs.h должен включать <string>, <vector>, <cstdint>, <mutex>
#include <string>
#include <vector>
#include <cstdint> // Для uint32_t
#include <mutex>   // Для wsa_mutex_

// Platform-specific includes
#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h> // Основные функции Winsock
    #include <ws2tcpip.h> // Для getaddrinfo, sockaddr_in, inet_ntop и т.д.
    #pragma comment(lib, "Ws2_32.lib") // Линковка с библиотекой сокетов Windows
#else // POSIX
    #include <sys/socket.h> // Основные функции сокетов
    #include <netinet/in.h> // Структуры sockaddr_in, sockaddr_in6
    #include <arpa/inet.h>  // Функции inet_ntop, htonl и т.д.
    #include <unistd.h>     // Для close, read, write, usleep
    #include <fcntl.h>      // Для fcntl (O_NONBLOCK)
    #include <netdb.h>      // Для getaddrinfo, gethostbyname (хотя getaddrinfo предпочтительнее)
    #include <cerrno>       // Для errno
    #include <cstring>      // Для strerror, memset, memcpy
    #include <sys/time.h>   // Для struct timeval (используется в setsockopt SO_RCVTIMEO на POSIX)
#endif

/*!
 * \file tcp_socket.h
 * \brief Определяет класс TCPSocket для инкапсуляции операций с TCP сокетами.
 * Предоставляет кроссплатформенный интерфейс для Windows (Winsock) и POSIX сокетов.
 */

/*!
 * \class TCPSocket
 * \brief Обертка для TCP сокета, предоставляющая основные сетевые операции.
 *
 * Включает создание сокета, подключение к серверу, привязку к порту,
 * прослушивание, прием соединений, отправку и получение данных,
 * а также корректное закрытие сокета.
 * Реализует протокол "длина (4 байта) + данные" для обмена сообщениями.
 * Класс помечен как final, так как не предназначен для наследования.
 */
class TCPSocket final {
public:
    /*! \brief Конструктор по умолчанию. Создает невалидный сокет. Инициализирует WSA для Windows, если необходимо. */
    TCPSocket();
    /*! \brief Конструктор для сокета, созданного внешне (например, после accept). \param socket_fd Дескриптор существующего сокета. */
    explicit TCPSocket(int socket_fd); // На Windows int будет приведен к SOCKET
    /*! \brief Деструктор. Закрывает сокет, если он валиден. Уменьшает счетчик ссылок WSA для Windows. */
    ~TCPSocket();

    // Запрещаем копирование, разрешаем перемещение
    TCPSocket(const TCPSocket&) = delete;
    TCPSocket& operator=(const TCPSocket&) = delete;
    /*! \brief Конструктор перемещения. \param other R-value ссылка на другой TCPSocket. */
    TCPSocket(TCPSocket&& other) noexcept;
    /*! \brief Оператор присваивания перемещением. \param other R-value ссылка на другой TCPSocket. \return Ссылка на текущий объект. */
    TCPSocket& operator=(TCPSocket&& other) noexcept;

    /*! \brief Устанавливает TCP соединение с указанным хостом и портом (для клиента). \param host Имя хоста или IP-адрес сервера. \param port Порт сервера. \return true в случае успеха, false при ошибке. */
    bool connectSocket(const std::string& host, int port);
    /*! \brief Привязывает сокет к указанному порту на всех доступных сетевых интерфейсах (для сервера). \param port Порт для привязки. \return true в случае успеха, false при ошибке. */
    bool bindSocket(int port);
    /*! \brief Переводит сокет в режим прослушивания входящих соединений (для сервера). \param backlog Максимальное количество ожидающих соединений в очереди. \return true в случае успеха, false при ошибке. */
    bool listenSocket(int backlog = 20); // Увеличен backlog по умолчанию
    /*!
     * \brief Принимает входящее соединение (для сервера). Блокирующий вызов.
     * \param client_ip Если не nullptr, сюда будет записан IP-адрес подключившегося клиента.
     * \param client_port Если не nullptr, сюда будет записан порт подключившегося клиента.
     * \return Новый объект TCPSocket для взаимодействия с клиентом. Если произошла ошибка, возвращается невалидный TCPSocket.
     */
    TCPSocket acceptSocket(std::string* client_ip = nullptr, int* client_port = nullptr);

    /*!
     * \brief Отправляет все данные из буфера через сокет. Гарантирует отправку всех байт или сообщает об ошибке.
     * \param buffer Указатель на буфер с данными.
     * \param length Количество байт для отправки.
     * \return Количество успешно отправленных байт. -1 в случае ошибки. 0 может означать, что соединение было закрыто.
     */
    int sendAllData(const char* buffer, size_t length) const;
    /*!
     * \brief Отправляет строку данных с предварительной отправкой ее длины (4 байта в сетевом порядке).
     * \param data Строка для отправки.
     * \return true в случае полной успешной отправки, false при ошибке.
     */
    bool sendAllDataWithLengthPrefix(const std::string& data) const;

    /*!
     * \brief Получает указанное количество байт данных из сокета в буфер. Гарантирует получение всех запрошенных байт или сообщает об ошибке/закрытии соединения.
     * \param buffer Указатель на буфер для сохранения полученных данных.
     * \param length_to_receive Количество байт, которое необходимо получить.
     * \return Количество успешно полученных байт. 0, если соединение было корректно закрыто другой стороной. -1 в случае ошибки сокета.
     */
    int receiveAllData(char* buffer, size_t length_to_receive) const;
    /*!
     * \brief Получает сообщение, предваренное его длиной (4 байта).
     * Сначала читает длину, затем сами данные.
     * \param success Выходной параметр, true если сообщение успешно получено, false в противном случае.
     * \param timeout_ms Таймаут в миллисекундах на операцию получения (-1 для блокирующего ожидания, 0 для немедленного возврата без блокировки на Windows, если SO_RCVTIMEO установлен в 0).
     * \return Строка с полученными данными. Пустая строка в случае ошибки, таймаута или если длина сообщения была 0.
     */
    std::string receiveAllDataWithLengthPrefix(bool& success, int timeout_ms = -1);

    /*! \brief Закрывает сокет. */
    void closeSocket();
    /*! \brief Проверяет, является ли сокет валидным (открытым). \return true, если сокет валиден. */
    bool isValid() const;
    /*! \brief Возвращает "сырой" дескриптор сокета. \return Дескриптор сокета (int или SOCKET). */
    int getRawSocketDescriptor() const; // На Windows возвращаемый int будет SOCKET

    /*! \brief Устанавливает сокет в неблокирующий или блокирующий режим. \param non_blocking true для неблокирующего режима, false для блокирующего. \return true в случае успеха. */
    bool setNonBlocking(bool non_blocking);
    /*! \brief Устанавливает опцию SO_RCVTIMEO для сокета. \param timeout_ms Таймаут в миллисекундах. 0 - отключает таймаут. \return true в случае успеха. */
    bool setRecvTimeout(int timeout_ms);
    /*! \brief Устанавливает опцию SO_SNDTIMEO для сокета. \param timeout_ms Таймаут в миллисекундах. 0 - отключает таймаут. \return true в случае успеха. */
    bool setSendTimeout(int timeout_ms);


private:
#ifdef _WIN32
    SOCKET socket_fd_ = INVALID_SOCKET; /*!< Дескриптор сокета для Windows. */
    static bool wsa_initialized_;       /*!< Флаг инициализации WSA. */
    static int wsa_ref_count_;          /*!< Счетчик ссылок для WSAStartup/WSACleanup. */
    static std::mutex wsa_mutex_;       /*!< Мьютекс для синхронизации инициализации/очистки WSA. */
    static bool initialize_wsa();       /*!< Вспомогательный метод для инициализации WSA. */
    static void cleanup_wsa();          /*!< Вспомогательный метод для очистки WSA. */
#else // POSIX
    int socket_fd_ = -1;                /*!< Файловый дескриптор сокета для POSIX. */
    // Вспомогательные методы для POSIX, обрабатывающие EINTR (можно сделать inline или оставить в .cpp)
    // int posix_retry_send(const char* data, size_t len, int flags) const;
    // int posix_retry_recv(char* data, size_t len, int flags) const;
#endif
};

#endif // TCP_SOCKET_H
