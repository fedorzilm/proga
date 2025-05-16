// Предполагаемый путь: src/server/server_command_handler.h
#ifndef SERVER_COMMAND_HANDLER_H
#define SERVER_COMMAND_HANDLER_H

#include "database.h"    // Предполагаемый путь: src/core/database.h
#include "tariff_plan.h" // Предполагаемый путь: src/core/tariff_plan.h
#include "query_parser.h"// Предполагаемый путь: src/core/query_parser.h
#include <string>
#include <sstream> // Для std::ostringstream

/*!
 * \class ServerCommandHandler
 * \brief Обрабатывает разобранные запросы Query, взаимодействуя с Database и TariffPlan.
 * Формирует строковый ответ для отправки клиенту.
 * Этот класс не управляет сетевым взаимодействием или потоками, только логикой команд.
 */
class ServerCommandHandler final {
public:
    /*!
     * \brief Конструктор.
     * \param db Ссылка на экземпляр Database.
     * \param plan Ссылка на экземпляр TariffPlan.
     * \param server_exec_path_param Путь к исполняемому файлу сервера или определенный корневой путь сервера,
     * используемый для разрешения относительных путей к файлам данных (LOAD/SAVE).
     */
    ServerCommandHandler(Database& db, TariffPlan& plan, const std::string& server_exec_path_param);

    ServerCommandHandler(const ServerCommandHandler&) = delete;
    ServerCommandHandler& operator=(const ServerCommandHandler&) = delete;
    // Можно разрешить перемещение, если необходимо, но для простоты пока запретим
    ServerCommandHandler(ServerCommandHandler&&) = delete;
    ServerCommandHandler& operator=(ServerCommandHandler&&) = delete;


    /*!
     * \brief Обрабатывает один объект Query и формирует строковый ответ.
     * \param query Разобранный запрос от клиента.
     * \return Строка с результатом выполнения команды или сообщением об ошибке.
     */
    std::string processCommand(const Query& query);

private:
    Database& db_;
    TariffPlan& tariff_plan_;
    std::string server_executable_path_; /*!< Базовый путь сервера для разрешения файловых операций. */

    // Вспомогательные методы для обработки каждого типа команды.
    // Они принимают QueryParameters и ostringstream для формирования ответа.
    void handleAdd(const QueryParameters& params, std::ostringstream& oss);
    void handleSelect(const QueryParameters& params, std::ostringstream& oss);
    void handleDelete(const QueryParameters& params, std::ostringstream& oss);
    void handleEdit(const QueryParameters& params, std::ostringstream& oss);
    void handleCalculateCharges(const QueryParameters& params, std::ostringstream& oss);
    void handlePrintAll(std::ostringstream& oss);
    void handleLoad(const QueryParameters& params, std::ostringstream& oss);
    void handleSave(const QueryParameters& params, std::ostringstream& oss);
    // Команды HELP и EXIT (сессии клиента) обрабатываются в processCommand особым образом.
};

#endif // SERVER_COMMAND_HANDLER_H
