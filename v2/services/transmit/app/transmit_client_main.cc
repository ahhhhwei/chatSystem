#include "transmit.pb.h"

#include <brpc/channel.h>
#include <brpc/controller.h>
#include <gflags/gflags.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

DEFINE_string(server, "127.0.0.1:10004", "MessageTransmitServer address");
DEFINE_int32(timeout_ms, 5000, "RPC timeout in milliseconds");
DEFINE_string(user_id, "user-1", "Message sender user id");
DEFINE_string(chat_session_id, "demo-session", "Target chat session");
DEFINE_string(type, "text", "Message type: text, image, file, or speech");
DEFINE_string(content, "hello from MessageTransmitServer", "Text content");
DEFINE_string(file_path, "", "Binary file used by image, file, or speech");

namespace {

bool read_file(
    const std::string& path,
    std::string& content,
    std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot open file: " + path;
        return false;
    }
    content.assign(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    if (input.bad()) {
        error = "cannot read file: " + path;
        return false;
    }
    return true;
}

bool set_content(
    ahwei_im::NewMessageReq& request,
    std::string& error) {
    if (FLAGS_type == "text") {
        request.mutable_message()->set_message_type(ahwei_im::STRING);
        request.mutable_message()->mutable_string_message()->set_content(
            FLAGS_content);
        return true;
    }
    if (FLAGS_file_path.empty()) {
        error = "--file_path is required for " + FLAGS_type;
        return false;
    }

    std::string binary;
    if (!read_file(FLAGS_file_path, binary, error)) {
        return false;
    }
    if (FLAGS_type == "image") {
        request.mutable_message()->set_message_type(ahwei_im::IMAGE);
        request.mutable_message()->mutable_image_message()->set_image_content(
            std::move(binary));
        return true;
    }
    if (FLAGS_type == "file") {
        request.mutable_message()->set_message_type(ahwei_im::FILE);
        auto* file = request.mutable_message()->mutable_file_message();
        file->set_file_name(
            std::filesystem::path(FLAGS_file_path).filename().string());
        file->set_file_size(static_cast<std::int64_t>(binary.size()));
        file->set_file_contents(std::move(binary));
        return true;
    }
    if (FLAGS_type == "speech") {
        request.mutable_message()->set_message_type(ahwei_im::SPEECH);
        request.mutable_message()->mutable_speech_message()->set_file_contents(
            std::move(binary));
        return true;
    }

    error = "unknown message type: " + FLAGS_type;
    return false;
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
        std::cerr << "cannot connect to " << FLAGS_server << '\n';
        return 1;
    }

    ahwei_im::NewMessageReq request;
    request.set_request_id("transmit-client-request");
    request.set_user_id(FLAGS_user_id);
    request.set_chat_session_id(FLAGS_chat_session_id);
    std::string error;
    if (!set_content(request, error)) {
        std::cerr << error << '\n';
        return 1;
    }

    ahwei_im::GetTransmitTargetRsp response;
    brpc::Controller controller;
    ahwei_im::MsgTransmitService_Stub stub(&channel);
    stub.GetTransmitTarget(&controller, &request, &response, nullptr);
    if (controller.Failed()) {
        std::cerr << "rpc failed: " << controller.ErrorText() << '\n';
        return 1;
    }
    if (!response.success()) {
        std::cerr << "transmit failed: " << response.errmsg() << '\n';
        return 1;
    }

    std::cout << "message_id=" << response.message().message_id()
              << " timestamp=" << response.message().timestamp()
              << " targets=" << response.target_id_list_size() << '\n';
    for (const auto& user_id : response.target_id_list()) {
        std::cout << "target=" << user_id << '\n';
    }
    return 0;
}
