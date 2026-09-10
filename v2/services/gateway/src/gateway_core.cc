#include "gateway/gateway_core.hpp"

#include "file.pb.h"
#include "friend.pb.h"
#include "gateway.pb.h"
#include "message.pb.h"
#include "speech.pb.h"
#include "transmit.pb.h"
#include "user.pb.h"

#include <google/protobuf/descriptor.h>
#include <spdlog/spdlog.h>

#include <functional>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace chat::gateway {
namespace {

const google::protobuf::MethodDescriptor& method(
    const google::protobuf::ServiceDescriptor* service,
    const char* name) {
    const auto* descriptor = service->FindMethodByName(name);
    if (!descriptor) {
        throw std::logic_error(std::string("RPC method not found: ") + name);
    }
    return *descriptor;
}

template <typename Response>
HttpResponse serialized(Response& response) {
    return HttpResponse{200, kProtobufContentType, response.SerializeAsString()};
}

template <typename Request, typename Response>
HttpResponse parse_error(const Request& request, const std::string& error) {
    Response response;
    response.set_request_id(request.request_id());
    response.set_success(false);
    response.set_errmsg(error);
    return serialized(response);
}

template <typename Request, typename Response>
HttpResponse forward_public(
    RpcClient& rpc,
    DownstreamService service,
    const google::protobuf::MethodDescriptor& rpc_method,
    const std::string& body) {
    Request request;
    if (!request.ParseFromString(body)) {
        return parse_error<Request, Response>(
            request, "cannot parse protobuf request body");
    }

    Response response;
    std::string error;
    if (!rpc.call(service, rpc_method, request, response, error)) {
        response.set_request_id(request.request_id());
        response.set_success(false);
        response.set_errmsg("downstream RPC failed: " + error);
    }
    return serialized(response);
}

template <typename Request, typename Response>
HttpResponse forward_authenticated(
    RpcClient& rpc,
    const std::function<bool(
        const std::string&,
        const std::string&,
        std::string&,
        std::string&)>& resolver,
    DownstreamService service,
    const google::protobuf::MethodDescriptor& rpc_method,
    const std::string& body) {
    Request request;
    if (!request.ParseFromString(body)) {
        return parse_error<Request, Response>(
            request, "cannot parse protobuf request body");
    }

    std::string user_id;
    std::string error;
    if (!resolver(
            request.request_id(),
            request.session_id(),
            user_id,
            error)) {
        return parse_error<Request, Response>(request, error);
    }
    // 客户端提交的 user_id 不可信，只使用 session_id 对应的真实用户。
    request.set_user_id(user_id);

    Response response;
    if (!rpc.call(service, rpc_method, request, response, error)) {
        response.set_request_id(request.request_id());
        response.set_success(false);
        response.set_errmsg("downstream RPC failed: " + error);
    }
    return serialized(response);
}

}  // namespace

GatewayCore::GatewayCore(
    std::shared_ptr<RpcClient> rpc_client,
    std::shared_ptr<NotificationSink> notifications)
    : rpc_client_(std::move(rpc_client)),
      notifications_(std::move(notifications)) {
    if (!rpc_client_) {
        throw std::invalid_argument("RpcClient cannot be null");
    }
    if (!notifications_) {
        throw std::invalid_argument("NotificationSink cannot be null");
    }
}

