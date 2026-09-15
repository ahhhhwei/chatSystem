#include "chat_client/app_window.hpp"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFont>
#include <QSettings>
#include <QTimer>

int main(int argc, char* argv[]) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("聊天"));
    application.setOrganizationName(QStringLiteral("Chat Desktop"));
    QFont font = application.font();
    font.setPointSize(10);
    application.setFont(font);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("桌面聊天客户端"));
    parser.addHelpOption();

    const QString config_path = QDir(QCoreApplication::applicationDirPath())
                                    .filePath(QStringLiteral("server.ini"));
    QSettings server_config(config_path, QSettings::IniFormat);
    const QString default_http_url = server_config.value(
        QStringLiteral("server/http_url"),
        QStringLiteral("http://127.0.0.1:9000")).toString();
    const QString default_websocket_url = server_config.value(
        QStringLiteral("server/ws_url"),
        QStringLiteral("ws://127.0.0.1:9001")).toString();

    QCommandLineOption http_option(
        QStringLiteral("http-url"),
        QStringLiteral("消息服务地址"),
        QStringLiteral("url"),
        default_http_url);
    QCommandLineOption websocket_option(
        QStringLiteral("ws-url"),
        QStringLiteral("实时消息服务地址"),
        QStringLiteral("url"),
        default_websocket_url);
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
