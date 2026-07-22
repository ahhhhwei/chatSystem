#include <cpr/cpr.h>
#include <iostream>

int main()
{
    const std::string username = "elastic";
    const std::string password = "***********";

    cpr::Response rsp = cpr::Post(
        cpr::Url{"https://127.0.0.1:9200/user/_search"},
        cpr::Authentication{username, password},
        cpr::Header{
            {"Content-Type", "application/json"}},
        cpr::Body{
            R"({
                "query": {
                    "match_all": {}
                }
            })"},
        // 临时调试：忽略自签名证书校验
        cpr::VerifySsl{false});

    if (rsp.error.code != cpr::ErrorCode::OK)
    {
        std::cerr << "网络请求失败：" << rsp.error.message << '\n';
        return -1;
    }

    std::cout << "状态码：" << rsp.status_code << '\n';
    std::cout << "响应正文：" << rsp.text << '\n';

    if (rsp.status_code < 200 || rsp.status_code >= 300)
    {
        std::cerr << "Elasticsearch 请求未成功\n";
        return -1;
    }

    return 0;
}