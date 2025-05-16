cmake_minimum_required(VERSION 3.14)
project(InternetProviderDB_CS CXX) # Имя проекта для Клиент-Серверной версии

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

# Устанавливаем выходную директорию для исполняемых файлов и библиотек
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# --- Общие флаги компиляции ---
set(COMMON_CXX_FLAGS "")
set(COMMON_WARNING_FLAGS "")
set(OPTIMIZATION_FLAGS "")

if(MSVC)
    list(APPEND COMMON_WARNING_FLAGS /W4 /WX)
    list(APPEND COMMON_CXX_FLAGS /GR /EHsc /D_CRT_SECURE_NO_WARNINGS) # /D_CRT_SECURE_NO_WARNINGS для localtime
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        list(APPEND OPTIMIZATION_FLAGS /O2 /DNDEBUG)
    elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND OPTIMIZATION_FLAGS /Od /Zi /D_DEBUG)
    else() # По умолчанию Release
        list(APPEND OPTIMIZATION_FLAGS /O2 /DNDEBUG)
    endif()
else() # GCC/Clang
    list(APPEND COMMON_WARNING_FLAGS
        -Wall -Wextra -Wpedantic
        -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused
        -Woverloaded-virtual -Wconversion -Wsign-conversion -Wnull-dereference
        -Wdouble-promotion -Wformat=2 -Wformat-security
        -Wsuggest-override
        -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wrestrict
        -Wzero-as-null-pointer-constant -Wctor-dtor-privacy
        -Wdelete-non-virtual-dtor 
        -Winvalid-offsetof
        -Wpessimizing-move -Wredundant-move -Wreorder -Wreturn-local-addr
        -Wsuggest-final-methods -Wsuggest-final-types -Wcatch-value
        -Wconditionally-supported
    )
    list(APPEND COMMON_CXX_FLAGS -std=c++17 -fPIC -fvisibility=hidden -fvisibility-inlines-hidden -pthread)
    list(APPEND COMMON_WARNING_FLAGS -Werror) # Предупреждения как ошибки

    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        list(APPEND OPTIMIZATION_FLAGS -O3 -DNDEBUG)
    elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND OPTIMIZATION_FLAGS -g -O0) 
    else() # По умолчанию Release
        list(APPEND OPTIMIZATION_FLAGS -O3 -DNDEBUG -g) 
    endif()
endif()

