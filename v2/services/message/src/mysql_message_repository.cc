#include "message/mysql_message_repository.hpp"

#include <algorithm>
#include <stdexcept>

namespace chat::message {

MysqlMessageRepository::MysqlMessageRepository(
    std::shared_ptr<infra::MysqlDatabase> database)
    : database_(std::move(database)) {
    if (!database_) throw std::invalid_argument("MySQL database cannot be null");
}

bool MysqlMessageRepository::append(
    const ahwei_im::MessageInfo& message, std::string& error) {
    error.clear();
    if (message.message_id().empty() || message.chat_session_id().empty() ||
        !message.has_sender() || message.sender().user_id().empty() ||
        !message.has_message()) {
        error = "invalid message";
        return false;
    }
    std::string record;
    if (!message.SerializeToString(&record)) {
        error = "cannot serialize message";
        return false;
    }
    std::string text;
    bool has_text = message.message().message_type() == ahwei_im::STRING &&
                    message.message().has_string_message();
    if (has_text) text = message.message().string_message().content();
    auto connection = database_->connect(error);
    if (!connection.valid()) return false;
    const std::string text_sql = has_text ? connection.quote(text) : "NULL";
    const std::string sql =
        "INSERT INTO messages(message_id,session_id,sender_id,message_time,text_content,record) VALUES(" +
        connection.quote(message.message_id()) + "," +
        connection.quote(message.chat_session_id()) + "," +
        connection.quote(message.sender().user_id()) + "," +
        std::to_string(message.timestamp()) + "," + text_sql + "," +
        connection.quote(record) + ")";
    if (!connection.execute(sql, error)) {
        if (error.find("Duplicate") != std::string::npos) error = "duplicate message_id";
        return false;
    }
    return true;
}

std::vector<ahwei_im::MessageInfo> MysqlMessageRepository::query_messages(
    const std::string& sql, bool reverse) const {
    std::vector<ahwei_im::MessageInfo> messages;
    std::string error;
    auto connection = database_->connect(error);
    if (!connection.valid()) return messages;
    infra::MysqlResult rows;
    if (!connection.query(sql, rows, error)) return messages;
    infra::MysqlResult::Row row;
    while (rows.next(row)) {
        if (row.empty() || !row[0]) continue;
        ahwei_im::MessageInfo message;
        if (message.ParseFromString(*row[0])) messages.push_back(std::move(message));
    }
    if (reverse) std::reverse(messages.begin(), messages.end());
    return messages;
}

std::vector<ahwei_im::MessageInfo> MysqlMessageRepository::range(
    std::string_view chat_session_id, std::int64_t start_time,
    std::int64_t over_time) const {
    std::string error;
    auto connection = database_->connect(error);
    if (!connection.valid()) return {};
    return query_messages(
        "SELECT record FROM messages WHERE session_id=" +
        connection.quote(chat_session_id) + " AND message_time>=" +
        std::to_string(start_time) + " AND message_time<=" +
        std::to_string(over_time) + " ORDER BY message_time,message_id", false);
}

std::vector<ahwei_im::MessageInfo> MysqlMessageRepository::recent(
    std::string_view chat_session_id, std::size_t count,
    std::int64_t current_time) const {
    if (count == 0) return {};
    std::string error;
    auto connection = database_->connect(error);
    if (!connection.valid()) return {};
    std::string sql = "SELECT record FROM messages WHERE session_id=" +
        connection.quote(chat_session_id);
    if (current_time > 0) sql += " AND message_time<=" + std::to_string(current_time);
    sql += " ORDER BY message_time DESC,message_id DESC LIMIT " + std::to_string(count);
    return query_messages(sql, true);
}

std::vector<ahwei_im::MessageInfo> MysqlMessageRepository::search(
    std::string_view chat_session_id, std::string_view search_key) const {
    std::string error;
    auto connection = database_->connect(error);
    if (!connection.valid()) return {};
    return query_messages(
        "SELECT record FROM messages WHERE session_id=" +
        connection.quote(chat_session_id) + " AND text_content LIKE " +
        connection.quote("%" + std::string(search_key) + "%") +
        " ORDER BY message_time,message_id", false);
}

}  // namespace chat::message
