#include "friend/mysql_friend_repository.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace chat::friend_service {
namespace {

std::pair<std::string, std::string> canonical(
    std::string_view first, std::string_view second) {
    return first < second
        ? std::make_pair(std::string(first), std::string(second))
        : std::make_pair(std::string(second), std::string(first));
}

std::string pair_value(std::string_view first, std::string_view second) {
    const auto pair = canonical(first, second);
    return pair.first + ":" + pair.second;
}

bool cell(const infra::MysqlResult::Row& row, std::size_t index,
          std::string& value) {
    if (index >= row.size() || !row[index]) return false;
    value = *row[index];
    return true;
}

}  // namespace

MysqlFriendRepository::MysqlFriendRepository(
    std::shared_ptr<infra::MysqlDatabase> database)
    : database_(std::move(database)) {
    if (!database_) throw std::invalid_argument("MySQL database cannot be null");
}

std::vector<std::string> MysqlFriendRepository::friends(
    std::string_view user_id) const {
    std::vector<std::string> result;
    std::string error;
    auto connection = database_->connect(error);
    if (!connection.valid()) return result;
    const auto id = connection.quote(user_id);
    infra::MysqlResult rows;
    if (!connection.query(
        "SELECT IF(first_user_id=" + id + ",second_user_id,first_user_id) "
        "FROM friend_relations WHERE first_user_id=" + id +
        " OR second_user_id=" + id + " ORDER BY 1", rows, error)) return result;
    infra::MysqlResult::Row row;
    while (rows.next(row)) {
        if (!row.empty() && row[0]) result.push_back(*row[0]);
    }
    return result;
}

bool MysqlFriendRepository::are_friends(
    std::string_view first, std::string_view second) const {
    const auto pair = canonical(first, second);
    std::string error;
    auto connection = database_->connect(error);
    if (!connection.valid()) return false;
    infra::MysqlResult rows;
    if (!connection.query(
        "SELECT 1 FROM friend_relations WHERE first_user_id=" +
        connection.quote(pair.first) + " AND second_user_id=" +
        connection.quote(pair.second) + " LIMIT 1", rows, error)) return false;
    return rows.size() != 0;
}

bool MysqlFriendRepository::add_application(
    const std::string& event_id, const std::string& applicant_id,
    const std::string& respondent_id, std::string& error) {
    error.clear();
    if (event_id.empty() || applicant_id.empty() || respondent_id.empty()) {
        error = "friend application field cannot be empty";
        return false;
    }
    if (applicant_id == respondent_id) {
        error = "cannot add yourself as a friend";
        return false;
    }
    if (are_friends(applicant_id, respondent_id)) {
        error = "users are already friends";
        return false;
    }
    auto connection = database_->connect(error);
    if (!connection.valid()) return false;
    const std::string sql =
        "INSERT INTO friend_applications(event_id,applicant_id,respondent_id,pair_key) VALUES(" +
        connection.quote(event_id) + "," + connection.quote(applicant_id) + "," +
        connection.quote(respondent_id) + "," +
        connection.quote(pair_value(applicant_id, respondent_id)) + ")";
    if (!connection.execute(sql, error)) {
        error = error.find("PRIMARY") != std::string::npos
            ? "duplicate friend event_id" : "friend application already exists";
        return false;
    }
    return true;
}

std::vector<FriendApplication> MysqlFriendRepository::pending_applications(
    std::string_view respondent_id) const {
    std::vector<FriendApplication> result;
    std::string error;
    auto connection = database_->connect(error);
    if (!connection.valid()) return result;
    infra::MysqlResult rows;
    if (!connection.query(
        "SELECT event_id,applicant_id,respondent_id FROM friend_applications "
        "WHERE respondent_id=" + connection.quote(respondent_id) +
        " ORDER BY event_id", rows, error)) return result;
    infra::MysqlResult::Row row;
    while (rows.next(row)) {
        if (row.size() < 3 || !row[0] || !row[1] || !row[2]) continue;
        auto& item = result.emplace_back();
        item.set_event_id(*row[0]);
        item.set_applicant_id(*row[1]);
        item.set_respondent_id(*row[2]);
    }
    return result;
}

