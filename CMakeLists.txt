#-------------------------------------------------------------------------------
# CMakeLists.txt
# Главный файл системы сборки CMake для проекта "База Данных Интернет-Провайдера".
#
# Определяет структуру проекта, цели сборки (библиотеки, исполняемые файлы, тесты),
# зависимости между ними, флаги компиляции, а также интеграцию с Google Test,
# Doxygen для генерации документации и LCOV для анализа покрытия кода.
# Автор: Fedor Zilnitskiy
#-------------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.14)
project(InternetProviderDB_CS CXX) # Имя проекта для Клиент-Серверной версии

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF) # Отключаем расширения компилятора (например, GNU extensions) для лучшей переносимости

# Устанавливаем выходную директорию для исполняемых файлов и библиотек
# ${CMAKE_BINARY_DIR} - это директория сборки (например, 'build')
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib) # Для статических библиотек (.a, .lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib) # Для динамических библиотек (.so, .dll)

# --- Общие флаги компиляции ---
set(COMMON_CXX_FLAGS "")          # Общие флаги для всех компиляторов
set(COMMON_WARNING_FLAGS "")     # Флаги предупреждений
set(OPTIMIZATION_FLAGS "")       # Флаги оптимизации

if(MSVC)
    # Флаги для компилятора Microsoft Visual C++
    list(APPEND COMMON_WARNING_FLAGS /W4 /WX) # /W4 - высокий уровень предупреждений, /WX - трактовать предупреждения как ошибки
    # /GR - включить информацию о типах времени выполнения (RTTI)
    # /EHsc - модель обработки исключений C++ (синхронная)
    # /D_CRT_SECURE_NO_WARNINGS - отключить предупреждения о "небезопасных" функциях CRT (например, localtime)
    # /D_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING - для подавления предупреждений, связанных с std::filesystem в некоторых версиях
    list(APPEND COMMON_CXX_FLAGS /GR /EHsc /D_CRT_SECURE_NO_WARNINGS /D_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING) 
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        list(APPEND OPTIMIZATION_FLAGS /O2 /DNDEBUG) # /O2 - оптимизация по скорости, /DNDEBUG - отключить assert'ы
    elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND OPTIMIZATION_FLAGS /Od /Zi /D_DEBUG) # /Od - отключить оптимизацию, /Zi - отладочная информация, /D_DEBUG - макрос для отладки
    else() # По умолчанию (если CMAKE_BUILD_TYPE не задан явно, часто используется Release)
        list(APPEND OPTIMIZATION_FLAGS /O2 /DNDEBUG)
    endif()
else() # GCC/Clang
    # Обширный набор флагов предупреждений для GCC/Clang
    list(APPEND COMMON_WARNING_FLAGS
        -Wall -Wextra -Wpedantic # Основные группы предупреждений
        -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused
        -Woverloaded-virtual -Wconversion -Wsign-conversion -Wnull-dereference
        -Wdouble-promotion -Wformat=2 -Wformat-security
        -Wsuggest-override # Предлагать использовать 'override' для виртуальных функций
        -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wrestrict
        -Wzero-as-null-pointer-constant -Wctor-dtor-privacy
        -Wdelete-non-virtual-dtor # Предупреждать об удалении объекта через указатель на базовый класс без виртуального деструктора
        -Winvalid-offsetof
        -Wpessimizing-move -Wredundant-move -Wreorder 
        # -Wreturn-local-addr # Может давать ложные срабатывания с RVO (Return Value Optimization)
        -Wsuggest-final-methods -Wsuggest-final-types # Предлагать использовать 'final'
        # -Wcatch-value # Предупреждать о `catch (std::exception e)` вместо `catch (const std::exception& e)` (может быть спорным)
        -Wconditionally-supported # Предупреждать об использовании условно-поддерживаемых конструкций
    )
    # Раскомментируйте -Werror для CI или перед релизом, чтобы все предупреждения считались ошибками
    # list(APPEND COMMON_WARNING_FLAGS -Werror) 
    
    # Общие флаги для GCC/Clang
    # -std=c++17 - стандарт языка
    # -fPIC - Position Independent Code (необходимо для разделяемых библиотек, полезно и для статических)
    # -fvisibility=hidden - по умолчанию скрывать символы, явно экспортировать через __attribute__((visibility("default")))
    # -fvisibility-inlines-hidden - скрывать inline-функции
    # -pthread - поддержка многопоточности (для std::thread и др.)
    list(APPEND COMMON_CXX_FLAGS -std=c++17 -fPIC -fvisibility=hidden -fvisibility-inlines-hidden -pthread)

    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        list(APPEND OPTIMIZATION_FLAGS -O3 -DNDEBUG) # -O3 - максимальная оптимизация
    elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND OPTIMIZATION_FLAGS -g -O0) # -g - отладочная информация, -O0 - без оптимизации
    else() # По умолчанию (например, если CMAKE_BUILD_TYPE не задан)
        # Для случая "не Debug и не Release" можно выбрать что-то среднее или Release по умолчанию
        list(APPEND OPTIMIZATION_FLAGS -O2 -DNDEBUG -g) # -O2, но с отладочной информацией
    endif()
