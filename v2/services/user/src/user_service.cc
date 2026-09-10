#include "user/user_service.hpp"

#include <brpc/closure_guard.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace chat::user {
namespace {

template <typename Response>
void fail(
    Response* response,
    const std::string& request_id,
    const std::string& error) {
    response->set_request_id(request_id);
    response->set_success(false);
    response->set_errmsg(error);
}

template <typename Response>
void succeed(Response* response, const std::string& request_id) {
    response->set_request_id(request_id);
    response->set_success(true);
    response->clear_errmsg();
}

PasswordDigest password_digest(const UserRecord& user) {
    return PasswordDigest{
        user.password_salt(),
        user.password_hash(),
        user.password_iterations(),
    };
}

}  // namespace

UserServiceImpl::UserServiceImpl(
    std::shared_ptr<UserRepository> repository,
    std::shared_ptr<AvatarFileClient> file_client,
    std::shared_ptr<VerificationCodeManager> codes,
    std::shared_ptr<SessionManager> sessions)
    : repository_(std::move(repository)),
      file_client_(std::move(file_client)),
      codes_(std::move(codes)),
      sessions_(std::move(sessions)) {
    if (!repository_ || !file_client_ || !codes_ || !sessions_) {
        throw std::invalid_argument("UserService dependencies cannot be null");
    }
}

void UserServiceImpl::UserRegister(
    google::protobuf::RpcController*,
    const ahwei_im::UserRegisterReq* request,
    ahwei_im::UserRegisterRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    if (!valid_nickname(request->nickname())) {
        fail(response, request->request_id(), "invalid nickname");
        return;
    }
    if (!valid_password(request->password())) {
        fail(response, request->request_id(), "invalid password format");
        return;
    }
    if (repository_->find_by_nickname(request->nickname())) {
        fail(response, request->request_id(), "nickname already exists");
        return;
    }

    PasswordDigest digest;
    std::string error;
    if (!PasswordHasher::hash(request->password(), digest, error)) {
        fail(response, request->request_id(), error);
        return;
    }

    UserRecord user;
    try {
        user.set_user_id(make_random_id());
    } catch (const std::exception& exception) {
        fail(response, request->request_id(), exception.what());
        return;
    }
    user.set_nickname(request->nickname());
    user.set_password_salt(std::move(digest.salt));
    user.set_password_hash(std::move(digest.hash));
    user.set_password_iterations(digest.iterations);
    if (!repository_->insert(user, error)) {
        fail(response, request->request_id(), error);
        return;
    }
    succeed(response, request->request_id());
}

void UserServiceImpl::UserLogin(
    google::protobuf::RpcController*,
    const ahwei_im::UserLoginReq* request,
    ahwei_im::UserLoginRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    const auto user = repository_->find_by_nickname(request->nickname());
    if (!user || user->password_hash().empty() ||
        !PasswordHasher::verify(request->password(), password_digest(*user))) {
        fail(response, request->request_id(), "nickname or password is incorrect");
        return;
    }

    std::string error;
    std::string session_id;
    try {
        if (!sessions_->login(user->user_id(), session_id, error)) {
            fail(response, request->request_id(), error);
            return;
        }
    } catch (const std::exception& exception) {
        fail(response, request->request_id(), exception.what());
        return;
    }
    succeed(response, request->request_id());
    response->set_login_session_id(std::move(session_id));
}

void UserServiceImpl::GetPhoneVerifyCode(
    google::protobuf::RpcController*,
    const ahwei_im::PhoneVerifyCodeReq* request,
    ahwei_im::PhoneVerifyCodeRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    if (!valid_phone(request->phone_number())) {
        fail(response, request->request_id(), "invalid phone number");
        return;
    }

    std::string code_id;
    std::string error;
    try {
        if (!codes_->issue(request->phone_number(), code_id, error)) {
            fail(response, request->request_id(), error);
            return;
        }
    } catch (const std::exception& exception) {
        fail(response, request->request_id(), exception.what());
        return;
    }
    succeed(response, request->request_id());
    response->set_verify_code_id(std::move(code_id));
}

