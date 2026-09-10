#include "gateway/gateway_server.hpp"
#include "gateway/rpc_client.hpp"

#include <gflags/gflags.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <limits>
#include <memory>
#include <thread>

DEFINE_string(listen_address, "0.0.0.0", "Gateway listen IPv4 address");
DEFINE_int32(http_port, 9000, "Gateway HTTP listen port");
DEFINE_int32(websocket_port, 9001, "Gateway WebSocket listen port");
DEFINE_uint64(
    max_http_body_mb,
    256,
    "Maximum protobuf HTTP request body size in MiB");

DEFINE_string(user_server, "127.0.0.1:10003", "UserServer address");
DEFINE_string(friend_server, "127.0.0.1:10006", "FriendServer address");
DEFINE_string(message_server, "127.0.0.1:10005", "MessageStoreServer address");
DEFINE_string(transmit_server, "127.0.0.1:10004", "MessageTransmitServer address");
DEFINE_string(file_server, "127.0.0.1:10002", "FileServer address");
DEFINE_string(
    speech_server,
    "",
    "SpeechServer address; empty keeps optional speech recognition disabled");
DEFINE_int32(rpc_timeout_ms, 3000, "Default downstream RPC timeout");
DEFINE_int32(file_rpc_timeout_ms, 30000, "File/Speech RPC timeout");

namespace {

std::atomic<bool> stop_requested{false};

void request_stop(int) {
    stop_requested = true;
}

std::uint16_t checked_port(int port, const char* name) {
    if (port <= 0 || port > std::numeric_limits<std::uint16_t>::max()) {
        throw std::invalid_argument(std::string(name) + " is out of range");
    }
    return static_cast<std::uint16_t>(port);
}

}  // namespace

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);
    try {
        chat::gateway::RpcEndpoints endpoints;
        endpoints.user = {FLAGS_user_server, FLAGS_rpc_timeout_ms};
        endpoints.friend_service = {FLAGS_friend_server, FLAGS_rpc_timeout_ms};
        endpoints.message = {FLAGS_message_server, FLAGS_rpc_timeout_ms};
        endpoints.transmit = {FLAGS_transmit_server, FLAGS_rpc_timeout_ms};
        endpoints.file = {FLAGS_file_server, FLAGS_file_rpc_timeout_ms};
        endpoints.speech = {FLAGS_speech_server, FLAGS_file_rpc_timeout_ms};
        auto rpc = std::make_shared<chat::gateway::BrpcRpcClient>(endpoints);

        chat::gateway::GatewayOptions options;
        options.listen_address = FLAGS_listen_address;
        options.http_port = checked_port(FLAGS_http_port, "http_port");
        options.websocket_port = checked_port(
            FLAGS_websocket_port, "websocket_port");
        options.max_http_body_size = static_cast<std::size_t>(
            FLAGS_max_http_body_mb * 1024ULL * 1024ULL);

        chat::gateway::GatewayServer server(options, std::move(rpc));
        std::string error;
        if (!server.start(error)) {
            spdlog::error("GatewayServer startup failed: {}", error);
            return 1;
        }

        std::signal(SIGINT, request_stop);
        std::signal(SIGTERM, request_stop);
        spdlog::info(
            "GatewayServer started: HTTP={}:{}, WebSocket={}:{}, speech={}",
            FLAGS_listen_address,
            server.http_port(),
            FLAGS_listen_address,
            server.websocket_port(),
            FLAGS_speech_server.empty() ? "disabled" : FLAGS_speech_server);
        while (!stop_requested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        server.stop();
        spdlog::info("GatewayServer stopped");
        return 0;
    } catch (const std::exception& exception) {
        spdlog::error("GatewayServer startup failed: {}", exception.what());
        return 1;
    }
}
