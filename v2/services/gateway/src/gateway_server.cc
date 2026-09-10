#include "gateway/gateway_server.hpp"

#include "httplib.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <stdexcept>
#include <thread>
#include <utility>

namespace chat::gateway {

class GatewayServer::HttpServer {
public:
    HttpServer(
        std::string address,
        std::uint16_t configured_port,
        std::size_t max_body_size,
        std::shared_ptr<GatewayCore> core)
        : address_(std::move(address)),
          configured_port_(configured_port),
          core_(std::move(core)) {
        server_.set_payload_max_length(max_body_size);
        server_.set_read_timeout(30, 0);
        server_.set_write_timeout(30, 0);
        server_.Post(
            R"(/service/.*)",
            [this](const httplib::Request& request, httplib::Response& response) {
                const auto result = core_->handle_http(
                    request.path, request.body);
                response.status = result.status;
                response.set_content(result.body, result.content_type);
            });
    }

    ~HttpServer() {
        stop();
    }

    bool start(std::string& error) {
        if (thread_.joinable()) {
            error = "HTTP server is already running";
            return false;
        }
        int actual_port = configured_port_;
        if (configured_port_ == 0) {
            actual_port = server_.bind_to_any_port(address_);
            if (actual_port <= 0) {
                error = "cannot bind Gateway HTTP socket";
                return false;
            }
        } else if (!server_.bind_to_port(address_, configured_port_)) {
            error = "cannot bind Gateway HTTP socket";
            return false;
        }
        bound_port_ = static_cast<std::uint16_t>(actual_port);
        thread_ = std::thread([this] {
            if (!server_.listen_after_bind() && running_) {
                spdlog::error("Gateway HTTP server stopped unexpectedly");
            }
            running_ = false;
        });
        server_.wait_until_ready();
        if (!server_.is_running()) {
            if (thread_.joinable()) {
                thread_.join();
            }
            bound_port_ = 0;
            error = "cannot start Gateway HTTP server";
            return false;
        }
        running_ = true;
        error.clear();
        return true;
    }

    void stop() {
        running_ = false;
        server_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
        bound_port_ = 0;
    }

    bool running() const {
        return running_ && server_.is_running();
    }

    std::uint16_t port() const {
        return bound_port_;
    }

private:
    std::string address_;
    std::uint16_t configured_port_;
    std::shared_ptr<GatewayCore> core_;
    httplib::Server server_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint16_t> bound_port_{0};
};

GatewayServer::GatewayServer(
    GatewayOptions options,
    std::shared_ptr<RpcClient> rpc_client,
    std::shared_ptr<infra::RedisClient> presence_redis)
    : options_(std::move(options)),
      websocket_(std::make_shared<WebSocketServer>(
          options_.listen_address,
          options_.websocket_port,
          options_.max_websocket_auth_size)),
      core_(std::make_shared<GatewayCore>(
          std::move(rpc_client), websocket_)),
      http_(std::make_unique<HttpServer>(
          options_.listen_address,
          options_.http_port,
          options_.max_http_body_size,
          core_)) {
    std::weak_ptr<GatewayCore> weak_core = core_;
    websocket_->set_authenticator(
        [weak_core](
            const std::string& body,
            std::string& user_id,
            std::string& session_id,
            std::string& error) {
            const auto core = weak_core.lock();
            if (!core) {
                error = "Gateway is stopping";
                return false;
            }
            return core->authenticate_websocket(
                body, user_id, session_id, error);
        });
    websocket_->set_disconnect_handler(
        [weak_core](const std::string& session_id) {
            if (const auto core = weak_core.lock()) {
                core->revoke_session(session_id);
            }
        });
    if (presence_redis) {
        websocket_->set_presence_handler(
            [redis = std::move(presence_redis)](
                const std::string& user_id,
                const std::string& session_id,
                bool online) {
                std::string error;
                const std::string key = redis->key("online:" + user_id);
                if (online) {
                    if (!redis->set_ex(key, session_id, 7 * 24 * 60 * 60, error))
                        spdlog::warn("cannot record Redis online state: {}", error);
                    return;
                }
                static const std::string script =
                    "if redis.call('GET',KEYS[1])==ARGV[1] then "
                    "return redis.call('DEL',KEYS[1]) end return 0";
                std::optional<std::string> ignored;
                if (!redis->eval(script, {key}, {session_id}, ignored, error))
                    spdlog::warn("cannot clear Redis online state: {}", error);
            });
    }
}

GatewayServer::~GatewayServer() {
    stop();
}

bool GatewayServer::start(std::string& error) {
    if (!websocket_->start(error)) {
        return false;
    }
    if (!http_->start(error)) {
        websocket_->stop();
        return false;
    }
    error.clear();
    return true;
}

void GatewayServer::stop() {
    if (http_) {
        http_->stop();
    }
    if (websocket_) {
        websocket_->stop();
    }
}

bool GatewayServer::running() const {
    return http_->running() && websocket_->running();
}

std::uint16_t GatewayServer::http_port() const {
    return http_->port();
}

std::uint16_t GatewayServer::websocket_port() const {
    return websocket_->port();
}

std::size_t GatewayServer::online_count() const {
    return websocket_->online_count();
}

}  // namespace chat::gateway
