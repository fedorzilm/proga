#!/bin/bash

# Скрипт для запуска многоклиентского тестирования (Этап 5)

# Путь к этому скрипту
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"
# Подключаем общие функции
# shellcheck source=test_common.sh
source "${SCRIPT_DIR}/test_common.sh"

# --- Конфигурация ---
PROJECT_ROOT_REL_PATH=".." 
SERVER_EXEC_REL_PATH="build/bin/database_server"
CLIENT_EXEC_REL_PATH="build/bin/database_client"

SERVER_EXEC_ABS_PATH="${SCRIPT_DIR}/${PROJECT_ROOT_REL_PATH}/${SERVER_EXEC_REL_PATH}"
CLIENT_EXEC_ABS_PATH="${SCRIPT_DIR}/${PROJECT_ROOT_REL_PATH}/${CLIENT_EXEC_REL_PATH}"

SERVER_PORT="12347" 
SERVER_HOST="127.0.0.1"
SERVER_LOG_FILE_NAME="server_stage5_test.log"
SERVER_DATA_ROOT_BASE_NAME="test_server_data_root_stage5"
SERVER_TARIFF_FILE_NAME="tariff_stage5.cfg" 

SERVER_LOG_FILE_ABS_PATH="${SCRIPT_DIR}/${SERVER_LOG_FILE_NAME}"
CLIENT_BASE_LOG_NAME="client_stage5_test" 
SERVER_DATA_ROOT_BASE_ABS_PATH="${SCRIPT_DIR}/${SERVER_DATA_ROOT_BASE_NAME}"
SERVER_TARIFF_FILE_ABS_PATH="${SCRIPT_DIR}/${SERVER_TARIFF_FILE_NAME}"

# SERVER_ACTUAL_DATA_DIR_NAME берется из common_defs.h, обычно "server_databases"
# Предположим, что common_defs.h содержит: const std::string DEFAULT_SERVER_DATA_SUBDIR = "server_databases";
# Если нет, замените на актуальное значение или передавайте через параметр серверу.
# Для данного скрипта, мы будем передавать -d "${SERVER_DATA_ROOT_BASE_ABS_PATH}",
# а сервер сам создаст поддиректорию "server_databases" внутри нее, если так реализовано.
SERVER_ACTUAL_DATA_DIR_PATH="${SERVER_DATA_ROOT_BASE_ABS_PATH}/server_databases"


RESULTS_DIR="${SCRIPT_DIR}/stage5_results"
SCENARIOS_STAGE5_DIR="${SCRIPT_DIR}/scenarios_stage5" 

# --- Подготовка ---
check_executable "${SERVER_EXEC_ABS_PATH}" "Сервер"
check_executable "${CLIENT_EXEC_ABS_PATH}" "Клиент"

print_header "Начало Тестирования Этапа 5: Многоклиентский Режим"

print_info "Очистка предыдущих результатов и данных..."
rm -rf "${SERVER_DATA_ROOT_BASE_ABS_PATH}"
rm -rf "${RESULTS_DIR}"
rm -f "${SCRIPT_DIR}/${CLIENT_BASE_LOG_NAME}"_*.log # Удаляем старые клиентские логи
rm -f "${SERVER_LOG_FILE_ABS_PATH}" # Удаляем старый серверный лог

mkdir -p "${SERVER_DATA_ROOT_BASE_ABS_PATH}" 
# Сервер сам создаст поддиректорию server_databases внутри SERVER_DATA_ROOT_BASE_ABS_PATH, если настроен так
mkdir -p "${RESULTS_DIR}"
mkdir -p "${SCENARIOS_STAGE5_DIR}/client1"
mkdir -p "${SCENARIOS_STAGE5_DIR}/client2"
mkdir -p "${SCENARIOS_STAGE5_DIR}/client3"


# Создание простого общего файла тарифов для теста
cat << EOF > "${SERVER_TARIFF_FILE_ABS_PATH}"
# Tariff for Stage 5 tests
0.10 0.10 0.10 0.10 0.10 0.10 0.10 0.10 0.10 0.10 0.10 0.10 0.10 0.10 0.10 0.10 0.10 0.10 0.10 0.10 0.10 0.10 0.10 0.10
0.05 0.05 0.05 0.05 0.05 0.05 0.05 0.05 0.05 0.05 0.05 0.05 0.05 0.05 0.05 0.05 0.05 0.05 0.05 0.05 0.05 0.05 0.05 0.05
EOF
print_debug "Создан файл тарифов: ${SERVER_TARIFF_FILE_ABS_PATH}"

