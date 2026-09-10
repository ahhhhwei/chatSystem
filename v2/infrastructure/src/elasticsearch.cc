#include "chat/infra/elasticsearch.hpp"

#include "chat/infra/http.hpp"

#include <nlohmann/json.hpp>

#include <utility>

namespace chat::infra {
namespace {

std::string trim_endpoint(std::string endpoint) {
    while (!endpoint.empty() && endpoint.back() == '/') endpoint.pop_back();
    return endpoint;
}

bool accepted(const HttpResponse& response, std::string& error) {
    if (response.status >= 200 && response.status < 300) return true;
    error = "Elasticsearch returned HTTP " + std::to_string(response.status) +
        ": " + response.body;
    return false;
}

}  // namespace

ElasticsearchClient::ElasticsearchClient(ElasticsearchConfig config)
    : config_(std::move(config)) {
    config_.endpoint = trim_endpoint(std::move(config_.endpoint));
}

bool ElasticsearchClient::ping(std::string& error) const {
    HttpResponse response;
    return HttpClient::request("GET", config_.endpoint, {}, {}, response, error,
               config_.timeout_ms) && accepted(response, error);
}

bool ElasticsearchClient::ensure_index(
    const std::string& index, const std::string& mapping,
    std::string& error) const {
    HttpResponse response;
    const std::string url = config_.endpoint + "/" + index;
    if (!HttpClient::request("HEAD", url, {}, {}, response, error,
            config_.timeout_ms)) return false;
    if (response.status == 200) return true;
    if (response.status != 404) return accepted(response, error);
    if (!HttpClient::request("PUT", url, mapping,
            {"Content-Type: application/json"}, response, error,
            config_.timeout_ms)) return false;
    if (accepted(response, error)) return true;
    if (response.status == 400 &&
        response.body.find("resource_already_exists_exception") !=
            std::string::npos) {
        error.clear();
        return true;
    }
    return false;
}

bool ElasticsearchClient::ensure_indices(std::string& error) const {
    const std::string users = R"({"settings":{"number_of_replicas":0},"mappings":{"properties":{"user_id":{"type":"keyword"},"nickname":{"type":"text","fields":{"raw":{"type":"keyword"}}},"phone":{"type":"keyword"}}}})";
    const std::string messages = R"({"settings":{"number_of_replicas":0},"mappings":{"properties":{"message_id":{"type":"keyword"},"session_id":{"type":"keyword"},"timestamp":{"type":"long"},"content":{"type":"text"}}}})";
    return ensure_index(config_.user_index, users, error) &&
           ensure_index(config_.message_index, messages, error);
}

bool ElasticsearchClient::put_document(
    const std::string& index, const std::string& id,
    const std::string& document, std::string& error) const {
    HttpResponse response;
    if (!HttpClient::request("PUT",
            config_.endpoint + "/" + index + "/_doc/" +
                HttpClient::url_encode(id) + "?refresh=wait_for",
            document, {"Content-Type: application/json"}, response, error,
            config_.timeout_ms)) return false;
    return accepted(response, error);
}

bool ElasticsearchClient::index_user(
    const std::string& user_id, const std::string& nickname,
    const std::string& phone, std::string& error) const {
    nlohmann::json document{{"user_id", user_id}, {"nickname", nickname},
                            {"phone", phone}};
    return put_document(config_.user_index, user_id, document.dump(), error);
}

bool ElasticsearchClient::index_message(
    const std::string& message_id, const std::string& session_id,
    std::int64_t timestamp, const std::string& content,
    std::string& error) const {
    nlohmann::json document{{"message_id", message_id},
        {"session_id", session_id}, {"timestamp", timestamp},
        {"content", content}};
    return put_document(config_.message_index, message_id, document.dump(), error);
}

bool ElasticsearchClient::search_users(
    std::string_view query, const std::vector<std::string>& excluded_ids,
    std::size_t limit, std::vector<std::string>& user_ids,
    std::string& error) const {
    nlohmann::json body;
    body["size"] = limit;
    body["sort"] = nlohmann::json::array({nlohmann::json{{"user_id", "asc"}}});
    auto& boolean = body["query"]["bool"];
    if (query.empty()) {
        boolean["must"] = nlohmann::json::array({{{"match_all", nlohmann::json::object()}}});
    } else {
        boolean["must"] = nlohmann::json::array({{{"multi_match", {
            {"query", std::string(query)},
            {"fields", {"user_id", "nickname", "phone"}}}}}});
    }
    if (!excluded_ids.empty()) {
        boolean["must_not"] = nlohmann::json::array({{{"terms", {{"user_id", excluded_ids}}}}});
    }
    HttpResponse response;
    if (!HttpClient::request("POST", config_.endpoint + "/" +
            config_.user_index + "/_search", body.dump(),
            {"Content-Type: application/json"}, response, error,
            config_.timeout_ms) || !accepted(response, error)) return false;
    try {
        user_ids.clear();
        const auto document = nlohmann::json::parse(response.body);
        for (const auto& hit : document.at("hits").at("hits"))
            user_ids.push_back(hit.at("_id").get<std::string>());
        return true;
    } catch (const std::exception& exception) {
        error = std::string("cannot parse Elasticsearch result: ") + exception.what();
        return false;
    }
}

bool ElasticsearchClient::search_messages(
    std::string_view session_id, std::string_view query,
    std::vector<std::string>& message_ids, std::string& error) const {
    nlohmann::json filters = nlohmann::json::array({
        {{"term", {{"session_id", std::string(session_id)}}}}});
    nlohmann::json must = query.empty()
        ? nlohmann::json::array({{{"match_all", nlohmann::json::object()}}})
        : nlohmann::json::array({{{"match", {{"content", std::string(query)}}}}});
    nlohmann::json body{{"size", 1000},
        {"query", {{"bool", {{"filter", filters}, {"must", must}}}}},
        {"sort", nlohmann::json::array({{{"timestamp", "asc"}},
                                        {{"message_id", "asc"}}})}};
    HttpResponse response;
    if (!HttpClient::request("POST", config_.endpoint + "/" +
            config_.message_index + "/_search", body.dump(),
            {"Content-Type: application/json"}, response, error,
            config_.timeout_ms) || !accepted(response, error)) return false;
    try {
        message_ids.clear();
        const auto document = nlohmann::json::parse(response.body);
        for (const auto& hit : document.at("hits").at("hits"))
            message_ids.push_back(hit.at("_id").get<std::string>());
        return true;
    } catch (const std::exception& exception) {
        error = std::string("cannot parse Elasticsearch result: ") + exception.what();
        return false;
    }
}

const ElasticsearchConfig& ElasticsearchClient::config() const noexcept {
    return config_;
}

}  // namespace chat::infra
