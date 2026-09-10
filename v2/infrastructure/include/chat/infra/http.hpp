#pragma once

#include <string>
#include <vector>

namespace chat::infra {

struct HttpResponse {
    long status = 0;
    std::string body;
};

class HttpClient final {
public:
    static bool request(
        const std::string& method,
        const std::string& url,
        const std::string& body,
        const std::vector<std::string>& headers,
        HttpResponse& response,
        std::string& error,
        long timeout_ms = 3000,
        const std::string& basic_auth = {});

    static std::string url_encode(const std::string& value);
};

}  // namespace chat::infra

