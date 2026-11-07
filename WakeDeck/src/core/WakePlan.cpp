#include "WakePlan.h"

#include <charconv>
#include <system_error>

namespace wakedeck {
namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
    return text;
}

std::optional<std::uint16_t> parsePort(std::string_view entry) {
    entry = trim(entry);
    if (entry.empty()) return std::nullopt;
    unsigned value = 0;
    const char* begin = entry.data();
    const char* end = begin + entry.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc() || result.ptr != end) return std::nullopt;
    if (value < 1 || value > 65535) return std::nullopt;
    return static_cast<std::uint16_t>(value);
}

}  // namespace

std::vector<WakeStep> expandWakePlan(const WakeTarget& target) {
    std::vector<WakeStep> steps;
    steps.reserve(target.ports.size() * target.repeat);
    for (std::uint16_t port : target.ports) {
        for (unsigned attempt = 1; attempt <= target.repeat; ++attempt) {
            steps.push_back(WakeStep{port, attempt});
        }
    }
    return steps;
}

std::optional<std::vector<std::uint16_t>> parsePortList(std::string_view text) {
    std::vector<std::uint16_t> ports;
    std::size_t start = 0;
    while (true) {
        const std::size_t comma = text.find(',', start);
        const std::size_t length = comma == std::string_view::npos ? std::string_view::npos : comma - start;
        const auto port = parsePort(text.substr(start, length));
        if (!port) return std::nullopt;
        ports.push_back(*port);
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return ports;
}

}  // namespace wakedeck
