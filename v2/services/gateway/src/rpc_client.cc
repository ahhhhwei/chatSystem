#include "gateway/rpc_client.hpp"

#include <brpc/channel.h>
#include <brpc/controller.h>

#include <array>
#include <stdexcept>
#include <utility>

namespace chat::gateway {

class BrpcRpcClient::ChannelHolder {
public:
    ChannelHolder(const std::string& address, int timeout_ms) {
        brpc::ChannelOptions options;
        options.protocol = "baidu_std";
        options.timeout_ms = timeout_ms;
        // Gateway 的接口包含注册、改资料、发消息等非幂等操作，禁止自动重试。
        options.max_retry = 0;
        if (channel.Init(address.c_str(), &options) != 0) {
            throw std::runtime_error("cannot initialize RPC channel: " + address);
        }
    }

    brpc::Channel channel;
};

namespace {

std::size_t index_of(DownstreamService service) {
    return static_cast<std::size_t>(service);
}

}  // namespace

BrpcRpcClient::BrpcRpcClient(const RpcEndpoints& endpoints) {
    const std::array<RpcEndpoint,
                     static_cast<std::size_t>(DownstreamService::COUNT)>
        configured{
            endpoints.user,
            endpoints.friend_service,
            endpoints.message,
            endpoints.transmit,
            endpoints.file,
            endpoints.speech,
        };
    for (std::size_t index = 0; index < configured.size(); ++index) {
        if (!configured[index].address.empty()) {
            channels_[index] = std::make_unique<ChannelHolder>(
                configured[index].address,
                configured[index].timeout_ms);
        }
    }
}

BrpcRpcClient::~BrpcRpcClient() = default;

bool BrpcRpcClient::call(
    DownstreamService service,
    const google::protobuf::MethodDescriptor& method,
    const google::protobuf::Message& request,
    google::protobuf::Message& response,
    std::string& error) {
    const auto index = index_of(service);
    if (index >= channels_.size() || !channels_[index]) {
        error = "downstream service is not configured";
        return false;
    }

    brpc::Controller controller;
    channels_[index]->channel.CallMethod(
        &method,
        &controller,
        &request,
        &response,
        nullptr);
    if (controller.Failed()) {
        error = controller.ErrorText();
        return false;
    }
    error.clear();
    return true;
}

}  // namespace chat::gateway
