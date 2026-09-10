#include "gateway/gateway_server.hpp"
#include "gateway/websocket_server.hpp"

#include "gateway.pb.h"
#include "httplib.h"
#include "notify.pb.h"
#include "user.pb.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace chat::gateway {
namespace {

class LoginRpcClient final : public RpcClient {
public:
    bool call(
        DownstreamService service,
        const google::protobuf::MethodDescriptor& method,
        const google::protobuf::Message& request_message,
        google::protobuf::Message& response_message,
        std::string& error) override {
        if (service != DownstreamService::USER || method.name() != "UserLogin") {
            error = "unexpected RPC";
            return false;
        }
        const auto& request =
            dynamic_cast<const ahwei_im::UserLoginReq&>(request_message);
        auto& response =
            dynamic_cast<ahwei_im::UserLoginRsp&>(response_message);
        response.set_request_id(request.request_id());
        response.set_success(true);
        response.set_login_session_id("session-from-http");
        return true;
    }
};

bool send_all(int fd, const std::string& data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        const auto size = ::send(
            fd, data.data() + offset, data.size() - offset, MSG_NOSIGNAL);
        if (size <= 0) {
            return false;
        }
        offset += static_cast<std::size_t>(size);
    }
    return true;
}

bool receive_all(int fd, char* data, std::size_t size) {
    std::size_t offset = 0;
    while (offset < size) {
        const auto received = ::recv(fd, data + offset, size - offset, 0);
        if (received <= 0) {
            return false;
        }
        offset += static_cast<std::size_t>(received);
    }
    return true;
}

