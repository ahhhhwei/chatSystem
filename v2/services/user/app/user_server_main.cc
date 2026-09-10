#include "user/avatar_file_client.hpp"
#include "user/indexed_user_repository.hpp"
#include "user/mysql_user_repository.hpp"
#include "user/redis_security.hpp"
#include "user/security.hpp"
#include "user/user_service.hpp"
#include "user/user_store.hpp"

#include "chat/infra/elasticsearch.hpp"
#include "chat/infra/etcd.hpp"
#include "chat/infra/mysql.hpp"
#include "chat/infra/redis.hpp"

#include <brpc/server.h>
#include <gflags/gflags.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <exception>
#include <memory>
#include <utility>

DEFINE_int32(port, 10003, "UserServer RPC listen port");
DEFINE_int32(idle_timeout_seconds, -1, "RPC connection idle timeout");
DEFINE_string(
    storage_file,
    "./v2/data/user/users.bin",
    "User snapshot storage file");
DEFINE_string(file_server, "127.0.0.1:10002", "FileServer address");
DEFINE_int32(file_timeout_ms, 3000, "FileServer RPC timeout");
DEFINE_int32(verification_ttl_seconds, 300, "Phone verification code TTL");
DEFINE_int32(session_ttl_seconds, 86400, "Login session TTL");
DEFINE_string(infrastructure_mode, "local", "Storage mode: local or v1");
DEFINE_string(mysql_host, "127.0.0.1", "MySQL host");
DEFINE_int32(mysql_port, 3306, "MySQL port");
DEFINE_string(mysql_user, "chat", "MySQL user");
DEFINE_string(mysql_password, "chat_dev_only", "MySQL password");
DEFINE_string(mysql_database, "ahwei_chat", "MySQL database");
DEFINE_string(redis_host, "127.0.0.1", "Redis host");
DEFINE_int32(redis_port, 6379, "Redis port");
DEFINE_string(redis_password, "", "Redis password");
DEFINE_string(elasticsearch_endpoint, "http://127.0.0.1:9200", "Elasticsearch URL");
DEFINE_string(etcd_endpoint, "http://127.0.0.1:2379", "etcd URL");
DEFINE_string(advertise_host, "127.0.0.1", "Host registered in etcd");

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    try {
        if (FLAGS_infrastructure_mode != "local" &&
            FLAGS_infrastructure_mode != "v1") {
            throw std::invalid_argument("infrastructure_mode must be local or v1");
        }
        const bool v1 = FLAGS_infrastructure_mode == "v1";
        std::shared_ptr<chat::user::UserRepository> repository;
        std::shared_ptr<chat::infra::RedisClient> redis;
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
            auto mysql = std::make_shared<chat::user::MysqlUserRepository>(database);
            chat::infra::ElasticsearchConfig search_config;
            search_config.endpoint = FLAGS_elasticsearch_endpoint;
            auto search = std::make_shared<chat::infra::ElasticsearchClient>(search_config);
            if (!search->ensure_indices(error))
                throw std::runtime_error("Elasticsearch: " + error);
            repository = std::make_shared<chat::user::IndexedUserRepository>(
                std::move(mysql), std::move(search));
            chat::infra::RedisConfig redis_config;
            redis_config.host = FLAGS_redis_host;
            redis_config.port = static_cast<std::uint16_t>(FLAGS_redis_port);
            redis_config.password = FLAGS_redis_password;
            redis = std::make_shared<chat::infra::RedisClient>(redis_config);
            if (!redis->ping(error)) throw std::runtime_error("Redis: " + error);
        } else {
            repository = std::make_shared<chat::user::UserStore>(FLAGS_storage_file);
        }
        std::shared_ptr<chat::infra::EndpointResolver> file_resolver;
        if (v1) {
            file_resolver = std::make_shared<chat::infra::EtcdEndpointResolver>(
                std::make_shared<chat::infra::EtcdClient>(FLAGS_etcd_endpoint),
                "/service/file_service");
        } else {
            file_resolver = std::make_shared<chat::infra::StaticEndpointResolver>(
                FLAGS_file_server);
        }
        auto files = std::make_shared<chat::user::BrpcAvatarFileClient>(
            std::move(file_resolver), FLAGS_file_timeout_ms);
        auto sender =
            std::make_shared<chat::user::LoggingVerificationCodeSender>();
        std::shared_ptr<chat::user::VerificationCodeManager> codes;
        std::shared_ptr<chat::user::SessionManager> sessions;
        if (v1) {
            codes = std::make_shared<chat::user::RedisVerificationCodeManager>(
                sender, redis, std::chrono::seconds(FLAGS_verification_ttl_seconds));
            sessions = std::make_shared<chat::user::RedisSessionManager>(
                redis, std::chrono::seconds(FLAGS_session_ttl_seconds));
        } else {
            codes = std::make_shared<chat::user::VerificationCodeManager>(
                sender, std::chrono::seconds(FLAGS_verification_ttl_seconds));
            sessions = std::make_shared<chat::user::SessionManager>(
                std::chrono::seconds(FLAGS_session_ttl_seconds));
        }
        chat::user::UserServiceImpl user_service(
            std::move(repository),
            std::move(files),
            std::move(codes),
            std::move(sessions));

        brpc::Server server;
        if (server.AddService(
                &user_service,
                brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
            spdlog::error("failed to add UserService to brpc server");
            return 1;
        }

        brpc::ServerOptions options;
        options.idle_timeout_sec = FLAGS_idle_timeout_seconds;
        if (server.Start(FLAGS_port, &options) != 0) {
            spdlog::error("failed to start UserServer on port {}", FLAGS_port);
            return 1;
        }

        std::unique_ptr<chat::infra::ServiceRegistry> registry;
        if (v1) {
            auto etcd = std::make_shared<chat::infra::EtcdClient>(FLAGS_etcd_endpoint);
            registry = std::make_unique<chat::infra::ServiceRegistry>(
                etcd,
                "/service/user_service/" + FLAGS_advertise_host + "-" +
                    std::to_string(FLAGS_port),
                FLAGS_advertise_host + ":" + std::to_string(FLAGS_port));
            std::string error;
            if (!registry->start(error))
                throw std::runtime_error("etcd registration: " + error);
        }

        spdlog::info(
            "UserServer started: port={}, mode={}, storage={}, file_server={}",
            FLAGS_port,
            FLAGS_infrastructure_mode,
            FLAGS_storage_file,
            FLAGS_file_server);
        spdlog::warn(
            "development code sender is enabled; verification codes are printed "
            "to this log");
        server.RunUntilAskedToQuit();
        return 0;
    } catch (const std::exception& exception) {
        spdlog::error("UserServer startup failed: {}", exception.what());
        return 1;
    }
}
