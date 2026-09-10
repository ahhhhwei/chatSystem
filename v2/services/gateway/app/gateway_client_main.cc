#include "file.pb.h"
#include "friend.pb.h"
#include "gateway/websocket_client.hpp"
#include "httplib.h"
#include "message.pb.h"
#include "transmit.pb.h"
#include "user.pb.h"

#include <gflags/gflags.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <unistd.h>

DEFINE_string(gateway_host, "127.0.0.1", "Gateway HTTP host");
DEFINE_int32(gateway_port, 9000, "Gateway HTTP port");
DEFINE_int32(gateway_websocket_port, 9001, "Gateway WebSocket port");
DEFINE_string(operation, "smoke", "Client operation: smoke");

namespace {

constexpr const char* kContentType = "application/x-protbuf";

template <typename Request, typename Response>
Response post(
    httplib::Client& client,
    const std::string& path,
    const Request& request) {
    const auto result = client.Post(
        path, request.SerializeAsString(), kContentType);
    if (!result) {
        throw std::runtime_error("HTTP request failed: " + path);
    }
    if (result->status != 200) {
        throw std::runtime_error(
            "Gateway returned HTTP " + std::to_string(result->status) +
            " for " + path);
    }
    Response response;
    if (!response.ParseFromString(result->body)) {
        throw std::runtime_error(
            "cannot parse Gateway response: " + path);
    }
    if (!response.success()) {
        throw std::runtime_error(path + ": " + response.errmsg());
    }
    return response;
}

std::string unique_suffix() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto micros =
        std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    return std::to_string(micros % 1000000) + "-" +
        std::to_string(::getpid() % 100);
}

ahwei_im::UserLoginRsp register_and_login(
    httplib::Client& client,
    const std::string& request_prefix,
    const std::string& nickname) {
    ahwei_im::UserRegisterReq register_request;
    register_request.set_request_id(request_prefix + "-register");
    register_request.set_nickname(nickname);
    register_request.set_password("GwSmoke123");
    post<ahwei_im::UserRegisterReq, ahwei_im::UserRegisterRsp>(
        client,
        "/service/user/username_register",
        register_request);

    ahwei_im::UserLoginReq login_request;
    login_request.set_request_id(request_prefix + "-login");
    login_request.set_nickname(nickname);
    login_request.set_password("GwSmoke123");
    return post<ahwei_im::UserLoginReq, ahwei_im::UserLoginRsp>(
        client, "/service/user/username_login", login_request);
}

ahwei_im::UserInfo get_current_user(
    httplib::Client& client,
    const std::string& request_id,
    const std::string& session_id) {
    ahwei_im::GetUserInfoReq request;
    request.set_request_id(request_id);
    request.set_session_id(session_id);
    const auto response = post<
        ahwei_im::GetUserInfoReq,
        ahwei_im::GetUserInfoRsp>(
        client, "/service/user/get_user_info", request);
    return response.user_info();
}

void connect_websocket(
    chat::gateway::WebSocketClient& client,
    const std::string& request_id,
    const std::string& session_id) {
    std::string error;
    if (!client.connect(
            FLAGS_gateway_host,
            static_cast<std::uint16_t>(FLAGS_gateway_websocket_port),
            error)) {
        throw std::runtime_error("WebSocket connect failed: " + error);
    }
    ahwei_im::ClientAuthenticationReq authentication;
    authentication.set_request_id(request_id);
    authentication.set_session_id(session_id);
    if (!client.authenticate(authentication, error)) {
        throw std::runtime_error("WebSocket authentication failed: " + error);
    }
}

ahwei_im::NotifyMessage receive_notification(
    chat::gateway::WebSocketClient& client,
    ahwei_im::NotifyType expected_type) {
    ahwei_im::NotifyMessage notification;
    std::string error;
    if (!client.receive_notification(notification, 3000, error)) {
        throw std::runtime_error("WebSocket notification failed: " + error);
    }
    if (notification.notify_type() != expected_type) {
        throw std::runtime_error("received an unexpected WebSocket event");
    }
    return notification;
}

