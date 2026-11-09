// wakedeck-cli: WakeDeck's command-line tool.
//
//   wake     send a wake request (Milestone 1; synchronous, no event loop)
//   check    ask once whether a service is ready (Milestone 2)
//   connect  check, wake if needed, keep checking until ready (Milestone 2)
//
// "Sent" always means the packets left this computer. Only a check can tell
// whether the service is actually ready.
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

#include "ConnectCommand.h"
#include "LaunchCommand.h"
#include "ProfileCommand.h"
#include "core/MacAddress.h"
#include "core/MagicPacket.h"
#include "core/UdpSender.h"
#include "core/WakePlan.h"
#include "core/WakeSender.h"

namespace {

#ifndef WAKEDECK_VERSION
#define WAKEDECK_VERSION "0.0.0-dev"  // CMake defines the real one from the project version
#endif
constexpr const char* kVersion = WAKEDECK_VERSION;

void printGeneralUsage(std::ostream& out) {
    out << "WakeDeck command-line tool " << kVersion << "\n"
        << "\n"
        << "Usage:\n"
        << "  wakedeck-cli wake    --mac <address> --to <host> [options]   send a wake request\n"
        << "  wakedeck-cli check   --host <host> [options]                 ask once whether a service is ready\n"
        << "  wakedeck-cli connect --check-host <host> [options]           check, wake if needed, wait until ready\n"
        << "  wakedeck-cli connect --profile <name>                        the same, from a saved profile\n"
        << "  wakedeck-cli launch  --profile <name> [--dry-run]            open the client only\n"
        << "  wakedeck-cli profile list|show|add|remove|import|export      manage saved profiles\n"
        << "  wakedeck-cli <command> --help                                options of one command\n"
        << "  wakedeck-cli --version\n";
}

void printWakeUsage(std::ostream& out) {
    out << "Usage:\n"
        << "  wakedeck-cli wake --mac <address> --to <host> [--ports 9,7] [--repeat 5]\n"
        << "                    [--interval-ms 100] [--dry-run]\n"
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

struct WakeOptions {
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

int usageError(const std::string& message, void (*usage)(std::ostream&)) {
    std::cerr << "error: " << message << "\n\n";
    usage(std::cerr);
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
// on a usage error or help request, std::nullopt when everything parsed.
std::optional<int> parseWakeArguments(const std::vector<std::string>& args, WakeOptions& options) {
    // A help request wins wherever it appears, even after a mistyped option.
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--help" || args[i] == "-h") {
            printWakeUsage(std::cout);
            return 0;
        }
    }
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& flag = args[i];
        if (flag == "--dry-run") {
            options.dryRun = true;
            continue;
        }
        const bool takesValue = flag == "--mac" || flag == "--to" || flag == "--ports" ||
                                flag == "--repeat" || flag == "--interval-ms";
        if (!takesValue) return usageError("unknown option '" + flag + "'", printWakeUsage);
        if (i + 1 >= args.size()) return usageError(flag + " needs a value", printWakeUsage);

        const std::string& value = args[++i];
        if (flag == "--mac") {
            options.mac = value;
        } else if (flag == "--to") {
            options.to = value;
        } else if (flag == "--ports") {
            options.ports = value;
        } else if (flag == "--repeat") {
            const auto n = parseUnsigned(value, 1, 100);
            if (!n) return usageError("--repeat must be a whole number from 1 to 100", printWakeUsage);
            options.repeat = *n;
        } else {
            const auto n = parseUnsigned(value, 0, 10000);
            if (!n) return usageError("--interval-ms must be a whole number from 0 to 10000", printWakeUsage);
            options.intervalMs = *n;
        }
    }
    if (options.mac.empty()) return usageError("--mac is required", printWakeUsage);
    if (options.to.empty()) return usageError("--to is required", printWakeUsage);
    return std::nullopt;
}

int runWake(const std::vector<std::string>& args) {
    WakeOptions options;
    if (const auto exitCode = parseWakeArguments(args, options)) return *exitCode;

    const auto mac = wakedeck::MacAddress::parse(options.mac);
    if (!mac) return usageError("'" + options.mac + "' is not a valid MAC address", printWakeUsage);
    const auto ports = wakedeck::parsePortList(options.ports);
    if (!ports) return usageError("'" + options.ports + "' is not a valid port list (example: 9,7)", printWakeUsage);

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
        std::cout << "\nDry run: nothing was sent and the destination was not resolved (no network use at all).\n"
                  << "Packet bytes (" << packet.size() << "):\n" << packet.toHex();
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

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        printGeneralUsage(std::cerr);
        return 1;
    }
    const std::string& command = args[0];
    if (command == "--help" || command == "-h" || command == "help") {
        printGeneralUsage(std::cout);
        return 0;
    }
    if (command == "--version") {
        std::cout << "wakedeck-cli " << kVersion << "\n";
        return 0;
    }
    if (command == "wake") return runWake(args);
    if (command == "check") return wakedeck::cli::runCheck(argc, argv, args);
    if (command == "connect") return wakedeck::cli::runConnect(argc, argv, args);
    if (command == "profile") return wakedeck::cli::runProfile(args);
    if (command == "launch") return wakedeck::cli::runLaunch(argc, argv, args);

    std::cerr << "error: unknown command '" << command << "'\n\n";
    printGeneralUsage(std::cerr);
    return 1;
}
