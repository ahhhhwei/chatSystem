#include "gateway/gateway_core.hpp"

#include "file.pb.h"
#include "friend.pb.h"
#include "gateway.pb.h"
#include "message.pb.h"
#include "transmit.pb.h"
#include "user.pb.h"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace chat::gateway {
namespace {

class ScriptedRpcClient final : public RpcClient {
public:
    using Handler = std::function<bool(
        DownstreamService,
        const google::protobuf::Message&,
        google::protobuf::Message&,
        std::string&)>;

    void when(std::string method_name, Handler handler) {
        handlers_[std::move(method_name)] = std::move(handler);
    }

    bool call(
        DownstreamService service,
        const google::protobuf::MethodDescriptor& method,
        const google::protobuf::Message& request,
        google::protobuf::Message& response,
        std::string& error) override {
        calls.push_back(method.name());
        const auto iterator = handlers_.find(method.name());
        if (iterator == handlers_.end()) {
            error = "unexpected RPC: " + method.name();
            return false;
        }
        return iterator->second(service, request, response, error);
    }

    std::vector<std::string> calls;

private:
    std::unordered_map<std::string, Handler> handlers_;
};

class RecordingNotifications final : public NotificationSink {
public:
    bool send(
        const std::string& user_id,
        const ahwei_im::NotifyMessage& notification) override {
        sent.emplace_back(user_id, notification);
        return true;
    }

