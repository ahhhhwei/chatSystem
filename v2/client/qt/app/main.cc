#include "chat_client/app_window.hpp"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFont>
#include <QIcon>
#include <QTimer>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("Ahwei Chat"));
    application.setOrganizationName(QStringLiteral("Ahwei IM"));
    application.setWindowIcon(QIcon(QStringLiteral(":/chat/logo.png")));
    QFont font = application.font();
    font.setPointSize(10);
    application.setFont(font);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("chatSystem V2 Qt desktop client"));
    parser.addHelpOption();
    QCommandLineOption http_option(
        QStringLiteral("http-url"),
        QStringLiteral("Gateway HTTP base URL"),
        QStringLiteral("url"),
        QStringLiteral("http://127.0.0.1:9000"));
    QCommandLineOption websocket_option(
        QStringLiteral("ws-url"),
        QStringLiteral("Gateway WebSocket URL"),
        QStringLiteral("url"),
        QStringLiteral("ws://127.0.0.1:9001"));
    QCommandLineOption smoke_option(
        QStringLiteral("ui-smoke"),
        QStringLiteral("Construct the UI offscreen and exit"));
    parser.addOption(http_option);
    parser.addOption(websocket_option);
    parser.addOption(smoke_option);
    parser.process(application);

    chat::desktop::AppWindow window(
        QUrl(parser.value(http_option)),
        QUrl(parser.value(websocket_option)));
    window.show();
    if (parser.isSet(smoke_option)) {
        QTimer::singleShot(300, &application, &QApplication::quit);
    }
    return application.exec();
}
