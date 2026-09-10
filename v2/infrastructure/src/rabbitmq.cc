#include "chat/infra/rabbitmq.hpp"

#include <amqp.h>
#include <amqp_framing.h>
#include <amqp_tcp_socket.h>

#include <chrono>
#include <utility>

namespace chat::infra {
namespace {

constexpr int kChannel = 1;

std::string rpc_error(
    const std::string& action,
    const amqp_rpc_reply_t& reply) {
    switch (reply.reply_type) {
        case AMQP_RESPONSE_NORMAL:
            return {};
        case AMQP_RESPONSE_NONE:
            return action + ": broker returned no response";
        case AMQP_RESPONSE_LIBRARY_EXCEPTION:
            return action + ": " + amqp_error_string2(reply.library_error);
        case AMQP_RESPONSE_SERVER_EXCEPTION:
            if (reply.reply.id == AMQP_CONNECTION_CLOSE_METHOD) {
                const auto* close = static_cast<amqp_connection_close_t*>(
                    reply.reply.decoded);
                return action + ": connection closed by broker: " +
                    std::string(
                        static_cast<char*>(close->reply_text.bytes),
                        close->reply_text.len);
            }
            if (reply.reply.id == AMQP_CHANNEL_CLOSE_METHOD) {
                const auto* close = static_cast<amqp_channel_close_t*>(
                    reply.reply.decoded);
                return action + ": channel closed by broker: " +
                    std::string(
                        static_cast<char*>(close->reply_text.bytes),
                        close->reply_text.len);
            }
            return action + ": broker server exception";
    }
    return action + ": unknown AMQP response";
}

void close_connection(amqp_connection_state_t& connection) {
    if (connection == nullptr) {
        return;
    }
    amqp_channel_close(connection, kChannel, AMQP_REPLY_SUCCESS);
    amqp_connection_close(connection, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(connection);
    connection = nullptr;
}

bool open_connection(
    const RabbitMqConfig& config,
    amqp_connection_state_t& connection,
    bool declare_queue,
    std::string& error) {
    close_connection(connection);
    connection = amqp_new_connection();
    if (connection == nullptr) {
        error = "amqp_new_connection failed";
        return false;
    }
    amqp_socket_t* socket = amqp_tcp_socket_new(connection);
    if (socket == nullptr) {
        error = "amqp_tcp_socket_new failed";
        close_connection(connection);
        return false;
    }
    const int socket_status = amqp_socket_open(
        socket, config.host.c_str(), config.port);
    if (socket_status != AMQP_STATUS_OK) {
        error = std::string("cannot connect to RabbitMQ: ") +
            amqp_error_string2(socket_status);
        close_connection(connection);
        return false;
    }
    auto reply = amqp_login(
        connection,
        config.virtual_host.c_str(),
        0,
        131072,
        config.heartbeat_seconds,
        AMQP_SASL_METHOD_PLAIN,
        config.username.c_str(),
        config.password.c_str());
    error = rpc_error("RabbitMQ login", reply);
    if (!error.empty()) {
        close_connection(connection);
        return false;
    }
    amqp_channel_open(connection, kChannel);
    error = rpc_error("RabbitMQ channel open", amqp_get_rpc_reply(connection));
    if (!error.empty()) {
        close_connection(connection);
        return false;
    }
    amqp_exchange_declare(
        connection,
        kChannel,
        amqp_cstring_bytes(config.exchange.c_str()),
        amqp_cstring_bytes("direct"),
        0,
        1,
        0,
        0,
        amqp_empty_table);
    error = rpc_error("RabbitMQ exchange declare", amqp_get_rpc_reply(connection));
    if (!error.empty()) {
        close_connection(connection);
        return false;
    }
    if (declare_queue) {
        amqp_queue_declare(
            connection,
            kChannel,
            amqp_cstring_bytes(config.queue.c_str()),
            0,
            1,
            0,
            0,
            amqp_empty_table);
        error = rpc_error("RabbitMQ queue declare", amqp_get_rpc_reply(connection));
        if (!error.empty()) {
            close_connection(connection);
            return false;
        }
        amqp_queue_bind(
            connection,
            kChannel,
            amqp_cstring_bytes(config.queue.c_str()),
            amqp_cstring_bytes(config.exchange.c_str()),
            amqp_cstring_bytes(config.routing_key.c_str()),
            amqp_empty_table);
        error = rpc_error("RabbitMQ queue bind", amqp_get_rpc_reply(connection));
        if (!error.empty()) {
            close_connection(connection);
            return false;
        }
    }
    return true;
}

}  // namespace

class RabbitMqPublisher::State {
public:
    amqp_connection_state_t connection = nullptr;
};

RabbitMqPublisher::RabbitMqPublisher(RabbitMqConfig config)
    : config_(std::move(config)), state_(std::make_unique<State>()) {}

RabbitMqPublisher::~RabbitMqPublisher() {
    std::lock_guard lock(mutex_);
    disconnect_locked();
}

bool RabbitMqPublisher::connect_locked(std::string& error) {
    if (state_->connection != nullptr) {
        return true;
    }
    // The publisher also declares the durable queue. This prevents messages
    // from being dropped when TransmitServer starts before MessageServer.
    return open_connection(config_, state_->connection, true, error);
}

void RabbitMqPublisher::disconnect_locked() {
    close_connection(state_->connection);
}

bool RabbitMqPublisher::publish(
    const std::string& payload,
    std::string& error) {
    std::lock_guard lock(mutex_);
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!connect_locked(error)) {
            continue;
        }
        amqp_basic_properties_t properties{};
        properties._flags = AMQP_BASIC_CONTENT_TYPE_FLAG |
            AMQP_BASIC_DELIVERY_MODE_FLAG;
        properties.content_type = amqp_cstring_bytes("application/x-protobuf");
        properties.delivery_mode = 2;
        const int status = amqp_basic_publish(
            state_->connection,
            kChannel,
            amqp_cstring_bytes(config_.exchange.c_str()),
            amqp_cstring_bytes(config_.routing_key.c_str()),
            0,
            0,
            &properties,
            amqp_bytes_t{payload.size(), const_cast<char*>(payload.data())});
        if (status == AMQP_STATUS_OK) {
            return true;
        }
        error = std::string("RabbitMQ publish failed: ") +
            amqp_error_string2(status);
        disconnect_locked();
    }
    return false;
}

