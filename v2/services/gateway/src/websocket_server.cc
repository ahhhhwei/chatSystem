#include "gateway/websocket_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <sstream>
#include <utility>

namespace chat::gateway {

struct WebSocketServer::Connection {
    explicit Connection(int socket_fd) : fd(socket_fd) {}

    int fd;
    std::mutex write_mutex;
    std::atomic<bool> closing{false};
    std::atomic<bool> authenticated{false};
    std::atomic<bool> revoke_on_close{true};
    std::string user_id;
    std::string session_id;
};

struct WebSocketServer::Frame {
    std::uint8_t opcode = 0;
    bool final = false;
    std::string payload;
};

namespace {

constexpr const char* kWebSocketGuid =
    "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

bool send_all(int fd, const char* data, std::size_t size) {
    std::size_t sent = 0;
    while (sent < size) {
        const auto result = ::send(
            fd,
            data + sent,
            size - sent,
            MSG_NOSIGNAL);
        if (result > 0) {
            sent += static_cast<std::size_t>(result);
            continue;
        }
        if (result < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

bool receive_all(int fd, char* data, std::size_t size) {
    std::size_t received = 0;
    while (received < size) {
        const auto result = ::recv(fd, data + received, size - received, 0);
        if (result > 0) {
            received += static_cast<std::size_t>(result);
            continue;
        }
        if (result < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string lower(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}

std::string websocket_accept_key(const std::string& client_key) {
    const std::string source = client_key + kWebSocketGuid;
    std::array<unsigned char, SHA_DIGEST_LENGTH> digest{};
    SHA1(
        reinterpret_cast<const unsigned char*>(source.data()),
        source.size(),
        digest.data());
    std::array<unsigned char, 4 * ((SHA_DIGEST_LENGTH + 2) / 3) + 1> encoded{};
    const int size = EVP_EncodeBlock(
        encoded.data(), digest.data(), SHA_DIGEST_LENGTH);
    return std::string(
        reinterpret_cast<const char*>(encoded.data()),
        static_cast<std::size_t>(size));
}

std::string close_payload(std::uint16_t code, const std::string& reason) {
    const std::size_t reason_size = std::min<std::size_t>(reason.size(), 123U);
    std::string payload(2U + reason_size, '\0');
    payload[0] = static_cast<char>((code >> 8U) & 0xffU);
    payload[1] = static_cast<char>(code & 0xffU);
    std::copy_n(reason.data(), reason_size, payload.data() + 2);
    return payload;
}

}  // namespace

WebSocketServer::WebSocketServer(
    std::string listen_address,
    std::uint16_t port,
    std::size_t max_incoming_frame_size)
    : listen_address_(std::move(listen_address)),
      configured_port_(port),
      max_incoming_frame_size_(max_incoming_frame_size) {}

WebSocketServer::~WebSocketServer() {
    stop();
}

void WebSocketServer::set_authenticator(Authenticator authenticator) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    authenticator_ = std::move(authenticator);
}

void WebSocketServer::set_disconnect_handler(DisconnectHandler handler) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    disconnect_handler_ = std::move(handler);
}

void WebSocketServer::set_presence_handler(PresenceHandler handler) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    presence_handler_ = std::move(handler);
}

bool WebSocketServer::start(std::string& error) {
    if (running_.exchange(true)) {
        error = "WebSocket server is already running";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        if (!authenticator_) {
            running_ = false;
            error = "WebSocket authenticator is not configured";
            return false;
        }
    }

    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        running_ = false;
        error = std::string("cannot create WebSocket socket: ") +
            std::strerror(errno);
        return false;
    }
    const int reuse = 1;
    ::setsockopt(
        listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(configured_port_);
    if (::inet_pton(
            AF_INET, listen_address_.c_str(), &address.sin_addr) != 1) {
        error = "invalid WebSocket listen address: " + listen_address_;
        ::close(listen_fd_);
        listen_fd_ = -1;
        running_ = false;
        return false;
    }
    if (::bind(
            listen_fd_,
            reinterpret_cast<const sockaddr*>(&address),
            sizeof(address)) != 0) {
        error = std::string("cannot bind WebSocket socket: ") +
            std::strerror(errno);
        ::close(listen_fd_);
        listen_fd_ = -1;
        running_ = false;
        return false;
    }
    if (::listen(listen_fd_, SOMAXCONN) != 0) {
        error = std::string("cannot listen on WebSocket socket: ") +
            std::strerror(errno);
        ::close(listen_fd_);
        listen_fd_ = -1;
        running_ = false;
        return false;
    }
    sockaddr_in bound{};
    socklen_t bound_size = sizeof(bound);
    if (::getsockname(
            listen_fd_,
            reinterpret_cast<sockaddr*>(&bound),
            &bound_size) == 0) {
        bound_port_ = ntohs(bound.sin_port);
    }
    accept_thread_ = std::thread(&WebSocketServer::accept_loop, this);
    error.clear();
    return true;
}

void WebSocketServer::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    std::vector<std::shared_ptr<Connection>> connections;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (const auto& item : connections_) {
            item.second->revoke_on_close = false;
            item.second->closing = true;
            connections.push_back(item.second);
        }
    }
    for (const auto& connection : connections) {
        ::shutdown(connection->fd, SHUT_RDWR);
    }
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    std::vector<std::thread> threads;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        threads.swap(connection_threads_);
    }
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_.clear();
        online_users_.clear();
    }
    bound_port_ = 0;
}

