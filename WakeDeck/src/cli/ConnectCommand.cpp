#include "ConnectCommand.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QObject>
#include <charconv>
#include <chrono>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <system_error>

#include "core/MacAddress.h"
#include "core/WakePlan.h"
#include "engine/ServiceProbe.h"
#include "engine/Session.h"

namespace wakedeck::cli {
namespace {

// "--name value" pairs after the command, plus --help.
struct Flags {
    std::map<std::string, std::string> values;
    bool help = false;

    bool has(const char* flag) const { return values.count(flag) > 0; }
    std::string valueOr(const char* flag, const char* fallback) const {
        const auto it = values.find(flag);
        return it == values.end() ? fallback : it->second;
    }
};

std::optional<std::string> parseFlags(const std::vector<std::string>& args, const std::set<std::string>& known,
                                      Flags& out) {
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& flag = args[i];
        if (flag == "--help" || flag == "-h") {
            out.help = true;
            return std::nullopt;
        }
        if (known.count(flag) == 0) return "unknown option '" + flag + "'";
        if (i + 1 >= args.size()) return flag + " needs a value";
        out.values[flag] = args[++i];
    }
    return std::nullopt;
}

std::optional<unsigned> parseUnsigned(std::string_view text, unsigned min, unsigned max) {
    unsigned value = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc() || result.ptr != end) return std::nullopt;
    if (value < min || value > max) return std::nullopt;
    return value;
}

std::optional<ProbeKind> parseKind(const std::string& text) {
    if (text == "sunshine") return ProbeKind::Sunshine;
    if (text == "tcp") return ProbeKind::Tcp;
    return std::nullopt;
}

int usageError(const std::string& message, void (*usage)(std::ostream&)) {
    std::cerr << "error: " << message << "\n\n";
    usage(std::cerr);
    return 1;
}

// The check target, with the flag names each command uses.
std::optional<ProbeTarget> buildTarget(const Flags& flags, const char* hostFlag, const char* portFlag,
                                       const char* kindFlag, const char* timeoutFlag, std::string& error) {
    ProbeTarget target;
    if (!flags.has(hostFlag)) {
        error = std::string(hostFlag) + " is required";
        return std::nullopt;
    }
    target.host = QString::fromStdString(flags.valueOr(hostFlag, "")).trimmed();
    if (target.host.isEmpty()) {
        error = std::string(hostFlag) + " needs a host name or address";
        return std::nullopt;
    }
    const auto port = parseUnsigned(flags.valueOr(portFlag, "47989"), 1, 65535);
    if (!port) {
        error = std::string(portFlag) + " must be a whole number from 1 to 65535";
        return std::nullopt;
    }
    target.port = static_cast<quint16>(*port);
    const auto kind = parseKind(flags.valueOr(kindFlag, "sunshine"));
    if (!kind) {
        error = std::string(kindFlag) + " must be sunshine or tcp";
        return std::nullopt;
    }
    target.kind = *kind;
    const auto timeout = parseUnsigned(flags.valueOr(timeoutFlag, "2000"), 100, 60000);
    if (!timeout) {
        error = std::string(timeoutFlag) + " must be a whole number of milliseconds from 100 to 60000";
        return std::nullopt;
    }
    target.timeoutMs = static_cast<int>(*timeout);
    return target;
}

std::string stamp(const QDateTime& at) {
    return at.toString(QStringLiteral("HH:mm:ss")).toStdString();
}

}  // namespace

void printCheckUsage(std::ostream& out) {
    out << "Usage:\n"
           "  wakedeck-cli check --host <host> [--port 47989] [--kind sunshine|tcp] [--timeout-ms 2000]\n"
           "\n"
           "Asks once whether the service answers. sunshine: GET /serverinfo over HTTP and read\n"
           "Sunshine's state. tcp: a plain connection to the port.\n"
           "\n"
           "Exit codes: 0 ready, 1 usage error, 2 not ready (the output says what was observed).\n";
}

void printConnectUsage(std::ostream& out) {
    out << "Usage:\n"
           "  wakedeck-cli connect --check-host <host> [--check-port 47989] [--check sunshine|tcp]\n"
           "                       [--probe-timeout-ms 2000] [--poll-s 3] [--timeout-s 120]\n"
           "                       [--mac <address> --to <host> [--ports 9,7] [--repeat 5] [--interval-ms 100]]\n"
           "\n"
           "Checks the service. If it is not ready and --mac/--to are given, sends the wake request,\n"
           "then keeps checking until the service answers or --timeout-s runs out. Without --mac/--to\n"
           "it only waits. Launching the client comes in a later milestone.\n"
           "\n"
           "Exit codes: 0 ready, 1 usage error, 2 timed out, 3 wake request failed, 4 client not launched.\n";
}

