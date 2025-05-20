#-------------------------------------------------------------------------------
# CMakeLists.txt
#-------------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.14)
project(InternetProviderDB_CS CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# --- Общие флаги компиляции ---
set(COMMON_CXX_FLAGS "")
set(COMMON_WARNING_FLAGS "")
set(OPTIMIZATION_FLAGS "")

if(MSVC)
    list(APPEND COMMON_WARNING_FLAGS /W4 /WX)
    list(APPEND COMMON_CXX_FLAGS /GR /EHsc /D_CRT_SECURE_NO_WARNINGS /D_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        list(APPEND OPTIMIZATION_FLAGS /O2 /DNDEBUG)
    elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND OPTIMIZATION_FLAGS /Od /Zi /D_DEBUG)
    else() # По умолчанию Release
        list(APPEND OPTIMIZATION_FLAGS /O2 /DNDEBUG)
    endif()
else() 
    # GCC/Clang
    list(APPEND COMMON_WARNING_FLAGS
        -Wall -Wextra -Wpedantic
        -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused
        -Woverloaded-virtual -Wconversion -Wnull-dereference # -Wsign-conversion Слишком много предупреждений в gtest
        -Wdouble-promotion -Wformat=2 -Wformat-security
        -Wsuggest-override
        -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wrestrict
        -Wzero-as-null-pointer-constant -Wctor-dtor-privacy
        -Wdelete-non-virtual-dtor
        -Winvalid-offsetof
        -Wpessimizing-move -Wredundant-move -Wreorder
        -Wsuggest-final-methods -Wsuggest-final-types
        -Wconditionally-supported
    )
    # list(APPEND COMMON_WARNING_FLAGS -Werror) # Раскомментировать для CI
    list(APPEND COMMON_CXX_FLAGS -std=c++17 -fPIC -fvisibility=hidden -fvisibility-inlines-hidden -pthread)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
         list(APPEND OPTIMIZATION_FLAGS -O3 -DNDEBUG)
    elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND OPTIMIZATION_FLAGS -g -O0)
    else() # По умолчанию Release с отладкой
        list(APPEND OPTIMIZATION_FLAGS -O2 -DNDEBUG -g)
    endif()
endif()

# --- Google Test (для модульного тестирования) ---
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE) 
FetchContent_MakeAvailable(googletest) 
set(INSTALL_GTEST OFF)

