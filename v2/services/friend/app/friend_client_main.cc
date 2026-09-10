#include "friend.pb.h"

#include <brpc/channel.h>
#include <brpc/controller.h>
#include <gflags/gflags.h>

#include <chrono>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

DEFINE_string(server, "127.0.0.1:10006", "FriendServer address");
DEFINE_int32(timeout_ms, 5000, "RPC timeout");
DEFINE_string(operation, "friends", "friends/add/process/pending/search/remove/sessions/create/members/member_ids");
DEFINE_string(user_id, "", "Current user id");
DEFINE_string(session_id, "", "Login session id");
DEFINE_string(peer_id, "", "Peer/respondent/applicant user id");
DEFINE_string(event_id, "", "Friend application event id");
DEFINE_bool(agree, true, "Whether to accept a friend application");
DEFINE_string(search_key, "", "User search key");
DEFINE_string(chat_session_id, "", "Chat session id");
DEFINE_string(chat_session_name, "", "New group chat name");
DEFINE_string(members, "", "Comma-separated group member ids");

namespace {

std::string make_request_id() {
    return "friend-client-" + std::to_string(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
}

std::vector<std::string> split_members(const std::string& input) {
    std::vector<std::string> result;
    std::istringstream stream(input);
    std::string value;
    while (std::getline(stream, value, ',')) {
        if (!value.empty()) result.push_back(value);
    }
    return result;
}

template <typename Response>
bool print_status(const Response& response) {
    if (!response.success()) {
        std::cerr << "request failed: " << response.errmsg() << '\n';
        return false;
    }
    std::cout << "success\n";
    return true;
}

void print_user(const ahwei_im::UserInfo& user) {
    std::cout << "user_id=" << user.user_id()
              << ", nickname=" << user.nickname()
              << ", phone=" << user.phone()
              << ", avatar_bytes=" << user.avatar().size() << '\n';
}

}  // namespace

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = "baidu_std";
    options.timeout_ms = FLAGS_timeout_ms;
    options.max_retry = 0;
    if (channel.Init(FLAGS_server.c_str(), &options) != 0) {
        std::cerr << "cannot connect to FriendServer\n";
        return 1;
    }
    ahwei_im::FriendService_Stub stub(&channel);
    brpc::Controller controller;
    const std::string request_id = make_request_id();

