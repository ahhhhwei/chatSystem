#include "user/redis_security.hpp"

#include <random>
#include <stdexcept>

namespace chat::user {

RedisVerificationCodeManager::RedisVerificationCodeManager(
    std::shared_ptr<VerificationCodeSender> sender,
    std::shared_ptr<infra::RedisClient> redis,
    std::chrono::seconds ttl)
    : VerificationCodeManager(sender, ttl),
      sender_impl_(std::move(sender)),
      redis_(std::move(redis)),
      redis_ttl_(ttl) {
    if (!redis_) throw std::invalid_argument("Redis client cannot be null");
}

std::string RedisVerificationCodeManager::generate_code() {
    thread_local std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<int> digit(0, 9);
    std::string result(4, '0');
    for (char& value : result) value = static_cast<char>('0' + digit(generator));
    return result;
}

bool RedisVerificationCodeManager::issue(
    const std::string& phone, std::string& code_id, std::string& error) {
    code_id.clear();
    error.clear();
    const std::string code = generate_code();
    if (!sender_impl_->send(phone, code, error)) return false;
    code_id = make_random_id();
    if (!redis_->set_ex(
            redis_->key("verify:" + code_id), phone + "\n" + code,
            redis_ttl_.count(), error)) {
        code_id.clear();
        return false;
    }
    return true;
}

bool RedisVerificationCodeManager::consume(
    const std::string& phone, const std::string& code_id,
    const std::string& code, std::string& error) {
    static const std::string script =
        "local v=redis.call('GET',KEYS[1]); "
        "if not v then return 'missing' end; "
        "if v~=ARGV[1] then return 'incorrect' end; "
        "redis.call('DEL',KEYS[1]); return 'ok'";
    std::optional<std::string> result;
    if (!redis_->eval(script, {redis_->key("verify:" + code_id)},
            {phone + "\n" + code}, result, error)) return false;
    if (result == "ok") return true;
    error = result == "incorrect"
        ? "verification code is incorrect"
        : "verification code is invalid or expired";
    return false;
}

RedisSessionManager::RedisSessionManager(
    std::shared_ptr<infra::RedisClient> redis, std::chrono::seconds ttl)
    : SessionManager(ttl), redis_(std::move(redis)), redis_ttl_(ttl) {
    if (!redis_) throw std::invalid_argument("Redis client cannot be null");
}

bool RedisSessionManager::login(
    const std::string& user_id, std::string& session_id, std::string& error) {
    session_id.clear();
    error.clear();
    if (user_id.empty()) { error = "user_id cannot be empty"; return false; }
    session_id = make_random_id();
    static const std::string script =
        "if redis.call('EXISTS',KEYS[1])==1 then return 'exists' end; "
        "redis.call('SET',KEYS[1],ARGV[1],'EX',ARGV[3]); "
        "redis.call('SET',KEYS[2],ARGV[2],'EX',ARGV[3]); return 'ok'";
    std::optional<std::string> result;
    if (!redis_->eval(script,
            {redis_->key("user-session:" + user_id),
             redis_->key("session:" + session_id)},
            {session_id, user_id, std::to_string(redis_ttl_.count())},
            result, error)) {
        session_id.clear();
        return false;
    }
    if (result != "ok") {
        session_id.clear();
        error = "user is already logged in";
        return false;
    }
    return true;
}

bool RedisSessionManager::resolve(
    const std::string& session_id, std::string& user_id, std::string& error) {
    user_id.clear();
    bool found = false;
    auto result = redis_->get(redis_->key("session:" + session_id), found, error);
    if (!result || !found) {
        if (error.empty()) error = "session is invalid or expired";
        return false;
    }
    user_id = std::move(*result);
    return true;
}

bool RedisSessionManager::revoke(
    const std::string& session_id, std::string& error) {
    static const std::string script =
        "local u=redis.call('GET',KEYS[1]); if not u then return 'missing' end; "
        "redis.call('DEL',KEYS[1]); local uk=ARGV[1]..u; "
        "if redis.call('GET',uk)==ARGV[2] then redis.call('DEL',uk) end; return 'ok'";
    std::optional<std::string> result;
    if (!redis_->eval(script, {redis_->key("session:" + session_id)},
            {redis_->key("user-session:"), session_id}, result, error)) return false;
    if (result != "ok") {
        error = "session is invalid or expired";
        return false;
    }
    return true;
}

}  // namespace chat::user
