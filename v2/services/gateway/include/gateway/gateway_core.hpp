#pragma once

#include "gateway/rpc_client.hpp"
#include "notify.pb.h"

#include <memory>
#include <string>

namespace chat::gateway {

inline constexpr const char* kProtobufContentType = "application/x-protbuf";

struct HttpResponse {
    int status = 200;
    std::string content_type = kProtobufContentType;
    std::string body;
};

class NotificationSink {
public:
    virtual ~NotificationSink() = default;
    virtual bool send(
        const std::string& user_id,
        const ahwei_im::NotifyMessage& notification) = 0;
};

class GatewayCore final {
public:
    GatewayCore(
        std::shared_ptr<RpcClient> rpc_client,
        std::shared_ptr<NotificationSink> notifications);

    HttpResponse handle_http(
        const std::string& path,
        const std::string& body);

    bool authenticate_websocket(
        const std::string& body,
        std::string& user_id,
        std::string& session_id,
        std::string& error);

    void revoke_session(const std::string& session_id);

private:
    bool resolve_session(
        const std::string& request_id,
        const std::string& session_id,
        std::string& user_id,
        std::string& error);
    bool get_user(
        const std::string& request_id,
        const std::string& user_id,
        ahwei_im::UserInfo& user,
        std::string& error);

    HttpResponse friend_add(const std::string& body);
    HttpResponse friend_add_process(const std::string& body);
    HttpResponse friend_remove(const std::string& body);
    HttpResponse chat_session_create(const std::string& body);
    HttpResponse new_message(const std::string& body);

    std::shared_ptr<RpcClient> rpc_client_;
    std::shared_ptr<NotificationSink> notifications_;
};

}  // namespace chat::gateway
