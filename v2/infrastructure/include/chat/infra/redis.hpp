#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct redisContext;
struct redisReply;

namespace chat::infra {

struct RedisConfig {
    std::string host{"127.0.0.1"};
    std::uint16_t port = 6379;
    int database = 0;
    std::string password;
    int timeout_ms = 2000;
    std::string key_prefix{"ahwei:v2:"};
};

class RedisClient final {
public:
    explicit RedisClient(RedisConfig config);
    ~RedisClient();
    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;

    bool ping(std::string& error);
    bool set_ex(
        const std::string& key,
        const std::string& value,
        std::int64_t ttl_seconds,
        std::string& error);
    std::optional<std::string> get(
        const std::string& key,
        bool& found,
        std::string& error);
    bool del(const std::string& key, std::string& error);
    bool eval(
        const std::string& script,
        const std::vector<std::string>& keys,
        const std::vector<std::string>& arguments,
        std::optional<std::string>& result,
        std::string& error);

    std::string key(const std::string& suffix) const;
    const RedisConfig& config() const noexcept;

private:
    bool connect_locked(std::string& error);
    redisReply* command_locked(
        const std::vector<std::string>& arguments,
        std::string& error);
    void disconnect_locked();

    RedisConfig config_;
    std::mutex mutex_;
    redisContext* context_ = nullptr;
};

}  // namespace chat::infra

