#include "user/avatar_file_client.hpp"
#include "user/security.hpp"
#include "user/user_service.hpp"
#include "user/user_store.hpp"

#include <brpc/server.h>
#include <gflags/gflags.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <exception>
#include <memory>
#include <utility>

DEFINE_int32(port, 10003, "UserServer RPC listen port");
DEFINE_int32(idle_timeout_seconds, -1, "RPC connection idle timeout");
DEFINE_string(
    storage_file,
    "./v2/data/user/users.bin",
    "User snapshot storage file");
DEFINE_string(file_server, "127.0.0.1:10002", "FileServer address");
DEFINE_int32(file_timeout_ms, 3000, "FileServer RPC timeout");
DEFINE_int32(verification_ttl_seconds, 300, "Phone verification code TTL");
DEFINE_int32(session_ttl_seconds, 86400, "Login session TTL");

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    try {
        auto repository = std::make_shared<chat::user::UserStore>(
            FLAGS_storage_file);
        auto files = std::make_shared<chat::user::BrpcAvatarFileClient>(
            FLAGS_file_server,
            FLAGS_file_timeout_ms);
        auto sender =
            std::make_shared<chat::user::LoggingVerificationCodeSender>();
        auto codes = std::make_shared<chat::user::VerificationCodeManager>(
            std::move(sender),
            std::chrono::seconds(FLAGS_verification_ttl_seconds));
        auto sessions = std::make_shared<chat::user::SessionManager>(
            std::chrono::seconds(FLAGS_session_ttl_seconds));
        chat::user::UserServiceImpl user_service(
            std::move(repository),
            std::move(files),
            std::move(codes),
            std::move(sessions));

        brpc::Server server;
        if (server.AddService(
                &user_service,
                brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
            spdlog::error("failed to add UserService to brpc server");
            return 1;
        }

        brpc::ServerOptions options;
        options.idle_timeout_sec = FLAGS_idle_timeout_seconds;
        if (server.Start(FLAGS_port, &options) != 0) {
            spdlog::error("failed to start UserServer on port {}", FLAGS_port);
            return 1;
        }

        spdlog::info(
            "UserServer started: port={}, storage={}, file_server={}",
            FLAGS_port,
            FLAGS_storage_file,
            FLAGS_file_server);
        spdlog::warn(
            "development code sender is enabled; verification codes are printed "
            "to this log");
        server.RunUntilAskedToQuit();
        return 0;
    } catch (const std::exception& exception) {
        spdlog::error("UserServer startup failed: {}", exception.what());
        return 1;
    }
}
