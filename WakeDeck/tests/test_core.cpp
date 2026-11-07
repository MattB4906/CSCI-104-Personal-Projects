// Unit tests for the WakeDeck core. No sockets: sendWake is exercised through
// a recording sink, so these tests run anywhere and never touch the network.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "check.h"
#include "core/MacAddress.h"
#include "core/MagicPacket.h"
#include "core/UdpSender.h"
#include "core/WakePlan.h"
#include "core/WakeSender.h"

using namespace wakedeck;

namespace {

// Records what would have been sent instead of using a socket.
class RecordingSink final : public DatagramSink {
public:
    struct Record {
        std::string address;
        std::uint16_t port;
        std::vector<std::uint8_t> payload;
    };

    std::vector<Record> records;
    std::uint16_t failingPort = 0;  // when non-zero, sends to this port report an error

    std::optional<std::string> send(const IPv4Address& address, std::uint16_t port,
                                    const std::uint8_t* data, std::size_t length) override {
        if (failingPort != 0 && port == failingPort) return std::string("simulated failure");
        records.push_back(Record{address.dotted, port, std::vector<std::uint8_t>(data, data + length)});
        return std::nullopt;
    }
};

const char* const kExampleMac = "00:00:5E:00:53:2A";  // RFC 7042 documentation range

void testMacParsing() {
    const auto colon = MacAddress::parse(kExampleMac);
    CHECK(colon.has_value());
    if (!colon) return;
    CHECK(colon->toString() == "00:00:5E:00:53:2A");
    CHECK(colon->bytes()[0] == 0x00 && colon->bytes()[2] == 0x5E && colon->bytes()[5] == 0x2A);

    const auto dash = MacAddress::parse("00-00-5e-00-53-2a");
    CHECK(dash && *dash == *colon);
    const auto bare = MacAddress::parse("00005E00532A");
    CHECK(bare && *bare == *colon);
    const auto padded = MacAddress::parse("  00:00:5E:00:53:2A\n");
    CHECK(padded && *padded == *colon);
    const auto other = MacAddress::parse("00:00:5E:00:53:2B");
    CHECK(other && *other != *colon);

    CHECK(!MacAddress::parse(""));
    CHECK(!MacAddress::parse("00:00:5E:00:53"));           // five groups
    CHECK(!MacAddress::parse("00:00:5E:00:53:2A:FF"));     // seven groups
    CHECK(!MacAddress::parse("00:00-5E:00:53:2A"));        // mixed separators
    CHECK(!MacAddress::parse("00:00:5G:00:53:2A"));        // not hex
    CHECK(!MacAddress::parse("00005E00532"));              // eleven digits
    CHECK(!MacAddress::parse("00.00.5E.00.53.2A"));        // unsupported separator
    CHECK(!MacAddress::parse("00:00:5E:00:53:2A extra"));  // trailing text
    CHECK(!MacAddress::parse("0:0:5E:0:53:2A"));           // groups must be two digits
}

void testMagicPacket() {
    const auto mac = MacAddress::parse(kExampleMac);
    CHECK(mac.has_value());
    if (!mac) return;

    const MagicPacket packet(*mac);
    CHECK(MagicPacket::size() == 102);
    CHECK(packet.bytes().size() == 102);

    const auto& bytes = packet.bytes();
    bool syncOk = true;
    for (std::size_t i = 0; i < 6; ++i) syncOk = syncOk && bytes[i] == 0xFF;
    CHECK(syncOk);

    bool repetitionsOk = true;
    for (std::size_t repetition = 0; repetition < 16; ++repetition) {
        for (std::size_t i = 0; i < 6; ++i) {
            repetitionsOk = repetitionsOk && bytes[6 + repetition * 6 + i] == mac->bytes()[i];
        }
    }
    CHECK(repetitionsOk);
    CHECK(packet.toHex().rfind("FF FF FF FF FF FF 00 00 5E 00 53 2A", 0) == 0);

    // The whole hex dump must decode back to the same 102 bytes, laid out as
    // upper-case pairs, 16 per line, each line ended by a newline.
    const std::string hex = packet.toHex();
    CHECK(hex.size() == MagicPacket::kSize * 3);  // two digits plus one separator per byte
    const auto hexDigit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::vector<std::uint8_t> decoded;
    std::size_t newlines = 0;
    bool layoutOk = true;
    for (std::size_t i = 0; i + 3 <= hex.size(); i += 3) {
        const int high = hexDigit(hex[i]);
        const int low = hexDigit(hex[i + 1]);
        if (high < 0 || low < 0) {
            layoutOk = false;
            break;
        }
        decoded.push_back(static_cast<std::uint8_t>(high * 16 + low));
        const bool endOfLine = decoded.size() % 16 == 0 || decoded.size() == MagicPacket::kSize;
        const char separator = hex[i + 2];
        if (separator != (endOfLine ? '\n' : ' ')) layoutOk = false;
        if (separator == '\n') ++newlines;
    }
    CHECK(layoutOk);
    CHECK(newlines == 7);
    CHECK((decoded.size() == MagicPacket::kSize &&
           std::equal(decoded.begin(), decoded.end(), packet.bytes().begin())));
}

void testPortList() {
    const auto ports = parsePortList("9,7");
    CHECK((ports && *ports == std::vector<std::uint16_t>{9, 7}));
    const auto spaced = parsePortList(" 9 , 7 ");
    CHECK((spaced && *spaced == std::vector<std::uint16_t>{9, 7}));
    const auto single = parsePortList("65535");
    CHECK((single && *single == std::vector<std::uint16_t>{65535}));

    CHECK(!parsePortList(""));
    CHECK(!parsePortList("0"));
    CHECK(!parsePortList("65536"));
    CHECK(!parsePortList("9,,7"));
    CHECK(!parsePortList("9,"));
    CHECK(!parsePortList(",9"));
    CHECK(!parsePortList("nine"));
    CHECK(!parsePortList("-9"));
    CHECK(!parsePortList("9 7"));
}

void testPlanOrder() {
    WakeTarget target;
    target.destination = "192.0.2.255";
    target.ports = {9, 7};
    target.repeat = 5;

    const auto plan = expandWakePlan(target);
    CHECK(plan.size() == 10);
    bool orderOk = plan.size() == 10;
    for (std::size_t i = 0; i < plan.size() && orderOk; ++i) {
        const std::uint16_t expectedPort = i < 5 ? 9 : 7;
        const unsigned expectedAttempt = static_cast<unsigned>(i % 5) + 1;
        orderOk = plan[i].port == expectedPort && plan[i].attempt == expectedAttempt;
    }
    CHECK(orderOk);

    WakeTarget nothing;
    nothing.repeat = 0;
    CHECK(expandWakePlan(nothing).empty());

    WakeTarget defaults;
    CHECK((defaults.ports == std::vector<std::uint16_t>{9, 7}));
    CHECK(defaults.repeat == 5);
    CHECK(defaults.interval == std::chrono::milliseconds(100));
}

void testSendWakeThroughRecorder() {
    const auto mac = MacAddress::parse(kExampleMac);
    CHECK(mac.has_value());
    if (!mac) return;
    const MagicPacket packet(*mac);

    IPv4Address destination;
    destination.dotted = "192.0.2.255";

    WakeTarget target;
    target.ports = {9, 7};
    target.repeat = 5;
    const auto plan = expandWakePlan(target);

    RecordingSink sink;
    int pauses = 0;
    const auto report = sendWake(packet, destination, plan, std::chrono::milliseconds(100), sink,
                                 [&pauses](std::chrono::milliseconds) { ++pauses; });
    CHECK(report.destination.dotted == "192.0.2.255");
    CHECK(report.sent() == 10);
    CHECK(report.failed() == 0);
    CHECK(report.allSent());
    CHECK(pauses == 9);  // between packets, never before the first
    CHECK(sink.records.size() == 10);

    bool payloadOk = sink.records.size() == 10;
    for (const auto& record : sink.records) {
        payloadOk = payloadOk && record.address == "192.0.2.255" && record.payload.size() == 102 &&
                    std::memcmp(record.payload.data(), packet.data(), 102) == 0;
    }
    CHECK(payloadOk);

    bool portOrderOk = sink.records.size() == 10;
    for (std::size_t i = 0; i < sink.records.size() && portOrderOk; ++i) {
        portOrderOk = sink.records[i].port == (i < 5 ? 9 : 7);
    }
    CHECK(portOrderOk);

    // No pause at all when the interval is zero.
    RecordingSink quick;
    int quickPauses = 0;
    sendWake(packet, destination, plan, std::chrono::milliseconds(0), quick,
             [&quickPauses](std::chrono::milliseconds) { ++quickPauses; });
    CHECK(quickPauses == 0);

    // A failing port is reported attempt by attempt, never hidden.
    RecordingSink flaky;
    flaky.failingPort = 7;
    const auto partial = sendWake(packet, destination, plan, std::chrono::milliseconds(0), flaky,
                                  [](std::chrono::milliseconds) {});
    CHECK(partial.sent() == 5);
    CHECK(partial.failed() == 5);
    CHECK(!partial.allSent());
    CHECK(partial.attempts.size() == 10);
    if (partial.attempts.size() == 10) {
        CHECK(!partial.attempts[0].error);
        CHECK(partial.attempts[5].error && *partial.attempts[5].error == "simulated failure");
        CHECK(partial.attempts[5].port == 7 && partial.attempts[5].attempt == 1);
    }

    WakeReport empty;
    CHECK(!empty.allSent());
}

void testResolveNumeric() {
    std::string error;
    const auto documentation = resolveIPv4("192.0.2.255", &error);
    CHECK(documentation && documentation->dotted == "192.0.2.255");
    const auto limited = resolveIPv4("255.255.255.255", &error);
    CHECK(limited && limited->dotted == "255.255.255.255");
    const auto loopback = resolveIPv4("127.0.0.1", &error);
    CHECK(loopback && loopback->dotted == "127.0.0.1");
    CHECK(!resolveIPv4("", &error));  // nothing to resolve; no name lookup happens here
}

}  // namespace

int main() {
    testMacParsing();
    testMagicPacket();
    testPortList();
    testPlanOrder();
    testSendWakeThroughRecorder();
    testResolveNumeric();
    return check::finish("test_core");
}
