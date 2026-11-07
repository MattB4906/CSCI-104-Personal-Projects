// MacAddress: a validated 48-bit hardware (Ethernet) address.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace wakedeck {

class MacAddress {
public:
    static constexpr std::size_t kLength = 6;
    using Bytes = std::array<std::uint8_t, kLength>;

    // Accepts "AA:BB:CC:DD:EE:FF", "aa-bb-cc-dd-ee-ff" or "AABBCCDDEEFF".
    // Surrounding whitespace is ignored. Anything else yields std::nullopt.
    static std::optional<MacAddress> parse(std::string_view text);

    explicit MacAddress(const Bytes& bytes) noexcept;

    const Bytes& bytes() const noexcept { return bytes_; }

    // Canonical form: upper-case hex pairs separated by colons.
    std::string toString() const;

    friend bool operator==(const MacAddress& a, const MacAddress& b) noexcept {
        return a.bytes_ == b.bytes_;
    }
    friend bool operator!=(const MacAddress& a, const MacAddress& b) noexcept {
        return !(a == b);
    }

private:
    Bytes bytes_{};
};

}  // namespace wakedeck