HttpResponse GatewayCore::handle_http(
    const std::string& path,
    const std::string& body) {
    auto resolver = [this](
                        const std::string& request_id,
                        const std::string& session_id,
                        std::string& user_id,
                        std::string& error) {
        return resolve_session(
            request_id, session_id, user_id, error);
    };

    if (path == "/service/user/get_phone_verify_code") {
        return forward_public<
            ahwei_im::PhoneVerifyCodeReq,
            ahwei_im::PhoneVerifyCodeRsp>(
            *rpc_client_,
            DownstreamService::USER,
            method(ahwei_im::UserService::descriptor(), "GetPhoneVerifyCode"),
            body);
    }
    if (path == "/service/user/username_register") {
        return forward_public<
            ahwei_im::UserRegisterReq,
            ahwei_im::UserRegisterRsp>(
            *rpc_client_,
            DownstreamService::USER,
            method(ahwei_im::UserService::descriptor(), "UserRegister"),
            body);
    }
    if (path == "/service/user/username_login") {
        return forward_public<ahwei_im::UserLoginReq, ahwei_im::UserLoginRsp>(
            *rpc_client_,
            DownstreamService::USER,
            method(ahwei_im::UserService::descriptor(), "UserLogin"),
            body);
    }
    if (path == "/service/user/phone_register") {
        return forward_public<
            ahwei_im::PhoneRegisterReq,
            ahwei_im::PhoneRegisterRsp>(
            *rpc_client_,
            DownstreamService::USER,
            method(ahwei_im::UserService::descriptor(), "PhoneRegister"),
            body);
    }
    if (path == "/service/user/phone_login") {
        return forward_public<ahwei_im::PhoneLoginReq, ahwei_im::PhoneLoginRsp>(
            *rpc_client_,
            DownstreamService::USER,
            method(ahwei_im::UserService::descriptor(), "PhoneLogin"),
            body);
    }

#define AUTH_FORWARD(PATH, REQUEST, RESPONSE, SERVICE, DESCRIPTOR, RPC_METHOD) \
    if (path == PATH) {                                                        \
        return forward_authenticated<ahwei_im::REQUEST, ahwei_im::RESPONSE>(   \
            *rpc_client_,                                                      \
            resolver,                                                          \
            DownstreamService::SERVICE,                                        \
            method(ahwei_im::DESCRIPTOR::descriptor(), RPC_METHOD),            \
            body);                                                             \
    }

    AUTH_FORWARD(
        "/service/user/get_user_info",
        GetUserInfoReq,
        GetUserInfoRsp,
        USER,
        UserService,
        "GetUserInfo")
    AUTH_FORWARD(
        "/service/user/set_avatar",
        SetUserAvatarReq,
        SetUserAvatarRsp,
        USER,
        UserService,
        "SetUserAvatar")
    AUTH_FORWARD(
        "/service/user/set_nickname",
        SetUserNicknameReq,
        SetUserNicknameRsp,
        USER,
        UserService,
        "SetUserNickname")
    AUTH_FORWARD(
        "/service/user/set_description",
        SetUserDescriptionReq,
        SetUserDescriptionRsp,
        USER,
        UserService,
        "SetUserDescription")
    AUTH_FORWARD(
        "/service/user/set_phone",
        SetUserPhoneNumberReq,
        SetUserPhoneNumberRsp,
        USER,
        UserService,
        "SetUserPhoneNumber")

    AUTH_FORWARD(
        "/service/friend/get_friend_list",
        GetFriendListReq,
        GetFriendListRsp,
        FRIEND,
        FriendService,
        "GetFriendList")
    AUTH_FORWARD(
        "/service/friend/search_friend",
        FriendSearchReq,
        FriendSearchRsp,
        FRIEND,
        FriendService,
        "FriendSearch")
    AUTH_FORWARD(
        "/service/friend/get_pending_friend_events",
        GetPendingFriendEventListReq,
        GetPendingFriendEventListRsp,
        FRIEND,
        FriendService,
        "GetPendingFriendEventList")
    AUTH_FORWARD(
        "/service/friend/get_chat_session_list",
        GetChatSessionListReq,
        GetChatSessionListRsp,
        FRIEND,
        FriendService,
        "GetChatSessionList")
    AUTH_FORWARD(
        "/service/friend/get_chat_session_member",
        GetChatSessionMemberReq,
        GetChatSessionMemberRsp,
        FRIEND,
        FriendService,
        "GetChatSessionMember")

    AUTH_FORWARD(
        "/service/message_storage/get_history",
        GetHistoryMsgReq,
        GetHistoryMsgRsp,
        MESSAGE,
        MsgStorageService,
        "GetHistoryMsg")
    AUTH_FORWARD(
        "/service/message_storage/get_recent",
        GetRecentMsgReq,
        GetRecentMsgRsp,
        MESSAGE,
        MsgStorageService,
        "GetRecentMsg")
    AUTH_FORWARD(
        "/service/message_storage/search_history",
        MsgSearchReq,
        MsgSearchRsp,
        MESSAGE,
        MsgStorageService,
        "MsgSearch")

    AUTH_FORWARD(
        "/service/file/get_single_file",
        GetSingleFileReq,
        GetSingleFileRsp,
        FILE,
        FileService,
        "GetSingleFile")
    AUTH_FORWARD(
        "/service/file/get_multi_file",
        GetMultiFileReq,
        GetMultiFileRsp,
        FILE,
        FileService,
        "GetMultiFile")
    AUTH_FORWARD(
        "/service/file/put_single_file",
        PutSingleFileReq,
        PutSingleFileRsp,
        FILE,
        FileService,
        "PutSingleFile")
    AUTH_FORWARD(
        "/service/file/put_multi_file",
        PutMultiFileReq,
        PutMultiFileRsp,
        FILE,
        FileService,
        "PutMultiFile")

    AUTH_FORWARD(
        "/service/speech/recognition",
        SpeechRecognitionReq,
        SpeechRecognitionRsp,
        SPEECH,
        SpeechService,
        "SpeechRecognition")

#undef AUTH_FORWARD

    if (path == "/service/friend/add_friend_apply") {
        return friend_add(body);
    }
    if (path == "/service/friend/add_friend_process") {
        return friend_add_process(body);
    }
    if (path == "/service/friend/remove_friend") {
        return friend_remove(body);
    }
    if (path == "/service/friend/create_chat_session") {
        return chat_session_create(body);
    }
    if (path == "/service/message_transmit/new_message") {
        return new_message(body);
    }

    return HttpResponse{404, "text/plain; charset=utf-8", "route not found"};
}

