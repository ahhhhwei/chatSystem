#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace chat::infra {

struct RabbitMqConfig {
    std::string host{"127.0.0.1"};
    std::uint16_t port = 5672;
    std::string username{"guest"};
    std::string password{"guest"};
    std::string virtual_host{"/"};
    // 与 V1 配置文件保持一致，升级时不需要重建绑定关系。
    std::string exchange{"msg_exchange"};
    std::string queue{"msg_queue"};
    std::string routing_key{"msg_queue"};
    int heartbeat_seconds = 10;
};

class RabbitMqPublisher final {
public:
    explicit RabbitMqPublisher(RabbitMqConfig config);
    ~RabbitMqPublisher();
    RabbitMqPublisher(const RabbitMqPublisher&) = delete;
    RabbitMqPublisher& operator=(const RabbitMqPublisher&) = delete;

    bool publish(const std::string& payload, std::string& error);

private:
    bool connect_locked(std::string& error);
    void disconnect_locked();

    class State;
    RabbitMqConfig config_;
    std::mutex mutex_;
    std::unique_ptr<State> state_;
};

class RabbitMqConsumer final {
public:
    using Handler = std::function<bool(const void*, std::size_t, std::string&)>;

    RabbitMqConsumer(RabbitMqConfig config, Handler handler);
    ~RabbitMqConsumer();
    RabbitMqConsumer(const RabbitMqConsumer&) = delete;
    RabbitMqConsumer& operator=(const RabbitMqConsumer&) = delete;

    bool start(std::string& error);
    void stop();

private:
    void consume_loop();

    RabbitMqConfig config_;
    Handler handler_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

}  // namespace chat::infra
