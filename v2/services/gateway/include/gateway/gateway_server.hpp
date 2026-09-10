#pragma once

#include "gateway/gateway_core.hpp"
#include "gateway/rpc_client.hpp"
#include "gateway/websocket_server.hpp"
#include "chat/infra/redis.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace chat::gateway {

struct GatewayOptions {
    std::string listen_address = "0.0.0.0";
    std::uint16_t http_port = 9000;
    std::uint16_t websocket_port = 9001;
    std::size_t max_http_body_size = 256U * 1024U * 1024U;
    std::size_t max_websocket_auth_size = 64U * 1024U;
};

class GatewayServer final {
public:
    GatewayServer(
        GatewayOptions options,
        std::shared_ptr<RpcClient> rpc_client,
        std::shared_ptr<infra::RedisClient> presence_redis = nullptr);
    ~GatewayServer();

    GatewayServer(const GatewayServer&) = delete;
    GatewayServer& operator=(const GatewayServer&) = delete;

    bool start(std::string& error);
    void stop();

    bool running() const;
    std::uint16_t http_port() const;
    std::uint16_t websocket_port() const;
    std::size_t online_count() const;

private:
    class HttpServer;

    GatewayOptions options_;
    std::shared_ptr<WebSocketServer> websocket_;
    std::shared_ptr<GatewayCore> core_;
    std::unique_ptr<HttpServer> http_;
};

}  // namespace chat::gateway
