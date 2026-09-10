#include "friend/friend_store.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace chat::friend_service {
namespace {

constexpr std::uint32_t kMaximumSnapshotSize = 64U * 1024U * 1024U;

std::array<char, 4> encode_size(std::uint32_t size) {
    return {
        static_cast<char>((size >> 24U) & 0xffU),
        static_cast<char>((size >> 16U) & 0xffU),
        static_cast<char>((size >> 8U) & 0xffU),
        static_cast<char>(size & 0xffU),
    };
}

std::uint32_t decode_size(const std::array<char, 4>& bytes) {
    return
        (static_cast<std::uint32_t>(
             static_cast<unsigned char>(bytes[0])) << 24U) |
        (static_cast<std::uint32_t>(
             static_cast<unsigned char>(bytes[1])) << 16U) |
        (static_cast<std::uint32_t>(
             static_cast<unsigned char>(bytes[2])) << 8U) |
        static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[3]));
}

std::pair<std::string, std::string> canonical_pair(
    std::string_view first,
    std::string_view second) {
    if (first < second) {
        return {std::string(first), std::string(second)};
    }
    return {std::string(second), std::string(first)};
}

std::string pair_key(std::string_view first, std::string_view second) {
    const auto pair = canonical_pair(first, second);
    return pair.first + '\0' + pair.second;
}

bool contains_member(
    const ahwei_im::internal::StoredChatSession& session,
    std::string_view user_id) {
    return std::find(
               session.member_ids().begin(),
               session.member_ids().end(),
               user_id) != session.member_ids().end();
}

}  // namespace

FriendStore::FriendStore(std::filesystem::path storage_file)
    : storage_file_(std::move(storage_file)) {
    if (storage_file_.empty()) {
        throw std::invalid_argument("friend storage file cannot be empty");
    }
    std::error_code error;
    const auto parent = storage_file_.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            throw std::runtime_error(
                "cannot create friend storage directory: " + error.message());
        }
    }
    load();
}

