// WakeSender: runs a wake plan through a DatagramSink and reports exactly
// what happened to every packet.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "MagicPacket.h"
#include "UdpSender.h"
#include "WakePlan.h"

namespace wakedeck {

struct SendAttempt {
    std::uint16_t port = 0;
    unsigned attempt = 0;
    std::optional<std::string> error;  // std::nullopt when the datagram left this machine
};

struct WakeReport {
    IPv4Address destination;
    std::vector<SendAttempt> attempts;

    unsigned sent() const noexcept;
    unsigned failed() const noexcept;
    bool allSent() const noexcept { return !attempts.empty() && failed() == 0; }
};

// Called between consecutive packets. The default really sleeps; tests pass a no-op.
using Pause = std::function<void(std::chrono::milliseconds)>;
void sleepFor(std::chrono::milliseconds duration);

WakeReport sendWake(const MagicPacket& packet, const IPv4Address& destination,
                    const std::vector<WakeStep>& plan, std::chrono::milliseconds interval,
                    DatagramSink& sink, const Pause& pause = sleepFor);

}  // namespace wakedeck
