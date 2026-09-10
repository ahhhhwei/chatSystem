add_library(transmit_dependencies
    src/dependencies.cc
)
target_include_directories(transmit_dependencies PUBLIC include)
target_link_libraries(transmit_dependencies
    PUBLIC
        chat_brpc
        chat_contracts
)

add_library(transmit_rpc_service
    src/transmit_service.cc
)
target_include_directories(transmit_rpc_service PUBLIC include)
target_link_libraries(transmit_rpc_service
    PUBLIC
        chat_brpc
        chat_contracts
        transmit_dependencies
        spdlog::spdlog
)

add_executable(transmit_server
    app/transmit_server_main.cc
)
target_link_libraries(transmit_server
    PRIVATE
        transmit_rpc_service
        ${GFLAGS_LIBRARY}
        spdlog::spdlog
)

add_executable(transmit_client
    app/transmit_client_main.cc
)
target_link_libraries(transmit_client
    PRIVATE
        chat_brpc
        chat_contracts
        ${GFLAGS_LIBRARY}
)

add_executable(transmit_service_test
    test/transmit_service_test.cc
)
target_link_libraries(transmit_service_test
    PRIVATE
        transmit_rpc_service
        message_rpc_service
        file_rpc_service
        GTest::GTest
        GTest::Main
)

include(GoogleTest)
gtest_discover_tests(transmit_service_test)