# Создание файлов сценариев для клиентов (примеры)
cat << EOF > "${SCENARIOS_STAGE5_DIR}/client1/commands.txt"
# Scenario for Client 1
DELAY_MS 100
ADD FIO "ClientOne Alpha" IP "10.0.0.1" DATE "01.06.2025" END
DELAY_RANDOM_MS 50 150
SELECT FIO "ClientOne Alpha" END
PRINT_ALL END
SAVE "db_after_c1.txt" END
EOF
print_debug "Создан файл сценария для client1: ${SCENARIOS_STAGE5_DIR}/client1/commands.txt"

cat << EOF > "${SCENARIOS_STAGE5_DIR}/client2/commands.txt"
# Scenario for Client 2
DELAY_MS 250 
ADD FIO "ClientTwo Bravo" IP "10.0.0.2" DATE "02.06.2025" TRAFFIC_IN 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 END
DELAY_MS 100
EDIT FIO "ClientOne Alpha" SET FIO "ClientOne Alpha MODIFIED" END # Попытка изменить запись ClientOne
PRINT_ALL END
EOF
print_debug "Создан файл сценария для client2: ${SCENARIOS_STAGE5_DIR}/client2/commands.txt"

cat << EOF > "${SCENARIOS_STAGE5_DIR}/client3/commands.txt"
# Scenario for Client 3 - focuses on reading and calculations
DELAY_MS 150
PRINT_ALL END
DELAY_RANDOM_MS 200 500
CALCULATE_CHARGES START_DATE 01.01.2025 END_DATE 31.12.2025 END
DELAY_MS 100
SELECT IP "10.0.0.1" END 
EOF
print_debug "Создан файл сценария для client3: ${SCENARIOS_STAGE5_DIR}/client3/commands.txt"


# --- Запуск Сервера ---
# shellcheck disable=SC2174 # -p для mkdir не нужен, если родитель уже есть
mkdir -p "$(dirname "${SERVER_LOG_FILE_ABS_PATH}")"

# shellcheck disable=SC2174
mkdir -p "${SERVER_DATA_ROOT_BASE_ABS_PATH}"

server_pid_var="" # Переменная для PID
server_params_array=(
    "-p" "${SERVER_PORT}"
    "-t" "${SERVER_TARIFF_FILE_ABS_PATH}"
    "-d" "${SERVER_DATA_ROOT_BASE_ABS_PATH}" # Сервер должен сам создавать server_databases внутри
    "--log-file" "${SERVER_LOG_FILE_ABS_PATH}"
    "-l" "DEBUG"
)
start_server "${SERVER_EXEC_ABS_PATH}" server_pid_var "${SERVER_LOG_FILE_ABS_PATH}" "${server_params_array[@]}"
if [ $? -ne 0 ]; then
    print_error "Не удалось запустить сервер для теста Этапа 5. Выход."
    exit 1
fi
# Убедимся, что server_pid_var действительно установлен функцией start_server
if [ -z "$server_pid_var" ]; then
    print_error "Функция start_server не установила PID сервера. Выход."
    exit 1
fi


# --- Определение клиентов и их сценариев ---
declare -A client_pids_map # Используем ассоциативный массив для PID
client_scenarios_config=(
    "client1:${SCENARIOS_STAGE5_DIR}/client1/commands.txt"
    "client2:${SCENARIOS_STAGE5_DIR}/client2/commands.txt"
    "client3:${SCENARIOS_STAGE5_DIR}/client3/commands.txt"
)

