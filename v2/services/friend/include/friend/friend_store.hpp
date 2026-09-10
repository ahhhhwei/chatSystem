#pragma once

#include "friend_storage.pb.h"

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chat::friend_service {

using FriendApplication = ahwei_im::internal::FriendApplication;
using StoredChatSession = ahwei_im::internal::StoredChatSession;

class FriendRepository {
public:
    virtual ~FriendRepository() = default;

    virtual std::vector<std::string> friends(
        std::string_view user_id) const = 0;
    virtual bool are_friends(
        std::string_view first_user_id,
        std::string_view second_user_id) const = 0;
    virtual bool add_application(
        const std::string& event_id,
        const std::string& applicant_id,
        const std::string& respondent_id,
        std::string& error) = 0;
    virtual std::vector<FriendApplication> pending_applications(
        std::string_view respondent_id) const = 0;
    virtual bool process_application(
        const std::string& event_id,
        const std::string& applicant_id,
        const std::string& respondent_id,
        bool agree,
        const std::string& new_session_id,
        std::string& error) = 0;
    virtual bool remove_friend(
        const std::string& user_id,
        const std::string& peer_id,
        std::string& error) = 0;
    virtual bool create_group(
        const std::string& session_id,
        const std::string& name,
        const std::vector<std::string>& member_ids,
        std::string& error) = 0;
    virtual std::vector<StoredChatSession> sessions_for(
        std::string_view user_id) const = 0;
    virtual std::optional<StoredChatSession> session(
        std::string_view session_id) const = 0;
    virtual std::vector<std::string> members(
        std::string_view session_id) const = 0;
    virtual bool is_member(
        std::string_view session_id,
        std::string_view user_id) const = 0;
};

// 一个 mutation 生成一份完整状态快照，因此同意申请时的“删除申请、建立
// 好友关系、创建单聊会话”要么全部成功，要么全部不生效。
class FriendStore final : public FriendRepository {
public:
    explicit FriendStore(std::filesystem::path storage_file);

    std::vector<std::string> friends(
        std::string_view user_id) const override;
    bool are_friends(
        std::string_view first_user_id,
        std::string_view second_user_id) const override;
    bool add_application(
        const std::string& event_id,
        const std::string& applicant_id,
        const std::string& respondent_id,
        std::string& error) override;
    std::vector<FriendApplication> pending_applications(
        std::string_view respondent_id) const override;
    bool process_application(
        const std::string& event_id,
        const std::string& applicant_id,
        const std::string& respondent_id,
        bool agree,
        const std::string& new_session_id,
        std::string& error) override;
    bool remove_friend(
        const std::string& user_id,
        const std::string& peer_id,
        std::string& error) override;
    bool create_group(
        const std::string& session_id,
        const std::string& name,
        const std::vector<std::string>& member_ids,
        std::string& error) override;
    std::vector<StoredChatSession> sessions_for(
        std::string_view user_id) const override;
    std::optional<StoredChatSession> session(
        std::string_view session_id) const override;
    std::vector<std::string> members(
        std::string_view session_id) const override;
    bool is_member(
        std::string_view session_id,
        std::string_view user_id) const override;

    const std::filesystem::path& storage_file() const noexcept;

private:
    static bool validate_state(
        const ahwei_im::internal::FriendState& state,
        std::string& error);
    static bool relation_exists(
        const ahwei_im::internal::FriendState& state,
        std::string_view first_user_id,
        std::string_view second_user_id);
    static bool session_id_exists(
        const ahwei_im::internal::FriendState& state,
        std::string_view session_id);
    bool commit_locked(
        ahwei_im::internal::FriendState next,
        std::string& error);
    void load();

    std::filesystem::path storage_file_;
    mutable std::mutex mutex_;
    ahwei_im::internal::FriendState state_;
};

}  // namespace chat::friend_service
