#include "user/avatar_file_client.hpp"
#include "user/security.hpp"
#include "user/user_service.hpp"
#include "user/user_store.hpp"

#include "file/file_service.hpp"

#include <brpc/server.h>
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>

namespace {

class TestClosure final : public google::protobuf::Closure {
public:
    void Run() override { called = true; }
    bool called = false;
};

class FakeCodeSender final : public chat::user::VerificationCodeSender {
public:
    bool send(
        const std::string& phone,
        const std::string& code,
        std::string& error) override {
        ++calls;
        last_phone = phone;
        last_code = code;
        error.clear();
        return !fail;
    }

    std::string last_phone;
    std::string last_code;
    int calls = 0;
    bool fail = false;
};

class UserTest : public testing::Test {
protected:
    void SetUp() override {
        test_path_ = std::filesystem::temp_directory_path() /
                     "chat_system_v2_user_test";
        std::error_code ignored;
        std::filesystem::remove_all(test_path_, ignored);
        std::filesystem::create_directories(test_path_);
    }

    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(test_path_, ignored);
    }

    std::filesystem::path storage_file() const {
        return test_path_ / "users.bin";
    }

    std::filesystem::path test_path_;
};

chat::user::UserRecord make_user(
    const std::string& id,
    const std::string& nickname,
    const std::string& phone = {}) {
    chat::user::UserRecord user;
    user.set_user_id(id);
    user.set_nickname(nickname);
    user.set_phone(phone);
    return user;
}

TEST_F(UserTest, UserStorePersistsUpdatesAndUniqueIndexes) {
    {
        chat::user::UserStore store(storage_file());
        std::string error;
        ASSERT_TRUE(store.insert(
            make_user("user-1", "alice", "13800000001"), error)) << error;
        ASSERT_TRUE(store.insert(
            make_user("user-2", "bob", "13800000002"), error)) << error;

        const auto search = store.search("alice", {"user-2"}, 10);
        ASSERT_EQ(1U, search.size());
        EXPECT_EQ("user-1", search.front().user_id());

        auto alice = store.find_by_id("user-1");
        ASSERT_TRUE(alice);
        alice->set_nickname("alice-new");
        alice->set_description("hello");
        ASSERT_TRUE(store.update(*alice, error)) << error;

        auto duplicate = *store.find_by_id("user-2");
        duplicate.set_nickname("alice-new");
        EXPECT_FALSE(store.update(duplicate, error));
        EXPECT_EQ("nickname already exists", error);
        EXPECT_EQ(2U, store.size());
    }

    chat::user::UserStore reopened(storage_file());
    ASSERT_EQ(2U, reopened.size());
    const auto alice = reopened.find_by_nickname("alice-new");
    ASSERT_TRUE(alice);
    EXPECT_EQ("user-1", alice->user_id());
    EXPECT_EQ("hello", alice->description());
    EXPECT_FALSE(reopened.find_by_nickname("alice"));
    EXPECT_EQ("user-2", reopened.find_by_phone("13800000002")->user_id());
}

TEST_F(UserTest, PasswordsAreSaltedHashedAndVerified) {
    chat::user::PasswordDigest first;
    chat::user::PasswordDigest second;
    std::string error;
    ASSERT_TRUE(chat::user::PasswordHasher::hash("abc123", first, error))
        << error;
    ASSERT_TRUE(chat::user::PasswordHasher::hash("abc123", second, error))
        << error;

    EXPECT_NE(first.salt, second.salt);
    EXPECT_NE(first.hash, second.hash);
    EXPECT_TRUE(chat::user::PasswordHasher::verify("abc123", first));
    EXPECT_FALSE(chat::user::PasswordHasher::verify("wrong1", first));
}

TEST_F(UserTest, VerificationCodesAndSessionsExpireAndAreOneTime) {
    auto now = std::chrono::steady_clock::now();
    auto sender = std::make_shared<FakeCodeSender>();
    chat::user::VerificationCodeManager codes(
        sender,
        std::chrono::seconds(5),
        [] { return "2468"; },
        [&now] { return now; });

    std::string code_id;
    std::string error;
    ASSERT_TRUE(codes.issue("13800000001", code_id, error)) << error;
    EXPECT_EQ("2468", sender->last_code);
    EXPECT_FALSE(codes.consume(
        "13800000002", code_id, "2468", error));
    EXPECT_TRUE(codes.consume(
        "13800000001", code_id, "2468", error)) << error;
    EXPECT_FALSE(codes.consume(
        "13800000001", code_id, "2468", error));

    ASSERT_TRUE(codes.issue("13800000001", code_id, error)) << error;
    now += std::chrono::seconds(5);
    EXPECT_FALSE(codes.consume(
        "13800000001", code_id, "2468", error));

    chat::user::SessionManager sessions(
        std::chrono::seconds(5),
        [&now] { return now; });
    std::string session_id;
    ASSERT_TRUE(sessions.login("user-1", session_id, error)) << error;
    std::string user_id;
    EXPECT_TRUE(sessions.resolve(session_id, user_id, error)) << error;
    EXPECT_EQ("user-1", user_id);
    EXPECT_FALSE(sessions.login("user-1", user_id, error));
    now += std::chrono::seconds(5);
    EXPECT_FALSE(sessions.resolve(session_id, user_id, error));
    EXPECT_TRUE(sessions.login("user-1", session_id, error)) << error;
}

