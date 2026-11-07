// Flags: the small command-line parser shared by every command. "--name value"
// pairs, stand-alone switches, and bare words (positional arguments).
#pragma once

#include <iosfwd>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace wakedeck::cli {

struct Flags {
    std::vector<std::string> positional;        // bare words after the command, in order
    std::map<std::string, std::string> values;  // --flag value
    std::set<std::string> switches;             // --flag without a value
    bool help = false;

    bool has(const char* flag) const;
    std::string valueOr(const char* flag, const char* fallback) const;
};

// Parses args[1..] (args[0] is the command). A help request anywhere sets
// `help` and stops. Returns an error text on an unknown or incomplete option.
std::optional<std::string> parseFlags(const std::vector<std::string>& args, const std::set<std::string>& valued,
                                      const std::set<std::string>& switches, Flags& out);

std::optional<unsigned> parseUnsigned(std::string_view text, unsigned min, unsigned max);

// Prints "error: <message>" and the usage, returns exit code 1.
int usageError(const std::string& message, void (*usage)(std::ostream&));

}  // namespace wakedeck::cli
