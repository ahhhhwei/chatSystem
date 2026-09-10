add_library(file_store
    src/file_store.cc
)
target_include_directories(file_store PUBLIC include)

add_library(file_rpc_service
    src/file_service.cc
)
target_include_directories(file_rpc_service PUBLIC include)
target_link_libraries(file_rpc_service
    PUBLIC
        chat_brpc
        chat_contracts
        file_store
        spdlog::spdlog
)

add_executable(file_server
    app/file_server_main.cc
)
target_link_libraries(file_server
    PRIVATE
        file_rpc_service
        chat_infrastructure
        ${GFLAGS_LIBRARY}
        spdlog::spdlog
)

add_executable(file_client
    app/file_client_main.cc
)
target_link_libraries(file_client
    PRIVATE
        chat_brpc
        chat_contracts
        ${GFLAGS_LIBRARY}
)

add_executable(file_store_test
    test/file_store_test.cc
)
target_link_libraries(file_store_test
    PRIVATE
        file_store
        GTest::GTest
        GTest::Main
)

include(GoogleTest)
gtest_discover_tests(file_store_test)
