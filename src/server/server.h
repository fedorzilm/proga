// Предполагаемый путь: src/server/server.h
#ifndef SERVER_H
#define SERVER_H

#include "database.h"       // Предполагаемый путь: src/core/database.h
#include "tariff_plan.h"    // Предполагаемый путь: src/core/tariff_plan.h
#include "query_parser.h"   // Предполагаемый путь: src/core/query_parser.h
#include "tcp_socket.h"     // Предполагаемый путь: src/net/tcp_socket.h
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable> // Для более чистого завершения потоков

// Глобальный флаг для обработки сигналов (объявлен в server_main.cpp)
extern std::atomic<bool> g_server_should_stop;

/*!
 * \class Server
 * \brief Управляет сетевым прослушиванием, приемом клиентов и делегированием обработки их запросов.
 */
class Server final {
public:
    /*!
     * \brief Конструктор сервера.
     * \param port Порт для прослушивания.
     * \param db Ссылка на экземпляр Database (общий для всех клиентов).
     * \param plan Ссылка на экземпляр TariffPlan.
     * \param parser Ссылка на экземпляр QueryParser (может быть создан внутри ServerCommandHandler).
     * \param server_exec_path Путь к исполняемому файлу сервера или базовый путь для файловых операций.
     */
    Server(int port,
           Database& db,
           TariffPlan& plan,
           QueryParser& parser, // QueryParser может быть и не нужен здесь, если он только в CommandHandler
           const std::string& server_exec_path);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    /*! \brief Запускает сервер (начинает прослушивание и прием соединений). \return true в случае успеха, false при ошибке. */
    bool start();
    /*! \brief Инициирует остановку сервера. */
    void stop();
    /*! \brief Проверяет, запущен ли сервер. \return true, если сервер активен. */
    bool isRunning() const { return running_.load(); }


private:
    void acceptorThreadLoop(); // Основной цикл принятия соединений
    void clientHandlerThread(TCPSocket client_socket); // Обработка одного клиента

    int port_;
    Database& db_;                  // Общая база данных
    TariffPlan& tariff_plan_;       // Общий тарифный план
    QueryParser& query_parser_;     // Общий парсер запросов (если он stateless)
                                    // Либо каждый CommandHandler создает свой.
    std::string server_executable_path_; // Для ServerCommandHandler

    TCPSocket listen_socket_{};       // Слушающий сокет сервера
    std::vector<std::thread> client_threads_{}; // Вектор для хранения потоков клиентов
    std::mutex client_threads_mutex_{}; // Мьютекс для защиты доступа к client_threads_
    std::mutex db_mutex_{};           // Мьютекс для защиты доступа к Database

    std::atomic<bool> running_{false}; // Флаг для управления основным циклом сервера и потоками
    std::thread acceptor_thread_{};   // Поток для acceptConnections
    
    // Для более чистого join потоков при остановке:
    // std::condition_variable shutdown_cv_; 
    // std::mutex shutdown_mutex_;
};

#endif // SERVER_H
