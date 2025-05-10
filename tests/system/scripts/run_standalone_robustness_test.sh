#!/bin/bash

# $1 = CMAKE_BINARY_DIR
# $2 = SYSTEM_TEST_DATA_ROOT_DIR 

CMAKE_BUILD_DIR="$1"
# SYSTEM_TEST_DATA_ROOT="$2" 

APP_EXE="$CMAKE_BUILD_DIR/bin/provider_app_standalone"
WORK_DIR="$CMAKE_BUILD_DIR/Testing/Temporary/standalone_robustness_work"

if [ ! -f "$APP_EXE" ]; then
    echo "ERROR: Application executable not found at $APP_EXE"
    exit 1
fi

mkdir -p "$WORK_DIR"
if [ $? -ne 0 ]; then echo "ERROR: Could not create work directory $WORK_DIR"; exit 1; fi

TEST_PASSED_COUNT=0
TOTAL_TESTS=4
TEST_FAILED_FLAG=0

echo_status() {
    echo "-----------------------------------------------------"
    echo "TEST: $1"
    echo "STATUS: $2"
    if [ "$2" != "PASSED" ]; then
        TEST_FAILED_FLAG=1
    else
        TEST_PASSED_COUNT=$((TEST_PASSED_COUNT + 1))
    fi
    echo "-----------------------------------------------------"
}

cleanup_work_dir() {
    rm -f "$WORK_DIR"/*
}

# --- Тест 1: Несуществующий файл БД ---
cleanup_work_dir
echo "Running: Standalone with non-existent DB file"
# Создаем фиктивный файл тарифа, чтобы тест не падал из-за его отсутствия
echo "0.1" > "$WORK_DIR/dummy_tariff_ok.cfg" 
for i in {1..23}; do echo "0.1" >> "$WORK_DIR/dummy_tariff_ok.cfg"; done

(echo "0"; echo "yes") | "$APP_EXE" --dbfile "$WORK_DIR/db_non_existent.txt" --tariff "$WORK_DIR/dummy_tariff_ok.cfg" > "$WORK_DIR/test1_output.log" 2>&1
APP_EXIT_CODE=$?
if [ $APP_EXIT_CODE -eq 0 ] && [ -f "$WORK_DIR/db_non_existent.txt" ]; then
    if [ ! -s "$WORK_DIR/db_non_existent.txt" ]; then # Файл должен быть пустым
        echo_status "Standalone non-existent DB (creates empty on save)" "PASSED"
    else
        echo_status "Standalone non-existent DB (creates non-empty on save)" "FAILED - DB file not empty"
        cat "$WORK_DIR/db_non_existent.txt"
    fi
    if grep -q -E "File .* not found or cannot be opened. Starting empty|Database loaded from .* 0 records" "$WORK_DIR/test1_output.log"; then
        echo "Log message for empty DB found."
    else
        echo "Warning: Log message for empty DB not found in output for non-existent DB test."
        # cat "$WORK_DIR/test1_output.log" # Можно раскомментировать для отладки
    fi
else
    echo_status "Standalone non-existent DB" "FAILED - App exit code $APP_EXIT_CODE or file not created"
    cat "$WORK_DIR/test1_output.log"
fi


# --- Тест 2: Пустой файл БД ---
cleanup_work_dir
touch "$WORK_DIR/db_empty.txt" 
echo "0.1" > "$WORK_DIR/dummy_tariff_ok.cfg" # Обновляем файл тарифа
for i in {1..23}; do echo "0.1" >> "$WORK_DIR/dummy_tariff_ok.cfg"; done

(echo "0"; echo "no") | "$APP_EXE" --dbfile "$WORK_DIR/db_empty.txt" --tariff "$WORK_DIR/dummy_tariff_ok.cfg" > "$WORK_DIR/test2_output.log" 2>&1
APP_EXIT_CODE=$?
if [ $APP_EXIT_CODE -eq 0 ]; then
    if grep -q -E "File .* not found or cannot be opened. Starting empty|Database loaded from .* 0 records|Loaded 0 valid records" "$WORK_DIR/test2_output.log"; then # Добавил "Loaded 0 valid records"
        echo_status "Standalone empty DB" "PASSED"
    else
        echo_status "Standalone empty DB" "FAILED - Expected log message for empty DB not found"
        cat "$WORK_DIR/test2_output.log"
    fi
else
    echo_status "Standalone empty DB" "FAILED - App exit code $APP_EXIT_CODE"
    cat "$WORK_DIR/test2_output.log"
fi


# --- Тест 3: Несуществующий файл тарифов ---
cleanup_work_dir
touch "$WORK_DIR/dummy_db_ok.txt"
(echo "0"; echo "no") | "$APP_EXE" --dbfile "$WORK_DIR/dummy_db_ok.txt" --tariff "$WORK_DIR/tariff_non_existent.cfg" > "$WORK_DIR/test3_output.log" 2>&1
APP_EXIT_CODE=$?
if [ $APP_EXIT_CODE -eq 0 ]; then
    if grep -q -E "Could not load tariff plan.*Using default rates|Tariff file .* not found or cannot be opened. Using default rates" "$WORK_DIR/test3_output.log"; then
        echo_status "Standalone non-existent tariff file" "PASSED"
    else
        echo_status "Standalone non-existent tariff file" "FAILED - Warning message not found"
        cat "$WORK_DIR/test3_output.log"
    fi
else
    echo_status "Standalone non-existent tariff file" "FAILED - App exit code $APP_EXIT_CODE"
    cat "$WORK_DIR/test3_output.log"
fi

# --- Тест 4: Пустой файл тарифов ---
cleanup_work_dir
touch "$WORK_DIR/dummy_db_ok.txt"
touch "$WORK_DIR/tariff_empty.cfg" 
(echo "0"; echo "no") | "$APP_EXE" --dbfile "$WORK_DIR/dummy_db_ok.txt" --tariff "$WORK_DIR/tariff_empty.cfg" > "$WORK_DIR/test4_output.log" 2>&1
APP_EXIT_CODE=$?
if [ $APP_EXIT_CODE -eq 0 ]; then
     if grep -q -E "Could not load tariff plan.*Using default rates|Tariff file .* must contain exactly .* rates" "$WORK_DIR/test4_output.log"; then
        echo_status "Standalone empty tariff file" "PASSED"
    else
        echo_status "Standalone empty tariff file" "FAILED - Warning message not found"
        cat "$WORK_DIR/test4_output.log"
    fi
else
    echo_status "Standalone empty tariff file" "FAILED - App exit code $APP_EXIT_CODE"
    cat "$WORK_DIR/test4_output.log"
fi


echo "====================================================="
echo "Standalone Robustness Tests Summary:"
echo "PASSED: $TEST_PASSED_COUNT / $TOTAL_TESTS"
echo "====================================================="

# rm -rf "$WORK_DIR" 
exit $TEST_FAILED_FLAG
