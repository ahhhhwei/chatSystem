#pragma once

#include "gateway/gateway_core.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace chat::gateway {

// Gateway 只需要 RFC 6455 的服务端子集：Upgrade、二进制消息、ping/pong、close。
// Qt 客户端发来的帧必须遵循标准并使用掩码。
class WebSocketServer final : public NotificationSink {
public:
    using Authenticator = std::function<bool(
        const std::string& body,
        std::string& user_id,
        std::string& session_id,
        std::string& error)>;
    using DisconnectHandler = std::function<void(const std::string& session_id)>;

    WebSocketServer(
        std::string listen_address,
        std::uint16_t port,
        std::size_t max_incoming_frame_size = 1024U * 1024U);
    ~WebSocketServer() override;

    WebSocketServer(const WebSocketServer&) = delete;
    WebSocketServer& operator=(const WebSocketServer&) = delete;

    void set_authenticator(Authenticator authenticator);
    void set_disconnect_handler(DisconnectHandler handler);

    bool start(std::string& error);
    void stop();

    bool send(
        const std::string& user_id,
        const ahwei_im::NotifyMessage& notification) override;

    bool running() const;
    std::uint16_t port() const;
    std::size_t online_count() const;

private:
    struct Connection;
    struct Frame;

    void accept_loop();
    void serve_connection(const std::shared_ptr<Connection>& connection);
    bool perform_handshake(const std::shared_ptr<Connection>& connection);
    bool read_frame(const std::shared_ptr<Connection>& connection, Frame& frame);
    bool send_frame(
        const std::shared_ptr<Connection>& connection,
        std::uint8_t opcode,
        const std::string& payload);
    void close_connection(
        const std::shared_ptr<Connection>& connection,
        std::uint16_t code,
        const std::string& reason);
    void bind_authenticated(
        const std::shared_ptr<Connection>& connection,
        std::string user_id,
        std::string session_id);
    void remove_connection(const std::shared_ptr<Connection>& connection);

    std::string listen_address_;
    std::uint16_t configured_port_;
    std::atomic<std::uint16_t> bound_port_{0};
    std::size_t max_incoming_frame_size_;
    std::atomic<bool> running_{false};
    int listen_fd_ = -1;
    std::thread accept_thread_;

    mutable std::mutex callbacks_mutex_;
    Authenticator authenticator_;
    DisconnectHandler disconnect_handler_;

    mutable std::mutex connections_mutex_;
    std::unordered_map<int, std::shared_ptr<Connection>> connections_;
    std::unordered_map<std::string, std::shared_ptr<Connection>> online_users_;
    std::vector<std::thread> connection_threads_;
};

}  // namespace chat::gateway
