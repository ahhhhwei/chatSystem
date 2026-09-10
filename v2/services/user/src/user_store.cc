#include "user/user_store.hpp"

#include <array>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace chat::user {
namespace {

constexpr std::uint32_t kMaximumRecordSize = 1024U * 1024U;

std::array<char, 4> encode_size(std::uint32_t size) {
    return {
        static_cast<char>((size >> 24U) & 0xffU),
        static_cast<char>((size >> 16U) & 0xffU),
        static_cast<char>((size >> 8U) & 0xffU),
        static_cast<char>(size & 0xffU),
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

UserStore::UserStore(std::filesystem::path storage_file)
    : storage_file_(std::move(storage_file)) {
    if (storage_file_.empty()) {
        throw std::invalid_argument("user storage file cannot be empty");
    }

    std::error_code error;
    const auto parent = storage_file_.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            throw std::runtime_error(
                "cannot create user storage directory: " + error.message());
        }
    }
    load();
}

bool UserStore::insert(const UserRecord& user, std::string& error) {
    error.clear();
    if (!validate(user, error)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (users_.find(user.user_id()) != users_.end()) {
        error = "duplicate user_id";
        return false;
    }
    if (!check_unique_locked(user, {}, error) ||
        !append_locked(user, error)) {
        return false;
    }

    replace_indexes_locked(std::nullopt, user);
    users_.emplace(user.user_id(), user);
    return true;
}

bool UserStore::update(const UserRecord& user, std::string& error) {
    error.clear();
    if (!validate(user, error)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto current = users_.find(user.user_id());
    if (current == users_.end()) {
        error = "user not found";
        return false;
    }
    if (!check_unique_locked(user, user.user_id(), error) ||
        !append_locked(user, error)) {
        return false;
    }

    const UserRecord old_user = current->second;
    replace_indexes_locked(old_user, user);
    current->second = user;
    return true;
}

std::optional<UserRecord> UserStore::find_by_id(
    std::string_view user_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto item = users_.find(std::string(user_id));
    if (item == users_.end()) {
        return std::nullopt;
    }
    return item->second;
}

std::optional<UserRecord> UserStore::find_by_nickname(
    std::string_view nickname) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto index = nickname_index_.find(std::string(nickname));
    if (index == nickname_index_.end()) {
        return std::nullopt;
    }
    const auto user = users_.find(index->second);
    return user == users_.end()
        ? std::optional<UserRecord>{}
        : std::optional<UserRecord>{user->second};
}

std::optional<UserRecord> UserStore::find_by_phone(
    std::string_view phone) const {
    if (phone.empty()) {
        return std::nullopt;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto index = phone_index_.find(std::string(phone));
    if (index == phone_index_.end()) {
        return std::nullopt;
    }
    const auto user = users_.find(index->second);
    return user == users_.end()
        ? std::optional<UserRecord>{}
        : std::optional<UserRecord>{user->second};
}

std::vector<UserRecord> UserStore::find_multi(
    const std::vector<std::string>& user_ids) const {
    std::vector<UserRecord> result;
    std::unordered_set<std::string> seen;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& user_id : user_ids) {
        if (!seen.insert(user_id).second) {
            continue;
        }
        const auto item = users_.find(user_id);
        if (item != users_.end()) {
            result.push_back(item->second);
        }
    }
    return result;
}

std::vector<UserRecord> UserStore::search(
    std::string_view search_key,
    const std::vector<std::string>& excluded_user_ids,
    std::size_t limit) const {
    std::vector<UserRecord> result;
    if (limit == 0) {
        return result;
    }
    const std::unordered_set<std::string> excluded(
        excluded_user_ids.begin(), excluded_user_ids.end());
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& item : users_) {
        const auto& user = item.second;
        if (excluded.find(user.user_id()) != excluded.end()) {
            continue;
        }
        if (user.user_id().find(search_key) != std::string::npos ||
            user.nickname().find(search_key) != std::string::npos ||
            user.phone().find(search_key) != std::string::npos) {
            result.push_back(user);
        }
    }
    std::sort(
        result.begin(),
        result.end(),
        [](const UserRecord& left, const UserRecord& right) {
            return left.user_id() < right.user_id();
        });
    if (result.size() > limit) {
        result.resize(limit);
    }
    return result;
}

std::size_t UserStore::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return users_.size();
}

