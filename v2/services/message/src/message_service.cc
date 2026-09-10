#include "message/message_service.hpp"

#include <brpc/closure_guard.h>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace chat::message {
namespace {

template <typename Response>
void set_error(
    Response* response,
    const std::string& request_id,
    const std::string& error) {
    response->set_request_id(request_id);
    response->set_success(false);
    response->set_errmsg(error);
    response->clear_msg_list();
}

template <typename Response>
void set_messages(
    Response* response,
    const std::string& request_id,
    std::vector<ahwei_im::MessageInfo> messages) {
    response->set_request_id(request_id);
    response->set_success(true);
    for (auto& message : messages) {
        response->add_msg_list()->Swap(&message);
    }
}

}  // namespace

MessageServiceImpl::MessageServiceImpl(
    std::shared_ptr<MessageRepository> repository,
    std::shared_ptr<FileClient> file_client)
    : repository_(std::move(repository)),
      file_client_(std::move(file_client)) {
    if (!repository_) {
        throw std::invalid_argument("MessageRepository cannot be null");
    }
    if (!file_client_) {
        throw std::invalid_argument("FileClient cannot be null");
    }
}

// 保存消息
bool MessageServiceImpl::store_message(
    ahwei_im::MessageInfo message,
    std::string& error) {
    error.clear();
    if (!message.has_message()) {
        error = "message content cannot be empty";
        return false;
    }

    auto* content = message.mutable_message();
    std::string file_id;
    switch (content->message_type()) {
        case ahwei_im::STRING:
            if (!content->has_string_message()) {
                error = "STRING message is missing string_message";
                return false;
            }
            break;
        case ahwei_im::IMAGE: {
            if (!content->has_image_message()) {
                error = "IMAGE message is missing image_message";
                return false;
            }
            auto* image = content->mutable_image_message();
            if (!file_client_->put(
                    message.message_id(),
                    "",
                    image->image_content(),
                    file_id,
                    error)) {
                return false;
            }
            image->set_file_id(file_id);
            image->clear_image_content();
            break;
        }
        case ahwei_im::FILE: {
            if (!content->has_file_message()) {
                error = "FILE message is missing file_message";
                return false;
            }
            auto* file = content->mutable_file_message();
            const auto actual_size = static_cast<std::int64_t>(
                file->file_contents().size());
            if (!file_client_->put(
                    message.message_id(),
                    file->file_name(),
                    file->file_contents(),
                    file_id,
                    error)) {
                return false;
            }
            file->set_file_id(file_id);
            file->set_file_size(actual_size);
            file->clear_file_contents();
            break;
        }
        case ahwei_im::SPEECH: {
            if (!content->has_speech_message()) {
                error = "SPEECH message is missing speech_message";
                return false;
            }
            auto* speech = content->mutable_speech_message();
            if (!file_client_->put(
                    message.message_id(),
                    "",
                    speech->file_contents(),
                    file_id,
                    error)) {
                return false;
            }
            speech->set_file_id(file_id);
            speech->clear_file_contents();
            break;
        }
        default:
            error = "unsupported message type";
            return false;
    }

    if (!repository_->append(message, error)) {
        spdlog::warn(
            "store message {} failed: {}",
            message.message_id(),
            error);
        return false;
    }
    return true;
}

// 接收一段原始二进制数据，然后解析成messageInfo，再交给store_message()
bool MessageServiceImpl::on_message(
    const void* data,
    std::size_t size,
    std::string& error) {
    ahwei_im::MessageInfo message;
    if (data == nullptr || size == 0 ||
        size > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        !message.ParseFromArray(data, static_cast<int>(size))) {
        error = "cannot parse MessageInfo";
        return false;
    }
    return store_message(std::move(message), error);
}

void MessageServiceImpl::GetHistoryMsg(
    google::protobuf::RpcController*,
    const ahwei_im::GetHistoryMsgReq* request,
    ahwei_im::GetHistoryMsgRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    if (request->chat_session_id().empty()) {
        set_error(
            response,
            request->request_id(),
            "chat_session_id cannot be empty");
        return;
    }
    if (request->start_time() > request->over_time()) {
        set_error(
            response,
            request->request_id(),
            "start_time cannot be greater than over_time");
        return;
    }

    auto messages = repository_->range(
        request->chat_session_id(),
        request->start_time(),
        request->over_time());
    std::string error;
    if (!hydrate_file_contents(request->request_id(), messages, error)) {
        set_error(response, request->request_id(), error);
        return;
    }
    set_messages(response, request->request_id(), std::move(messages));
}

