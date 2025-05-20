#!/bin/bash

# Главный скрипт для запуска всех интеграционных тестов

# Путь к этому скрипту
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"
# Подключаем общие функции
source "${SCRIPT_DIR}/test_common.sh"

# --- Конфигурация ---
PROJECT_ROOT_REL_PATH=".." 
SERVER_EXEC_REL_PATH="build/bin/database_server"
CLIENT_EXEC_REL_PATH="build/bin/database_client"

SERVER_EXEC_ABS_PATH="${SCRIPT_DIR}/${PROJECT_ROOT_REL_PATH}/${SERVER_EXEC_REL_PATH}"
CLIENT_EXEC_ABS_PATH="${SCRIPT_DIR}/${PROJECT_ROOT_REL_PATH}/${CLIENT_EXEC_REL_PATH}"

SERVER_PORT="12346" 
SERVER_HOST="127.0.0.1" # Используем IP для надежности
SERVER_LOG_FILE_NAME="server_integration_test.log" 
CLIENT_LOG_FILE_NAME="client_integration_test.log" 
SERVER_DATA_ROOT_BASE_NAME="test_server_data_root" 

SERVER_LOG_FILE_ABS_PATH="${SCRIPT_DIR}/${SERVER_LOG_FILE_NAME}"
CLIENT_LOG_FILE_ABS_PATH="${SCRIPT_DIR}/${CLIENT_LOG_FILE_NAME}"
SERVER_DATA_ROOT_BASE_ABS_PATH="${SCRIPT_DIR}/${SERVER_DATA_ROOT_BASE_NAME}"

SERVER_ACTUAL_DATA_DIR_NAME="server_databases" 
SERVER_FULL_DATA_DIR_ABS_PATH="${SERVER_DATA_ROOT_BASE_ABS_PATH}/${SERVER_ACTUAL_DATA_DIR_NAME}"

SCENARIOS_DIR="${SCRIPT_DIR}/scenarios"

# Счетчик для результатов
total_scenarios=0
passed_scenarios=0

# --- Функции ---

