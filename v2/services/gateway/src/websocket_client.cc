#include "gateway/websocket_client.hpp"

#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <sstream>

namespace chat::gateway {
namespace {

bool send_all(int fd, const char* data, std::size_t size) {
    std::size_t offset = 0;
    while (offset < size) {
        const auto sent = ::send(
            fd, data + offset, size - offset, MSG_NOSIGNAL);
        if (sent > 0) {
            offset += static_cast<std::size_t>(sent);
        } else if (sent < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

bool receive_all(int fd, char* data, std::size_t size) {
    std::size_t offset = 0;
    while (offset < size) {
        const auto received = ::recv(fd, data + offset, size - offset, 0);
        if (received > 0) {
            offset += static_cast<std::size_t>(received);
        } else if (received < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

int connect_socket(
    const std::string& host,
    std::uint16_t port,
    std::string& error) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    const std::string service = std::to_string(port);
    const int lookup = ::getaddrinfo(
        host.c_str(), service.c_str(), &hints, &addresses);
    if (lookup != 0) {
        error = ::gai_strerror(lookup);
        return -1;
    }
    int result = -1;
    for (auto* address = addresses; address; address = address->ai_next) {
        const int fd = ::socket(
            address->ai_family,
            address->ai_socktype,
            address->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (::connect(fd, address->ai_addr, address->ai_addrlen) == 0) {
            result = fd;
            break;
        }
        ::close(fd);
    }
    ::freeaddrinfo(addresses);
    if (result < 0) {
        error = std::string("cannot connect: ") + std::strerror(errno);
    }
    return result;
}

}  // namespace

WebSocketClient::~WebSocketClient() {
    close();
}

bool WebSocketClient::connect(
    const std::string& host,
    std::uint16_t port,
    std::string& error) {
    if (fd_ >= 0) {
        error = "WebSocket client is already connected";
        return false;
    }
    fd_ = connect_socket(host, port, error);
    if (fd_ < 0) {
        return false;
    }
    const std::string request =
        "GET / HTTP/1.1\r\n"
        "Host: " + host + ":" + std::to_string(port) + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";
    if (!send_all(fd_, request.data(), request.size())) {
        error = "cannot send WebSocket upgrade request";
        close();
        return false;
    }
    std::string response;
    char byte = '\0';
    while (response.find("\r\n\r\n") == std::string::npos &&
           response.size() <= 16U * 1024U) {
        if (!receive_all(fd_, &byte, 1)) {
            error = "cannot receive WebSocket upgrade response";
            close();
            return false;
        }
        response.push_back(byte);
    }
    if (response.find("HTTP/1.1 101") != 0 ||
        response.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") ==
            std::string::npos) {
        error = "Gateway rejected the WebSocket upgrade";
        close();
        return false;
    }
    error.clear();
    return true;
}

bool WebSocketClient::authenticate(
    const ahwei_im::ClientAuthenticationReq& request,
    std::string& error) {
    if (!send_frame(0x2U, request.SerializeAsString(), error) ||
        !send_frame(0x9U, "ready", error)) {
        return false;
    }
    for (;;) {
        std::uint8_t opcode = 0;
        std::string payload;
        if (!read_frame(opcode, payload, 3000, error)) {
            return false;
        }
        if (opcode == 0xAU && payload == "ready") {
            return true;
        }
        if (opcode == 0x9U) {
            if (!send_frame(0xAU, payload, error)) {
                return false;
            }
        } else if (opcode == 0x8U) {
            error = "Gateway closed WebSocket during authentication";
            return false;
        }
    }
}

bool WebSocketClient::receive_notification(
    ahwei_im::NotifyMessage& notification,
    int timeout_ms,
    std::string& error) {
    for (;;) {
        std::uint8_t opcode = 0;
        std::string payload;
        if (!read_frame(opcode, payload, timeout_ms, error)) {
            return false;
        }
        if (opcode == 0x2U) {
            if (!notification.ParseFromString(payload)) {
                error = "cannot parse NotifyMessage";
                return false;
            }
            return true;
        }
        if (opcode == 0x9U) {
            if (!send_frame(0xAU, payload, error)) {
                return false;
            }
        } else if (opcode == 0x8U) {
            error = "Gateway closed WebSocket";
            return false;
        }
    }
}

void WebSocketClient::close() {
    if (fd_ < 0) {
        return;
    }
    std::string ignored;
    const std::string payload{"\x03\xe8", 2};
    send_frame(0x8U, payload, ignored);
    ::shutdown(fd_, SHUT_RDWR);
    ::close(fd_);
    fd_ = -1;
}

bool WebSocketClient::send_frame(
    std::uint8_t opcode,
    const std::string& payload,
    std::string& error) {
    if (fd_ < 0) {
        error = "WebSocket is not connected";
        return false;
    }
    std::string frame;
    frame.push_back(static_cast<char>(0x80U | (opcode & 0x0fU)));
    if (payload.size() <= 125U) {
        frame.push_back(static_cast<char>(0x80U | payload.size()));
    } else if (payload.size() <= 0xffffU) {
        frame.push_back(static_cast<char>(0x80U | 126U));
        frame.push_back(static_cast<char>((payload.size() >> 8U) & 0xffU));
        frame.push_back(static_cast<char>(payload.size() & 0xffU));
    } else {
        frame.push_back(static_cast<char>(0x80U | 127U));
        const auto size = static_cast<std::uint64_t>(payload.size());
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame.push_back(static_cast<char>((size >> shift) & 0xffU));
        }
    }
    thread_local std::mt19937 generator(std::random_device{}());
    std::array<unsigned char, 4> mask{};
    for (auto& byte : mask) {
        byte = static_cast<unsigned char>(generator() & 0xffU);
        frame.push_back(static_cast<char>(byte));
    }
    for (std::size_t index = 0; index < payload.size(); ++index) {
        frame.push_back(static_cast<char>(
            static_cast<unsigned char>(payload[index]) ^
            mask[index % mask.size()]));
    }
    if (!send_all(fd_, frame.data(), frame.size())) {
        error = "cannot send WebSocket frame";
        return false;
    }
    error.clear();
    return true;
}

bool WebSocketClient::read_frame(
    std::uint8_t& opcode,
    std::string& payload,
    int timeout_ms,
    std::string& error) {
    pollfd descriptor{fd_, POLLIN, 0};
    const int ready = ::poll(&descriptor, 1, timeout_ms);
    if (ready <= 0) {
        error = ready == 0
            ? "timed out waiting for WebSocket frame"
            : std::strerror(errno);
        return false;
    }
    std::array<unsigned char, 2> header{};
    if (!receive_all(fd_, reinterpret_cast<char*>(header.data()), 2)) {
        error = "cannot receive WebSocket frame header";
        return false;
    }
    opcode = header[0] & 0x0fU;
    if ((header[0] & 0x80U) == 0 || (header[1] & 0x80U) != 0) {
        error = "unsupported WebSocket frame";
        return false;
    }
    std::uint64_t size = header[1] & 0x7fU;
    if (size == 126U) {
        std::array<unsigned char, 2> extended{};
        if (!receive_all(
                fd_, reinterpret_cast<char*>(extended.data()), 2)) {
            error = "cannot receive WebSocket frame length";
            return false;
        }
        size = (static_cast<std::uint64_t>(extended[0]) << 8U) | extended[1];
    } else if (size == 127U) {
        std::array<unsigned char, 8> extended{};
        if (!receive_all(
                fd_, reinterpret_cast<char*>(extended.data()), 8)) {
            error = "cannot receive WebSocket frame length";
            return false;
        }
        size = 0;
        for (const auto byte : extended) {
            size = (size << 8U) | byte;
        }
    }
    if (size > 256U * 1024U * 1024U ||
        size > std::numeric_limits<std::size_t>::max()) {
        error = "WebSocket frame is too large";
        return false;
    }
    payload.resize(static_cast<std::size_t>(size));
    if (!payload.empty() &&
        !receive_all(fd_, payload.data(), payload.size())) {
        error = "cannot receive WebSocket frame body";
        return false;
    }
    error.clear();
    return true;
}

}  // namespace chat::gateway
