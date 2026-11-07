// wakedeck-cli: Milestone 1 of WakeDeck, the wake logic as a command-line tool.
//
//   wakedeck-cli wake --mac 00:00:5E:00:53:2A --to 192.168.1.255 [--ports 9,7]
//                     [--repeat 5] [--interval-ms 100] [--dry-run]
//
// "Sent" means the packets left this computer. Only the readiness check of a
// later milestone can tell whether the computer actually woke up.
#include <charconv>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "core/MacAddress.h"
#include "core/MagicPacket.h"
#include "core/UdpSender.h"
#include "core/WakePlan.h"
#include "core/WakeSender.h"

namespace {

constexpr const char* kVersion = "0.1.0-m1";

void printUsage(std::ostream& out) {
    out << "WakeDeck command-line prototype " << kVersion << "\n"
        << "\n"
        << "Usage:\n"
        << "  wakedeck-cli wake --mac <address> --to <host> [--ports 9,7] [--repeat 5]\n"
        << "                    [--interval-ms 100] [--dry-run]\n"
        << "  wakedeck-cli --help | --version\n"
        << "\n"
        << "Options:\n"
        << "  --mac <address>    Target MAC: 00:00:5E:00:53:2A, 00-00-5E-00-53-2A or 00005E00532A\n"
        << "  --to <host>        Destination IPv4 address or host name, e.g. 192.168.1.255\n"
        << "  --ports <list>     UDP ports, comma separated, used in this order (default 9,7)\n"
        << "  --repeat <n>       Packets per port, 1-100 (default 5)\n"
        << "  --interval-ms <n>  Pause between packets in milliseconds, 0-10000 (default 100)\n"
        << "  --dry-run          Show the plan and the packet bytes without sending\n"
        << "\n"
        << "Exit codes: 0 all packets sent, 1 usage error, 2 destination not resolved,\n"
        << "            3 socket error or some packets not sent.\n";
}

struct Options {
    std::string mac;
    std::string to;
    std::string ports = "9,7";
    unsigned repeat = 5;
    unsigned intervalMs = 100;
    bool dryRun = false;
};

std::optional<unsigned> parseUnsigned(std::string_view text, unsigned min, unsigned max) {
    unsigned value = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc() || result.ptr != end) return std::nullopt;
    if (value < min || value > max) return std::nullopt;
    return value;
}

int usageError(const std::string& message) {
    std::cerr << "error: " << message << "\n\n";
    printUsage(std::cerr);
    return 1;
}

std::string localClock() {
    const std::time_t now = std::time(nullptr);
    char buffer[16] = {};
    if (const std::tm* local = std::localtime(&now)) {
        std::strftime(buffer, sizeof buffer, "%H:%M:%S", local);
    }
    return buffer;
}

std::string describePlan(const wakedeck::WakeTarget& target, std::size_t count) {
    std::string out = std::to_string(count) + " packets:";
    for (std::size_t i = 0; i < target.ports.size(); ++i) {
        out += (i == 0 ? " port " : ", port ");
        out += std::to_string(target.ports[i]) + " x" + std::to_string(target.repeat);
    }
    out += ", " + std::to_string(target.interval.count()) + " ms apart";
    return out;
}

// Fills `options` from the arguments after the command. Returns an exit code
// on a usage error, std::nullopt when everything parsed.
std::optional<int> parseArguments(const std::vector<std::string>& args, Options& options) {
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& flag = args[i];
        if (flag == "--dry-run") {
            options.dryRun = true;
            continue;
        }
        const bool takesValue = flag == "--mac" || flag == "--to" || flag == "--ports" ||
                                flag == "--repeat" || flag == "--interval-ms";
        if (!takesValue) return usageError("unknown option '" + flag + "'");
        if (i + 1 >= args.size()) return usageError(flag + " needs a value");

        const std::string& value = args[++i];
        if (flag == "--mac") {
            options.mac = value;
        } else if (flag == "--to") {
            options.to = value;
        } else if (flag == "--ports") {
            options.ports = value;
        } else if (flag == "--repeat") {
            const auto n = parseUnsigned(value, 1, 100);
            if (!n) return usageError("--repeat must be a whole number from 1 to 100");
            options.repeat = *n;
        } else {
            const auto n = parseUnsigned(value, 0, 10000);
            if (!n) return usageError("--interval-ms must be a whole number from 0 to 10000");
            options.intervalMs = *n;
        }
    }
    if (options.mac.empty()) return usageError("--mac is required");
    if (options.to.empty()) return usageError("--to is required");
    return std::nullopt;
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        printUsage(std::cerr);
        return 1;
    }
    if (args[0] == "--help" || args[0] == "-h" || args[0] == "help") {
        printUsage(std::cout);
        return 0;
    }
    if (args[0] == "--version") {
        std::cout << "wakedeck-cli " << kVersion << "\n";
        return 0;
    }
    if (args[0] != "wake") return usageError("unknown command '" + args[0] + "'");

    Options options;
    if (const auto exitCode = parseArguments(args, options)) return *exitCode;

    const auto mac = wakedeck::MacAddress::parse(options.mac);
    if (!mac) return usageError("'" + options.mac + "' is not a valid MAC address");
    const auto ports = wakedeck::parsePortList(options.ports);
    if (!ports) return usageError("'" + options.ports + "' is not a valid port list (example: 9,7)");

    wakedeck::WakeTarget target;
    target.destination = options.to;
    target.ports = *ports;
    target.repeat = options.repeat;
    target.interval = std::chrono::milliseconds(options.intervalMs);

    const auto plan = wakedeck::expandWakePlan(target);
    const wakedeck::MagicPacket packet(*mac);

    std::cout << "Target MAC    " << mac->toString() << "\n"
              << "Destination   " << target.destination << "\n"
              << "Plan          " << describePlan(target, plan.size()) << "\n";

    if (options.dryRun) {
        std::cout << "\nDry run: nothing was sent. Packet bytes (" << packet.size() << "):\n" << packet.toHex();
        return 0;
    }

    std::string error;
    const auto destination = wakedeck::resolveIPv4(target.destination, &error);
    if (!destination) {
        std::cerr << "error: cannot resolve '" << target.destination << "': " << error << "\n";
        return 2;
    }
    if (destination->dotted != target.destination) {
        std::cout << "Resolved to   " << destination->dotted << "\n";
    }

    wakedeck::UdpSender sender;
    if (!sender.isOpen()) {
        std::cerr << "error: cannot open a UDP socket: " << sender.openError() << "\n";
        return 3;
    }

    const auto report = wakedeck::sendWake(packet, *destination, plan, target.interval, sender);
    const std::string when = localClock();
    std::cout << "\n";
    if (report.allSent()) {
        std::cout << "Wake request sent: " << report.sent() << " packets left this computer for "
                  << destination->dotted << " at " << when << ".\n";
    } else {
        std::cout << "Wake request incomplete: " << report.sent() << " of " << report.attempts.size()
                  << " packets left this computer at " << when << ".\n";
        for (const auto& attempt : report.attempts) {
            if (attempt.error) {
                std::cout << "  port " << attempt.port << ", attempt " << attempt.attempt << ": " << *attempt.error << "\n";
            }
        }
    }
    std::cout << "This confirms the packets were sent, not that the computer is awake.\n"
              << "Check the service you need (for example Sunshine) before connecting.\n";
    return report.allSent() ? 0 : 3;
}
