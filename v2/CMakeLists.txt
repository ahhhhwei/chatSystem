cmake_minimum_required(VERSION 3.16)

project(chat_system_v2 LANGUAGES CXX)

option(BUILD_QT_CLIENT "Build the Qt desktop client" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(GTest REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Protobuf REQUIRED)
find_package(spdlog REQUIRED)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

pkg_check_modules(BRPC REQUIRED IMPORTED_TARGET brpc)

find_library(GFLAGS_LIBRARY NAMES gflags REQUIRED)
find_library(LEVELDB_LIBRARY NAMES leveldb REQUIRED)
find_library(PROTOC_LIBRARY NAMES protoc REQUIRED)

set(Protobuf_IMPORT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/contracts")
protobuf_generate_cpp(
    CHAT_PROTO_SOURCES
    CHAT_PROTO_HEADERS
    contracts/base.proto
    contracts/file.proto
    contracts/friend.proto
    contracts/friend_storage.proto
    contracts/gateway.proto
    contracts/message.proto
    contracts/notify.proto
    contracts/speech.proto
    contracts/transmit.proto
    contracts/user.proto
    contracts/user_storage.proto
)

add_library(chat_contracts
    ${CHAT_PROTO_SOURCES}
    ${CHAT_PROTO_HEADERS}
)
target_include_directories(chat_contracts PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(chat_contracts PUBLIC protobuf::libprotobuf)

add_library(chat_brpc INTERFACE)
target_link_libraries(chat_brpc
    INTERFACE
        PkgConfig::BRPC
        ${GFLAGS_LIBRARY}
        ${LEVELDB_LIBRARY}
        ${PROTOC_LIBRARY}
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
        ZLIB::ZLIB
        ${CMAKE_DL_LIBS}
        rt
)

include(CTest)

add_subdirectory(infrastructure)
add_subdirectory(services/file)
add_subdirectory(services/message)
add_subdirectory(services/user)
add_subdirectory(services/friend)
add_subdirectory(services/transmit)
add_subdirectory(services/gateway)

if(BUILD_TESTING)
    add_executable(infrastructure_integration_test
        test/infrastructure_integration_test.cc
    )
    target_link_libraries(infrastructure_integration_test
        PRIVATE
            chat_infrastructure
            friend_store
            message_store
            user_security
            user_store
            GTest::GTest
            GTest::Main
    )
    add_test(NAME V1InfrastructureIntegration
        COMMAND infrastructure_integration_test)
endif()

if(BUILD_QT_CLIENT)
    add_subdirectory(client/qt)
endif()