endif()

# --- Google Test (для модульного тестирования) ---
# Используем FetchContent для загрузки GTest во время конфигурации CMake
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip # Прямая ссылка на архив ZIP (более надежно, чем GIT_REPOSITORY для некоторых окружений)
  # Или через GIT:
  # GIT_REPOSITORY https://github.com/google/googletest.git
  # GIT_TAG release-1.14.0 # Указывайте актуальный стабильный тег
)
# Для MSVC, чтобы избежать проблем с линковкой CRT (C Runtime) в Debug/Release конфигурациях GTest
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE) 
FetchContent_MakeAvailable(googletest) # Загружает и делает цели GTest доступными
set(INSTALL_GTEST OFF) # Не устанавливать GTest вместе с проектом при 'make install'

# --- Общая статическая библиотека ядра и утилит (core_utils_lib) ---
# Исходные файлы библиотеки
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
# Заголовочные файлы библиотеки (для корректной работы IDE и зависимостей)
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
target_include_directories(core_utils_lib PUBLIC # PUBLIC, чтобы цели, линкующиеся с core_utils_lib, автоматически получали эти пути
    "${CMAKE_CURRENT_SOURCE_DIR}/src"        # Для доступа к common_defs.h из других модулей
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"   # Для доступа к заголовкам ядра
    "${CMAKE_CURRENT_SOURCE_DIR}/src/utils"  # Для доступа к заголовкам утилит
)
target_compile_features(core_utils_lib PUBLIC cxx_std_17) # Требуем C++17 для этой библиотеки
target_compile_options(core_utils_lib PRIVATE ${COMMON_CXX_FLAGS} ${COMMON_WARNING_FLAGS} ${OPTIMIZATION_FLAGS})