# --- Общая статическая библиотека ядра и утилит (core_utils_lib) ---
set(CORE_UTILS_LIB_SOURCES
    src/core/database.cpp
    src/core/date.cpp
    src/core/ip_address.cpp
    src/core/provider_record.cpp
    src/core/query_parser.cpp
    src/core/tariff_plan.cpp
    src/utils/file_utils.cpp
    src/utils/logger.cpp
    src/utils/thread_pool.cpp
)
set(CORE_UTILS_LIB_HEADERS
    src/common_defs.h
    src/core/database.h
    src/core/date.h
    src/core/ip_address.h
    src/core/provider_record.h
    src/core/query_parser.h
    src/core/tariff_plan.h
    src/utils/file_utils.h
    src/utils/logger.h
    src/utils/thread_pool.h
)
add_library(core_utils_lib STATIC ${CORE_UTILS_LIB_SOURCES} ${CORE_UTILS_LIB_HEADERS})
target_include_directories(core_utils_lib PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"      
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core" 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/utils"
)
target_compile_features(core_utils_lib PUBLIC cxx_std_17)
target_compile_options(core_utils_lib PRIVATE ${COMMON_CXX_FLAGS} ${COMMON_WARNING_FLAGS} ${OPTIMIZATION_FLAGS})
if(NOT MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    target_compile_options(core_utils_lib PRIVATE --coverage)
    target_link_libraries(core_utils_lib INTERFACE --coverage)
endif()

# --- Библиотека сетевых утилит (net_lib) ---
set(NET_LIB_SOURCES src/net/tcp_socket.cpp)
set(NET_LIB_HEADERS src/net/tcp_socket.h)
add_library(net_lib STATIC ${NET_LIB_SOURCES} ${NET_LIB_HEADERS})
target_include_directories(net_lib PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/net"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/utils" 
    "${CMAKE_CURRENT_SOURCE_DIR}/src"    
)
target_link_libraries(net_lib PUBLIC core_utils_lib)
target_compile_features(net_lib PUBLIC cxx_std_17)
target_compile_options(net_lib PRIVATE ${COMMON_CXX_FLAGS} ${COMMON_WARNING_FLAGS} ${OPTIMIZATION_FLAGS})
if(NOT MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    target_compile_options(net_lib PRIVATE --coverage)
    target_link_libraries(net_lib INTERFACE --coverage)
endif()
if(WIN32)
    target_link_libraries(net_lib PRIVATE ws2_32)
endif()

# --- Библиотека серверных компонентов (server_components_lib) ---
set(SERVER_COMPONENTS_LIB_SOURCES
    src/server/server_command_handler.cpp
    src/server/server_config.cpp
)
set(SERVER_COMPONENTS_LIB_HEADERS
    src/server/server_command_handler.h
    src/server/server_config.h
)
add_library(server_components_lib STATIC ${SERVER_COMPONENTS_LIB_SOURCES} ${SERVER_COMPONENTS_LIB_HEADERS})
target_include_directories(server_components_lib PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/utils"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/net"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(server_components_lib PUBLIC core_utils_lib net_lib)
target_compile_features(server_components_lib PUBLIC cxx_std_17)
target_compile_options(server_components_lib PRIVATE ${COMMON_CXX_FLAGS} ${COMMON_WARNING_FLAGS} ${OPTIMIZATION_FLAGS})
if(NOT MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    target_compile_options(server_components_lib PRIVATE --coverage)
    target_link_libraries(server_components_lib INTERFACE --coverage)
endif()

# --- Серверное приложение ---
set(SERVER_APP_SOURCES
    src/server/server_main.cpp
    src/server/server.cpp 
)
set(SERVER_APP_HEADERS 
    src/server/server.h
)
add_executable(database_server ${SERVER_APP_SOURCES} ${SERVER_APP_HEADERS})
target_link_libraries(database_server PRIVATE server_components_lib core_utils_lib net_lib)
target_include_directories(database_server PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_features(database_server PUBLIC cxx_std_17)
target_compile_options(database_server PRIVATE ${COMMON_CXX_FLAGS} ${COMMON_WARNING_FLAGS} ${OPTIMIZATION_FLAGS})
if(NOT MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    target_compile_options(database_server PRIVATE --coverage)
    target_link_libraries(database_server PRIVATE --coverage)
endif()

# --- Клиентское приложение ---
set(CLIENT_APP_SOURCES src/client/client_main.cpp)
add_executable(database_client ${CLIENT_APP_SOURCES})
target_link_libraries(database_client PRIVATE core_utils_lib net_lib) 
target_include_directories(database_client PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/net"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/utils"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_features(database_client PUBLIC cxx_std_17)
target_compile_options(database_client PRIVATE ${COMMON_CXX_FLAGS} ${COMMON_WARNING_FLAGS} ${OPTIMIZATION_FLAGS})
if(NOT MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    target_compile_options(database_client PRIVATE --coverage)
    target_link_libraries(database_client PRIVATE --coverage)
endif()

# --- Исполняемый файл генератора данных ---
add_executable(generate_data tools/generator.cpp)
target_link_libraries(generate_data PRIVATE core_utils_lib)
target_include_directories(generate_data PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/utils"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_features(generate_data PUBLIC cxx_std_17)
target_compile_options(generate_data PRIVATE ${COMMON_CXX_FLAGS} ${COMMON_WARNING_FLAGS} ${OPTIMIZATION_FLAGS})

# --- Модульные тесты ---
enable_testing()
set(UNIT_TEST_MAIN_SOURCES
    unit_tests/test_main.cpp
    unit_tests/test_date.cpp
    unit_tests/test_ip_address.cpp
    unit_tests/test_provider_record.cpp
    unit_tests/test_tariff_plan.cpp
    unit_tests/test_query_parser.cpp
    unit_tests/test_database.cpp
    unit_tests/test_file_utils.cpp
    unit_tests/test_logger.cpp
    unit_tests/test_tcp_socket.cpp
    unit_tests/test_thread_pool.cpp
    unit_tests/test_server_config.cpp
    unit_tests/test_server_command_handler.cpp
    unit_tests/test_client_request_processing.cpp 
    unit_tests/test_server.cpp                    
)

add_executable(run_unit_tests ${UNIT_TEST_MAIN_SOURCES})
target_link_libraries(run_unit_tests PRIVATE server_components_lib core_utils_lib net_lib GTest::gtest GTest::gtest_main)

target_include_directories(run_unit_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/utils"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/net"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server" 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client"
    "${CMAKE_CURRENT_SOURCE_DIR}/unit_tests" 
    "${googletest_SOURCE_DIR}/googletest/include"
    "${googletest_SOURCE_DIR}/googlemock/include" 
)
target_compile_features(run_unit_tests PUBLIC cxx_std_17)
target_compile_options(run_unit_tests PRIVATE ${COMMON_CXX_FLAGS} ${COMMON_WARNING_FLAGS} ${OPTIMIZATION_FLAGS})
if(NOT MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    target_compile_options(run_unit_tests PRIVATE --coverage)
    target_link_libraries(run_unit_tests PRIVATE --coverage)
endif()

include(GoogleTest)
gtest_discover_tests(run_unit_tests
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)

# --- Doxygen ---
find_package(Doxygen)
if(DOXYGEN_FOUND)
    set(DOXYGEN_PROJECT_NAME "${PROJECT_NAME}")
    set(DOXYGEN_PROJECT_NUMBER "1.7.4_client_server_stage4_final") 
    set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/doxygen_docs")
    set(DOXYGEN_INPUT_DIRECTORIES
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools"
        "${CMAKE_CURRENT_SOURCE_DIR}/unit_tests"
    )
    set(DOXYGEN_FILE_PATTERNS "*.h" "*.cpp")
    set(DOXYGEN_EXCLUDE_PATTERNS "*/build/*" "*/googletest/*" "*/_deps/*")
    set(DOXYGEN_RECURSIVE YES)
    set(DOXYGEN_GENERATE_LATEX NO)
    set(DOXYGEN_EXTRACT_ALL YES)
    set(DOXYGEN_EXTRACT_PRIVATE NO)
    set(DOXYGEN_EXTRACT_STATIC YES)
    set(DOXYGEN_WARN_IF_UNDOCUMENTED YES) 
    set(DOXYGEN_WARN_LOGFILE "${CMAKE_BINARY_DIR}/doxygen_warnings.log")
    set(DOXYGEN_HAVE_DOT YES)
    set(DOXYGEN_UML_LOOK YES)
    set(DOXYGEN_CALL_GRAPH YES)
    set(DOXYGEN_CALLER_GRAPH YES)

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in"
            "${CMAKE_BINARY_DIR}/Doxyfile"
            @ONLY
        )
        add_custom_target(doxygen ALL 
            COMMAND "${DOXYGEN_EXECUTABLE}" "${CMAKE_BINARY_DIR}/Doxyfile"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMENT "Generating API documentation with Doxygen using configured Doxyfile. Output in ${DOXYGEN_OUTPUT_DIRECTORY}/html/"
            VERBATIM
        )
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile")
         add_custom_target(doxygen ALL 
            COMMAND "${DOXYGEN_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            COMMENT "Generating API documentation with Doxygen using existing Doxyfile."
            VERBATIM
        )
    else()
        message(WARNING "Doxyfile or Doxyfile.in not found. Doxygen target may not be fully effective.")
    endif()
else()
    message(WARNING "Doxygen not found. Documentation target 'doxygen' will not be available.")
endif()

# --- Интеграционные тесты ---
if(UNIX)
    find_program(BASH_EXECUTABLE bash)
    if(BASH_EXECUTABLE)
        set(INTEGRATION_TEST_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/integration_tests/run_all_tests.sh")
        if(EXISTS "${INTEGRATION_TEST_SCRIPT}")
            add_test(
                NAME IntegrationTestsRunner
                COMMAND "${BASH_EXECUTABLE}" "${INTEGRATION_TEST_SCRIPT}"
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/integration_tests"
            )
            set_tests_properties(IntegrationTestsRunner PROPERTIES DEPENDS "database_server;database_client")
            message(STATUS "Интеграционные тесты (через ${INTEGRATION_TEST_SCRIPT}) добавлены в CTest.")
        else()
            message(WARNING "Скрипт интеграционных тестов ${INTEGRATION_TEST_SCRIPT} не найден. Интеграционные тесты не будут добавлены.")
        endif()
    else()
        message(WARNING "Интерпретатор Bash не найден. Интеграционные тесты (shell-скрипты) не будут добавлены.")
    endif()
else()
    message(STATUS "Интеграционные тесты (на основе shell-скриптов) настроены для UNIX-подобных систем. На Windows они могут не работать без дополнительной настройки.")
endif()

# --- Настройка Code Coverage с LCOV (Только для Debug на GCC/Clang) ---
option(ENABLE_COVERAGE "Enable code coverage reporting (Debug GCC/Clang only)" ON)

if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang") AND ENABLE_COVERAGE)
    find_program(LCOV_EXECUTABLE lcov)
    find_program(GENHTML_EXECUTABLE genhtml)
    if(NOT GCOV_TOOL AND CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        find_program(GCOV_TOOL gcov NAMES gcov-${CMAKE_CXX_COMPILER_VERSION} gcov)
    elseif(NOT GCOV_TOOL AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        find_program(GCOV_TOOL llvm-cov NAMES llvm-cov-${CMAKE_CXX_COMPILER_VERSION} llvm-cov)
    endif()

    if(LCOV_EXECUTABLE AND GENHTML_EXECUTABLE AND GCOV_TOOL)
        message(STATUS "Инструменты для анализа покрытия кода найдены: LCOV=${LCOV_EXECUTABLE}, GENHTML=${GENHTML_EXECUTABLE}, GCOV/LLVM-COV=${GCOV_TOOL}")
        set(COVERAGE_OUTPUT_DIR_BASE "${CMAKE_BINARY_DIR}/coverage_report")

        add_custom_target(coverage_clean 
            COMMAND "${CMAKE_COMMAND}" -E remove_directory "${COVERAGE_OUTPUT_DIR_BASE}"
            COMMAND find "${CMAKE_BINARY_DIR}" -name "*.gcda" -type f -delete
            # COMMAND find "${CMAKE_BINARY_DIR}" \( -name "*.gcno" -o -name "*.gcov" \) -type f -delete  # .gcno не удаляем здесь
            COMMENT "Очистка предыдущих данных .gcda и отчетов о покрытии кода (файлы .gcno НЕ удаляются)."
            VERBATIM
        )

        set(LCOV_BASE_PARAMS
            --capture
            --directory "${CMAKE_BINARY_DIR}" 
            --base-directory "${CMAKE_SOURCE_DIR}" 
            --gcov-tool "${GCOV_TOOL}"
           # --no-external 
        )
        set(LCOV_REMOVE_PATTERNS 
            "/usr/*"                  
            "*/_deps/*"               
            "*/googletest/*"          
            "*/unit_tests/*"          
            "*/tools/generator.cpp"   
        )

        set(COVERAGE_INFO_ALL "${COVERAGE_OUTPUT_DIR_BASE}/total_coverage.info")
        set(COVERAGE_HTML_ALL "${COVERAGE_OUTPUT_DIR_BASE}/html_total")

        add_custom_target(coverage 
            COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" -C "${CMAKE_BUILD_TYPE}" --output-on-failure
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${COVERAGE_OUTPUT_DIR_BASE}" 
            COMMAND "${LCOV_EXECUTABLE}" ${LCOV_BASE_PARAMS} --output-file "${COVERAGE_INFO_ALL}.initial"
            # ИСПРАВЛЕНИЕ ДЛЯ ДИАГНОСТИКИ: Закомментируем --remove и будем использовать .initial для genhtml
            # COMMAND "${LCOV_EXECUTABLE}" --remove "${COVERAGE_INFO_ALL}.initial" ${LCOV_REMOVE_PATTERNS} --output-file "${COVERAGE_INFO_ALL}"
            # COMMAND "${GENHTML_EXECUTABLE}" "${COVERAGE_INFO_ALL}" --output-directory "${COVERAGE_HTML_ALL}" --title "Total Code Coverage" --legend --show-details --demangle-cpp
            COMMAND "${GENHTML_EXECUTABLE}" "${COVERAGE_INFO_ALL}.initial" --output-directory "${COVERAGE_HTML_ALL}" --title "Total Code Coverage (Initial)" --legend --show-details --demangle-cpp
            COMMENT "Генерация полного отчета о покрытии кода. Отчет будет в ${COVERAGE_HTML_ALL}/index.html"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" 
            DEPENDS coverage_clean run_unit_tests 
            VERBATIM
        )
    else()
        message(WARNING "lcov, genhtml или gcov/llvm-cov не найдены. Цель 'coverage' не будет доступна.")
    endif()
endif()

# --- Финальные информационные сообщения ---
message(STATUS "---------------------------------------------------------------------")
message(STATUS "Проект: ${PROJECT_NAME}")
message(STATUS "Тип сборки: ${CMAKE_BUILD_TYPE} (Используйте -DCMAKE_BUILD_TYPE=Debug или -DCMAKE_BUILD_TYPE=Release для явного указания)")
message(STATUS "Выходная директория для исполняемых файлов: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "Выходная директория для библиотек: ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}")
message(STATUS "Компилятор: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "---------------------------------------------------------------------")
message(STATUS "Команды для работы с проектом (после конфигурации CMake):")
message(STATUS "  Сборка проекта:           cmake --build . (или ${CMAKE_MAKE_PROGRAM} / ninja)")
message(STATUS "  Запуск модульных тестов:  ctest            (или ${CMAKE_MAKE_PROGRAM} test, или ./bin/run_unit_tests)")

if(UNIX AND BASH_EXECUTABLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/integration_tests/run_all_tests.sh")
    message(STATUS "  Запуск интеграц. тестов:  cd integration_tests && ./run_all_tests.sh (или через ctest, если настроено)")
endif()

if(DOXYGEN_FOUND)
    message(STATUS "  Генерация документации:   ${CMAKE_MAKE_PROGRAM} doxygen (или ninja doxygen)")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang") AND ENABLE_COVERAGE)
    if(LCOV_EXECUTABLE AND GENHTML_EXECUTABLE AND GCOV_TOOL)
        message(STATUS "  Генерация отчета покрытия: cmake --build . --target coverage (или ${CMAKE_MAKE_PROGRAM} coverage / ninja coverage) (только для Debug GCC/Clang)")
    else()
        message(STATUS "  Анализ покрытия кода включен (ENABLE_COVERAGE=ON), но необходимые инструменты (lcov, genhtml, gcov/llvm-cov) не найдены. Отчет не будет сгенерирован.")
    endif()
elseif(ENABLE_COVERAGE)
    message(STATUS "  Анализ покрытия кода включен (ENABLE_COVERAGE=ON), но доступен только для Debug-сборок с GCC/Clang.")
else()
    message(STATUS "  Анализ покрытия кода отключен (ENABLE_COVERAGE=OFF).")
endif()

message(STATUS "---------------------------------------------------------------------")
