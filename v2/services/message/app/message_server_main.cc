#include "message/file_client.hpp"
#include "message/indexed_message_repository.hpp"
#include "message/message_service.hpp"
#include "message/message_store.hpp"
#include "message/mysql_message_repository.hpp"

#include "chat/infra/elasticsearch.hpp"
#include "chat/infra/etcd.hpp"
#include "chat/infra/mysql.hpp"
#include "chat/infra/rabbitmq.hpp"

#include <brpc/server.h>
#include <gflags/gflags.h>
#include <spdlog/spdlog.h>

#include <exception>
#include <memory>

DEFINE_int32(port, 10005, "MessageStoreServer RPC listen port");
DEFINE_int32(idle_timeout_seconds, -1, "RPC connection idle timeout");
DEFINE_string(
    storage_file,
    "./v2/data/message/messages.bin",
    "Message metadata storage file");
DEFINE_string(
    file_server,
    "127.0.0.1:10002",
    "FileServer address");
DEFINE_int32(file_timeout_ms, 3000, "FileServer RPC timeout");
DEFINE_string(infrastructure_mode, "local", "Storage mode: local or v1");
DEFINE_string(mysql_host, "127.0.0.1", "MySQL host");
DEFINE_int32(mysql_port, 3306, "MySQL port");
DEFINE_string(mysql_user, "chat", "MySQL user");
DEFINE_string(mysql_password, "chat_dev_only", "MySQL password");
DEFINE_string(mysql_database, "ahwei_chat", "MySQL database");
DEFINE_string(elasticsearch_endpoint, "http://127.0.0.1:9200", "Elasticsearch URL");
DEFINE_string(rabbitmq_host, "127.0.0.1", "RabbitMQ host");
DEFINE_int32(rabbitmq_port, 5672, "RabbitMQ port");
DEFINE_string(rabbitmq_user, "guest", "RabbitMQ user");
DEFINE_string(rabbitmq_password, "guest", "RabbitMQ password");
DEFINE_string(etcd_endpoint, "http://127.0.0.1:2379", "etcd URL");
DEFINE_string(advertise_host, "127.0.0.1", "Host registered in etcd");

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    try {
        if (FLAGS_infrastructure_mode != "local" &&
            FLAGS_infrastructure_mode != "v1")
            throw std::invalid_argument("infrastructure_mode must be local or v1");
        const bool v1 = FLAGS_infrastructure_mode == "v1";
        std::shared_ptr<chat::message::MessageRepository> store;
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
            auto mysql = std::make_shared<chat::message::MysqlMessageRepository>(database);
            chat::infra::ElasticsearchConfig search_config;
            search_config.endpoint = FLAGS_elasticsearch_endpoint;
            auto search = std::make_shared<chat::infra::ElasticsearchClient>(search_config);
            if (!search->ensure_indices(error))
                throw std::runtime_error("Elasticsearch: " + error);
            store = std::make_shared<chat::message::IndexedMessageRepository>(
                std::move(mysql), std::move(search));
        } else {
            store = std::make_shared<chat::message::MessageStore>(FLAGS_storage_file);
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
        auto file_client = std::make_shared<chat::message::BrpcFileClient>(
            std::move(file_resolver), FLAGS_file_timeout_ms);
        chat::message::MessageServiceImpl message_service(
            std::move(store),
            std::move(file_client));

        std::unique_ptr<chat::infra::RabbitMqConsumer> consumer;
        if (v1) {
            chat::infra::RabbitMqConfig config;
            config.host = FLAGS_rabbitmq_host;
            config.port = static_cast<std::uint16_t>(FLAGS_rabbitmq_port);
            config.username = FLAGS_rabbitmq_user;
            config.password = FLAGS_rabbitmq_password;
            consumer = std::make_unique<chat::infra::RabbitMqConsumer>(
                config, [&message_service](const void* data, std::size_t size,
                                           std::string& error) {
                    return message_service.on_message(data, size, error);
                });
            std::string error;
            if (!consumer->start(error))
                throw std::runtime_error("RabbitMQ consumer: " + error);
        }

        brpc::Server server;
        if (server.AddService(
                &message_service,
                brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
            spdlog::error("failed to add MsgStorageService to brpc server");
            return 1;
        }

        brpc::ServerOptions options;
        options.idle_timeout_sec = FLAGS_idle_timeout_seconds;
        if (server.Start(FLAGS_port, &options) != 0) {
            spdlog::error(
                "failed to start MessageStoreServer on port {}",
                FLAGS_port);
            return 1;
        }
        std::unique_ptr<chat::infra::ServiceRegistry> registry;
        if (v1) {
            auto etcd = std::make_shared<chat::infra::EtcdClient>(FLAGS_etcd_endpoint);
            registry = std::make_unique<chat::infra::ServiceRegistry>(etcd,
                "/service/message_service/" + FLAGS_advertise_host + "-" +
                    std::to_string(FLAGS_port),
                FLAGS_advertise_host + ":" + std::to_string(FLAGS_port));
            std::string error;
            if (!registry->start(error))
                throw std::runtime_error("etcd registration: " + error);
        }

        spdlog::info(
            "MessageStoreServer started: port={}, mode={}, storage={}, file_server={}",
            FLAGS_port,
            FLAGS_infrastructure_mode,
            FLAGS_storage_file,
            FLAGS_file_server);
        server.RunUntilAskedToQuit();
        return 0;
    } catch (const std::exception& exception) {
        spdlog::error(
            "MessageStoreServer startup failed: {}",
            exception.what());
        return 1;
    }
}
