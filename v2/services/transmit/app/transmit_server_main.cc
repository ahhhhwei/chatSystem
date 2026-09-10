#include "transmit/dependencies.hpp"
#include "transmit/transmit_service.hpp"

#include "chat/infra/etcd.hpp"

#include <brpc/server.h>
#include <gflags/gflags.h>
#include <spdlog/spdlog.h>

#include <exception>
#include <memory>
#include <utility>

DEFINE_int32(port, 10004, "MessageTransmitServer RPC listen port");
DEFINE_int32(idle_timeout_seconds, -1, "RPC connection idle timeout");
DEFINE_string(user_server, "127.0.0.1:10003", "UserServer address");
DEFINE_int32(user_timeout_ms, 3000, "UserServer RPC timeout");
DEFINE_string(friend_server, "127.0.0.1:10006", "FriendServer address");
DEFINE_int32(friend_timeout_ms, 3000, "FriendServer RPC timeout");
DEFINE_string(
    message_server,
    "127.0.0.1:10005",
    "MessageStoreServer address");
DEFINE_int32(message_timeout_ms, 5000, "MessageStoreServer RPC timeout");
DEFINE_string(infrastructure_mode, "local", "Transport mode: local or v1");
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
        auto etcd = v1
            ? std::make_shared<chat::infra::EtcdClient>(FLAGS_etcd_endpoint)
            : std::shared_ptr<chat::infra::EtcdClient>{};
        std::shared_ptr<chat::infra::EndpointResolver> user_resolver = v1
            ? std::static_pointer_cast<chat::infra::EndpointResolver>(
                  std::make_shared<chat::infra::EtcdEndpointResolver>(
                      etcd, "/service/user_service"))
            : std::static_pointer_cast<chat::infra::EndpointResolver>(
                  std::make_shared<chat::infra::StaticEndpointResolver>(FLAGS_user_server));
        std::shared_ptr<chat::infra::EndpointResolver> friend_resolver = v1
            ? std::static_pointer_cast<chat::infra::EndpointResolver>(
                  std::make_shared<chat::infra::EtcdEndpointResolver>(
                      etcd, "/service/friend_service"))
            : std::static_pointer_cast<chat::infra::EndpointResolver>(
                  std::make_shared<chat::infra::StaticEndpointResolver>(FLAGS_friend_server));
        auto user_client = std::make_shared<chat::transmit::BrpcUserClient>(
            std::move(user_resolver), FLAGS_user_timeout_ms);
        auto member_repository = std::make_shared<
            chat::transmit::BrpcFriendSessionMemberRepository>(
                std::move(friend_resolver), FLAGS_friend_timeout_ms);
        std::shared_ptr<chat::transmit::MessagePublisher> publisher;
        if (v1) {
            chat::infra::RabbitMqConfig config;
            config.host = FLAGS_rabbitmq_host;
            config.port = static_cast<std::uint16_t>(FLAGS_rabbitmq_port);
            config.username = FLAGS_rabbitmq_user;
            config.password = FLAGS_rabbitmq_password;
            publisher = std::make_shared<chat::transmit::RabbitMqMessagePublisher>(config);
        } else {
            publisher = std::make_shared<chat::transmit::BrpcMessagePublisher>(
                FLAGS_message_server, FLAGS_message_timeout_ms);
        }
        chat::transmit::TransmitServiceImpl transmit_service(
            std::move(user_client),
            std::move(member_repository),
            std::move(publisher));

        brpc::Server server;
        if (server.AddService(
                &transmit_service,
                brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
            spdlog::error("failed to add MsgTransmitService to brpc server");
            return 1;
        }

        brpc::ServerOptions options;
        options.idle_timeout_sec = FLAGS_idle_timeout_seconds;
        if (server.Start(FLAGS_port, &options) != 0) {
            spdlog::error(
                "failed to start MessageTransmitServer on port {}",
                FLAGS_port);
            return 1;
        }
        std::unique_ptr<chat::infra::ServiceRegistry> registry;
        if (v1) {
            auto etcd = std::make_shared<chat::infra::EtcdClient>(FLAGS_etcd_endpoint);
            registry = std::make_unique<chat::infra::ServiceRegistry>(etcd,
                "/service/transmite_service/" + FLAGS_advertise_host + "-" +
                    std::to_string(FLAGS_port),
                FLAGS_advertise_host + ":" + std::to_string(FLAGS_port));
            std::string error;
            if (!registry->start(error))
                throw std::runtime_error("etcd registration: " + error);
        }

        spdlog::info(
            "MessageTransmitServer started: port={}, mode={}, user_server={}, "
            "friend_server={}, message_server={}",
            FLAGS_port,
            FLAGS_infrastructure_mode,
            FLAGS_user_server,
            FLAGS_friend_server,
            FLAGS_message_server);
        server.RunUntilAskedToQuit();
        return 0;
    } catch (const std::exception& exception) {
        spdlog::error(
            "MessageTransmitServer startup failed: {}",
            exception.what());
        return 1;
    }
}
