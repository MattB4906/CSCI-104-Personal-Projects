#include "MagicPacket.h"

namespace wakedeck {

MagicPacket::MagicPacket(const MacAddress& target) noexcept {
    std::size_t pos = 0;
    for (std::size_t i = 0; i < kSyncLength; ++i) {
        bytes_[pos++] = 0xFF;
    }
    for (std::size_t repetition = 0; repetition < kRepetitions; ++repetition) {
        for (std::uint8_t byte : target.bytes()) {
            bytes_[pos++] = byte;
        }
    }
}

std::string MagicPacket::toHex() const {
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(kSize * 3 + kSize / 16);
    for (std::size_t i = 0; i < kSize; ++i) {
        if (i > 0) {
            out.push_back(i % 16 == 0 ? '\n' : ' ');
        }
        out.push_back(kDigits[bytes_[i] >> 4]);
        out.push_back(kDigits[bytes_[i] & 0x0F]);
    }
    out.push_back('\n');
    return out;
}

}  // namespace wakedeck
