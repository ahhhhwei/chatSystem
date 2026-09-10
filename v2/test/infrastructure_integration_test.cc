#include "chat/infra/elasticsearch.hpp"
#include "chat/infra/etcd.hpp"
#include "chat/infra/http.hpp"
#include "chat/infra/mysql.hpp"
#include "chat/infra/rabbitmq.hpp"
#include "chat/infra/redis.hpp"
#include "friend/mysql_friend_repository.hpp"
#include "message/mysql_message_repository.hpp"
#include "user/mysql_user_repository.hpp"
#include "user/redis_security.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>

namespace {

class CapturingSender final : public chat::user::VerificationCodeSender {
public:
    bool send(const std::string&, const std::string& code,
              std::string& error) override {
        error.clear();
        code_ = code;
        return true;
    }
    std::string code_;
};

TEST(V1InfrastructureIntegration, PersistsDiscoversQueuesAndSearches) {
    if (std::getenv("CHAT_RUN_INFRA_TESTS") == nullptr) {
        GTEST_SKIP() << "set CHAT_RUN_INFRA_TESTS=1 to test local middleware";
    }
    const std::string suffix = chat::user::make_random_id();
    auto mysql = std::make_shared<chat::infra::MysqlDatabase>(
        chat::infra::MysqlConfig{});
    std::string error;
    ASSERT_TRUE(mysql->ping(error)) << error;

    chat::user::MysqlUserRepository users(mysql);
    ahwei_im::internal::UserRecord user;
    user.set_user_id("infra-user-" + suffix);
    user.set_nickname("infra_nick_" + suffix);
    user.set_description("integration test");
    ASSERT_TRUE(users.insert(user, error)) << error;
    auto loaded_user = users.find_by_id(user.user_id());
    ASSERT_TRUE(loaded_user);
    EXPECT_EQ(loaded_user->nickname(), user.nickname());
    ASSERT_FALSE(users.search(user.nickname(), {}, 5).empty());

    chat::friend_service::MysqlFriendRepository friends(mysql);
    const std::string peer = "infra-peer-" + suffix;
    const std::string event = "infra-event-" + suffix;
    const std::string session = "infra-session-" + suffix;
    ASSERT_TRUE(friends.add_application(event, user.user_id(), peer, error)) << error;
    ASSERT_TRUE(friends.process_application(
        event, user.user_id(), peer, true, session, error)) << error;
    EXPECT_TRUE(friends.are_friends(user.user_id(), peer));
    EXPECT_TRUE(friends.is_member(session, user.user_id()));

    chat::message::MysqlMessageRepository messages(mysql);
    ahwei_im::MessageInfo message;
    message.set_message_id("infra-message-" + suffix);
    message.set_chat_session_id(session);
    message.set_timestamp(42);
    message.mutable_sender()->set_user_id(user.user_id());
    message.mutable_message()->set_message_type(ahwei_im::STRING);
    message.mutable_message()->mutable_string_message()->set_content(
        "infrastructure searchable payload");
    ASSERT_TRUE(messages.append(message, error)) << error;
    ASSERT_EQ(messages.recent(session, 1).size(), 1U);
    ASSERT_EQ(messages.search(session, "searchable").size(), 1U);

    auto redis = std::make_shared<chat::infra::RedisClient>(
        chat::infra::RedisConfig{});
    ASSERT_TRUE(redis->ping(error)) << error;
    chat::user::RedisSessionManager sessions(
        redis, std::chrono::seconds(30));
    std::string login_session;
    ASSERT_TRUE(sessions.login(user.user_id(), login_session, error)) << error;
    std::string resolved;
    ASSERT_TRUE(sessions.resolve(login_session, resolved, error)) << error;
    EXPECT_EQ(resolved, user.user_id());
    ASSERT_TRUE(sessions.revoke(login_session, error)) << error;

    auto sender = std::make_shared<CapturingSender>();
    chat::user::RedisVerificationCodeManager codes(
        sender, redis, std::chrono::seconds(30));
    std::string code_id;
    ASSERT_TRUE(codes.issue("13800000000", code_id, error)) << error;
    ASSERT_TRUE(codes.consume(
        "13800000000", code_id, sender->code_, error)) << error;

    auto etcd = std::make_shared<chat::infra::EtcdClient>();
    ASSERT_TRUE(etcd->health(error)) << error;
    const std::string registry_key = "/service/integration/" + suffix;
    chat::infra::ServiceRegistry registry(
        etcd, registry_key, "127.0.0.1:19999", std::chrono::seconds(5));
    ASSERT_TRUE(registry.start(error)) << error;
    chat::infra::EtcdEndpointResolver resolver(etcd, "/service/integration");
    std::string endpoint;
    ASSERT_TRUE(resolver.resolve(endpoint, error)) << error;
    EXPECT_EQ(endpoint, "127.0.0.1:19999");
    registry.stop();

    chat::infra::RabbitMqConfig mq_config;
    mq_config.queue = "msg_queue_integration";
    mq_config.routing_key = mq_config.queue;
    std::mutex mutex;
    std::condition_variable received_cv;
    bool received = false;
    chat::infra::RabbitMqConsumer consumer(
        mq_config, [&](const void* data, std::size_t size, std::string&) {
            const std::string body(static_cast<const char*>(data), size);
            std::lock_guard<std::mutex> lock(mutex);
            received = body == suffix;
            received_cv.notify_all();
            return true;
        });
    ASSERT_TRUE(consumer.start(error)) << error;
    chat::infra::RabbitMqPublisher publisher(mq_config);
    ASSERT_TRUE(publisher.publish(suffix, error)) << error;
    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(received_cv.wait_for(
            lock, std::chrono::seconds(5), [&] { return received; }));
    }
    consumer.stop();

