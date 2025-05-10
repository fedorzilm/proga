#!/bin/bash

# Аргументы от CMake:
# $1 = CMAKE_BINARY_DIR (путь к директории сборки, например, /path/to/project/build)
# $2 = SYSTEM_TEST_DATA_ROOT_DIR (путь к tests/system/, например, /path/to/project/tests/system)

CMAKE_BUILD_DIR="$1"
SYSTEM_TEST_DATA_ROOT="$2"

APP_EXE="$CMAKE_BUILD_DIR/bin/provider_app_standalone"
TEST_DATA_DIR="$SYSTEM_TEST_DATA_ROOT/test_data_standalone"
# Используем уникальное имя рабочей директории для этого теста
WORK_DIR_NAME="test_work_dir_standalone_batch" 
WORK_DIR="$CMAKE_BUILD_DIR/Testing/Temporary/$WORK_DIR_NAME"

TIMESTAMP_REGEX="[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}"
TIMESTAMP_PLACEHOLDER="TIMESTAMP_PLACEHOLDER"

if [ ! -f "$APP_EXE" ]; then
    echo "ERROR: Application executable not found at $APP_EXE"
    exit 1
fi

mkdir -p "$WORK_DIR"
if [ $? -ne 0 ]; then
    echo "ERROR: Could not create work directory $WORK_DIR"
    exit 1
fi
rm -f "$WORK_DIR"/* # Проверка существования файлов данных
if [ ! -f "$TEST_DATA_DIR/standalone_db_initial.txt" ]; then echo "ERROR: File $TEST_DATA_DIR/standalone_db_initial.txt not found"; exit 1; fi
if [ ! -f "$TEST_DATA_DIR/standalone_tariff.cfg" ]; then echo "ERROR: File $TEST_DATA_DIR/standalone_tariff.cfg not found"; exit 1; fi
if [ ! -f "$TEST_DATA_DIR/tariff_alt.cfg" ]; then echo "ERROR: File $TEST_DATA_DIR/tariff_alt.cfg not found"; exit 1; fi
if [ ! -f "$TEST_DATA_DIR/standalone_queries.txt" ]; then echo "ERROR: File $TEST_DATA_DIR/standalone_queries.txt not found"; exit 1; fi
if [ ! -f "$TEST_DATA_DIR/standalone_expected_output.txt" ]; then echo "ERROR: File $TEST_DATA_DIR/standalone_expected_output.txt not found"; exit 1; fi
if [ ! -f "$TEST_DATA_DIR/standalone_db_expected_final.txt" ]; then echo "ERROR: File $TEST_DATA_DIR/standalone_db_expected_final.txt not found"; exit 1; fi

# Подготовка файлов в рабочей директории
cp "$TEST_DATA_DIR/standalone_db_initial.txt" "$WORK_DIR/current_db.txt"
cp "$TEST_DATA_DIR/standalone_tariff.cfg" "$WORK_DIR/current_tariff.cfg"
# Копируем tariff_alt.cfg в WORK_DIR, чтобы main.cpp мог его найти по относительному пути при LOAD_TARIFF
cp "$TEST_DATA_DIR/tariff_alt.cfg" "$WORK_DIR/tariff_alt.cfg" 

echo "Running standalone batch test..."
# Запуск приложения. main.cpp определяет путь к data/WORK_DIR для tariff_alt.cfg относительно current_tariff.cfg
"$APP_EXE" --input "$TEST_DATA_DIR/standalone_queries.txt" \
           --output "$WORK_DIR/actual_output.txt" \
           --dbfile "$WORK_DIR/current_db.txt" \
           --tariff "$WORK_DIR/current_tariff.cfg"

if [ ! -f "$WORK_DIR/actual_output.txt" ]; then
    echo "ERROR: Actual output file $WORK_DIR/actual_output.txt was not created."
    exit 1
fi

echo "Comparing output file..."
ACTUAL_OUTPUT_PROCESSED="$WORK_DIR/actual_output.processed"
EXPECTED_OUTPUT_PROCESSED="$WORK_DIR/expected_output.processed"

sed -E "s/$TIMESTAMP_REGEX/$TIMESTAMP_PLACEHOLDER/g" "$TEST_DATA_DIR/standalone_expected_output.txt" > "$EXPECTED_OUTPUT_PROCESSED"
sed -E "s/$TIMESTAMP_REGEX/$TIMESTAMP_PLACEHOLDER/g" "$WORK_DIR/actual_output.txt" > "$ACTUAL_OUTPUT_PROCESSED"

if diff -u --strip-trailing-cr "$EXPECTED_OUTPUT_PROCESSED" "$ACTUAL_OUTPUT_PROCESSED"; then
    echo "Output file matches expected. OK."
else
    echo "Output file MISMATCH!"
    echo "--- DIFF PROCESSED FILES: ---"
    diff -u --strip-trailing-cr "$EXPECTED_OUTPUT_PROCESSED" "$ACTUAL_OUTPUT_PROCESSED"
    echo "--- END DIFF ---"
    # echo "--- EXPECTED OUTPUT (Original): ---"; cat "$TEST_DATA_DIR/standalone_expected_output.txt"; echo "--- ACTUAL OUTPUT (Original): ---"; cat "$WORK_DIR/actual_output.txt"; echo "--- END OUTPUT ---"
    exit 1
fi

if [ ! -f "$WORK_DIR/current_db.txt" ]; then
    echo "ERROR: Current DB file $WORK_DIR/current_db.txt was not found/updated."
    exit 1
fi

echo "Comparing final DB file..."
if diff -u --strip-trailing-cr "$TEST_DATA_DIR/standalone_db_expected_final.txt" "$WORK_DIR/current_db.txt"; then
    echo "Final DB file matches expected. OK."
else
    echo "Final DB file MISMATCH!"
    # echo "--- EXPECTED DB: ---"; cat "$TEST_DATA_DIR/standalone_db_expected_final.txt"; echo "--- ACTUAL DB: ---"; cat "$WORK_DIR/current_db.txt"; echo "--- END DB ---"
    exit 1
fi

echo "Standalone batch test PASSED."
# rm -rf "$WORK_DIR" 
exit 0
