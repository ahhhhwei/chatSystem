#include "user.pb.h"

#include <brpc/channel.h>
#include <brpc/controller.h>
#include <gflags/gflags.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <string>

DEFINE_string(server, "127.0.0.1:10003", "UserServer address");
DEFINE_int32(timeout_ms, 5000, "RPC timeout");
DEFINE_string(operation, "info", "register/login/code/phone_register/phone_login/info/avatar/nickname/description/phone/resolve/revoke");
DEFINE_string(nickname, "alice", "Nickname");
DEFINE_string(password, "123456", "Password");
DEFINE_string(phone_number, "", "Phone number");
DEFINE_string(verify_code_id, "", "Verification code id");
DEFINE_string(verify_code, "", "Verification code");
DEFINE_string(user_id, "", "User id");
DEFINE_string(session_id, "", "Login session id");
DEFINE_string(description, "", "New description");
DEFINE_string(avatar_file, "", "Avatar file path");

namespace {

std::string request_id() {
    const auto now = std::chrono::high_resolution_clock::now()
                         .time_since_epoch()
                         .count();
    return "user-client-" + std::to_string(now);
}

template <typename Response>
int print_result(const Response& response) {
    if (!response.success()) {
        std::cerr << "request failed: " << response.errmsg() << '\n';
        return 1;
    }
    std::cout << "success\n";
    return 0;
}

bool read_binary(const std::string& path, std::string& content) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    content.assign(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    return !input.bad();
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
        std::cerr << "cannot connect to UserServer\n";
        return 1;
    }
    ahwei_im::UserService_Stub stub(&channel);
    brpc::Controller controller;
    const std::string rid = request_id();

