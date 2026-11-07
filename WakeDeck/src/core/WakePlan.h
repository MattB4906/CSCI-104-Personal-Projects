// WakePlan: what the user configured for waking one computer, and how that
// expands into the exact sequence of datagrams to send.
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wakedeck {

struct WakeTarget {
    std::string destination;                  // IPv4 address or host name, e.g. "192.168.1.255"
    std::vector<std::uint16_t> ports{9, 7};   // sent in this order
    unsigned repeat = 5;                      // packets per port
    std::chrono::milliseconds interval{100};  // pause between consecutive packets
};

// One datagram to send: which port, and which attempt (1-based) for that port.
struct WakeStep {
    std::uint16_t port;
    unsigned attempt;
};

// Every packet for the first port, then every packet for the next port, and so on.
std::vector<WakeStep> expandWakePlan(const WakeTarget& target);

// "9,7" -> {9, 7}. Ports are 1..65535; spaces around entries are ignored.
// Empty input, empty entries or anything non-numeric yields std::nullopt.
std::optional<std::vector<std::uint16_t>> parsePortList(std::string_view text);

}  // namespace wakedeck