# Функция для выполнения одного сценария
run_scenario() {
    local scenario_tag=$1 
    local scenario_name_dir="scenario${scenario_tag#s}" 
    local scenario_dir="${SCENARIOS_DIR}/${scenario_name_dir}"

    local commands_file="${scenario_dir}/commands_${scenario_tag}.txt"
    local initial_data_filename_for_load="initial_data_${scenario_tag}.txt" 
    local initial_data_source_path="${scenario_dir}/${initial_data_filename_for_load}"
    local tariff_file_abs_path="${scenario_dir}/tariff_${scenario_tag}.cfg" 
    local client_output_actual="${scenario_dir}/output_actual_${scenario_tag}.txt"
    local client_output_expected="${scenario_dir}/expected_output_${scenario_tag}.txt"
    
    local server_runtime_data_dir_for_verification="${SERVER_FULL_DATA_DIR_ABS_PATH}"
    local expected_db_files_source_dir="${scenario_dir}" 

    print_info "--- Запуск сценария: ${scenario_name_dir} (тег: ${scenario_tag}) ---"
    ((total_scenarios++))

    print_debug "Очистка ${SERVER_DATA_ROOT_BASE_ABS_PATH}..."
    rm -rf "${SERVER_DATA_ROOT_BASE_ABS_PATH}"
    mkdir -p "${SERVER_FULL_DATA_DIR_ABS_PATH}" 
    
    if [ -f "${initial_data_source_path}" ]; then
        if [ ! -s "${initial_data_source_path}" ] && [[ "${scenario_tag}" == "s2" ]]; then 
             print_warning "Файл начальных данных ${initial_data_source_path} пуст или не содержит достаточно данных для ${scenario_tag}. Чанкинг может не сработать как ожидается."
        fi
        cp "${initial_data_source_path}" "${SERVER_FULL_DATA_DIR_ABS_PATH}/"
        print_debug "Скопирован файл начальных данных: ${initial_data_source_path} в ${SERVER_FULL_DATA_DIR_ABS_PATH}/"
    else
        print_debug "Файл начальных данных ${initial_data_source_path} не найден для ${scenario_tag}, пропуск копирования."
    fi

    local server_pid
    local server_params_array=(
        "-p" "${SERVER_PORT}"
        "-t" "${tariff_file_abs_path}"
        "-d" "${SERVER_DATA_ROOT_BASE_ABS_PATH}"
        "--log-file" "${SERVER_LOG_FILE_ABS_PATH}"
        "-l" "DEBUG"
    )
    
    start_server "${SERVER_EXEC_ABS_PATH}" server_pid "${SERVER_LOG_FILE_ABS_PATH}" "${server_params_array[@]}"
    if [ $? -ne 0 ]; then
        print_error "Не удалось запустить сервер для сценария ${scenario_name_dir}."
        return 1
    fi

    local client_params_array=(
        "-s" "${SERVER_HOST}" 
        "-p" "${SERVER_PORT}"
        "-f" "${commands_file}" # Путь к файлу команд
        "-o" "${client_output_actual}"
        "--timeout" "20000"
        "--log-file" "${CLIENT_LOG_FILE_ABS_PATH}"
        "-l" "DEBUG"
    )
    run_client "${CLIENT_EXEC_ABS_PATH}" "${CLIENT_LOG_FILE_ABS_PATH}" "${client_params_array[@]}"
    local client_exit_code=$?

    stop_server "$server_pid" "${SERVER_LOG_FILE_ABS_PATH}" 
    
    local scenario_passed=true

    if [ $client_exit_code -ne 0 ]; then
        print_error "Клиент завершился с кодом ошибки: ${client_exit_code} для сценария ${scenario_name_dir}."
        scenario_passed=false
    fi

    if [ -f "${client_output_expected}" ]; then
        compare_files_with_placeholder_replacement \
            "${client_output_actual}" \
            "${client_output_expected}" \
            "[ACTUAL_SERVER_PATH_TO_DB_DIR]" \
            "${SERVER_FULL_DATA_DIR_ABS_PATH}" \
            "[CLIENT_COMMAND_FILE_PATH_PLACEHOLDER]" \
            "${commands_file}" # Передаем фактический путь к файлу команд для замены плейсхолдера
        
        if [ $? -ne 0 ]; then
            print_error "Вывод клиента не соответствует ожидаемому для сценария ${scenario_name_dir}."
            print_info "  Фактический вывод: ${client_output_actual}"
            print_info "  Ожидаемый вывод (шаблон): ${client_output_expected}"
            scenario_passed=false
        else
            print_success "Вывод клиента соответствует ожидаемому для сценария ${scenario_name_dir}."
        fi
    else
        print_warning "Файл с ожидаемым выводом ${client_output_expected} не найден. Проверка вывода клиента пропущена."
    fi

    # ... (проверка файлов БД остается такой же) ...
    if [[ "${scenario_tag}" == "s1" ]]; then
        local expected_db_file="${expected_db_files_source_dir}/expected_db_after_s1.txt"
        local actual_db_file="${server_runtime_data_dir_for_verification}/final_db_s1.txt"
        if [ -f "${expected_db_file}" ]; then
            if [ -f "${actual_db_file}" ]; then
                compare_files "${actual_db_file}" "${expected_db_file}"
                if [ $? -ne 0 ]; then print_error "Сохраненный файл БД (final_db_s1.txt) не соответствует ожидаемому."; scenario_passed=false; else print_success "Сохраненный файл БД (final_db_s1.txt) соответствует ожидаемому."; fi
            else print_error "Ожидаемый сохраненный файл БД ${actual_db_file} не найден."; scenario_passed=false; fi
        else print_warning "Ожидаемый файл БД ${expected_db_file} для сценария ${scenario_tag} не найден. Проверка пропущена."; fi
    elif [[ "${scenario_tag}" == "s5" ]]; then
        local actual_db_A_final="${server_runtime_data_dir_for_verification}/dataset_A_s5.txt"
        local expected_db_A_final="${expected_db_files_source_dir}/expected_dataset_A_after_save_s5.txt"
        local actual_db_B="${server_runtime_data_dir_for_verification}/dataset_B_s5.txt"
        local expected_db_B="${expected_db_files_source_dir}/expected_dataset_B_after_save_s5.txt"
        
        if [ -f "${expected_db_A_final}" ]; then
             if [ -f "${actual_db_A_final}" ]; then compare_files "${actual_db_A_final}" "${expected_db_A_final}"; if [ $? -ne 0 ]; then print_error "Сохраненный файл БД (dataset_A_s5.txt final) не соответствует ожидаемому."; scenario_passed=false; else print_success "Сохраненный файл БД (dataset_A_s5.txt final) соответствует ожидаемому."; fi
            else print_error "Ожидаемый сохраненный файл БД ${actual_db_A_final} не найден."; scenario_passed=false; fi
        else print_warning "Ожидаемый файл БД ${expected_db_A_final} для сценария ${scenario_tag} не найден. Проверка пропущена."; fi

        if [ -f "${expected_db_B}" ]; then
            if [ -f "${actual_db_B}" ]; then compare_files "${actual_db_B}" "${expected_db_B}"; if [ $? -ne 0 ]; then print_error "Сохраненный файл БД (dataset_B_s5.txt) не соответствует ожидаемому."; scenario_passed=false; else print_success "Сохраненный файл БД (dataset_B_s5.txt) соответствует ожидаемому."; fi
            else print_error "Ожидаемый сохраненный файл БД ${actual_db_B} не найден."; scenario_passed=false; fi
        else print_warning "Ожидаемый файл БД ${expected_db_B} для сценария ${scenario_tag} не найден. Проверка пропущена."; fi
    fi

    if $scenario_passed; then
        print_success "--- Сценарий ${scenario_name_dir} ПРОЙДЕН ---"
        ((passed_scenarios++))
    else
        print_error "--- Сценарий ${scenario_name_dir} ПРОВАЛЕН ---"
    fi
    echo "" 
    return 0
}

# --- Основная логика ---
# ... (остается такой же) ...
check_executable "${SERVER_EXEC_ABS_PATH}" "Сервер"
check_executable "${CLIENT_EXEC_ABS_PATH}" "Клиент"

print_header "Начало Интеграционного Тестирования"

SCENARIO_TAG_LIST=("s1" "s2" "s3" "s4" "s5")

for tag in "${SCENARIO_TAG_LIST[@]}"; do
    scenario_dir_check="${SCENARIOS_DIR}/scenario${tag#s}" 
    if [ -d "${scenario_dir_check}" ]; then
        run_scenario "${tag}"
    else
        print_warning "Директория сценария ${scenario_dir_check} для тега ${tag} не найдена. Пропуск."
    fi
done

print_header "Результаты Интеграционного Тестирования"
print_info "Всего сценариев: ${total_scenarios}"
print_info "Пройдено успешно: ${passed_scenarios}"

if [ "${passed_scenarios}" -eq "${total_scenarios}" ] && [ "${total_scenarios}" -gt 0 ]; then
    print_success "Все интеграционные тесты пройдены успешно!"
    exit 0
else
    print_error "Некоторые интеграционные тесты провалены."
    exit 1
fi
