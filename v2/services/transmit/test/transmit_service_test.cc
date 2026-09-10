#include "transmit/dependencies.hpp"
#include "transmit/transmit_service.hpp"

#include "file/file_service.hpp"
#include "message/file_client.hpp"
#include "message/message_service.hpp"
#include "message/message_store.hpp"
#include "friend.pb.h"
#include "user.pb.h"

#include <brpc/closure_guard.h>
#include <brpc/server.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

class TransmitTest : public testing::Test {
protected:
    void SetUp() override {
        test_path_ = std::filesystem::temp_directory_path() /
                     "chat_system_v2_transmit_test";
        std::error_code ignored;
        std::filesystem::remove_all(test_path_, ignored);
        std::filesystem::create_directories(test_path_);
    }

    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(test_path_, ignored);
    }

    std::filesystem::path test_path_;
};

class TestClosure final : public google::protobuf::Closure {
public:
    void Run() override {
        called = true;
    }

    bool called = false;
};

class FakeUserClient final : public chat::transmit::UserClient {
public:
    bool get_user(
        const std::string&,
        const std::string& user_id,
        ahwei_im::UserInfo& user,
        std::string& error) override {
        ++calls;
        if (fail) {
            error = "user lookup failed";
            return false;
        }
        error.clear();
        user.set_user_id(user_id);
        user.set_nickname("Alice");
        return true;
    }

    bool fail = false;
    int calls = 0;
};

class FakeMemberRepository final
    : public chat::transmit::SessionMemberRepository {
public:
    bool members(
        const std::string&,
        std::vector<std::string>& user_ids,
        std::string& error) const override {
        ++calls;
        if (fail) {
            error = "member lookup failed";
            return false;
        }
        error.clear();
        user_ids = values;
        return true;
    }

    std::vector<std::string> values{"user-1", "user-2"};
    bool fail = false;
    mutable int calls = 0;
};

class FakePublisher final : public chat::transmit::MessagePublisher {
public:
    bool publish(
        const std::string&,
        const ahwei_im::MessageInfo& message,
        std::string& error) override {
        ++calls;
        if (fail) {
            error = "publish failed";
            return false;
        }
        error.clear();
        published.CopyFrom(message);
        return true;
    }

    ahwei_im::MessageInfo published;
    bool fail = false;
    int calls = 0;
};

class RunningServer {
public:
    ~RunningServer() {
        if (started_) {
            server_.Stop(0);
            server_.Join();
        }
    }

