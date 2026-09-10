#pragma once

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include <array>
#include <memory>
#include <string>

namespace chat::gateway {

enum class DownstreamService {
    USER = 0,
    FRIEND,
    MESSAGE,
    TRANSMIT,
    FILE,
    SPEECH,
    COUNT,
};

class RpcClient {
public:
    virtual ~RpcClient() = default;

    virtual bool call(
        DownstreamService service,
        const google::protobuf::MethodDescriptor& method,
        const google::protobuf::Message& request,
        google::protobuf::Message& response,
        std::string& error) = 0;
};

struct RpcEndpoint {
    std::string address;
    int timeout_ms = 3000;
};

struct RpcEndpoints {
    RpcEndpoint user{"127.0.0.1:10003", 3000};
    RpcEndpoint friend_service{"127.0.0.1:10006", 3000};
    RpcEndpoint message{"127.0.0.1:10005", 3000};
    RpcEndpoint transmit{"127.0.0.1:10004", 3000};
    RpcEndpoint file{"127.0.0.1:10002", 10000};
    // 空地址表示暂不启用语音识别。HTTP 路由仍然存在并返回明确错误。
    RpcEndpoint speech{"", 10000};
};

class BrpcRpcClient final : public RpcClient {
public:
    explicit BrpcRpcClient(const RpcEndpoints& endpoints);
    ~BrpcRpcClient() override;

    BrpcRpcClient(const BrpcRpcClient&) = delete;
    BrpcRpcClient& operator=(const BrpcRpcClient&) = delete;

    bool call(
        DownstreamService service,
        const google::protobuf::MethodDescriptor& method,
        const google::protobuf::Message& request,
        google::protobuf::Message& response,
        std::string& error) override;

private:
    class ChannelHolder;
    std::array<std::unique_ptr<ChannelHolder>,
               static_cast<std::size_t>(DownstreamService::COUNT)>
        channels_;
};

}  // namespace chat::gateway