# --- Запуск Клиентов ---
for client_config_entry in "${client_scenarios_config[@]}"; do
    IFS=':' read -r client_id_val scenario_file_val <<< "$client_config_entry"
    
    output_file_val="${RESULTS_DIR}/output_${client_id_val}.txt"
    client_log_abs_path="${SCRIPT_DIR}/${CLIENT_BASE_LOG_NAME}_${client_id_val}.log"
    
    # shellcheck disable=SC2174
    mkdir -p "$(dirname "${client_log_abs_path}")"
    
    current_client_params_array=(
        "-s" "${SERVER_HOST}"
        "-p" "${SERVER_PORT}"
        "-f" "${scenario_file_val}"
        "-o" "${output_file_val}"
        "--client-id" "${client_id_val}"
        "--timeout" "25000" 
        "--log-file" "${client_log_abs_path}"
        "-l" "DEBUG"
    )
    
    print_info "Запуск клиента ${client_id_val} со сценарием ${scenario_file_val}..."
    # Запускаем клиента, перенаправляя его собственный stdout/stderr в отдельный файл для отладки,
    # основной вывод команд идет в файл, указанный через -o
    "${CLIENT_EXEC_ABS_PATH}" "${current_client_params_array[@]}" > "${RESULTS_DIR}/raw_stdout_${client_id_val}.log" 2>&1 &
    client_pids_map[$client_id_val]=$!
    print_debug "Клиент ${client_id_val} запущен с PID ${client_pids_map[$client_id_val]}. Вывод команд в ${output_file_val}, лог в ${client_log_abs_path}"
done

# --- Ожидание завершения клиентов ---
all_clients_finished_successfully=true
for client_id_val_wait in "${!client_pids_map[@]}"; do
    pid_to_wait=${client_pids_map[$client_id_val_wait]}
    print_info "Ожидание завершения клиента ${client_id_val_wait} (PID: ${pid_to_wait})..."
    wait "$pid_to_wait"
    exit_code_client=$?
    if [ $exit_code_client -eq 0 ]; then
        print_success "Клиент ${client_id_val_wait} (PID: ${pid_to_wait}) успешно завершил работу."
    else
        print_error "Клиент ${client_id_val_wait} (PID: ${pid_to_wait}) завершился с кодом ошибки: $exit_code_client."
        print_info "  См. вывод команд клиента в: ${RESULTS_DIR}/output_${client_id_val_wait}.txt"
        print_info "  См. прямой stdout/stderr клиента в: ${RESULTS_DIR}/raw_stdout_${client_id_val_wait}.log"
        print_info "  См. лог клиента в: ${SCRIPT_DIR}/${CLIENT_BASE_LOG_NAME}_${client_id_val_wait}.log"
        all_clients_finished_successfully=false
    fi
done

# --- Остановка Сервера ---
stop_server "$server_pid_var" "${SERVER_LOG_FILE_ABS_PATH}" # Используем server_pid_var

# --- Анализ Результатов (Пример) ---
print_header "Анализ Результатов Тестирования Этапа 5"
if $all_clients_finished_successfully; then
    print_success "Все клиенты завершили работу без ошибок."
else
    print_warning "Некоторые клиенты завершились с ошибками."
fi

print_info "Выходные файлы клиентов (с ответами сервера и временными метками клиента) находятся в директории: ${RESULTS_DIR}"
print_info "Лог сервера: ${SERVER_LOG_FILE_ABS_PATH}"
print_info "Файлы базы данных сервера (если были созданы/сохранены) находятся в: ${SERVER_ACTUAL_DATA_DIR_PATH}"

# Пример проверки файла, сохраненного client1
expected_db_file_after_c1="${SERVER_ACTUAL_DATA_DIR_PATH}/db_after_c1.txt"
if [ -f "$expected_db_file_after_c1" ]; then
    print_info "Найден файл БД, сохраненный client1: $expected_db_file_after_c1"
    # Здесь можно добавить сравнение с эталоном, если он есть
    # Например, если мы ожидаем, что он будет содержать запись ClientOne Alpha MODIFIED (если client2 успел ее изменить до SAVE client1)
    # или ClientOne Alpha (если client1 сохранил до изменения client2)
    # Это сложный момент для автоматической проверки без точного знания порядка выполнения на сервере,
    # но можно проверить наличие определенных записей.
    if grep -q "ClientOne Alpha" "$expected_db_file_after_c1"; then
        print_success "  Файл $expected_db_file_after_c1 содержит 'ClientOne Alpha' (или 'ClientOne Alpha MODIFIED')."
    else
        print_warning "  Файл $expected_db_file_after_c1 НЕ содержит 'ClientOne Alpha' (или 'ClientOne Alpha MODIFIED')."
    fi
else
    print_warning "Файл БД, который должен был сохранить client1 ($expected_db_file_after_c1), не найден."
fi


if $all_clients_finished_successfully; then
    print_success "Тестирование Этапа 5 завершено."
    exit 0
else
    print_error "Тестирование Этапа 5 выявило проблемы."
    exit 1
fi
