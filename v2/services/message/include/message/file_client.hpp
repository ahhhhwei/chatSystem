#pragma once

#include <brpc/channel.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace chat::message {

class FileClient {
public:
    virtual ~FileClient() = default;

    virtual bool put(
        const std::string& request_id,
        const std::string& file_name,
        const std::string& content,
        std::string& file_id,
        std::string& error) = 0;

    virtual bool get_multi(
        const std::string& request_id,
        const std::vector<std::string>& file_ids,
        std::unordered_map<std::string, std::string>& files,
        std::string& error) = 0;
};

class BrpcFileClient final : public FileClient {
public:
    BrpcFileClient(std::string server_address, std::int32_t timeout_ms);

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
    brpc::Channel channel_;
};

}  // namespace chat::message
