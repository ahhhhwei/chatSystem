add_library(friend_store
    src/friend_store.cc
    src/mysql_friend_repository.cc
)
target_include_directories(friend_store PUBLIC include)
target_link_libraries(friend_store PUBLIC chat_contracts chat_infrastructure)

add_library(friend_dependencies
    src/dependencies.cc
)
target_include_directories(friend_dependencies PUBLIC include)
target_link_libraries(friend_dependencies
    PUBLIC
        chat_brpc
        chat_contracts
        chat_infrastructure
)

add_library(friend_rpc_service
    src/friend_service.cc
)
target_include_directories(friend_rpc_service PUBLIC include)
target_link_libraries(friend_rpc_service
    PUBLIC
        chat_brpc
        chat_contracts
        friend_dependencies
        friend_store
        user_security
        spdlog::spdlog
)

add_executable(friend_server
    app/friend_server_main.cc
)
target_link_libraries(friend_server
    PRIVATE
        friend_rpc_service
        ${GFLAGS_LIBRARY}
        spdlog::spdlog
)

add_executable(friend_client
    app/friend_client_main.cc
)
target_link_libraries(friend_client
    PRIVATE
        chat_brpc
        chat_contracts
        ${GFLAGS_LIBRARY}
)

add_executable(friend_service_test
    test/friend_service_test.cc
)
target_link_libraries(friend_service_test
    PRIVATE
        friend_rpc_service
        GTest::GTest
        GTest::Main
)

include(GoogleTest)
gtest_discover_tests(friend_service_test)
