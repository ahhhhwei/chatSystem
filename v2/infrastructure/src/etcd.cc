#include "chat/infra/etcd.hpp"

#include "chat/infra/http.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <utility>

namespace chat::infra {
namespace {

void collect_values(
    const nlohmann::json& node,
    std::vector<std::string>& values) {
    if (node.contains("value") && node["value"].is_string()) {
        values.push_back(node["value"].get<std::string>());
    }
    if (node.contains("nodes") && node["nodes"].is_array()) {
        for (const auto& child : node["nodes"]) {
            collect_values(child, values);
        }
    }
}

}  // namespace

StaticEndpointResolver::StaticEndpointResolver(std::string endpoint)
    : endpoint_(std::move(endpoint)) {}

bool StaticEndpointResolver::resolve(
    std::string& endpoint,
    std::string& error) {
    if (endpoint_.empty()) {
        error = "static endpoint is empty";
        return false;
    }
    endpoint = endpoint_;
    return true;
}

EtcdClient::EtcdClient(std::string endpoint, long timeout_ms)
    : endpoint_(std::move(endpoint)), timeout_ms_(timeout_ms) {
    while (!endpoint_.empty() && endpoint_.back() == '/') {
        endpoint_.pop_back();
    }
}

std::string EtcdClient::key_url(const std::string& key) const {
    return endpoint_ + "/v2/keys" + (key.empty() || key.front() == '/' ? key : "/" + key);
}

bool EtcdClient::put_with_ttl(
    const std::string& key,
    const std::string& value,
    std::chrono::seconds ttl,
    std::string& error) const {
    HttpResponse response;
    const std::string body = "value=" + HttpClient::url_encode(value) +
        "&ttl=" + std::to_string(std::max<std::int64_t>(1, ttl.count()));
    if (!HttpClient::request(
            "PUT",
            key_url(key),
            body,
            {"Content-Type: application/x-www-form-urlencoded"},
            response,
            error,
            timeout_ms_)) {
        return false;
    }
    if (response.status != 200 && response.status != 201) {
        error = "etcd PUT returned HTTP " + std::to_string(response.status) +
            ": " + response.body;
        return false;
    }
    return true;
}

bool EtcdClient::erase(const std::string& key, std::string& error) const {
    HttpResponse response;
    if (!HttpClient::request(
            "DELETE", key_url(key), {}, {}, response, error, timeout_ms_)) {
        return false;
    }
    if (response.status != 200 && response.status != 404) {
        error = "etcd DELETE returned HTTP " + std::to_string(response.status) +
            ": " + response.body;
        return false;
    }
    return true;
}

bool EtcdClient::values(
    const std::string& prefix,
    std::vector<std::string>& values_result,
    std::string& error) const {
    HttpResponse response;
    if (!HttpClient::request(
            "GET",
            key_url(prefix) + "?recursive=true&sorted=true",
            {},
            {},
            response,
            error,
            timeout_ms_)) {
        return false;
    }
    values_result.clear();
    if (response.status == 404) {
        error = "no service instance registered under " + prefix;
        return false;
    }
    if (response.status != 200) {
        error = "etcd GET returned HTTP " + std::to_string(response.status) +
            ": " + response.body;
        return false;
    }
    try {
        const auto document = nlohmann::json::parse(response.body);
        if (document.contains("node")) {
            collect_values(document["node"], values_result);
        }
    } catch (const std::exception& exception) {
        error = std::string("cannot parse etcd response: ") + exception.what();
        return false;
    }
    values_result.erase(
        std::remove_if(
            values_result.begin(),
            values_result.end(),
            [](const std::string& value) { return value.empty(); }),
        values_result.end());
    if (values_result.empty()) {
        error = "no live service instance registered under " + prefix;
        return false;
    }
    return true;
}

bool EtcdClient::health(std::string& error) const {
    HttpResponse response;
    if (!HttpClient::request(
            "GET", endpoint_ + "/health", {}, {}, response, error, timeout_ms_)) {
        return false;
    }
    if (response.status != 200 || response.body.find("true") == std::string::npos) {
        error = "etcd health check failed: " + response.body;
        return false;
    }
    return true;
}

EtcdEndpointResolver::EtcdEndpointResolver(
    std::shared_ptr<EtcdClient> client,
    std::string service_prefix)
    : client_(std::move(client)), service_prefix_(std::move(service_prefix)) {}

bool EtcdEndpointResolver::resolve(
    std::string& endpoint,
    std::string& error) {
    std::vector<std::string> endpoints;
    if (!client_->values(service_prefix_, endpoints, error)) {
        return false;
    }
    std::lock_guard lock(mutex_);
    endpoint = endpoints[next_++ % endpoints.size()];
    return true;
}

ServiceRegistry::ServiceRegistry(
    std::shared_ptr<EtcdClient> client,
    std::string key,
    std::string endpoint,
    std::chrono::seconds ttl)
    : client_(std::move(client)),
      key_(std::move(key)),
      endpoint_(std::move(endpoint)),
      ttl_(ttl) {}

ServiceRegistry::~ServiceRegistry() {
    stop();
}

bool ServiceRegistry::start(std::string& error) {
    if (running_) {
        return true;
    }
    if (!client_->put_with_ttl(key_, endpoint_, ttl_, error)) {
        return false;
    }
    running_ = true;
    keepalive_thread_ = std::thread(&ServiceRegistry::keepalive_loop, this);
    return true;
}

void ServiceRegistry::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (keepalive_thread_.joinable()) {
        keepalive_thread_.join();
    }
    std::string ignored;
    client_->erase(key_, ignored);
}

void ServiceRegistry::keepalive_loop() {
    const auto interval = std::max(std::chrono::seconds(1), ttl_ / 3);
    while (running_) {
        const auto deadline = std::chrono::steady_clock::now() + interval;
        while (running_ && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!running_) {
            break;
        }
        std::string ignored;
        client_->put_with_ttl(key_, endpoint_, ttl_, ignored);
    }
}

}  // namespace chat::infra