    if (FLAGS_operation == "register") {
        ahwei_im::UserRegisterReq request;
        ahwei_im::UserRegisterRsp response;
        request.set_request_id(rid);
        request.set_nickname(FLAGS_nickname);
        request.set_password(FLAGS_password);
        stub.UserRegister(&controller, &request, &response, nullptr);
        if (!controller.Failed()) return print_result(response);
    } else if (FLAGS_operation == "login") {
        ahwei_im::UserLoginReq request;
        ahwei_im::UserLoginRsp response;
        request.set_request_id(rid);
        request.set_nickname(FLAGS_nickname);
        request.set_password(FLAGS_password);
        stub.UserLogin(&controller, &request, &response, nullptr);
        if (!controller.Failed()) {
            const int result = print_result(response);
            if (result == 0) std::cout << "session_id=" << response.login_session_id() << '\n';
            return result;
        }
    } else if (FLAGS_operation == "code") {
        ahwei_im::PhoneVerifyCodeReq request;
        ahwei_im::PhoneVerifyCodeRsp response;
        request.set_request_id(rid);
        request.set_phone_number(FLAGS_phone_number);
        stub.GetPhoneVerifyCode(&controller, &request, &response, nullptr);
        if (!controller.Failed()) {
            const int result = print_result(response);
            if (result == 0) std::cout << "verify_code_id=" << response.verify_code_id() << '\n';
            return result;
        }
    } else if (FLAGS_operation == "phone_register") {
        ahwei_im::PhoneRegisterReq request;
        ahwei_im::PhoneRegisterRsp response;
        request.set_request_id(rid);
        request.set_phone_number(FLAGS_phone_number);
        request.set_verify_code_id(FLAGS_verify_code_id);
        request.set_verify_code(FLAGS_verify_code);
        stub.PhoneRegister(&controller, &request, &response, nullptr);
        if (!controller.Failed()) return print_result(response);
    } else if (FLAGS_operation == "phone_login") {
        ahwei_im::PhoneLoginReq request;
        ahwei_im::PhoneLoginRsp response;
        request.set_request_id(rid);
        request.set_phone_number(FLAGS_phone_number);
        request.set_verify_code_id(FLAGS_verify_code_id);
        request.set_verify_code(FLAGS_verify_code);
        stub.PhoneLogin(&controller, &request, &response, nullptr);
        if (!controller.Failed()) {
            const int result = print_result(response);
            if (result == 0) std::cout << "session_id=" << response.login_session_id() << '\n';
            return result;
        }
    } else if (FLAGS_operation == "info") {
        ahwei_im::GetUserInfoReq request;
        ahwei_im::GetUserInfoRsp response;
        request.set_request_id(rid);
        request.set_user_id(FLAGS_user_id);
        request.set_session_id(FLAGS_session_id);
        stub.GetUserInfo(&controller, &request, &response, nullptr);
        if (!controller.Failed()) {
            const int result = print_result(response);
            if (result == 0) {
                const auto& user = response.user_info();
                std::cout << "user_id=" << user.user_id()
                          << "\nnickname=" << user.nickname()
                          << "\ndescription=" << user.description()
                          << "\nphone=" << user.phone()
                          << "\navatar_bytes=" << user.avatar().size() << '\n';
            }
            return result;
        }
    } else if (FLAGS_operation == "avatar") {
        std::string avatar;
        if (!read_binary(FLAGS_avatar_file, avatar)) {
            std::cerr << "cannot read avatar file\n";
            return 1;
        }
        ahwei_im::SetUserAvatarReq request;
        ahwei_im::SetUserAvatarRsp response;
        request.set_request_id(rid);
        request.set_user_id(FLAGS_user_id);
        request.set_session_id(FLAGS_session_id);
        request.set_avatar(std::move(avatar));
        stub.SetUserAvatar(&controller, &request, &response, nullptr);
        if (!controller.Failed()) return print_result(response);
    } else if (FLAGS_operation == "nickname") {
        ahwei_im::SetUserNicknameReq request;
        ahwei_im::SetUserNicknameRsp response;
        request.set_request_id(rid);
        request.set_user_id(FLAGS_user_id);
        request.set_session_id(FLAGS_session_id);
        request.set_nickname(FLAGS_nickname);
        stub.SetUserNickname(&controller, &request, &response, nullptr);
        if (!controller.Failed()) return print_result(response);
    } else if (FLAGS_operation == "description") {
        ahwei_im::SetUserDescriptionReq request;
        ahwei_im::SetUserDescriptionRsp response;
        request.set_request_id(rid);
        request.set_user_id(FLAGS_user_id);
        request.set_session_id(FLAGS_session_id);
        request.set_description(FLAGS_description);
        stub.SetUserDescription(&controller, &request, &response, nullptr);
        if (!controller.Failed()) return print_result(response);
    } else if (FLAGS_operation == "phone") {
        ahwei_im::SetUserPhoneNumberReq request;
        ahwei_im::SetUserPhoneNumberRsp response;
        request.set_request_id(rid);
        request.set_user_id(FLAGS_user_id);
        request.set_session_id(FLAGS_session_id);
        request.set_phone_number(FLAGS_phone_number);
        request.set_phone_verify_code_id(FLAGS_verify_code_id);
        request.set_phone_verify_code(FLAGS_verify_code);
        stub.SetUserPhoneNumber(&controller, &request, &response, nullptr);
        if (!controller.Failed()) return print_result(response);
    } else if (FLAGS_operation == "resolve") {
        ahwei_im::ResolveSessionReq request;
        ahwei_im::ResolveSessionRsp response;
        request.set_request_id(rid);
        request.set_session_id(FLAGS_session_id);
        stub.ResolveSession(&controller, &request, &response, nullptr);
        if (!controller.Failed()) {
            const int result = print_result(response);
            if (result == 0) std::cout << "user_id=" << response.user_id() << '\n';
            return result;
        }
    } else if (FLAGS_operation == "revoke") {
        ahwei_im::RevokeSessionReq request;
        ahwei_im::RevokeSessionRsp response;
        request.set_request_id(rid);
        request.set_session_id(FLAGS_session_id);
        stub.RevokeSession(&controller, &request, &response, nullptr);
        if (!controller.Failed()) return print_result(response);
    } else {
        std::cerr << "unsupported operation: " << FLAGS_operation << '\n';
        return 1;
    }

    std::cerr << "RPC failed: " << controller.ErrorText() << '\n';
    return 1;
}
