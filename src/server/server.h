/*!
 * \file server.h
 * \author Fedor Zilnitskiy
 * \brief Определяет класс Server, который управляет работой TCP-сервера базы данных.
 *
 * Класс Server отвечает за инициализацию слушающего сокета, прием входящих клиентских
 * соединений и передачу их на обработку в пул потоков. Управляет жизненным циклом
 * сервера, включая запуск и корректную остановку. Взаимодействует с другими
 * компонентами, такими как Database, TariffPlan, QueryParser (через ServerCommandHandler)
 * и ServerConfig.
 */
#ifndef SERVER_H
#define SERVER_H

#include "common_defs.h"    // Включает <atomic>, <vector>, <thread>, <mutex>, <shared_mutex>, <memory>, <string>
#include "database.h"
#include "tariff_plan.h"
#include "query_parser.h"
#include "tcp_socket.h"
#include "thread_pool.h"
#include "server_config.h"

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <memory> // Для std::unique_ptr и std::shared_ptr

/*!
 * \brief Глобальный атомарный флаг для запроса остановки сервера.
 * Устанавливается обработчиками сигналов (SIGINT, SIGTERM) или консольных событий (Ctrl+C).
 * Серверные потоки периодически проверяют этот флаг для корректного завершения.
 */
extern std::atomic<bool> g_server_should_stop;

/*!
 * \class Server
 * \brief Управляет сетевым прослушиванием, приемом клиентских соединений и делегированием
 * обработки их запросов пулу потоков.
 *
 * Сервер инициализируется с конфигурацией (`ServerConfig`), общей базой данных (`Database`),
 * тарифным планом (`TariffPlan`) и парсером запросов (`QueryParser`).
 * При запуске начинает прослушивать указанный в конфигурации порт.
 * Каждое новое клиентское соединение передается как задача в `ThreadPool`.
 * Доступ к базе данных (`Database`) синхронизируется с помощью `std::shared_mutex`
 * для обеспечения возможности одновременного чтения несколькими клиентами.
 */
class Server final {
public:
    /*!
     * \brief Конструктор сервера.
     * Инициализирует сервер с предоставленной конфигурацией и ссылками на основные компоненты.
     * Создает пул потоков для обработки клиентских запросов.
     * \param config Конфигурация сервера (порт, размер пула потоков, пути и т.д.).
     * \param db Ссылка на экземпляр `Database` (общий для всех клиентов).
     * \param plan Ссылка на экземпляр `TariffPlan`.
     * \param parser Ссылка на экземпляр `QueryParser` (используется в `ServerCommandHandler`).
     * \param server_executable_path Полный путь к исполняемому файлу сервера. Используется для
     * определения базового пути для файловых операций `ServerCommandHandler`, если
     * `config.server_data_root_dir` не задан или задан относительно.
     * \throw std::runtime_error если не удается создать ThreadPool.
     */
    Server(const ServerConfig& config,
           Database& db,
           TariffPlan& plan,
           QueryParser& parser,
           const std::string& server_executable_path);

    /*!
     * \brief Деструктор сервера.
     * Гарантирует корректную остановку сервера, включая остановку пула потоков
     * и завершение потока-акцептора, если они еще активны.
     */
    ~Server();

    // Запрещаем копирование и присваивание, так как сервер - уникальный ресурс.
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    /*!
     * \brief Запускает сервер: инициализирует слушающий сокет и начинает прием соединений.
     * Запускает поток `acceptorThreadLoop` для приема новых клиентов.
     * \return `true` в случае успешного запуска, `false` при ошибке (например, не удалось привязать порт или создать ThreadPool).
     */
    bool start();

    /*!
     * \brief Инициирует процедуру остановки сервера.
     * Устанавливает флаги остановки, закрывает слушающий сокет, останавливает пул потоков
     * и ожидает завершения потока-акцептора. Безопасен для многократного вызова.
     */
    void stop();

    /*!
     * \brief Проверяет, активен ли сервер в данный момент.
     * \return `true`, если сервер запущен и работает (не находится в процессе остановки), иначе `false`.
     */
    bool isRunning() const noexcept { return running_.load(); } 


private:
    /*! \brief Основной цикл потока, принимающего новые клиентские соединения. */
    void acceptorThreadLoop();

    /*!
     * \brief Задача, выполняемая в пуле потоков для обработки одного клиента.
     * Получает запросы от клиента, передает их в `ServerCommandHandler` и отправляет ответы.
     * \param client_socket_sptr Умный указатель (shared_ptr) на объект `TCPSocket` для взаимодействия с конкретным клиентом.
     */
    void clientHandlerTask(std::shared_ptr<TCPSocket> client_socket_sptr);

    const ServerConfig& config_;        /*!< Ссылка на неизменяемую конфигурацию сервера. */
    Database& db_;                      /*!< Ссылка на общий экземпляр базы данных. */
    TariffPlan& tariff_plan_;           /*!< Ссылка на общий экземпляр тарифного плана. */
    QueryParser& query_parser_;         /*!< Ссылка на общий экземпляр парсера запросов. */

    std::string server_base_path_for_commands_; /*!< Базовый путь, используемый ServerCommandHandler для файловых операций (LOAD/SAVE). Определяется из config или server_executable_path. */

    TCPSocket listen_socket_{};           /*!< Слушающий сокет сервера. */

    std::unique_ptr<ThreadPool> thread_pool_; /*!< Умный указатель на пул потоков для обработки клиентских задач. */

    // Мьютекс для защиты доступа к Database.
    // Используется shared_mutex для разрешения одновременного чтения несколькими потоками
    // и эксклюзивного доступа для операций записи.
    mutable std::shared_mutex db_shared_mutex_{}; /*!< Мьютекс для синхронизации доступа к базе данных. `mutable` чтобы можно было лочить в const методах, если бы такие были и требовали лок (здесь не тот случай, но для общего понимания). */

    std::atomic<bool> running_{false};    /*!< Атомарный флаг, указывающий, активен ли сервер. */
    std::thread acceptor_thread_{};       /*!< Поток для выполнения `acceptorThreadLoop`. */
};

#endif // SERVER_H
