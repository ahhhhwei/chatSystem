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

class MessageRepository {
public:
    virtual ~MessageRepository() = default;

    virtual bool append(
        const ahwei_im::MessageInfo& message,
        std::string& error) = 0;

    virtual std::vector<ahwei_im::MessageInfo> range(
        std::string_view chat_session_id,
        std::int64_t start_time,
        std::int64_t over_time) const = 0;

    virtual std::vector<ahwei_im::MessageInfo> recent(
        std::string_view chat_session_id,
        std::size_t count,
        std::int64_t current_time = 0) const = 0;

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

    std::size_t size() const;
    const std::filesystem::path& storage_file() const noexcept;

private:
    static bool validate(
        const ahwei_im::MessageInfo& message,
        std::string& error);
    void load();

    std::filesystem::path storage_file_;
    mutable std::mutex mutex_;
    std::vector<ahwei_im::MessageInfo> messages_;
    std::unordered_set<std::string> message_ids_;
};

}  // namespace chat::message