bool MysqlFriendRepository::process_application(
    const std::string& event_id, const std::string& applicant_id,
    const std::string& respondent_id, bool agree,
    const std::string& new_session_id, std::string& error) {
    error.clear();
    if (agree && new_session_id.empty()) {
        error = "new single chat session_id cannot be empty";
        return false;
    }
    auto connection = database_->connect(error);
    if (!connection.valid() || !connection.begin(error)) return false;
    const auto fail = [&](std::string message) {
        connection.rollback(); error = std::move(message); return false;
    };
    infra::MysqlResult application;
    if (!connection.query(
        "SELECT 1 FROM friend_applications WHERE event_id=" +
        connection.quote(event_id) + " AND applicant_id=" +
        connection.quote(applicant_id) + " AND respondent_id=" +
        connection.quote(respondent_id) + " FOR UPDATE", application, error)) {
        connection.rollback(); return false;
    }
    if (application.size() == 0) return fail("friend application not found");
    if (agree) {
        const auto pair = canonical(applicant_id, respondent_id);
        if (!connection.execute(
            "INSERT INTO friend_relations(first_user_id,second_user_id) VALUES(" +
            connection.quote(pair.first) + "," + connection.quote(pair.second) + ")",
            error)) return fail("users are already friends");
        if (!connection.execute(
            "INSERT INTO chat_sessions(session_id,session_name,session_type) VALUES(" +
            connection.quote(new_session_id) + ",'',1)", error))
            return fail("duplicate chat_session_id");
        for (const auto* member : {&applicant_id, &respondent_id}) {
            if (!connection.execute(
                "INSERT INTO chat_session_members(session_id,user_id) VALUES(" +
                connection.quote(new_session_id) + "," + connection.quote(*member) + ")",
                error)) return fail(error);
        }
    }
    if (!connection.execute("DELETE FROM friend_applications WHERE event_id=" +
        connection.quote(event_id), error)) { connection.rollback(); return false; }
    if (!connection.commit(error)) { connection.rollback(); return false; }
    return true;
}

bool MysqlFriendRepository::remove_friend(
    const std::string& user_id, const std::string& peer_id,
    std::string& error) {
    error.clear();
    const auto pair = canonical(user_id, peer_id);
    auto connection = database_->connect(error);
    if (!connection.valid() || !connection.begin(error)) return false;
    if (!connection.execute(
        "DELETE FROM friend_relations WHERE first_user_id=" +
        connection.quote(pair.first) + " AND second_user_id=" +
        connection.quote(pair.second), error)) { connection.rollback(); return false; }
    if (connection.affected_rows() == 0) {
        connection.rollback(); error = "friend relationship not found"; return false;
    }
    const std::string first = connection.quote(user_id);
    const std::string second = connection.quote(peer_id);
    if (!connection.execute(
        "DELETE s FROM chat_sessions s "
        "JOIN chat_session_members a ON a.session_id=s.session_id AND a.user_id=" + first +
        " JOIN chat_session_members b ON b.session_id=s.session_id AND b.user_id=" + second +
        " WHERE s.session_type=1", error)) { connection.rollback(); return false; }
    if (!connection.commit(error)) { connection.rollback(); return false; }
    return true;
}

bool MysqlFriendRepository::create_group(
    const std::string& session_id, const std::string& name,
    const std::vector<std::string>& member_ids, std::string& error) {
    error.clear();
    std::unordered_set<std::string> unique(member_ids.begin(), member_ids.end());
    if (session_id.empty() || name.empty() || member_ids.size() < 2 ||
        unique.size() != member_ids.size() || unique.count("")) {
        error = "invalid group chat session";
        return false;
    }
    auto connection = database_->connect(error);
    if (!connection.valid() || !connection.begin(error)) return false;
    if (!connection.execute(
        "INSERT INTO chat_sessions(session_id,session_name,session_type) VALUES(" +
        connection.quote(session_id) + "," + connection.quote(name) + ",2)", error)) {
        connection.rollback(); error = "duplicate chat_session_id"; return false;
    }
    for (const auto& member : member_ids) {
        if (!connection.execute(
            "INSERT INTO chat_session_members(session_id,user_id) VALUES(" +
            connection.quote(session_id) + "," + connection.quote(member) + ")", error)) {
            connection.rollback(); return false;
        }
    }
    if (!connection.commit(error)) { connection.rollback(); return false; }
    return true;
}

