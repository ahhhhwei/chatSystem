#include "message/message_store.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace chat::message {
namespace {

constexpr std::uint32_t kMaximumRecordSize = 64U * 1024U * 1024U;

bool chronological(
    const ahwei_im::MessageInfo& left,
    const ahwei_im::MessageInfo& right) {
    if (left.timestamp() != right.timestamp()) {
        return left.timestamp() < right.timestamp();
    }
    return left.message_id() < right.message_id();
}

// 把一个32位整数size拆成4字节，并且按大端序存入 std::array<char, 4>（一个固定长度为4的数组，每个元素类型为char
// 大端序：高位字节放在低地址
// 小端序：低位字节放在低地址
std::array<char, 4> encode_size(std::uint32_t size) {
    return {
        // U是整数字面量的后缀，表示这是一个unsigned int，即无符号整数
        static_cast<char>((size >> 24U) & 0xffU), // 取最高8位 （32位整数右移24位，然后留下最低8位）
        static_cast<char>((size >> 16U) & 0xffU), // 取次高8位
        static_cast<char>((size >> 8U) & 0xffU),  // 取次次高8位
        static_cast<char>(size & 0xffU),          // 取末8位  
    };
}

std::uint32_t decode_size(const std::array<char, 4>& bytes) {
    return
        (static_cast<std::uint32_t>(
             static_cast<unsigned char>(bytes[0])) << 24U) |
        (static_cast<std::uint32_t>(
             static_cast<unsigned char>(bytes[1])) << 16U) |
        (static_cast<std::uint32_t>(
             static_cast<unsigned char>(bytes[2])) << 8U) |
        static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[3]));
}

}  // namespace

MessageStore::MessageStore(std::filesystem::path storage_file)
    : storage_file_(std::move(storage_file)) {
    if (storage_file_.empty()) {
        throw std::invalid_argument("message storage file cannot be empty");
    }

    std::error_code error;
    const auto parent = storage_file_.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            throw std::runtime_error(
                "cannot create message storage directory: " +
                error.message());
        }
    }
    load();
}

bool MessageStore::append(
    const ahwei_im::MessageInfo& message,
    std::string& error) {
    error.clear();
    // 收到消息后，检查消息是否合法
    if (!validate(message, error)) {
        return false;
    }

    // 序列化
    std::string serialized;
    if (!message.SerializeToString(&serialized)) {
        error = "cannot serialize message";
        return false;
    }
    if (serialized.empty() || serialized.size() > kMaximumRecordSize) {
        error = "serialized message is too large";
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (message_ids_.find(message.message_id()) != message_ids_.end()) {
        error = "duplicate message_id";
        return false;
    }

    // 写入文件
    std::ofstream output(
        storage_file_,
        std::ios::binary | std::ios::app);
    if (!output) {
        error = "cannot open message storage file for writing";
        return false;
    }

    const auto header = encode_size(
        static_cast<std::uint32_t>(serialized.size()));
    output.write(header.data(), static_cast<std::streamsize>(header.size()));
    output.write(
        serialized.data(),
        static_cast<std::streamsize>(serialized.size()));
    output.flush();
    if (!output) {
        error = "cannot persist message";
        return false;
    }

    messages_.push_back(message);
    message_ids_.insert(message.message_id());
    return true;
}

std::vector<ahwei_im::MessageInfo> MessageStore::range(
    std::string_view chat_session_id,
    std::int64_t start_time,
    std::int64_t over_time) const {
    std::vector<ahwei_im::MessageInfo> result;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& message : messages_) {
        if (message.chat_session_id() == chat_session_id &&
            message.timestamp() >= start_time &&
            message.timestamp() <= over_time) {
            result.push_back(message);
        }
    }
    std::sort(result.begin(), result.end(), chronological);
    return result;
}

std::vector<ahwei_im::MessageInfo> MessageStore::recent(
    std::string_view chat_session_id,
    std::size_t count,
    std::int64_t current_time) const {
    std::vector<ahwei_im::MessageInfo> result;
    if (count == 0) {
        return result;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& message : messages_) {
        if (message.chat_session_id() == chat_session_id &&
            (current_time <= 0 || message.timestamp() <= current_time)) {
            result.push_back(message);
        }
    }
    std::sort(result.begin(), result.end(), chronological);
    if (result.size() > count) {
        result.erase(result.begin(), result.end() - count);
    }
    return result;
}

