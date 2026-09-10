#include "gateway/gateway_server.hpp"
#include "gateway/rpc_client.hpp"
#include "chat/infra/redis.hpp"

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
DEFINE_string(infrastructure_mode, "local", "Discovery/presence mode: local or v1");
DEFINE_string(etcd_endpoint, "http://127.0.0.1:2379", "etcd URL");
DEFINE_string(redis_host, "127.0.0.1", "Redis host");
DEFINE_int32(redis_port, 6379, "Redis port");
DEFINE_string(redis_password, "", "Redis password");

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
        if (FLAGS_infrastructure_mode != "local" &&
            FLAGS_infrastructure_mode != "v1")
            throw std::invalid_argument("infrastructure_mode must be local or v1");
        const bool v1 = FLAGS_infrastructure_mode == "v1";
        chat::gateway::RpcEndpoints endpoints;
        endpoints.user = {FLAGS_user_server, FLAGS_rpc_timeout_ms};
        endpoints.friend_service = {FLAGS_friend_server, FLAGS_rpc_timeout_ms};
        endpoints.message = {FLAGS_message_server, FLAGS_rpc_timeout_ms};
        endpoints.transmit = {FLAGS_transmit_server, FLAGS_rpc_timeout_ms};
        endpoints.file = {FLAGS_file_server, FLAGS_file_rpc_timeout_ms};
        endpoints.speech = {FLAGS_speech_server, FLAGS_file_rpc_timeout_ms};
        if (v1) {
            auto etcd = std::make_shared<chat::infra::EtcdClient>(FLAGS_etcd_endpoint);
            endpoints.user.resolver = std::make_shared<chat::infra::EtcdEndpointResolver>(
                etcd, "/service/user_service");
            endpoints.friend_service.resolver = std::make_shared<chat::infra::EtcdEndpointResolver>(
                etcd, "/service/friend_service");
            endpoints.message.resolver = std::make_shared<chat::infra::EtcdEndpointResolver>(
                etcd, "/service/message_service");
            endpoints.transmit.resolver = std::make_shared<chat::infra::EtcdEndpointResolver>(
                etcd, "/service/transmite_service");
            endpoints.file.resolver = std::make_shared<chat::infra::EtcdEndpointResolver>(
                etcd, "/service/file_service");
        }
        auto rpc = std::make_shared<chat::gateway::BrpcRpcClient>(endpoints);
        std::shared_ptr<chat::infra::RedisClient> presence_redis;
        if (v1) {
            chat::infra::RedisConfig config;
            config.host = FLAGS_redis_host;
            config.port = static_cast<std::uint16_t>(FLAGS_redis_port);
            config.password = FLAGS_redis_password;
            presence_redis = std::make_shared<chat::infra::RedisClient>(config);
            std::string error;
            if (!presence_redis->ping(error))
                throw std::runtime_error("Redis: " + error);
        }

        chat::gateway::GatewayOptions options;
        options.listen_address = FLAGS_listen_address;
        options.http_port = checked_port(FLAGS_http_port, "http_port");
        options.websocket_port = checked_port(
            FLAGS_websocket_port, "websocket_port");
        options.max_http_body_size = static_cast<std::size_t>(
            FLAGS_max_http_body_mb * 1024ULL * 1024ULL);

        chat::gateway::GatewayServer server(
            options, std::move(rpc), std::move(presence_redis));
        std::string error;
        if (!server.start(error)) {
            spdlog::error("GatewayServer startup failed: {}", error);
            return 1;
        }

        std::signal(SIGINT, request_stop);
        std::signal(SIGTERM, request_stop);
        spdlog::info(
            "GatewayServer started: mode={}, HTTP={}:{}, WebSocket={}:{}, speech={}",
            FLAGS_infrastructure_mode,
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
