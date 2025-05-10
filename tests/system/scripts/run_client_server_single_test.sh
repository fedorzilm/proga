#!/bin/bash

# Аргументы от CMake:
# $1 = CMAKE_BINARY_DIR
# $2 = SYSTEM_TEST_DATA_ROOT_DIR (путь к tests/system/)

CMAKE_BUILD_DIR="$1"
SYSTEM_TEST_DATA_ROOT="$2"

SERVER_EXE="$CMAKE_BUILD_DIR/bin/db_server_app"
CLIENT_EXE="$CMAKE_BUILD_DIR/bin/db_client_app"
TEST_DATA_DIR="$SYSTEM_TEST_DATA_ROOT/test_data_cs_single"
# Используем уникальное имя рабочей директории
WORK_DIR_NAME="test_work_dir_cs_single_run" 
WORK_DIR="$CMAKE_BUILD_DIR/Testing/Temporary/$WORK_DIR_NAME"
PORT=12346 # Убедитесь, что порт уникален для параллельных тестов, если они есть

TIMESTAMP_REGEX="[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}"
TIMESTAMP_PLACEHOLDER="TIMESTAMP_PLACEHOLDER"

if [ ! -f "$SERVER_EXE" ]; then echo "ERROR: Server executable not found at $SERVER_EXE"; exit 1; fi
if [ ! -f "$CLIENT_EXE" ]; then echo "ERROR: Client executable not found at $CLIENT_EXE"; exit 1; fi

