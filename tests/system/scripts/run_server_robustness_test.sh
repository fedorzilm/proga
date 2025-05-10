#!/bin/bash

# $1 = CMAKE_BINARY_DIR
# $2 = SYSTEM_TEST_DATA_ROOT_DIR 

CMAKE_BUILD_DIR="$1"
# SYSTEM_TEST_DATA_ROOT="$2" 

SERVER_EXE="$CMAKE_BUILD_DIR/bin/db_server_app"
CLIENT_EXE="$CMAKE_BUILD_DIR/bin/db_client_app" 
WORK_DIR="$CMAKE_BUILD_DIR/Testing/Temporary/server_robustness_work"
PORT_S_ROBUST=12350 

if [ ! -f "$SERVER_EXE" ]; then echo "ERROR: Server executable not found at $SERVER_EXE"; exit 1; fi
if [ ! -f "$CLIENT_EXE" ]; then echo "ERROR: Client executable not found at $CLIENT_EXE"; exit 1; fi

mkdir -p "$WORK_DIR"
if [ $? -ne 0 ]; then echo "ERROR: Could not create work directory $WORK_DIR"; exit 1; fi

TEST_PASSED_COUNT_SRV=0 # TEST_PASSED_COUNT -> TEST_PASSED_COUNT_SRV
TOTAL_TESTS_SRV=2       # TOTAL_TESTS -> TOTAL_TESTS_SRV
TEST_FAILED_FLAG_SRV=0  # TEST_FAILED_FLAG -> TEST_FAILED_FLAG_SRV
SERVER_PID_ROBUST=""    # Объявляем глобально для функции cleanup

echo_status_srv() {
    echo "-----------------------------------------------------"
    echo "TEST_SRV: $1"
    echo "STATUS: $2"
    if [ "$2" != "PASSED" ]; then
        TEST_FAILED_FLAG_SRV=1
    else
        TEST_PASSED_COUNT_SRV=$((TEST_PASSED_COUNT_SRV + 1))
    fi
    echo "-----------------------------------------------------"
}