void UserServiceImpl::PhoneRegister(
    google::protobuf::RpcController*,
    const ahwei_im::PhoneRegisterReq* request,
    ahwei_im::PhoneRegisterRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    if (!valid_phone(request->phone_number())) {
        fail(response, request->request_id(), "invalid phone number");
        return;
    }
    if (repository_->find_by_phone(request->phone_number())) {
        fail(response, request->request_id(), "phone number already exists");
        return;
    }

    std::string error;
    if (!codes_->consume(
            request->phone_number(),
            request->verify_code_id(),
            request->verify_code(),
            error)) {
        fail(response, request->request_id(), error);
        return;
    }

    UserRecord user;
    try {
        user.set_user_id(make_random_id());
    } catch (const std::exception& exception) {
        fail(response, request->request_id(), exception.what());
        return;
    }
    user.set_nickname(user.user_id());
    user.set_phone(request->phone_number());
    if (!repository_->insert(user, error)) {
        fail(response, request->request_id(), error);
        return;
    }
    succeed(response, request->request_id());
}

void UserServiceImpl::PhoneLogin(
    google::protobuf::RpcController*,
    const ahwei_im::PhoneLoginReq* request,
    ahwei_im::PhoneLoginRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    if (!valid_phone(request->phone_number())) {
        fail(response, request->request_id(), "invalid phone number");
        return;
    }
    const auto user = repository_->find_by_phone(request->phone_number());
    if (!user) {
        fail(response, request->request_id(), "phone number is not registered");
        return;
    }

    std::string error;
    if (!codes_->consume(
            request->phone_number(),
            request->verify_code_id(),
            request->verify_code(),
            error)) {
        fail(response, request->request_id(), error);
        return;
    }
    std::string session_id;
    try {
        if (!sessions_->login(user->user_id(), session_id, error)) {
            fail(response, request->request_id(), error);
            return;
        }
    } catch (const std::exception& exception) {
        fail(response, request->request_id(), exception.what());
        return;
    }
    succeed(response, request->request_id());
    response->set_login_session_id(std::move(session_id));
}

void UserServiceImpl::GetUserInfo(
    google::protobuf::RpcController*,
    const ahwei_im::GetUserInfoReq* request,
    ahwei_im::GetUserInfoRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    std::string user_id;
    std::string error;
    if (!resolve_user_id(
            request->user_id(), request->session_id(), user_id, error)) {
        fail(response, request->request_id(), error);
        return;
    }
    const auto user = repository_->find_by_id(user_id);
    if (!user) {
        fail(response, request->request_id(), "user not found");
        return;
    }

    std::unordered_map<std::string, ahwei_im::UserInfo> users;
    if (!hydrate_users(request->request_id(), {*user}, users, error)) {
        fail(response, request->request_id(), error);
        return;
    }
    succeed(response, request->request_id());
    response->mutable_user_info()->CopyFrom(users.at(user_id));
}

void UserServiceImpl::GetMultiUserInfo(
    google::protobuf::RpcController*,
    const ahwei_im::GetMultiUserInfoReq* request,
    ahwei_im::GetMultiUserInfoRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    std::vector<std::string> user_ids;
    std::unordered_set<std::string> unique_ids;
    for (const auto& user_id : request->users_id()) {
        if (user_id.empty()) {
            fail(response, request->request_id(), "user_id cannot be empty");
            return;
        }
        if (unique_ids.insert(user_id).second) {
            user_ids.push_back(user_id);
        }
    }

    const auto records = repository_->find_multi(user_ids);
    if (records.size() != user_ids.size()) {
        fail(response, request->request_id(), "one or more users were not found");
        return;
    }
    std::unordered_map<std::string, ahwei_im::UserInfo> users;
    std::string error;
    if (!hydrate_users(request->request_id(), records, users, error)) {
        fail(response, request->request_id(), error);
        return;
    }
    for (const auto& item : users) {
        (*response->mutable_users_info())[item.first].CopyFrom(item.second);
    }
    succeed(response, request->request_id());
}

