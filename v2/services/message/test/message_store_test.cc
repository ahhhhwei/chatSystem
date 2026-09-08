#include "message/file_client.hpp"
#include "message/message_service.hpp"
#include "message/message_store.hpp"
#include "file/file_service.hpp"

#include <brpc/server.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

class MessageTest : public testing::Test {
protected:
    void SetUp() override {
        test_path_ = std::filesystem::temp_directory_path() /
                     "chat_system_v2_message_test";
        std::error_code ignored;
        std::filesystem::remove_all(test_path_, ignored);
    }

    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(test_path_, ignored);
    }

    std::filesystem::path storage_file() const {
        return test_path_ / "messages.bin";
    }

    std::filesystem::path test_path_;
};

ahwei_im::MessageInfo make_message(
    std::string id,
    std::int64_t timestamp) {
    ahwei_im::MessageInfo message;
    message.set_message_id(std::move(id));
    message.set_chat_session_id("session-1");
    message.set_timestamp(timestamp);
    message.mutable_sender()->set_user_id("user-1");
    message.mutable_sender()->set_nickname("Alice");
    return message;
}

ahwei_im::MessageInfo make_text_message(
    std::string id,
    std::int64_t timestamp,
    std::string text) {
    auto message = make_message(std::move(id), timestamp);
    message.mutable_message()->set_message_type(ahwei_im::STRING);
    message.mutable_message()->mutable_string_message()->set_content(
        std::move(text));
    return message;
}

class FakeFileClient final : public chat::message::FileClient {
public:
    bool put(
        const std::string&,
        const std::string&,
        const std::string& content,
        std::string& file_id,
        std::string& error) override {
        error.clear();
        file_id = "stored-" + std::to_string(next_id_++);
        files_[file_id] = content;
        return true;
    }

    bool get_multi(
        const std::string&,
        const std::vector<std::string>& file_ids,
        std::unordered_map<std::string, std::string>& files,
        std::string& error) override {
        files.clear();
        error.clear();
        for (const auto& file_id : file_ids) {
            const auto item = files_.find(file_id);
            if (item == files_.end()) {
                error = "missing fake file: " + file_id;
                return false;
            }
            files.emplace(item->first, item->second);
        }
        return true;
    }

private:
    int next_id_ = 1;
    std::unordered_map<std::string, std::string> files_;
};

class TestClosure final : public google::protobuf::Closure {
public:
    void Run() override {
        called = true;
    }

    bool called = false;
};