std::vector<StoredChatSession> MysqlFriendRepository::sessions_for(
    std::string_view user_id) const {
    std::vector<StoredChatSession> result;
    std::string error;
    auto connection = database_->connect(error);
    if (!connection.valid()) return result;
    infra::MysqlResult rows;
    if (!connection.query(
        "SELECT s.session_id,s.session_name,s.session_type,m.user_id "
        "FROM chat_sessions s JOIN chat_session_members owner ON "
        "owner.session_id=s.session_id AND owner.user_id=" + connection.quote(user_id) +
        " JOIN chat_session_members m ON m.session_id=s.session_id "
        "ORDER BY s.session_id,m.user_id", rows, error)) return result;
    infra::MysqlResult::Row row;
    std::string current;
    while (rows.next(row)) {
        if (row.size() < 4 || !row[0] || !row[1] || !row[2] || !row[3]) continue;
        if (current != *row[0]) {
            current = *row[0];
            auto& item = result.emplace_back();
            item.set_chat_session_id(*row[0]);
            item.set_chat_session_name(*row[1]);
            item.set_type(static_cast<ahwei_im::internal::StoredChatSessionType>(
                std::stoi(*row[2])));
        }
        result.back().add_member_ids(*row[3]);
    }
    return result;
}

std::optional<StoredChatSession> MysqlFriendRepository::session(
    std::string_view session_id) const {
    std::string error;
    auto connection = database_->connect(error);
    if (!connection.valid()) return std::nullopt;
    infra::MysqlResult rows;
    if (!connection.query(
        "SELECT s.session_id,s.session_name,s.session_type,m.user_id "
        "FROM chat_sessions s JOIN chat_session_members m ON m.session_id=s.session_id "
        "WHERE s.session_id=" + connection.quote(session_id) + " ORDER BY m.user_id",
        rows, error)) return std::nullopt;
    infra::MysqlResult::Row row;
    StoredChatSession result;
    while (rows.next(row)) {
        if (row.size() < 4 || !row[0] || !row[1] || !row[2] || !row[3]) continue;
        if (result.chat_session_id().empty()) {
            result.set_chat_session_id(*row[0]);
            result.set_chat_session_name(*row[1]);
            result.set_type(static_cast<ahwei_im::internal::StoredChatSessionType>(
                std::stoi(*row[2])));
        }
        result.add_member_ids(*row[3]);
    }
    return result.chat_session_id().empty()
        ? std::optional<StoredChatSession>{} : std::optional<StoredChatSession>{result};
}

std::vector<std::string> MysqlFriendRepository::members(
    std::string_view session_id) const {
    std::vector<std::string> result;
    std::string error;
    auto connection = database_->connect(error);
    if (!connection.valid()) return result;
    infra::MysqlResult rows;
    if (!connection.query("SELECT user_id FROM chat_session_members WHERE session_id=" +
        connection.quote(session_id) + " ORDER BY user_id", rows, error)) return result;
    infra::MysqlResult::Row row;
    while (rows.next(row)) if (!row.empty() && row[0]) result.push_back(*row[0]);
    return result;
}

bool MysqlFriendRepository::is_member(
    std::string_view session_id, std::string_view user_id) const {
    std::string error;
    auto connection = database_->connect(error);
    if (!connection.valid()) return false;
    infra::MysqlResult rows;
    if (!connection.query(
        "SELECT 1 FROM chat_session_members WHERE session_id=" +
        connection.quote(session_id) + " AND user_id=" + connection.quote(user_id) +
        " LIMIT 1", rows, error)) return false;
    return rows.size() != 0;
}

}  // namespace chat::friend_service
