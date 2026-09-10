#include "chat/infra/redis.hpp"

#include <hiredis/hiredis.h>

#include <chrono>
#include <cstdlib>
#include <utility>

namespace chat::infra {
namespace {

void free_reply(redisReply* reply) {
    if (reply != nullptr) {
        freeReplyObject(reply);
    }
}

bool reply_ok(redisReply* reply, std::string& error) {
    if (reply == nullptr) {
        if (error.empty()) {
            error = "Redis returned no reply";
        }
        return false;
    }
    if (reply->type == REDIS_REPLY_ERROR) {
        error.assign(reply->str, static_cast<std::size_t>(reply->len));
        return false;
    }
    return true;
}

}  // namespace

RedisClient::RedisClient(RedisConfig config) : config_(std::move(config)) {}

RedisClient::~RedisClient() {
    std::lock_guard lock(mutex_);
    disconnect_locked();
}

bool RedisClient::connect_locked(std::string& error) {
    if (context_ != nullptr && context_->err == 0) {
        return true;
    }
    disconnect_locked();
    const timeval timeout{
        config_.timeout_ms / 1000,
        (config_.timeout_ms % 1000) * 1000};
    context_ = redisConnectWithTimeout(
        config_.host.c_str(), config_.port, timeout);
    if (context_ == nullptr) {
        error = "cannot allocate Redis context";
        return false;
    }
    if (context_->err != 0) {
        error = context_->errstr;
        disconnect_locked();
        return false;
    }
    if (!config_.password.empty()) {
        redisReply* reply = static_cast<redisReply*>(redisCommand(
            context_, "AUTH %b", config_.password.data(), config_.password.size()));
        const bool ok = reply_ok(reply, error);
        free_reply(reply);
        if (!ok) {
            disconnect_locked();
            return false;
        }
    }
    if (config_.database != 0) {
        redisReply* reply = static_cast<redisReply*>(redisCommand(
            context_, "SELECT %d", config_.database));
        const bool ok = reply_ok(reply, error);
        free_reply(reply);
        if (!ok) {
            disconnect_locked();
            return false;
        }
    }
    return true;
}

redisReply* RedisClient::command_locked(
    const std::vector<std::string>& arguments,
    std::string& error) {
    if (!connect_locked(error)) {
        return nullptr;
    }
    std::vector<const char*> values;
    std::vector<std::size_t> lengths;
    values.reserve(arguments.size());
    lengths.reserve(arguments.size());
    for (const auto& argument : arguments) {
        values.push_back(argument.data());
        lengths.push_back(argument.size());
    }
    redisReply* reply = static_cast<redisReply*>(redisCommandArgv(
        context_,
        static_cast<int>(values.size()),
        values.data(),
        lengths.data()));
    if (reply == nullptr) {
        error = context_ != nullptr && context_->err != 0
            ? context_->errstr
            : "Redis command returned no reply";
        disconnect_locked();
    }
    return reply;
}

void RedisClient::disconnect_locked() {
    if (context_ != nullptr) {
        redisFree(context_);
        context_ = nullptr;
    }
}

bool RedisClient::ping(std::string& error) {
    std::lock_guard lock(mutex_);
    redisReply* reply = command_locked({"PING"}, error);
    const bool ok = reply_ok(reply, error) && reply->type == REDIS_REPLY_STATUS;
    free_reply(reply);
    return ok;
}

bool RedisClient::set_ex(
    const std::string& key_value,
    const std::string& value,
    std::int64_t ttl_seconds,
    std::string& error) {
    std::lock_guard lock(mutex_);
    redisReply* reply = command_locked(
        {"SET", key_value, value, "EX", std::to_string(ttl_seconds)}, error);
    const bool ok = reply_ok(reply, error) && reply->type == REDIS_REPLY_STATUS;
    free_reply(reply);
    return ok;
}

std::optional<std::string> RedisClient::get(
    const std::string& key_value,
    bool& found,
    std::string& error) {
    std::lock_guard lock(mutex_);
    found = false;
    redisReply* reply = command_locked({"GET", key_value}, error);
    if (!reply_ok(reply, error)) {
        free_reply(reply);
        return std::nullopt;
    }
    std::optional<std::string> result;
    if (reply->type == REDIS_REPLY_STRING) {
        found = true;
        result.emplace(reply->str, static_cast<std::size_t>(reply->len));
    } else if (reply->type != REDIS_REPLY_NIL) {
        error = "Redis GET returned an unexpected reply type";
    }
    free_reply(reply);
    return result;
}

bool RedisClient::del(const std::string& key_value, std::string& error) {
    std::lock_guard lock(mutex_);
    redisReply* reply = command_locked({"DEL", key_value}, error);
    const bool ok = reply_ok(reply, error) && reply->type == REDIS_REPLY_INTEGER;
    free_reply(reply);
    return ok;
}

bool RedisClient::eval(
    const std::string& script,
    const std::vector<std::string>& keys,
    const std::vector<std::string>& arguments,
    std::optional<std::string>& result,
    std::string& error) {
    std::vector<std::string> command{"EVAL", script, std::to_string(keys.size())};
    command.insert(command.end(), keys.begin(), keys.end());
    command.insert(command.end(), arguments.begin(), arguments.end());
    std::lock_guard lock(mutex_);
    redisReply* reply = command_locked(command, error);
    if (!reply_ok(reply, error)) {
        free_reply(reply);
        return false;
    }
    result.reset();
    if (reply->type == REDIS_REPLY_STRING || reply->type == REDIS_REPLY_STATUS) {
        result.emplace(reply->str, static_cast<std::size_t>(reply->len));
    } else if (reply->type == REDIS_REPLY_INTEGER) {
        result = std::to_string(reply->integer);
    } else if (reply->type != REDIS_REPLY_NIL) {
        error = "Redis EVAL returned an unexpected reply type";
        free_reply(reply);
        return false;
    }
    free_reply(reply);
    return true;
}

std::string RedisClient::key(const std::string& suffix) const {
    return config_.key_prefix + suffix;
}

const RedisConfig& RedisClient::config() const noexcept {
    return config_;
}

}  // namespace chat::infra

