// QtWaker: sends a wake request from inside the Qt event loop, one datagram per
// timer tick, so neither name resolution nor the pauses between packets ever
// block the interface. The bytes and the plan come from the core library.
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "core/MacAddress.h"
#include "core/MagicPacket.h"
#include "core/UdpSender.h"
#include "core/WakePlan.h"

namespace wakedeck {

// Where the wake request goes: the configured address, or the broadcast
// address of every local network this Mac is on, read again for every send.
enum class WakeDestination { Address, LocalNetworks };
QString destinationModeName(WakeDestination mode);  // "address", "localNetworks"

struct WakeConfig {
    static constexpr int kDefaultResendSeconds = 15;
    static constexpr int kMinResendSeconds = 5;
    static constexpr int kMaxResendSeconds = 600;

    WakeConfig(MacAddress mac_, WakeTarget target_, WakeDestination mode = WakeDestination::Address,
               int resend = kDefaultResendSeconds)
        : mac(mac_), target(std::move(target_)), destinationMode(mode), resendSeconds(resend) {}

    MacAddress mac;
    WakeTarget target;  // target.destination is empty in LocalNetworks mode
    WakeDestination destinationMode = WakeDestination::Address;
    int resendSeconds = kDefaultResendSeconds;  // 0: send once; otherwise again every n seconds while waiting
};

// Empty when `seconds` is 0 or within 5..600, otherwise the reason.
QString resendSecondsError(int seconds);

// "10 packets to 192.168.1.255 (ports 9 x5, 7 x5)", or with LocalNetworks
// "10 packets to every local network of this Mac (ports 9 x5, 7 x5)"
QString describe(const WakeConfig& config);
// "again every 15 s while waiting" or "sent once"
QString describeResend(const WakeConfig& config);

// Supplies the broadcast addresses for LocalNetworks mode, as dotted IPv4 text.
// The default is localBroadcastAddresses() (LocalNetworks.h); tests supply loopback.
using BroadcastAddressProvider = std::function<QStringList()>;

// Anything that can send a wake request. Emits finished() exactly once.
class Waker : public QObject {
    Q_OBJECT
public:
    explicit Waker(QObject* parent = nullptr) : QObject(parent) {}
    virtual void start() = 0;

signals:
    void finished(bool allSent, const QString& summary);
};

// In Address mode: resolves the destination, then sends the plan to it.
// In LocalNetworks mode: asks the provider for the broadcast addresses when
// started, then sends every step of the plan to each of them.
class QtWaker final : public Waker {
    Q_OBJECT
public:
    explicit QtWaker(const WakeConfig& config, QObject* parent = nullptr);
    QtWaker(const WakeConfig& config, BroadcastAddressProvider provider, QObject* parent = nullptr);
    void start() override;

private:
    void startLocalNetworks();
    void openAndSend();
    void sendNext();
    void finish();
    QString portsText() const;

    WakeConfig config_;
    BroadcastAddressProvider provider_;
    MagicPacket packet_;
    std::vector<WakeStep> plan_;
    std::size_t next_ = 0;
    std::vector<IPv4Address> destinations_;  // one in Address mode, one per network otherwise
    std::unique_ptr<UdpSender> sender_;
    unsigned sent_ = 0;
    QStringList errors_;
    bool done_ = false;
};

using WakerFactory = std::function<Waker*(const WakeConfig&, QObject* parent)>;
Waker* makeWaker(const WakeConfig& config, QObject* parent);

}  // namespace wakedeck
