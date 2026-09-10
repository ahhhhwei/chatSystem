#pragma once

#include "base.pb.h"
#include "chat/infra/etcd.hpp"

#include <brpc/channel.h>

#include <cstdint>
#include <optional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace chat::friend_service {

class UserDirectory {
public:
    virtual ~UserDirectory() = default;

    virtual bool get_multi(
        const std::string& request_id,
        const std::vector<std::string>& user_ids,
        std::unordered_map<std::string, ahwei_im::UserInfo>& users,
        std::string& error) = 0;
    virtual bool search(
        const std::string& request_id,
        const std::string& search_key,
        const std::vector<std::string>& excluded_user_ids,
        std::vector<ahwei_im::UserInfo>& users,
        std::string& error) = 0;
    virtual bool resolve_session(
        const std::string& request_id,
        const std::string& session_id,
        std::string& user_id,
        std::string& error) = 0;
};

class RecentMessageClient {
public:
    virtual ~RecentMessageClient() = default;

    virtual bool latest(
        const std::string& request_id,
        const std::string& chat_session_id,
        std::optional<ahwei_im::MessageInfo>& message,
        std::string& error) = 0;
};

class BrpcUserDirectory final : public UserDirectory {
public:
    BrpcUserDirectory(std::string server_address, std::int32_t timeout_ms);
    BrpcUserDirectory(std::shared_ptr<infra::EndpointResolver> resolver,
        std::int32_t timeout_ms);

    bool get_multi(
        const std::string& request_id,
        const std::vector<std::string>& user_ids,
        std::unordered_map<std::string, ahwei_im::UserInfo>& users,
        std::string& error) override;
    bool search(
        const std::string& request_id,
        const std::string& search_key,
        const std::vector<std::string>& excluded_user_ids,
        std::vector<ahwei_im::UserInfo>& users,
        std::string& error) override;
    bool resolve_session(
        const std::string& request_id,
        const std::string& session_id,
        std::string& user_id,
        std::string& error) override;

private:
    std::shared_ptr<infra::EndpointResolver> resolver_;
    std::int32_t timeout_ms_;
};

class BrpcRecentMessageClient final : public RecentMessageClient {
public:
    BrpcRecentMessageClient(
        std::string server_address,
        std::int32_t timeout_ms);
    BrpcRecentMessageClient(std::shared_ptr<infra::EndpointResolver> resolver,
        std::int32_t timeout_ms);

    bool latest(
        const std::string& request_id,
        const std::string& chat_session_id,
        std::optional<ahwei_im::MessageInfo>& message,
        std::string& error) override;

private:
    std::shared_ptr<infra::EndpointResolver> resolver_;
    std::int32_t timeout_ms_;
};

}  // namespace chat::friend_service
