#include "Flags.h"

#include <charconv>
#include <iostream>
#include <system_error>

namespace wakedeck::cli {

bool Flags::has(const char* flag) const {
    return values.count(flag) > 0 || switches.count(flag) > 0;
}

std::string Flags::valueOr(const char* flag, const char* fallback) const {
    const auto it = values.find(flag);
    return it == values.end() ? fallback : it->second;
}

std::optional<std::string> parseFlags(const std::vector<std::string>& args, const std::set<std::string>& valued,
                                      const std::set<std::string>& switches, Flags& out) {
    // A help request wins wherever it appears, even after a mistyped option.
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--help" || args[i] == "-h") {
            out.help = true;
            return std::nullopt;
        }
    }
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& word = args[i];
        if (word.rfind("--", 0) != 0) {
            out.positional.push_back(word);
            continue;
        }
        if (switches.count(word) > 0) {
            out.switches.insert(word);
            continue;
        }
        if (valued.count(word) == 0) return "unknown option '" + word + "'";
        if (i + 1 >= args.size()) return word + " needs a value";
        out.values[word] = args[++i];
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

int usageError(const std::string& message, void (*usage)(std::ostream&)) {
    std::cerr << "error: " << message << "\n\n";
    usage(std::cerr);
    return 1;
}

}  // namespace wakedeck::cli
