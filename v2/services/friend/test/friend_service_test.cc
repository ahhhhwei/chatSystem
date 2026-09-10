#include "friend/dependencies.hpp"
#include "friend/friend_service.hpp"
#include "friend/friend_store.hpp"

#include "message.pb.h"
#include "user.pb.h"

#include <brpc/closure_guard.h>
#include <brpc/server.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

class TestClosure final : public google::protobuf::Closure {
public:
    void Run() override { called = true; }
    bool called = false;
};

class FriendTest : public testing::Test {
protected:
    void SetUp() override {
        test_path_ = std::filesystem::temp_directory_path() /
                     "chat_system_v2_friend_test";
        std::error_code ignored;
        std::filesystem::remove_all(test_path_, ignored);
        std::filesystem::create_directories(test_path_);
    }

    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(test_path_, ignored);
    }

    std::filesystem::path storage_file() const {
        return test_path_ / "friends.bin";
    }

    std::filesystem::path test_path_;
};

class FakeUserDirectory final : public chat::friend_service::UserDirectory {
public:
    FakeUserDirectory() {
        add_user("user-1", "Alice", "13800000001");
        add_user("user-2", "Bob", "13800000002");
        add_user("user-3", "Carol", "13800000003");
        sessions["login-1"] = "user-1";
        sessions["login-2"] = "user-2";
        sessions["login-3"] = "user-3";
    }

    bool get_multi(
        const std::string&,
        const std::vector<std::string>& user_ids,
        std::unordered_map<std::string, ahwei_im::UserInfo>& result,
        std::string& error) override {
        result.clear();
        error.clear();
        for (const auto& user_id : user_ids) {
            const auto user = users.find(user_id);
            if (user == users.end()) {
                error = "missing fake user: " + user_id;
                result.clear();
                return false;
            }
            result[user_id] = user->second;
        }
        return true;
    }

    bool search(
        const std::string&,
        const std::string& search_key,
        const std::vector<std::string>& excluded_user_ids,
        std::vector<ahwei_im::UserInfo>& result,
        std::string& error) override {
        result.clear();
        error.clear();
        const std::unordered_set<std::string> excluded(
            excluded_user_ids.begin(), excluded_user_ids.end());
        for (const auto& item : users) {
            if (excluded.find(item.first) == excluded.end() &&
                (item.first.find(search_key) != std::string::npos ||
                 item.second.nickname().find(search_key) != std::string::npos ||
                 item.second.phone().find(search_key) != std::string::npos)) {
                result.push_back(item.second);
            }
        }
        std::sort(
            result.begin(),
            result.end(),
            [](const auto& left, const auto& right) {
                return left.user_id() < right.user_id();
            });
        return true;
    }

    bool resolve_session(
        const std::string&,
        const std::string& session_id,
        std::string& user_id,
        std::string& error) override {
        const auto item = sessions.find(session_id);
        if (item == sessions.end()) {
            error = "invalid fake session";
            return false;
        }
        user_id = item->second;
        error.clear();
        return true;
    }

    void add_user(
        const std::string& user_id,
        const std::string& nickname,
        const std::string& phone) {
        auto& user = users[user_id];
        user.set_user_id(user_id);
        user.set_nickname(nickname);
        user.set_phone(phone);
    }

    std::unordered_map<std::string, ahwei_im::UserInfo> users;
    std::unordered_map<std::string, std::string> sessions;
};

class FakeRecentMessageClient final
    : public chat::friend_service::RecentMessageClient {
public:
    bool latest(
        const std::string&,
        const std::string& chat_session_id,
        std::optional<ahwei_im::MessageInfo>& message,
        std::string& error) override {
        error.clear();
        const auto item = messages.find(chat_session_id);
        if (item == messages.end()) {
            message.reset();
        } else {
            message = item->second;
        }
        return true;
    }

    std::unordered_map<std::string, ahwei_im::MessageInfo> messages;
};

