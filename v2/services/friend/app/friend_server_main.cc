#include "friend/dependencies.hpp"
#include "friend/friend_service.hpp"
#include "friend/friend_store.hpp"
#include "friend/mysql_friend_repository.hpp"

#include "chat/infra/etcd.hpp"
#include "chat/infra/mysql.hpp"

#include <brpc/server.h>
#include <gflags/gflags.h>
#include <spdlog/spdlog.h>

#include <exception>
#include <memory>
#include <utility>

DEFINE_int32(port, 10006, "FriendServer RPC listen port");
DEFINE_int32(idle_timeout_seconds, -1, "RPC connection idle timeout");
DEFINE_string(
    storage_file,
    "./v2/data/friend/friends.bin",
    "Friend, application and chat session storage file");
DEFINE_string(user_server, "127.0.0.1:10003", "UserServer address");
DEFINE_int32(user_timeout_ms, 3000, "UserServer RPC timeout");
DEFINE_string(
    message_server,
    "127.0.0.1:10005",
    "MessageStoreServer address");
DEFINE_int32(message_timeout_ms, 3000, "MessageStoreServer RPC timeout");
DEFINE_string(infrastructure_mode, "local", "Storage mode: local or v1");
DEFINE_string(mysql_host, "127.0.0.1", "MySQL host");
DEFINE_int32(mysql_port, 3306, "MySQL port");
DEFINE_string(mysql_user, "chat", "MySQL user");
DEFINE_string(mysql_password, "chat_dev_only", "MySQL password");
DEFINE_string(mysql_database, "ahwei_chat", "MySQL database");
DEFINE_string(etcd_endpoint, "http://127.0.0.1:2379", "etcd URL");
DEFINE_string(advertise_host, "127.0.0.1", "Host registered in etcd");

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);
    try {
        if (FLAGS_infrastructure_mode != "local" &&
            FLAGS_infrastructure_mode != "v1")
            throw std::invalid_argument("infrastructure_mode must be local or v1");
        const bool v1 = FLAGS_infrastructure_mode == "v1";
        std::shared_ptr<chat::friend_service::FriendRepository> repository;
        if (v1) {
            chat::infra::MysqlConfig config;
            config.host = FLAGS_mysql_host;
            config.port = static_cast<std::uint16_t>(FLAGS_mysql_port);
            config.user = FLAGS_mysql_user;
            config.password = FLAGS_mysql_password;
            config.database = FLAGS_mysql_database;
            auto database = std::make_shared<chat::infra::MysqlDatabase>(config);
            std::string error;
            if (!database->ping(error)) throw std::runtime_error("MySQL: " + error);
            repository = std::make_shared<chat::friend_service::MysqlFriendRepository>(database);
        } else {
            repository = std::make_shared<chat::friend_service::FriendStore>(FLAGS_storage_file);
        }
        auto etcd = v1
            ? std::make_shared<chat::infra::EtcdClient>(FLAGS_etcd_endpoint)
            : std::shared_ptr<chat::infra::EtcdClient>{};
        std::shared_ptr<chat::infra::EndpointResolver> user_resolver = v1
            ? std::static_pointer_cast<chat::infra::EndpointResolver>(
                  std::make_shared<chat::infra::EtcdEndpointResolver>(
                      etcd, "/service/user_service"))
            : std::static_pointer_cast<chat::infra::EndpointResolver>(
                  std::make_shared<chat::infra::StaticEndpointResolver>(FLAGS_user_server));
        std::shared_ptr<chat::infra::EndpointResolver> message_resolver = v1
            ? std::static_pointer_cast<chat::infra::EndpointResolver>(
                  std::make_shared<chat::infra::EtcdEndpointResolver>(
                      etcd, "/service/message_service"))
            : std::static_pointer_cast<chat::infra::EndpointResolver>(
                  std::make_shared<chat::infra::StaticEndpointResolver>(FLAGS_message_server));
        auto users = std::make_shared<chat::friend_service::BrpcUserDirectory>(
            std::move(user_resolver), FLAGS_user_timeout_ms);
        auto messages = std::make_shared<chat::friend_service::BrpcRecentMessageClient>(
            std::move(message_resolver), FLAGS_message_timeout_ms);
        chat::friend_service::FriendServiceImpl friend_service(
            std::move(repository),
            std::move(users),
            std::move(messages));

        brpc::Server server;
        if (server.AddService(
                &friend_service,
                brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
            spdlog::error("failed to add FriendService to brpc server");
            return 1;
        }
        brpc::ServerOptions options;
        options.idle_timeout_sec = FLAGS_idle_timeout_seconds;
        if (server.Start(FLAGS_port, &options) != 0) {
            spdlog::error("failed to start FriendServer on port {}", FLAGS_port);
            return 1;
        }
        std::unique_ptr<chat::infra::ServiceRegistry> registry;
        if (v1) {
            auto etcd = std::make_shared<chat::infra::EtcdClient>(FLAGS_etcd_endpoint);
            registry = std::make_unique<chat::infra::ServiceRegistry>(etcd,
                "/service/friend_service/" + FLAGS_advertise_host + "-" +
                    std::to_string(FLAGS_port),
                FLAGS_advertise_host + ":" + std::to_string(FLAGS_port));
            std::string error;
            if (!registry->start(error))
                throw std::runtime_error("etcd registration: " + error);
        }
        spdlog::info(
            "FriendServer started: port={}, mode={}, storage={}, user_server={}, "
            "message_server={}",
            FLAGS_port,
            FLAGS_infrastructure_mode,
            FLAGS_storage_file,
            FLAGS_user_server,
            FLAGS_message_server);
        server.RunUntilAskedToQuit();
        return 0;
    } catch (const std::exception& exception) {
        spdlog::error("FriendServer startup failed: {}", exception.what());
        return 1;
    }
}