std::vector<ahwei_im::MessageInfo> MessageStore::search(
    std::string_view chat_session_id,
    std::string_view search_key) const {
    std::vector<ahwei_im::MessageInfo> result;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& message : messages_) {
        if (message.chat_session_id() != chat_session_id ||
            message.message().message_type() != ahwei_im::STRING ||
            !message.message().has_string_message()) {
            continue;
        }

        const auto& content = message.message().string_message().content();
        if (content.find(search_key) != std::string::npos) {
            result.push_back(message);
        }
    }
    std::sort(result.begin(), result.end(), chronological);
    return result;
}

std::size_t MessageStore::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return messages_.size();
}

const std::filesystem::path& MessageStore::storage_file() const noexcept {
    return storage_file_;
}

bool MessageStore::validate(
    const ahwei_im::MessageInfo& message,
    std::string& error) {
    if (message.message_id().empty()) {
        error = "message_id cannot be empty";
        return false;
    }
    if (message.message_id().size() > 128) {
        error = "message_id is too long";
        return false;
    }
    if (message.chat_session_id().empty()) {
        error = "chat_session_id cannot be empty";
        return false;
    }
    if (!message.has_sender() || message.sender().user_id().empty()) {
        error = "sender user_id cannot be empty";
        return false;
    }
    if (!message.has_message()) {
        error = "message content cannot be empty";
        return false;
    }

    const auto& content = message.message();
    switch (content.message_type()) {
        case ahwei_im::STRING:
            if (!content.has_string_message()) {
                error = "STRING message is missing string_message";
                return false;
            }
            return true;
        case ahwei_im::IMAGE:
            if (!content.has_image_message() ||
                content.image_message().file_id().empty()) {
                error = "IMAGE message is missing file_id";
                return false;
            }
            if (!content.image_message().image_content().empty()) {
                error = "IMAGE binary content must not be stored as metadata";
                return false;
            }
            return true;
        case ahwei_im::FILE:
            if (!content.has_file_message() ||
                content.file_message().file_id().empty()) {
                error = "FILE message is missing file_id";
                return false;
            }
            if (content.file_message().file_size() < 0) {
                error = "FILE size cannot be negative";
                return false;
            }
            if (!content.file_message().file_contents().empty()) {
                error = "FILE binary content must not be stored as metadata";
                return false;
            }
            return true;
        case ahwei_im::SPEECH:
            if (!content.has_speech_message() ||
                content.speech_message().file_id().empty()) {
                error = "SPEECH message is missing file_id";
                return false;
            }
            if (!content.speech_message().file_contents().empty()) {
                error = "SPEECH binary content must not be stored as metadata";
                return false;
            }
            return true;
        default:
            error = "unsupported message type";
            return false;
    }
}

void MessageStore::load() {
    std::ifstream input(storage_file_, std::ios::binary);
    if (!input) {
        std::error_code error;
        if (std::filesystem::exists(storage_file_, error)) {
            throw std::runtime_error("cannot open message storage file");
        }
        return;
    }

    std::uintmax_t valid_size = 0;
    bool has_partial_tail = false;
    while (true) {
        std::array<char, 4> header{};
        input.read(header.data(), static_cast<std::streamsize>(header.size()));
        if (input.gcount() == 0 && input.eof()) {
            break;
        }
        if (input.gcount() != static_cast<std::streamsize>(header.size())) {
            has_partial_tail = true;
            break;
        }

        const auto record_size = decode_size(header);
        if (record_size == 0 || record_size > kMaximumRecordSize) {
            throw std::runtime_error("message storage file is corrupted");
        }

        std::string serialized(record_size, '\0');
        input.read(
            serialized.data(),
            static_cast<std::streamsize>(serialized.size()));
        if (input.gcount() != static_cast<std::streamsize>(serialized.size())) {
            has_partial_tail = true;
            break;
        }

        ahwei_im::MessageInfo message;
        std::string validation_error;
        if (!message.ParseFromString(serialized) ||
            !validate(message, validation_error) ||
            !message_ids_.insert(message.message_id()).second) {
            throw std::runtime_error("message storage file is corrupted");
        }
        messages_.push_back(std::move(message));
        valid_size += header.size() + serialized.size();
    }
    input.close();

    if (has_partial_tail) {
        std::error_code error;
        std::filesystem::resize_file(storage_file_, valid_size, error);
        if (error) {
            throw std::runtime_error(
                "cannot recover partial message record: " + error.message());
        }
    }
}

}  // namespace chat::message