bool GatewayCore::authenticate_websocket(
    const std::string& body,
    std::string& user_id,
    std::string& session_id,
    std::string& error) {
    ahwei_im::ClientAuthenticationReq request;
    if (!request.ParseFromString(body)) {
        error = "cannot parse WebSocket authentication request";
        return false;
    }
    if (request.session_id().empty()) {
        error = "session_id cannot be empty";
        return false;
    }
    session_id = request.session_id();
    return resolve_session(
        request.request_id(), session_id, user_id, error);
}

void GatewayCore::revoke_session(const std::string& session_id) {
    if (session_id.empty()) {
        return;
    }
    ahwei_im::RevokeSessionReq request;
    ahwei_im::RevokeSessionRsp response;
    request.set_request_id("gateway-websocket-close");
    request.set_session_id(session_id);
    std::string error;
    if (!rpc_client_->call(
            DownstreamService::USER,
            method(ahwei_im::UserService::descriptor(), "RevokeSession"),
            request,
            response,
            error)) {
        spdlog::warn("cannot revoke disconnected session: {}", error);
    }
}

bool GatewayCore::resolve_session(
    const std::string& request_id,
    const std::string& session_id,
    std::string& user_id,
    std::string& error) {
    if (session_id.empty()) {
        error = "session_id cannot be empty";
        return false;
    }
    ahwei_im::ResolveSessionReq request;
    ahwei_im::ResolveSessionRsp response;
    request.set_request_id(request_id);
    request.set_session_id(session_id);
    if (!rpc_client_->call(
            DownstreamService::USER,
            method(ahwei_im::UserService::descriptor(), "ResolveSession"),
            request,
            response,
            error)) {
        error = "session service RPC failed: " + error;
        return false;
    }
    if (!response.success() || response.user_id().empty()) {
        error = response.errmsg().empty()
            ? "login session is invalid or expired"
            : response.errmsg();
        return false;
    }
    user_id = response.user_id();
    error.clear();
    return true;
}

bool GatewayCore::get_user(
    const std::string& request_id,
    const std::string& user_id,
    ahwei_im::UserInfo& user,
    std::string& error) {
    ahwei_im::GetUserInfoReq request;
    ahwei_im::GetUserInfoRsp response;
    request.set_request_id(request_id);
    request.set_user_id(user_id);
    if (!rpc_client_->call(
            DownstreamService::USER,
            method(ahwei_im::UserService::descriptor(), "GetUserInfo"),
            request,
            response,
            error)) {
        return false;
    }
    if (!response.success() || !response.has_user_info()) {
        error = response.errmsg().empty()
            ? "cannot get user information"
            : response.errmsg();
        return false;
    }
    user.CopyFrom(response.user_info());
    return true;
}

HttpResponse GatewayCore::friend_add(const std::string& body) {
    ahwei_im::FriendAddReq request;
    if (!request.ParseFromString(body)) {
        return parse_error<ahwei_im::FriendAddReq, ahwei_im::FriendAddRsp>(
            request, "cannot parse protobuf request body");
    }
    std::string actor_id;
    std::string error;
    if (!resolve_session(
            request.request_id(),
            request.session_id(),
            actor_id,
            error)) {
        return parse_error<ahwei_im::FriendAddReq, ahwei_im::FriendAddRsp>(
            request, error);
    }
    request.set_user_id(actor_id);

    ahwei_im::FriendAddRsp response;
    if (!rpc_client_->call(
            DownstreamService::FRIEND,
            method(ahwei_im::FriendService::descriptor(), "FriendAdd"),
            request,
            response,
            error)) {
        response.set_request_id(request.request_id());
        response.set_success(false);
        response.set_errmsg("downstream RPC failed: " + error);
        return serialized(response);
    }

    if (response.success()) {
        ahwei_im::UserInfo actor;
        if (get_user(request.request_id(), actor_id, actor, error)) {
            ahwei_im::NotifyMessage notification;
            notification.set_notify_event_id(response.notify_event_id());
            notification.set_notify_type(ahwei_im::FRIEND_ADD_APPLY_NOTIFY);
            notification.mutable_friend_add_apply()
                ->mutable_user_info()
                ->CopyFrom(actor);
            notifications_->send(request.respondent_id(), notification);
        } else {
            spdlog::warn(
                "friend application succeeded but profile notification was "
                "skipped: {}",
                error);
        }
    }
    return serialized(response);
}

