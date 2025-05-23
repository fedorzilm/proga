/*!
 * \file server_command_handler.h
 * \brief Определяет класс ServerCommandHandler для обработки команд, полученных сервером от клиента,
 * и отправки структурированных ответов, включая поддержку многочастных ответов.
 *
 * ServerCommandHandler инкапсулирует логику выполнения различных запросов к базе данных,
 * таких как добавление, выборка, удаление, редактирование записей, расчет начислений,
 * а также операции загрузки и сохранения базы данных из/в файл на сервере.
 * Он взаимодействует с экземплярами `Database` и `TariffPlan` для выполнения этих операций
 * и формирует структурированный ответ (одно- или многочастный) для отправки обратно клиенту
 * через предоставленный `TCPSocket`.
 */
#ifndef SERVER_COMMAND_HANDLER_H
#define SERVER_COMMAND_HANDLER_H

#include "database.h"       // Для взаимодействия с базой данных (включая FileOperationResult)
#include "tariff_plan.h"    // Для доступа к тарифному плану при расчете начислений
#include "query_parser.h"   // Для использования Query и QueryParameters
#include "common_defs.h"    // Для общих определений, констант протокола и стандартных заголовков
#include "tcp_socket.h"     // Для std::shared_ptr<TCPSocket>

#include <string>
#include <sstream>          // Для std::ostringstream при формировании ответа
#include <vector>           // Для передачи вектора записей в sendChunkedResponse
#include <memory>           // Для std::shared_ptr

/*!
 * \struct ServerResponse
 * \brief Структура для подготовки ответа сервера перед его форматированием и отправкой.
 *
 * Содержит все необходимые поля для формирования как одночастного, так и многочастного ответа,
 * включая код статуса, сообщение, тип и данные полезной нагрузки, а также информацию для чанкования.
 */
struct ServerResponse {
    int statusCode = SRV_STATUS_SERVER_ERROR;           /*!< Числовой код статуса ответа (см. SRV_STATUS_*). */
    std::string statusMessage{};                        /*!< Текстовое сообщение, поясняющее статус. */
    std::string payloadType = SRV_PAYLOAD_TYPE_NONE;    /*!< Строковый идентификатор типа полезной нагрузки (см. SRV_PAYLOAD_TYPE_*). */
    std::ostringstream payloadDataStream{};             /*!< Поток для формирования текстовых данных полезной нагрузки (для одночастных ответов или первого чанка). */
    size_t recordsInPayload = 0;                        /*!< Количество записей ProviderRecord в payloadDataStream или текущем чанке. */
    size_t totalRecordsOverall = 0;                     /*!< Общее количество записей ProviderRecord (используется для первого сообщения многочастного ответа). */

    // Данные для чанкования, если payloadType = SRV_PAYLOAD_TYPE_PROVIDER_RECORDS_LIST и записей много
    bool requiresChunking = false;                      /*!< Флаг, указывающий, требуется ли отправка ответа по частям. */
    std::vector<ProviderRecord> recordsForChunking{};   /*!< Копия записей для отправки по частям. Заполняется, если requiresChunking = true. */

    /*! \brief Сбрасывает все поля структуры в состояние по умолчанию. */
    void reset() noexcept {
        statusCode = SRV_STATUS_SERVER_ERROR;
        statusMessage.clear();
        payloadType = SRV_PAYLOAD_TYPE_NONE;
        payloadDataStream.str("");
        payloadDataStream.clear();
        recordsInPayload = 0;
        totalRecordsOverall = 0;
        requiresChunking = false;
        recordsForChunking.clear();
    }

    /*! \brief Конструктор по умолчанию, вызывает reset. */
    ServerResponse() { // Не может быть noexcept из-за потенциально бросающих конструкторов std::string/vector/ostringstream (хотя они обычно nothrow_constructible по умолчанию)
        reset();
    }
};


/*!
 * \class ServerCommandHandler
 * \brief Обрабатывает разобранные запросы `Query`, взаимодействуя с `Database` и `TariffPlan`,
 * и отправляет структурированные ответы клиенту.
 *
 * Формирует и отправляет строковый ответ (возможно, многочастный) через предоставленный
 * `TCPSocket`. Этот класс не управляет сетевым взаимодействием на низком уровне или потоками,
 * он сосредоточен на логике выполнения команд и формировании ответа согласно протоколу.
 * Для операций `LOAD` и `SAVE` использует безопасное формирование путей к файлам
 * на сервере. Класс помечен как `final`, так как не предназначен для наследования.
 */
class ServerCommandHandler final {
public:
    /*!
     * \brief Конструктор обработчика команд сервера.
     * \param db Ссылка на экземпляр `Database`, используемый сервером.
     * \param plan Ссылка на экземпляр `TariffPlan`, используемый сервером.
     * \param server_data_path_base Абсолютный или разрешенный базовый путь на сервере для операций `LOAD`/`SAVE`.
     */
    ServerCommandHandler(Database& db, TariffPlan& plan, const std::string& server_data_path_base);

    // Запрещаем копирование и перемещение
    ServerCommandHandler(const ServerCommandHandler&) = delete;
    ServerCommandHandler& operator=(const ServerCommandHandler&) = delete;
    ServerCommandHandler(ServerCommandHandler&&) = delete;
    ServerCommandHandler& operator=(ServerCommandHandler&&) = delete;


