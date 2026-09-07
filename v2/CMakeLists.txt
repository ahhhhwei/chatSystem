cmake_minimum_required(VERSION 3.16)

project(chat_system_v2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(GTest REQUIRED)
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

add_subdirectory(services/file)
