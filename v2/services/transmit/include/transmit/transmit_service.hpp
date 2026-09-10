#pragma once

#include "transmit.pb.h"
#include "transmit/dependencies.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace chat::transmit {

class TransmitServiceImpl final : public ahwei_im::MsgTransmitService {
public:
    using IdGenerator = std::function<std::string()>;
    using Clock = std::function<std::int64_t()>;

    TransmitServiceImpl(
        std::shared_ptr<UserClient> user_client,
        std::shared_ptr<SessionMemberRepository> member_repository,
        std::shared_ptr<MessagePublisher> publisher,
        IdGenerator id_generator = {},
        Clock clock = {});

    void GetTransmitTarget(
        google::protobuf::RpcController* controller,
        const ahwei_im::NewMessageReq* request,
        ahwei_im::GetTransmitTargetRsp* response,
        google::protobuf::Closure* done) override;

private:
    static bool validate_content(
        const ahwei_im::MessageContent& content,
        std::string& error);
    static std::string make_message_id();
    static std::int64_t current_timestamp();

    std::shared_ptr<UserClient> user_client_;
    std::shared_ptr<SessionMemberRepository> member_repository_;
    std::shared_ptr<MessagePublisher> publisher_;
    IdGenerator id_generator_;
    Clock clock_;
};

}  // namespace chat::transmit