TEST_F(FriendTest, StoreCommitsCompleteFriendAndSessionTransactions) {
    {
        chat::friend_service::FriendStore store(storage_file());
        std::string error;
        ASSERT_TRUE(store.add_application(
            "event-1", "user-1", "user-2", error)) << error;
        EXPECT_FALSE(store.add_application(
            "event-2", "user-2", "user-1", error));
        EXPECT_FALSE(store.process_application(
            "wrong-event", "user-1", "user-2", true, "single-1", error));
        EXPECT_FALSE(store.are_friends("user-1", "user-2"));

        ASSERT_TRUE(store.process_application(
            "event-1", "user-1", "user-2", true, "single-1", error))
            << error;
        EXPECT_TRUE(store.are_friends("user-1", "user-2"));
        EXPECT_EQ(
            (std::vector<std::string>{"user-2"}),
            store.friends("user-1"));
        EXPECT_EQ(
            (std::vector<std::string>{"user-1", "user-2"}),
            store.members("single-1"));

        ASSERT_TRUE(store.create_group(
            "group-1",
            "Team",
            {"user-1", "user-2", "user-3"},
            error)) << error;
        ASSERT_EQ(2U, store.sessions_for("user-1").size());
    }

    chat::friend_service::FriendStore reopened(storage_file());
    EXPECT_TRUE(reopened.are_friends("user-2", "user-1"));
    EXPECT_EQ(2U, reopened.sessions_for("user-2").size());
    EXPECT_EQ(1U, reopened.sessions_for("user-3").size());
    std::string error;
    ASSERT_TRUE(reopened.remove_friend("user-2", "user-1", error)) << error;
    EXPECT_FALSE(reopened.are_friends("user-1", "user-2"));
    EXPECT_FALSE(reopened.session("single-1"));
    EXPECT_TRUE(reopened.session("group-1"));
}

TEST_F(FriendTest, StoreRecoversAnIncompleteLastSnapshot) {
    std::uintmax_t complete_size = 0;
    {
        chat::friend_service::FriendStore store(storage_file());
        std::string error;
        ASSERT_TRUE(store.add_application(
            "event-1", "user-1", "user-2", error)) << error;
        complete_size = std::filesystem::file_size(storage_file());
    }
    {
        std::ofstream output(storage_file(), std::ios::binary | std::ios::app);
        output.write("\0\0", 2);
    }
    ASSERT_GT(std::filesystem::file_size(storage_file()), complete_size);
    chat::friend_service::FriendStore reopened(storage_file());
    EXPECT_EQ(complete_size, std::filesystem::file_size(storage_file()));
    ASSERT_EQ(1U, reopened.pending_applications("user-2").size());
}