int connect_to(std::uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    timeval timeout{2, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (::connect(
            fd,
            reinterpret_cast<const sockaddr*>(&address),
            sizeof(address)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

bool send_masked_frame(
    int fd,
    std::uint8_t opcode,
    const std::string& payload) {
    if (payload.size() > 125U) {
        return false;
    }
    constexpr std::array<unsigned char, 4> mask{0x12U, 0x34U, 0x56U, 0x78U};
    std::string frame;
    frame.push_back(static_cast<char>(0x80U | opcode));
    frame.push_back(static_cast<char>(0x80U | payload.size()));
    frame.append(
        reinterpret_cast<const char*>(mask.data()), mask.size());
    for (std::size_t index = 0; index < payload.size(); ++index) {
        frame.push_back(static_cast<char>(
            static_cast<unsigned char>(payload[index]) ^
            mask[index % mask.size()]));
    }
    return send_all(fd, frame);
}

bool read_server_frame(int fd, std::uint8_t& opcode, std::string& payload) {
    std::array<unsigned char, 2> header{};
    if (!receive_all(
            fd, reinterpret_cast<char*>(header.data()), header.size())) {
        return false;
    }
    opcode = header[0] & 0x0fU;
    std::uint64_t payload_size = header[1] & 0x7fU;
    if ((header[1] & 0x80U) != 0) {
        return false;
    }
    if (payload_size == 126U) {
        std::array<unsigned char, 2> extended{};
        if (!receive_all(
                fd,
                reinterpret_cast<char*>(extended.data()),
                extended.size())) {
            return false;
        }
        payload_size =
            (static_cast<std::uint64_t>(extended[0]) << 8U) | extended[1];
    } else if (payload_size == 127U) {
        std::array<unsigned char, 8> extended{};
        if (!receive_all(
                fd,
                reinterpret_cast<char*>(extended.data()),
                extended.size())) {
            return false;
        }
        payload_size = 0;
        for (const auto byte : extended) {
            payload_size = (payload_size << 8U) | byte;
        }
    }
    payload.resize(static_cast<std::size_t>(payload_size));
    return payload.empty() || receive_all(fd, payload.data(), payload.size());
}

bool wait_until(const std::function<bool()>& predicate) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return predicate();
}

TEST(GatewayTransportTest, ServesARealProtobufHttpRequest) {
    GatewayOptions options;
    options.listen_address = "127.0.0.1";
    options.http_port = 0;
    options.websocket_port = 0;
    GatewayServer server(options, std::make_shared<LoginRpcClient>());
    std::string error;
    ASSERT_TRUE(server.start(error)) << error;

    ahwei_im::UserLoginReq request;
    request.set_request_id("http-login-1");
    request.set_nickname("alice");
    request.set_password("password");
    httplib::Client client("127.0.0.1", server.http_port());
    const auto result = client.Post(
        "/service/user/username_login",
        request.SerializeAsString(),
        kProtobufContentType);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->status, 200);
    EXPECT_EQ(result->get_header_value("Content-Type"), kProtobufContentType);
    ahwei_im::UserLoginRsp response;
    ASSERT_TRUE(response.ParseFromString(result->body));
    EXPECT_TRUE(response.success());
    EXPECT_EQ(response.login_session_id(), "session-from-http");

    server.stop();
}

TEST(GatewayTransportTest, AuthenticatesAndPushesARealWebSocketBinaryFrame) {
    WebSocketServer server("127.0.0.1", 0, 1024);
    std::atomic<int> disconnects{0};
    server.set_authenticator(
        [](const std::string& body,
           std::string& user_id,
           std::string& session_id,
           std::string& error) {
            ahwei_im::ClientAuthenticationReq request;
            if (!request.ParseFromString(body) ||
                request.session_id() != "session-alice") {
                error = "invalid session";
                return false;
            }
            user_id = "alice-id";
            session_id = request.session_id();
            return true;
        });
    server.set_disconnect_handler(
        [&disconnects](const std::string& session_id) {
            EXPECT_EQ(session_id, "session-alice");
            ++disconnects;
        });
    std::string error;
    ASSERT_TRUE(server.start(error)) << error;

    const int fd = connect_to(server.port());
    ASSERT_GE(fd, 0);
    const std::string handshake =
        "GET / HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";
    ASSERT_TRUE(send_all(fd, handshake));
    std::array<char, 512> handshake_response{};
    const auto received = ::recv(
        fd, handshake_response.data(), handshake_response.size(), 0);
    ASSERT_GT(received, 0);
    const std::string response(
        handshake_response.data(), static_cast<std::size_t>(received));
    EXPECT_NE(response.find("101 Switching Protocols"), std::string::npos);
    EXPECT_NE(
        response.find("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="),
        std::string::npos);

    ahwei_im::ClientAuthenticationReq authentication;
    authentication.set_request_id("ws-auth-1");
    authentication.set_session_id("session-alice");
    ASSERT_TRUE(send_masked_frame(
        fd, 0x2U, authentication.SerializeAsString()));
    ASSERT_TRUE(wait_until([&server] { return server.online_count() == 1U; }));

    ahwei_im::NotifyMessage notification;
    notification.set_notify_type(ahwei_im::FRIEND_REMOVE_NOTIFY);
    notification.mutable_friend_remove()->set_user_id("bob-id");
    ASSERT_TRUE(server.send("alice-id", notification));
    std::uint8_t opcode = 0;
    std::string payload;
    ASSERT_TRUE(read_server_frame(fd, opcode, payload));
    EXPECT_EQ(opcode, 0x2U);
    ahwei_im::NotifyMessage received_notification;
    ASSERT_TRUE(received_notification.ParseFromString(payload));
    EXPECT_EQ(received_notification.notify_type(), ahwei_im::FRIEND_REMOVE_NOTIFY);
    EXPECT_EQ(received_notification.friend_remove().user_id(), "bob-id");

    ASSERT_TRUE(send_masked_frame(fd, 0x8U, {}));
    ASSERT_TRUE(read_server_frame(fd, opcode, payload));
    EXPECT_EQ(opcode, 0x8U);
    ASSERT_TRUE(wait_until([&disconnects] { return disconnects.load() == 1; }));
    ::close(fd);
    server.stop();
}

}  // namespace
}  // namespace chat::gateway