    chat::infra::ElasticsearchClient search(chat::infra::ElasticsearchConfig{});
    ASSERT_TRUE(search.ensure_indices(error)) << error;
    ASSERT_TRUE(search.index_user(
        user.user_id(), user.nickname(), user.phone(), error)) << error;
    std::vector<std::string> user_ids;
    ASSERT_TRUE(search.search_users(
        user.nickname(), {}, 10, user_ids, error)) << error;
    EXPECT_NE(std::find(user_ids.begin(), user_ids.end(), user.user_id()),
              user_ids.end());
    ASSERT_TRUE(search.index_message(message.message_id(), session, 42,
        "infrastructure searchable payload", error)) << error;
    std::vector<std::string> message_ids;
    ASSERT_TRUE(search.search_messages(
        session, "searchable", message_ids, error)) << error;
    EXPECT_NE(std::find(message_ids.begin(), message_ids.end(), message.message_id()),
              message_ids.end());

    // A successful integration run leaves the developer's business tables clean.
    auto cleanup = mysql->connect(error);
    ASSERT_TRUE(cleanup.valid()) << error;
    ASSERT_TRUE(cleanup.execute("DELETE FROM messages WHERE message_id=" +
        cleanup.quote(message.message_id()), error)) << error;
    ASSERT_TRUE(cleanup.execute("DELETE FROM friend_relations WHERE first_user_id=" +
        cleanup.quote(std::min(user.user_id(), peer)) + " AND second_user_id=" +
        cleanup.quote(std::max(user.user_id(), peer)), error)) << error;
    ASSERT_TRUE(cleanup.execute("DELETE FROM chat_sessions WHERE session_id=" +
        cleanup.quote(session), error)) << error;
    ASSERT_TRUE(cleanup.execute("DELETE FROM users WHERE user_id=" +
        cleanup.quote(user.user_id()), error)) << error;

    chat::infra::HttpResponse response;
    EXPECT_TRUE(chat::infra::HttpClient::request("DELETE",
        search.config().endpoint + "/" + search.config().user_index +
            "/_doc/" + chat::infra::HttpClient::url_encode(user.user_id()) +
            "?refresh=wait_for",
        {}, {}, response, error, 10000)) << error;
    EXPECT_TRUE(chat::infra::HttpClient::request("DELETE",
        search.config().endpoint + "/" + search.config().message_index +
            "/_doc/" + chat::infra::HttpClient::url_encode(message.message_id()) +
            "?refresh=wait_for",
        {}, {}, response, error, 10000)) << error;
}

}  // namespace
