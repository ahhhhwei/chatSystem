#include "chat/infra/mysql.hpp"

#include <mysql/mysql.h>

#include <utility>

namespace chat::infra {

MysqlResult::MysqlResult(MYSQL_RES* result) : result_(result) {}

MysqlResult::~MysqlResult() {
    if (result_ != nullptr) {
        mysql_free_result(result_);
    }
}

MysqlResult::MysqlResult(MysqlResult&& other) noexcept
    : result_(std::exchange(other.result_, nullptr)) {}

MysqlResult& MysqlResult::operator=(MysqlResult&& other) noexcept {
    if (this != &other) {
        if (result_ != nullptr) {
            mysql_free_result(result_);
        }
        result_ = std::exchange(other.result_, nullptr);
    }
    return *this;
}

bool MysqlResult::next(Row& row) {
    if (result_ == nullptr) {
        return false;
    }
    MYSQL_ROW values = mysql_fetch_row(result_);
    if (values == nullptr) {
        return false;
    }
    const unsigned int count = mysql_num_fields(result_);
    const unsigned long* lengths = mysql_fetch_lengths(result_);
    row.clear();
    row.reserve(count);
    for (unsigned int index = 0; index < count; ++index) {
        if (values[index] == nullptr) {
            row.emplace_back(std::nullopt);
        } else {
            row.emplace_back(std::string(values[index], lengths[index]));
        }
    }
    return true;
}

std::size_t MysqlResult::size() const {
    return result_ == nullptr
        ? 0
        : static_cast<std::size_t>(mysql_num_rows(result_));
}

MysqlConnection::MysqlConnection(MYSQL* connection)
    : connection_(connection) {}

MysqlConnection::~MysqlConnection() {
    if (connection_ != nullptr) {
        mysql_close(connection_);
    }
}

MysqlConnection::MysqlConnection(MysqlConnection&& other) noexcept
    : connection_(std::exchange(other.connection_, nullptr)) {}

MysqlConnection& MysqlConnection::operator=(MysqlConnection&& other) noexcept {
    if (this != &other) {
        if (connection_ != nullptr) {
            mysql_close(connection_);
        }
        connection_ = std::exchange(other.connection_, nullptr);
    }
    return *this;
}

bool MysqlConnection::execute(const std::string& sql, std::string& error) {
    if (connection_ == nullptr) {
        error = "MySQL connection is not initialized";
        return false;
    }
    if (mysql_real_query(connection_, sql.data(), sql.size()) != 0) {
        error = mysql_error(connection_);
        return false;
    }
    MYSQL_RES* unexpected = mysql_store_result(connection_);
    if (unexpected != nullptr) {
        mysql_free_result(unexpected);
    }
    return true;
}

bool MysqlConnection::query(
    const std::string& sql,
    MysqlResult& result,
    std::string& error) {
    if (connection_ == nullptr) {
        error = "MySQL connection is not initialized";
        return false;
    }
    if (mysql_real_query(connection_, sql.data(), sql.size()) != 0) {
        error = mysql_error(connection_);
        return false;
    }
    MYSQL_RES* rows = mysql_store_result(connection_);
    if (rows == nullptr && mysql_field_count(connection_) != 0) {
        error = mysql_error(connection_);
        return false;
    }
    result = MysqlResult(rows);
    return true;
}

bool MysqlConnection::begin(std::string& error) {
    return execute("START TRANSACTION", error);
}

bool MysqlConnection::commit(std::string& error) {
    return execute("COMMIT", error);
}

void MysqlConnection::rollback() {
    std::string ignored;
    execute("ROLLBACK", ignored);
}

std::string MysqlConnection::quote(std::string_view value) const {
    if (connection_ == nullptr) {
        return "NULL";
    }
    std::string escaped(value.size() * 2 + 1, '\0');
    const unsigned long size = mysql_real_escape_string(
        connection_, escaped.data(), value.data(), value.size());
    escaped.resize(size);
    return "'" + escaped + "'";
}

std::uint64_t MysqlConnection::affected_rows() const {
    return connection_ == nullptr
        ? 0
        : static_cast<std::uint64_t>(mysql_affected_rows(connection_));
}

bool MysqlConnection::valid() const noexcept {
    return connection_ != nullptr;
}

MysqlDatabase::MysqlDatabase(MysqlConfig config) : config_(std::move(config)) {}

MysqlConnection MysqlDatabase::connect(std::string& error) const {
    MYSQL* connection = mysql_init(nullptr);
    if (connection == nullptr) {
        error = "mysql_init failed";
        return {};
    }
    mysql_options(
        connection,
        MYSQL_OPT_CONNECT_TIMEOUT,
        &config_.connect_timeout_seconds);
    mysql_options(connection, MYSQL_SET_CHARSET_NAME, "utf8mb4");
    if (mysql_real_connect(
            connection,
            config_.host.c_str(),
            config_.user.c_str(),
            config_.password.c_str(),
            config_.database.empty() ? nullptr : config_.database.c_str(),
            config_.port,
            nullptr,
            0) == nullptr) {
        error = mysql_error(connection);
        mysql_close(connection);
        return {};
    }
    return MysqlConnection(connection);
}

bool MysqlDatabase::ping(std::string& error) const {
    auto connection = connect(error);
    return connection.valid();
}

const MysqlConfig& MysqlDatabase::config() const noexcept {
    return config_;
}

}  // namespace chat::infra
