#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace chat::infra {

struct ElasticsearchConfig {
    std::string endpoint{"http://127.0.0.1:9200"};
    std::string user_index{"ahwei-users-v2"};
    std::string message_index{"ahwei-messages-v2"};
    long timeout_ms = 10000;
};

class ElasticsearchClient final {
public:
    explicit ElasticsearchClient(ElasticsearchConfig config);

    bool ping(std::string& error) const;
    bool ensure_indices(std::string& error) const;
    bool index_user(const std::string& user_id, const std::string& nickname,
        const std::string& phone, std::string& error) const;
    bool index_message(const std::string& message_id,
        const std::string& session_id, std::int64_t timestamp,
        const std::string& content, std::string& error) const;
    bool search_users(std::string_view query,
        const std::vector<std::string>& excluded_ids, std::size_t limit,
        std::vector<std::string>& user_ids, std::string& error) const;
    bool search_messages(std::string_view session_id, std::string_view query,
        std::vector<std::string>& message_ids, std::string& error) const;

    const ElasticsearchConfig& config() const noexcept;

private:
    bool ensure_index(const std::string& index, const std::string& mapping,
        std::string& error) const;
    bool put_document(const std::string& index, const std::string& id,
        const std::string& document, std::string& error) const;

    ElasticsearchConfig config_;
};

}  // namespace chat::infra
