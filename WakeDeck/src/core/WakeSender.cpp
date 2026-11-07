#include "WakeSender.h"

#include <thread>

namespace wakedeck {

unsigned WakeReport::sent() const noexcept {
    unsigned count = 0;
    for (const auto& attempt : attempts) {
        if (!attempt.error) ++count;
    }
    return count;
}

unsigned WakeReport::failed() const noexcept {
    return static_cast<unsigned>(attempts.size()) - sent();
}

void sleepFor(std::chrono::milliseconds duration) {
    std::this_thread::sleep_for(duration);
}

WakeReport sendWake(const MagicPacket& packet, const IPv4Address& destination,
                    const std::vector<WakeStep>& plan, std::chrono::milliseconds interval,
                    DatagramSink& sink, const Pause& pause) {
    WakeReport report;
    report.destination = destination;
    report.attempts.reserve(plan.size());
    for (std::size_t i = 0; i < plan.size(); ++i) {
        if (i > 0 && interval.count() > 0) pause(interval);
        SendAttempt attempt;
        attempt.port = plan[i].port;
        attempt.attempt = plan[i].attempt;
        attempt.error = sink.send(destination, plan[i].port, packet.data(), packet.size());
        report.attempts.push_back(std::move(attempt));
    }
    return report;
}

}  // namespace wakedeck
