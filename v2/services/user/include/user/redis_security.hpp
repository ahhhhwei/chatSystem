#pragma once

#include "chat/infra/redis.hpp"
#include "user/security.hpp"

#include <memory>

namespace chat::user {

class RedisVerificationCodeManager final : public VerificationCodeManager {
public:
    RedisVerificationCodeManager(
        std::shared_ptr<VerificationCodeSender> sender,
        std::shared_ptr<infra::RedisClient> redis,
        std::chrono::seconds ttl = std::chrono::minutes(5));

    bool issue(const std::string& phone, std::string& code_id,
        std::string& error) override;
    bool consume(const std::string& phone, const std::string& code_id,
        const std::string& code, std::string& error) override;

private:
    static std::string generate_code();
    std::shared_ptr<VerificationCodeSender> sender_impl_;
    std::shared_ptr<infra::RedisClient> redis_;
    std::chrono::seconds redis_ttl_;
};

class RedisSessionManager final : public SessionManager {
public:
    RedisSessionManager(
        std::shared_ptr<infra::RedisClient> redis,
        std::chrono::seconds ttl = std::chrono::hours(24));

    bool login(const std::string& user_id, std::string& session_id,
        std::string& error) override;
    bool resolve(const std::string& session_id, std::string& user_id,
        std::string& error) override;
    bool revoke(const std::string& session_id, std::string& error) override;

private:
    std::shared_ptr<infra::RedisClient> redis_;
    std::chrono::seconds redis_ttl_;
};

}  // namespace chat::user
