// QtWaker: sends a wake request from inside the Qt event loop, one datagram per
// timer tick, so neither name resolution nor the pauses between packets ever
// block the interface. The bytes and the plan come from the core library.
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>
#include <vector>

#include "core/MacAddress.h"
#include "core/MagicPacket.h"
#include "core/UdpSender.h"
#include "core/WakePlan.h"

namespace wakedeck {

struct WakeConfig {
    MacAddress mac;
    WakeTarget target;
};

// "10 packets to 192.168.1.255 (ports 9 x5, 7 x5)"
QString describe(const WakeConfig& config);

// Anything that can send a wake request. Emits finished() exactly once.
class Waker : public QObject {
    Q_OBJECT
public:
    explicit Waker(QObject* parent = nullptr) : QObject(parent) {}
    virtual void start() = 0;

signals:
    void finished(bool allSent, const QString& summary);
};

class QtWaker final : public Waker {
    Q_OBJECT
public:
    explicit QtWaker(const WakeConfig& config, QObject* parent = nullptr);
    void start() override;

private:
    void sendNext();
    void finish();

    WakeConfig config_;
    MagicPacket packet_;
    std::vector<WakeStep> plan_;
    std::size_t next_ = 0;
    IPv4Address destination_;
    std::unique_ptr<UdpSender> sender_;
    unsigned sent_ = 0;
    QStringList errors_;
    bool done_ = false;
};

using WakerFactory = std::function<Waker*(const WakeConfig&, QObject* parent)>;
Waker* makeWaker(const WakeConfig& config, QObject* parent);

}  // namespace wakedeck