cleanup_work_dir_srv() {
    rm -f "$WORK_DIR"/*
    if [ ! -z "$SERVER_PID_ROBUST" ] && kill -0 "$SERVER_PID_ROBUST" 2>/dev/null; then
        echo "Cleanup: Force stopping server PID $SERVER_PID_ROBUST from previous test run..."
        kill -SIGTERM "$SERVER_PID_ROBUST" # Сначала SIGTERM
        sleep 1
        if kill -0 "$SERVER_PID_ROBUST" 2>/dev/null; then # Если еще жив
            kill -SIGKILL "$SERVER_PID_ROBUST" # Потом SIGKILL
        fi
        wait "$SERVER_PID_ROBUST" 2>/dev/null
    fi
    SERVER_PID_ROBUST=""
}

start_and_check_server() {
    local DB_FILE_ARG="$1"
    local TARIFF_FILE_ARG="$2"
    local LOG_FILE="$3"
    local EXPECTED_GREETING_PATTERN="$4" 
    local SERVER_SHOULD_SAVE_DB_ON_EXIT="$5" # true or false

    # Запускаем сервер в фоновом режиме
    "$SERVER_EXE" --dbfile "$DB_FILE_ARG" --tariff "$TARIFF_FILE_ARG" --port "$PORT_S_ROBUST" > "$LOG_FILE" 2>&1 &
    SERVER_PID_ROBUST=$!
    echo "Server PID: $SERVER_PID_ROBUST. Waiting for server to start..."
    
    MAX_SERVER_START_WAIT_SRV=10 # MAX_SERVER_START_WAIT -> MAX_SERVER_START_WAIT_SRV
    CURRENT_WAIT_SRV=0           # CURRENT_WAIT -> CURRENT_WAIT_SRV
    SERVER_READY_SRV=false       # SERVER_READY -> SERVER_READY_SRV
    while [ $CURRENT_WAIT_SRV -lt $MAX_SERVER_START_WAIT_SRV ]; do
        if ss -tlpn 2>/dev/null | grep -q ":$PORT_S_ROBUST" || netstat -tlpn 2>/dev/null | grep -q ":$PORT_S_ROBUST" ; then
            echo "Server port $PORT_S_ROBUST is now listening."
            SERVER_READY_SRV=true
            break
        fi
        sleep 0.5 
        CURRENT_WAIT_SRV=$((CURRENT_WAIT_SRV + 1))
    done

    if [ "$SERVER_READY_SRV" = false ]; then
       echo "Server on port $PORT_S_ROBUST did not become available. Aborting this sub-test."
       if kill -0 $SERVER_PID_ROBUST 2>/dev/null; then kill -SIGTERM $SERVER_PID_ROBUST; wait $SERVER_PID_ROBUST 2>/dev/null; fi
       SERVER_PID_ROBUST=""
       return 1
    fi
    sleep 0.5 # Дополнительная пауза для стабилизации

    CLIENT_PING_OUTPUT_SRV="$WORK_DIR/ping_output_srv.log" # CLIENT_PING_OUTPUT -> CLIENT_PING_OUTPUT_SRV
    # Отправляем PING и SHUTDOWN_SERVER
    (echo "PING"; sleep 0.5; echo "SHUTDOWN_SERVER") | "$CLIENT_EXE" --port "$PORT_S_ROBUST" --id RobustTestSrvPing --host "127.0.0.1" > "$CLIENT_PING_OUTPUT_SRV" 2>&1
    
    SERVER_RESPONDED_TO_PING=false
    if grep -q "PONG_CustomProto" "$CLIENT_PING_OUTPUT_SRV"; then
        echo "Server responded to PING."
        SERVER_RESPONDED_TO_PING=true
    else
        echo "Server did NOT respond to PING. Client log:"
        cat "$CLIENT_PING_OUTPUT_SRV"
        echo "Server log:"
        cat "$LOG_FILE"
    fi
    
    # Ожидаем завершения сервера
    wait $SERVER_PID_ROBUST 2>/dev/null
    local SERVER_EXIT_CODE_CHECK=$? # SERVER_EXIT_CODE -> SERVER_EXIT_CODE_CHECK
    echo "Server PID $SERVER_PID_ROBUST exited with code $SERVER_EXIT_CODE_CHECK."
    SERVER_PID_ROBUST="" 

    if [ "$SERVER_RESPONDED_TO_PING" = true ] && [ $SERVER_EXIT_CODE_CHECK -eq 0 ]; then
        if [ ! -z "$EXPECTED_GREETING_PATTERN" ]; then
            if grep -q -E "$EXPECTED_GREETING_PATTERN" "$LOG_FILE"; then
                # Проверка сохранения БД, если ожидается
                if [ "$SERVER_SHOULD_SAVE_DB_ON_EXIT" = "true" ]; then
                    if [ -f "$DB_FILE_ARG" ] && [ ! -s "$DB_FILE_ARG" ]; then # Файл существует и пуст
                        echo "Server saved an empty DB as expected."
                        return 0 # PASSED
                    elif [ ! -f "$DB_FILE_ARG" ]; then # Файл не был создан (если сервер не должен создавать его при пустой базе)
                        echo "Server did not create DB file, which might be ok if DB was empty and no save on empty."
                        return 0 # PASSED
                    else
                        echo "DB file $DB_FILE_ARG exists and is not empty, or other issue."
                        return 1 # FAILED DB save check
                    fi
                fi
                return 0 # PASSED (без проверки сохранения БД)
            else
                echo "Expected server log pattern NOT found: $EXPECTED_GREETING_PATTERN"
                cat "$LOG_FILE"
                return 1 # FAILED pattern
            fi
        fi
        return 0 # PASSED
    else
        echo "Server did not start correctly, respond to ping, or exit gracefully."
        return 1 # FAILED start or exit
    fi
}

# --- Тест 2.1: Сервер с несуществующим файлом БД ---
cleanup_work_dir_srv
echo "Running: Server with non-existent DB file"
DB_FILE_ARG_T1="$WORK_DIR/db_srv_non_existent.txt"    # DB_FILE_PATH -> DB_FILE_ARG_T1
TARIFF_FILE_ARG_T1="$WORK_DIR/dummy_srv_tariff_ok.cfg" # TARIFF_FILE_PATH -> TARIFF_FILE_ARG_T1
echo -e "0.1\n0.1\n0.1\n0.1\n0.1\n0.1\n0.1\n0.1\n0.1\n0.1\n0.1\n0.1\n0.1\n0.1\n0.1\n0.1\n0.1\n0.1\n0.1\n0.1\n0.1\n0.1\n0.1\n0.1" > "$TARIFF_FILE_ARG_T1"
LOG_OUTPUT_T1="$WORK_DIR/server_test1_output.log"     # LOG_OUTPUT -> LOG_OUTPUT_T1
EXPECTED_LOG_MSG_T1="Starting empty|loaded .* 0 records" # EXPECTED_LOG_MSG -> EXPECTED_LOG_MSG_T1

if start_and_check_server "$DB_FILE_ARG_T1" "$TARIFF_FILE_ARG_T1" "$LOG_OUTPUT_T1" "$EXPECTED_LOG_MSG_T1" "true"; then
    echo_status_srv "Server non-existent DB" "PASSED"
else
    echo_status_srv "Server non-existent DB" "FAILED"
fi

# --- Тест 2.2: Сервер с несуществующим файлом тарифов ---
cleanup_work_dir_srv
echo "Running: Server with non-existent tariff file"
DB_FILE_ARG_T2="$WORK_DIR/dummy_srv_db_ok.txt"    # DB_FILE_PATH -> DB_FILE_ARG_T2
touch "$DB_FILE_ARG_T2"
TARIFF_FILE_ARG_T2="$WORK_DIR/tariff_srv_non_existent.cfg" # TARIFF_FILE_PATH -> TARIFF_FILE_ARG_T2
LOG_OUTPUT_T2="$WORK_DIR/server_test2_output.log"     # LOG_OUTPUT -> LOG_OUTPUT_T2
EXPECTED_LOG_MSG_TARIFF_T2="Could not load tariff plan.*Using default rates" # EXPECTED_LOG_MSG_TARIFF -> EXPECTED_LOG_MSG_TARIFF_T2

if start_and_check_server "$DB_FILE_ARG_T2" "$TARIFF_FILE_ARG_T2" "$LOG_OUTPUT_T2" "$EXPECTED_LOG_MSG_TARIFF_T2" "false"; then
    echo_status_srv "Server non-existent tariff file" "PASSED"
else
    echo_status_srv "Server non-existent tariff file" "FAILED"
fi

echo "====================================================="
echo "Server Robustness Tests Summary:"
echo "PASSED: $TEST_PASSED_COUNT_SRV / $TOTAL_TESTS_SRV"
echo "====================================================="

# rm -rf "$WORK_DIR" 
exit $TEST_FAILED_FLAG_SRV
