set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_library(chat_v3_qt_network STATIC
    src/gateway_client.cc
    include/chat_client/gateway_client.hpp
)
target_include_directories(chat_v3_qt_network PUBLIC include)
target_link_libraries(chat_v3_qt_network
    PUBLIC
        chat_v3_contracts
        Qt5::Core
        Qt5::Network
        Qt5::WebSockets
)

set(CHAT_V3_DESKTOP_SOURCES
    app/main.cc
    src/app_window.cc
    include/chat_client/app_window.hpp
    resources.qrc
)

if(WIN32)
    add_executable(chat_desktop WIN32 ${CHAT_V3_DESKTOP_SOURCES})
else()
    add_executable(chat_desktop ${CHAT_V3_DESKTOP_SOURCES})
endif()

target_include_directories(chat_desktop PRIVATE include)
target_link_libraries(chat_desktop
    PRIVATE
        chat_v3_qt_network
        chat_v3_contracts
        Qt5::Widgets
)
set_target_properties(chat_desktop PROPERTIES OUTPUT_NAME "Chat")

if(MSVC)
    target_compile_options(chat_v3_qt_network PRIVATE /utf-8)
    target_compile_options(chat_desktop PRIVATE /utf-8)
    target_compile_definitions(chat_desktop PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

install(TARGETS chat_desktop RUNTIME DESTINATION .)

if(BUILD_TESTING)
    add_test(NAME V3Client.UiStartsOffscreen COMMAND chat_desktop --ui-smoke)
    set_tests_properties(V3Client.UiStartsOffscreen PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 10
    )
endif()
