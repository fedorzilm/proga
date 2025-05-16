/*!
 * \file server_command_handler.h
 * \author Fedor Zilnitskiy
 * \brief Определяет класс ServerCommandHandler для обработки команд, полученных сервером от клиента.
 *
 * ServerCommandHandler инкапсулирует логику выполнения различных запросов к базе данных,
 * таких как добавление, выборка, удаление, редактирование записей, расчет начислений,
 * а также операции загрузки и сохранения базы данных из/в файл на сервере.
 * Он взаимодействует с экземплярами `Database` и `TariffPlan` для выполнения этих операций
 * и формирует текстовый ответ для отправки обратно клиенту.
 */
#ifndef SERVER_COMMAND_HANDLER_H
#define SERVER_COMMAND_HANDLER_H

#include "database.h"       // Для взаимодействия с базой данных (включая FileOperationResult)
#include "tariff_plan.h"    // Для доступа к тарифному плану при расчете начислений
#include "query_parser.h"   // Для использования Query и QueryParameters
#include "common_defs.h"    // Для общих определений и стандартных заголовков <string>, <sstream>

#include <string>
#include <sstream> // Для std::ostringstream при формировании ответа

/*!
 * \class ServerCommandHandler
 * \brief Обрабатывает разобранные запросы `Query`, взаимодействуя с `Database` и `TariffPlan`.
 *
 * Формирует строковый ответ для отправки клиенту. Этот класс не управляет сетевым
 * взаимодействием или потоками, он сосредоточен исключительно на логике выполнения команд.
 * Для операций `LOAD` и `SAVE` использует безопасное формирование путей к файлам
 * на сервере, чтобы предотвратить несанкционированный доступ к файловой системе.
 * Класс помечен как `final`, так как не предназначен для наследования.
 */
class ServerCommandHandler final {
public:
    /*!
     * \brief Конструктор обработчика команд сервера.
     * \param db Ссылка на экземпляр `Database`, используемый сервером.
     * \param plan Ссылка на экземпляр `TariffPlan`, используемый сервером.
     * \param server_data_path_base Абсолютный базовый путь на сервере, относительно которого
     * будут разрешаться пути к файлам данных для операций `LOAD`/`SAVE`.
     * Обычно это корень проекта или специально выделенная директория данных,
     * к которой будет добавлена поддиректория `DEFAULT_SERVER_DATA_SUBDIR`.
     * Если строка пуста, `getSafeServerFilePath_SCH` попытается определить
     * корень проекта на основе текущей рабочей директории сервера.
     */
    ServerCommandHandler(Database& db, TariffPlan& plan, const std::string& server_data_path_base);

    // Запрещаем копирование и перемещение, так как обработчик обычно создается на сессию/запрос
    // и содержит ссылки на общие ресурсы (db, plan).
    ServerCommandHandler(const ServerCommandHandler&) = delete;
    ServerCommandHandler& operator=(const ServerCommandHandler&) = delete;
    ServerCommandHandler(ServerCommandHandler&&) = delete;
    ServerCommandHandler& operator=(ServerCommandHandler&&) = delete;


    /*!
     * \brief Обрабатывает один объект `Query` (разобранный запрос от клиента) и формирует строковый ответ.
     * Вызывает соответствующий приватный метод-обработчик в зависимости от `query.type`.
     * Оборачивает выполнение в `try-catch` для обработки возможных исключений и формирования
     * сообщений об ошибках для клиента.
     * \param query Разобранный запрос от клиента, содержащий тип команды и параметры.
     * \return Строка с результатом выполнения команды или сообщением об ошибке, готовая к отправке клиенту.
     */
    std::string processCommand(const Query& query);

private:
    Database& db_;                          /*!< Ссылка на экземпляр базы данных. */
    TariffPlan& tariff_plan_;               /*!< Ссылка на экземпляр тарифного плана. */
    std::string server_data_base_path_;     /*!< Базовый путь на сервере для операций `LOAD`/`SAVE` (например, корень проекта или `data-dir` из конфига). */

    // Приватные вспомогательные методы для обработки каждого типа команды.
    // Они принимают параметры запроса и формируют ответ в `std::ostringstream`.

    /*! \brief Обрабатывает команду `ADD`. */
    void handleAdd(const QueryParameters& params, std::ostringstream& oss);
    /*! \brief Обрабатывает команду `SELECT`. */
    void handleSelect(const QueryParameters& params, std::ostringstream& oss);
    /*! \brief Обрабатывает команду `DELETE`. */
    void handleDelete(const QueryParameters& params, std::ostringstream& oss);
    /*! \brief Обрабатывает команду `EDIT`. */
    void handleEdit(const QueryParameters& params, std::ostringstream& oss);
    /*! \brief Обрабатывает команду `CALCULATE_CHARGES`. */
    void handleCalculateCharges(const QueryParameters& params, std::ostringstream& oss);
    /*! \brief Обрабатывает команду `PRINT_ALL`. */
    void handlePrintAll(std::ostringstream& oss);
    /*! \brief Обрабатывает команду `LOAD`. */
    void handleLoad(const QueryParameters& params, std::ostringstream& oss);
    /*! \brief Обрабатывает команду `SAVE`. */
    void handleSave(const QueryParameters& params, std::ostringstream& oss);
    
    // Команды HELP и EXIT (сессии клиента) обрабатываются в processCommand особым образом или на уровне Server.
};

#endif // SERVER_COMMAND_HANDLER_H
