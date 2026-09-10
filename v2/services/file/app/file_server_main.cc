#include "file/file_service.hpp"
#include "chat/infra/etcd.hpp"

#include <brpc/server.h>
#include <gflags/gflags.h>
#include <spdlog/spdlog.h>

#include <exception>
#include <memory>

DEFINE_int32(port, 10002, "FileServer RPC listen port");
DEFINE_int32(idle_timeout_seconds, -1, "RPC connection idle timeout");
DEFINE_string(storage_path, "./v2/data/file", "Directory used to store files");
DEFINE_string(infrastructure_mode, "local", "Discovery mode: local or v1");
DEFINE_string(etcd_endpoint, "http://127.0.0.1:2379", "etcd URL");
DEFINE_string(advertise_host, "127.0.0.1", "Host registered in etcd");

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    try {
        chat::file::FileServiceImpl file_service(FLAGS_storage_path);
        // 创建 brpc server
        brpc::Server server;
        // 注册服务
        if (server.AddService(
                &file_service,
                brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
            spdlog::error("failed to add FileService to brpc server");
            return 1;
        }

        // 服务器配置
        brpc::ServerOptions options;
        options.idle_timeout_sec = FLAGS_idle_timeout_seconds; // TCP 连接空闲多久以后关闭
        // 启动服务器，监听端口
        if (server.Start(FLAGS_port, &options) != 0) {
            spdlog::error("failed to start FileServer on port {}", FLAGS_port);
            return 1;
        }

        std::unique_ptr<chat::infra::ServiceRegistry> registry;
        if (FLAGS_infrastructure_mode == "v1") {
            auto etcd = std::make_shared<chat::infra::EtcdClient>(FLAGS_etcd_endpoint);
            registry = std::make_unique<chat::infra::ServiceRegistry>(etcd,
                "/service/file_service/" + FLAGS_advertise_host + "-" +
                    std::to_string(FLAGS_port),
                FLAGS_advertise_host + ":" + std::to_string(FLAGS_port));
            std::string error;
            if (!registry->start(error))
                throw std::runtime_error("etcd registration: " + error);
        } else if (FLAGS_infrastructure_mode != "local") {
            throw std::invalid_argument("infrastructure_mode must be local or v1");
        }

        spdlog::info(
            "FileServer started: port={}, mode={}, storage={}",
            FLAGS_port,
            FLAGS_infrastructure_mode,
            FLAGS_storage_path);
        // 一直运行，直到收到退出信号 （没有这句就直接return 0 退出了）
        server.RunUntilAskedToQuit();
        return 0;
    } catch (const std::exception& exception) {
        spdlog::error("FileServer startup failed: {}", exception.what());
        return 1;
    }
}
