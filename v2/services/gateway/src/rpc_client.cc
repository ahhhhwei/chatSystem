#include "gateway/rpc_client.hpp"
#include "chat/infra/brpc_resolver.hpp"

#include <brpc/channel.h>
#include <brpc/controller.h>

#include <array>
#include <stdexcept>
#include <utility>

namespace chat::gateway {

class BrpcRpcClient::ChannelHolder {
public:
    explicit ChannelHolder(const RpcEndpoint& endpoint)
        : resolver(endpoint.resolver
              ? endpoint.resolver
              : std::make_shared<infra::StaticEndpointResolver>(endpoint.address)),
          timeout_ms(endpoint.timeout_ms) {}

    std::shared_ptr<infra::EndpointResolver> resolver;
    int timeout_ms;
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
        if (!configured[index].address.empty() || configured[index].resolver) {
            channels_[index] = std::make_unique<ChannelHolder>(configured[index]);
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
    auto channel = infra::make_brpc_channel(
        channels_[index]->resolver, channels_[index]->timeout_ms, 0, error);
    if (!channel) return false;
    channel->CallMethod(
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
