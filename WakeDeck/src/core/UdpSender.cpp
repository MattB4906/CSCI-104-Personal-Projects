#include "UdpSender.h"

#include <sys/types.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace wakedeck {

std::optional<IPv4Address> resolveIPv4(const std::string& host, std::string* error) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    addrinfo* result = nullptr;
    const int rc = ::getaddrinfo(host.c_str(), nullptr, &hints, &result);
    if (rc != 0 || result == nullptr) {
        if (error) *error = rc != 0 ? ::gai_strerror(rc) : "no address returned";
        return std::nullopt;
    }

    const auto* address = reinterpret_cast<const sockaddr_in*>(result->ai_addr);
    IPv4Address out;
    out.networkOrder = address->sin_addr.s_addr;
    char text[INET_ADDRSTRLEN] = {};
    ::inet_ntop(AF_INET, &address->sin_addr, text, sizeof text);
    out.dotted = text;
    ::freeaddrinfo(result);
    return out;
}

UdpSender::UdpSender() {
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
        openError_ = std::strerror(errno);
        return;
    }
    // Without this option the kernel refuses to send to broadcast addresses.
    int enable = 1;
    if (::setsockopt(fd_, SOL_SOCKET, SO_BROADCAST, &enable, sizeof enable) != 0) {
        openError_ = std::string("cannot enable broadcast: ") + std::strerror(errno);
        ::close(fd_);
        fd_ = -1;
    }
}

UdpSender::~UdpSender() {
    if (fd_ >= 0) ::close(fd_);
}

std::optional<std::string> UdpSender::send(const IPv4Address& address, std::uint16_t port,
                                           const std::uint8_t* data, std::size_t length) {
    if (fd_ < 0) {
        return openError_.empty() ? std::string("socket is not open") : openError_;
    }
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = address.networkOrder;

    const ssize_t sent = ::sendto(fd_, data, length, 0, reinterpret_cast<const sockaddr*>(&to), sizeof to);
    if (sent < 0) return std::string(std::strerror(errno));
    if (static_cast<std::size_t>(sent) != length) return std::string("short send");
    return std::nullopt;
}

}  // namespace wakedeck