void UserServiceImpl::SetUserAvatar(
    google::protobuf::RpcController*,
    const ahwei_im::SetUserAvatarReq* request,
    ahwei_im::SetUserAvatarRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    std::string user_id;
    std::string error;
    if (!resolve_user_id(
            request->user_id(), request->session_id(), user_id, error)) {
        fail(response, request->request_id(), error);
        return;
    }
    auto user = repository_->find_by_id(user_id);
    if (!user) {
        fail(response, request->request_id(), "user not found");
        return;
    }

    std::string file_id;
    if (!file_client_->put(
            request->request_id(), request->avatar(), file_id, error)) {
        fail(response, request->request_id(), error);
        return;
    }
    user->set_avatar_file_id(std::move(file_id));
    if (!repository_->update(*user, error)) {
        fail(response, request->request_id(), error);
        return;
    }
    succeed(response, request->request_id());
}

void UserServiceImpl::SetUserNickname(
    google::protobuf::RpcController*,
    const ahwei_im::SetUserNicknameReq* request,
    ahwei_im::SetUserNicknameRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    if (!valid_nickname(request->nickname())) {
        fail(response, request->request_id(), "invalid nickname");
        return;
    }
    std::string user_id;
    std::string error;
    if (!resolve_user_id(
            request->user_id(), request->session_id(), user_id, error)) {
        fail(response, request->request_id(), error);
        return;
    }
    auto user = repository_->find_by_id(user_id);
    if (!user) {
        fail(response, request->request_id(), "user not found");
        return;
    }
    user->set_nickname(request->nickname());
    if (!repository_->update(*user, error)) {
        fail(response, request->request_id(), error);
        return;
    }
    succeed(response, request->request_id());
}

void UserServiceImpl::SetUserDescription(
    google::protobuf::RpcController*,
    const ahwei_im::SetUserDescriptionReq* request,
    ahwei_im::SetUserDescriptionRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    if (request->description().size() > 4096) {
        fail(response, request->request_id(), "description is too long");
        return;
    }
    std::string user_id;
    std::string error;
    if (!resolve_user_id(
            request->user_id(), request->session_id(), user_id, error)) {
        fail(response, request->request_id(), error);
        return;
    }
    auto user = repository_->find_by_id(user_id);
    if (!user) {
        fail(response, request->request_id(), "user not found");
        return;
    }
    user->set_description(request->description());
    if (!repository_->update(*user, error)) {
        fail(response, request->request_id(), error);
        return;
    }
    succeed(response, request->request_id());
}

void UserServiceImpl::SetUserPhoneNumber(
    google::protobuf::RpcController*,
    const ahwei_im::SetUserPhoneNumberReq* request,
    ahwei_im::SetUserPhoneNumberRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    if (!valid_phone(request->phone_number())) {
        fail(response, request->request_id(), "invalid phone number");
        return;
    }
    std::string user_id;
    std::string error;
    if (!resolve_user_id(
            request->user_id(), request->session_id(), user_id, error)) {
        fail(response, request->request_id(), error);
        return;
    }
    auto user = repository_->find_by_id(user_id);
    if (!user) {
        fail(response, request->request_id(), "user not found");
        return;
    }
    const auto owner = repository_->find_by_phone(request->phone_number());
    if (owner && owner->user_id() != user_id) {
        fail(response, request->request_id(), "phone number already exists");
        return;
    }
    if (!codes_->consume(
            request->phone_number(),
            request->phone_verify_code_id(),
            request->phone_verify_code(),
            error)) {
        fail(response, request->request_id(), error);
        return;
    }
    user->set_phone(request->phone_number());
    if (!repository_->update(*user, error)) {
        fail(response, request->request_id(), error);
        return;
    }
    succeed(response, request->request_id());
}

void UserServiceImpl::ResolveSession(
    google::protobuf::RpcController*,
    const ahwei_im::ResolveSessionReq* request,
    ahwei_im::ResolveSessionRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    std::string user_id;
    std::string error;
    if (!sessions_->resolve(request->session_id(), user_id, error)) {
        fail(response, request->request_id(), error);
        return;
    }
    succeed(response, request->request_id());
    response->set_user_id(std::move(user_id));
}

