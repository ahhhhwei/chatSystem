#include "friend/dependencies.hpp"
#include "friend/friend_service.hpp"
#include "friend/friend_store.hpp"

#include <brpc/server.h>
#include <gflags/gflags.h>
#include <spdlog/spdlog.h>

#include <exception>
#include <memory>
#include <utility>

DEFINE_int32(port, 10006, "FriendServer RPC listen port");
DEFINE_int32(idle_timeout_seconds, -1, "RPC connection idle timeout");
DEFINE_string(
    storage_file,
    "./v2/data/friend/friends.bin",
    "Friend, application and chat session storage file");
DEFINE_string(user_server, "127.0.0.1:10003", "UserServer address");
DEFINE_int32(user_timeout_ms, 3000, "UserServer RPC timeout");
DEFINE_string(
    message_server,
    "127.0.0.1:10005",
    "MessageStoreServer address");
DEFINE_int32(message_timeout_ms, 3000, "MessageStoreServer RPC timeout");

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);
    try {
        auto repository = std::make_shared<chat::friend_service::FriendStore>(
            FLAGS_storage_file);
        auto users = std::make_shared<chat::friend_service::BrpcUserDirectory>(
            FLAGS_user_server,
            FLAGS_user_timeout_ms);
        auto messages =
            std::make_shared<chat::friend_service::BrpcRecentMessageClient>(
                FLAGS_message_server,
                FLAGS_message_timeout_ms);
        chat::friend_service::FriendServiceImpl friend_service(
            std::move(repository),
            std::move(users),
            std::move(messages));

        brpc::Server server;
        if (server.AddService(
                &friend_service,
                brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
            spdlog::error("failed to add FriendService to brpc server");
            return 1;
        }
        brpc::ServerOptions options;
        options.idle_timeout_sec = FLAGS_idle_timeout_seconds;
        if (server.Start(FLAGS_port, &options) != 0) {
            spdlog::error("failed to start FriendServer on port {}", FLAGS_port);
            return 1;
        }
        spdlog::info(
            "FriendServer started: port={}, storage={}, user_server={}, "
            "message_server={}",
            FLAGS_port,
            FLAGS_storage_file,
            FLAGS_user_server,
            FLAGS_message_server);
        server.RunUntilAskedToQuit();
        return 0;
    } catch (const std::exception& exception) {
        spdlog::error("FriendServer startup failed: {}", exception.what());
        return 1;
    }
}
