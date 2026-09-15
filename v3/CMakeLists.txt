cmake_minimum_required(VERSION 3.16)

project(chat_desktop_v3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTING "Build V3 smoke tests" ON)

find_package(Protobuf REQUIRED)
find_package(Qt5 5.12 REQUIRED COMPONENTS Widgets Network WebSockets)

set(Protobuf_IMPORT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/contracts")
protobuf_generate_cpp(
    CHAT_V3_PROTO_SOURCES
    CHAT_V3_PROTO_HEADERS
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

add_library(chat_v3_contracts STATIC
    ${CHAT_V3_PROTO_SOURCES}
    ${CHAT_V3_PROTO_HEADERS}
)
target_include_directories(chat_v3_contracts PUBLIC
    "${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(chat_v3_contracts PUBLIC protobuf::libprotobuf)

if(MSVC)
    target_compile_options(chat_v3_contracts PRIVATE /utf-8)
endif()

include(CTest)
add_subdirectory(client/qt)