void UserServiceImpl::RevokeSession(
    google::protobuf::RpcController*,
    const ahwei_im::RevokeSessionReq* request,
    ahwei_im::RevokeSessionRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    std::string error;
    if (!sessions_->revoke(request->session_id(), error)) {
        fail(response, request->request_id(), error);
        return;
    }
    succeed(response, request->request_id());
}

void UserServiceImpl::SearchUsers(
    google::protobuf::RpcController*,
    const ahwei_im::SearchUsersReq* request,
    ahwei_im::SearchUsersRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    if (request->search_key().size() > 128) {
        fail(response, request->request_id(), "search key is too long");
        return;
    }

    std::vector<std::string> excluded(
        request->exclude_user_ids().begin(),
        request->exclude_user_ids().end());
    const std::size_t limit = request->limit() == 0
        ? 50U
        : std::min<std::size_t>(request->limit(), 200U);
    const auto records = repository_->search(
        request->search_key(), excluded, limit);
    std::unordered_map<std::string, ahwei_im::UserInfo> users;
    std::string error;
    if (!hydrate_users(request->request_id(), records, users, error)) {
        fail(response, request->request_id(), error);
        return;
    }
    for (const auto& record : records) {
        response->add_users()->CopyFrom(users.at(record.user_id()));
    }
    succeed(response, request->request_id());
}

bool UserServiceImpl::valid_nickname(const std::string& nickname) {
    // 与 v1 的实际判断保持一致：UTF-8 字节长度小于 22。
    return !nickname.empty() && nickname.size() < 22;
}

bool UserServiceImpl::valid_password(const std::string& password) {
    if (password.size() < 6 || password.size() > 15) {
        return false;
    }
    for (const unsigned char character : password) {
        if (std::isalnum(character) == 0 && character != '_' && character != '-') {
            return false;
        }
    }
    return true;
}

bool UserServiceImpl::valid_phone(const std::string& phone) {
    if (phone.size() != 11 || phone[0] != '1' ||
        phone[1] < '3' || phone[1] > '9') {
        return false;
    }
    for (std::size_t index = 2; index < phone.size(); ++index) {
        if (phone[index] < '0' || phone[index] > '9') {
            return false;
        }
    }
    return true;
}

bool UserServiceImpl::resolve_user_id(
    const std::string& requested_user_id,
    const std::string& session_id,
    std::string& user_id,
    std::string& error) {
    user_id.clear();
    error.clear();
    if (session_id.empty()) {
        if (requested_user_id.empty()) {
            error = "user_id or session_id is required";
            return false;
        }
        user_id = requested_user_id;
        return true;
    }

    if (!sessions_->resolve(session_id, user_id, error)) {
        return false;
    }
    if (!requested_user_id.empty() && requested_user_id != user_id) {
        user_id.clear();
        error = "session does not belong to user_id";
        return false;
    }
    return true;
}

bool UserServiceImpl::hydrate_users(
    const std::string& request_id,
    const std::vector<UserRecord>& records,
    std::unordered_map<std::string, ahwei_im::UserInfo>& users,
    std::string& error) {
    users.clear();
    error.clear();
    std::vector<std::string> avatar_ids;
    for (const auto& record : records) {
        if (!record.avatar_file_id().empty()) {
            avatar_ids.push_back(record.avatar_file_id());
        }
    }
    std::unordered_map<std::string, std::string> avatars;
    if (!file_client_->get_multi(request_id, avatar_ids, avatars, error)) {
        if (error.empty()) {
            error = "cannot download user avatar";
        }
        return false;
    }

    for (const auto& record : records) {
        ahwei_im::UserInfo info;
        info.set_user_id(record.user_id());
        info.set_nickname(record.nickname());
        info.set_description(record.description());
        info.set_phone(record.phone());
        if (!record.avatar_file_id().empty()) {
            const auto avatar = avatars.find(record.avatar_file_id());
            if (avatar == avatars.end()) {
                error = "FileServer did not return user avatar";
                users.clear();
                return false;
            }
            info.set_avatar(avatar->second);
        }
        users.emplace(record.user_id(), std::move(info));
    }
    return true;
}

}  // namespace chat::user
