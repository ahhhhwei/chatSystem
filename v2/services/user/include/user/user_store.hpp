#pragma once

#include "user_storage.pb.h"

#include <cstddef>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace chat::user {

using UserRecord = ahwei_im::internal::UserRecord;

class UserRepository {
public:
    virtual ~UserRepository() = default;

    virtual bool insert(const UserRecord& user, std::string& error) = 0;
    virtual bool update(const UserRecord& user, std::string& error) = 0;
    virtual std::optional<UserRecord> find_by_id(
        std::string_view user_id) const = 0;
    virtual std::optional<UserRecord> find_by_nickname(
        std::string_view nickname) const = 0;
    virtual std::optional<UserRecord> find_by_phone(
        std::string_view phone) const = 0;
    virtual std::vector<UserRecord> find_multi(
        const std::vector<std::string>& user_ids) const = 0;
    virtual std::vector<UserRecord> search(
        std::string_view search_key,
        const std::vector<std::string>& excluded_user_ids,
        std::size_t limit) const = 0;
};

// 当前阶段使用追加日志保存完整用户快照，最后一条快照为最新状态。
// 后续替换成 MySQL 时，UserService 不需要修改。
class UserStore final : public UserRepository {
public:
    explicit UserStore(std::filesystem::path storage_file);

    bool insert(const UserRecord& user, std::string& error) override;
    bool update(const UserRecord& user, std::string& error) override;
    std::optional<UserRecord> find_by_id(
        std::string_view user_id) const override;
    std::optional<UserRecord> find_by_nickname(
        std::string_view nickname) const override;
    std::optional<UserRecord> find_by_phone(
        std::string_view phone) const override;
    std::vector<UserRecord> find_multi(
        const std::vector<std::string>& user_ids) const override;
    std::vector<UserRecord> search(
        std::string_view search_key,
        const std::vector<std::string>& excluded_user_ids,
        std::size_t limit) const override;

    std::size_t size() const;
    const std::filesystem::path& storage_file() const noexcept;

private:
    static bool validate(const UserRecord& user, std::string& error);
    bool append_locked(const UserRecord& user, std::string& error);
    bool check_unique_locked(
        const UserRecord& user,
        std::string_view ignored_user_id,
        std::string& error) const;
    void replace_indexes_locked(
        const std::optional<UserRecord>& old_user,
        const UserRecord& new_user);
    void load();

    std::filesystem::path storage_file_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, UserRecord> users_;
    std::unordered_map<std::string, std::string> nickname_index_;
    std::unordered_map<std::string, std::string> phone_index_;
};

}  // namespace chat::user
