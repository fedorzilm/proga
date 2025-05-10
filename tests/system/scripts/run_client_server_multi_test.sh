#!/bin/bash

# Аргументы от CMake:
# $1 = CMAKE_BINARY_DIR
# $2 = SYSTEM_TEST_DATA_ROOT_DIR (путь к tests/system/)
# $3 = SCENARIO_PREFIX (A, B, C, D, E, F ...)

CMAKE_BUILD_DIR="$1"
SYSTEM_TEST_DATA_ROOT="$2"
SCENARIO_PREFIX_ARG="$3"

if [ -z "$SCENARIO_PREFIX_ARG" ]; then
    echo "ERROR: SCENARIO_PREFIX argument (3rd argument) is missing!"
    exit 1
fi

SERVER_EXE="$CMAKE_BUILD_DIR/bin/db_server_app"
CLIENT_EXE="$CMAKE_BUILD_DIR/bin/db_client_app"
TEST_DATA_DIR="$SYSTEM_TEST_DATA_ROOT/test_data_cs_multi" 
# Используем уникальное имя рабочей директории для каждого сценария
WORK_DIR_NAME="test_work_dir_cs_multi_scenario_${SCENARIO_PREFIX_ARG}" 
WORK_DIR="$CMAKE_BUILD_DIR/Testing/Temporary/$WORK_DIR_NAME"
# Используем разные порты для разных сценариев, если они могут запускаться параллельно,
# или один и тот же, если CTest запускает их последовательно. Для простоты, пока один.
PORT=12347 

TIMESTAMP_REGEX="[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}"
TIMESTAMP_PLACEHOLDER="TIMESTAMP_PLACEHOLDER"

if [ ! -f "$SERVER_EXE" ]; then echo "ERROR: Server executable not found at $SERVER_EXE"; exit 1; fi
if [ ! -f "$CLIENT_EXE" ]; then echo "ERROR: Client executable not found at $CLIENT_EXE"; exit 1; fi