std::vector<std::string> FriendStore::friends(
    std::string_view user_id) const {
    std::vector<std::string> result;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& relation : state_.relations()) {
        if (relation.first_user_id() == user_id) {
            result.push_back(relation.second_user_id());
        } else if (relation.second_user_id() == user_id) {
            result.push_back(relation.first_user_id());
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

bool FriendStore::are_friends(
    std::string_view first_user_id,
    std::string_view second_user_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return relation_exists(state_, first_user_id, second_user_id);
}

bool FriendStore::add_application(
    const std::string& event_id,
    const std::string& applicant_id,
    const std::string& respondent_id,
    std::string& error) {
    error.clear();
    if (event_id.empty() || applicant_id.empty() || respondent_id.empty()) {
        error = "friend application field cannot be empty";
        return false;
    }
    if (applicant_id == respondent_id) {
        error = "cannot add yourself as a friend";
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (relation_exists(state_, applicant_id, respondent_id)) {
        error = "users are already friends";
        return false;
    }
    for (const auto& application : state_.applications()) {
        if (application.event_id() == event_id) {
            error = "duplicate friend event_id";
            return false;
        }
        const bool same_pair =
            (application.applicant_id() == applicant_id &&
             application.respondent_id() == respondent_id) ||
            (application.applicant_id() == respondent_id &&
             application.respondent_id() == applicant_id);
        if (same_pair) {
            error = "friend application already exists";
            return false;
        }
    }

    auto next = state_;
    auto* application = next.add_applications();
    application->set_event_id(event_id);
    application->set_applicant_id(applicant_id);
    application->set_respondent_id(respondent_id);
    return commit_locked(std::move(next), error);
}

std::vector<FriendApplication> FriendStore::pending_applications(
    std::string_view respondent_id) const {
    std::vector<FriendApplication> result;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& application : state_.applications()) {
        if (application.respondent_id() == respondent_id) {
            result.push_back(application);
        }
    }
    std::sort(
        result.begin(),
        result.end(),
        [](const FriendApplication& left, const FriendApplication& right) {
            return left.event_id() < right.event_id();
        });
    return result;
}

bool FriendStore::process_application(
    const std::string& event_id,
    const std::string& applicant_id,
    const std::string& respondent_id,
    bool agree,
    const std::string& new_session_id,
    std::string& error) {
    error.clear();
    std::lock_guard<std::mutex> lock(mutex_);

    int application_index = -1;
    for (int index = 0; index < state_.applications_size(); ++index) {
        const auto& application = state_.applications(index);
        if (application.event_id() == event_id &&
            application.applicant_id() == applicant_id &&
            application.respondent_id() == respondent_id) {
            application_index = index;
            break;
        }
    }
    if (application_index < 0) {
        error = "friend application not found";
        return false;
    }
    if (agree && new_session_id.empty()) {
        error = "new single chat session_id cannot be empty";
        return false;
    }
    if (agree && relation_exists(state_, applicant_id, respondent_id)) {
        error = "users are already friends";
        return false;
    }
    if (agree && session_id_exists(state_, new_session_id)) {
        error = "duplicate chat_session_id";
        return false;
    }

    auto next = state_;
    auto* applications = next.mutable_applications();
    applications->SwapElements(
        application_index, applications->size() - 1);
    applications->RemoveLast();
    if (agree) {
        const auto pair = canonical_pair(applicant_id, respondent_id);
        auto* relation = next.add_relations();
        relation->set_first_user_id(pair.first);
        relation->set_second_user_id(pair.second);

        auto* session = next.add_sessions();
        session->set_chat_session_id(new_session_id);
        session->set_type(ahwei_im::internal::SINGLE_SESSION);
        session->add_member_ids(applicant_id);
        session->add_member_ids(respondent_id);
    }
    return commit_locked(std::move(next), error);
}

bool FriendStore::remove_friend(
    const std::string& user_id,
    const std::string& peer_id,
    std::string& error) {
    error.clear();
    std::lock_guard<std::mutex> lock(mutex_);
    if (!relation_exists(state_, user_id, peer_id)) {
        error = "friend relationship not found";
        return false;
    }

    auto next = state_;
    auto* relations = next.mutable_relations();
    for (int index = 0; index < relations->size(); ++index) {
        const auto& relation = relations->Get(index);
        if (pair_key(relation.first_user_id(), relation.second_user_id()) ==
            pair_key(user_id, peer_id)) {
            relations->SwapElements(index, relations->size() - 1);
            relations->RemoveLast();
            break;
        }
    }
    auto* sessions = next.mutable_sessions();
    for (int index = sessions->size() - 1; index >= 0; --index) {
        const auto& session = sessions->Get(index);
        if (session.type() == ahwei_im::internal::SINGLE_SESSION &&
            session.member_ids_size() == 2 &&
            pair_key(session.member_ids(0), session.member_ids(1)) ==
                pair_key(user_id, peer_id)) {
            sessions->SwapElements(index, sessions->size() - 1);
            sessions->RemoveLast();
        }
    }
    return commit_locked(std::move(next), error);
}

bool FriendStore::create_group(
    const std::string& session_id,
    const std::string& name,
    const std::vector<std::string>& member_ids,
    std::string& error) {
    error.clear();
    if (session_id.empty() || name.empty() || member_ids.size() < 2) {
        error = "invalid group chat session";
        return false;
    }
    std::unordered_set<std::string> unique_members;
    for (const auto& member_id : member_ids) {
        if (member_id.empty() || !unique_members.insert(member_id).second) {
            error = "group members must be non-empty and unique";
            return false;
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (session_id_exists(state_, session_id)) {
        error = "duplicate chat_session_id";
        return false;
    }
    auto next = state_;
    auto* session = next.add_sessions();
    session->set_chat_session_id(session_id);
    session->set_chat_session_name(name);
    session->set_type(ahwei_im::internal::GROUP_SESSION);
    for (const auto& member_id : member_ids) {
        session->add_member_ids(member_id);
    }
    return commit_locked(std::move(next), error);
}

std::vector<StoredChatSession> FriendStore::sessions_for(
    std::string_view user_id) const {
    std::vector<StoredChatSession> result;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& session : state_.sessions()) {
        if (contains_member(session, user_id)) {
            result.push_back(session);
        }
    }
    std::sort(
        result.begin(),
        result.end(),
        [](const StoredChatSession& left, const StoredChatSession& right) {
            return left.chat_session_id() < right.chat_session_id();
        });
    return result;
}

std::optional<StoredChatSession> FriendStore::session(
    std::string_view session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& session : state_.sessions()) {
        if (session.chat_session_id() == session_id) {
            return session;
        }
    }
    return std::nullopt;
}

std::vector<std::string> FriendStore::members(
    std::string_view session_id) const {
    std::vector<std::string> result;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& session : state_.sessions()) {
        if (session.chat_session_id() == session_id) {
            result.assign(
                session.member_ids().begin(), session.member_ids().end());
            break;
        }
    }
    return result;
}

bool FriendStore::is_member(
    std::string_view session_id,
    std::string_view user_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& session : state_.sessions()) {
        if (session.chat_session_id() == session_id) {
            return contains_member(session, user_id);
        }
    }
    return false;
}

const std::filesystem::path& FriendStore::storage_file() const noexcept {
    return storage_file_;
}

