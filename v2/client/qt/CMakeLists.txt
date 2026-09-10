find_package(Qt5 5.12 REQUIRED COMPONENTS Widgets Network WebSockets Test)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_library(chat_qt_network
    src/gateway_client.cc
    include/chat_client/gateway_client.hpp
)
target_include_directories(chat_qt_network PUBLIC include)
target_link_libraries(chat_qt_network
    PUBLIC
        chat_contracts
        Qt5::Core
        Qt5::Network
        Qt5::WebSockets
)

add_executable(chat_desktop
    app/main.cc
    src/app_window.cc
    include/chat_client/app_window.hpp
    resources.qrc
)
target_include_directories(chat_desktop PRIVATE include)
target_link_libraries(chat_desktop
    PRIVATE
        chat_qt_network
        chat_contracts
        Qt5::Widgets
)

add_test(
    NAME QtClient.UiStartsOffscreen
    COMMAND chat_desktop --ui-smoke
)
set_tests_properties(QtClient.UiStartsOffscreen PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 10
)

add_executable(qt_gateway_client_test
    test/gateway_client_test.cc
)
target_link_libraries(qt_gateway_client_test
    PRIVATE
        chat_qt_network
        gateway_transport
        Qt5::Test
        Qt5::Widgets
        GTest::GTest
)
add_test(NAME QtClient.RealGatewayTransport COMMAND qt_gateway_client_test)
set_tests_properties(QtClient.RealGatewayTransport PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 15
)
