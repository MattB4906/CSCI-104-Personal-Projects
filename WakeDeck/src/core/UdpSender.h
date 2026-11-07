// UdpSender: the one place where WakeDeck talks to the operating system's
// network stack. Everything above it works with the DatagramSink interface.
//
// This file targets POSIX (macOS). A Windows build would provide the same
// interface on top of Winsock in a sibling file.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace wakedeck {

// A resolved IPv4 address. `networkOrder` is the 32-bit address as it travels
// on the wire; `dotted` is the same address as text, for messages.
struct IPv4Address {
    std::string dotted;
    std::uint32_t networkOrder = 0;
};

// Resolves a dotted address or a host name to IPv4. On failure returns
// std::nullopt and, if `error` is given, a readable reason.
std::optional<IPv4Address> resolveIPv4(const std::string& host, std::string* error);

// Something that can deliver one UDP datagram. UdpSender is the real thing;
// tests use a recorder.
class DatagramSink {
public:
    virtual ~DatagramSink() = default;

    // Returns std::nullopt on success, otherwise a readable error message.
    virtual std::optional<std::string> send(const IPv4Address& address, std::uint16_t port,
                                            const std::uint8_t* data, std::size_t length) = 0;
};

// Sends UDP datagrams, including to broadcast addresses.
class UdpSender final : public DatagramSink {
public:
    UdpSender();            // opens the socket and enables broadcast
    ~UdpSender() override;  // closes it

    UdpSender(const UdpSender&) = delete;
    UdpSender& operator=(const UdpSender&) = delete;

    bool isOpen() const noexcept { return fd_ >= 0; }
    const std::string& openError() const noexcept { return openError_; }

    std::optional<std::string> send(const IPv4Address& address, std::uint16_t port,
                                    const std::uint8_t* data, std::size_t length) override;

private:
    int fd_ = -1;
    std::string openError_;
};

}  // namespace wakedeck
