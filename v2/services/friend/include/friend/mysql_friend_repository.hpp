#pragma once

#include "chat/infra/mysql.hpp"
#include "friend/friend_store.hpp"

#include <memory>

namespace chat::friend_service {

class MysqlFriendRepository final : public FriendRepository {
public:
    explicit MysqlFriendRepository(std::shared_ptr<infra::MysqlDatabase> database);

    std::vector<std::string> friends(std::string_view user_id) const override;
    bool are_friends(std::string_view first, std::string_view second) const override;
    bool add_application(const std::string& event_id,
        const std::string& applicant_id, const std::string& respondent_id,
        std::string& error) override;
    std::vector<FriendApplication> pending_applications(
        std::string_view respondent_id) const override;
    bool process_application(const std::string& event_id,
        const std::string& applicant_id, const std::string& respondent_id,
        bool agree, const std::string& new_session_id,
        std::string& error) override;
    bool remove_friend(const std::string& user_id, const std::string& peer_id,
        std::string& error) override;
    bool create_group(const std::string& session_id, const std::string& name,
        const std::vector<std::string>& member_ids,
        std::string& error) override;
    std::vector<StoredChatSession> sessions_for(
        std::string_view user_id) const override;
    std::optional<StoredChatSession> session(
        std::string_view session_id) const override;
    std::vector<std::string> members(
        std::string_view session_id) const override;
    bool is_member(std::string_view session_id,
        std::string_view user_id) const override;

private:
    std::shared_ptr<infra::MysqlDatabase> database_;
};

}  // namespace chat::friend_service
