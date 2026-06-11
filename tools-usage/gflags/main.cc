#include <gflags/gflags.h>
#include <iostream> 

DEFINE_string(ip, "127.0.0.1", "这是服务器的默认监听地址");
DEFINE_int32(port, 8080, "这是服务器的默认监听端口");
DEFINE_bool(debug_enable, true, "是否启用调试模式");

int main(int argc, char *argv[])
{
    google::ParseCommandLineFlags(&argc, &argv, true);
    std::cout << "ip: " << FLAGS_ip << std::endl;
    std::cout << "port: " << FLAGS_port << std::endl;
    std::cout << "debug_enable: " << FLAGS_debug_enable << std::endl;
    return 0;
}