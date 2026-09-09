#pragma once

#include "base.pb.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace chat::message {

// 仓储模式：业务代码不要直接操作数据库，中间加一层 MessageRepository
class MessageRepository {
public:
    virtual ~MessageRepository() = default;

    // 添加一条消息
    virtual bool append(
        const ahwei_im::MessageInfo& message,
        std::string& error) = 0;

    // 查询时间范围
    virtual std::vector<ahwei_im::MessageInfo> range(
        std::string_view chat_session_id,
        std::int64_t start_time,
        std::int64_t over_time) const = 0;

    // 查询最近 N 条
    virtual std::vector<ahwei_im::MessageInfo> recent(
        std::string_view chat_session_id,
        std::size_t count,
        std::int64_t current_time = 0) const = 0;

    // 在某个聊天会话中，根据关键词搜索消息
    virtual std::vector<ahwei_im::MessageInfo> search(
        std::string_view chat_session_id,
        std::string_view search_key) const = 0;
};

// MessageStore 保存已经去掉文件正文的消息元数据。
// 当前独立开发阶段使用本地追加日志持久化；后续可在不改 RPC 的情况下
// 替换为 v1 的 MySQL + Elasticsearch 实现。
class MessageStore final : public MessageRepository {
public:
    explicit MessageStore(std::filesystem::path storage_file);

    bool append(
        const ahwei_im::MessageInfo& message,
        std::string& error) override;

    std::vector<ahwei_im::MessageInfo> range(
        std::string_view chat_session_id,
        std::int64_t start_time,
        std::int64_t over_time) const override;

    std::vector<ahwei_im::MessageInfo> recent(
        std::string_view chat_session_id,
        std::size_t count,
        std::int64_t current_time = 0) const override;

    std::vector<ahwei_im::MessageInfo> search(
        std::string_view chat_session_id,
        std::string_view search_key) const override;

    // 返回当前保存多少消息
    std::size_t size() const;
    const std::filesystem::path& storage_file() const noexcept;

private:
    // 验证消息是否合法
    static bool validate(
        const ahwei_im::MessageInfo& message,
        std::string& error);
    // 启动时加载历史数据
    void load();

    std::filesystem::path storage_file_;
    // mutable：允许一个类的成员变量，在const成员函数中被修改
    // 为什么要搞个mutable破坏规则？因为有一些变量，从逻辑上不属于对象状态
    // 例如mutux，在上面的search函数中，搜索不会改变消息，所以设置为const
    // 但是线程安全需要锁
    // 在开发中mutable并不常用，主要用于mutex、cache等
    mutable std::mutex mutex_;
    std::vector<ahwei_im::MessageInfo> messages_;   // 消息数组
    std::unordered_set<std::string> message_ids_;   // 消息 id 集合
};

}  // namespace chat::message
