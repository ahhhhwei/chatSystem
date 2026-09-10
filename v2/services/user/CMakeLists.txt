add_library(user_store
    src/user_store.cc
)
target_include_directories(user_store PUBLIC include)
target_link_libraries(user_store PUBLIC chat_contracts)

add_library(user_security
    src/security.cc
)
target_include_directories(user_security PUBLIC include)
target_link_libraries(user_security
    PUBLIC
        OpenSSL::Crypto
        spdlog::spdlog
)

add_library(user_file_client
    src/avatar_file_client.cc
)
target_include_directories(user_file_client PUBLIC include)
target_link_libraries(user_file_client
    PUBLIC
        chat_brpc
        chat_contracts
)

add_library(user_rpc_service
    src/user_service.cc
)
target_include_directories(user_rpc_service PUBLIC include)
target_link_libraries(user_rpc_service
    PUBLIC
        chat_brpc
        chat_contracts
        user_file_client
        user_security
        user_store
        spdlog::spdlog
)

add_executable(user_server
    app/user_server_main.cc
)
target_link_libraries(user_server
    PRIVATE
        user_rpc_service
        ${GFLAGS_LIBRARY}
        spdlog::spdlog
)

add_executable(user_client
    app/user_client_main.cc
)
target_link_libraries(user_client
    PRIVATE
        chat_brpc
        chat_contracts
        ${GFLAGS_LIBRARY}
)

add_executable(user_service_test
    test/user_service_test.cc
)
target_link_libraries(user_service_test
    PRIVATE
        user_rpc_service
        file_rpc_service
        GTest::GTest
        GTest::Main
)

include(GoogleTest)
gtest_discover_tests(user_service_test)