TEST_F(FriendTest, ImplementsAllV1FriendAndChatSessionFlows) {
    auto repository = std::make_shared<chat::friend_service::FriendStore>(
        storage_file());
    auto users = std::make_shared<FakeUserDirectory>();
    auto messages = std::make_shared<FakeRecentMessageClient>();
    std::vector<std::string> ids{"event-1", "single-1", "group-1"};
    std::size_t id_index = 0;
    chat::friend_service::FriendServiceImpl service(
        repository,
        users,
        messages,
        [&ids, &id_index] { return ids.at(id_index++); });

    ahwei_im::FriendAddReq add_request;
    add_request.set_request_id("add");
    add_request.set_session_id("login-1");
    add_request.set_respondent_id("user-2");
    ahwei_im::FriendAddRsp add_response;
    TestClosure add_done;
    service.FriendAdd(nullptr, &add_request, &add_response, &add_done);
    ASSERT_TRUE(add_done.called);
    ASSERT_TRUE(add_response.success()) << add_response.errmsg();
    EXPECT_EQ("event-1", add_response.notify_event_id());

    ahwei_im::GetPendingFriendEventListReq pending_request;
    pending_request.set_request_id("pending");
    pending_request.set_session_id("login-2");
    ahwei_im::GetPendingFriendEventListRsp pending_response;
    TestClosure pending_done;
    service.GetPendingFriendEventList(
        nullptr, &pending_request, &pending_response, &pending_done);
    ASSERT_TRUE(pending_response.success()) << pending_response.errmsg();
    ASSERT_EQ(1, pending_response.event_size());
    EXPECT_EQ("event-1", pending_response.event(0).event_id());
    EXPECT_EQ("Alice", pending_response.event(0).sender().nickname());

    ahwei_im::FriendAddProcessReq process_request;
    process_request.set_request_id("process");
    process_request.set_session_id("login-2");
    process_request.set_notify_event_id("event-1");
    process_request.set_apply_user_id("user-1");
    process_request.set_agree(true);
    ahwei_im::FriendAddProcessRsp process_response;
    TestClosure process_done;
    service.FriendAddProcess(
        nullptr, &process_request, &process_response, &process_done);
    ASSERT_TRUE(process_response.success()) << process_response.errmsg();
    EXPECT_EQ("single-1", process_response.new_session_id());

    ahwei_im::GetFriendListReq friends_request;
    friends_request.set_request_id("friends");
    friends_request.set_session_id("login-1");
    ahwei_im::GetFriendListRsp friends_response;
    TestClosure friends_done;
    service.GetFriendList(
        nullptr, &friends_request, &friends_response, &friends_done);
    ASSERT_TRUE(friends_response.success()) << friends_response.errmsg();
    ASSERT_EQ(1, friends_response.friend_list_size());
    EXPECT_EQ("Bob", friends_response.friend_list(0).nickname());

    ahwei_im::FriendSearchReq search_request;
    search_request.set_request_id("search");
    search_request.set_session_id("login-1");
    search_request.set_search_key("Carol");
    ahwei_im::FriendSearchRsp search_response;
    TestClosure search_done;
    service.FriendSearch(
        nullptr, &search_request, &search_response, &search_done);
    ASSERT_TRUE(search_response.success()) << search_response.errmsg();
    ASSERT_EQ(1, search_response.user_info_size());
    EXPECT_EQ("user-3", search_response.user_info(0).user_id());

    ahwei_im::ChatSessionCreateReq create_request;
    create_request.set_request_id("create");
    create_request.set_session_id("login-1");
    create_request.set_chat_session_name("Team");
    create_request.add_member_id_list("user-1");
    create_request.add_member_id_list("user-2");
    create_request.add_member_id_list("user-3");
    create_request.add_member_id_list("user-3");
    ahwei_im::ChatSessionCreateRsp create_response;
    TestClosure create_done;
    service.ChatSessionCreate(
        nullptr, &create_request, &create_response, &create_done);
    ASSERT_TRUE(create_response.success()) << create_response.errmsg();
    EXPECT_EQ("group-1", create_response.chat_session_info().chat_session_id());

    ahwei_im::GetChatSessionMemberReq member_request;
    member_request.set_request_id("members");
    member_request.set_session_id("login-3");
    member_request.set_chat_session_id("group-1");
    ahwei_im::GetChatSessionMemberRsp member_response;
    TestClosure member_done;
    service.GetChatSessionMember(
        nullptr, &member_request, &member_response, &member_done);
    ASSERT_TRUE(member_response.success()) << member_response.errmsg();
    EXPECT_EQ(3, member_response.member_info_list_size());

    ahwei_im::MessageInfo previous;
    previous.set_message_id("message-1");
    previous.set_chat_session_id("single-1");
    previous.set_timestamp(100);
    messages->messages["single-1"] = previous;
    ahwei_im::GetChatSessionListReq sessions_request;
    sessions_request.set_request_id("sessions");
    sessions_request.set_session_id("login-1");
    ahwei_im::GetChatSessionListRsp sessions_response;
    TestClosure sessions_done;
    service.GetChatSessionList(
        nullptr, &sessions_request, &sessions_response, &sessions_done);
    ASSERT_TRUE(sessions_response.success()) << sessions_response.errmsg();
    ASSERT_EQ(2, sessions_response.chat_session_info_list_size());
    bool found_single = false;
    bool found_group = false;
    for (const auto& session : sessions_response.chat_session_info_list()) {
        if (session.chat_session_id() == "single-1") {
            found_single = true;
            EXPECT_EQ("user-2", session.single_chat_friend_id());
            EXPECT_EQ("Bob", session.chat_session_name());
            EXPECT_EQ("message-1", session.prev_message().message_id());
        }
        if (session.chat_session_id() == "group-1") {
            found_group = true;
            EXPECT_EQ("Team", session.chat_session_name());
        }
    }
    EXPECT_TRUE(found_single);
    EXPECT_TRUE(found_group);

    ahwei_im::GetChatSessionMemberIdsReq ids_request;
    ids_request.set_request_id("member-ids");
    ids_request.set_chat_session_id("group-1");
    ahwei_im::GetChatSessionMemberIdsRsp ids_response;
    TestClosure ids_done;
    service.GetChatSessionMemberIds(
        nullptr, &ids_request, &ids_response, &ids_done);
    ASSERT_TRUE(ids_response.success()) << ids_response.errmsg();
    EXPECT_EQ(3, ids_response.member_id_list_size());

    ahwei_im::FriendRemoveReq remove_request;
    remove_request.set_request_id("remove");
    remove_request.set_session_id("login-1");
    remove_request.set_peer_id("user-2");
    ahwei_im::FriendRemoveRsp remove_response;
    TestClosure remove_done;
    service.FriendRemove(
        nullptr, &remove_request, &remove_response, &remove_done);
    ASSERT_TRUE(remove_response.success()) << remove_response.errmsg();
    EXPECT_FALSE(repository->are_friends("user-1", "user-2"));
    EXPECT_FALSE(repository->session("single-1"));
    EXPECT_TRUE(repository->session("group-1"));
}

