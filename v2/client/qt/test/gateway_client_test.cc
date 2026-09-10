#include "chat_client/gateway_client.hpp"
#include "gateway/gateway_server.hpp"

#include "friend.pb.h"
#include "notify.pb.h"
#include "user.pb.h"

#include <QApplication>
#include <QByteArray>
#include <QSignalSpy>
#include <QUrl>
#include <QtTest>

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace chat::desktop {
namespace {

class DesktopFakeRpc final : public gateway::RpcClient {
public:
    bool call(
        gateway::DownstreamService,
        const google::protobuf::MethodDescriptor& method,
        const google::protobuf::Message& request_message,
        google::protobuf::Message& response_message,
        std::string& error) override {
        if (method.name() == "UserLogin") {
            const auto& request =
                dynamic_cast<const ahwei_im::UserLoginReq&>(request_message);
            auto& response =
                dynamic_cast<ahwei_im::UserLoginRsp&>(response_message);
            response.set_request_id(request.request_id());
            response.set_success(true);
            response.set_login_session_id("alice-session");
            return true;
        }
        if (method.name() == "ResolveSession") {
            const auto& request = dynamic_cast<
                const ahwei_im::ResolveSessionReq&>(request_message);
            auto& response = dynamic_cast<
                ahwei_im::ResolveSessionRsp&>(response_message);
            response.set_request_id(request.request_id());
            response.set_success(true);
            response.set_user_id(
                request.session_id() == "bob-session" ? "bob-id" : "alice-id");
            return true;
        }
        if (method.name() == "FriendRemove") {
            const auto& request = dynamic_cast<
                const ahwei_im::FriendRemoveReq&>(request_message);
            auto& response = dynamic_cast<
                ahwei_im::FriendRemoveRsp&>(response_message);
            EXPECT_EQ(request.user_id(), "alice-id");
            EXPECT_EQ(request.peer_id(), "bob-id");
            response.set_request_id(request.request_id());
            response.set_success(true);
            return true;
        }
        if (method.name() == "RevokeSession") {
            const auto& request = dynamic_cast<
                const ahwei_im::RevokeSessionReq&>(request_message);
            auto& response = dynamic_cast<
                ahwei_im::RevokeSessionRsp&>(response_message);
            response.set_request_id(request.request_id());
            response.set_success(true);
            return true;
        }
        error = "unexpected RPC: " + method.name();
        return false;
    }
};

TEST(QtGatewayClientTest, ExchangesRealHttpAndWebSocketProtobuf) {
    gateway::GatewayOptions options;
    options.listen_address = "127.0.0.1";
    options.http_port = 0;
    options.websocket_port = 0;
    gateway::GatewayServer server(
        options, std::make_shared<DesktopFakeRpc>());
    std::string startup_error;
    ASSERT_TRUE(server.start(startup_error)) << startup_error;

    GatewayClient client;
    client.configure(
        QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.http_port())),
        QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.websocket_port())));

    bool login_finished = false;
    ahwei_im::UserLoginReq login_request;
    login_request.set_request_id("qt-login-1");
    login_request.set_nickname("alice");
    login_request.set_password("abc123");
    client.post<ahwei_im::UserLoginReq, ahwei_im::UserLoginRsp>(
        QStringLiteral("/service/user/username_login"),
        login_request,
        [&login_finished](
            const ahwei_im::UserLoginRsp& response,
            const QString& error) {
            EXPECT_TRUE(error.isEmpty()) << error.toStdString();
            EXPECT_TRUE(response.success());
            EXPECT_EQ(response.login_session_id(), "alice-session");
            login_finished = true;
        });
    QTRY_VERIFY_WITH_TIMEOUT(login_finished, 2000);

    QSignalSpy connected(&client, &GatewayClient::websocketConnected);
    QSignalSpy notifications(&client, &GatewayClient::notificationReceived);
    client.open_websocket(QStringLiteral("bob-session"));
    QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(server.online_count(), 1U, 2000);

    bool remove_finished = false;
    ahwei_im::FriendRemoveReq remove_request;
    remove_request.set_request_id("qt-remove-1");
    remove_request.set_session_id("alice-session");
    remove_request.set_peer_id("bob-id");
    client.post<ahwei_im::FriendRemoveReq, ahwei_im::FriendRemoveRsp>(
        QStringLiteral("/service/friend/remove_friend"),
        remove_request,
        [&remove_finished](
            const ahwei_im::FriendRemoveRsp& response,
            const QString& error) {
            EXPECT_TRUE(error.isEmpty()) << error.toStdString();
            EXPECT_TRUE(response.success());
            remove_finished = true;
        });
    QTRY_VERIFY_WITH_TIMEOUT(remove_finished, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(notifications.count(), 1, 2000);

    const QByteArray body = notifications.takeFirst().at(0).toByteArray();
    ahwei_im::NotifyMessage notification;
    ASSERT_TRUE(notification.ParseFromArray(body.constData(), body.size()));
    EXPECT_EQ(notification.notify_type(), ahwei_im::FRIEND_REMOVE_NOTIFY);
    EXPECT_EQ(notification.friend_remove().user_id(), "alice-id");

    client.close_websocket();
    server.stop();
}

}  // namespace
}  // namespace chat::desktop

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
