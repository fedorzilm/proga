#!/bin/bash

# Общие функции и переменные для скриптов интеграционных тестов

# Цвета для вывода
C_RESET='\033[0m'
C_RED='\033[0;31m'
C_GREEN='\033[0;32m'
C_YELLOW='\033[0;33m'
C_BLUE='\033[0;34m'
C_MAGENTA='\033[0;35m'

# Функции для вывода сообщений
print_error() {
    echo -e "${C_RED}[ОШИБКА] $1${C_RESET}"
}

print_success() {
    echo -e "${C_GREEN}[УСПЕХ] $1${C_RESET}"
}

print_warning() {
    echo -e "${C_YELLOW}[ПРЕДУПРЕЖДЕНИЕ] $1${C_RESET}"
}

print_info() {
    echo -e "${C_BLUE}[ИНФО] $1${C_RESET}"
}

print_debug() {
    if [[ "${DEBUG_MODE}" == "true" || -n "$BASH_INTEGRATION_DEBUG" ]]; then
        echo -e "${C_MAGENTA}[ОТЛАДКА] $1${C_RESET}"
    fi
}

print_header() {
    echo -e "\n${C_BLUE}===============================================================${C_RESET}"
    echo -e "${C_BLUE} $1"
    echo -e "${C_BLUE}===============================================================${C_RESET}\n"
}

check_executable() {
    local exec_path=$1
    local exec_name=$2
    if [ ! -f "${exec_path}" ]; then
        print_error "${exec_name} не найден по пути: ${exec_path}"
        exit 1
    fi
    if [ ! -x "${exec_path}" ]; then
        print_error "${exec_name} по пути ${exec_path} не является исполняемым."
        exit 1
    fi
    print_debug "${exec_name} найден и является исполняемым: ${exec_path}"
}

# Запуск сервера в фоновом режиме
# $1 - путь к исполняемому файлу сервера
# $2 - имя переменной, в которую будет записан PID сервера
# $3 - путь к лог-файлу для nohup/сервера
# $@[4..] - параметры командной строки для сервера (как отдельные элементы массива)
start_server() {
    local server_exec=$1
    local -n pid_var=$2 
    local server_log_for_this_run=$3 
    shift 3 
    local server_params_cmd_line=("$@") 

    local cmd_display_str="\"${server_exec}\""
    for param in "${server_params_cmd_line[@]}"; do
        if [[ "$param" == *" "* ]]; then
            cmd_display_str+=" \"$param\""
        else
            cmd_display_str+=" $param"
        fi
    done
    print_info "Запуск сервера: ${cmd_display_str}"
    
    "${server_exec}" "${server_params_cmd_line[@]}" > "${server_log_for_this_run}" 2>&1 &
    pid_var=$!
    
    sleep 2 

    if ps -p "$pid_var" > /dev/null; then
        print_success "Сервер запущен успешно. PID: ${pid_var}. Лог: ${server_log_for_this_run}"
        return 0
    else
        print_error "Не удалось запустить сервер. Проверьте лог: ${server_log_for_this_run}"
        if [ -f "${server_log_for_this_run}" ]; then
            echo "--- Начало лога сервера (${server_log_for_this_run}) ---"
            tail -n 20 "${server_log_for_this_run}" 
            echo "--- Конец лога сервера (${server_log_for_this_run}) ---"
        fi
        return 1
    fi
}

stop_server() {
    local server_pid=$1
    local server_log_for_this_run=$2 
    print_info "Остановка сервера с PID: ${server_pid}..."
    if ps -p "$server_pid" > /dev/null; then
        kill -SIGTERM "$server_pid"
        local countdown=10
        while [ $countdown -gt 0 ] && ps -p "$server_pid" > /dev/null; do
            sleep 1
            print_debug "Ожидание остановки сервера... осталось ${countdown}с"
            ((countdown--))
        done

        if ps -p "$server_pid" > /dev/null; then
            print_warning "Сервер PID ${server_pid} не завершился по SIGTERM. Попытка SIGKILL. Лог сервера: ${server_log_for_this_run}"
            kill -SIGKILL "$server_pid"
            sleep 1
            if ps -p "$server_pid" > /dev/null; then
                 print_error "Не удалось остановить сервер PID ${server_pid} даже с SIGKILL. Лог: ${server_log_for_this_run}"
                 return 1
            fi
        fi
        print_success "Сервер PID ${server_pid} остановлен."
        return 0
    else
        print_warning "Сервер с PID ${server_pid} не найден (возможно, уже остановлен или не был запущен)."
        return 0 
    fi
}

# Запуск клиента
# $1 - путь к исполняемому файлу клиента
# $2 - путь к лог-файлу для клиента
# $@[3..] - параметры командной строки для клиента (как отдельные элементы массива)
run_client() {
    local client_exec=$1
    local client_log_for_this_run=$2
    shift 2
    local client_params_cmd_line=("$@")

    local cmd_display_str="\"${client_exec}\""
     for param in "${client_params_cmd_line[@]}"; do
        if [[ "$param" == *" "* ]]; then
            cmd_display_str+=" \"$param\""
        else
            cmd_display_str+=" $param"
        fi
    done   
    print_info "Запуск клиента: ${cmd_display_str}"
    
    "${client_exec}" "${client_params_cmd_line[@]}" >> "${client_log_for_this_run}" 2>&1 
    local exit_code=$?
    if [ $exit_code -ne 0 ]; then
        print_warning "Клиент завершился с кодом ошибки: $exit_code. Лог клиента: ${client_log_for_this_run}"
         if [ -f "${client_log_for_this_run}" ]; then 
            echo "--- Начало лога клиента (${client_log_for_this_run}) ---"
            tail -n 20 "${client_log_for_this_run}"
            echo "--- Конец лога клиента (${client_log_for_this_run}) ---"
        fi
    else
        print_success "Клиент успешно завершил работу." 
    fi
    return $exit_code
}

