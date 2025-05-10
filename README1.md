InternetProviderDB/
├── CMakeLists.txt                 # Главный CMake-файл проекта (изменен)
├── README.md                      # Описание проекта (будет сгенерировано новое)
│
├── data/                          # Директория для данных по умолчанию и рабочих данных
│   ├── provider_data.txt          # Файл данных абонентов (создается генератором)
│   ├── tariff.cfg                 # Файл конфигурации тарифов (основной)
│   └── tariff_alt.cfg             # Альтернативный файл тарифов (для тестов standalone и single_client)
│
├── src/                           # Исходные коды основного приложения
│   ├── common_defs.h              # Общие определения, утилиты (не изменялся)
│   ├── date.h                     # (не изменялся)
│   ├── date.cpp                   # (не изменялся)
│   ├── ip_address.h               # (не изменялся)
│   ├── ip_address.cpp             # (не изменялся)
│   ├── provider_record.h          # (не изменялся)
│   ├── provider_record.cpp        # (не изменялся)
│   ├── tariff_plan.h              # (не изменялся)
│   ├── tariff_plan.cpp            # (не изменялся)
│   ├── query_parser.h             # (не изменялся)
│   ├── query_parser.cpp           # (не изменялся)
│   ├── database.h                 # (изменен - добавлен update_record)
│   ├── database.cpp               # (изменен - реализован update_record)
│   │
│   ├── network/                   # Сетевые компоненты
│   │   ├── network_protocol.h     # (изменен - добавлен EDIT_RECORD_CMD)
│   │   ├── socket_utils.h         # (не изменялся)
│   │   └── socket_utils.cpp       # (не изменялся)
│   │
│   ├── server/                    # Компоненты сервера
│   │   ├── db_server.h            # (не изменялся)
│   │   ├── db_server.cpp          # (изменен - логика EDIT, CALCULATE_BILL, LOAD_TARIFF для HTTP и custom proto)
│   │   └── server_main.cpp        # (не изменялся)
│   │
│   ├── client/                    # Компоненты клиента
│   │   ├── db_client.h            # (изменен - добавлены методы для EDIT)
│   │   ├── db_client.cpp          # (изменен - реализованы методы для EDIT, поддержка RAW_EDIT_COMMAND в batch)
│   │   └── client_main.cpp        # (не изменялся)
│   │
│   ├── user_interface.h           # Консольный UI для монолитного приложения (не изменялся)
│   ├── user_interface.cpp         # (не изменялся)
│   └── main.cpp                   # Точка входа для provider_app_standalone (не изменялся)
│
├── tools/                         # Вспомогательные инструменты
│   └── generator.cpp              # Генератор тестовых данных provider_data.txt (не изменялся)
│
└── tests/                         # Директория для всех тестов
    │
    ├── unit/                      # Модульные тесты (gtest)
    │   ├── test_common_defs.cpp   # (изменен)
    │   ├── test_date.cpp          # (не изменялся)
    │   ├── test_ip_address.cpp    # (не изменялся)
    │   ├── test_tariff_plan.cpp   # (изменен)
    │   ├── test_provider_record.cpp # (изменен)
    │   ├── test_query_parser.cpp  # (изменен)
    │   └── test_database_integration.cpp # (изменен)
    │
    └── system/                    # Системные/E2E тесты
        ├── scripts/               # Скрипты для запуска системных тестов
        │   ├── run_standalone_batch_test.sh      # (изменен - обработка timestamp)
        │   ├── run_client_server_single_test.sh  # (изменен - обработка timestamp, новые команды EDIT)
        │   ├── run_client_server_multi_test.sh   # (изменен - обработка timestamp, параметризация для сценариев A-F)
        │   ├── run_http_tests.sh                 # (изменен - тесты EDIT, CALCULATE_BILL, LOAD_TARIFF, безопасность)
        │   ├── check_generated_data.sh           # (НОВЫЙ)
        │   ├── run_standalone_robustness_test.sh # (НОВЫЙ)
        │   └── run_server_robustness_test.sh     # (НОВЫЙ)
        │
        ├── test_data_standalone/    # Данные для тестов provider_app_standalone
        │   ├── standalone_db_initial.txt         # (не изменялся)
        │   ├── standalone_tariff.cfg           # (не изменялся)
        │   ├── tariff_alt.cfg                  # (не изменялся, но используется)
        │   ├── standalone_queries.txt            # (изменен - добавлены SELECT с полями/сортировкой, ошибочные команды)
        │   ├── standalone_expected_output.txt    # (изменен)
        │   └── standalone_db_expected_final.txt  # (не изменялся)
        │
        ├── test_data_cs_single/     # Данные для тестов одного клиента с сервером
        │   ├── server_db_initial.txt             # (не изменялся - если используется общий)
        │   ├── server_tariff.cfg               # (не изменялся - если используется общий)
        │   ├── tariff_alt.cfg                  # (не изменялся, но используется)
        │   ├── client_scenario_single.txt        # (изменен - добавлены EDIT, SELECT с полями/сортировкой, тесты на регистр/спецсимволы)
        │   ├── client_scenario_single_expected_output.txt # (изменен)
        │   └── server_db_expected_final_single.txt        # (изменен)
        │
        ├── test_data_cs_multi/      # Данные для тестов нескольких клиентов с сервером
        │   ├── server_db_initial.txt             # (изменен - добавлена "Race Condition Target")
        │   ├── server_tariff.cfg               # (не изменялся)
        │   ├── tariff_alt_concurrent.cfg       # (НОВЫЙ)
        │   │
        │   ├── client_scenario_multi_A1.txt      # (НОВЫЙ - Сценарий A)
        │   ├── client_scenario_multi_A1_expected_output.txt # (НОВЫЙ)
        │   ├── client_scenario_multi_A2.txt      # (НОВЫЙ - Сценарий A)
        │   ├── client_scenario_multi_A2_expected_output.txt # (НОВЫЙ)
        │   ├── client_scenario_multi_A3.txt      # (НОВЫЙ - Сценарий A)
        │   ├── client_scenario_multi_A3_expected_output.txt # (НОВЫЙ)
        │   ├── server_db_expected_final_multi_A.txt # (НОВЫЙ)
        │   │
        │   ├── client_scenario_multi_B1.txt      # (НОВЫЙ - Сценарий B)
        │   ├── client_scenario_multi_B1_expected_output.txt # (НОВЫЙ)
        │   ├── client_scenario_multi_B2.txt      # (НОВЫЙ - Сценарий B)
        │   ├── client_scenario_multi_B2_expected_output.txt # (НОВЫЙ)
        │   ├── client_scenario_multi_B3.txt      # (НОВЫЙ - Сценарий B)
        │   ├── client_scenario_multi_B3_expected_output.txt # (НОВЫЙ)
        │   ├── server_db_expected_final_multi_B.txt # (НОВЫЙ)
        │   │
        │   ├── client_scenario_multi_C1.txt      # (НОВЫЙ/Переписан - Сценарий C)
        │   ├── client_scenario_multi_C1_expected_output.txt # (НОВЫЙ/Переписан)
        │   ├── client_scenario_multi_C2.txt      # (НОВЫЙ/Переписан - Сценарий C)
        │   ├── client_scenario_multi_C2_expected_output.txt # (НОВЫЙ/Переписан)
        │   ├── client_scenario_multi_C3.txt      # (НОВЫЙ/Переписан - Сценарий C)
        │   ├── client_scenario_multi_C3_expected_output.txt # (НОВЫЙ/Переписан)
        │   ├── server_db_expected_final_multi_C.txt # (НОВЫЙ/Переписан)
        │   │
        │   ├── client_scenario_multi_D1.txt      # (НОВЫЙ - Сценарий D)
        │   ├── client_scenario_multi_D1_expected_output.txt # (НОВЫЙ)
        │   ├── client_scenario_multi_D2.txt      # (НОВЫЙ - Сценарий D)
        │   ├── client_scenario_multi_D2_expected_output.txt # (НОВЫЙ)
        │   ├── client_scenario_multi_D3.txt      # (НОВЫЙ - Сценарий D)
        │   ├── client_scenario_multi_D3_expected_output.txt # (НОВЫЙ)
        │   ├── server_db_expected_final_multi_D.txt # (НОВЫЙ)
        │   │
        │   ├── client_scenario_multi_E1.txt      # (НОВЫЙ - Сценарий E)
        │   ├── client_scenario_multi_E1_expected_output.txt # (НОВЫЙ)
        │   ├── client_scenario_multi_E2.txt      # (НОВЫЙ - Сценарий E)
        │   ├── client_scenario_multi_E2_expected_output.txt # (НОВЫЙ)
        │   ├── client_scenario_multi_E3.txt      # (НОВЫЙ - Сценарий E)
        │   ├── client_scenario_multi_E3_expected_output.txt # (НОВЫЙ)
        │   ├── server_db_expected_final_multi_E.txt # (НОВЫЙ)
        │   │
        │   ├── client_scenario_multi_F1.txt      # (НОВЫЙ - Сценарий F)
        │   ├── client_scenario_multi_F1_expected_output.txt # (НОВЫЙ)
        │   ├── client_scenario_multi_F2.txt      # (НОВЫЙ - Сценарий F)
        │   ├── client_scenario_multi_F2_expected_output.txt # (НОВЫЙ)
        │   ├── client_scenario_multi_F3.txt      # (НОВЫЙ - Сценарий F)
        │   ├── client_scenario_multi_F3_expected_output.txt # (НОВЫЙ)
        │   ├── server_db_expected_final_multi_F.txt # (НОВЫЙ)
        │
        └── test_data_http/          # Данные для HTTP тестов
            ├── server_db_initial.txt             # (не изменялся - если используется общий)
            └── server_tariff.cfg               # (не изменялся - если используется общий)
            └── tariff_alt_http.cfg             # (НОВЫЙ - создается в run_http_tests.sh)
