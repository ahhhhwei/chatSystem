#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace chat::user {

struct PasswordDigest {
    std::string salt;
    std::string hash;
    std::uint32_t iterations = 0;
};

class PasswordHasher {
public:
    static bool hash(
        const std::string& password,
        PasswordDigest& digest,
        std::string& error);
    static bool verify(
        const std::string& password,
        const PasswordDigest& digest);
};

class VerificationCodeSender {
public:
    virtual ~VerificationCodeSender() = default;
    virtual bool send(
        const std::string& phone,
        const std::string& code,
        std::string& error) = 0;
};

// 开发环境不接付费短信平台，验证码打印在 UserServer 日志中。
class LoggingVerificationCodeSender final : public VerificationCodeSender {
public:
    bool send(
        const std::string& phone,
        const std::string& code,
        std::string& error) override;
};

class VerificationCodeManager {
public:
    using CodeGenerator = std::function<std::string()>;
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    VerificationCodeManager(
        std::shared_ptr<VerificationCodeSender> sender,
        std::chrono::seconds ttl = std::chrono::minutes(5),
        CodeGenerator code_generator = {},
        Clock clock = {});
    virtual ~VerificationCodeManager() = default;

    virtual bool issue(
        const std::string& phone,
        std::string& code_id,
        std::string& error);
    virtual bool consume(
        const std::string& phone,
        const std::string& code_id,
        const std::string& code,
        std::string& error);

private:
    struct Entry {
        std::string phone;
        std::string code;
        std::chrono::steady_clock::time_point expires_at;
    };

    static std::string make_code();

    std::shared_ptr<VerificationCodeSender> sender_;
    std::chrono::seconds ttl_;
    CodeGenerator code_generator_;
    Clock clock_;
    std::mutex mutex_;
    std::unordered_map<std::string, Entry> codes_;
};

class SessionManager {
public:
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    explicit SessionManager(
        std::chrono::seconds ttl = std::chrono::hours(24),
        Clock clock = {});
    virtual ~SessionManager() = default;

    virtual bool login(
        const std::string& user_id,
        std::string& session_id,
        std::string& error);
    virtual bool resolve(
        const std::string& session_id,
        std::string& user_id,
        std::string& error);
    virtual bool revoke(const std::string& session_id, std::string& error);

private:
    struct Entry {
        std::string user_id;
        std::chrono::steady_clock::time_point expires_at;
    };

    void remove_expired_user_locked(const std::string& user_id);

    std::chrono::seconds ttl_;
    Clock clock_;
    std::mutex mutex_;
    std::unordered_map<std::string, Entry> sessions_;
    std::unordered_map<std::string, std::string> active_users_;
};

std::string make_random_id();

}  // namespace chat::user
