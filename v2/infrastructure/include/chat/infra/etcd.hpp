#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace chat::infra {

class EndpointResolver {
public:
    virtual ~EndpointResolver() = default;
    virtual bool resolve(std::string& endpoint, std::string& error) = 0;
};

class StaticEndpointResolver final : public EndpointResolver {
public:
    explicit StaticEndpointResolver(std::string endpoint);
    bool resolve(std::string& endpoint, std::string& error) override;

private:
    std::string endpoint_;
};

class EtcdClient final {
public:
    explicit EtcdClient(
        std::string endpoint = "http://127.0.0.1:2379",
        long timeout_ms = 3000);

    bool put_with_ttl(
        const std::string& key,
        const std::string& value,
        std::chrono::seconds ttl,
        std::string& error) const;
    bool erase(const std::string& key, std::string& error) const;
    bool values(
        const std::string& prefix,
        std::vector<std::string>& values,
        std::string& error) const;
    bool health(std::string& error) const;

private:
    std::string key_url(const std::string& key) const;
    std::string endpoint_;
    long timeout_ms_;
};

class EtcdEndpointResolver final : public EndpointResolver {
public:
    EtcdEndpointResolver(
        std::shared_ptr<EtcdClient> client,
        std::string service_prefix);
    bool resolve(std::string& endpoint, std::string& error) override;

private:
    std::shared_ptr<EtcdClient> client_;
    std::string service_prefix_;
    std::mutex mutex_;
    std::size_t next_ = 0;
};

class ServiceRegistry final {
public:
    ServiceRegistry(
        std::shared_ptr<EtcdClient> client,
        std::string key,
        std::string endpoint,
        std::chrono::seconds ttl = std::chrono::seconds(10));
    ~ServiceRegistry();
    ServiceRegistry(const ServiceRegistry&) = delete;
    ServiceRegistry& operator=(const ServiceRegistry&) = delete;

    bool start(std::string& error);
    void stop();

private:
    void keepalive_loop();

    std::shared_ptr<EtcdClient> client_;
    std::string key_;
    std::string endpoint_;
    std::chrono::seconds ttl_;
    std::atomic<bool> running_{false};
    std::thread keepalive_thread_;
};

}  // namespace chat::infra