bool FriendStore::validate_state(
    const ahwei_im::internal::FriendState& state,
    std::string& error) {
    if (state.revision() == 0) {
        error = "friend state revision cannot be zero";
        return false;
    }
    std::unordered_set<std::string> relation_pairs;
    for (const auto& relation : state.relations()) {
        if (relation.first_user_id().empty() ||
            relation.second_user_id().empty() ||
            relation.first_user_id() >= relation.second_user_id() ||
            !relation_pairs.insert(pair_key(
                relation.first_user_id(), relation.second_user_id())).second) {
            error = "invalid friend relation";
            return false;
        }
    }

    std::unordered_set<std::string> event_ids;
    std::unordered_set<std::string> application_pairs;
    for (const auto& application : state.applications()) {
        if (application.event_id().empty() ||
            application.applicant_id().empty() ||
            application.respondent_id().empty() ||
            application.applicant_id() == application.respondent_id() ||
            !event_ids.insert(application.event_id()).second ||
            !application_pairs.insert(pair_key(
                application.applicant_id(),
                application.respondent_id())).second) {
            error = "invalid friend application";
            return false;
        }
    }

    std::unordered_set<std::string> session_ids;
    std::unordered_set<std::string> single_pairs;
    for (const auto& session : state.sessions()) {
        if (session.chat_session_id().empty() ||
            !session_ids.insert(session.chat_session_id()).second) {
            error = "invalid chat session id";
            return false;
        }
        std::unordered_set<std::string> members;
        for (const auto& member : session.member_ids()) {
            if (member.empty() || !members.insert(member).second) {
                error = "invalid chat session members";
                return false;
            }
        }
        if (session.type() == ahwei_im::internal::SINGLE_SESSION) {
            if (session.member_ids_size() != 2 ||
                !session.chat_session_name().empty()) {
                error = "invalid single chat session";
                return false;
            }
            const std::string key = pair_key(
                session.member_ids(0), session.member_ids(1));
            if (relation_pairs.find(key) == relation_pairs.end() ||
                !single_pairs.insert(key).second) {
                error = "single chat session has no unique relation";
                return false;
            }
        } else if (session.type() == ahwei_im::internal::GROUP_SESSION) {
            if (session.chat_session_name().empty() ||
                session.member_ids_size() < 2) {
                error = "invalid group chat session";
                return false;
            }
        } else {
            error = "unknown chat session type";
            return false;
        }
    }
    return true;
}

bool FriendStore::relation_exists(
    const ahwei_im::internal::FriendState& state,
    std::string_view first_user_id,
    std::string_view second_user_id) {
    const std::string expected = pair_key(first_user_id, second_user_id);
    for (const auto& relation : state.relations()) {
        if (pair_key(relation.first_user_id(), relation.second_user_id()) ==
            expected) {
            return true;
        }
    }
    return false;
}

bool FriendStore::session_id_exists(
    const ahwei_im::internal::FriendState& state,
    std::string_view session_id) {
    for (const auto& session : state.sessions()) {
        if (session.chat_session_id() == session_id) {
            return true;
        }
    }
    return false;
}

bool FriendStore::commit_locked(
    ahwei_im::internal::FriendState next,
    std::string& error) {
    next.set_revision(state_.revision() + 1);
    if (!validate_state(next, error)) {
        return false;
    }
    std::string serialized;
    if (!next.SerializeToString(&serialized) || serialized.empty() ||
        serialized.size() > kMaximumSnapshotSize) {
        error = "cannot serialize friend state";
        return false;
    }

    std::ofstream output(storage_file_, std::ios::binary | std::ios::app);
    if (!output) {
        error = "cannot open friend storage file for writing";
        return false;
    }
    const auto header = encode_size(
        static_cast<std::uint32_t>(serialized.size()));
    output.write(header.data(), static_cast<std::streamsize>(header.size()));
    output.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
    output.flush();
    if (!output) {
        error = "cannot persist friend state";
        return false;
    }
    state_.Swap(&next);
    return true;
}

void FriendStore::load() {
    std::ifstream input(storage_file_, std::ios::binary);
    if (!input) {
        std::error_code error;
        if (std::filesystem::exists(storage_file_, error)) {
            throw std::runtime_error("cannot open friend storage file");
        }
        return;
    }

    std::uintmax_t valid_size = 0;
    bool has_partial_tail = false;
    while (true) {
        std::array<char, 4> header{};
        input.read(header.data(), static_cast<std::streamsize>(header.size()));
        if (input.gcount() == 0 && input.eof()) {
            break;
        }
        if (input.gcount() != static_cast<std::streamsize>(header.size())) {
            has_partial_tail = true;
            break;
        }
        const auto snapshot_size = decode_size(header);
        if (snapshot_size == 0 || snapshot_size > kMaximumSnapshotSize) {
            throw std::runtime_error("friend storage file is corrupted");
        }
        std::string serialized(snapshot_size, '\0');
        input.read(
            serialized.data(), static_cast<std::streamsize>(serialized.size()));
        if (input.gcount() != static_cast<std::streamsize>(serialized.size())) {
            has_partial_tail = true;
            break;
        }

        ahwei_im::internal::FriendState loaded;
        std::string error;
        if (!loaded.ParseFromString(serialized) ||
            !validate_state(loaded, error) ||
            loaded.revision() <= state_.revision()) {
            throw std::runtime_error("friend storage file is corrupted");
        }
        state_.Swap(&loaded);
        valid_size += header.size() + serialized.size();
    }
    input.close();

    if (has_partial_tail) {
        std::error_code error;
        std::filesystem::resize_file(storage_file_, valid_size, error);
        if (error) {
            throw std::runtime_error(
                "cannot recover partial friend snapshot: " + error.message());
        }
    }
}

}  // namespace chat::friend_service
