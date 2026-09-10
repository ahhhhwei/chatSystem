#pragma once

#include "gateway.pb.h"

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QWebSocket>

#include <functional>
#include <memory>
#include <string>

class QNetworkReply;

namespace chat::desktop {

class GatewayClient final : public QObject {
    Q_OBJECT

public:
    explicit GatewayClient(QObject* parent = nullptr);

    void configure(QUrl http_base_url, QUrl websocket_url);
    const QUrl& http_base_url() const;
    const QUrl& websocket_url() const;

    static QString make_request_id();

    template <typename Request, typename Response>
    void post(
        const QString& path,
        const Request& request,
        std::function<void(const Response&, const QString&)> callback) {
        const std::string serialized = request.SerializeAsString();
        post_raw(
            path,
            QByteArray(serialized.data(), static_cast<int>(serialized.size())),
            [callback = std::move(callback)](
                const QByteArray& body,
                const QString& transport_error) {
                Response response;
                if (!transport_error.isEmpty()) {
                    callback(response, transport_error);
                    return;
                }
                if (!response.ParseFromArray(body.constData(), body.size())) {
                    callback(response, QStringLiteral("无法解析服务器 Protobuf 响应"));
                    return;
                }
                callback(response, {});
            });
    }

    void open_websocket(const QString& session_id);
    void close_websocket();
    bool websocket_connected() const;

signals:
    void websocketConnected();
    void websocketDisconnected();
    void websocketError(const QString& message);
    void notificationReceived(const QByteArray& body);

private:
    using RawCallback =
        std::function<void(const QByteArray&, const QString&)>;

    void post_raw(
        const QString& path,
        const QByteArray& body,
        RawCallback callback);
    void send_websocket_authentication();

    QUrl http_base_url_{QStringLiteral("http://127.0.0.1:9000")};
    QUrl websocket_url_{QStringLiteral("ws://127.0.0.1:9001")};
    QString session_id_;
    QNetworkAccessManager http_;
    QWebSocket websocket_;
    bool websocket_connected_ = false;
};

}  // namespace chat::desktop
