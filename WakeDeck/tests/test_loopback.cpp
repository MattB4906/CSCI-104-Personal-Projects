// Loopback test: real sockets, but only on 127.0.0.1. A UDP listener receives
// what UdpSender sends and checks every byte. No packet leaves this computer.
#include <sys/types.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>

#include "check.h"
#include "core/MacAddress.h"
#include "core/MagicPacket.h"
#include "core/UdpSender.h"
#include "core/WakePlan.h"
#include "core/WakeSender.h"

using namespace wakedeck;

int main() {
    const int listener = ::socket(AF_INET, SOCK_DGRAM, 0);
    CHECK(listener >= 0);
    if (listener < 0) return check::finish("test_loopback");

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = 0;  // the OS picks a free port
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(::bind(listener, reinterpret_cast<const sockaddr*>(&local), sizeof local) == 0);

    socklen_t length = sizeof local;
    CHECK(::getsockname(listener, reinterpret_cast<sockaddr*>(&local), &length) == 0);
    const std::uint16_t port = ntohs(local.sin_port);
    CHECK(port != 0);

    timeval timeout{};
    timeout.tv_sec = 2;  // never hang if nothing arrives
    CHECK(::setsockopt(listener, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) == 0);

    const auto mac = MacAddress::parse("00:00:5E:00:53:2A");
    CHECK(mac.has_value());
    std::string error;
    const auto destination = resolveIPv4("127.0.0.1", &error);
    CHECK(destination.has_value());
    if (!mac || !destination) {
        ::close(listener);
        return check::finish("test_loopback");
    }
    const MagicPacket packet(*mac);

    WakeTarget target;
    target.ports = {port};
    target.repeat = 3;

    UdpSender sender;
    CHECK(sender.isOpen());
    const auto report = sendWake(packet, *destination, expandWakePlan(target), std::chrono::milliseconds(0),
                                 sender, [](std::chrono::milliseconds) {});
    CHECK(report.sent() == 3);
    CHECK(report.allSent());

    for (int i = 0; i < 3; ++i) {
        std::uint8_t buffer[256] = {};
        sockaddr_in from{};
        socklen_t fromLength = sizeof from;
        const ssize_t received = ::recvfrom(listener, buffer, sizeof buffer, 0,
                                            reinterpret_cast<sockaddr*>(&from), &fromLength);
        CHECK(received == static_cast<ssize_t>(MagicPacket::size()));
        CHECK(received == 102 && std::memcmp(buffer, packet.data(), 102) == 0);
        CHECK(from.sin_addr.s_addr == htonl(INADDR_LOOPBACK));
    }

    ::close(listener);

    // A sender whose socket cannot be opened must say so instead of pretending
    // to send. Refusing every new descriptor for a moment forces that path.
    rlimit saved{};
    CHECK(::getrlimit(RLIMIT_NOFILE, &saved) == 0);
    rlimit none = saved;
    none.rlim_cur = 0;
    CHECK(::setrlimit(RLIMIT_NOFILE, &none) == 0);
    {
        UdpSender blocked;
        CHECK(::setrlimit(RLIMIT_NOFILE, &saved) == 0);  // restore before anything else
        CHECK(!blocked.isOpen());
        CHECK(!blocked.openError().empty());
        const std::uint8_t byte = 0;
        const auto error = blocked.send(*destination, port, &byte, 1);
        CHECK(error.has_value());
    }

    return check::finish("test_loopback");
}