void run_smoke(httplib::Client& client) {
    const auto suffix = unique_suffix();
    const auto alice_login = register_and_login(
        client, "alice-" + suffix, "alice-" + suffix);
    const auto bob_login = register_and_login(
        client, "bob-" + suffix, "bob-" + suffix);
    const auto alice = get_current_user(
        client,
        "alice-info-" + suffix,
        alice_login.login_session_id());
    const auto bob = get_current_user(
        client,
        "bob-info-" + suffix,
        bob_login.login_session_id());

    chat::gateway::WebSocketClient alice_websocket;
    chat::gateway::WebSocketClient bob_websocket;
    connect_websocket(
        alice_websocket,
        "alice-ws-" + suffix,
        alice_login.login_session_id());
    connect_websocket(
        bob_websocket,
        "bob-ws-" + suffix,
        bob_login.login_session_id());

    ahwei_im::FriendAddReq add_request;
    add_request.set_request_id("friend-add-" + suffix);
    add_request.set_session_id(alice_login.login_session_id());
    add_request.set_respondent_id(bob.user_id());
    const auto add_response = post<
        ahwei_im::FriendAddReq,
        ahwei_im::FriendAddRsp>(
        client, "/service/friend/add_friend_apply", add_request);
    const auto apply_notification = receive_notification(
        bob_websocket, ahwei_im::FRIEND_ADD_APPLY_NOTIFY);
    if (apply_notification.notify_event_id() != add_response.notify_event_id() ||
        apply_notification.friend_add_apply().user_info().user_id() !=
            alice.user_id()) {
        throw std::runtime_error("friend application notification is invalid");
    }

    ahwei_im::FriendAddProcessReq process_request;
    process_request.set_request_id("friend-process-" + suffix);
    process_request.set_session_id(bob_login.login_session_id());
    process_request.set_notify_event_id(add_response.notify_event_id());
    process_request.set_apply_user_id(alice.user_id());
    process_request.set_agree(true);
    post<ahwei_im::FriendAddProcessReq, ahwei_im::FriendAddProcessRsp>(
        client, "/service/friend/add_friend_process", process_request);
    const auto process_notification = receive_notification(
        alice_websocket, ahwei_im::FRIEND_ADD_PROCESS_NOTIFY);
    if (!process_notification.friend_process_result().agree() ||
        process_notification.friend_process_result().user_info().user_id() !=
            bob.user_id()) {
        throw std::runtime_error("friend process notification is invalid");
    }
    const auto alice_single_session = receive_notification(
        alice_websocket, ahwei_im::CHAT_SESSION_CREATE_NOTIFY);
    const auto bob_single_session = receive_notification(
        bob_websocket, ahwei_im::CHAT_SESSION_CREATE_NOTIFY);
    if (alice_single_session.new_chat_session_info()
            .chat_session_info()
            .single_chat_friend_id() != bob.user_id() ||
        bob_single_session.new_chat_session_info()
            .chat_session_info()
            .single_chat_friend_id() != alice.user_id()) {
        throw std::runtime_error("single chat notifications are invalid");
    }

    ahwei_im::GetFriendListReq friend_list_request;
    friend_list_request.set_request_id("friend-list-" + suffix);
    friend_list_request.set_session_id(alice_login.login_session_id());
    const auto friend_list = post<
        ahwei_im::GetFriendListReq,
        ahwei_im::GetFriendListRsp>(
        client, "/service/friend/get_friend_list", friend_list_request);
    if (friend_list.friend_list_size() != 1 ||
        friend_list.friend_list(0).user_id() != bob.user_id()) {
        throw std::runtime_error("friend flow returned an unexpected list");
    }

    const std::string group_name = "gateway-smoke-" + suffix;
    ahwei_im::ChatSessionCreateReq create_request;
    create_request.set_request_id("group-create-" + suffix);
    create_request.set_session_id(alice_login.login_session_id());
    create_request.set_chat_session_name(group_name);
    create_request.add_member_id_list(alice.user_id());
    create_request.add_member_id_list(bob.user_id());
    post<ahwei_im::ChatSessionCreateReq, ahwei_im::ChatSessionCreateRsp>(
        client, "/service/friend/create_chat_session", create_request);
    const auto alice_group_notification = receive_notification(
        alice_websocket, ahwei_im::CHAT_SESSION_CREATE_NOTIFY);
    const auto bob_group_notification = receive_notification(
        bob_websocket, ahwei_im::CHAT_SESSION_CREATE_NOTIFY);
    if (alice_group_notification.new_chat_session_info()
            .chat_session_info()
            .chat_session_name() != group_name ||
        bob_group_notification.new_chat_session_info()
            .chat_session_info()
            .chat_session_name() != group_name) {
        throw std::runtime_error("group chat notifications are invalid");
    }

    ahwei_im::GetChatSessionListReq sessions_request;
    sessions_request.set_request_id("session-list-" + suffix);
    sessions_request.set_session_id(alice_login.login_session_id());
    const auto sessions = post<
        ahwei_im::GetChatSessionListReq,
        ahwei_im::GetChatSessionListRsp>(
        client, "/service/friend/get_chat_session_list", sessions_request);
    std::string group_id;
    for (const auto& session : sessions.chat_session_info_list()) {
        if (session.chat_session_name() == group_name) {
            group_id = session.chat_session_id();
            break;
        }
    }
    if (group_id.empty()) {
        throw std::runtime_error("created group is missing from session list");
    }

    const std::string message_text = "hello through GatewayServer";
    ahwei_im::NewMessageReq message_request;
    message_request.set_request_id("message-send-" + suffix);
    message_request.set_session_id(alice_login.login_session_id());
    message_request.set_chat_session_id(group_id);
    message_request.mutable_message()->set_message_type(ahwei_im::STRING);
    message_request.mutable_message()
        ->mutable_string_message()
        ->set_content(message_text);
    post<ahwei_im::NewMessageReq, ahwei_im::NewMessageRsp>(
        client, "/service/message_transmit/new_message", message_request);
    const auto message_notification = receive_notification(
        bob_websocket, ahwei_im::CHAT_MESSAGE_NOTIFY);
    if (message_notification.new_message_info()
            .message_info()
            .message()
            .string_message()
            .content() != message_text) {
        throw std::runtime_error("chat message notification is invalid");
    }

    ahwei_im::GetRecentMsgReq recent_request;
    recent_request.set_request_id("message-recent-" + suffix);
    recent_request.set_session_id(bob_login.login_session_id());
    recent_request.set_chat_session_id(group_id);
    recent_request.set_msg_count(10);
    const auto recent = post<
        ahwei_im::GetRecentMsgReq,
        ahwei_im::GetRecentMsgRsp>(
        client, "/service/message_storage/get_recent", recent_request);
    if (recent.msg_list_size() != 1 ||
        recent.msg_list(0).message().string_message().content() != message_text) {
        throw std::runtime_error("message was not stored through GatewayServer");
    }

    const std::string file_content("\0Gateway\xff" "File\n", 14);
    ahwei_im::PutSingleFileReq put_request;
    put_request.set_request_id("file-put-" + suffix);
    put_request.set_session_id(alice_login.login_session_id());
    put_request.mutable_file_data()->set_file_name("gateway-smoke.bin");
    put_request.mutable_file_data()->set_file_size(file_content.size());
    put_request.mutable_file_data()->set_file_content(file_content);
    const auto put_response = post<
        ahwei_im::PutSingleFileReq,
        ahwei_im::PutSingleFileRsp>(
        client, "/service/file/put_single_file", put_request);

    ahwei_im::GetSingleFileReq get_request;
    get_request.set_request_id("file-get-" + suffix);
    get_request.set_session_id(bob_login.login_session_id());
    get_request.set_file_id(put_response.file_info().file_id());
    const auto get_response = post<
        ahwei_im::GetSingleFileReq,
        ahwei_im::GetSingleFileRsp>(
        client, "/service/file/get_single_file", get_request);
    if (get_response.file_data().file_content() != file_content) {
        throw std::runtime_error("binary file changed during Gateway transfer");
    }

    spdlog::info(
        "Gateway HTTP/WebSocket smoke passed: alice={}, bob={}, group={}, "
        "message={}, file={}",
        alice.user_id(),
        bob.user_id(),
        group_id,
        recent.msg_list(0).message_id(),
        put_response.file_info().file_id());
}

}  // namespace

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);
    try {
        httplib::Client client(FLAGS_gateway_host, FLAGS_gateway_port);
        client.set_connection_timeout(3, 0);
        client.set_read_timeout(30, 0);
        client.set_write_timeout(30, 0);
        if (FLAGS_operation == "smoke") {
            run_smoke(client);
            return 0;
        }
        spdlog::error("unsupported operation: {}", FLAGS_operation);
        return 2;
    } catch (const std::exception& exception) {
        spdlog::error("Gateway client failed: {}", exception.what());
        return 1;
    }
}
