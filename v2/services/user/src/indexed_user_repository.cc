#include "user/indexed_user_repository.hpp"

#include <stdexcept>
#include <unordered_set>

namespace chat::user {

IndexedUserRepository::IndexedUserRepository(
    std::shared_ptr<UserRepository> backing,
    std::shared_ptr<infra::ElasticsearchClient> search)
    : backing_(std::move(backing)), search_(std::move(search)) {
    if (!backing_ || !search_) throw std::invalid_argument("indexed user repository dependency is null");
}

void IndexedUserRepository::index(const UserRecord& user) const {
    std::string ignored;
    search_->index_user(user.user_id(), user.nickname(), user.phone(), ignored);
}

bool IndexedUserRepository::insert(const UserRecord& user, std::string& error) {
    if (!backing_->insert(user, error)) return false;
    index(user);
    return true;
}

bool IndexedUserRepository::update(const UserRecord& user, std::string& error) {
    if (!backing_->update(user, error)) return false;
    index(user);
    return true;
}

std::optional<UserRecord> IndexedUserRepository::find_by_id(std::string_view id) const {
    return backing_->find_by_id(id);
}
std::optional<UserRecord> IndexedUserRepository::find_by_nickname(std::string_view value) const {
    return backing_->find_by_nickname(value);
}
std::optional<UserRecord> IndexedUserRepository::find_by_phone(std::string_view value) const {
    return backing_->find_by_phone(value);
}
std::vector<UserRecord> IndexedUserRepository::find_multi(
    const std::vector<std::string>& ids) const { return backing_->find_multi(ids); }

std::vector<UserRecord> IndexedUserRepository::search(
    std::string_view key, const std::vector<std::string>& excluded,
    std::size_t limit) const {
    std::vector<std::string> ids;
    std::string error;
    if (search_->search_users(key, excluded, limit, ids, error)) {
        auto indexed = backing_->find_multi(ids);
        if (indexed.size() >= limit) return indexed;
        std::unordered_set<std::string> seen;
        for (const auto& item : indexed) seen.insert(item.user_id());
        for (auto& item : backing_->search(key, excluded, limit)) {
            if (seen.insert(item.user_id()).second) indexed.push_back(std::move(item));
            if (indexed.size() == limit) break;
        }
        return indexed;
    }
    return backing_->search(key, excluded, limit);
}

}  // namespace chat::user
