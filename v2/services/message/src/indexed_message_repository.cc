#include "message/indexed_message_repository.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace chat::message {

IndexedMessageRepository::IndexedMessageRepository(
    std::shared_ptr<MessageRepository> backing,
    std::shared_ptr<infra::ElasticsearchClient> search)
    : backing_(std::move(backing)), search_(std::move(search)) {
    if (!backing_ || !search_) throw std::invalid_argument("indexed message repository dependency is null");
}

bool IndexedMessageRepository::append(
    const ahwei_im::MessageInfo& message, std::string& error) {
    if (!backing_->append(message, error)) return false;
    if (message.message().message_type() == ahwei_im::STRING &&
        message.message().has_string_message()) {
        std::string ignored;
        search_->index_message(message.message_id(), message.chat_session_id(),
            message.timestamp(), message.message().string_message().content(), ignored);
    }
    return true;
}

std::vector<ahwei_im::MessageInfo> IndexedMessageRepository::range(
    std::string_view session, std::int64_t start, std::int64_t end) const {
    return backing_->range(session, start, end);
}

std::vector<ahwei_im::MessageInfo> IndexedMessageRepository::recent(
    std::string_view session, std::size_t count, std::int64_t current) const {
    return backing_->recent(session, count, current);
}

std::vector<ahwei_im::MessageInfo> IndexedMessageRepository::search(
    std::string_view session, std::string_view key) const {
    std::vector<std::string> ids;
    std::string error;
    if (!search_->search_messages(session, key, ids, error))
        return backing_->search(session, key);
    std::unordered_set<std::string> wanted(ids.begin(), ids.end());
    std::vector<ahwei_im::MessageInfo> result;
    for (auto& message : backing_->range(session,
            std::numeric_limits<std::int64_t>::min(),
            std::numeric_limits<std::int64_t>::max())) {
        if (wanted.erase(message.message_id())) result.push_back(std::move(message));
    }
    // Keep searches correct while a best-effort index write is catching up.
    std::unordered_set<std::string> seen;
    for (const auto& item : result) seen.insert(item.message_id());
    for (auto& item : backing_->search(session, key))
        if (seen.insert(item.message_id()).second) result.push_back(std::move(item));
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.timestamp() == right.timestamp()
            ? left.message_id() < right.message_id()
            : left.timestamp() < right.timestamp();
    });
    return result;
}

}  // namespace chat::message