HttpResponse GatewayCore::friend_add_process(const std::string& body) {
    ahwei_im::FriendAddProcessReq request;
    if (!request.ParseFromString(body)) {
        return parse_error<
            ahwei_im::FriendAddProcessReq,
            ahwei_im::FriendAddProcessRsp>(
            request, "cannot parse protobuf request body");
    }
    std::string respondent_id;
    std::string error;
    if (!resolve_session(
            request.request_id(),
            request.session_id(),
            respondent_id,
            error)) {
        return parse_error<
            ahwei_im::FriendAddProcessReq,
            ahwei_im::FriendAddProcessRsp>(request, error);
    }
    request.set_user_id(respondent_id);

    ahwei_im::FriendAddProcessRsp response;
    if (!rpc_client_->call(
            DownstreamService::FRIEND,
            method(ahwei_im::FriendService::descriptor(), "FriendAddProcess"),
            request,
            response,
            error)) {
        response.set_request_id(request.request_id());
        response.set_success(false);
        response.set_errmsg("downstream RPC failed: " + error);
        return serialized(response);
    }
    if (!response.success()) {
        return serialized(response);
    }

    ahwei_im::UserInfo applicant;
    ahwei_im::UserInfo respondent;
    std::string applicant_error;
    std::string respondent_error;
    const bool has_applicant = get_user(
        request.request_id(),
        request.apply_user_id(),
        applicant,
        applicant_error);
    const bool has_respondent = get_user(
        request.request_id(), respondent_id, respondent, respondent_error);

    if (has_respondent) {
        ahwei_im::NotifyMessage notification;
        notification.set_notify_event_id(request.notify_event_id());
        notification.set_notify_type(ahwei_im::FRIEND_ADD_PROCESS_NOTIFY);
        auto* result = notification.mutable_friend_process_result();
        result->set_agree(request.agree());
        result->mutable_user_info()->CopyFrom(respondent);
        notifications_->send(request.apply_user_id(), notification);
    }

    if (request.agree() && has_respondent) {
        ahwei_im::NotifyMessage notification;
        notification.set_notify_type(ahwei_im::CHAT_SESSION_CREATE_NOTIFY);
        auto* session = notification.mutable_new_chat_session_info()
                            ->mutable_chat_session_info();
        session->set_single_chat_friend_id(respondent_id);
        session->set_chat_session_id(response.new_session_id());
        session->set_chat_session_name(respondent.nickname());
        session->set_avatar(respondent.avatar());
        notifications_->send(request.apply_user_id(), notification);
    }
    if (request.agree() && has_applicant) {
        ahwei_im::NotifyMessage notification;
        notification.set_notify_type(ahwei_im::CHAT_SESSION_CREATE_NOTIFY);
        auto* session = notification.mutable_new_chat_session_info()
                            ->mutable_chat_session_info();
        session->set_single_chat_friend_id(request.apply_user_id());
        session->set_chat_session_id(response.new_session_id());
        session->set_chat_session_name(applicant.nickname());
        session->set_avatar(applicant.avatar());
        notifications_->send(respondent_id, notification);
    }
    if (!has_applicant || !has_respondent) {
        spdlog::warn(
            "friend process succeeded but some notifications were skipped: "
            "applicant={}, respondent={}",
            applicant_error,
            respondent_error);
    }
    return serialized(response);
}

