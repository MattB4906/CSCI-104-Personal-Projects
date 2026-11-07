// MagicPacket: the Wake-on-LAN frame a network card recognises while the
// computer sleeps: six 0xFF bytes, then the target MAC address sixteen times.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "MacAddress.h"

namespace wakedeck {

class MagicPacket {
public:
    static constexpr std::size_t kSyncLength = 6;
    static constexpr std::size_t kRepetitions = 16;
    static constexpr std::size_t kSize = kSyncLength + kRepetitions * MacAddress::kLength;  // 102
    using Bytes = std::array<std::uint8_t, kSize>;

    explicit MagicPacket(const MacAddress& target) noexcept;

    const Bytes& bytes() const noexcept { return bytes_; }
    const std::uint8_t* data() const noexcept { return bytes_.data(); }
    static constexpr std::size_t size() noexcept { return kSize; }

    // Upper-case hex, space separated, sixteen bytes per line. Used by --dry-run.
    std::string toHex() const;

private:
    Bytes bytes_{};
};

}  // namespace wakedeck