mkdir -p "$WORK_DIR"
if [ $? -ne 0 ]; then echo "ERROR: Could not create work directory $WORK_DIR"; exit 1; fi
rm -f "$WORK_DIR"/*

# Проверка и копирование файлов данных для указанного сценария
DB_INITIAL_FILE="$TEST_DATA_DIR/server_db_initial.txt" # Общий исходный файл БД для multi-тестов
DB_EXPECTED_FINAL_FILE="$TEST_DATA_DIR/server_db_expected_final_multi_${SCENARIO_PREFIX_ARG}.txt"
TARIFF_CFG_FILE="$TEST_DATA_DIR/server_tariff.cfg" # Основной тариф

if [ ! -f "$DB_INITIAL_FILE" ]; then echo "ERROR: File $DB_INITIAL_FILE not found"; exit 1; fi
if [ ! -f "$TARIFF_CFG_FILE" ]; then echo "ERROR: File $TARIFF_CFG_FILE not found"; exit 1; fi
if [ ! -f "$DB_EXPECTED_FINAL_FILE" ]; then echo "ERROR: File $DB_EXPECTED_FINAL_FILE not found"; exit 1; fi

NUM_CLIENTS_IN_SCENARIO=3 # Большинство наших сценариев используют 3 клиента
for i in $(seq 1 $NUM_CLIENTS_IN_SCENARIO); do
    CLIENT_SCENARIO_FILE="$TEST_DATA_DIR/client_scenario_multi_${SCENARIO_PREFIX_ARG}${i}.txt"
    CLIENT_EXPECTED_OUTPUT_FILE="$TEST_DATA_DIR/client_scenario_multi_${SCENARIO_PREFIX_ARG}${i}_expected_output.txt"
    if [ ! -f "$CLIENT_SCENARIO_FILE" ]; then echo "ERROR: File $CLIENT_SCENARIO_FILE not found"; exit 1; fi
    if [ ! -f "$CLIENT_EXPECTED_OUTPUT_FILE" ]; then echo "ERROR: File $CLIENT_EXPECTED_OUTPUT_FILE not found"; exit 1; fi
done

cp "$DB_INITIAL_FILE" "$WORK_DIR/server_db.txt"
cp "$TARIFF_CFG_FILE" "$WORK_DIR/server_tariff.cfg"

# Копирование специфичных для сценария файлов, например, альтернативных тарифов
if [ "$SCENARIO_PREFIX_ARG" == "C" ] || [ "$SCENARIO_PREFIX_ARG" == "F" ]; then
    ALT_TARIFF_CFG_NAME="tariff_alt_concurrent.cfg"
    if [ ! -f "$TEST_DATA_DIR/$ALT_TARIFF_CFG_NAME" ]; then 
        echo "ERROR: File $TEST_DATA_DIR/$ALT_TARIFF_CFG_NAME not found for Scenario $SCENARIO_PREFIX_ARG"; exit 1; 
    fi
    cp "$TEST_DATA_DIR/$ALT_TARIFF_CFG_NAME" "$WORK_DIR/$ALT_TARIFF_CFG_NAME"
fi

echo "Starting server for multi-client test (Scenario ${SCENARIO_PREFIX_ARG}) on port $PORT..."
"$SERVER_EXE" --dbfile "$WORK_DIR/server_db.txt" --tariff "$WORK_DIR/server_tariff.cfg" --port "$PORT" &
SERVER_PID=$!
echo "Server PID: $SERVER_PID. Waiting for server to start..."

# Даем серверу время запуститься и открыть порт
MAX_SERVER_START_WAIT_MULTI=10
CURRENT_WAIT_MULTI=0
SERVER_READY_MULTI=false
while [ $CURRENT_WAIT_MULTI -lt $MAX_SERVER_START_WAIT_MULTI ]; do
    if ss -tlpn 2>/dev/null | grep -q ":$PORT" || netstat -tlpn 2>/dev/null | grep -q ":$PORT" ; then
        echo "Server port $PORT is now listening."
        SERVER_READY_MULTI=true
        break
    fi
    sleep 0.5 
    CURRENT_WAIT_MULTI=$((CURRENT_WAIT_MULTI + 1))
done

if [ "$SERVER_READY_MULTI" = false ]; then
    echo "ERROR: Server did not start listening on port $PORT within $MAX_SERVER_START_WAIT_MULTI seconds for Scenario ${SCENARIO_PREFIX_ARG}."
    if kill -0 $SERVER_PID 2>/dev/null; then kill -SIGTERM $SERVER_PID; wait $SERVER_PID 2>/dev/null; fi
    exit 1
fi
sleep 1 

CLIENT_PIDS=()
echo "Starting clients for multi-client test (Scenario ${SCENARIO_PREFIX_ARG})..."
for i in $(seq 1 $NUM_CLIENTS_IN_SCENARIO); do
    CLIENT_ID="C${SCENARIO_PREFIX_ARG}${i}" 
    CLIENT_SCENARIO_FILE="$TEST_DATA_DIR/client_scenario_multi_${SCENARIO_PREFIX_ARG}${i}.txt"
    CLIENT_ACTUAL_OUTPUT_FILE="$WORK_DIR/client_output_multi_${SCENARIO_PREFIX_ARG}${i}.txt"
    "$CLIENT_EXE" --input "$CLIENT_SCENARIO_FILE" \
                  --output "$CLIENT_ACTUAL_OUTPUT_FILE" \
                  --port "$PORT" --id "$CLIENT_ID" --host "127.0.0.1" &
    CLIENT_PIDS+=($!)
done

echo "Waiting for clients (PIDs: ${CLIENT_PIDS[*]}) to finish..."
for pid_c_multi in "${CLIENT_PIDS[@]}"; do # pid_c -> pid_c_multi
    wait "$pid_c_multi"
done
echo "All clients finished for Scenario ${SCENARIO_PREFIX_ARG}."

echo "Sending SHUTDOWN to server from a dedicated client for Scenario ${SCENARIO_PREFIX_ARG}..."
"$CLIENT_EXE" --port "$PORT" --id "C_Shutdown_${SCENARIO_PREFIX_ARG}" --host "127.0.0.1" --input <(echo "SHUTDOWN_SERVER") --output "$WORK_DIR/shutdown_output_${SCENARIO_PREFIX_ARG}.txt"

echo "Waiting for server (PID: $SERVER_PID) to shut down after command..."
shutdown_timeout_multi=15 # shutdown_timeout -> shutdown_timeout_multi
elapsed_time_multi=0    # elapsed_time -> elapsed_time_multi
SERVER_STOPPED_GRACEFULLY_MULTI=false # SERVER_STOPPED_GRACEFULLY -> SERVER_STOPPED_GRACEFULLY_MULTI
while [ $elapsed_time_multi -lt $shutdown_timeout_multi ]; do
    if ! kill -0 $SERVER_PID 2>/dev/null; then
        SERVER_STOPPED_GRACEFULLY_MULTI=true
        echo "Server (PID: $SERVER_PID) shut down."
        break
    fi
    sleep 1
    elapsed_time_multi=$((elapsed_time_multi + 1))
done

if [ "$SERVER_STOPPED_GRACEFULLY_MULTI" = false ]; then
   echo "Server (PID: $SERVER_PID) did not shut down as expected for Scenario ${SCENARIO_PREFIX_ARG}! Forcing kill."
   kill -SIGTERM $SERVER_PID; sleep 1; kill -SIGKILL $SERVER_PID 2>/dev/null
else
   echo "Server shut down procedure completed for Scenario ${SCENARIO_PREFIX_ARG}."
fi
wait $SERVER_PID 2>/dev/null


ALL_CLIENTS_OK_MULTI=true # ALL_CLIENTS_OK -> ALL_CLIENTS_OK_MULTI
for i in $(seq 1 $NUM_CLIENTS_IN_SCENARIO); do
    echo "Comparing client ${SCENARIO_PREFIX_ARG}${i} output..."
    ACTUAL_OUTPUT_FILE_MULTI="$WORK_DIR/client_output_multi_${SCENARIO_PREFIX_ARG}${i}.txt" # ACTUAL_OUTPUT_FILE -> ACTUAL_OUTPUT_FILE_MULTI
    EXPECTED_OUTPUT_FILE_MULTI="$TEST_DATA_DIR/client_scenario_multi_${SCENARIO_PREFIX_ARG}${i}_expected_output.txt" # EXPECTED_OUTPUT_FILE -> EXPECTED_OUTPUT_FILE_MULTI
    
    PROCESSED_ACTUAL_OUTPUT_MULTI="$WORK_DIR/client_output_multi_${SCENARIO_PREFIX_ARG}${i}.processed" # PROCESSED_ACTUAL_OUTPUT_FILE -> PROCESSED_ACTUAL_OUTPUT_MULTI
    PROCESSED_EXPECTED_OUTPUT_MULTI="$WORK_DIR/client_expected_multi_${SCENARIO_PREFIX_ARG}${i}.processed" # PROCESSED_EXPECTED_OUTPUT_FILE -> PROCESSED_EXPECTED_OUTPUT_MULTI

    if [ ! -f "$ACTUAL_OUTPUT_FILE_MULTI" ]; then
        echo "ERROR: Client ${SCENARIO_PREFIX_ARG}${i} actual output file $ACTUAL_OUTPUT_FILE_MULTI was not created."
        ALL_CLIENTS_OK_MULTI=false
        continue
    fi

    sed -E "s/$TIMESTAMP_REGEX/$TIMESTAMP_PLACEHOLDER/g" "$EXPECTED_OUTPUT_FILE_MULTI" > "$PROCESSED_EXPECTED_OUTPUT_MULTI"
    sed -E "s/$TIMESTAMP_REGEX/$TIMESTAMP_PLACEHOLDER/g" "$ACTUAL_OUTPUT_FILE_MULTI" > "$PROCESSED_ACTUAL_OUTPUT_MULTI"

    if diff -u --strip-trailing-cr "$PROCESSED_EXPECTED_OUTPUT_MULTI" "$PROCESSED_ACTUAL_OUTPUT_MULTI"; then
        echo "Client ${SCENARIO_PREFIX_ARG}${i} output matches expected. OK."
    else
        echo "Client ${SCENARIO_PREFIX_ARG}${i} output MISMATCH!"
        echo "--- DIFF PROCESSED CLIENT ${SCENARIO_PREFIX_ARG}${i} OUTPUT: ---"
        diff -u --strip-trailing-cr "$PROCESSED_EXPECTED_OUTPUT_MULTI" "$PROCESSED_ACTUAL_OUTPUT_MULTI"
        echo "--- END DIFF ---"
        ALL_CLIENTS_OK_MULTI=false
    fi
done

if [ ! -f "$WORK_DIR/server_db.txt" ]; then
    echo "ERROR: Server DB file $WORK_DIR/server_db.txt was not found/updated for Scenario ${SCENARIO_PREFIX_ARG}."
    # exit 1 # Решите, является ли это фатальной ошибкой
fi

echo "Comparing final server DB file for multi-client test (Scenario ${SCENARIO_PREFIX_ARG})..."
if diff -u --strip-trailing-cr "$DB_EXPECTED_FINAL_FILE" "$WORK_DIR/server_db.txt"; then
    echo "Final server DB file matches expected. OK."
else
    echo "Final server DB file MISMATCH for Scenario ${SCENARIO_PREFIX_ARG}!"
    echo "--- DIFF DB FILES (Scenario ${SCENARIO_PREFIX_ARG}): ---"
    diff -u --strip-trailing-cr "$DB_EXPECTED_FINAL_FILE" "$WORK_DIR/server_db.txt"
    echo "--- END DIFF ---"
    exit 1 
fi

if $ALL_CLIENTS_OK_MULTI; then
    echo "Client-Server multi-client test (Scenario ${SCENARIO_PREFIX_ARG}) PASSED."
else
    echo "Client-Server multi-client test (Scenario ${SCENARIO_PREFIX_ARG}) FAILED due to client output mismatch(es)."
    exit 1
fi

# rm -rf "$WORK_DIR" 
exit 0
