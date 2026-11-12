// SessionFlags: turns command-line flags into the engine's configuration
// types. Shared by `connect` (ad-hoc runs) and `profile add` (saved runs), so
// both accept exactly the same options.
#pragma once

#include <optional>
#include <set>
#include <string>

#include "Flags.h"
#include "engine/Profile.h"
#include "engine/ServiceProbe.h"
#include "engine/Session.h"

namespace wakedeck::cli {

// --check-host, --check-port, --check, --probe-timeout-ms, --poll-s, --timeout-s,
// --mac, --to, --ports, --repeat, --interval-ms, --resend-s
const std::set<std::string>& sessionValueFlags();
// --local-networks (the alternative to --to)
const std::set<std::string>& sessionSwitchFlags();
// --client, --client-path, --launch, --app, --client-args
const std::set<std::string>& clientValueFlags();

// True when any session flag or switch other than --poll-s and --timeout-s is present.
bool hasCheckOrWakeFlags(const Flags& flags);
// True when any client flag is present.
bool hasClientFlags(const Flags& flags);

// A check target from the given flag names (check and connect use different ones).
std::optional<wakedeck::ProbeTarget> probeTargetFromFlags(const Flags& flags, const char* hostFlag,
                                                          const char* portFlag, const char* kindFlag,
                                                          const char* timeoutFlag, std::string& error);

std::optional<wakedeck::SessionConfig> sessionConfigFromFlags(const Flags& flags, std::string& error);
bool applyTimingOverrides(const Flags& flags, wakedeck::SessionConfig& config, std::string& error);
std::optional<wakedeck::ClientConfig> clientConfigFromFlags(const Flags& flags, std::string& error);

}  // namespace wakedeck::cli