void MessageServiceImpl::GetRecentMsg(
    google::protobuf::RpcController*,
    const ahwei_im::GetRecentMsgReq* request,
    ahwei_im::GetRecentMsgRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    if (request->chat_session_id().empty()) {
        set_error(
            response,
            request->request_id(),
            "chat_session_id cannot be empty");
        return;
    }
    if (request->msg_count() < 0) {
        set_error(
            response,
            request->request_id(),
            "msg_count cannot be negative");
        return;
    }

    auto messages = repository_->recent(
        request->chat_session_id(),
        static_cast<std::size_t>(request->msg_count()),
        request->cur_time());
    std::string error;
    if (!hydrate_file_contents(request->request_id(), messages, error)) {
        set_error(response, request->request_id(), error);
        return;
    }
    set_messages(response, request->request_id(), std::move(messages));
}

void MessageServiceImpl::MsgSearch(
    google::protobuf::RpcController*,
    const ahwei_im::MsgSearchReq* request,
    ahwei_im::MsgSearchRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    if (request->chat_session_id().empty()) {
        set_error(
            response,
            request->request_id(),
            "chat_session_id cannot be empty");
        return;
    }

    auto messages = repository_->search(
        request->chat_session_id(),
        request->search_key());
    set_messages(response, request->request_id(), std::move(messages));
}

void MessageServiceImpl::StoreMessage(
    google::protobuf::RpcController*,
    const ahwei_im::StoreMessageReq* request,
    ahwei_im::StoreMessageRsp* response,
    google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    response->set_request_id(request->request_id());
    if (!request->has_message()) {
        response->set_success(false);
        response->set_errmsg("message cannot be empty");
        return;
    }

    std::string error;
    if (!store_message(request->message(), error)) {
        response->set_success(false);
        response->set_errmsg(error);
        return;
    }
    response->set_success(true);
}

bool MessageServiceImpl::hydrate_file_contents(
    const std::string& request_id,
    std::vector<ahwei_im::MessageInfo>& messages,
    std::string& error) const {
    std::vector<std::string> file_ids;
    std::unordered_set<std::string> unique_ids;
    for (const auto& message : messages) {
        std::string file_id;
        switch (message.message().message_type()) {
            case ahwei_im::STRING:
                continue;
            case ahwei_im::IMAGE:
                file_id = message.message().image_message().file_id();
                break;
            case ahwei_im::FILE:
                file_id = message.message().file_message().file_id();
                break;
            case ahwei_im::SPEECH:
                file_id = message.message().speech_message().file_id();
                break;
            default:
                error = "stored message has an unsupported type";
                return false;
        }
        if (file_id.empty()) {
            error = "stored file message is missing file_id";
            return false;
        }
        if (unique_ids.insert(file_id).second) {
            file_ids.push_back(std::move(file_id));
        }
    }

    std::unordered_map<std::string, std::string> files;
    if (!file_client_->get_multi(request_id, file_ids, files, error)) {
        if (error.empty()) {
            error = "cannot download message files";
        }
        return false;
    }

    for (auto& message : messages) {
        auto* content = message.mutable_message();
        std::string file_id;
        switch (content->message_type()) {
            case ahwei_im::STRING:
                continue;
            case ahwei_im::IMAGE:
                file_id = content->image_message().file_id();
                break;
            case ahwei_im::FILE:
                file_id = content->file_message().file_id();
                break;
            case ahwei_im::SPEECH:
                file_id = content->speech_message().file_id();
                break;
            default:
                error = "stored message has an unsupported type";
                return false;
        }
        const auto file = files.find(file_id);
        if (file == files.end()) {
            error = "FileServer did not return file_id: " + file_id;
            return false;
        }

        switch (content->message_type()) {
            case ahwei_im::IMAGE:
                content->mutable_image_message()->set_image_content(
                    file->second);
                break;
            case ahwei_im::FILE:
                content->mutable_file_message()->set_file_contents(
                    file->second);
                break;
            case ahwei_im::SPEECH:
                content->mutable_speech_message()->set_file_contents(
                    file->second);
                break;
            default:
                break;
        }
    }
    return true;
}

}  // namespace chat::message
