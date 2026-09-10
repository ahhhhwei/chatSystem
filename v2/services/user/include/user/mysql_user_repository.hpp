#pragma once

#include "chat/infra/mysql.hpp"
#include "user/user_store.hpp"

#include <memory>

namespace chat::user {

class MysqlUserRepository final : public UserRepository {
public:
    explicit MysqlUserRepository(std::shared_ptr<infra::MysqlDatabase> database);

    bool insert(const UserRecord& user, std::string& error) override;
    bool update(const UserRecord& user, std::string& error) override;
    std::optional<UserRecord> find_by_id(std::string_view user_id) const override;
    std::optional<UserRecord> find_by_nickname(std::string_view nickname) const override;
    std::optional<UserRecord> find_by_phone(std::string_view phone) const override;
    std::vector<UserRecord> find_multi(
        const std::vector<std::string>& user_ids) const override;
    std::vector<UserRecord> search(
        std::string_view search_key,
        const std::vector<std::string>& excluded_user_ids,
        std::size_t limit) const override;

private:
    std::optional<UserRecord> find_one(
        const std::string& condition,
        std::string& error) const;
    std::shared_ptr<infra::MysqlDatabase> database_;
};

}  // namespace chat::user
