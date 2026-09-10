#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct MYSQL;
struct MYSQL_RES;

namespace chat::infra {

struct MysqlConfig {
    std::string host{"127.0.0.1"};
    std::uint16_t port = 3306;
    std::string user{"chat"};
    std::string password{"chat_dev_only"};
    std::string database{"ahwei_chat"};
    unsigned int connect_timeout_seconds = 3;
};

class MysqlResult final {
public:
    using Cell = std::optional<std::string>;
    using Row = std::vector<Cell>;

    MysqlResult() = default;
    ~MysqlResult();
    MysqlResult(MysqlResult&& other) noexcept;
    MysqlResult& operator=(MysqlResult&& other) noexcept;
    MysqlResult(const MysqlResult&) = delete;
    MysqlResult& operator=(const MysqlResult&) = delete;

    bool next(Row& row);
    std::size_t size() const;

private:
    friend class MysqlConnection;
    explicit MysqlResult(MYSQL_RES* result);
    MYSQL_RES* result_ = nullptr;
};

class MysqlConnection final {
public:
    MysqlConnection() = default;
    ~MysqlConnection();
    MysqlConnection(MysqlConnection&& other) noexcept;
    MysqlConnection& operator=(MysqlConnection&& other) noexcept;
    MysqlConnection(const MysqlConnection&) = delete;
    MysqlConnection& operator=(const MysqlConnection&) = delete;

    bool execute(const std::string& sql, std::string& error);
    bool query(
        const std::string& sql,
        MysqlResult& result,
        std::string& error);
    bool begin(std::string& error);
    bool commit(std::string& error);
    void rollback();
    std::string quote(std::string_view value) const;
    std::uint64_t affected_rows() const;
    bool valid() const noexcept;

private:
    friend class MysqlDatabase;
    explicit MysqlConnection(MYSQL* connection);
    MYSQL* connection_ = nullptr;
};

class MysqlDatabase final {
public:
    explicit MysqlDatabase(MysqlConfig config);

    MysqlConnection connect(std::string& error) const;
    bool ping(std::string& error) const;
    const MysqlConfig& config() const noexcept;

private:
    MysqlConfig config_;
};

}  // namespace chat::infra
