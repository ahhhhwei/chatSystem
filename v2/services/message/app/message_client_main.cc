#include "message.pb.h"

#include <brpc/channel.h>
#include <brpc/controller.h>
#include <gflags/gflags.h>

#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

DEFINE_string(server, "127.0.0.1:10005", "MessageStoreServer address");
DEFINE_int32(timeout_ms, 3000, "RPC timeout in milliseconds");
DEFINE_string(
    operation,
    "recent",
    "Operation: recent, history, or search");
DEFINE_string(chat_session_id, "demo-session", "Chat session to query");
DEFINE_int64(msg_count, 20, "Number of recent messages");
DEFINE_int64(cur_time, 0, "Only return recent messages at or before this time");
DEFINE_int64(start_time, 0, "History range start time");
DEFINE_int64(
    over_time,
    std::numeric_limits<std::int64_t>::max(),
    "History range end time");
DEFINE_string(search_key, "", "Text message search key");

namespace {

void print_message(const ahwei_im::MessageInfo& message) {
    std::cout << "message_id=" << message.message_id()
              << " session=" << message.chat_session_id()
              << " timestamp=" << message.timestamp()
              << " sender=" << message.sender().user_id();
    switch (message.message().message_type()) {
        case ahwei_im::STRING:
            std::cout << " type=text content="
                      << message.message().string_message().content();
            break;
        case ahwei_im::IMAGE:
            std::cout << " type=image file_id="
                      << message.message().image_message().file_id()
                      << " bytes="
                      << message.message().image_message().image_content().size();
            break;
        case ahwei_im::FILE:
            std::cout << " type=file file_id="
                      << message.message().file_message().file_id()
                      << " name="
                      << message.message().file_message().file_name()
                      << " bytes="
                      << message.message().file_message().file_contents().size();
            break;
        case ahwei_im::SPEECH:
            std::cout << " type=speech file_id="
                      << message.message().speech_message().file_id()
                      << " bytes="
                      << message.message().speech_message().file_contents().size();
            break;
        default:
            std::cout << " type=unknown";
            break;
    }
    std::cout << '\n';
}

template <typename Response>
int print_response(
    const brpc::Controller& controller,
    const Response& response) {
    if (controller.Failed()) {
        std::cerr << "rpc failed: " << controller.ErrorText() << '\n';
        return 1;
    }
    if (!response.success()) {
        std::cerr << "query failed: " << response.errmsg() << '\n';
        return 1;
    }
    std::cout << "messages=" << response.msg_list_size() << '\n';
    for (const auto& message : response.msg_list()) {
        print_message(message);
    }
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = "baidu_std";
    options.timeout_ms = FLAGS_timeout_ms;
    options.max_retry = 3;
    if (channel.Init(FLAGS_server.c_str(), &options) != 0) {
        std::cerr << "cannot connect to " << FLAGS_server << '\n';
        return 1;
    }

    ahwei_im::MsgStorageService_Stub stub(&channel);
    brpc::Controller controller;
    if (FLAGS_operation == "recent") {
        ahwei_im::GetRecentMsgReq request;
        request.set_request_id("message-client-recent");
        request.set_chat_session_id(FLAGS_chat_session_id);
        request.set_msg_count(FLAGS_msg_count);
        request.set_cur_time(FLAGS_cur_time);
        ahwei_im::GetRecentMsgRsp response;
        stub.GetRecentMsg(&controller, &request, &response, nullptr);
        return print_response(controller, response);
    }
    if (FLAGS_operation == "history") {
        ahwei_im::GetHistoryMsgReq request;
        request.set_request_id("message-client-history");
        request.set_chat_session_id(FLAGS_chat_session_id);
        request.set_start_time(FLAGS_start_time);
        request.set_over_time(FLAGS_over_time);
        ahwei_im::GetHistoryMsgRsp response;
        stub.GetHistoryMsg(&controller, &request, &response, nullptr);
        return print_response(controller, response);
    }
    if (FLAGS_operation == "search") {
        ahwei_im::MsgSearchReq request;
        request.set_request_id("message-client-search");
        request.set_chat_session_id(FLAGS_chat_session_id);
        request.set_search_key(FLAGS_search_key);
        ahwei_im::MsgSearchRsp response;
        stub.MsgSearch(&controller, &request, &response, nullptr);
        return print_response(controller, response);
    }

    std::cerr << "unknown operation: " << FLAGS_operation << '\n';
    return 1;
}
