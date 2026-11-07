#include "MacAddress.h"

#include <cctype>

namespace wakedeck {
namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

std::optional<int> hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return std::nullopt;
}

}  // namespace

MacAddress::MacAddress(const Bytes& bytes) noexcept : bytes_(bytes) {}

std::optional<MacAddress> MacAddress::parse(std::string_view text) {
    text = trim(text);

    // Two shapes are accepted: "xx?xx?xx?xx?xx?xx" (17 characters, one separator
    // character used consistently) and twelve bare hex digits.
    char separator = '\0';
    if (text.size() == 17) {
        separator = text[2];
        if (separator != ':' && separator != '-') return std::nullopt;
    } else if (text.size() != 12) {
        return std::nullopt;
    }

    Bytes bytes{};
    std::size_t pos = 0;
    for (std::size_t i = 0; i < kLength; ++i) {
        if (i > 0 && separator != '\0') {
            if (text[pos] != separator) return std::nullopt;
            ++pos;
        }
        const auto high = hexValue(text[pos]);
        const auto low = hexValue(text[pos + 1]);
        if (!high || !low) return std::nullopt;
        bytes[i] = static_cast<std::uint8_t>((*high << 4) | *low);
        pos += 2;
    }
    return MacAddress(bytes);
}

std::string MacAddress::toString() const {
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(17);
    for (std::size_t i = 0; i < kLength; ++i) {
        if (i > 0) out.push_back(':');
        out.push_back(kDigits[bytes_[i] >> 4]);
        out.push_back(kDigits[bytes_[i] & 0x0F]);
    }
    return out;
}

}  // namespace wakedeck
