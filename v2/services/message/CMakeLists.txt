add_library(message_store
    src/message_store.cc
    src/mysql_message_repository.cc
    src/indexed_message_repository.cc
)
target_include_directories(message_store PUBLIC include)
target_link_libraries(message_store PUBLIC chat_contracts chat_infrastructure)

add_library(message_file_client
    src/file_client.cc
)
target_include_directories(message_file_client PUBLIC include)
target_link_libraries(message_file_client
    PUBLIC
        chat_brpc
        chat_contracts
        chat_infrastructure
)

add_library(message_rpc_service
    src/message_service.cc
)
target_include_directories(message_rpc_service PUBLIC include)
target_link_libraries(message_rpc_service
    PUBLIC
        chat_brpc
        chat_contracts
        message_file_client
        message_store
        spdlog::spdlog
)

add_executable(message_server
    app/message_server_main.cc
)
target_link_libraries(message_server
    PRIVATE
        message_rpc_service
        ${GFLAGS_LIBRARY}
        spdlog::spdlog
)

add_executable(message_client
    app/message_client_main.cc
)
target_link_libraries(message_client
    PRIVATE
        chat_brpc
        chat_contracts
        ${GFLAGS_LIBRARY}
)

add_executable(message_store_test
    test/message_store_test.cc
)
target_link_libraries(message_store_test
    PRIVATE
        message_rpc_service
        file_rpc_service
        GTest::GTest
        GTest::Main
)

include(GoogleTest)
gtest_discover_tests(message_store_test)
