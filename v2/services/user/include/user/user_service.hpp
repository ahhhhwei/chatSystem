#pragma once

#include "user/avatar_file_client.hpp"
#include "user/security.hpp"
#include "user/user_store.hpp"
#include "user.pb.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace chat::user {

class UserServiceImpl final : public ahwei_im::UserService {
public:
    UserServiceImpl(
        std::shared_ptr<UserRepository> repository,
        std::shared_ptr<AvatarFileClient> file_client,
        std::shared_ptr<VerificationCodeManager> codes,
        std::shared_ptr<SessionManager> sessions);

    void UserRegister(
        google::protobuf::RpcController* controller,
        const ahwei_im::UserRegisterReq* request,
        ahwei_im::UserRegisterRsp* response,
        google::protobuf::Closure* done) override;
    void UserLogin(
        google::protobuf::RpcController* controller,
        const ahwei_im::UserLoginReq* request,
        ahwei_im::UserLoginRsp* response,
        google::protobuf::Closure* done) override;
    void GetPhoneVerifyCode(
        google::protobuf::RpcController* controller,
        const ahwei_im::PhoneVerifyCodeReq* request,
        ahwei_im::PhoneVerifyCodeRsp* response,
        google::protobuf::Closure* done) override;
    void PhoneRegister(
        google::protobuf::RpcController* controller,
        const ahwei_im::PhoneRegisterReq* request,
        ahwei_im::PhoneRegisterRsp* response,
        google::protobuf::Closure* done) override;
    void PhoneLogin(
        google::protobuf::RpcController* controller,
        const ahwei_im::PhoneLoginReq* request,
        ahwei_im::PhoneLoginRsp* response,
        google::protobuf::Closure* done) override;
    void GetUserInfo(
        google::protobuf::RpcController* controller,
        const ahwei_im::GetUserInfoReq* request,
        ahwei_im::GetUserInfoRsp* response,
        google::protobuf::Closure* done) override;
    void GetMultiUserInfo(
        google::protobuf::RpcController* controller,
        const ahwei_im::GetMultiUserInfoReq* request,
        ahwei_im::GetMultiUserInfoRsp* response,
        google::protobuf::Closure* done) override;
    void SetUserAvatar(
        google::protobuf::RpcController* controller,
        const ahwei_im::SetUserAvatarReq* request,
        ahwei_im::SetUserAvatarRsp* response,
        google::protobuf::Closure* done) override;
    void SetUserNickname(
        google::protobuf::RpcController* controller,
        const ahwei_im::SetUserNicknameReq* request,
        ahwei_im::SetUserNicknameRsp* response,
        google::protobuf::Closure* done) override;
    void SetUserDescription(
        google::protobuf::RpcController* controller,
        const ahwei_im::SetUserDescriptionReq* request,
        ahwei_im::SetUserDescriptionRsp* response,
        google::protobuf::Closure* done) override;
    void SetUserPhoneNumber(
        google::protobuf::RpcController* controller,
        const ahwei_im::SetUserPhoneNumberReq* request,
        ahwei_im::SetUserPhoneNumberRsp* response,
        google::protobuf::Closure* done) override;
    void ResolveSession(
        google::protobuf::RpcController* controller,
        const ahwei_im::ResolveSessionReq* request,
        ahwei_im::ResolveSessionRsp* response,
        google::protobuf::Closure* done) override;
    void RevokeSession(
        google::protobuf::RpcController* controller,
        const ahwei_im::RevokeSessionReq* request,
        ahwei_im::RevokeSessionRsp* response,
        google::protobuf::Closure* done) override;
    void SearchUsers(
        google::protobuf::RpcController* controller,
        const ahwei_im::SearchUsersReq* request,
        ahwei_im::SearchUsersRsp* response,
        google::protobuf::Closure* done) override;

private:
    static bool valid_nickname(const std::string& nickname);
    static bool valid_password(const std::string& password);
    static bool valid_phone(const std::string& phone);
    bool resolve_user_id(
        const std::string& requested_user_id,
        const std::string& session_id,
        std::string& user_id,
        std::string& error);
    bool hydrate_users(
        const std::string& request_id,
        const std::vector<UserRecord>& records,
        std::unordered_map<std::string, ahwei_im::UserInfo>& users,
        std::string& error);

    std::shared_ptr<UserRepository> repository_;
    std::shared_ptr<AvatarFileClient> file_client_;
    std::shared_ptr<VerificationCodeManager> codes_;
    std::shared_ptr<SessionManager> sessions_;
};

}  // namespace chat::user
