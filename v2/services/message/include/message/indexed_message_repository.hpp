#pragma once

#include "chat/infra/elasticsearch.hpp"
#include "message/message_store.hpp"

#include <memory>

namespace chat::message {

class IndexedMessageRepository final : public MessageRepository {
public:
    IndexedMessageRepository(std::shared_ptr<MessageRepository> backing,
        std::shared_ptr<infra::ElasticsearchClient> search);

    bool append(const ahwei_im::MessageInfo& message,
        std::string& error) override;
    std::vector<ahwei_im::MessageInfo> range(std::string_view session,
        std::int64_t start, std::int64_t end) const override;
    std::vector<ahwei_im::MessageInfo> recent(std::string_view session,
        std::size_t count, std::int64_t current = 0) const override;
    std::vector<ahwei_im::MessageInfo> search(std::string_view session,
        std::string_view key) const override;

private:
    std::shared_ptr<MessageRepository> backing_;
    std::shared_ptr<infra::ElasticsearchClient> search_;
};

}  // namespace chat::message
