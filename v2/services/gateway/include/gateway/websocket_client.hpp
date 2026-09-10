#pragma once

#include "gateway.pb.h"
#include "notify.pb.h"

#include <cstdint>
#include <string>

namespace chat::gateway {

// gateway_client 使用的轻量同步客户端，只用于本地联调和协议示例。
class WebSocketClient final {
public:
    WebSocketClient() = default;
    ~WebSocketClient();

    WebSocketClient(const WebSocketClient&) = delete;
    WebSocketClient& operator=(const WebSocketClient&) = delete;

    bool connect(
        const std::string& host,
        std::uint16_t port,
        std::string& error);
    bool authenticate(
        const ahwei_im::ClientAuthenticationReq& request,
        std::string& error);
    bool receive_notification(
        ahwei_im::NotifyMessage& notification,
        int timeout_ms,
        std::string& error);
    void close();

private:
    bool send_frame(
        std::uint8_t opcode,
        const std::string& payload,
        std::string& error);
    bool read_frame(
        std::uint8_t& opcode,
        std::string& payload,
        int timeout_ms,
        std::string& error);

    int fd_ = -1;
};

}  // namespace chat::gateway
