#include "transmit/dependencies.hpp"
#include "transmit/transmit_service.hpp"

#include <brpc/server.h>
#include <gflags/gflags.h>
#include <spdlog/spdlog.h>

#include <exception>
#include <memory>
#include <utility>

DEFINE_int32(port, 10004, "MessageTransmitServer RPC listen port");
DEFINE_int32(idle_timeout_seconds, -1, "RPC connection idle timeout");
DEFINE_string(user_server, "127.0.0.1:10003", "UserServer address");
DEFINE_int32(user_timeout_ms, 3000, "UserServer RPC timeout");
DEFINE_string(friend_server, "127.0.0.1:10006", "FriendServer address");
DEFINE_int32(friend_timeout_ms, 3000, "FriendServer RPC timeout");
DEFINE_string(
    message_server,
    "127.0.0.1:10005",
    "MessageStoreServer address");
DEFINE_int32(message_timeout_ms, 5000, "MessageStoreServer RPC timeout");

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    try {
        auto user_client = std::make_shared<chat::transmit::BrpcUserClient>(
            FLAGS_user_server,
            FLAGS_user_timeout_ms);
        auto member_repository = std::make_shared<
            chat::transmit::BrpcFriendSessionMemberRepository>(
                FLAGS_friend_server,
                FLAGS_friend_timeout_ms);
        auto publisher =
            std::make_shared<chat::transmit::BrpcMessagePublisher>(
                FLAGS_message_server,
                FLAGS_message_timeout_ms);
        chat::transmit::TransmitServiceImpl transmit_service(
            std::move(user_client),
            std::move(member_repository),
            std::move(publisher));

        brpc::Server server;
        if (server.AddService(
                &transmit_service,
                brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
            spdlog::error("failed to add MsgTransmitService to brpc server");
            return 1;
        }

        brpc::ServerOptions options;
        options.idle_timeout_sec = FLAGS_idle_timeout_seconds;
        if (server.Start(FLAGS_port, &options) != 0) {
            spdlog::error(
                "failed to start MessageTransmitServer on port {}",
                FLAGS_port);
            return 1;
        }

        spdlog::info(
            "MessageTransmitServer started: port={}, user_server={}, "
            "friend_server={}, message_server={}",
            FLAGS_port,
            FLAGS_user_server,
            FLAGS_friend_server,
            FLAGS_message_server);
        server.RunUntilAskedToQuit();
        return 0;
    } catch (const std::exception& exception) {
        spdlog::error(
            "MessageTransmitServer startup failed: {}",
            exception.what());
        return 1;
    }
}