RabbitMqConsumer::RabbitMqConsumer(
    RabbitMqConfig config,
    Handler handler)
    : config_(std::move(config)), handler_(std::move(handler)) {}

RabbitMqConsumer::~RabbitMqConsumer() {
    stop();
}

bool RabbitMqConsumer::start(std::string& error) {
    if (running_) {
        return true;
    }
    amqp_connection_state_t probe = nullptr;
    if (!open_connection(config_, probe, true, error)) {
        return false;
    }
    close_connection(probe);
    running_ = true;
    thread_ = std::thread(&RabbitMqConsumer::consume_loop, this);
    return true;
}

void RabbitMqConsumer::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

void RabbitMqConsumer::consume_loop() {
    amqp_connection_state_t connection = nullptr;
    while (running_) {
        std::string error;
        if (!open_connection(config_, connection, true, error)) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        amqp_basic_qos(connection, kChannel, 0, 16, 0);
        if (!rpc_error("RabbitMQ qos", amqp_get_rpc_reply(connection)).empty()) {
            close_connection(connection);
            continue;
        }
        amqp_basic_consume(
            connection,
            kChannel,
            amqp_cstring_bytes(config_.queue.c_str()),
            amqp_empty_bytes,
            0,
            0,
            0,
            amqp_empty_table);
        if (!rpc_error(
                "RabbitMQ consume", amqp_get_rpc_reply(connection)).empty()) {
            close_connection(connection);
            continue;
        }
        while (running_) {
            amqp_maybe_release_buffers(connection);
            amqp_envelope_t envelope;
    timeval timeout{1, 0};
            const amqp_rpc_reply_t reply = amqp_consume_message(
                connection, &envelope, &timeout, 0);
            if (reply.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION &&
                reply.library_error == AMQP_STATUS_TIMEOUT) {
                continue;
            }
            if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
                break;
            }
            std::string handler_error;
            const bool handled = handler_(
                envelope.message.body.bytes,
                envelope.message.body.len,
                handler_error);
            if (handled) {
                amqp_basic_ack(
                    connection, kChannel, envelope.delivery_tag, 0);
            } else {
                // Malformed or permanently invalid messages must not create an
                // infinite hot requeue loop.
                amqp_basic_reject(
                    connection, kChannel, envelope.delivery_tag, 0);
            }
            amqp_destroy_envelope(&envelope);
        }
        close_connection(connection);
    }
    close_connection(connection);
}

}  // namespace chat::infra
