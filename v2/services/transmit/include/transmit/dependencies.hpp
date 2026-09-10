#pragma once

#include "base.pb.h"
#include "chat/infra/rabbitmq.hpp"
#include "chat/infra/etcd.hpp"

#include <brpc/channel.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace chat::transmit {

class UserClient {
public:
    virtual ~UserClient() = default;

    virtual bool get_user(
        const std::string& request_id,
        const std::string& user_id,
        ahwei_im::UserInfo& user,
        std::string& error) = 0;
};

class SessionMemberRepository {
public:
    virtual ~SessionMemberRepository() = default;

    virtual bool members(
        const std::string& chat_session_id,
        std::vector<std::string>& user_ids,
        std::string& error) const = 0;
};

class MessagePublisher {
public:
    virtual ~MessagePublisher() = default;

    virtual bool publish(
        const std::string& request_id,
        const ahwei_im::MessageInfo& message,
        std::string& error) = 0;
};

// 仅供不启动 UserServer 的隔离测试使用：只填充 user_id。
class IdentityUserClient final : public UserClient {
public:
    bool get_user(
        const std::string& request_id,
        const std::string& user_id,
        ahwei_im::UserInfo& user,
        std::string& error) override;
};

// 通过 UserServer.GetUserInfo 获取完整发送者资料。
class BrpcUserClient final : public UserClient {
public:
    BrpcUserClient(std::string server_address, std::int32_t timeout_ms);
    BrpcUserClient(std::shared_ptr<infra::EndpointResolver> resolver,
        std::int32_t timeout_ms);

    bool get_user(
        const std::string& request_id,
        const std::string& user_id,
        ahwei_im::UserInfo& user,
        std::string& error) override;

private:
    std::shared_ptr<infra::EndpointResolver> resolver_;
    std::int32_t timeout_ms_;
};

// 每行一组会话成员：<chat_session_id> <user_id>。
class FileSessionMemberRepository final : public SessionMemberRepository {
public:
    explicit FileSessionMemberRepository(std::filesystem::path members_file);

    bool members(
        const std::string& chat_session_id,
        std::vector<std::string>& user_ids,
        std::string& error) const override;

    const std::filesystem::path& members_file() const noexcept;

private:
    void load();

    std::filesystem::path members_file_;
    std::unordered_map<std::string, std::vector<std::string>> members_;
};

class BrpcFriendSessionMemberRepository final
    : public SessionMemberRepository {
public:
    BrpcFriendSessionMemberRepository(
        std::string server_address,
        std::int32_t timeout_ms);
    BrpcFriendSessionMemberRepository(
        std::shared_ptr<infra::EndpointResolver> resolver,
        std::int32_t timeout_ms);

    bool members(
        const std::string& chat_session_id,
        std::vector<std::string>& user_ids,
        std::string& error) const override;

private:
    std::shared_ptr<infra::EndpointResolver> resolver_;
    std::int32_t timeout_ms_;
};

// 当前阶段直接调用 MessageStoreServer 投递消息。后续仅需把
// MessagePublisher 的实现换成 RabbitMQ，TransmitService 无需修改。
class BrpcMessagePublisher final : public MessagePublisher {
public:
    BrpcMessagePublisher(
        std::string server_address,
        std::int32_t timeout_ms);
    BrpcMessagePublisher(std::shared_ptr<infra::EndpointResolver> resolver,
        std::int32_t timeout_ms);

    bool publish(
        const std::string& request_id,
        const ahwei_im::MessageInfo& message,
        std::string& error) override;

private:
    std::shared_ptr<infra::EndpointResolver> resolver_;
    std::int32_t timeout_ms_;
};

class RabbitMqMessagePublisher final : public MessagePublisher {
public:
    explicit RabbitMqMessagePublisher(infra::RabbitMqConfig config);

    bool publish(const std::string& request_id,
        const ahwei_im::MessageInfo& message,
        std::string& error) override;

private:
    infra::RabbitMqPublisher publisher_;
};

}  // namespace chat::transmit