bool WebSocketServer::send(
    const std::string& user_id,
    const ahwei_im::NotifyMessage& notification) {
    std::shared_ptr<Connection> connection;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        const auto iterator = online_users_.find(user_id);
        if (iterator == online_users_.end()) {
            return false;
        }
        connection = iterator->second;
    }
    if (!send_frame(connection, 0x2U, notification.SerializeAsString())) {
        ::shutdown(connection->fd, SHUT_RDWR);
        return false;
    }
    return true;
}

bool WebSocketServer::running() const {
    return running_;
}

std::uint16_t WebSocketServer::port() const {
    return bound_port_;
}

std::size_t WebSocketServer::online_count() const {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    return online_users_.size();
}

void WebSocketServer::accept_loop() {
    while (running_) {
        const int fd = ::accept(listen_fd_, nullptr, nullptr);
        if (fd < 0) {
            if (running_ && errno != EINTR) {
                spdlog::warn(
                    "WebSocket accept failed: {}", std::strerror(errno));
            }
            continue;
        }
        auto connection = std::make_shared<Connection>(fd);
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_[fd] = connection;
        connection_threads_.emplace_back(
            &WebSocketServer::serve_connection, this, connection);
    }
}

void WebSocketServer::serve_connection(
    const std::shared_ptr<Connection>& connection) {
    if (!perform_handshake(connection)) {
        remove_connection(connection);
        return;
    }

    auto last_ping = std::chrono::steady_clock::now();
    while (running_ && !connection->closing) {
        pollfd descriptor{connection->fd, POLLIN, 0};
        const int result = ::poll(&descriptor, 1, 1000);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (result == 0) {
            const auto now = std::chrono::steady_clock::now();
            if (now - last_ping >= std::chrono::seconds(60)) {
                if (!send_frame(connection, 0x9U, {})) {
                    break;
                }
                last_ping = now;
            }
            continue;
        }
        if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            break;
        }

        Frame frame;
        if (!read_frame(connection, frame)) {
            break;
        }
        if (!frame.final && frame.opcode != 0x0U) {
            close_connection(connection, 1003U, "fragmented frames unsupported");
            break;
        }
        if (frame.opcode == 0x8U) {
            send_frame(connection, 0x8U, frame.payload);
            break;
        }
        if (frame.opcode == 0x9U) {
            if (!send_frame(connection, 0xAU, frame.payload)) {
                break;
            }
            continue;
        }
        if (frame.opcode == 0xAU) {
            continue;
        }
        if (frame.opcode != 0x2U) {
            close_connection(connection, 1003U, "binary messages required");
            break;
        }
        if (connection->authenticated) {
            close_connection(connection, 1008U, "already authenticated");
            break;
        }

        Authenticator authenticator;
        {
            std::lock_guard<std::mutex> lock(callbacks_mutex_);
            authenticator = authenticator_;
        }
        std::string user_id;
        std::string session_id;
        std::string error;
        if (!authenticator || !authenticator(
                frame.payload, user_id, session_id, error)) {
            close_connection(
                connection,
                1008U,
                error.empty() ? "authentication failed" : error);
            break;
        }
        bind_authenticated(
            connection, std::move(user_id), std::move(session_id));
    }
    remove_connection(connection);
}

