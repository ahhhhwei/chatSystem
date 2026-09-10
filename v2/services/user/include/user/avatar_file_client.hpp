#pragma once

#include <brpc/channel.h>
#include "chat/infra/etcd.hpp"

#include <cstdint>
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

namespace chat::user {

class AvatarFileClient {
public:
    virtual ~AvatarFileClient() = default;

    virtual bool put(
        const std::string& request_id,
        const std::string& content,
        std::string& file_id,
        std::string& error) = 0;
    virtual bool get_multi(
        const std::string& request_id,
        const std::vector<std::string>& file_ids,
        std::unordered_map<std::string, std::string>& files,
        std::string& error) = 0;
};

class BrpcAvatarFileClient final : public AvatarFileClient {
public:
    BrpcAvatarFileClient(std::string server_address, std::int32_t timeout_ms);
    BrpcAvatarFileClient(std::shared_ptr<infra::EndpointResolver> resolver,
        std::int32_t timeout_ms);

    bool put(
        const std::string& request_id,
        const std::string& content,
        std::string& file_id,
        std::string& error) override;
    bool get_multi(
        const std::string& request_id,
        const std::vector<std::string>& file_ids,
        std::unordered_map<std::string, std::string>& files,
        std::string& error) override;

private:
    std::shared_ptr<infra::EndpointResolver> resolver_;
    std::int32_t timeout_ms_;
};

}  // namespace chat::user
