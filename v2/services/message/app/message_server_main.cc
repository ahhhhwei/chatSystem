#include "message/file_client.hpp"
#include "message/message_service.hpp"
#include "message/message_store.hpp"

#include <brpc/server.h>
#include <gflags/gflags.h>
#include <spdlog/spdlog.h>

#include <exception>
#include <memory>

DEFINE_int32(port, 10005, "MessageStoreServer RPC listen port");
DEFINE_int32(idle_timeout_seconds, -1, "RPC connection idle timeout");
DEFINE_string(
    storage_file,
    "./v2/data/message/messages.bin",
    "Message metadata storage file");
DEFINE_string(
    file_server,
    "127.0.0.1:10002",
    "FileServer address");
DEFINE_int32(file_timeout_ms, 3000, "FileServer RPC timeout");

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    try {
        auto store = std::make_shared<chat::message::MessageStore>(
            FLAGS_storage_file);
        auto file_client = std::make_shared<chat::message::BrpcFileClient>(
            FLAGS_file_server,
            FLAGS_file_timeout_ms);
        chat::message::MessageServiceImpl message_service(
            std::move(store),
            std::move(file_client));

        brpc::Server server;
        if (server.AddService(
                &message_service,
                brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
            spdlog::error("failed to add MsgStorageService to brpc server");
            return 1;
        }

        brpc::ServerOptions options;
        options.idle_timeout_sec = FLAGS_idle_timeout_seconds;
        if (server.Start(FLAGS_port, &options) != 0) {
            spdlog::error(
                "failed to start MessageStoreServer on port {}",
                FLAGS_port);
            return 1;
        }

        spdlog::info(
            "MessageStoreServer started: port={}, storage={}, file_server={}",
            FLAGS_port,
            FLAGS_storage_file,
            FLAGS_file_server);
        server.RunUntilAskedToQuit();
        return 0;
    } catch (const std::exception& exception) {
        spdlog::error(
            "MessageStoreServer startup failed: {}",
            exception.what());
        return 1;
    }
}