const std::filesystem::path& UserStore::storage_file() const noexcept {
    return storage_file_;
}

bool UserStore::validate(const UserRecord& user, std::string& error) {
    if (user.user_id().empty() || user.user_id().size() > 128) {
        error = "invalid user_id";
        return false;
    }
    if (user.nickname().empty() || user.nickname().size() > 128) {
        error = "invalid nickname";
        return false;
    }
    if (user.description().size() > 4096) {
        error = "description is too long";
        return false;
    }
    if (user.phone().size() > 32 || user.avatar_file_id().size() > 128) {
        error = "user field is too long";
        return false;
    }
    const bool has_salt = !user.password_salt().empty();
    const bool has_hash = !user.password_hash().empty();
    if (has_salt != has_hash ||
        (has_hash && user.password_iterations() == 0) ||
        (!has_hash && user.password_iterations() != 0)) {
        error = "invalid password digest";
        return false;
    }
    return true;
}

bool UserStore::append_locked(const UserRecord& user, std::string& error) {
    std::string serialized;
    if (!user.SerializeToString(&serialized) || serialized.empty() ||
        serialized.size() > kMaximumRecordSize) {
        error = "cannot serialize user record";
        return false;
    }

    std::ofstream output(storage_file_, std::ios::binary | std::ios::app);
    if (!output) {
        error = "cannot open user storage file for writing";
        return false;
    }
    const auto header = encode_size(
        static_cast<std::uint32_t>(serialized.size()));
    output.write(header.data(), static_cast<std::streamsize>(header.size()));
    output.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
    output.flush();
    if (!output) {
        error = "cannot persist user record";
        return false;
    }
    return true;
}

bool UserStore::check_unique_locked(
    const UserRecord& user,
    std::string_view ignored_user_id,
    std::string& error) const {
    const auto nickname = nickname_index_.find(user.nickname());
    if (nickname != nickname_index_.end() &&
        nickname->second != ignored_user_id) {
        error = "nickname already exists";
        return false;
    }
    if (!user.phone().empty()) {
        const auto phone = phone_index_.find(user.phone());
        if (phone != phone_index_.end() && phone->second != ignored_user_id) {
            error = "phone number already exists";
            return false;
        }
    }
    return true;
}

void UserStore::replace_indexes_locked(
    const std::optional<UserRecord>& old_user,
    const UserRecord& new_user) {
    if (old_user) {
        nickname_index_.erase(old_user->nickname());
        if (!old_user->phone().empty()) {
            phone_index_.erase(old_user->phone());
        }
    }
    nickname_index_[new_user.nickname()] = new_user.user_id();
    if (!new_user.phone().empty()) {
        phone_index_[new_user.phone()] = new_user.user_id();
    }
}

void UserStore::load() {
    std::ifstream input(storage_file_, std::ios::binary);
    if (!input) {
        std::error_code error;
        if (std::filesystem::exists(storage_file_, error)) {
            throw std::runtime_error("cannot open user storage file");
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
            throw std::runtime_error("user storage file is corrupted");
        }
        std::string serialized(record_size, '\0');
        input.read(serialized.data(), static_cast<std::streamsize>(record_size));
        if (input.gcount() != static_cast<std::streamsize>(record_size)) {
            has_partial_tail = true;
            break;
        }

        UserRecord user;
        std::string error;
        if (!user.ParseFromString(serialized) || !validate(user, error)) {
            throw std::runtime_error("user storage file is corrupted");
        }

        const auto current = users_.find(user.user_id());
        if (current != users_.end()) {
            nickname_index_.erase(current->second.nickname());
            if (!current->second.phone().empty()) {
                phone_index_.erase(current->second.phone());
            }
        }
        if (!check_unique_locked(user, user.user_id(), error)) {
            throw std::runtime_error("user storage uniqueness is corrupted");
        }
        replace_indexes_locked(std::nullopt, user);
        users_[user.user_id()] = std::move(user);
        valid_size += header.size() + serialized.size();
    }
    input.close();

    if (has_partial_tail) {
        std::error_code error;
        std::filesystem::resize_file(storage_file_, valid_size, error);
        if (error) {
            throw std::runtime_error(
                "cannot recover partial user record: " + error.message());
        }
    }
}

}  // namespace chat::user