    std::vector<std::pair<std::string, ahwei_im::NotifyMessage>> sent;
};

void resolves_to(
    const std::shared_ptr<ScriptedRpcClient>& rpc,
    const std::string& expected_session,
    const std::string& user_id) {
    rpc->when(
        "ResolveSession",
        [expected_session, user_id](
            DownstreamService service,
            const google::protobuf::Message& request_message,
            google::protobuf::Message& response_message,
            std::string&) {
            EXPECT_EQ(service, DownstreamService::USER);
            const auto& request = dynamic_cast<
                const ahwei_im::ResolveSessionReq&>(request_message);
            auto& response = dynamic_cast<
                ahwei_im::ResolveSessionRsp&>(response_message);
            response.set_request_id(request.request_id());
            if (request.session_id() != expected_session) {
                response.set_success(false);
                response.set_errmsg("session not found");
                return true;
            }
            response.set_success(true);
            response.set_user_id(user_id);
            return true;
        });
}

void serves_users(
    const std::shared_ptr<ScriptedRpcClient>& rpc,
    std::unordered_map<std::string, ahwei_im::UserInfo> users) {
    rpc->when(
        "GetUserInfo",
        [users = std::move(users)](
            DownstreamService,
            const google::protobuf::Message& request_message,
            google::protobuf::Message& response_message,
            std::string&) {
            const auto& request = dynamic_cast<
                const ahwei_im::GetUserInfoReq&>(request_message);
            auto& response = dynamic_cast<
                ahwei_im::GetUserInfoRsp&>(response_message);
            response.set_request_id(request.request_id());
            const auto iterator = users.find(request.user_id());
            if (iterator == users.end()) {
                response.set_success(false);
                response.set_errmsg("user not found");
            } else {
                response.set_success(true);
                response.mutable_user_info()->CopyFrom(iterator->second);
            }
            return true;
        });
}

ahwei_im::UserInfo user(
    const std::string& user_id,
    const std::string& nickname) {
    ahwei_im::UserInfo result;
    result.set_user_id(user_id);
    result.set_nickname(nickname);
    result.set_avatar("avatar-" + user_id);
    return result;
}

TEST(GatewayCoreTest, PublicRouteForwardsWithoutSessionResolution) {
    auto rpc = std::make_shared<ScriptedRpcClient>();
    auto notifications = std::make_shared<RecordingNotifications>();
    rpc->when(
        "UserRegister",
        [](DownstreamService service,
           const google::protobuf::Message& request_message,
           google::protobuf::Message& response_message,
           std::string&) {
            EXPECT_EQ(service, DownstreamService::USER);
            const auto& request =
                dynamic_cast<const ahwei_im::UserRegisterReq&>(request_message);
            EXPECT_EQ(request.nickname(), "alice");
            auto& response =
                dynamic_cast<ahwei_im::UserRegisterRsp&>(response_message);
            response.set_request_id(request.request_id());
            response.set_success(true);
            return true;
        });
    GatewayCore core(rpc, notifications);

    ahwei_im::UserRegisterReq request;
    request.set_request_id("register-1");
    request.set_nickname("alice");
    request.set_password("password");
    const auto http = core.handle_http(
        "/service/user/username_register", request.SerializeAsString());

    ahwei_im::UserRegisterRsp response;
    ASSERT_TRUE(response.ParseFromString(http.body));
    EXPECT_EQ(http.status, 200);
    EXPECT_EQ(http.content_type, kProtobufContentType);
    EXPECT_TRUE(response.success());
    ASSERT_EQ(rpc->calls.size(), 1U);
    EXPECT_EQ(rpc->calls.front(), "UserRegister");
}

TEST(GatewayCoreTest, AuthenticatedRouteOverwritesUntrustedUserId) {
    auto rpc = std::make_shared<ScriptedRpcClient>();
    auto notifications = std::make_shared<RecordingNotifications>();
    resolves_to(rpc, "session-alice", "alice-id");
    rpc->when(
        "GetFriendList",
        [](DownstreamService service,
           const google::protobuf::Message& request_message,
           google::protobuf::Message& response_message,
           std::string&) {
            EXPECT_EQ(service, DownstreamService::FRIEND);
            const auto& request = dynamic_cast<
                const ahwei_im::GetFriendListReq&>(request_message);
            EXPECT_EQ(request.user_id(), "alice-id");
            auto& response = dynamic_cast<
                ahwei_im::GetFriendListRsp&>(response_message);
            response.set_request_id(request.request_id());
            response.set_success(true);
            return true;
        });
    GatewayCore core(rpc, notifications);

    ahwei_im::GetFriendListReq request;
    request.set_request_id("friends-1");
    request.set_session_id("session-alice");
    request.set_user_id("mallory-forged-id");
    const auto http = core.handle_http(
        "/service/friend/get_friend_list", request.SerializeAsString());

    ahwei_im::GetFriendListRsp response;
    ASSERT_TRUE(response.ParseFromString(http.body));
    EXPECT_TRUE(response.success());
    ASSERT_EQ(rpc->calls.size(), 2U);
    EXPECT_EQ(rpc->calls[0], "ResolveSession");
    EXPECT_EQ(rpc->calls[1], "GetFriendList");
}

TEST(GatewayCoreTest, InvalidSessionNeverReachesBusinessService) {
    auto rpc = std::make_shared<ScriptedRpcClient>();
    auto notifications = std::make_shared<RecordingNotifications>();
    resolves_to(rpc, "valid-session", "alice-id");
    GatewayCore core(rpc, notifications);

    ahwei_im::GetRecentMsgReq request;
    request.set_request_id("recent-1");
    request.set_session_id("expired-session");
    const auto http = core.handle_http(
        "/service/message_storage/get_recent", request.SerializeAsString());

    ahwei_im::GetRecentMsgRsp response;
    ASSERT_TRUE(response.ParseFromString(http.body));
    EXPECT_FALSE(response.success());
    EXPECT_EQ(response.errmsg(), "session not found");
    ASSERT_EQ(rpc->calls.size(), 1U);
    EXPECT_EQ(rpc->calls.front(), "ResolveSession");
}

TEST(GatewayCoreTest, FriendApplicationNotifiesRespondentWithEventAndProfile) {
    auto rpc = std::make_shared<ScriptedRpcClient>();
    auto notifications = std::make_shared<RecordingNotifications>();
    resolves_to(rpc, "session-alice", "alice-id");
    serves_users(rpc, {{"alice-id", user("alice-id", "Alice")}});
    rpc->when(
        "FriendAdd",
        [](DownstreamService,
           const google::protobuf::Message& request_message,
           google::protobuf::Message& response_message,
           std::string&) {
            const auto& request =
                dynamic_cast<const ahwei_im::FriendAddReq&>(request_message);
            EXPECT_EQ(request.user_id(), "alice-id");
            auto& response =
                dynamic_cast<ahwei_im::FriendAddRsp&>(response_message);
            response.set_request_id(request.request_id());
            response.set_success(true);
            response.set_notify_event_id("event-1");
            return true;
        });
    GatewayCore core(rpc, notifications);

    ahwei_im::FriendAddReq request;
    request.set_request_id("apply-1");
    request.set_session_id("session-alice");
    request.set_respondent_id("bob-id");
    const auto http = core.handle_http(
        "/service/friend/add_friend_apply", request.SerializeAsString());

    ahwei_im::FriendAddRsp response;
    ASSERT_TRUE(response.ParseFromString(http.body));
    ASSERT_TRUE(response.success());
    ASSERT_EQ(notifications->sent.size(), 1U);
    EXPECT_EQ(notifications->sent[0].first, "bob-id");
    const auto& notification = notifications->sent[0].second;
    EXPECT_EQ(notification.notify_type(), ahwei_im::FRIEND_ADD_APPLY_NOTIFY);
    EXPECT_EQ(notification.notify_event_id(), "event-1");
    EXPECT_EQ(notification.friend_add_apply().user_info().user_id(), "alice-id");
}

TEST(GatewayCoreTest, AcceptedFriendApplicationNotifiesBothSides) {
    auto rpc = std::make_shared<ScriptedRpcClient>();
    auto notifications = std::make_shared<RecordingNotifications>();
    resolves_to(rpc, "session-bob", "bob-id");
    serves_users(
        rpc,
        {{"alice-id", user("alice-id", "Alice")},
         {"bob-id", user("bob-id", "Bob")}});
    rpc->when(
        "FriendAddProcess",
        [](DownstreamService,
           const google::protobuf::Message& request_message,
           google::protobuf::Message& response_message,
           std::string&) {
            const auto& request = dynamic_cast<
                const ahwei_im::FriendAddProcessReq&>(request_message);
            EXPECT_EQ(request.user_id(), "bob-id");
            auto& response = dynamic_cast<
                ahwei_im::FriendAddProcessRsp&>(response_message);
            response.set_request_id(request.request_id());
            response.set_success(true);
            response.set_new_session_id("chat-1");
            return true;
        });
    GatewayCore core(rpc, notifications);

    ahwei_im::FriendAddProcessReq request;
    request.set_request_id("process-1");
    request.set_session_id("session-bob");
    request.set_notify_event_id("event-1");
    request.set_apply_user_id("alice-id");
    request.set_agree(true);
    const auto http = core.handle_http(
        "/service/friend/add_friend_process", request.SerializeAsString());

    ahwei_im::FriendAddProcessRsp response;
    ASSERT_TRUE(response.ParseFromString(http.body));
    ASSERT_TRUE(response.success());
    ASSERT_EQ(notifications->sent.size(), 3U);
    EXPECT_EQ(notifications->sent[0].first, "alice-id");
    EXPECT_EQ(
        notifications->sent[0].second.notify_type(),
        ahwei_im::FRIEND_ADD_PROCESS_NOTIFY);
    EXPECT_EQ(notifications->sent[0].second.notify_event_id(), "event-1");
    EXPECT_EQ(notifications->sent[1].first, "alice-id");
    EXPECT_EQ(
        notifications->sent[1]
            .second.new_chat_session_info()
            .chat_session_info()
            .single_chat_friend_id(),
        "bob-id");
    EXPECT_EQ(notifications->sent[2].first, "bob-id");
    EXPECT_EQ(
        notifications->sent[2]
            .second.new_chat_session_info()
            .chat_session_info()
            .single_chat_friend_id(),
        "alice-id");
}

TEST(GatewayCoreTest, GroupCreationPushesOncePerMemberAndHidesHttpSession) {
    auto rpc = std::make_shared<ScriptedRpcClient>();
    auto notifications = std::make_shared<RecordingNotifications>();
    resolves_to(rpc, "session-alice", "alice-id");
    rpc->when(
        "ChatSessionCreate",
        [](DownstreamService,
           const google::protobuf::Message& request_message,
           google::protobuf::Message& response_message,
           std::string&) {
            const auto& request = dynamic_cast<
                const ahwei_im::ChatSessionCreateReq&>(request_message);
            auto& response = dynamic_cast<
                ahwei_im::ChatSessionCreateRsp&>(response_message);
            response.set_request_id(request.request_id());
            response.set_success(true);
            response.mutable_chat_session_info()->set_chat_session_id("group-1");
            response.mutable_chat_session_info()->set_chat_session_name("Team");
            return true;
        });
    GatewayCore core(rpc, notifications);

    ahwei_im::ChatSessionCreateReq request;
    request.set_request_id("group-create-1");
    request.set_session_id("session-alice");
    request.set_chat_session_name("Team");
    request.add_member_id_list("alice-id");
    request.add_member_id_list("bob-id");
    request.add_member_id_list("bob-id");
    const auto http = core.handle_http(
        "/service/friend/create_chat_session", request.SerializeAsString());

    ahwei_im::ChatSessionCreateRsp response;
    ASSERT_TRUE(response.ParseFromString(http.body));
    EXPECT_TRUE(response.success());
    EXPECT_FALSE(response.has_chat_session_info());
    ASSERT_EQ(notifications->sent.size(), 2U);
    EXPECT_EQ(notifications->sent[0].second.notify_type(),
              ahwei_im::CHAT_SESSION_CREATE_NOTIFY);
    EXPECT_EQ(notifications->sent[0]
                  .second.new_chat_session_info()
                  .chat_session_info()
                  .chat_session_id(),
              "group-1");
}

TEST(GatewayCoreTest, NewMessageNotifiesOtherMembersOnce) {
    auto rpc = std::make_shared<ScriptedRpcClient>();
    auto notifications = std::make_shared<RecordingNotifications>();
    resolves_to(rpc, "session-alice", "alice-id");
    rpc->when(
        "GetTransmitTarget",
        [](DownstreamService service,
           const google::protobuf::Message& request_message,
           google::protobuf::Message& response_message,
           std::string&) {
            EXPECT_EQ(service, DownstreamService::TRANSMIT);
            const auto& request = dynamic_cast<
                const ahwei_im::NewMessageReq&>(request_message);
            EXPECT_EQ(request.user_id(), "alice-id");
            auto& response = dynamic_cast<
                ahwei_im::GetTransmitTargetRsp&>(response_message);
            response.set_request_id(request.request_id());
            response.set_success(true);
            response.mutable_message()->set_message_id("message-1");
            response.mutable_message()->set_chat_session_id("chat-1");
            response.add_target_id_list("alice-id");
            response.add_target_id_list("bob-id");
            response.add_target_id_list("bob-id");
            return true;
        });
    GatewayCore core(rpc, notifications);

    ahwei_im::NewMessageReq request;
    request.set_request_id("send-1");
    request.set_session_id("session-alice");
    request.set_chat_session_id("chat-1");
    request.mutable_message()->set_message_type(ahwei_im::STRING);
    request.mutable_message()->mutable_string_message()->set_content("hello");
    const auto http = core.handle_http(
        "/service/message_transmit/new_message", request.SerializeAsString());

    ahwei_im::NewMessageRsp response;
    ASSERT_TRUE(response.ParseFromString(http.body));
    EXPECT_TRUE(response.success());
    ASSERT_EQ(notifications->sent.size(), 1U);
    EXPECT_EQ(notifications->sent[0].first, "bob-id");
    EXPECT_EQ(notifications->sent[0].second.notify_type(),
              ahwei_im::CHAT_MESSAGE_NOTIFY);
    EXPECT_EQ(notifications->sent[0]
                  .second.new_message_info()
                  .message_info()
                  .message_id(),
              "message-1");
}

TEST(GatewayCoreTest, WebSocketAuthenticationUsesSameSessionResolver) {
    auto rpc = std::make_shared<ScriptedRpcClient>();
    auto notifications = std::make_shared<RecordingNotifications>();
    resolves_to(rpc, "session-alice", "alice-id");
    GatewayCore core(rpc, notifications);
    ahwei_im::ClientAuthenticationReq request;
    request.set_request_id("ws-1");
    request.set_session_id("session-alice");

    std::string user_id;
    std::string session_id;
    std::string error;
    ASSERT_TRUE(core.authenticate_websocket(
        request.SerializeAsString(), user_id, session_id, error));
    EXPECT_EQ(user_id, "alice-id");
    EXPECT_EQ(session_id, "session-alice");
}

}  // namespace
}  // namespace chat::gateway