TEST_F(UserTest, ImplementsV1UserFlowWithRealAvatarFileRpc) {
    chat::file::FileServiceImpl file_service(test_path_ / "files");
    brpc::Server file_server;
    ASSERT_EQ(0, file_server.AddService(
        &file_service, brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, file_server.Start(0, nullptr));

    const std::string file_address = "127.0.0.1:" +
        std::to_string(file_server.listen_address().port);
    auto repository = std::make_shared<chat::user::UserStore>(storage_file());
    auto files = std::make_shared<chat::user::BrpcAvatarFileClient>(
        file_address, 3000);
    auto sender = std::make_shared<FakeCodeSender>();
    auto codes = std::make_shared<chat::user::VerificationCodeManager>(
        sender,
        std::chrono::minutes(5),
        [] { return "2468"; });
    auto sessions = std::make_shared<chat::user::SessionManager>();
    chat::user::UserServiceImpl service(
        repository, files, codes, sessions);

    ahwei_im::UserRegisterReq register_request;
    register_request.set_request_id("register");
    register_request.set_nickname("alice");
    register_request.set_password("abc123");
    ahwei_im::UserRegisterRsp register_response;
    TestClosure register_done;
    service.UserRegister(
        nullptr, &register_request, &register_response, &register_done);
    ASSERT_TRUE(register_done.called);
    ASSERT_TRUE(register_response.success()) << register_response.errmsg();

    ahwei_im::UserLoginReq login_request;
    login_request.set_request_id("login");
    login_request.set_nickname("alice");
    login_request.set_password("wrong1");
    ahwei_im::UserLoginRsp login_response;
    TestClosure wrong_login_done;
    service.UserLogin(
        nullptr, &login_request, &login_response, &wrong_login_done);
    ASSERT_FALSE(login_response.success());

    login_request.set_password("abc123");
    login_response.Clear();
    TestClosure login_done;
    service.UserLogin(nullptr, &login_request, &login_response, &login_done);
    ASSERT_TRUE(login_response.success()) << login_response.errmsg();
    const std::string session_id = login_response.login_session_id();
    ASSERT_FALSE(session_id.empty());

    std::string user_id;
    std::string error;
    ASSERT_TRUE(sessions->resolve(session_id, user_id, error)) << error;
    const auto stored_user = repository->find_by_id(user_id);
    ASSERT_TRUE(stored_user);
    EXPECT_FALSE(stored_user->password_hash().empty());
    EXPECT_NE("abc123", stored_user->password_hash());

    ahwei_im::SetUserDescriptionReq description_request;
    description_request.set_request_id("description");
    description_request.set_session_id(session_id);
    description_request.set_description("new profile");
    ahwei_im::SetUserDescriptionRsp description_response;
    TestClosure description_done;
    service.SetUserDescription(
        nullptr,
        &description_request,
        &description_response,
        &description_done);
    ASSERT_TRUE(description_response.success())
        << description_response.errmsg();

    ahwei_im::SetUserNicknameReq nickname_request;
    nickname_request.set_request_id("nickname");
    nickname_request.set_session_id(session_id);
    nickname_request.set_nickname("alice2");
    ahwei_im::SetUserNicknameRsp nickname_response;
    TestClosure nickname_done;
    service.SetUserNickname(
        nullptr, &nickname_request, &nickname_response, &nickname_done);
    ASSERT_TRUE(nickname_response.success()) << nickname_response.errmsg();

    const std::string avatar("real\0avatar\xff", 12);
    ahwei_im::SetUserAvatarReq avatar_request;
    avatar_request.set_request_id("avatar");
    avatar_request.set_session_id(session_id);
    avatar_request.set_avatar(avatar);
    ahwei_im::SetUserAvatarRsp avatar_response;
    TestClosure avatar_done;
    service.SetUserAvatar(
        nullptr, &avatar_request, &avatar_response, &avatar_done);
    ASSERT_TRUE(avatar_response.success()) << avatar_response.errmsg();

    ahwei_im::GetUserInfoReq info_request;
    info_request.set_request_id("info");
    info_request.set_session_id(session_id);
    ahwei_im::GetUserInfoRsp info_response;
    TestClosure info_done;
    service.GetUserInfo(nullptr, &info_request, &info_response, &info_done);
    ASSERT_TRUE(info_response.success()) << info_response.errmsg();
    EXPECT_EQ(user_id, info_response.user_info().user_id());
    EXPECT_EQ("alice2", info_response.user_info().nickname());
    EXPECT_EQ("new profile", info_response.user_info().description());
    EXPECT_EQ(avatar, info_response.user_info().avatar());

    ahwei_im::GetMultiUserInfoReq multi_request;
    multi_request.set_request_id("multi");
    multi_request.add_users_id(user_id);
    multi_request.add_users_id(user_id);
    ahwei_im::GetMultiUserInfoRsp multi_response;
    TestClosure multi_done;
    service.GetMultiUserInfo(
        nullptr, &multi_request, &multi_response, &multi_done);
    ASSERT_TRUE(multi_response.success()) << multi_response.errmsg();
    ASSERT_EQ(1, multi_response.users_info_size());
    EXPECT_EQ(avatar, multi_response.users_info().at(user_id).avatar());

    ahwei_im::PhoneVerifyCodeReq code_request;
    code_request.set_request_id("phone-code");
    code_request.set_phone_number("13800000001");
    ahwei_im::PhoneVerifyCodeRsp code_response;
    TestClosure code_done;
    service.GetPhoneVerifyCode(
        nullptr, &code_request, &code_response, &code_done);
    ASSERT_TRUE(code_response.success()) << code_response.errmsg();

    ahwei_im::SetUserPhoneNumberReq phone_request;
    phone_request.set_request_id("set-phone");
    phone_request.set_session_id(session_id);
    phone_request.set_phone_number("13800000001");
    phone_request.set_phone_verify_code_id(code_response.verify_code_id());
    phone_request.set_phone_verify_code("2468");
    ahwei_im::SetUserPhoneNumberRsp phone_response;
    TestClosure phone_done;
    service.SetUserPhoneNumber(
        nullptr, &phone_request, &phone_response, &phone_done);
    ASSERT_TRUE(phone_response.success()) << phone_response.errmsg();
    EXPECT_EQ(
        user_id,
        repository->find_by_phone("13800000001")->user_id());

    ahwei_im::ResolveSessionReq resolve_request;
    resolve_request.set_request_id("resolve");
    resolve_request.set_session_id(session_id);
    ahwei_im::ResolveSessionRsp resolve_response;
    TestClosure resolve_done;
    service.ResolveSession(
        nullptr, &resolve_request, &resolve_response, &resolve_done);
    ASSERT_TRUE(resolve_response.success()) << resolve_response.errmsg();
    EXPECT_EQ(user_id, resolve_response.user_id());

    ahwei_im::RevokeSessionReq revoke_request;
    revoke_request.set_request_id("revoke");
    revoke_request.set_session_id(session_id);
    ahwei_im::RevokeSessionRsp revoke_response;
    TestClosure revoke_done;
    service.RevokeSession(
        nullptr, &revoke_request, &revoke_response, &revoke_done);
    ASSERT_TRUE(revoke_response.success()) << revoke_response.errmsg();

    code_response.Clear();
    TestClosure login_code_done;
    service.GetPhoneVerifyCode(
        nullptr, &code_request, &code_response, &login_code_done);
    ASSERT_TRUE(code_response.success()) << code_response.errmsg();
    ahwei_im::PhoneLoginReq phone_login_request;
    phone_login_request.set_request_id("phone-login");
    phone_login_request.set_phone_number("13800000001");
    phone_login_request.set_verify_code_id(code_response.verify_code_id());
    phone_login_request.set_verify_code("2468");
    ahwei_im::PhoneLoginRsp phone_login_response;
    TestClosure phone_login_done;
    service.PhoneLogin(
        nullptr,
        &phone_login_request,
        &phone_login_response,
        &phone_login_done);
    ASSERT_TRUE(phone_login_response.success())
        << phone_login_response.errmsg();

    ahwei_im::PhoneVerifyCodeReq register_code_request;
    register_code_request.set_request_id("register-code");
    register_code_request.set_phone_number("13900000002");
    ahwei_im::PhoneVerifyCodeRsp register_code_response;
    TestClosure register_code_done;
    service.GetPhoneVerifyCode(
        nullptr,
        &register_code_request,
        &register_code_response,
        &register_code_done);
    ASSERT_TRUE(register_code_response.success())
        << register_code_response.errmsg();
    ahwei_im::PhoneRegisterReq phone_register_request;
    phone_register_request.set_request_id("phone-register");
    phone_register_request.set_phone_number("13900000002");
    phone_register_request.set_verify_code_id(
        register_code_response.verify_code_id());
    phone_register_request.set_verify_code("2468");
    ahwei_im::PhoneRegisterRsp phone_register_response;
    TestClosure phone_register_done;
    service.PhoneRegister(
        nullptr,
        &phone_register_request,
        &phone_register_response,
        &phone_register_done);
    ASSERT_TRUE(phone_register_response.success())
        << phone_register_response.errmsg();
    ASSERT_TRUE(repository->find_by_phone("13900000002"));

    file_server.Stop(0);
    file_server.Join();
}

}  // namespace
