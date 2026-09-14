add_library(gateway_rpc_client
    src/rpc_client.cc
)
target_include_directories(gateway_rpc_client PUBLIC include)
target_link_libraries(gateway_rpc_client
    PUBLIC
        chat_brpc
        chat_contracts
        chat_infrastructure
)

add_library(gateway_core
    src/gateway_core.cc
)
target_include_directories(gateway_core PUBLIC include)
target_link_libraries(gateway_core
    PUBLIC
        chat_contracts
        gateway_rpc_client
        spdlog::spdlog
)

add_library(gateway_transport
    src/gateway_server.cc
    src/websocket_server.cc
)
target_include_directories(gateway_transport
    PUBLIC include
)
target_link_libraries(gateway_transport
    PUBLIC
        gateway_core
    PRIVATE
        chat_httplib
        OpenSSL::Crypto
        Threads::Threads
        spdlog::spdlog
)

add_executable(gateway_server
    app/gateway_server_main.cc
)
target_link_libraries(gateway_server
    PRIVATE
        gateway_transport
        gateway_rpc_client
        ${GFLAGS_LIBRARY}
        spdlog::spdlog
)

add_executable(gateway_client
    app/gateway_client_main.cc
    src/websocket_client.cc
)
target_include_directories(gateway_client
    PRIVATE include
)
target_link_libraries(gateway_client
    PRIVATE
        chat_httplib
        chat_contracts
        Threads::Threads
        ${GFLAGS_LIBRARY}
        spdlog::spdlog
)

add_executable(gateway_core_test
    test/gateway_core_test.cc
)
target_link_libraries(gateway_core_test
    PRIVATE
        gateway_core
        GTest::GTest
        GTest::Main
)

include(GoogleTest)
gtest_discover_tests(gateway_core_test)

add_executable(gateway_transport_test
    test/gateway_transport_test.cc
)
target_link_libraries(gateway_transport_test
    PRIVATE
        chat_httplib
        gateway_transport
        GTest::GTest
        GTest::Main
)
gtest_discover_tests(gateway_transport_test)