mkdir -p "$WORK_DIR"
if [ $? -ne 0 ]; then echo "ERROR: Could not create work directory $WORK_DIR"; exit 1; fi
rm -f "$WORK_DIR"/* # Очищаем от предыдущих запусков

# Проверка существования файлов данных
DB_INITIAL_FILE="$TEST_DATA_DIR/server_db_initial.txt"
TARIFF_CFG_FILE="$TEST_DATA_DIR/server_tariff.cfg"
ALT_TARIFF_CFG_FILE="$TEST_DATA_DIR/tariff_alt.cfg" # Альтернативный тариф для LOAD_TARIFF
CLIENT_SCENARIO_FILE="$TEST_DATA_DIR/client_scenario_single.txt"
CLIENT_EXPECTED_OUTPUT_FILE="$TEST_DATA_DIR/client_scenario_single_expected_output.txt"
DB_EXPECTED_FINAL_FILE="$TEST_DATA_DIR/server_db_expected_final_single.txt"

if [ ! -f "$DB_INITIAL_FILE" ]; then echo "ERROR: File $DB_INITIAL_FILE not found"; exit 1; fi
if [ ! -f "$TARIFF_CFG_FILE" ]; then echo "ERROR: File $TARIFF_CFG_FILE not found"; exit 1; fi
if [ ! -f "$ALT_TARIFF_CFG_FILE" ]; then echo "ERROR: File $ALT_TARIFF_CFG_FILE not found"; exit 1; fi
if [ ! -f "$CLIENT_SCENARIO_FILE" ]; then echo "ERROR: File $CLIENT_SCENARIO_FILE not found"; exit 1; fi
if [ ! -f "$CLIENT_EXPECTED_OUTPUT_FILE" ]; then echo "ERROR: File $CLIENT_EXPECTED_OUTPUT_FILE not found"; exit 1; fi
if [ ! -f "$DB_EXPECTED_FINAL_FILE" ]; then echo "ERROR: File $DB_EXPECTED_FINAL_FILE not found"; exit 1; fi

# Подготовка файлов в рабочей директории
cp "$DB_INITIAL_FILE" "$WORK_DIR/server_db.txt"
cp "$TARIFF_CFG_FILE" "$WORK_DIR/server_tariff.cfg"
# Копируем tariff_alt.cfg в рабочую директорию сервера, чтобы LOAD_TARIFF его нашел
cp "$ALT_TARIFF_CFG_FILE" "$WORK_DIR/tariff_alt.cfg" 

echo "Starting server on port $PORT for single client test..."
"$SERVER_EXE" --dbfile "$WORK_DIR/server_db.txt" --tariff "$WORK_DIR/server_tariff.cfg" --port "$PORT" &
SERVER_PID=$!
echo "Server PID: $SERVER_PID. Waiting for server to start..."
# Даем серверу время запуститься и открыть порт
MAX_SERVER_START_WAIT=10
CURRENT_WAIT=0
SERVER_READY=false
while [ $CURRENT_WAIT -lt $MAX_SERVER_START_WAIT ]; do
    # Проверяем, слушает ли кто-то порт (netstat или ss)
    if ss -tlpn 2>/dev/null | grep -q ":$PORT" || netstat -tlpn 2>/dev/null | grep -q ":$PORT" ; then
        echo "Server port $PORT is now listening."
        SERVER_READY=true
        break
    fi
    sleep 0.5 # Ждем полсекунды
    CURRENT_WAIT=$((CURRENT_WAIT + 1))
done

if [ "$SERVER_READY" = false ]; then
    echo "ERROR: Server did not start listening on port $PORT within $MAX_SERVER_START_WAIT seconds."
    if kill -0 $SERVER_PID 2>/dev/null; then kill -SIGTERM $SERVER_PID; wait $SERVER_PID 2>/dev/null; fi
    exit 1
fi
sleep 1 # Дополнительная короткая пауза для стабилизации сервера

echo "Running client for single client test..."
CLIENT_ACTUAL_OUTPUT_FILE="$WORK_DIR/client_actual_output_single.txt"
"$CLIENT_EXE" --input "$CLIENT_SCENARIO_FILE" \
              --output "$CLIENT_ACTUAL_OUTPUT_FILE" \
              --port "$PORT" --id C_SINGLE --host "127.0.0.1" # Явно указываем host

if [ ! -f "$CLIENT_ACTUAL_OUTPUT_FILE" ]; then
    echo "ERROR: Client actual output file $CLIENT_ACTUAL_OUTPUT_FILE was not created."
    if kill -0 $SERVER_PID 2>/dev/null; then kill -SIGTERM $SERVER_PID; wait $SERVER_PID 2>/dev/null; fi
    exit 1
fi

echo "Client finished. Comparing output..."
PROCESSED_CLIENT_ACTUAL_OUTPUT="$WORK_DIR/client_actual_output.processed"
PROCESSED_CLIENT_EXPECTED_OUTPUT="$WORK_DIR/client_expected_output.processed"

sed -E "s/$TIMESTAMP_REGEX/$TIMESTAMP_PLACEHOLDER/g" "$CLIENT_EXPECTED_OUTPUT_FILE" > "$PROCESSED_CLIENT_EXPECTED_OUTPUT"
sed -E "s/$TIMESTAMP_REGEX/$TIMESTAMP_PLACEHOLDER/g" "$CLIENT_ACTUAL_OUTPUT_FILE" > "$PROCESSED_CLIENT_ACTUAL_OUTPUT"

if diff -u --strip-trailing-cr "$PROCESSED_CLIENT_EXPECTED_OUTPUT" "$PROCESSED_CLIENT_ACTUAL_OUTPUT"; then
    echo "Client output matches expected. OK."
else
    echo "Client output MISMATCH!"
    echo "--- DIFF PROCESSED CLIENT OUTPUT: ---"
    diff -u --strip-trailing-cr "$PROCESSED_CLIENT_EXPECTED_OUTPUT" "$PROCESSED_CLIENT_ACTUAL_OUTPUT"
    echo "--- END DIFF ---"
    if kill -0 $SERVER_PID 2>/dev/null; then kill -SIGTERM $SERVER_PID; wait $SERVER_PID 2>/dev/null; fi
    exit 1
fi

# Клиент должен отправить команду SHUTDOWN_SERVER в конце своего сценария
echo "Waiting for server (PID: $SERVER_PID) to shut down (commanded by client)..."
shutdown_timeout=15 # Увеличим таймаут, если серверу нужно время на сохранение
elapsed_time=0
SERVER_STOPPED_GRACEFULLY=false
while [ $elapsed_time -lt $shutdown_timeout ]; do
    if ! kill -0 $SERVER_PID 2>/dev/null; then
        SERVER_STOPPED_GRACEFULLY=true
        echo "Server (PID: $SERVER_PID) shut down."
        break
    fi
    sleep 1
    elapsed_time=$((elapsed_time + 1))
done

if [ "$SERVER_STOPPED_GRACEFULLY" = false ]; then
   echo "Server (PID: $SERVER_PID) did not shut down as expected within $shutdown_timeout seconds! Forcing kill."
   kill -SIGTERM $SERVER_PID; sleep 1; kill -SIGKILL $SERVER_PID 2>/dev/null
else
   echo "Server shut down procedure completed."
fi
wait $SERVER_PID 2>/dev/null # Убедимся, что процесс точно завершен

# Проверка финального файла БД сервера
if [ ! -f "$WORK_DIR/server_db.txt" ]; then
    echo "ERROR: Server DB file $WORK_DIR/server_db.txt was not found/updated after server shutdown."
    exit 1
fi

echo "Comparing final server DB file..."
if diff -u --strip-trailing-cr "$DB_EXPECTED_FINAL_FILE" "$WORK_DIR/server_db.txt"; then
    echo "Final server DB file matches expected. OK."
else
    echo "Final server DB file MISMATCH!"
    echo "--- DIFF DB FILES: ---"
    diff -u --strip-trailing-cr "$DB_EXPECTED_FINAL_FILE" "$WORK_DIR/server_db.txt"
    echo "--- END DIFF ---"
    exit 1
fi

echo "Client-Server single client test PASSED."
# rm -rf "$WORK_DIR" 
exit 0
