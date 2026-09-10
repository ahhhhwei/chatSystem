#include "user/mysql_user_repository.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace chat::user {
namespace {

std::optional<UserRecord> parse_first(infra::MysqlResult& result) {
    infra::MysqlResult::Row row;
    if (!result.next(row) || row.empty() || !row[0]) {
        return std::nullopt;
    }
    UserRecord user;
    if (!user.ParseFromString(*row[0])) {
        throw std::runtime_error("invalid UserRecord stored in MySQL");
    }
    return user;
}

}  // namespace

MysqlUserRepository::MysqlUserRepository(
    std::shared_ptr<infra::MysqlDatabase> database)
    : database_(std::move(database)) {
    if (!database_) {
        throw std::invalid_argument("MySQL database cannot be null");
    }
}

bool MysqlUserRepository::insert(const UserRecord& user, std::string& error) {
    error.clear();
    if (user.user_id().empty() || user.nickname().empty()) {
        error = "invalid user";
        return false;
    }
    std::string record;
    if (!user.SerializeToString(&record)) {
        error = "cannot serialize user record";
        return false;
    }
    auto connection = database_->connect(error);
    if (!connection.valid()) return false;
    const std::string phone = user.phone().empty()
        ? "NULL" : connection.quote(user.phone());
    const std::string sql =
        "INSERT INTO users(user_id,nickname,phone,record) VALUES(" +
        connection.quote(user.user_id()) + "," +
        connection.quote(user.nickname()) + "," + phone + "," +
        connection.quote(record) + ")";
    if (!connection.execute(sql, error)) {
        if (error.find("uk_users_nickname") != std::string::npos) {
            error = "nickname already exists";
        } else if (error.find("uk_users_phone") != std::string::npos) {
            error = "phone number already exists";
        } else if (error.find("PRIMARY") != std::string::npos) {
            error = "duplicate user_id";
        }
        return false;
    }
    return true;
}

bool MysqlUserRepository::update(const UserRecord& user, std::string& error) {
    error.clear();
    std::string record;
    if (user.user_id().empty() || user.nickname().empty() ||
        !user.SerializeToString(&record)) {
        error = "invalid user";
        return false;
    }
    auto connection = database_->connect(error);
    if (!connection.valid()) return false;
    const std::string phone = user.phone().empty()
        ? "NULL" : connection.quote(user.phone());
    const std::string sql = "UPDATE users SET nickname=" +
        connection.quote(user.nickname()) + ",phone=" + phone +
        ",record=" + connection.quote(record) + " WHERE user_id=" +
        connection.quote(user.user_id());
    if (!connection.execute(sql, error)) {
        if (error.find("uk_users_nickname") != std::string::npos) {
            error = "nickname already exists";
        } else if (error.find("uk_users_phone") != std::string::npos) {
            error = "phone number already exists";
        }
        return false;
    }
    if (connection.affected_rows() == 0 && !find_by_id(user.user_id())) {
        error = "user not found";
        return false;
    }
    return true;
}

std::optional<UserRecord> MysqlUserRepository::find_one(
    const std::string& condition,
    std::string& error) const {
    auto connection = database_->connect(error);
    if (!connection.valid()) return std::nullopt;
    infra::MysqlResult result;
    if (!connection.query(
            "SELECT record FROM users WHERE " + condition + " LIMIT 1",
            result,
            error)) {
        return std::nullopt;
    }
    return parse_first(result);
}

std::optional<UserRecord> MysqlUserRepository::find_by_id(
    std::string_view user_id) const {
    std::string error;
    auto connection = database_->connect(error);
    if (!connection.valid()) return std::nullopt;
    infra::MysqlResult result;
    if (!connection.query(
            "SELECT record FROM users WHERE user_id=" +
                connection.quote(user_id) + " LIMIT 1",
            result,
            error)) return std::nullopt;
    return parse_first(result);
}

std::optional<UserRecord> MysqlUserRepository::find_by_nickname(
    std::string_view nickname) const {
    std::string error;
    auto connection = database_->connect(error);
    if (!connection.valid()) return std::nullopt;
    infra::MysqlResult result;
    if (!connection.query("SELECT record FROM users WHERE nickname=" +
        connection.quote(nickname) + " LIMIT 1", result, error)) return std::nullopt;
    return parse_first(result);
}

std::optional<UserRecord> MysqlUserRepository::find_by_phone(
    std::string_view phone) const {
    if (phone.empty()) return std::nullopt;
    std::string error;
    auto connection = database_->connect(error);
    if (!connection.valid()) return std::nullopt;
    infra::MysqlResult result;
    if (!connection.query("SELECT record FROM users WHERE phone=" +
        connection.quote(phone) + " LIMIT 1", result, error)) return std::nullopt;
    return parse_first(result);
}

std::vector<UserRecord> MysqlUserRepository::find_multi(
    const std::vector<std::string>& user_ids) const {
    std::vector<UserRecord> users;
    std::unordered_set<std::string> seen;
    for (const auto& id : user_ids) {
        if (!seen.insert(id).second) continue;
        auto user = find_by_id(id);
        if (user) users.push_back(std::move(*user));
    }
    return users;
}

std::vector<UserRecord> MysqlUserRepository::search(
    std::string_view search_key,
    const std::vector<std::string>& excluded_user_ids,
    std::size_t limit) const {
    std::vector<UserRecord> users;
    if (limit == 0) return users;
    std::string error;
    auto connection = database_->connect(error);
    if (!connection.valid()) return users;
    std::string sql = "SELECT record FROM users WHERE (user_id LIKE " +
        connection.quote("%" + std::string(search_key) + "%") +
        " OR nickname LIKE " +
        connection.quote("%" + std::string(search_key) + "%") +
        " OR COALESCE(phone,'') LIKE " +
        connection.quote("%" + std::string(search_key) + "%") + ")";
    if (!excluded_user_ids.empty()) {
        sql += " AND user_id NOT IN (";
        for (std::size_t i = 0; i < excluded_user_ids.size(); ++i) {
            if (i) sql += ',';
            sql += connection.quote(excluded_user_ids[i]);
        }
        sql += ')';
    }
    sql += " ORDER BY user_id LIMIT " + std::to_string(limit);
    infra::MysqlResult result;
    if (!connection.query(sql, result, error)) return users;
    infra::MysqlResult::Row row;
    while (result.next(row)) {
        if (row.empty() || !row[0]) continue;
        UserRecord user;
        if (user.ParseFromString(*row[0])) users.push_back(std::move(user));
    }
    return users;
}

}  // namespace chat::user
