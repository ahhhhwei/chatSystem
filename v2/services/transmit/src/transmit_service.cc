#include "transmit/transmit_service.hpp"

#include <brpc/closure_guard.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

namespace chat::transmit {
namespace {

void set_error(
    ahwei_im::GetTransmitTargetRsp* response,
    const std::string& request_id,
    const std::string& error) {
    response->set_request_id(request_id);
    response->set_success(false);
    response->set_errmsg(error);
    response->clear_message();
    response->clear_target_id_list();
}

}  // namespace

TransmitServiceImpl::TransmitServiceImpl(
    std::shared_ptr<UserClient> user_client,
    std::shared_ptr<SessionMemberRepository> member_repository,
    std::shared_ptr<MessagePublisher> publisher,
    IdGenerator id_generator,
    Clock clock)
    : user_client_(std::move(user_client)),
      member_repository_(std::move(member_repository)),
      publisher_(std::move(publisher)),
      id_generator_(id_generator ? std::move(id_generator) : make_message_id),
      clock_(clock ? std::move(clock) : current_timestamp) {
    if (!user_client_) {
        throw std::invalid_argument("UserClient cannot be null");
    }
    if (!member_repository_) {
        throw std::invalid_argument(
            "SessionMemberRepository cannot be null");
    }
    if (!publisher_) {
        throw std::invalid_argument("MessagePublisher cannot be null");
    }
}

void TransmitServiceImpl::GetTransmitTarget(
    google::protobuf::RpcController*,
    const ahwei_im::NewMessageReq* request,
    ahwei_im::GetTransmitTargetRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);

    // 1. 校验请求中组装消息所需的基础字段。
    const auto& request_id = request->request_id();
    if (request_id.empty()) {
        set_error(response, request_id, "request_id cannot be empty");
        return;
    }
    if (request->user_id().empty()) {
        set_error(response, request_id, "user_id cannot be empty");
        return;
    }
    if (request->chat_session_id().empty()) {
        set_error(response, request_id, "chat_session_id cannot be empty");
        return;
    }
    if (!request->has_message()) {
        set_error(response, request_id, "message cannot be empty");
        return;
    }

    std::string error;
    if (!validate_content(request->message(), error)) {
        set_error(response, request_id, error);
        return;
    }

    // 2. 从 UserServer 适配器获取完整的发送者信息。
    ahwei_im::UserInfo sender;
    if (!user_client_->get_user(
            request_id,
            request->user_id(),
            sender,
            error)) {
        if (error.empty()) {
            error = "cannot get sender information";
        }
        set_error(response, request_id, error);
        return;
    }
    if (sender.user_id().empty()) {
        set_error(response, request_id, "UserClient returned an empty user_id");
        return;
    }
    if (sender.user_id() != request->user_id()) {
        set_error(response, request_id, "UserClient returned the wrong user");
        return;
    }

    // 3. 查询推送目标，并确保发送者确实属于该会话。
    std::vector<std::string> members;
    if (!member_repository_->members(
            request->chat_session_id(),
            members,
            error)) {
        if (error.empty()) {
            error = "cannot get chat session members";
        }
        set_error(response, request_id, error);
        return;
    }
    if (std::find(
            members.begin(),
            members.end(),
            request->user_id()) == members.end()) {
        set_error(response, request_id, "sender is not a chat session member");
        return;
    }

    // 4. 生成全链路使用的完整 MessageInfo。
    ahwei_im::MessageInfo message;
    message.set_message_id(id_generator_());
    if (message.message_id().empty()) {
        set_error(response, request_id, "cannot generate message_id");
        return;
    }
    message.set_chat_session_id(request->chat_session_id());
    message.set_timestamp(clock_());
    message.mutable_sender()->CopyFrom(sender);
    message.mutable_message()->CopyFrom(request->message());

    // 5. 先投递给存储端；投递失败时不允许 Gateway 继续推送。
    if (!publisher_->publish(request_id, message, error)) {
        if (error.empty()) {
            error = "cannot publish message for persistence";
        }
        spdlog::warn(
            "publish message {} failed: {}",
            message.message_id(),
            error);
        set_error(response, request_id, error);
        return;
    }

    // 6. 返回完整消息和去重后的 WebSocket 推送目标。
    response->set_request_id(request_id);
    response->set_success(true);
    response->mutable_message()->CopyFrom(message);
    std::unordered_set<std::string> unique_members;
    for (const auto& user_id : members) {
        if (!user_id.empty() && unique_members.insert(user_id).second) {
            response->add_target_id_list(user_id);
        }
    }
}

bool TransmitServiceImpl::validate_content(
    const ahwei_im::MessageContent& content,
    std::string& error) {
    switch (content.message_type()) {
        case ahwei_im::STRING:
            if (!content.has_string_message()) {
                error = "STRING message is missing string_message";
                return false;
            }
            return true;
        case ahwei_im::IMAGE:
            if (!content.has_image_message()) {
                error = "IMAGE message is missing image_message";
                return false;
            }
            return true;
        case ahwei_im::FILE:
            if (!content.has_file_message()) {
                error = "FILE message is missing file_message";
                return false;
            }
            if (content.file_message().file_size() < 0) {
                error = "FILE size cannot be negative";
                return false;
            }
            return true;
        case ahwei_im::SPEECH:
            if (!content.has_speech_message()) {
                error = "SPEECH message is missing speech_message";
                return false;
            }
            return true;
        default:
            error = "unsupported message type";
            return false;
    }
}

std::string TransmitServiceImpl::make_message_id() {
    thread_local std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<int> distribution(0, 255);
    std::array<unsigned char, 16> bytes{};
    for (auto& byte : bytes) {
        byte = static_cast<unsigned char>(distribution(generator));
    }
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0fU) | 0x40U);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3fU) | 0x80U);

    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) {
            result << '-';
        }
        result << std::setw(2) << static_cast<int>(bytes[index]);
    }
    return result.str();
}

std::int64_t TransmitServiceImpl::current_timestamp() {
    return static_cast<std::int64_t>(std::time(nullptr));
}

}  // namespace chat::transmit
