#!/bin/bash

# $1: GENERATOR_EXE_PATH (путь к исполняемому файлу generate_data)
# $2: OUTPUT_DIR (директория, куда будет записан файл)
# $3: NUM_RECORDS (количество записей для генерации)

GENERATOR_EXE="$1"
OUTPUT_DIR="$2"
NUM_RECORDS="$3"

# Проверяем, что все аргументы переданы
if [ -z "$GENERATOR_EXE" ] || [ -z "$OUTPUT_DIR" ] || [ -z "$NUM_RECORDS" ]; then
    echo "ERROR: Missing arguments to check_generated_data.sh"
    echo "Usage: $0 <generator_exe_path> <output_dir> <num_records>"
    exit 1
fi

# Проверяем, что NUM_RECORDS - это число и оно больше 0
if ! [[ "$NUM_RECORDS" =~ ^[0-9]+$ ]] || [ "$NUM_RECORDS" -le 0 ]; then
    echo "ERROR: NUM_RECORDS must be a positive integer. Got: '$NUM_RECORDS'"
    exit 1
fi

EXPECTED_LINES=$((NUM_RECORDS * 4)) # Каждая запись ProviderRecord занимает 4 строки
OUTPUT_FILE="$OUTPUT_DIR/generated_smoke_data.txt" # Имя файла для вывода генератора

# Проверка существования исполняемого файла генератора
if [ ! -f "$GENERATOR_EXE" ]; then
    echo "ERROR: Generator executable not found at $GENERATOR_EXE"
    exit 1
fi

# Создаем директорию для вывода, если ее нет
mkdir -p "$OUTPUT_DIR"
if [ $? -ne 0 ]; then
    echo "ERROR: Could not create output directory $OUTPUT_DIR"
    exit 1
fi
# Очищаем от предыдущих запусков, если файл существует
rm -f "$OUTPUT_FILE"

echo "Running generator: $GENERATOR_EXE $NUM_RECORDS \"$OUTPUT_FILE\""
# Передаем путь к выходному файлу в кавычках, если он может содержать пробелы
"$GENERATOR_EXE" "$NUM_RECORDS" "$OUTPUT_FILE"
GENERATOR_EXIT_CODE=$?

if [ $GENERATOR_EXIT_CODE -ne 0 ]; then
    echo "ERROR: Generator exited with code $GENERATOR_EXIT_CODE"
    # Попробуем вывести лог генератора, если он что-то писал в stderr/stdout
    # (предполагается, что вызов в CMake не перенаправляет это)
    exit 1
fi

if [ ! -f "$OUTPUT_FILE" ]; then
    echo "ERROR: Generator did not create output file at $OUTPUT_FILE"
    exit 1
fi

# Проверка, что файл не пустой
if [ ! -s "$OUTPUT_FILE" ]; then
    echo "ERROR: Generated file $OUTPUT_FILE is empty."
    exit 1
fi

# Подсчитываем количество строк в файле
ACTUAL_LINES=$(wc -l < "$OUTPUT_FILE" | tr -d ' ') # tr -d ' ' для удаления пробелов, которые wc может добавить

if [ "$ACTUAL_LINES" -eq "$EXPECTED_LINES" ]; then
    echo "Generated file $OUTPUT_FILE has expected number of lines ($EXPECTED_LINES). OK."
else
    echo "ERROR: Generated file $OUTPUT_FILE has $ACTUAL_LINES lines, expected $EXPECTED_LINES lines."
    exit 1
fi

echo "Generator smoke test PASSED."
# rm -f "$OUTPUT_FILE" # Очистка после успешного теста, если это временный файл только для этого теста
exit 0