TEST_F(MessageTest, BrpcFileClientTransfersRealBinaryContent) {
    chat::file::FileServiceImpl file_service(test_path_ / "files");
    brpc::Server server;
    ASSERT_EQ(
        0,
        server.AddService(
            &file_service,
            brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, server.Start(0, nullptr));

    const auto address =
        "127.0.0.1:" + std::to_string(server.listen_address().port);
    chat::message::BrpcFileClient client(address, 3000);
    const std::string original("real\0binary\xff", 12);
    std::string file_id;
    std::string error;
    const bool put_ok = client.put(
        "put-real-binary",
        "real.bin",
        original,
        file_id,
        error);

    std::unordered_map<std::string, std::string> downloaded;
    const bool get_ok = put_ok && client.get_multi(
        "get-real-binary",
        {file_id},
        downloaded,
        error);

    server.Stop(0);
    server.Join();

    ASSERT_TRUE(put_ok) << error;
    ASSERT_TRUE(get_ok) << error;
    ASSERT_EQ(1U, downloaded.size());
    EXPECT_EQ(original, downloaded.at(file_id));
}

TEST_F(MessageTest, PersistsAndQueriesMessagesInChronologicalOrder) {
    {
        chat::message::MessageStore store(storage_file());
        std::string error;
        ASSERT_TRUE(store.append(
            make_text_message("message-1", 100, "first needle"),
            error)) << error;
        ASSERT_TRUE(store.append(
            make_text_message("message-2", 200, "second"),
            error)) << error;
        ASSERT_TRUE(store.append(
            make_text_message("message-3", 300, "third needle"),
            error)) << error;

        auto other_session =
            make_text_message("message-4", 400, "needle elsewhere");
        other_session.set_chat_session_id("session-2");
        ASSERT_TRUE(store.append(other_session, error)) << error;

        const auto recent = store.recent("session-1", 2);
        ASSERT_EQ(2U, recent.size());
        EXPECT_EQ("message-2", recent[0].message_id());
        EXPECT_EQ("message-3", recent[1].message_id());

        const auto recent_before = store.recent("session-1", 10, 250);
        ASSERT_EQ(2U, recent_before.size());
        EXPECT_EQ("message-1", recent_before[0].message_id());
        EXPECT_EQ("message-2", recent_before[1].message_id());

        const auto history = store.range("session-1", 150, 300);
        ASSERT_EQ(2U, history.size());
        EXPECT_EQ("message-2", history[0].message_id());
        EXPECT_EQ("message-3", history[1].message_id());

        const auto search = store.search("session-1", "needle");
        ASSERT_EQ(2U, search.size());
        EXPECT_EQ("message-1", search[0].message_id());
        EXPECT_EQ("message-3", search[1].message_id());
    }

    chat::message::MessageStore reopened(storage_file());
    EXPECT_EQ(4U, reopened.size());
    const auto messages = reopened.recent("session-1", 10);
    ASSERT_EQ(3U, messages.size());
    EXPECT_EQ("message-3", messages.back().message_id());
}

TEST_F(MessageTest, RejectsDuplicateMessageId) {
    chat::message::MessageStore store(storage_file());
    std::string error;
    ASSERT_TRUE(store.append(
        make_text_message("same-id", 100, "first"),
        error)) << error;
    EXPECT_FALSE(store.append(
        make_text_message("same-id", 200, "second"),
        error));
    EXPECT_EQ("duplicate message_id", error);
    EXPECT_EQ(1U, store.size());
}

TEST_F(MessageTest, RecoversAnIncompleteLastRecord) {
    std::uintmax_t complete_size = 0;
    {
        chat::message::MessageStore store(storage_file());
        std::string error;
        ASSERT_TRUE(store.append(
            make_text_message("complete", 100, "saved"),
            error)) << error;
        complete_size = std::filesystem::file_size(storage_file());
    }

    {
        std::ofstream output(
            storage_file(),
            std::ios::binary | std::ios::app);
        ASSERT_TRUE(output.good());
        output.write("\0\0", 2);
    }
    ASSERT_GT(std::filesystem::file_size(storage_file()), complete_size);

    chat::message::MessageStore reopened(storage_file());
    EXPECT_EQ(1U, reopened.size());
    EXPECT_EQ(complete_size, std::filesystem::file_size(storage_file()));

    std::string error;
    EXPECT_TRUE(reopened.append(
        make_text_message("after-recovery", 200, "saved too"),
        error)) << error;
    EXPECT_EQ(2U, reopened.size());
}

TEST_F(MessageTest, StoresFileMetadataAndHydratesAllFileMessageTypes) {
    auto store = std::make_shared<chat::message::MessageStore>(storage_file());
    auto files = std::make_shared<FakeFileClient>();
    chat::message::MessageServiceImpl service(store, files);
    std::string error;

    const auto text = make_text_message("text-1", 100, "find this text");
    const std::string serialized = text.SerializeAsString();
    ASSERT_TRUE(service.on_message(
        serialized.data(),
        serialized.size(),
        error)) << error;

    auto image = make_message("image-1", 200);
    image.mutable_message()->set_message_type(ahwei_im::IMAGE);
    image.mutable_message()->mutable_image_message()->set_image_content(
        std::string("image\0data", 10));
    ASSERT_TRUE(service.store_message(std::move(image), error)) << error;

    auto file = make_message("file-1", 300);
    file.mutable_message()->set_message_type(ahwei_im::FILE);
    file.mutable_message()->mutable_file_message()->set_file_name("test.bin");
    file.mutable_message()->mutable_file_message()->set_file_size(999);
    file.mutable_message()->mutable_file_message()->set_file_contents(
        std::string("a\0b\0", 4));
    ASSERT_TRUE(service.store_message(std::move(file), error)) << error;

    auto speech = make_message("speech-1", 400);
    speech.mutable_message()->set_message_type(ahwei_im::SPEECH);
    speech.mutable_message()->mutable_speech_message()->set_file_contents(
        "speech-data");
    ASSERT_TRUE(service.store_message(std::move(speech), error)) << error;

    ASSERT_EQ(4U, store->size());
    const auto metadata = store->recent("session-1", 10);
    ASSERT_EQ(4U, metadata.size());
    EXPECT_TRUE(
        metadata[1].message().image_message().image_content().empty());
    EXPECT_TRUE(
        metadata[2].message().file_message().file_contents().empty());
    EXPECT_EQ(4, metadata[2].message().file_message().file_size());
    EXPECT_TRUE(
        metadata[3].message().speech_message().file_contents().empty());

    ahwei_im::GetHistoryMsgReq history_request;
    history_request.set_request_id("history-request");
    history_request.set_chat_session_id("session-1");
    history_request.set_start_time(0);
    history_request.set_over_time(500);
    ahwei_im::GetHistoryMsgRsp history_response;
    TestClosure history_done;
    service.GetHistoryMsg(
        nullptr,
        &history_request,
        &history_response,
        &history_done);

    EXPECT_TRUE(history_done.called);
    ASSERT_TRUE(history_response.success()) << history_response.errmsg();
    ASSERT_EQ(4, history_response.msg_list_size());
    EXPECT_EQ(
        std::string("image\0data", 10),
        history_response.msg_list(1)
            .message()
            .image_message()
            .image_content());
    EXPECT_EQ(
        std::string("a\0b\0", 4),
        history_response.msg_list(2)
            .message()
            .file_message()
            .file_contents());
    EXPECT_EQ(
        "speech-data",
        history_response.msg_list(3)
            .message()
            .speech_message()
            .file_contents());
    EXPECT_EQ("Alice", history_response.msg_list(0).sender().nickname());

    ahwei_im::GetRecentMsgReq recent_request;
    recent_request.set_request_id("recent-request");
    recent_request.set_chat_session_id("session-1");
    recent_request.set_msg_count(2);
    recent_request.set_cur_time(350);
    ahwei_im::GetRecentMsgRsp recent_response;
    TestClosure recent_done;
    service.GetRecentMsg(
        nullptr,
        &recent_request,
        &recent_response,
        &recent_done);

    EXPECT_TRUE(recent_done.called);
    ASSERT_TRUE(recent_response.success()) << recent_response.errmsg();
    ASSERT_EQ(2, recent_response.msg_list_size());
    EXPECT_EQ("image-1", recent_response.msg_list(0).message_id());
    EXPECT_EQ("file-1", recent_response.msg_list(1).message_id());

    ahwei_im::MsgSearchReq search_request;
    search_request.set_request_id("search-request");
    search_request.set_chat_session_id("session-1");
    search_request.set_search_key("this text");
    ahwei_im::MsgSearchRsp search_response;
    TestClosure search_done;
    service.MsgSearch(
        nullptr,
        &search_request,
        &search_response,
        &search_done);

    EXPECT_TRUE(search_done.called);
    ASSERT_TRUE(search_response.success()) << search_response.errmsg();
    ASSERT_EQ(1, search_response.msg_list_size());
    EXPECT_EQ("text-1", search_response.msg_list(0).message_id());
}

}  // namespace