bool WebSocketServer::perform_handshake(
    const std::shared_ptr<Connection>& connection) {
    constexpr std::size_t kMaximumHeaderSize = 16U * 1024U;
    std::string request;
    std::array<char, 2048> buffer{};
    while (request.find("\r\n\r\n") == std::string::npos) {
        const auto size = ::recv(
            connection->fd, buffer.data(), buffer.size(), 0);
        if (size <= 0) {
            return false;
        }
        request.append(buffer.data(), static_cast<std::size_t>(size));
        if (request.size() > kMaximumHeaderSize) {
            return false;
        }
    }

    std::istringstream stream(request);
    std::string request_line;
    std::getline(stream, request_line);
    if (request_line.rfind("GET ", 0) != 0) {
        return false;
    }
    std::string key;
    bool upgrade = false;
    bool connection_upgrade = false;
    bool supported_version = false;
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            break;
        }
        const auto separator = line.find(':');
        if (separator == std::string::npos) {
            continue;
        }
        const auto name = lower(trim(line.substr(0, separator)));
        const auto value = trim(line.substr(separator + 1));
        const auto lowered_value = lower(value);
        if (name == "sec-websocket-key") {
            key = value;
        } else if (name == "sec-websocket-version" && value == "13") {
            supported_version = true;
        } else if (name == "upgrade" && lowered_value == "websocket") {
            upgrade = true;
        } else if (name == "connection" &&
                   lowered_value.find("upgrade") != std::string::npos) {
            connection_upgrade = true;
        }
    }
    if (key.empty() || !upgrade || !connection_upgrade || !supported_version) {
        return false;
    }

    const std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " +
        websocket_accept_key(key) + "\r\n\r\n";
    return send_all(connection->fd, response.data(), response.size());
}

bool WebSocketServer::read_frame(
    const std::shared_ptr<Connection>& connection,
    Frame& frame) {
    std::array<unsigned char, 2> initial{};
    if (!receive_all(
            connection->fd,
            reinterpret_cast<char*>(initial.data()),
            initial.size())) {
        return false;
    }
    frame.final = (initial[0] & 0x80U) != 0;
    const bool reserved_bits = (initial[0] & 0x70U) != 0;
    frame.opcode = initial[0] & 0x0fU;
    const bool masked = (initial[1] & 0x80U) != 0;
    std::uint64_t payload_size = initial[1] & 0x7fU;
    if (reserved_bits || !masked) {
        return false;
    }
    if (payload_size == 126U) {
        std::array<unsigned char, 2> extended{};
        if (!receive_all(
                connection->fd,
                reinterpret_cast<char*>(extended.data()),
                extended.size())) {
            return false;
        }
        payload_size =
            (static_cast<std::uint64_t>(extended[0]) << 8U) |
            static_cast<std::uint64_t>(extended[1]);
    } else if (payload_size == 127U) {
        std::array<unsigned char, 8> extended{};
        if (!receive_all(
                connection->fd,
                reinterpret_cast<char*>(extended.data()),
                extended.size())) {
            return false;
        }
        payload_size = 0;
        for (const auto byte : extended) {
            payload_size = (payload_size << 8U) | byte;
        }
    }
    const bool control_frame = frame.opcode >= 0x8U;
    if ((control_frame && payload_size > 125U) ||
        payload_size > max_incoming_frame_size_ ||
        payload_size > std::numeric_limits<std::size_t>::max()) {
        return false;
    }

    std::array<unsigned char, 4> mask{};
    if (!receive_all(
            connection->fd,
            reinterpret_cast<char*>(mask.data()),
            mask.size())) {
        return false;
    }
    frame.payload.resize(static_cast<std::size_t>(payload_size));
    if (payload_size > 0 && !receive_all(
            connection->fd,
            frame.payload.data(),
            frame.payload.size())) {
        return false;
    }
    for (std::size_t index = 0; index < frame.payload.size(); ++index) {
        frame.payload[index] = static_cast<char>(
            static_cast<unsigned char>(frame.payload[index]) ^
            mask[index % mask.size()]);
    }
    return true;
}