    /*!
     * \brief Обрабатывает один объект `Query` (разобранный запрос от клиента) и отправляет
     * структурированный ответ (или серию ответов при чанковании) клиенту через предоставленный сокет.
     * Вызывает соответствующий приватный метод-обработчик в зависимости от `query.type` для
     * заполнения структуры `ServerResponse`. Затем вызывает методы для отправки этого ответа.
     * Оборачивает выполнение в `try-catch` для обработки возможных исключений и формирования
     * сообщений об ошибках для клиента.
     * \param client_socket Умный указатель на объект `TCPSocket` для взаимодействия с конкретным клиентом.
     * \param query Разобранный запрос от клиента.
     */
    void processAndSendCommandResponse(std::shared_ptr<TCPSocket> client_socket, const Query& query);

    /*!
     * \brief (Сделан public для возможности отправки ответа об ошибке парсинга из Server::clientHandlerTask)
     * Отправляет одночастный ответ или первый чанк многочастного ответа.
     * Формирует полный текстовый блок (заголовок + данные) и отправляет его через `client_socket->sendAllDataWithLengthPrefix`.
     * \param client_socket Сокет клиента.
     * \param response Заполненная структура `ServerResponse`, содержащая данные для отправки.
     * Для первого чанка `response.statusCode` должен быть `SRV_STATUS_OK_MULTI_PART_BEGIN`.
     */
    void sendSingleMessageResponsePart(std::shared_ptr<TCPSocket> client_socket, const ServerResponse& response) const;


private:
    Database& db_;                          /*!< Ссылка на экземпляр базы данных. */
    TariffPlan& tariff_plan_;               /*!< Ссылка на экземпляр тарифного плана. */
    std::string server_data_base_path_;     /*!< Базовый путь на сервере для операций `LOAD`/`SAVE`. */

    // Приватные вспомогательные методы для обработки каждого типа команды.
    // Они принимают параметры запроса и заполняют структуру `ServerResponse`.

    /*! \brief Обрабатывает команду `ADD`. Заполняет `response`. */
    void handleAdd(const QueryParameters& params, ServerResponse& response);
    /*! \brief Обрабатывает команду `SELECT`. Заполняет `response`. Может установить `response.requiresChunking`. */
    void handleSelect(const QueryParameters& params, ServerResponse& response);
    /*! \brief Обрабатывает команду `DELETE`. Заполняет `response`. */
    void handleDelete(const QueryParameters& params, ServerResponse& response);
    /*! \brief Обрабатывает команду `EDIT`. Заполняет `response`. */
    void handleEdit(const QueryParameters& params, ServerResponse& response);
    /*! \brief Обрабатывает команду `CALCULATE_CHARGES`. Заполняет `response`. */
    void handleCalculateCharges(const QueryParameters& params, ServerResponse& response);
    /*! \brief Обрабатывает команду `PRINT_ALL`. Заполняет `response`. Может установить `response.requiresChunking`. */
    void handlePrintAll(ServerResponse& response);
    /*! \brief Обрабатывает команду `LOAD`. Заполняет `response`. */
    void handleLoad(const QueryParameters& params, ServerResponse& response);
    /*! \brief Обрабатывает команду `SAVE`. Заполняет `response`. */
    void handleSave(const QueryParameters& params, ServerResponse& response);
    /*! \brief Обрабатывает команду `HELP`. Заполняет `response`. */
    void handleHelp(ServerResponse& response);
    /*! \brief Обрабатывает команду `EXIT`. Заполняет `response`. */
    void handleExit(ServerResponse& response);
    /*! \brief Обрабатывает неизвестную команду. Заполняет `response`. */
    void handleUnknown(const Query& query, ServerResponse& response);

    // Вспомогательные методы для отправки ответов через сокет.

    /*!
     * \brief Отправляет последующие чанки данных для многочастного ответа.
     * Вызывается после `sendSingleMessageResponsePart` с кодом `SRV_STATUS_OK_MULTI_PART_BEGIN`.
     * Отправляет оставшиеся записи из `response.recordsForChunking` порциями.
     * Завершает отправку сообщением со статусом `SRV_STATUS_OK_MULTI_PART_END`.
     * \param client_socket Сокет клиента.
     * \param initialResponseContext Структура `ServerResponse`, которая была использована для отправки первого чанка.
     * Используется для получения `recordsForChunking` и `totalRecordsOverall`.
     */
    void sendRemainingChunks(std::shared_ptr<TCPSocket> client_socket, const ServerResponse& initialResponseContext) const;

    /*!
     * \brief Вспомогательный метод для форматирования вектора записей `ProviderRecord` в поток `std::ostringstream`.
     * \param oss Выходной поток.
     * \param records Вектор записей для форматирования.
     * \param start_index Начальный индекс в векторе `records`.
     * \param count Количество записей для форматирования из `records`, начиная с `start_index`.
     * \param add_display_indices Если `true`, перед каждой записью будет добавлен префикс "Record (Display Index #<idx>):\n", где idx - это индекс в текущей выборке/чанке.
     * \param [[maybe_unused]] db_indices_map Карта, связывающая индекс в `records` с оригинальным индексом в БД (пока не используется в полной мере, задел на будущее).
     */
    void formatRecordsToStream(std::ostringstream& oss,
                               const std::vector<ProviderRecord>& records,
                               size_t start_index,
                               size_t count,
                               bool add_display_indices = false,
                               [[maybe_unused]] const std::map<size_t, size_t>* db_indices_map = nullptr) const; // db_indices_map - для отображения реальных индексов БД, если потребуется
};

#endif // SERVER_COMMAND_HANDLER_H
