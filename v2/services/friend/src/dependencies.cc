#include "friend/dependencies.hpp"

#include "message.pb.h"
#include "user.pb.h"

#include <brpc/controller.h>

#include <stdexcept>
#include <unordered_set>

namespace chat::friend_service {

BrpcUserDirectory::BrpcUserDirectory(
    std::string server_address,
    std::int32_t timeout_ms) {
    brpc::ChannelOptions options;
    options.protocol = "baidu_std";
    options.timeout_ms = timeout_ms;
    options.max_retry = 3;
    if (channel_.Init(server_address.c_str(), &options) != 0) {
        throw std::runtime_error(
            "cannot initialize UserServer channel: " + server_address);
    }
}

bool BrpcUserDirectory::get_multi(
    const std::string& request_id,
    const std::vector<std::string>& user_ids,
    std::unordered_map<std::string, ahwei_im::UserInfo>& users,
    std::string& error) {
    users.clear();
    error.clear();
    if (user_ids.empty()) {
        return true;
    }

    ahwei_im::GetMultiUserInfoReq request;
    request.set_request_id(request_id);
    std::unordered_set<std::string> unique_ids;
    for (const auto& user_id : user_ids) {
        if (unique_ids.insert(user_id).second) {
            request.add_users_id(user_id);
        }
    }
    ahwei_im::GetMultiUserInfoRsp response;
    brpc::Controller controller;
    ahwei_im::UserService_Stub stub(&channel_);
    stub.GetMultiUserInfo(&controller, &request, &response, nullptr);
    if (controller.Failed()) {
        error = controller.ErrorText();
        return false;
    }
    if (!response.success()) {
        error = response.errmsg();
        return false;
    }
    for (const auto& item : response.users_info()) {
        users.emplace(item.first, item.second);
    }
    for (const auto& user_id : unique_ids) {
        if (users.find(user_id) == users.end()) {
            users.clear();
            error = "UserServer did not return user_id: " + user_id;
            return false;
        }
    }
    return true;
}

bool BrpcUserDirectory::search(
    const std::string& request_id,
    const std::string& search_key,
    const std::vector<std::string>& excluded_user_ids,
    std::vector<ahwei_im::UserInfo>& users,
    std::string& error) {
    users.clear();
    error.clear();
    ahwei_im::SearchUsersReq request;
    request.set_request_id(request_id);
    request.set_search_key(search_key);
    request.set_limit(50);
    for (const auto& user_id : excluded_user_ids) {
        request.add_exclude_user_ids(user_id);
    }

    ahwei_im::SearchUsersRsp response;
    brpc::Controller controller;
    ahwei_im::UserService_Stub stub(&channel_);
    stub.SearchUsers(&controller, &request, &response, nullptr);
    if (controller.Failed()) {
        error = controller.ErrorText();
        return false;
    }
    if (!response.success()) {
        error = response.errmsg();
        return false;
    }
    users.assign(response.users().begin(), response.users().end());
    return true;
}

bool BrpcUserDirectory::resolve_session(
    const std::string& request_id,
    const std::string& session_id,
    std::string& user_id,
    std::string& error) {
    user_id.clear();
    error.clear();
    ahwei_im::ResolveSessionReq request;
    request.set_request_id(request_id);
    request.set_session_id(session_id);
    ahwei_im::ResolveSessionRsp response;
    brpc::Controller controller;
    ahwei_im::UserService_Stub stub(&channel_);
    stub.ResolveSession(&controller, &request, &response, nullptr);
    if (controller.Failed()) {
        error = controller.ErrorText();
        return false;
    }
    if (!response.success()) {
        error = response.errmsg();
        return false;
    }
    if (response.user_id().empty()) {
        error = "UserServer returned an empty user_id for session";
        return false;
    }
    user_id = response.user_id();
    return true;
}

BrpcRecentMessageClient::BrpcRecentMessageClient(
    std::string server_address,
    std::int32_t timeout_ms) {
    brpc::ChannelOptions options;
    options.protocol = "baidu_std";
    options.timeout_ms = timeout_ms;
    options.max_retry = 3;
    if (channel_.Init(server_address.c_str(), &options) != 0) {
        throw std::runtime_error(
            "cannot initialize MessageStoreServer channel: " +
            server_address);
    }
}

bool BrpcRecentMessageClient::latest(
    const std::string& request_id,
    const std::string& chat_session_id,
    std::optional<ahwei_im::MessageInfo>& message,
    std::string& error) {
    message.reset();
    error.clear();
    ahwei_im::GetRecentMsgReq request;
    request.set_request_id(request_id);
    request.set_chat_session_id(chat_session_id);
    request.set_msg_count(1);
    ahwei_im::GetRecentMsgRsp response;
    brpc::Controller controller;
    ahwei_im::MsgStorageService_Stub stub(&channel_);
    stub.GetRecentMsg(&controller, &request, &response, nullptr);
    if (controller.Failed()) {
        error = controller.ErrorText();
        return false;
    }
    if (!response.success()) {
        error = response.errmsg();
        return false;
    }
    if (response.msg_list_size() > 0) {
        message = response.msg_list(response.msg_list_size() - 1);
    }
    return true;
}

}  // namespace chat::friend_service