compare_files() {
    local actual_file=$1
    local expected_file=$2
    print_debug "Сравнение файлов: '${actual_file}' и '${expected_file}'"

    if [ ! -f "${actual_file}" ]; then print_error "Файл с фактическим результатом '${actual_file}' не найден."; return 1; fi
    if [ ! -f "${expected_file}" ]; then print_error "Файл с ожидаемым результатом '${expected_file}' не найден."; return 1; fi

    local temp_actual_lf=$(mktemp)
    local temp_expected_lf=$(mktemp)
    trap 'rm -f "${temp_actual_lf}" "${temp_expected_lf}" "${temp_actual_trimmed-}" "${temp_expected_trimmed-}"; trap - EXIT' EXIT HUP INT QUIT TERM 

    tr -d '\r' < "${actual_file}" > "${temp_actual_lf}"
    tr -d '\r' < "${expected_file}" > "${temp_expected_lf}"
    
    local temp_actual_trimmed=$(mktemp)
    local temp_expected_trimmed=$(mktemp)
    awk 'NF {p=1} p {if(p) print $0}' "${temp_actual_lf}" > "${temp_actual_trimmed}"
    awk 'NF {p=1} p {if(p) print $0}' "${temp_expected_lf}" > "${temp_expected_trimmed}"

    diff_output=$(diff -u -b -B --strip-trailing-cr "${temp_actual_trimmed}" "${temp_expected_trimmed}")
    local diff_rc=$?
    
    if [ $diff_rc -ne 0 ]; then
        return 1
    fi
    return 0
}

# Сравнение файлов с заменой плейсхолдеров в ожидаемом файле
# $1 - фактический файл (output_actual_sX.txt)
# $2 - ожидаемый файл (шаблон, expected_output_sX.txt)
# $3 - плейсхолдер для пути к БД сервера (e.g., "[ACTUAL_SERVER_PATH_TO_DB_DIR]")
# $4 - фактическое значение для пути к БД сервера
# $5 - плейсхолдер для пути к файлу команд клиента (e.g., "[CLIENT_COMMAND_FILE_PATH_PLACEHOLDER]")
# $6 - фактическое значение для пути к файлу команд клиента
compare_files_with_placeholder_replacement() {
    local actual_file=$1
    local expected_template_file=$2
    local server_db_path_placeholder=$3
    local server_db_path_replacement=$4
    local client_cmd_file_path_placeholder=$5
    local client_cmd_file_path_replacement=$6

    print_debug "Сравнение файлов с заменой: '${actual_file}' и шаблон '${expected_template_file}'"
    print_debug "  Плейсхолдер БД сервера: '${server_db_path_placeholder}' -> '${server_db_path_replacement}'"
    print_debug "  Плейсхолдер файла команд клиента: '${client_cmd_file_path_placeholder}' -> '${client_cmd_file_path_replacement}'"

    if [ ! -f "${actual_file}" ]; then print_error "Файл с фактическим результатом '${actual_file}' не найден."; return 1; fi
    if [ ! -f "${expected_template_file}" ]; then print_error "Файл-шаблон с ожидаемым результатом '${expected_template_file}' не найден."; return 1; fi

    local temp_actual_processed=$(mktemp)
    local temp_expected_processed=$(mktemp)
    trap 'rm -f "${temp_actual_processed}" "${temp_expected_processed}" "${temp_actual_trimmed-}" "${temp_expected_trimmed-}"; trap - EXIT' EXIT HUP INT QUIT TERM
    
    tr -d '\r' < "${actual_file}" > "${temp_actual_processed}"
    
    local escaped_server_db_placeholder=$(printf '%s\n' "$server_db_path_placeholder" | sed 's:[][\\/.^$*]:\\&:g')
    local escaped_server_db_replacement=$(printf '%s\n' "$server_db_path_replacement" | sed 's:[&/\\]:\\&:g')
    
    local escaped_client_cmd_placeholder=$(printf '%s\n' "$client_cmd_file_path_placeholder" | sed 's:[][\\/.^$*]:\\&:g')
    local escaped_client_cmd_replacement=$(printf '%s\n' "$client_cmd_file_path_replacement" | sed 's:[&/\\]:\\&:g')

    sed \
        -e "s/${escaped_server_db_placeholder}/${escaped_server_db_replacement}/g" \
        -e "s/${escaped_client_cmd_placeholder}/${escaped_client_cmd_replacement}/g" \
        "${expected_template_file}" | tr -d '\r' > "${temp_expected_processed}"

    local temp_actual_trimmed=$(mktemp)
    local temp_expected_trimmed=$(mktemp)
    awk 'NF {p=1} p {if(p) print $0}' "${temp_actual_processed}" > "${temp_actual_trimmed}"
    awk 'NF {p=1} p {if(p) print $0}' "${temp_expected_processed}" > "${temp_expected_trimmed}"

    diff_output=$(diff -u -b -B --strip-trailing-cr "${temp_actual_trimmed}" "${temp_expected_trimmed}")
    local diff_rc=$?
    
    if [ $diff_rc -ne 0 ]; then
        return 1
    fi
    return 0
}
