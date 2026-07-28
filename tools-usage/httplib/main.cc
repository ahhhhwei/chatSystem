#include "../common/httplib.h"

int main()
{
    // 1. 实例化服务器对象
    httplib::Server server;
    // 2. 注册回调函数 void(const httplib::Request&, httplib::Response&)
    server.Get("/hello", [](const httplib::Request &req, httplib::Response &res)
               {
        std::cout << "method: " << req.method << std::endl;
        std::cout << "path: " << req.path << std::endl;
        for (auto it :req.headers) {
            std::cout << it.first << " : " << it.second << std::endl;
        }
        std::string body = "<html><body><h1>ahwei</h1></body></html>";
        res.set_content(body, "text/html");
        res.status = 200; });
    // 3. 启动服务器
    server.listen("0.0.0.0", 9090);
    return 0;
}