#include "chat_client/gateway_client.hpp"

#include <QAbstractSocket>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUuid>

#include <utility>

namespace chat::desktop {

GatewayClient::GatewayClient(QObject* parent) : QObject(parent) {
    connect(&websocket_, &QWebSocket::connected, this, [this] {
        websocket_connected_ = true;
        send_websocket_authentication();
        emit websocketConnected();
    });
    connect(&websocket_, &QWebSocket::disconnected, this, [this] {
        websocket_connected_ = false;
        emit websocketDisconnected();
    });
    connect(
        &websocket_,
        &QWebSocket::binaryMessageReceived,
        this,
        &GatewayClient::notificationReceived);
    connect(
        &websocket_,
        QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
        this,
        [this](QAbstractSocket::SocketError) {
            emit websocketError(websocket_.errorString());
        });
}

void GatewayClient::configure(QUrl http_base_url, QUrl websocket_url) {
    http_base_url_ = std::move(http_base_url);
    websocket_url_ = std::move(websocket_url);
}

const QUrl& GatewayClient::http_base_url() const {
    return http_base_url_;
}

const QUrl& GatewayClient::websocket_url() const {
    return websocket_url_;
}

QString GatewayClient::make_request_id() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void GatewayClient::open_websocket(const QString& session_id) {
    session_id_ = session_id;
    websocket_.open(websocket_url_);
}

void GatewayClient::close_websocket() {
    session_id_.clear();
    websocket_.close(QWebSocketProtocol::CloseCodeNormal, QStringLiteral("logout"));
}

bool GatewayClient::websocket_connected() const {
    return websocket_connected_;
}

void GatewayClient::post_raw(
    const QString& path,
    const QByteArray& body,
    RawCallback callback) {
    QUrl url = http_base_url_;
    QString base_path = url.path();
    if (base_path.endsWith('/')) {
        base_path.chop(1);
    }
    url.setPath(base_path + path);
    QNetworkRequest request(url);
    request.setHeader(
        QNetworkRequest::ContentTypeHeader,
        QStringLiteral("application/x-protbuf"));
    request.setRawHeader("Accept", "application/x-protbuf");
    QNetworkReply* reply = http_.post(request, body);
    connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)] {
        const QByteArray response_body = reply->readAll();
        QString error;
        if (reply->error() != QNetworkReply::NoError) {
            error = reply->errorString();
        } else {
            const int status = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status != 200) {
                error = QStringLiteral("Gateway 返回 HTTP %1").arg(status);
            }
        }
        reply->deleteLater();
        callback(response_body, error);
    });
}

void GatewayClient::send_websocket_authentication() {
    ahwei_im::ClientAuthenticationReq request;
    request.set_request_id(make_request_id().toStdString());
    request.set_session_id(session_id_.toStdString());
    const std::string serialized = request.SerializeAsString();
    websocket_.sendBinaryMessage(QByteArray(
        serialized.data(), static_cast<int>(serialized.size())));
}

}  // namespace chat::desktop
