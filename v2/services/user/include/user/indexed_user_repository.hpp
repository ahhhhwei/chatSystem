#pragma once

#include "chat/infra/elasticsearch.hpp"
#include "user/user_store.hpp"

#include <memory>

namespace chat::user {

class IndexedUserRepository final : public UserRepository {
public:
    IndexedUserRepository(std::shared_ptr<UserRepository> backing,
        std::shared_ptr<infra::ElasticsearchClient> search);

    bool insert(const UserRecord& user, std::string& error) override;
    bool update(const UserRecord& user, std::string& error) override;
    std::optional<UserRecord> find_by_id(std::string_view id) const override;
    std::optional<UserRecord> find_by_nickname(std::string_view value) const override;
    std::optional<UserRecord> find_by_phone(std::string_view value) const override;
    std::vector<UserRecord> find_multi(
        const std::vector<std::string>& ids) const override;
    std::vector<UserRecord> search(std::string_view key,
        const std::vector<std::string>& excluded,
        std::size_t limit) const override;

private:
    void index(const UserRecord& user) const;
    std::shared_ptr<UserRepository> backing_;
    std::shared_ptr<infra::ElasticsearchClient> search_;
};

}  // namespace chat::user