# --- Google Test ---
include(FetchContent)
FetchContent_Declare(
  googletest
  #GIT_REPOSITORY https://github.com/google/googletest.git
  #GIT_TAG        release-1.14.0 # Используйте актуальный стабильный тег
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip # Прямая ссылка на архив
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE) # Для MSVC
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
    "${CMAKE_CURRENT_SOURCE_DIR}/src/utils" # Logger используется в TCPSocket
    "${CMAKE_CURRENT_SOURCE_DIR}/src"      # common_defs.h
)
target_link_libraries(net_lib PRIVATE core_utils_lib) # Зависимость от Logger из core_utils_lib
target_compile_features(net_lib PUBLIC cxx_std_17)
target_compile_options(net_lib PRIVATE ${COMMON_CXX_FLAGS} ${COMMON_WARNING_FLAGS} ${OPTIMIZATION_FLAGS})
if(NOT MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    target_compile_options(net_lib PRIVATE --coverage)
    target_link_libraries(net_lib INTERFACE --coverage)
endif()
if(WIN32) # Для Windows линкуем с библиотекой сокетов
    target_link_libraries(net_lib PRIVATE ws2_32)
endif()

# --- Серверное приложение ---
set(SERVER_APP_SOURCES
    src/server/server_main.cpp
    src/server/server.cpp
    src/server/server_command_handler.cpp
)
add_executable(database_server ${SERVER_APP_SOURCES})
target_link_libraries(database_server PRIVATE core_utils_lib net_lib)
target_include_directories(database_server PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/utils"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/net"
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
target_link_libraries(database_client PRIVATE net_lib core_utils_lib)
target_include_directories(database_client PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client" # Если будут специфичные для клиента заголовки
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
    unit_tests/test_logger.cpp          # Новый тест
    unit_tests/test_tcp_socket.cpp      # Новый тест
    unit_tests/test_server_command_handler.cpp # Новый тест
)
add_executable(run_unit_tests ${UNIT_TEST_MAIN_SOURCES})
target_link_libraries(run_unit_tests PRIVATE core_utils_lib net_lib GTest::gtest GTest::gtest_main)
target_include_directories(run_unit_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/utils"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/net"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server" # Для test_server_command_handler
    "${googletest_SOURCE_DIR}/googletest/include"
    "${googletest_SOURCE_DIR}/googlemock/include" # Если будете использовать GMock
)
target_compile_features(run_unit_tests PUBLIC cxx_std_17)
target_compile_options(run_unit_tests PRIVATE ${COMMON_CXX_FLAGS} ${COMMON_WARNING_FLAGS} ${OPTIMIZATION_FLAGS})
if(NOT MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    target_compile_options(run_unit_tests PRIVATE --coverage)
    target_link_libraries(run_unit_tests PRIVATE --coverage)
endif()

include(GoogleTest)
gtest_discover_tests(run_unit_tests
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} # Тесты запускаются из bin/
)
add_test(NAME UnitTests COMMAND run_unit_tests)


# --- Doxygen ---
find_package(Doxygen)
if(DOXYGEN_FOUND)
    set(DOXYGEN_PROJECT_NAME "${PROJECT_NAME}")
    set(DOXYGEN_PROJECT_NUMBER "1.6_client_server_final_stage4") # Обновляем версию
    set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/doxygen_docs")
    set(DOXYGEN_INPUT_DIRECTORIES
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools"
    )
    set(DOXYGEN_EXCLUDE_PATTERNS "*/build/*" "*/googletest/*" "*/_deps/*")
    set(DOXYGEN_RECURSIVE YES)
    set(DOXYGEN_GENERATE_LATEX NO)
    set(DOXYGEN_EXTRACT_ALL YES) 
    set(DOXYGEN_EXTRACT_PRIVATE NO)
    set(DOXYGEN_EXTRACT_STATIC YES)
    set(DOXYGEN_WARN_IF_UNDOCUMENTED YES)
    set(DOXYGEN_WARN_LOGFILE "${CMAKE_BINARY_DIR}/doxygen_warnings.log")

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in")
        set(DOXYGEN_USE_DOT YES)
        set(DOXYGEN_DOT_FONTNAME "Arial")
        set(DOXYGEN_DOT_FONTSIZE "10")
        set(DOXYGEN_HAVE_DOT YES)

        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in"
            "${CMAKE_BINARY_DIR}/Doxyfile"
            @ONLY) 
        add_custom_target(doxygen ALL # Собирать документацию всегда при сборке "all"
            COMMAND "${DOXYGEN_EXECUTABLE}" "${CMAKE_BINARY_DIR}/Doxyfile"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMENT "Generating API documentation with Doxygen using configured Doxyfile. Output in ${DOXYGEN_OUTPUT_DIRECTORY}/html/"
            VERBATIM
        )
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile")
         add_custom_target(doxygen ALL
            COMMAND "${DOXYGEN_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            COMMENT "Generating API documentation with Doxygen using Doxyfile. Output in ${CMAKE_BINARY_DIR}/doxygen_docs/ (check Doxyfile for actual path)"
            VERBATIM
        )
    else()
        message(WARNING "Doxyfile or Doxyfile.in not found. Doxygen target may not be fully effective.")
    endif()
else()
    message(WARNING "Doxygen not found. 'doxygen' target will not be available.")
endif()

# --- Интеграционные тесты ---
# (Убедитесь, что test_common.sh и сценарии адаптированы для клиент-сервера)
if(UNIX) # Эти тесты обычно являются shell-скриптами
    find_program(BASH_EXECUTABLE bash)
    if(BASH_EXECUTABLE)
        set(INTEGRATION_TEST_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/integration_tests/run_all_tests.sh")
        if(EXISTS "${INTEGRATION_TEST_SCRIPT}")
            add_test(
                NAME IntegrationTestsRunner
                COMMAND "${BASH_EXECUTABLE}" "${INTEGRATION_TEST_SCRIPT}"
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/integration_tests"
            )
            # Зависимости: тесты должны запускаться после сборки сервера и клиента
            set_tests_properties(IntegrationTestsRunner PROPERTIES DEPENDS database_server database_client)
            message(STATUS "Интеграционные тесты (через ${INTEGRATION_TEST_SCRIPT}) добавлены.")
        else()
            message(WARNING "Скрипт интеграционных тестов ${INTEGRATION_TEST_SCRIPT} не найден.")
        endif()
    else()
        message(WARNING "Bash не найден. Интеграционные тесты (shell-скрипты) не будут добавлены.")
    endif()
else()
    message(STATUS "Интеграционные тесты (shell-скрипты) настроены для Unix-подобных систем. На Windows они могут не работать.")
endif()


# --- Настройка Code Coverage с LCOV (Только для Debug на GCC/Clang) ---
option(ENABLE_COVERAGE "Enable code coverage reporting (Debug GCC/Clang only)" OFF)
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang") AND ENABLE_COVERAGE)
    find_program(LCOV_EXECUTABLE lcov)
    find_program(GENHTML_EXECUTABLE genhtml)
    # GCOV_TOOL уже должен быть известен CMake или его можно найти явно
    if(NOT GCOV_TOOL AND CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        find_program(GCOV_TOOL gcov NAMES gcov-${CMAKE_CXX_COMPILER_VERSION} gcov)
    elseif(NOT GCOV_TOOL AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        find_program(GCOV_TOOL llvm-cov NAMES llvm-cov-${CMAKE_CXX_COMPILER_VERSION} llvm-cov)
    endif()

    if(LCOV_EXECUTABLE AND GENHTML_EXECUTABLE AND GCOV_TOOL)
        message(STATUS "Coverage tools found: LCOV=${LCOV_EXECUTABLE}, GENHTML=${GENHTML_EXECUTABLE}, GCOV=${GCOV_TOOL}")
        set(COVERAGE_OUTPUT_DIR_BASE "${CMAKE_BINARY_DIR}/coverage_report")
        
        add_custom_target(coverage_clean ALL # Запускать очистку всегда перед генерацией покрытия
            COMMAND "${CMAKE_COMMAND}" -E remove_directory "${COVERAGE_OUTPUT_DIR_BASE}"
            COMMAND find "${CMAKE_BINARY_DIR}" -name "*.gcda" -type f -delete # Удаляем только .gcda, .gcno нужны для lcov
            COMMENT "Cleaning up previous coverage data and reports."
            VERBATIM
        )
        
        set(LCOV_BASE_PARAMS
            --capture
            --directory "${CMAKE_BINARY_DIR}" 
            --base-directory "${CMAKE_SOURCE_DIR}"
            --gcov-tool "${GCOV_TOOL}"
            --no-external # Исключаем системные заголовки и внешние библиотеки
        )
        set(LCOV_REMOVE_PATTERNS
            "/usr/*" "*/googletest/*" "*/_deps/*"
            "*/unit_tests/*" # Сами тесты (test_*.cpp)
            "*/tools/generator.cpp" # Генератор данных, если его не нужно покрывать
            "*/src/client/*" # Можно исключить клиент, если фокус на сервере
        )

        # Цель для генерации общего отчета по покрытию
        set(COVERAGE_INFO_ALL "${COVERAGE_OUTPUT_DIR_BASE}/total_coverage.info")
        set(COVERAGE_HTML_ALL "${COVERAGE_OUTPUT_DIR_BASE}/html_total")
        add_custom_target(coverage ALL # Запускать после тестов
            # Сначала запускаем тесты, чтобы сгенерировать .gcda файлы
            # Важно: CTest должен быть настроен на запуск из CMAKE_BINARY_DIR
            COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" -C "${CMAKE_BUILD_TYPE}" --output-on-failure
            # Затем собираем данные покрытия
            COMMAND "${LCOV_EXECUTABLE}" ${LCOV_BASE_PARAMS} --output-file "${COVERAGE_INFO_ALL}.initial"
            COMMAND "${LCOV_EXECUTABLE}" --remove "${COVERAGE_INFO_ALL}.initial" ${LCOV_REMOVE_PATTERNS} --output-file "${COVERAGE_INFO_ALL}"
            COMMAND "${GENHTML_EXECUTABLE}" "${COVERAGE_INFO_ALL}" --output-directory "${COVERAGE_HTML_ALL}" --title "Total Code Coverage" --legend --show-details --demangle-cpp
            COMMENT "Generating total code coverage report. Output in ${COVERAGE_HTML_ALL}/index.html"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" # Для корректной работы lcov и genhtml
            DEPENDS coverage_clean run_unit_tests IntegrationTestsRunner # Зависит от очистки и выполнения всех тестов
            VERBATIM
        )
        message(STATUS "Code coverage target 'coverage' added (Debug GCC/Clang only).")
        message(STATUS "To generate coverage: build in Debug, run tests, then 'make coverage' or 'ninja coverage'.")
    else()
        message(WARNING "lcov, genhtml или gcov не найдены. Цель 'coverage' не будет доступна.")
        if(NOT LCOV_EXECUTABLE) message(WARNING "lcov not found.") endif()
        if(NOT GENHTML_EXECUTABLE) message(WARNING "genhtml not found.") endif()
        if(NOT GCOV_TOOL) message(WARNING "gcov/llvm-cov not found.") endif()
    endif()
else()
    if(NOT ENABLE_COVERAGE)
         message(STATUS "Code coverage reporting is disabled (ENABLE_COVERAGE=OFF).")
    else()
        if(NOT (CMAKE_BUILD_TYPE STREQUAL "Debug"))
            message(STATUS "Code coverage target is only available for Debug builds. Current build type: ${CMAKE_BUILD_TYPE}")
        endif() 
        if(NOT (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
             message(STATUS "Code coverage target is only available for GCC/Clang compilers. Current compiler: ${CMAKE_CXX_COMPILER_ID}")
        endif()
    endif()
endif()
# --- КОНЕЦ НАСТРОЙКИ CODE COVERAGE ---

message(STATUS "---------------------------------------------------------------------")
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE} (Use -DCMAKE_BUILD_TYPE=Debug or -DCMAKE_BUILD_TYPE=Release)")
message(STATUS "Output executables in: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "Output libraries in: ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "To build: cmake --build .")
message(STATUS "To run unit tests: ctest (after build) or ./bin/run_unit_tests")
message(STATUS "To run integration tests: ./integration_tests/run_all_tests.sh (after build)")
if(DOXYGEN_FOUND)
    message(STATUS "To generate Doxygen docs: make doxygen (or ninja doxygen)")
endif()
if(ENABLE_COVERAGE AND LCOV_EXECUTABLE AND GENHTML_EXECUTABLE AND GCOV_TOOL AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    message(STATUS "To generate coverage report (Debug GCC/Clang): make coverage (or ninja coverage)")
endif()
message(STATUS "---------------------------------------------------------------------")
