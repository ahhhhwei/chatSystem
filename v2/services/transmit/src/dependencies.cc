#include "transmit/dependencies.hpp"

#include "message.pb.h"
#include "friend.pb.h"
#include "user.pb.h"
#include "chat/infra/brpc_resolver.hpp"

#include <brpc/controller.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace chat::transmit {

bool IdentityUserClient::get_user(
    const std::string&,
    const std::string& user_id,
    ahwei_im::UserInfo& user,
    std::string& error) {
    user.Clear();
    error.clear();
    if (user_id.empty()) {
        error = "user_id cannot be empty";
        return false;
    }
    user.set_user_id(user_id);
    return true;
}

BrpcUserClient::BrpcUserClient(
    std::string server_address,
    std::int32_t timeout_ms)
    : BrpcUserClient(
          std::make_shared<infra::StaticEndpointResolver>(std::move(server_address)),
          timeout_ms) {}

BrpcUserClient::BrpcUserClient(
    std::shared_ptr<infra::EndpointResolver> resolver,
    std::int32_t timeout_ms)
    : resolver_(std::move(resolver)), timeout_ms_(timeout_ms) {}

bool BrpcUserClient::get_user(
    const std::string& request_id,
    const std::string& user_id,
    ahwei_im::UserInfo& user,
    std::string& error) {
    user.Clear();
    error.clear();
    if (user_id.empty()) {
        error = "user_id cannot be empty";
        return false;
    }

    ahwei_im::GetUserInfoReq request;
    request.set_request_id(request_id);
    request.set_user_id(user_id);
    ahwei_im::GetUserInfoRsp response;
    brpc::Controller controller;
    auto channel = infra::make_brpc_channel(resolver_, timeout_ms_, 3, error);
    if (!channel) return false;
    ahwei_im::UserService_Stub stub(channel.get());
    stub.GetUserInfo(&controller, &request, &response, nullptr);
    if (controller.Failed()) {
        error = controller.ErrorText();
        return false;
    }
    if (!response.success()) {
        error = response.errmsg();
        return false;
    }
    if (!response.has_user_info()) {
        error = "UserServer returned no user information";
        return false;
    }
    user.CopyFrom(response.user_info());
    return true;
}

FileSessionMemberRepository::FileSessionMemberRepository(
    std::filesystem::path members_file)
    : members_file_(std::move(members_file)) {
    if (members_file_.empty()) {
        throw std::invalid_argument("session members file cannot be empty");
    }

    std::error_code error;
    const auto parent = members_file_.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            throw std::runtime_error(
                "cannot create session members directory: " +
                error.message());
        }
    }
    if (!std::filesystem::exists(members_file_, error)) {
        std::ofstream output(members_file_);
        if (!output) {
            throw std::runtime_error("cannot create session members file");
        }
    } else if (error) {
        throw std::runtime_error(
            "cannot inspect session members file: " + error.message());
    }
    load();
}

bool FileSessionMemberRepository::members(
    const std::string& chat_session_id,
    std::vector<std::string>& user_ids,
    std::string& error) const {
    user_ids.clear();
    error.clear();
    const auto item = members_.find(chat_session_id);
    if (item != members_.end()) {
        user_ids = item->second;
    }
    return true;
}

const std::filesystem::path&
FileSessionMemberRepository::members_file() const noexcept {
    return members_file_;
}

void FileSessionMemberRepository::load() {
    std::ifstream input(members_file_);
    if (!input) {
        throw std::runtime_error("cannot open session members file");
    }

    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] == '#') {
            continue;
        }

        std::istringstream fields(line);
        std::string chat_session_id;
        std::string user_id;
        std::string extra;
        if (!(fields >> chat_session_id >> user_id) || (fields >> extra)) {
            throw std::runtime_error(
                "invalid session members line " +
                std::to_string(line_number));
        }

        auto& session_members = members_[chat_session_id];
        if (std::find(
                session_members.begin(),
                session_members.end(),
                user_id) == session_members.end()) {
            session_members.push_back(std::move(user_id));
        }
    }
    if (input.bad()) {
        throw std::runtime_error("cannot read session members file");
    }
}

BrpcFriendSessionMemberRepository::BrpcFriendSessionMemberRepository(
    std::string server_address,
    std::int32_t timeout_ms)
    : BrpcFriendSessionMemberRepository(
          std::make_shared<infra::StaticEndpointResolver>(std::move(server_address)),
          timeout_ms) {}

BrpcFriendSessionMemberRepository::BrpcFriendSessionMemberRepository(
    std::shared_ptr<infra::EndpointResolver> resolver,
    std::int32_t timeout_ms)
    : resolver_(std::move(resolver)), timeout_ms_(timeout_ms) {}

bool BrpcFriendSessionMemberRepository::members(
    const std::string& chat_session_id,
    std::vector<std::string>& user_ids,
    std::string& error) const {
    user_ids.clear();
    error.clear();
    if (chat_session_id.empty()) {
        error = "chat_session_id cannot be empty";
        return false;
    }

    ahwei_im::GetChatSessionMemberIdsReq request;
    request.set_request_id("transmit-members-" + chat_session_id);
    request.set_chat_session_id(chat_session_id);
    ahwei_im::GetChatSessionMemberIdsRsp response;
    brpc::Controller controller;
    auto channel = infra::make_brpc_channel(resolver_, timeout_ms_, 3, error);
    if (!channel) return false;
    ahwei_im::FriendService_Stub stub(channel.get());
    stub.GetChatSessionMemberIds(&controller, &request, &response, nullptr);
    if (controller.Failed()) {
        error = controller.ErrorText();
        return false;
    }
    if (!response.success()) {
        error = response.errmsg();
        return false;
    }
    user_ids.assign(
        response.member_id_list().begin(),
        response.member_id_list().end());
    return true;
}

BrpcMessagePublisher::BrpcMessagePublisher(
    std::string server_address,
    std::int32_t timeout_ms)
    : BrpcMessagePublisher(
          std::make_shared<infra::StaticEndpointResolver>(std::move(server_address)),
          timeout_ms) {}

BrpcMessagePublisher::BrpcMessagePublisher(
    std::shared_ptr<infra::EndpointResolver> resolver,
    std::int32_t timeout_ms)
    : resolver_(std::move(resolver)), timeout_ms_(timeout_ms) {}

bool BrpcMessagePublisher::publish(
    const std::string& request_id,
    const ahwei_im::MessageInfo& message,
    std::string& error) {
    error.clear();
    ahwei_im::StoreMessageReq request;
    request.set_request_id(request_id);
    request.mutable_message()->CopyFrom(message);

    ahwei_im::StoreMessageRsp response;
    brpc::Controller controller;
    auto channel = infra::make_brpc_channel(resolver_, timeout_ms_, 0, error);
    if (!channel) return false;
    ahwei_im::MsgStorageService_Stub stub(channel.get());
    stub.StoreMessage(&controller, &request, &response, nullptr);
    if (controller.Failed()) {
        error = controller.ErrorText();
        return false;
    }
    if (!response.success()) {
        error = response.errmsg();
        return false;
    }
    return true;
}

RabbitMqMessagePublisher::RabbitMqMessagePublisher(infra::RabbitMqConfig config)
    : publisher_(std::move(config)) {}

bool RabbitMqMessagePublisher::publish(
    const std::string&,
    const ahwei_im::MessageInfo& message,
    std::string& error) {
    std::string payload;
    if (!message.SerializeToString(&payload)) {
        error = "cannot serialize MessageInfo for RabbitMQ";
        return false;
    }
    return publisher_.publish(payload, error);
}

}  // namespace chat::transmit