    if (FLAGS_operation == "friends") {
        ahwei_im::GetFriendListReq request;
        ahwei_im::GetFriendListRsp response;
        request.set_request_id(request_id);
        request.set_user_id(FLAGS_user_id);
        request.set_session_id(FLAGS_session_id);
        stub.GetFriendList(&controller, &request, &response, nullptr);
        if (!controller.Failed() && print_status(response)) {
            for (const auto& user : response.friend_list()) print_user(user);
            return 0;
        }
    } else if (FLAGS_operation == "add") {
        ahwei_im::FriendAddReq request;
        ahwei_im::FriendAddRsp response;
        request.set_request_id(request_id);
        request.set_user_id(FLAGS_user_id);
        request.set_session_id(FLAGS_session_id);
        request.set_respondent_id(FLAGS_peer_id);
        stub.FriendAdd(&controller, &request, &response, nullptr);
        if (!controller.Failed() && print_status(response)) {
            std::cout << "event_id=" << response.notify_event_id() << '\n';
            return 0;
        }
    } else if (FLAGS_operation == "process") {
        ahwei_im::FriendAddProcessReq request;
        ahwei_im::FriendAddProcessRsp response;
        request.set_request_id(request_id);
        request.set_user_id(FLAGS_user_id);
        request.set_session_id(FLAGS_session_id);
        request.set_notify_event_id(FLAGS_event_id);
        request.set_apply_user_id(FLAGS_peer_id);
        request.set_agree(FLAGS_agree);
        stub.FriendAddProcess(&controller, &request, &response, nullptr);
        if (!controller.Failed() && print_status(response)) {
            if (!response.new_session_id().empty()) {
                std::cout << "chat_session_id="
                          << response.new_session_id() << '\n';
            }
            return 0;
        }
    } else if (FLAGS_operation == "pending") {
        ahwei_im::GetPendingFriendEventListReq request;
        ahwei_im::GetPendingFriendEventListRsp response;
        request.set_request_id(request_id);
        request.set_user_id(FLAGS_user_id);
        request.set_session_id(FLAGS_session_id);
        stub.GetPendingFriendEventList(
            &controller, &request, &response, nullptr);
        if (!controller.Failed() && print_status(response)) {
            for (const auto& event : response.event()) {
                std::cout << "event_id=" << event.event_id() << ", ";
                print_user(event.sender());
            }
            return 0;
        }
    } else if (FLAGS_operation == "search") {
        ahwei_im::FriendSearchReq request;
        ahwei_im::FriendSearchRsp response;
        request.set_request_id(request_id);
        request.set_user_id(FLAGS_user_id);
        request.set_session_id(FLAGS_session_id);
        request.set_search_key(FLAGS_search_key);
        stub.FriendSearch(&controller, &request, &response, nullptr);
        if (!controller.Failed() && print_status(response)) {
            for (const auto& user : response.user_info()) print_user(user);
            return 0;
        }
    } else if (FLAGS_operation == "remove") {
        ahwei_im::FriendRemoveReq request;
        ahwei_im::FriendRemoveRsp response;
        request.set_request_id(request_id);
        request.set_user_id(FLAGS_user_id);
        request.set_session_id(FLAGS_session_id);
        request.set_peer_id(FLAGS_peer_id);
        stub.FriendRemove(&controller, &request, &response, nullptr);
        if (!controller.Failed() && print_status(response)) return 0;
    } else if (FLAGS_operation == "sessions") {
        ahwei_im::GetChatSessionListReq request;
        ahwei_im::GetChatSessionListRsp response;
        request.set_request_id(request_id);
        request.set_user_id(FLAGS_user_id);
        request.set_session_id(FLAGS_session_id);
        stub.GetChatSessionList(&controller, &request, &response, nullptr);
        if (!controller.Failed() && print_status(response)) {
            for (const auto& session : response.chat_session_info_list()) {
                std::cout << "chat_session_id=" << session.chat_session_id()
                          << ", name=" << session.chat_session_name()
                          << ", peer_id=" << session.single_chat_friend_id()
                          << ", has_previous_message="
                          << session.has_prev_message() << '\n';
            }
            return 0;
        }
    } else if (FLAGS_operation == "create") {
        ahwei_im::ChatSessionCreateReq request;
        ahwei_im::ChatSessionCreateRsp response;
        request.set_request_id(request_id);
        request.set_user_id(FLAGS_user_id);
        request.set_session_id(FLAGS_session_id);
        request.set_chat_session_name(FLAGS_chat_session_name);
        for (const auto& member : split_members(FLAGS_members)) {
            request.add_member_id_list(member);
        }
        stub.ChatSessionCreate(&controller, &request, &response, nullptr);
        if (!controller.Failed() && print_status(response)) {
            std::cout << "chat_session_id="
                      << response.chat_session_info().chat_session_id() << '\n';
            return 0;
        }
    } else if (FLAGS_operation == "members") {
        ahwei_im::GetChatSessionMemberReq request;
        ahwei_im::GetChatSessionMemberRsp response;
        request.set_request_id(request_id);
        request.set_user_id(FLAGS_user_id);
        request.set_session_id(FLAGS_session_id);
        request.set_chat_session_id(FLAGS_chat_session_id);
        stub.GetChatSessionMember(&controller, &request, &response, nullptr);
        if (!controller.Failed() && print_status(response)) {
            for (const auto& user : response.member_info_list()) print_user(user);
            return 0;
        }
    } else if (FLAGS_operation == "member_ids") {
        ahwei_im::GetChatSessionMemberIdsReq request;
        ahwei_im::GetChatSessionMemberIdsRsp response;
        request.set_request_id(request_id);
        request.set_chat_session_id(FLAGS_chat_session_id);
        stub.GetChatSessionMemberIds(&controller, &request, &response, nullptr);
        if (!controller.Failed() && print_status(response)) {
            for (const auto& member : response.member_id_list()) {
                std::cout << "member_id=" << member << '\n';
            }
            return 0;
        }
    } else {
        std::cerr << "unsupported operation: " << FLAGS_operation << '\n';
        return 1;
    }

    if (controller.Failed()) {
        std::cerr << "RPC failed: " << controller.ErrorText() << '\n';
    }
    return 1;
}
