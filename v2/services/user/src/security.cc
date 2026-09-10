#include "user/security.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <spdlog/spdlog.h>

#include <array>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace chat::user {
namespace {

constexpr int kSaltSize = 16;
constexpr int kHashSize = 32;
constexpr std::uint32_t kPasswordIterations = 120000;

bool derive_key(
    const std::string& password,
    const std::string& salt,
    std::uint32_t iterations,
    std::string& result) {
    if (iterations == 0 || iterations > 10000000U || salt.empty()) {
        return false;
    }
    result.resize(kHashSize);
    const int status = PKCS5_PBKDF2_HMAC(
        password.data(),
        static_cast<int>(password.size()),
        reinterpret_cast<const unsigned char*>(salt.data()),
        static_cast<int>(salt.size()),
        static_cast<int>(iterations),
        EVP_sha256(),
        kHashSize,
        reinterpret_cast<unsigned char*>(result.data()));
    if (status != 1) {
        result.clear();
        return false;
    }
    return true;
}

}  // namespace

bool PasswordHasher::hash(
    const std::string& password,
    PasswordDigest& digest,
    std::string& error) {
    digest = {};
    error.clear();

    digest.salt.resize(kSaltSize);
    if (RAND_bytes(
            reinterpret_cast<unsigned char*>(digest.salt.data()),
            kSaltSize) != 1) {
        digest = {};
        error = "cannot generate password salt";
        return false;
    }
    digest.iterations = kPasswordIterations;
    if (!derive_key(
            password,
            digest.salt,
            digest.iterations,
            digest.hash)) {
        digest = {};
        error = "cannot hash password";
        return false;
    }
    return true;
}

bool PasswordHasher::verify(
    const std::string& password,
    const PasswordDigest& digest) {
    if (digest.hash.size() != kHashSize) {
        return false;
    }
    std::string actual;
    if (!derive_key(password, digest.salt, digest.iterations, actual)) {
        return false;
    }
    return CRYPTO_memcmp(
               actual.data(),
               digest.hash.data(),
               digest.hash.size()) == 0;
}

bool LoggingVerificationCodeSender::send(
    const std::string& phone,
    const std::string& code,
    std::string& error) {
    error.clear();
    spdlog::warn(
        "development verification code: phone={}, code={}",
        phone,
        code);
    return true;
}

VerificationCodeManager::VerificationCodeManager(
    std::shared_ptr<VerificationCodeSender> sender,
    std::chrono::seconds ttl,
    CodeGenerator code_generator,
    Clock clock)
    : sender_(std::move(sender)),
      ttl_(ttl),
      code_generator_(code_generator ? std::move(code_generator) : make_code),
      clock_(clock ? std::move(clock) : std::chrono::steady_clock::now) {
    if (!sender_) {
        throw std::invalid_argument("VerificationCodeSender cannot be null");
    }
    if (ttl_ <= std::chrono::seconds::zero()) {
        throw std::invalid_argument("verification code TTL must be positive");
    }
}

bool VerificationCodeManager::issue(
    const std::string& phone,
    std::string& code_id,
    std::string& error) {
    code_id.clear();
    error.clear();
    const std::string code = code_generator_();
    if (code.size() != 4 ||
        code.find_first_not_of("0123456789") != std::string::npos) {
        error = "verification code generator returned invalid code";
        return false;
    }
    if (!sender_->send(phone, code, error)) {
        if (error.empty()) {
            error = "cannot send verification code";
        }
        return false;
    }

    code_id = make_random_id();
    std::lock_guard<std::mutex> lock(mutex_);
    codes_[code_id] = Entry{phone, code, clock_() + ttl_};
    return true;
}

bool VerificationCodeManager::consume(
    const std::string& phone,
    const std::string& code_id,
    const std::string& code,
    std::string& error) {
    error.clear();
    std::lock_guard<std::mutex> lock(mutex_);
    const auto item = codes_.find(code_id);
    if (item == codes_.end()) {
        error = "verification code is invalid or expired";
        return false;
    }
    if (clock_() >= item->second.expires_at) {
        codes_.erase(item);
        error = "verification code is invalid or expired";
        return false;
    }
    if (item->second.phone != phone || item->second.code != code) {
        error = "verification code is incorrect";
        return false;
    }
    codes_.erase(item);
    return true;
}

std::string VerificationCodeManager::make_code() {
    thread_local std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<int> distribution(0, 9);
    std::string code;
    code.reserve(4);
    for (int index = 0; index < 4; ++index) {
        code.push_back(static_cast<char>('0' + distribution(generator)));
    }
    return code;
}

SessionManager::SessionManager(std::chrono::seconds ttl, Clock clock)
    : ttl_(ttl),
      clock_(clock ? std::move(clock) : std::chrono::steady_clock::now) {
    if (ttl_ <= std::chrono::seconds::zero()) {
        throw std::invalid_argument("session TTL must be positive");
    }
}

bool SessionManager::login(
    const std::string& user_id,
    std::string& session_id,
    std::string& error) {
    session_id.clear();
    error.clear();
    if (user_id.empty()) {
        error = "user_id cannot be empty";
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    remove_expired_user_locked(user_id);
    if (active_users_.find(user_id) != active_users_.end()) {
        error = "user is already logged in";
        return false;
    }
    session_id = make_random_id();
    sessions_[session_id] = Entry{user_id, clock_() + ttl_};
    active_users_[user_id] = session_id;
    return true;
}

bool SessionManager::resolve(
    const std::string& session_id,
    std::string& user_id,
    std::string& error) {
    user_id.clear();
    error.clear();
    std::lock_guard<std::mutex> lock(mutex_);
    const auto item = sessions_.find(session_id);
    if (item == sessions_.end()) {
        error = "session is invalid or expired";
        return false;
    }
    if (clock_() >= item->second.expires_at) {
        active_users_.erase(item->second.user_id);
        sessions_.erase(item);
        error = "session is invalid or expired";
        return false;
    }
    user_id = item->second.user_id;
    return true;
}

bool SessionManager::revoke(
    const std::string& session_id,
    std::string& error) {
    error.clear();
    std::lock_guard<std::mutex> lock(mutex_);
    const auto item = sessions_.find(session_id);
    if (item == sessions_.end()) {
        error = "session is invalid or expired";
        return false;
    }
    active_users_.erase(item->second.user_id);
    sessions_.erase(item);
    return true;
}

void SessionManager::remove_expired_user_locked(const std::string& user_id) {
    const auto active = active_users_.find(user_id);
    if (active == active_users_.end()) {
        return;
    }
    const auto session = sessions_.find(active->second);
    if (session == sessions_.end() || clock_() >= session->second.expires_at) {
        if (session != sessions_.end()) {
            sessions_.erase(session);
        }
        active_users_.erase(active);
    }
}

std::string make_random_id() {
    std::array<unsigned char, 16> bytes{};
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) {
        throw std::runtime_error("cannot generate random id");
    }
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0fU) | 0x40U);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3fU) | 0x80U);

    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) {
            result << '-';
        }
        result << std::setw(2) << static_cast<int>(bytes[index]);
    }
    return result.str();
}

}  // namespace chat::user
