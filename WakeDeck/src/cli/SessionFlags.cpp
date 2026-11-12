#include "SessionFlags.h"

#include <chrono>

#include "core/MacAddress.h"
#include "core/WakePlan.h"

namespace wakedeck::cli {

const std::set<std::string>& sessionValueFlags() {
    static const std::set<std::string> flags = {"--check-host", "--check-port", "--check", "--probe-timeout-ms",
                                               "--poll-s", "--timeout-s", "--mac", "--to", "--ports", "--repeat",
                                               "--interval-ms", "--resend-s"};
    return flags;
}

const std::set<std::string>& sessionSwitchFlags() {
    static const std::set<std::string> flags = {"--local-networks"};
    return flags;
}

const std::set<std::string>& clientValueFlags() {
    static const std::set<std::string> flags = {"--client", "--client-path", "--launch", "--app", "--client-args"};
    return flags;
}

bool hasCheckOrWakeFlags(const Flags& flags) {
    for (const std::string& flag : sessionValueFlags()) {
        if (flag == "--poll-s" || flag == "--timeout-s") continue;
        if (flags.has(flag.c_str())) return true;
    }
    for (const std::string& flag : sessionSwitchFlags()) {
        if (flags.has(flag.c_str())) return true;
    }
    return false;
}

bool hasClientFlags(const Flags& flags) {
    for (const std::string& flag : clientValueFlags()) {
        if (flags.has(flag.c_str())) return true;
    }
    return false;
}

std::optional<ProbeTarget> probeTargetFromFlags(const Flags& flags, const char* hostFlag, const char* portFlag,
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
    const std::string kind = flags.valueOr(kindFlag, "sunshine");
    if (kind == "sunshine") {
        target.kind = ProbeKind::Sunshine;
    } else if (kind == "tcp") {
        target.kind = ProbeKind::Tcp;
    } else {
        error = std::string(kindFlag) + " must be sunshine or tcp";
        return std::nullopt;
    }
    const auto timeout = parseUnsigned(flags.valueOr(timeoutFlag, "2000"), 100, 60000);
    if (!timeout) {
        error = std::string(timeoutFlag) + " must be a whole number of milliseconds from 100 to 60000";
        return std::nullopt;
    }
    target.timeoutMs = static_cast<int>(*timeout);
    return target;
}

bool applyTimingOverrides(const Flags& flags, SessionConfig& config, std::string& error) {
    if (flags.has("--poll-s")) {
        const auto poll = parseUnsigned(flags.valueOr("--poll-s", ""), 1, 600);
        if (!poll) {
            error = "--poll-s must be a whole number of seconds from 1 to 600";
            return false;
        }
        config.pollIntervalMs = static_cast<int>(*poll) * 1000;
    }
    if (flags.has("--timeout-s")) {
        const auto timeout = parseUnsigned(flags.valueOr("--timeout-s", ""), 1, 3600);
        if (!timeout) {
            error = "--timeout-s must be a whole number of seconds from 1 to 3600";
            return false;
        }
        config.overallTimeoutMs = static_cast<int>(*timeout) * 1000;
    }
    return true;
}

std::optional<SessionConfig> sessionConfigFromFlags(const Flags& flags, std::string& error) {
    SessionConfig config;
    const auto target = probeTargetFromFlags(flags, "--check-host", "--check-port", "--check", "--probe-timeout-ms", error);
    if (!target) return std::nullopt;
    config.check = *target;
    if (!applyTimingOverrides(flags, config, error)) return std::nullopt;

    const bool hasMac = flags.has("--mac");
    const bool hasTo = flags.has("--to");
    const bool localNetworks = flags.has("--local-networks");
    if (hasTo && localNetworks) {
        error = "give either --to or --local-networks, not both";
        return std::nullopt;
    }
    if (hasMac != (hasTo || localNetworks)) {
        error = "--mac goes together with --to or --local-networks: give both or neither";
        return std::nullopt;
    }
    if (!hasMac) {
        if (flags.has("--ports") || flags.has("--repeat") || flags.has("--interval-ms") || flags.has("--resend-s")) {
            error = "--ports, --repeat, --interval-ms and --resend-s need --mac with --to or --local-networks";
            return std::nullopt;
        }
        return config;
    }

    const auto mac = MacAddress::parse(flags.valueOr("--mac", ""));
    if (!mac) {
        error = "'" + flags.valueOr("--mac", "") + "' is not a valid MAC address";
        return std::nullopt;
    }
    WakeTarget wake;
    if (!localNetworks) {
        wake.destination = QString::fromStdString(flags.valueOr("--to", "")).trimmed().toStdString();
        if (wake.destination.empty()) {
            error = "--to needs a host name or address";
            return std::nullopt;
        }
    }
    const auto ports = parsePortList(flags.valueOr("--ports", "9,7"));
    if (!ports) {
        error = "'" + flags.valueOr("--ports", "") + "' is not a valid port list (example: 9,7)";
        return std::nullopt;
    }
    wake.ports = *ports;
    const auto repeat = parseUnsigned(flags.valueOr("--repeat", "5"), 1, 100);
    if (!repeat) {
        error = "--repeat must be a whole number from 1 to 100";
        return std::nullopt;
    }
    wake.repeat = *repeat;
    const auto interval = parseUnsigned(flags.valueOr("--interval-ms", "100"), 0, 10000);
    if (!interval) {
        error = "--interval-ms must be a whole number from 0 to 10000";
        return std::nullopt;
    }
    wake.interval = std::chrono::milliseconds(*interval);
    const auto resend = parseUnsigned(flags.valueOr("--resend-s", "15"), 0, WakeConfig::kMaxResendSeconds);
    if (!resend || !resendSecondsError(static_cast<int>(*resend)).isEmpty()) {
        error = "--resend-s must be 0 (send once) or a whole number of seconds from 5 to 600";
        return std::nullopt;
    }
    config.wake = WakeConfig{*mac, wake, localNetworks ? WakeDestination::LocalNetworks : WakeDestination::Address,
                             static_cast<int>(*resend)};
    config.resendIntervalMs = static_cast<int>(*resend) * 1000;
    return config;
}

std::optional<ClientConfig> clientConfigFromFlags(const Flags& flags, std::string& error) {
    ClientConfig client;
    const auto preset = presetFromName(QString::fromStdString(flags.valueOr("--client", "none")));
    if (!preset) {
        error = "--client must be none, moonlight or custom";
        return std::nullopt;
    }
    client.preset = *preset;
    client.path = QString::fromStdString(flags.valueOr("--client-path", "")).trimmed();
    const std::string launch = flags.valueOr("--launch", "open");
    if (launch != "open" && launch != "stream") {
        error = "--launch must be open or stream";
        return std::nullopt;
    }
    client.launchMode = QString::fromStdString(launch);
    client.appName = QString::fromStdString(flags.valueOr("--app", "")).trimmed();
    if (flags.has("--client-args")) {
        // One string, split on spaces; "{host}" inside it is replaced at launch time.
        client.arguments = QString::fromStdString(flags.valueOr("--client-args", "")).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    }
    // The same rules as for a profile file, so nothing half-configured gets saved either way.
    const QString inconsistency = clientConfigError(client);
    if (!inconsistency.isEmpty()) {
        error = "client options: " + inconsistency.toStdString();
        return std::nullopt;
    }
    return client;
}

}  // namespace wakedeck::cli
