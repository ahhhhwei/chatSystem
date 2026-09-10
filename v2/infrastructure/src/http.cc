#include "chat/infra/http.hpp"

#include <curl/curl.h>

#include <mutex>

namespace chat::infra {
namespace {

std::once_flag curl_init_flag;

void ensure_curl_initialized() {
    std::call_once(curl_init_flag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

std::size_t append_body(
    char* data,
    std::size_t size,
    std::size_t count,
    void* destination) {
    const std::size_t bytes = size * count;
    static_cast<std::string*>(destination)->append(data, bytes);
    return bytes;
}

}  // namespace

bool HttpClient::request(
    const std::string& method,
    const std::string& url,
    const std::string& body,
    const std::vector<std::string>& headers,
    HttpResponse& response,
    std::string& error,
    long timeout_ms,
    const std::string& basic_auth) {
    ensure_curl_initialized();
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        error = "curl_easy_init failed";
        return false;
    }
    curl_slist* header_list = nullptr;
    for (const auto& header : headers) {
        header_list = curl_slist_append(header_list, header.c_str());
    }
    response = {};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    if (method == "HEAD") {
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    }
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    if (header_list != nullptr) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    }
    if (!body.empty() || method == "POST" || method == "PUT") {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(
            curl,
            CURLOPT_POSTFIELDSIZE_LARGE,
            static_cast<curl_off_t>(body.size()));
    }
    if (!basic_auth.empty()) {
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
        curl_easy_setopt(curl, CURLOPT_USERPWD, basic_auth.c_str());
    }

    const CURLcode status = curl_easy_perform(curl);
    if (status == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    } else {
        error = curl_easy_strerror(status);
    }
    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);
    return status == CURLE_OK;
}

std::string HttpClient::url_encode(const std::string& value) {
    ensure_curl_initialized();
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        return {};
    }
    char* encoded = curl_easy_escape(
        curl, value.data(), static_cast<int>(value.size()));
    std::string result = encoded == nullptr ? std::string{} : encoded;
    curl_free(encoded);
    curl_easy_cleanup(curl);
    return result;
}

}  // namespace chat::infra