HttpResponse GatewayCore::friend_remove(const std::string& body) {
    ahwei_im::FriendRemoveReq request;
    if (!request.ParseFromString(body)) {
        return parse_error<ahwei_im::FriendRemoveReq, ahwei_im::FriendRemoveRsp>(
            request, "cannot parse protobuf request body");
    }
    std::string actor_id;
    std::string error;
    if (!resolve_session(
            request.request_id(),
            request.session_id(),
            actor_id,
            error)) {
        return parse_error<ahwei_im::FriendRemoveReq, ahwei_im::FriendRemoveRsp>(
            request, error);
    }
    request.set_user_id(actor_id);

    ahwei_im::FriendRemoveRsp response;
    if (!rpc_client_->call(
            DownstreamService::FRIEND,
            method(ahwei_im::FriendService::descriptor(), "FriendRemove"),
            request,
            response,
            error)) {
        response.set_request_id(request.request_id());
        response.set_success(false);
        response.set_errmsg("downstream RPC failed: " + error);
        return serialized(response);
    }
    if (response.success()) {
        ahwei_im::NotifyMessage notification;
        notification.set_notify_type(ahwei_im::FRIEND_REMOVE_NOTIFY);
        notification.mutable_friend_remove()->set_user_id(actor_id);
        notifications_->send(request.peer_id(), notification);
    }
    return serialized(response);
}

HttpResponse GatewayCore::chat_session_create(const std::string& body) {
    ahwei_im::ChatSessionCreateReq request;
    if (!request.ParseFromString(body)) {
        return parse_error<
            ahwei_im::ChatSessionCreateReq,
            ahwei_im::ChatSessionCreateRsp>(
            request, "cannot parse protobuf request body");
    }
    std::string actor_id;
    std::string error;
    if (!resolve_session(
            request.request_id(),
            request.session_id(),
            actor_id,
            error)) {
        return parse_error<
            ahwei_im::ChatSessionCreateReq,
            ahwei_im::ChatSessionCreateRsp>(request, error);
    }
    request.set_user_id(actor_id);

    ahwei_im::ChatSessionCreateRsp response;
    if (!rpc_client_->call(
            DownstreamService::FRIEND,
            method(ahwei_im::FriendService::descriptor(), "ChatSessionCreate"),
            request,
            response,
            error)) {
        response.set_request_id(request.request_id());
        response.set_success(false);
        response.set_errmsg("downstream RPC failed: " + error);
        return serialized(response);
    }
    if (response.success() && response.has_chat_session_info()) {
        ahwei_im::NotifyMessage notification;
        notification.set_notify_type(ahwei_im::CHAT_SESSION_CREATE_NOTIFY);
        notification.mutable_new_chat_session_info()
            ->mutable_chat_session_info()
            ->CopyFrom(response.chat_session_info());
        std::unordered_set<std::string> notified;
        for (const auto& member_id : request.member_id_list()) {
            if (!member_id.empty() && notified.insert(member_id).second) {
                notifications_->send(member_id, notification);
            }
        }
    }
    // 与 v1 Qt 客户端约定一致：新会话通过 WebSocket 通知，不放在 HTTP 响应中。
    response.clear_chat_session_info();
    return serialized(response);
}

HttpResponse GatewayCore::new_message(const std::string& body) {
    ahwei_im::NewMessageReq request;
    if (!request.ParseFromString(body)) {
        return parse_error<ahwei_im::NewMessageReq, ahwei_im::NewMessageRsp>(
            request, "cannot parse protobuf request body");
    }
    std::string actor_id;
    std::string error;
    if (!resolve_session(
            request.request_id(),
            request.session_id(),
            actor_id,
            error)) {
        return parse_error<ahwei_im::NewMessageReq, ahwei_im::NewMessageRsp>(
            request, error);
    }
    request.set_user_id(actor_id);

    ahwei_im::GetTransmitTargetRsp target_response;
    ahwei_im::NewMessageRsp response;
    if (!rpc_client_->call(
            DownstreamService::TRANSMIT,
            method(
                ahwei_im::MsgTransmitService::descriptor(),
                "GetTransmitTarget"),
            request,
            target_response,
            error)) {
        response.set_request_id(request.request_id());
        response.set_success(false);
        response.set_errmsg("downstream RPC failed: " + error);
        return serialized(response);
    }

    response.set_request_id(request.request_id());
    response.set_success(target_response.success());
    response.set_errmsg(target_response.errmsg());
    if (target_response.success() && target_response.has_message()) {
        ahwei_im::NotifyMessage notification;
        notification.set_notify_type(ahwei_im::CHAT_MESSAGE_NOTIFY);
        notification.mutable_new_message_info()
            ->mutable_message_info()
            ->CopyFrom(target_response.message());
        std::unordered_set<std::string> notified;
        for (const auto& target_id : target_response.target_id_list()) {
            if (target_id != actor_id && !target_id.empty() &&
                notified.insert(target_id).second) {
                notifications_->send(target_id, notification);
            }
        }
    }
    return serialized(response);
}

}  // namespace chat::gateway
