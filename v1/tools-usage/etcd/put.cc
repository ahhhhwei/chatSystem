#include <etcd/Client.hpp>
#include <etcd/KeepAlive.hpp>
#include <etcd/Response.hpp>
#include <thread>
int main(int argc, char *argv[])
{
    std::string etcd_host = "127.0.0.1:2379";
    // 实例客户端对象
    etcd::Client client(etcd_host);
    // 获取租约保活对象（伴随这创建一个指定有效时长的租约）
    auto keep_alive = client.leasekeepalive(3).get();
    // 获取租约 id
    auto lease_id = keep_alive->Lease();
    // 向 etcd 新增数据
    auto resp1 = client.put("/service/user", "127.0.0.1:8080", lease_id).get();
    if (resp1.is_ok() == false)
    {
        std::cout << "新增数据失败：" << resp1.error_message() << std::endl;
        return -1;
    }
    auto resp2 = client.put("/service/friend", "127.0.0.1:9090", lease_id).get();
    if (resp2.is_ok() == false)
    {
        std::cout << "新增数据失败：" << resp2.error_message() << std::endl;
        return -1;
    }
    std::this_thread::sleep_for(std::chrono::seconds(10));
    return 0;
}