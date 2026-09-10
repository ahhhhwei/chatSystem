#pragma once

#include "chat/infra/mysql.hpp"
#include "message/message_store.hpp"

#include <memory>

namespace chat::message {

class MysqlMessageRepository final : public MessageRepository {
public:
    explicit MysqlMessageRepository(std::shared_ptr<infra::MysqlDatabase> database);

    bool append(const ahwei_im::MessageInfo& message,
        std::string& error) override;
    std::vector<ahwei_im::MessageInfo> range(
        std::string_view chat_session_id, std::int64_t start_time,
        std::int64_t over_time) const override;
    std::vector<ahwei_im::MessageInfo> recent(
        std::string_view chat_session_id, std::size_t count,
        std::int64_t current_time = 0) const override;
    std::vector<ahwei_im::MessageInfo> search(
        std::string_view chat_session_id,
        std::string_view search_key) const override;

private:
    std::vector<ahwei_im::MessageInfo> query_messages(
        const std::string& sql, bool reverse) const;
    std::shared_ptr<infra::MysqlDatabase> database_;
};

}  // namespace chat::message