bool WebSocketServer::send_frame(
    const std::shared_ptr<Connection>& connection,
    std::uint8_t opcode,
    const std::string& payload) {
    if (!connection || connection->closing) {
        return false;
    }
    std::string header;
    header.push_back(static_cast<char>(0x80U | (opcode & 0x0fU)));
    if (payload.size() <= 125U) {
        header.push_back(static_cast<char>(payload.size()));
    } else if (payload.size() <= 0xffffU) {
        header.push_back(static_cast<char>(126U));
        header.push_back(static_cast<char>((payload.size() >> 8U) & 0xffU));
        header.push_back(static_cast<char>(payload.size() & 0xffU));
    } else {
        header.push_back(static_cast<char>(127U));
        const auto size = static_cast<std::uint64_t>(payload.size());
        for (int shift = 56; shift >= 0; shift -= 8) {
            header.push_back(static_cast<char>((size >> shift) & 0xffU));
        }
    }

    std::lock_guard<std::mutex> lock(connection->write_mutex);
    if (connection->closing) {
        return false;
    }
    return send_all(connection->fd, header.data(), header.size()) &&
        send_all(connection->fd, payload.data(), payload.size());
}

void WebSocketServer::close_connection(
    const std::shared_ptr<Connection>& connection,
    std::uint16_t code,
    const std::string& reason) {
    if (!connection->closing.exchange(true)) {
        const auto payload = close_payload(code, reason);
        std::string header;
        header.push_back(static_cast<char>(0x88U));
        header.push_back(static_cast<char>(payload.size()));
        std::lock_guard<std::mutex> lock(connection->write_mutex);
        send_all(connection->fd, header.data(), header.size());
        send_all(connection->fd, payload.data(), payload.size());
    }
    ::shutdown(connection->fd, SHUT_RDWR);
}

void WebSocketServer::bind_authenticated(
    const std::shared_ptr<Connection>& connection,
    std::string user_id,
    std::string session_id) {
    std::shared_ptr<Connection> previous;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        const auto iterator = online_users_.find(user_id);
        if (iterator != online_users_.end() &&
            iterator->second.get() != connection.get()) {
            previous = iterator->second;
            previous->revoke_on_close = false;
        }
        connection->user_id = std::move(user_id);
        connection->session_id = std::move(session_id);
        connection->authenticated = true;
        online_users_[connection->user_id] = connection;
    }
    if (previous) {
        close_connection(previous, 1008U, "replaced by a newer connection");
    }
    PresenceHandler presence;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        presence = presence_handler_;
    }
    if (presence) presence(connection->user_id, connection->session_id, true);
    spdlog::info("WebSocket user online: {}", connection->user_id);
}

void WebSocketServer::remove_connection(
    const std::shared_ptr<Connection>& connection) {
    connection->closing = true;
    std::string session_id;
    bool revoke = false;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_.erase(connection->fd);
        if (connection->authenticated) {
            const auto iterator = online_users_.find(connection->user_id);
            if (iterator != online_users_.end() &&
                iterator->second.get() == connection.get()) {
                online_users_.erase(iterator);
            }
            session_id = connection->session_id;
            revoke = connection->revoke_on_close;
        }
    }
    ::shutdown(connection->fd, SHUT_RDWR);
    ::close(connection->fd);

    DisconnectHandler handler;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        handler = disconnect_handler_;
    }
    if (revoke && handler) {
        handler(session_id);
    }
    PresenceHandler presence;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        presence = presence_handler_;
    }
    if (connection->authenticated && presence) {
        presence(connection->user_id, connection->session_id, false);
    }
    if (connection->authenticated) {
        spdlog::info("WebSocket user offline: {}", connection->user_id);
    }
}

}  // namespace chat::gateway