    bool start(google::protobuf::Service* service) {
        if (server_.AddService(
                service,
                brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
            return false;
        }
        if (server_.Start(0, nullptr) != 0) {
            return false;
        }
        started_ = true;
        return true;
    }

    std::string address() const {
        return "127.0.0.1:" +
               std::to_string(server_.listen_address().port);
    }

private:
    brpc::Server server_;
    bool started_ = false;
};

class FakeRpcUserService final : public ahwei_im::UserService {
public:
    void GetUserInfo(
        google::protobuf::RpcController*,
        const ahwei_im::GetUserInfoReq* request,
        ahwei_im::GetUserInfoRsp* response,
        google::protobuf::Closure* done) override {
        brpc::ClosureGuard guard(done);
        response->set_request_id(request->request_id());
        response->set_success(true);
        response->mutable_user_info()->set_user_id(request->user_id());
        response->mutable_user_info()->set_nickname("RPC Alice");
    }
};

class FakeRpcFriendService final : public ahwei_im::FriendService {
public:
    void GetChatSessionMemberIds(
        google::protobuf::RpcController*,
        const ahwei_im::GetChatSessionMemberIdsReq* request,
        ahwei_im::GetChatSessionMemberIdsRsp* response,
        google::protobuf::Closure* done) override {
        brpc::ClosureGuard guard(done);
        response->set_request_id(request->request_id());
        response->set_success(true);
        response->add_member_id_list("user-1");
        response->add_member_id_list("user-2");
    }
};

ahwei_im::NewMessageReq make_text_request() {
    ahwei_im::NewMessageReq request;
    request.set_request_id("request-1");
    request.set_user_id("user-1");
    request.set_session_id("login-session");
    request.set_chat_session_id("chat-1");
    request.mutable_message()->set_message_type(ahwei_im::STRING);
    request.mutable_message()->mutable_string_message()->set_content("hello");
    return request;
}

TEST_F(TransmitTest, BrpcUserClientGetsCompleteUserFromUserServer) {
    FakeRpcUserService service;
    RunningServer server;
    ASSERT_TRUE(server.start(&service));
    chat::transmit::BrpcUserClient client(server.address(), 3000);

    ahwei_im::UserInfo user;
    std::string error;
    ASSERT_TRUE(client.get_user("get-user", "user-1", user, error)) << error;
    EXPECT_EQ("user-1", user.user_id());
    EXPECT_EQ("RPC Alice", user.nickname());
}

TEST_F(TransmitTest, BrpcMemberRepositoryGetsIdsFromFriendServer) {
    FakeRpcFriendService service;
    RunningServer server;
    ASSERT_TRUE(server.start(&service));
    chat::transmit::BrpcFriendSessionMemberRepository repository(
        server.address(), 3000);

    std::vector<std::string> members;
    std::string error;
    ASSERT_TRUE(repository.members("chat-1", members, error)) << error;
    EXPECT_EQ(
        (std::vector<std::string>{"user-1", "user-2"}),
        members);
}

TEST_F(TransmitTest, BuildsPublishesAndReturnsACompleteMessage) {
    auto users = std::make_shared<FakeUserClient>();
    auto members = std::make_shared<FakeMemberRepository>();
    members->values = {"user-1", "user-2", "user-2", ""};
    auto publisher = std::make_shared<FakePublisher>();
    chat::transmit::TransmitServiceImpl service(
        users,
        members,
        publisher,
        [] { return "generated-message-id"; },
        [] { return 123456; });

    const auto request = make_text_request();
    ahwei_im::GetTransmitTargetRsp response;
    TestClosure done;
    service.GetTransmitTarget(nullptr, &request, &response, &done);

    EXPECT_TRUE(done.called);
    ASSERT_TRUE(response.success()) << response.errmsg();
    ASSERT_TRUE(response.has_message());
    EXPECT_EQ("request-1", response.request_id());
    EXPECT_EQ("generated-message-id", response.message().message_id());
    EXPECT_EQ("chat-1", response.message().chat_session_id());
    EXPECT_EQ(123456, response.message().timestamp());
    EXPECT_EQ("Alice", response.message().sender().nickname());
    EXPECT_EQ(
        "hello",
        response.message().message().string_message().content());
    ASSERT_EQ(2, response.target_id_list_size());
    EXPECT_EQ("user-1", response.target_id_list(0));
    EXPECT_EQ("user-2", response.target_id_list(1));

    ASSERT_EQ(1, publisher->calls);
    EXPECT_EQ("generated-message-id", publisher->published.message_id());
    EXPECT_EQ(1, users->calls);
    EXPECT_EQ(1, members->calls);
}

TEST_F(TransmitTest, RejectsMessageTypeAndOneofMismatchBeforeDependencies) {
    auto users = std::make_shared<FakeUserClient>();
    auto members = std::make_shared<FakeMemberRepository>();
    auto publisher = std::make_shared<FakePublisher>();
    chat::transmit::TransmitServiceImpl service(users, members, publisher);

    auto request = make_text_request();
    request.mutable_message()->set_message_type(ahwei_im::FILE);
    ahwei_im::GetTransmitTargetRsp response;
    TestClosure done;
    service.GetTransmitTarget(nullptr, &request, &response, &done);

    EXPECT_TRUE(done.called);
    EXPECT_FALSE(response.success());
    EXPECT_EQ("FILE message is missing file_message", response.errmsg());
    EXPECT_EQ(0, users->calls);
    EXPECT_EQ(0, members->calls);
    EXPECT_EQ(0, publisher->calls);
}

TEST_F(TransmitTest, DoesNotReturnTargetsWhenPersistenceFails) {
    auto users = std::make_shared<FakeUserClient>();
    auto members = std::make_shared<FakeMemberRepository>();
    auto publisher = std::make_shared<FakePublisher>();
    publisher->fail = true;
    chat::transmit::TransmitServiceImpl service(
        users,
        members,
        publisher,
        [] { return "message-id"; },
        [] { return 100; });

    const auto request = make_text_request();
    ahwei_im::GetTransmitTargetRsp response;
    TestClosure done;
    service.GetTransmitTarget(nullptr, &request, &response, &done);

    EXPECT_TRUE(done.called);
    EXPECT_FALSE(response.success());
    EXPECT_EQ("publish failed", response.errmsg());
    EXPECT_FALSE(response.has_message());
    EXPECT_EQ(0, response.target_id_list_size());
}

TEST_F(TransmitTest, RejectsSenderOutsideTheChatSession) {
    auto users = std::make_shared<FakeUserClient>();
    auto members = std::make_shared<FakeMemberRepository>();
    members->values = {"user-2", "user-3"};
    auto publisher = std::make_shared<FakePublisher>();
    chat::transmit::TransmitServiceImpl service(users, members, publisher);

    const auto request = make_text_request();
    ahwei_im::GetTransmitTargetRsp response;
    TestClosure done;
    service.GetTransmitTarget(nullptr, &request, &response, &done);

    EXPECT_TRUE(done.called);
    EXPECT_FALSE(response.success());
    EXPECT_EQ("sender is not a chat session member", response.errmsg());
    EXPECT_EQ(0, publisher->calls);
}

TEST_F(TransmitTest, LoadsAndDeduplicatesLocalSessionMembers) {
    const auto members_file = test_path_ / "session_members.txt";
    {
        std::ofstream output(members_file);
        ASSERT_TRUE(output.good());
        output << "# session user\n"
               << "chat-1 user-1\n"
               << "chat-1 user-2\n"
               << "chat-1 user-2\n"
               << "chat-2 user-3\n";
    }

    chat::transmit::FileSessionMemberRepository repository(members_file);
    std::vector<std::string> members;
    std::string error;
    ASSERT_TRUE(repository.members("chat-1", members, error)) << error;
    ASSERT_EQ(2U, members.size());
    EXPECT_EQ("user-1", members[0]);
    EXPECT_EQ("user-2", members[1]);

    ASSERT_TRUE(repository.members("missing", members, error)) << error;
    EXPECT_TRUE(members.empty());
}

TEST_F(TransmitTest, SendsARealFileThroughTransmitAndMessageStore) {
    chat::file::FileServiceImpl file_service(test_path_ / "files");
    RunningServer file_server;
    ASSERT_TRUE(file_server.start(&file_service));

    auto file_client = std::make_shared<chat::message::BrpcFileClient>(
        file_server.address(),
        3000);
    auto message_repository =
        std::make_shared<chat::message::MessageStore>(
            test_path_ / "messages.bin");
    chat::message::MessageServiceImpl message_service(
        message_repository,
        file_client);
    RunningServer message_server;
    ASSERT_TRUE(message_server.start(&message_service));

    auto users = std::make_shared<FakeUserClient>();
    auto members = std::make_shared<FakeMemberRepository>();
    auto publisher =
        std::make_shared<chat::transmit::BrpcMessagePublisher>(
            message_server.address(),
            5000);
    chat::transmit::TransmitServiceImpl transmit_service(
        users,
        members,
        publisher,
        [] { return "file-message-id"; },
        [] { return 500; });

    const std::string original("real\0file\xff", 10);
    ahwei_im::NewMessageReq request;
    request.set_request_id("file-request");
    request.set_user_id("user-1");
    request.set_chat_session_id("chat-1");
    request.mutable_message()->set_message_type(ahwei_im::FILE);
    request.mutable_message()->mutable_file_message()->set_file_name(
        "real.bin");
    request.mutable_message()->mutable_file_message()->set_file_size(
        original.size());
    request.mutable_message()->mutable_file_message()->set_file_contents(
        original);

    ahwei_im::GetTransmitTargetRsp transmit_response;
    TestClosure transmit_done;
    transmit_service.GetTransmitTarget(
        nullptr,
        &request,
        &transmit_response,
        &transmit_done);
    ASSERT_TRUE(transmit_response.success()) << transmit_response.errmsg();

    const auto metadata = message_repository->recent("chat-1", 10);
    ASSERT_EQ(1U, metadata.size());
    EXPECT_FALSE(metadata[0].message().file_message().file_id().empty());
    EXPECT_TRUE(metadata[0].message().file_message().file_contents().empty());

    ahwei_im::GetRecentMsgReq recent_request;
    recent_request.set_request_id("recent-after-transmit");
    recent_request.set_chat_session_id("chat-1");
    recent_request.set_msg_count(10);
    ahwei_im::GetRecentMsgRsp recent_response;
    TestClosure recent_done;
    message_service.GetRecentMsg(
        nullptr,
        &recent_request,
        &recent_response,
        &recent_done);

    EXPECT_TRUE(transmit_done.called);
    EXPECT_TRUE(recent_done.called);
    ASSERT_TRUE(recent_response.success()) << recent_response.errmsg();
    ASSERT_EQ(1, recent_response.msg_list_size());
    EXPECT_EQ(
        original,
        recent_response.msg_list(0)
            .message()
            .file_message()
            .file_contents());
}

}  // namespace
