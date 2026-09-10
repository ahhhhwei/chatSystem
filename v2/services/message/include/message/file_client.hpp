#pragma once

#include <brpc/channel.h>
#include "chat/infra/etcd.hpp"

#include <cstdint>
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

namespace chat::message {

class FileClient {
public:
    virtual ~FileClient() = default;

    // 上传文件
    virtual bool put(
        const std::string& request_id,
        const std::string& file_name,
        const std::string& content,
        std::string& file_id,
        std::string& error) = 0;

    // 批量下载文件，一次拿多个文件，减少rpc次数
    virtual bool get_multi(
        const std::string& request_id,
        const std::vector<std::string>& file_ids,
        std::unordered_map<std::string, std::string>& files,
        std::string& error) = 0;
};

class BrpcFileClient final : public FileClient {
public:
    BrpcFileClient(std::string server_address, std::int32_t timeout_ms);
    BrpcFileClient(std::shared_ptr<infra::EndpointResolver> resolver,
        std::int32_t timeout_ms);

    bool put(
        const std::string& request_id,
        const std::string& file_name,
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

}  // namespace chat::message