class FakeRpcUserService final : public ahwei_im::UserService {
public:
    void GetMultiUserInfo(
        google::protobuf::RpcController*,
        const ahwei_im::GetMultiUserInfoReq* request,
        ahwei_im::GetMultiUserInfoRsp* response,
        google::protobuf::Closure* done) override {
        brpc::ClosureGuard guard(done);
        response->set_request_id(request->request_id());
        response->set_success(true);
        for (const auto& user_id : request->users_id()) {
            auto& user = (*response->mutable_users_info())[user_id];
            user.set_user_id(user_id);
            user.set_nickname("RPC " + user_id);
        }
    }

    void SearchUsers(
        google::protobuf::RpcController*,
        const ahwei_im::SearchUsersReq* request,
        ahwei_im::SearchUsersRsp* response,
        google::protobuf::Closure* done) override {
        brpc::ClosureGuard guard(done);
        response->set_request_id(request->request_id());
        response->set_success(true);
        response->add_users()->set_user_id("search-result");
    }

    void ResolveSession(
        google::protobuf::RpcController*,
        const ahwei_im::ResolveSessionReq* request,
        ahwei_im::ResolveSessionRsp* response,
        google::protobuf::Closure* done) override {
        brpc::ClosureGuard guard(done);
        response->set_request_id(request->request_id());
        response->set_success(true);
        response->set_user_id("resolved-user");
    }
};

class FakeRpcMessageService final : public ahwei_im::MsgStorageService {
public:
    void GetRecentMsg(
        google::protobuf::RpcController*,
        const ahwei_im::GetRecentMsgReq* request,
        ahwei_im::GetRecentMsgRsp* response,
        google::protobuf::Closure* done) override {
        brpc::ClosureGuard guard(done);
        response->set_request_id(request->request_id());
        response->set_success(true);
        auto* message = response->add_msg_list();
        message->set_message_id("rpc-message");
        message->set_chat_session_id(request->chat_session_id());
    }
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
                service, brpc::SERVER_DOESNT_OWN_SERVICE) != 0 ||
            server_.Start(0, nullptr) != 0) {
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

TEST_F(FriendTest, BrpcDependencyClientsCallUserAndMessageServers) {
    FakeRpcUserService user_service;
    FakeRpcMessageService message_service;
    RunningServer user_server;
    RunningServer message_server;
    ASSERT_TRUE(user_server.start(&user_service));
    ASSERT_TRUE(message_server.start(&message_service));

    chat::friend_service::BrpcUserDirectory users(
        user_server.address(), 3000);
    std::unordered_map<std::string, ahwei_im::UserInfo> found;
    std::string error;
    ASSERT_TRUE(users.get_multi("users", {"user-1", "user-2"}, found, error))
        << error;
    EXPECT_EQ("RPC user-1", found.at("user-1").nickname());
    std::vector<ahwei_im::UserInfo> search_results;
    ASSERT_TRUE(users.search("search", "key", {}, search_results, error))
        << error;
    ASSERT_EQ(1U, search_results.size());
    std::string resolved_user;
    ASSERT_TRUE(users.resolve_session(
        "resolve", "session", resolved_user, error)) << error;
    EXPECT_EQ("resolved-user", resolved_user);

    chat::friend_service::BrpcRecentMessageClient messages(
        message_server.address(), 3000);
    std::optional<ahwei_im::MessageInfo> latest;
    ASSERT_TRUE(messages.latest("latest", "chat-1", latest, error)) << error;
    ASSERT_TRUE(latest);
    EXPECT_EQ("rpc-message", latest->message_id());
}

}  // namespace