int runCheck(int argc, char** argv, const std::vector<std::string>& args) {
    Flags flags;
    if (const auto error = parseFlags(args, {"--host", "--port", "--kind", "--timeout-ms"}, flags)) {
        return usageError(*error, printCheckUsage);
    }
    if (flags.help) {
        printCheckUsage(std::cout);
        return 0;
    }
    std::string error;
    const auto target = buildTarget(flags, "--host", "--port", "--kind", "--timeout-ms", error);
    if (!target) return usageError(error, printCheckUsage);

    QCoreApplication app(argc, argv);
    std::cout << "Checking " << describe(*target).toStdString() << " ..." << std::endl;
    int exitCode = 2;
    Probe* probe = makeProbe(*target, &app);
    QObject::connect(probe, &Probe::finished, &app, [&](const ProbeResult& result) {
        if (result.ready()) {
            std::cout << "Ready: " << result.detail.toStdString();
        } else {
            std::cout << "Not ready: " << statusName(result.status).toStdString() << " (" << result.detail.toStdString() << ")";
        }
        std::cout << " after " << result.elapsedMs << " ms" << std::endl;
        exitCode = result.ready() ? 0 : 2;
        app.quit();
    });
    probe->start();
    app.exec();
    return exitCode;
}

int runConnect(int argc, char** argv, const std::vector<std::string>& args) {
    Flags flags;
    const std::set<std::string> known = {"--check-host", "--check-port", "--check", "--probe-timeout-ms", "--poll-s",
                                         "--timeout-s", "--mac", "--to", "--ports", "--repeat", "--interval-ms"};
    if (const auto error = parseFlags(args, known, flags)) return usageError(*error, printConnectUsage);
    if (flags.help) {
        printConnectUsage(std::cout);
        return 0;
    }

    std::string error;
    SessionConfig config;
    const auto target = buildTarget(flags, "--check-host", "--check-port", "--check", "--probe-timeout-ms", error);
    if (!target) return usageError(error, printConnectUsage);
    config.check = *target;

    const auto poll = parseUnsigned(flags.valueOr("--poll-s", "3"), 1, 600);
    if (!poll) return usageError("--poll-s must be a whole number of seconds from 1 to 600", printConnectUsage);
    config.pollIntervalMs = static_cast<int>(*poll) * 1000;
    const auto timeout = parseUnsigned(flags.valueOr("--timeout-s", "120"), 1, 3600);
    if (!timeout) return usageError("--timeout-s must be a whole number of seconds from 1 to 3600", printConnectUsage);
    config.overallTimeoutMs = static_cast<int>(*timeout) * 1000;

    const bool hasMac = flags.has("--mac");
    const bool hasTo = flags.has("--to");
    if (hasMac != hasTo) return usageError("--mac and --to go together: give both or neither", printConnectUsage);
    if (hasMac) {
        const auto mac = MacAddress::parse(flags.valueOr("--mac", ""));
        if (!mac) return usageError("'" + flags.valueOr("--mac", "") + "' is not a valid MAC address", printConnectUsage);
        WakeTarget wake;
        wake.destination = flags.valueOr("--to", "");
        const auto ports = parsePortList(flags.valueOr("--ports", "9,7"));
        if (!ports) return usageError("'" + flags.valueOr("--ports", "") + "' is not a valid port list (example: 9,7)", printConnectUsage);
        wake.ports = *ports;
        const auto repeat = parseUnsigned(flags.valueOr("--repeat", "5"), 1, 100);
        if (!repeat) return usageError("--repeat must be a whole number from 1 to 100", printConnectUsage);
        wake.repeat = *repeat;
        const auto interval = parseUnsigned(flags.valueOr("--interval-ms", "100"), 0, 10000);
        if (!interval) return usageError("--interval-ms must be a whole number from 0 to 10000", printConnectUsage);
        wake.interval = std::chrono::milliseconds(*interval);
        config.wake = WakeConfig{*mac, wake};
    } else if (flags.has("--ports") || flags.has("--repeat") || flags.has("--interval-ms")) {
        return usageError("--ports, --repeat and --interval-ms need --mac and --to", printConnectUsage);
    }

    QCoreApplication app(argc, argv);
    Session session(config);
    int exitCode = 2;
    QObject::connect(&session, &Session::timelineEvent, &app, [](const TimelineEvent& e) {
        std::cout << stamp(e.at) << "  " << e.text.toStdString() << std::endl;
    });
    QObject::connect(&session, &Session::finished, &app, [&](Outcome outcome, const QString& summary) {
        std::cout << "\nResult: " << outcomeName(outcome).toStdString() << "\n" << summary.toStdString() << std::endl;
        switch (outcome) {
        case Outcome::ReadyWithoutWake:
        case Outcome::ReadyAfterWake: exitCode = 0; break;
        case Outcome::TimedOut: exitCode = 2; break;
        case Outcome::WakeFailed: exitCode = 3; break;
        case Outcome::LaunchFailed: exitCode = 4; break;
        case Outcome::Cancelled: exitCode = 5; break;
        }
        app.quit();
    });
    session.start();
    app.exec();
    return exitCode;
}

}  // namespace wakedeck::cli
