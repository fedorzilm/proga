cmake_minimum_required(VERSION 3.14) 
project(InternetProviderDB CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

# Определение директории для выходных файлов (бинарников)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Threads REQUIRED)
# find_package(Python3 COMPONENTS Interpreter) # Раскомментируйте, если Python действительно нужен для чего-то
# Для std::filesystem в C++17 может потребоваться линковка с stdc++fs или аналогичной библиотекой,
# но обычно современные компиляторы (GCC 8+, Clang 7+) включают это в стандартную библиотеку C++.
# Если возникнут проблемы с std::filesystem, проверьте документацию вашего компилятора.

if(UNIX AND (CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_CLANG))
  message(STATUS "UNIX GCC/Clang detected. Threads::Threads will handle pthreads.")
endif()

if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_CLANG)
  add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wnull-dereference -Wformat=2)
  # add_compile_options(-Werror) # Для строгой разработки
endif()

if(WIN32)
  find_library(WS2_32_LIBRARY ws2_32)
  if(NOT WS2_32_LIBRARY)
    message(FATAL_ERROR "ws2_32.lib not found. Please ensure Windows SDK is installed correctly.")
  endif()
endif()

# --- Общая библиотека ядра ---
add_library(db_core_lib STATIC
    src/date.cpp
    src/ip_address.cpp
    src/provider_record.cpp
    src/tariff_plan.cpp
    src/database.cpp
    src/query_parser.cpp
)
target_include_directories(db_core_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
# Для std::filesystem в GCC < 9 может потребоваться -lstdc++fs
# if(CMAKE_COMPILER_IS_GNUCC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0.0")
#   target_link_libraries(db_core_lib INTERFACE "-lstdc++fs")
# endif()


# --- Вспомогательная библиотека для сети ---
add_library(network_utils_lib STATIC
    src/network/socket_utils.cpp
)
target_include_directories(network_utils_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network 
    ${CMAKE_CURRENT_SOURCE_DIR}/src 
)

# --- Исполняемые файлы приложения ---

# Сервер
add_executable(db_server_app
    src/server/server_main.cpp
    src/server/db_server.cpp
)
target_link_libraries(db_server_app PRIVATE db_core_lib network_utils_lib)
if(WIN32)
  target_link_libraries(db_server_app PRIVATE ${WS2_32_LIBRARY})
else()
  target_link_libraries(db_server_app PRIVATE Threads::Threads)
endif()
target_include_directories(db_server_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# if(CMAKE_COMPILER_IS_GNUCC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0.0")
#   target_link_libraries(db_server_app PRIVATE "-lstdc++fs")
# endif()


# Клиент
add_executable(db_client_app
    src/client/client_main.cpp
    src/client/db_client.cpp
)
# Клиенту db_core_lib может быть не нужен напрямую, если он только шлет строки и не парсит ответы локально.
# Но если он использует Date, IpAddress для валидации ввода перед отправкой, то нужен.
target_link_libraries(db_client_app PRIVATE db_core_lib network_utils_lib) 
if(WIN32)
  target_link_libraries(db_client_app PRIVATE ${WS2_32_LIBRARY})
else()
  target_link_libraries(db_client_app PRIVATE Threads::Threads)
endif()
target_include_directories(db_client_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Монолитное приложение
add_executable(provider_app_standalone
    src/main.cpp
    src/user_interface.cpp
)
target_link_libraries(provider_app_standalone PRIVATE db_core_lib)
target_include_directories(provider_app_standalone PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# if(CMAKE_COMPILER_IS_GNUCC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0.0")
#   target_link_libraries(provider_app_standalone PRIVATE "-lstdc++fs")
# endif()

# Генератор данных
add_executable(generate_data
    tools/generator.cpp
    src/date.cpp             
    src/ip_address.cpp
    src/provider_record.cpp
)
target_include_directories(generate_data PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# if(CMAKE_COMPILER_IS_GNUCC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0.0")
#   target_link_libraries(generate_data PRIVATE "-lstdc++fs") # Если generator.cpp использует ensure_directory_exists_util с filesystem
# endif()


# --- Тестирование с Google Test ---
enable_testing()
include(FetchContent)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip # Актуальная версия на момент написания
)
FetchContent_MakeAvailable(googletest)

# Функция для добавления модульного теста
function(add_gtest_unit_test test_name test_source_file)
    add_executable(${test_name} tests/unit/${test_source_file})
    # Все юнит-тесты линкуются с db_core_lib, так как они тестируют его компоненты
    target_link_libraries(${test_name} PRIVATE gtest_main db_core_lib) 
    target_include_directories(${test_name} PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/network # Если тесты затрагивают network_protocol.h
    )
    # if(CMAKE_COMPILER_IS_GNUCC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0.0")
    #    target_link_libraries(${test_name} PRIVATE "-lstdc++fs") # Для filesystem в common_defs через db_core_lib
    # endif()
    add_test(NAME ${test_name} COMMAND ${test_name} WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
endfunction()

# Добавление модульных тестов
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/test_common_defs.cpp")
    add_gtest_unit_test(CommonDefsTests test_common_defs.cpp)
endif()
add_gtest_unit_test(DateTests test_date.cpp)
add_gtest_unit_test(IpAddressTests test_ip_address.cpp)
add_gtest_unit_test(TariffPlanTests test_tariff_plan.cpp)
add_gtest_unit_test(ProviderRecordTests test_provider_record.cpp)
add_gtest_unit_test(QueryParserTests test_query_parser.cpp)
add_gtest_unit_test(DatabaseIntegrationTests test_database_integration.cpp)


# --- Системные тесты (запуск скриптов через CTest) ---
set(SYSTEM_TEST_SCRIPT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/system/scripts)
set(SYSTEM_TEST_DATA_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/system) 

if(WIN32)
  set(SCRIPT_INTERPRETER "cmd.exe")
  set(SCRIPT_INTERPRETER_FLAG "/c")
  # Для .sh на Windows (Git Bash/WSL) - если используется, нужно найти bash
  # find_program(BASH_EXE bash sh) ...
else() 
  find_program(BASH_EXE NAMES bash sh REQUIRED HINTS /bin /usr/bin /usr/local/bin)
  if(NOT BASH_EXE)
    message(FATAL_ERROR "Bash interpreter (bash or sh) not found. System tests cannot run.")
  endif()
  set(SCRIPT_INTERPRETER ${BASH_EXE})
  set(SCRIPT_CHMOD_FLAG "-c") # Флаг для chmod через execute_process
endif()

# Функция для создания исполняемых скриптов и добавления системного теста
function(add_system_test test_name script_filename target_dependencies)
    set(FULL_SCRIPT_PATH "${SYSTEM_TEST_SCRIPT_DIR}/${script_filename}")
    if(NOT EXISTS ${FULL_SCRIPT_PATH})
        message(WARNING "System test script ${script_filename} not found at ${FULL_SCRIPT_PATH}. Skipping test ${test_name}.")
        return()
    endif()

    if(UNIX AND SCRIPT_INTERPRETER)
        execute_process(COMMAND ${SCRIPT_INTERPRETER} ${SCRIPT_CHMOD_FLAG} "chmod +x \"${FULL_SCRIPT_PATH}\"")
    endif()

    # Для add_test, если SCRIPT_INTERPRETER это bash/sh, то он первый аргумент, а сам скрипт - второй.
    # Если SCRIPT_INTERPRETER это cmd.exe, то нужен флаг /c.
    # Для единообразия, если скрипт исполняемый, можно было бы запускать его напрямую на Unix.
    # Но для поддержки cmd.exe /c script.bat, оставим SCRIPT_INTERPRETER первым.
    if(WIN32 AND SCRIPT_INTERPRETER_FLAG)
         add_test(NAME ${test_name}
             COMMAND ${SCRIPT_INTERPRETER} ${SCRIPT_INTERPRETER_FLAG} "${FULL_SCRIPT_PATH}"
                     "${CMAKE_BINARY_DIR}"
                     "${SYSTEM_TEST_DATA_ROOT_DIR}"
             WORKING_DIRECTORY ${SYSTEM_TEST_SCRIPT_DIR} # Чтобы относительные пути внутри скрипта работали
         )
    else() # Unix or no flag needed
         add_test(NAME ${test_name}
             COMMAND ${SCRIPT_INTERPRETER} "${FULL_SCRIPT_PATH}" # На Unix SCRIPT_INTERPRETER это /bin/bash, сам скрипт - аргумент
                     "${CMAKE_BINARY_DIR}"
                     "${SYSTEM_TEST_DATA_ROOT_DIR}"
             WORKING_DIRECTORY ${SYSTEM_TEST_SCRIPT_DIR}
         )
    endif()
    set_tests_properties(${test_name} PROPERTIES DEPENDS "${target_dependencies}")
endfunction()

# Smoke test для генератора
set(GENERATOR_CHECK_SCRIPT_NAME "check_generated_data.sh")
set(FULL_GENERATOR_CHECK_SCRIPT "${SYSTEM_TEST_SCRIPT_DIR}/${GENERATOR_CHECK_SCRIPT_NAME}")
if(NOT EXISTS ${FULL_GENERATOR_CHECK_SCRIPT})
    message(WARNING "Generator smoke test check script not found. Skipping.")
else()
    if(UNIX AND SCRIPT_INTERPRETER)
        execute_process(COMMAND ${SCRIPT_INTERPRETER} ${SCRIPT_CHMOD_FLAG} "chmod +x ${FULL_GENERATOR_CHECK_SCRIPT}")
    endif()
    if(WIN32 AND SCRIPT_INTERPRETER_FLAG)
        add_test(NAME GeneratorSmokeTest
            COMMAND ${SCRIPT_INTERPRETER} ${SCRIPT_INTERPRETER_FLAG} "${FULL_GENERATOR_CHECK_SCRIPT}"
                    "$<TARGET_FILE:generate_data>"
                    "${CMAKE_BINARY_DIR}/Testing/Temporary/generator_smoke_output"
                    "10" # num_records
            WORKING_DIRECTORY ${SYSTEM_TEST_SCRIPT_DIR}
        )
    else()
        add_test(NAME GeneratorSmokeTest
            COMMAND ${SCRIPT_INTERPRETER} "${FULL_GENERATOR_CHECK_SCRIPT}"
                    "$<TARGET_FILE:generate_data>"
                    "${CMAKE_BINARY_DIR}/Testing/Temporary/generator_smoke_output"
                    "10" # num_records
            WORKING_DIRECTORY ${SYSTEM_TEST_SCRIPT_DIR}
        )
    endif()
    set_tests_properties(GeneratorSmokeTest PROPERTIES DEPENDS "generate_data")
endif()

# Тесты на устойчивость
add_system_test(SystemTestStandaloneRobustness "run_standalone_robustness_test.sh" "provider_app_standalone")
add_system_test(SystemTestServerRobustness "run_server_robustness_test.sh" "db_server_app;db_client_app")

# Основные системные тесты
add_system_test(SystemTestStandaloneBatch "run_standalone_batch_test.sh" "provider_app_standalone")
add_system_test(SystemTestClientServerSingle "run_client_server_single_test.sh" "db_server_app;db_client_app")
add_system_test(SystemTestHttp "run_http_tests.sh" "db_server_app")


# Параметризованные тесты для run_client_server_multi_test.sh
# Передаем третий аргумент - префикс сценария
function(add_multi_client_scenario_test scenario_prefix)
    set(TEST_NAME "SystemTestClientServerMulti_${scenario_prefix}")
    set(FULL_SCRIPT_PATH "${SYSTEM_TEST_SCRIPT_DIR}/run_client_server_multi_test.sh") # Один и тот же скрипт

    # Проверяем, что необходимые файлы данных для сценария существуют, чтобы не добавлять сломанный тест
    # Это базовая проверка, скрипт сам должен делать более детальные проверки
    if(NOT EXISTS "${SYSTEM_TEST_DATA_ROOT_DIR}/test_data_cs_multi/client_scenario_multi_${scenario_prefix}1.txt")
        message(WARNING "Data for multi-client scenario ${scenario_prefix} not found. Skipping test ${TEST_NAME}.")
        return()
    endif()

    if(WIN32 AND SCRIPT_INTERPRETER_FLAG)
        add_test(NAME ${TEST_NAME}
            COMMAND ${SCRIPT_INTERPRETER} ${SCRIPT_INTERPRETER_FLAG} "${FULL_SCRIPT_PATH}"
                    "${CMAKE_BINARY_DIR}"
                    "${SYSTEM_TEST_DATA_ROOT_DIR}"
                    "${scenario_prefix}" # Третий аргумент для скрипта
            WORKING_DIRECTORY ${SYSTEM_TEST_SCRIPT_DIR}
        )
    else()
        add_test(NAME ${TEST_NAME}
            COMMAND ${SCRIPT_INTERPRETER} "${FULL_SCRIPT_PATH}"
                    "${CMAKE_BINARY_DIR}"
                    "${SYSTEM_TEST_DATA_ROOT_DIR}"
                    "${scenario_prefix}" # Третий аргумент для скрипта
            WORKING_DIRECTORY ${SYSTEM_TEST_SCRIPT_DIR}
        )
    endif()
    set_tests_properties(${TEST_NAME} PROPERTIES DEPENDS "db_server_app;db_client_app")
endfunction()

add_multi_client_scenario_test("A") # Конкурентное Редактирование/Удаление
add_multi_client_scenario_test("B") # Конкурентное Чтение/Запись
add_multi_client_scenario_test("C") # Конкурентный LOAD_TARIFF/CALCULATE_BILL
add_multi_client_scenario_test("D") # Конкурентное Редактирование Разных Полей
add_multi_client_scenario_test("E") # Конкурентное Удаление
add_multi_client_scenario_test("F") # SHUTDOWN_SERVER при активных клиентах


message(STATUS "Project Output Binaries will be in: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "Project Output Libraries will be in: ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message(STATUS "To run all tests execute: ctest (or make test / ninja test) in build directory")
