#pragma once

#include "chat/infra/etcd.hpp"

#include <brpc/channel.h>

#include <cstdint>
#include <memory>
#include <string>

namespace chat::infra {

inline std::unique_ptr<brpc::Channel> make_brpc_channel(
    const std::shared_ptr<EndpointResolver>& resolver,
    std::int32_t timeout_ms,
    int max_retry,
    std::string& error) {
    std::string endpoint;
    if (!resolver || !resolver->resolve(endpoint, error)) {
        if (error.empty()) error = "cannot resolve service endpoint";
        return nullptr;
    }
    auto channel = std::make_unique<brpc::Channel>();
    brpc::ChannelOptions options;
    options.protocol = "baidu_std";
    options.timeout_ms = timeout_ms;
    options.max_retry = max_retry;
    if (channel->Init(endpoint.c_str(), &options) != 0) {
        error = "cannot initialize RPC channel: " + endpoint;
        return nullptr;
    }
    return channel;
}

}  // namespace chat::infra