# Добавляем флаги для анализа покрытия кода (только для Debug GCC/Clang)
if(NOT MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    target_compile_options(core_utils_lib PRIVATE --coverage) # Флаг компилятора для генерации .gcno
    target_link_libraries(core_utils_lib INTERFACE --coverage) # Флаг линковщика для .gcda
endif()

# --- Библиотека сетевых утилит (net_lib) ---
set(NET_LIB_SOURCES src/net/tcp_socket.cpp)
set(NET_LIB_HEADERS src/net/tcp_socket.h)
add_library(net_lib STATIC ${NET_LIB_SOURCES} ${NET_LIB_HEADERS})
target_include_directories(net_lib PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/net"
    # TCPSocket использует Logger и common_defs, поэтому нужны пути к ним
    "${CMAKE_CURRENT_SOURCE_DIR}/src/utils" 
    "${CMAKE_CURRENT_SOURCE_DIR}/src"      
)
# net_lib зависит от core_utils_lib (из-за Logger и common_defs.h, которые теперь часть core_utils_lib)
target_link_libraries(net_lib PUBLIC core_utils_lib) 
target_compile_features(net_lib PUBLIC cxx_std_17)
target_compile_options(net_lib PRIVATE ${COMMON_CXX_FLAGS} ${COMMON_WARNING_FLAGS} ${OPTIMIZATION_FLAGS})
if(NOT MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    target_compile_options(net_lib PRIVATE --coverage)
    target_link_libraries(net_lib INTERFACE --coverage)
endif()
if(WIN32) # Для Windows необходимо линковать с библиотекой сокетов Ws2_32.lib
    target_link_libraries(net_lib PRIVATE ws2_32)
endif()

# --- Серверное приложение ---
set(SERVER_APP_SOURCES
    src/server/server_main.cpp
    src/server/server.cpp
    src/server/server_command_handler.cpp
    src/server/server_config.cpp 
)
set(SERVER_APP_HEADERS # Явное перечисление заголовочных файлов для лучшей организации
    src/server/server.h
    src/server/server_command_handler.h
    src/server/server_config.h   
)
add_executable(database_server ${SERVER_APP_SOURCES} ${SERVER_APP_HEADERS})
# Сервер зависит от core_utils_lib (включая ThreadPool, Logger, Database и др.) и net_lib (TCPSocket)
target_link_libraries(database_server PRIVATE core_utils_lib net_lib) 
target_include_directories(database_server PUBLIC # PUBLIC, если другие цели будут зависеть от заголовков сервера (маловероятно для exe)
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
# Клиент также зависит от core_utils_lib (для Logger, common_defs) и net_lib (TCPSocket)
target_link_libraries(database_client PRIVATE core_utils_lib net_lib) 
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
target_link_libraries(generate_data PRIVATE core_utils_lib) # Генератор использует классы из core и Logger из utils
target_include_directories(generate_data PUBLIC # Для доступа к заголовкам core, utils, common_defs
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/utils"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_features(generate_data PUBLIC cxx_std_17)
target_compile_options(generate_data PRIVATE ${COMMON_CXX_FLAGS} ${COMMON_WARNING_FLAGS} ${OPTIMIZATION_FLAGS})

# --- Модульные тесты ---
enable_testing() # Включаем поддержку CTest
set(UNIT_TEST_MAIN_SOURCES
    unit_tests/test_main.cpp # Главный файл тестов (main для GTest)
    unit_tests/test_date.cpp
    unit_tests/test_ip_address.cpp
    unit_tests/test_provider_record.cpp
    unit_tests/test_tariff_plan.cpp
    unit_tests/test_query_parser.cpp
    unit_tests/test_database.cpp
    unit_tests/test_file_utils.cpp
    unit_tests/test_logger.cpp        
    unit_tests/test_tcp_socket.cpp    
    unit_tests/test_server_command_handler.cpp 
    # Добавьте сюда файлы с тестами для новых компонентов:
    # unit_tests/test_thread_pool.cpp 
    # unit_tests/test_server_config.cpp 
)
add_executable(run_unit_tests ${UNIT_TEST_MAIN_SOURCES})
# Тесты зависят от тестируемых библиотек (core_utils_lib, net_lib) и GTest
target_link_libraries(run_unit_tests PRIVATE core_utils_lib net_lib GTest::gtest GTest::gtest_main)
target_include_directories(run_unit_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"      # Для common_defs.h
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core" # Для тестирования классов ядра
    "${CMAKE_CURRENT_SOURCE_DIR}/src/utils" # Для тестирования утилит (Logger, FileUtils, ThreadPool)
    "${CMAKE_CURRENT_SOURCE_DIR}/src/net"   # Для тестирования TCPSocket
    "${CMAKE_CURRENT_SOURCE_DIR}/src/server"# Для test_server_command_handler и test_server_config
    "${googletest_SOURCE_DIR}/googletest/include" # Путь к заголовкам GTest
    "${googletest_SOURCE_DIR}/googlemock/include" # Если используется GMock
)
target_compile_features(run_unit_tests PUBLIC cxx_std_17)
target_compile_options(run_unit_tests PRIVATE ${COMMON_CXX_FLAGS} ${COMMON_WARNING_FLAGS} ${OPTIMIZATION_FLAGS})
if(NOT MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    target_compile_options(run_unit_tests PRIVATE --coverage)
    target_link_libraries(run_unit_tests PRIVATE --coverage)
endif()

# Интеграция GTest с CTest для автоматического обнаружения тестов
include(GoogleTest)
gtest_discover_tests(run_unit_tests
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} # Указываем рабочую директорию для запуска тестов (где лежат исполняемые файлы)
)
# Можно также добавить тест вручную, если gtest_discover_tests не используется или для специфичных настроек
# add_test(NAME UnitTests COMMAND run_unit_tests)


# --- Doxygen (генерация документации) ---
find_package(Doxygen) # Поиск Doxygen в системе
if(DOXYGEN_FOUND)
    set(DOXYGEN_PROJECT_NAME "${PROJECT_NAME}")
    set(DOXYGEN_PROJECT_NUMBER "1.7.2_client_server_stage4_final") # Обновляем версию проекта для документации
    set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/doxygen_docs") # Куда генерировать документацию
    set(DOXYGEN_INPUT_DIRECTORIES # Директории с исходным кодом для документирования
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools" 
    )
    set(DOXYGEN_FILE_PATTERNS "*.h" "*.cpp") # Шаблоны файлов для документирования
    set(DOXYGEN_EXCLUDE_PATTERNS "*/build/*" "*/googletest/*" "*/_deps/*") # Исключить директории
    set(DOXYGEN_RECURSIVE YES)              # Рекурсивно обходить INPUT_DIRECTORIES
    set(DOXYGEN_GENERATE_LATEX NO)          # Не генерировать LaTeX документацию
    set(DOXYGEN_EXTRACT_ALL YES)            # Документировать все сущности, даже если нет явных комментариев Doxygen
    set(DOXYGEN_EXTRACT_PRIVATE NO)         # Не включать приватные члены классов в документацию (можно изменить на YES при необходимости)
    set(DOXYGEN_EXTRACT_STATIC YES)         # Включать статические члены
    set(DOXYGEN_WARN_IF_UNDOCUMENTED YES)   # Предупреждать о недокументированных сущностях (если EXTRACT_ALL = NO)
    set(DOXYGEN_WARN_LOGFILE "${CMAKE_BINARY_DIR}/doxygen_warnings.log") # Файл для лога предупреждений Doxygen

    # Если используется Graphviz/DOT для генерации диаграмм
    set(DOXYGEN_HAVE_DOT YES) # Указываем Doxygen, что DOT доступен (предполагается, что Graphviz установлен)
    set(DOXYGEN_USE_DOT YES)  # Включаем использование DOT
    # Можно настроить параметры DOT, например:
    # set(DOXYGEN_DOT_FONTNAME "Arial")
    # set(DOXYGEN_DOT_FONTSIZE "10")
    # set(DOXYGEN_CLASS_DIAGRAMS YES)
    # set(DOXYGEN_COLLABORATION_ल्यू YES) 
    # set(DOXYGEN_CALL_GRAPH YES)
    # set(DOXYGEN_CALLER_GRAPH YES)

    # Используем Doxyfile.in для настройки Doxygen
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in")
        configure_file( # Создаем Doxyfile из Doxyfile.in, заменяя переменные CMake (@VAR@)
            "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in"
            "${CMAKE_BINARY_DIR}/Doxyfile" 
            @ONLY # Заменять только переменные, обрамленные @...@
        ) 
        add_custom_target(doxygen ALL # Добавляем цель 'doxygen', которая будет выполняться при сборке 'all'
            COMMAND "${DOXYGEN_EXECUTABLE}" "${CMAKE_BINARY_DIR}/Doxyfile" # Команда для запуска Doxygen
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" # Рабочая директория для Doxygen
            COMMENT "Generating API documentation with Doxygen using configured Doxyfile. Output in ${DOXYGEN_OUTPUT_DIRECTORY}/html/"
            VERBATIM # Не экранировать аргументы команды
        )
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile") # Если есть Doxyfile, но нет Doxyfile.in
         add_custom_target(doxygen ALL
            COMMAND "${DOXYGEN_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" # Или CMAKE_BINARY_DIR, в зависимости от настроек в Doxyfile
            COMMENT "Generating API documentation with Doxygen using existing Doxyfile. Output in ${DOXYGEN_OUTPUT_DIRECTORY}/ (check Doxyfile for actual output path)"
            VERBATIM
        )
    else() # Если нет ни Doxyfile.in, ни Doxyfile
        message(WARNING "Doxyfile or Doxyfile.in not found. Doxygen target may not be fully effective. Consider creating one, or use CMake's Doxygen module for basic Doxygen configuration if find_package(Doxygen MODULE) is used.")
        # Пример использования модуля Doxygen CMake (если find_package(Doxygen MODULE ...)):
        # doxyfile_generate(DOXYFILE_CONFIG_FILE doxyfile_config)
        # add_doxygen_target(doxygen_target ${DOXYFILE_CONFIG_FILE})
    endif()
else() # DOXYGEN_FOUND == FALSE
    message(WARNING "Doxygen not found. Documentation target 'doxygen' will not be available.")
endif()

# --- Интеграционные тесты (shell-скрипты для UNIX-подобных систем) ---
# Убедитесь, что скрипты run_all_tests.sh и test_common.sh адаптированы для клиент-серверного взаимодействия
if(UNIX) # Эти тесты обычно представляют собой shell-скрипты, работающие на UNIX-подобных системах
    find_program(BASH_EXECUTABLE bash) # Ищем интерпретатор bash
    if(BASH_EXECUTABLE)
        set(INTEGRATION_TEST_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/integration_tests/run_all_tests.sh")
        if(EXISTS "${INTEGRATION_TEST_SCRIPT}")
            add_test( # Добавляем интеграционные тесты как один тест в CTest
                NAME IntegrationTestsRunner # Имя теста в CTest
                COMMAND "${BASH_EXECUTABLE}" "${INTEGRATION_TEST_SCRIPT}" # Команда для запуска
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/integration_tests" # Рабочая директория для скрипта
            )
            # Указываем, что интеграционные тесты зависят от сборки сервера и клиента
            set_tests_properties(IntegrationTestsRunner PROPERTIES DEPENDS "database_server;database_client")
            message(STATUS "Интеграционные тесты (через ${INTEGRATION_TEST_SCRIPT}) добавлены в CTest.")
        else()
            message(WARNING "Скрипт интеграционных тестов ${INTEGRATION_TEST_SCRIPT} не найден. Интеграционные тесты не будут добавлены.")
        endif()
    else()
        message(WARNING "Интерпретатор Bash не найден. Интеграционные тесты (shell-скрипты) не будут добавлены.")
    endif()
else() # Не UNIX (например, Windows)
    message(STATUS "Интеграционные тесты (на основе shell-скриптов) настроены для UNIX-подобных систем. На Windows они могут не работать без дополнительной настройки (например, использование WSL, Cygwin, или адаптация скриптов под PowerShell/CMD).")
endif()

# ... (весь предыдущий код CMakeLists.txt до блока ENABLE_COVERAGE) ...

# --- Настройка Code Coverage с LCOV (Только для Debug на GCC/Clang) ---
option(ENABLE_COVERAGE "Enable code coverage reporting (Debug GCC/Clang only)" OFF)

if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang") AND ENABLE_COVERAGE)
    # (весь блок if(LCOV_EXECUTABLE AND GENHTML_EXECUTABLE AND GCOV_TOOL) ... else() ... endif() как был)
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
        
        add_custom_target(coverage_clean ALL 
            COMMAND "${CMAKE_COMMAND}" -E remove_directory "${COVERAGE_OUTPUT_DIR_BASE}" 
            COMMAND find "${CMAKE_BINARY_DIR}" -name "*.gcda" -type f -delete 
            COMMENT "Очистка предыдущих данных и отчетов о покрытии кода."
            VERBATIM
        )
        
        set(LCOV_BASE_PARAMS
            --capture                               
            --directory "${CMAKE_BINARY_DIR}"       
            --base-directory "${CMAKE_SOURCE_DIR}"  
            --gcov-tool "${GCOV_TOOL}"              
            --no-external                           
        )
        set(LCOV_REMOVE_PATTERNS
            "/usr/*"                                
            "*/googletest/*"                        
            "*/_deps/*"                             
            "*/unit_tests/*"                        
            "*/tools/generator.cpp"                 
        )

        set(COVERAGE_INFO_ALL "${COVERAGE_OUTPUT_DIR_BASE}/total_coverage.info")
        set(COVERAGE_HTML_ALL "${COVERAGE_OUTPUT_DIR_BASE}/html_total")

        add_custom_target(coverage ALL 
            COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" -C "${CMAKE_BUILD_TYPE}" --output-on-failure
            COMMAND "${LCOV_EXECUTABLE}" ${LCOV_BASE_PARAMS} --output-file "${COVERAGE_INFO_ALL}.initial"
            COMMAND "${LCOV_EXECUTABLE}" --remove "${COVERAGE_INFO_ALL}.initial" ${LCOV_REMOVE_PATTERNS} --output-file "${COVERAGE_INFO_ALL}"
            COMMAND "${GENHTML_EXECUTABLE}" "${COVERAGE_INFO_ALL}" --output-directory "${COVERAGE_HTML_ALL}" --title "Total Code Coverage" --legend --show-details --demangle-cpp
            COMMENT "Генерация полного отчета о покрытии кода. Отчет будет в ${COVERAGE_HTML_ALL}/index.html"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" 
            DEPENDS coverage_clean run_unit_tests IntegrationTestsRunner 
            VERBATIM
        )
        # Сообщение о доступности цели coverage теперь будет в общем блоке message(STATUS...)
    else() 
        message(WARNING "lcov, genhtml или gcov/llvm-cov не найдены, хотя анализ покрытия включен и условия сборки подходящие. Цель 'coverage' не будет доступна.")
        
        
        if (NOT LCOV_EXECUTABLE)         
        message(WARNING "  lcov не найден.") 
        
        endif()
        
        if(NOT GENHTML_EXECUTABLE) 
        message(WARNING "  genhtml не найден.") 
        endif()
        if(NOT GCOV_TOOL) 
        message(WARNING "  gcov (для GCC) или llvm-cov (для Clang) не найден.") 
        endif()
    endif()
else() 
    # Этот блок выполняется, если покрытие НЕ включено ИЛИ условия сборки не подходят
    # (т.е. CMAKE_BUILD_TYPE не Debug, или компилятор не GCC/Clang, или ENABLE_COVERAGE=OFF)
    # Не нужно выводить сообщения здесь, если ENABLE_COVERAGE=OFF, так как это ожидаемое поведение.
    # Сообщения о несоответствии типа сборки или компилятора выведем ниже, если ENABLE_COVERAGE=ON.
    if(ENABLE_COVERAGE) # Только если пользователь явно включил покрытие, но условия не совпали
        if(NOT (CMAKE_BUILD_TYPE STREQUAL "Debug"))
            # Это сообщение не должно быть здесь, т.к. внешний if уже проверяет CMAKE_BUILD_TYPE
        endif() 
        if(NOT (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
            # И это тоже
        endif()
    endif()
endif() 

# Вот этот endif закрывает if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang") AND ENABLE_COVERAGE)

# --- КОНЕЦ НАСТРОЙКИ CODE COVERAGE ---

# --- Финальные информационные сообщения ---
# Они должны быть ВНЕ каких-либо блоков if/else, относящихся к ENABLE_COVERAGE,
# чтобы всегда выводиться при конфигурации.

message(STATUS "---------------------------------------------------------------------")

message(STATUS "Проект: ${PROJECT_NAME}")
message(STATUS "Тип сборки: ${CMAKE_BUILD_TYPE} (Используйте -DCMAKE_BUILD_TYPE=Debug или -DCMAKE_BUILD_TYPE=Release для явного указания)")
message(STATUS "Выходная директория для исполняемых файлов: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "Выходная директория для библиотек: ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}")
message(STATUS "Компилятор: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "---------------------------------------------------------------------")
message(STATUS "Команды для работы с проектом (после конфигурации CMake):")
message(STATUS "  Сборка проекта:           cmake --build .  (или ${CMAKE_MAKE_PROGRAM} / ninja)")
message(STATUS "  Запуск модульных тестов:  ctest            (или ${CMAKE_MAKE_PROGRAM} test, или ./bin/run_unit_tests)")

if(UNIX AND BASH_EXECUTABLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/integration_tests/run_all_tests.sh")
    message(STATUS "  Запуск интеграц. тестов:  cd integration_tests && ./run_all_tests.sh (или через ctest, если настроено)")
endif()

if(DOXYGEN_FOUND)
    message(STATUS "  Генерация документации:   ${CMAKE_MAKE_PROGRAM} doxygen (или ninja doxygen)")
endif()

# Проверяем условия для вывода сообщения о coverage здесь, чтобы оно было консистентным
# и выводилось только если цель 'coverage' действительно была добавлена.
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang") AND ENABLE_COVERAGE)
    if(LCOV_EXECUTABLE AND GENHTML_EXECUTABLE AND GCOV_TOOL) # Проверяем, что инструменты действительно найдены
        message(STATUS "  Генерация отчета покрытия: ${CMAKE_MAKE_PROGRAM} coverage (или ninja coverage) (только для Debug GCC/Clang)")
    else()
        message(STATUS "  Анализ покрытия кода включен (ENABLE_COVERAGE=ON), но необходимые инструменты (lcov, genhtml, gcov/llvm-cov) не найдены. Отчет не будет сгенерирован.")
    endif()
elif(ENABLE_COVERAGE) # Если ENABLE_COVERAGE=ON, но условия сборки не Debug или компилятор не тот
    message(STATUS "  Анализ покрытия кода включен (ENABLE_COVERAGE=ON), но доступен только для Debug-сборок с GCC/Clang.")
    if(NOT (CMAKE_BUILD_TYPE STREQUAL "Debug"))
            message(STATUS "    Текущий тип сборки: ${CMAKE_BUILD_TYPE} (требуется Debug)")
    endif() 
    if(NOT (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
            message(STATUS "    Текущий компилятор: ${CMAKE_CXX_COMPILER_ID} (требуется GCC или Clang)")
    endif()
else() # ENABLE_COVERAGE=OFF
    message(STATUS "  Анализ покрытия кода отключен (ENABLE_COVERAGE=OFF).")
endif()

message(STATUS "---------------------------------------------------------------------")
