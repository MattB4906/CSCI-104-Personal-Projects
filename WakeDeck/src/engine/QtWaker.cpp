#include "QtWaker.h"

#include <QHostAddress>
#include <QHostInfo>
#include <QTimer>
#include <QtEndian>
#include <algorithm>
#include <optional>

#include "LocalNetworks.h"

namespace wakedeck {
namespace {

QString portsOf(const WakeTarget& target) {
    QStringList ports;
    for (std::uint16_t port : target.ports) {
        ports << QStringLiteral("%1 x%2").arg(port).arg(target.repeat);
    }
    return ports.join(QStringLiteral(", "));
}

IPv4Address fromQHostAddress(const QHostAddress& address) {
    IPv4Address out;
    out.dotted = address.toString().toStdString();
    out.networkOrder = qToBigEndian<quint32>(address.toIPv4Address());
    return out;
}

}  // namespace

QString destinationModeName(WakeDestination mode) {
    return mode == WakeDestination::LocalNetworks ? QStringLiteral("localNetworks") : QStringLiteral("address");
}

QString resendSecondsError(int seconds) {
    if (seconds == 0 || (seconds >= WakeConfig::kMinResendSeconds && seconds <= WakeConfig::kMaxResendSeconds)) {
        return QString();
    }
    return QStringLiteral("must be 0 (send once) or a whole number of seconds from %1 to %2")
        .arg(WakeConfig::kMinResendSeconds)
        .arg(WakeConfig::kMaxResendSeconds);
}

QString describe(const WakeConfig& config) {
    const WakeTarget& target = config.target;
    const unsigned total = static_cast<unsigned>(target.ports.size()) * target.repeat;
    if (config.destinationMode == WakeDestination::LocalNetworks) {
        return QStringLiteral("%1 packets to every local network of this Mac (ports %2)").arg(total).arg(portsOf(target));
    }
    return QStringLiteral("%1 packets to %2 (ports %3)")
        .arg(total)
        .arg(QString::fromStdString(target.destination), portsOf(target));
}

QString describeResend(const WakeConfig& config) {
    if (config.resendSeconds <= 0) return QStringLiteral("sent once");
    return QStringLiteral("again every %1 s while waiting").arg(config.resendSeconds);
}

QtWaker::QtWaker(const WakeConfig& config, QObject* parent)
    : QtWaker(config, [] { return localBroadcastAddresses(); }, parent) {}

QtWaker::QtWaker(const WakeConfig& config, BroadcastAddressProvider provider, QObject* parent)
    : Waker(parent), config_(config), provider_(std::move(provider)), packet_(config.mac),
      plan_(expandWakePlan(config.target)) {}

void QtWaker::start() {
    if (config_.destinationMode == WakeDestination::LocalNetworks) {
        // Read on the next turn of the event loop, so finished() never fires
        // from inside start(), exactly like the lookup path below.
        QTimer::singleShot(0, this, [this] { startLocalNetworks(); });
        return;
    }
    const QString destination = QString::fromStdString(config_.target.destination);
    // Asynchronous lookup: a slow DNS answer must never freeze the caller.
    QHostInfo::lookupHost(destination, this, [this, destination](const QHostInfo& info) {
        if (info.error() != QHostInfo::NoError) {
            errors_ << QStringLiteral("cannot resolve %1: %2").arg(destination, info.errorString());
            finish();
            return;
        }
        std::optional<QHostAddress> ipv4;
        for (const QHostAddress& address : info.addresses()) {
            if (address.protocol() == QAbstractSocket::IPv4Protocol) {
                ipv4 = address;
                break;
            }
        }
        if (!ipv4) {
            errors_ << QStringLiteral("%1 has no IPv4 address").arg(destination);
            finish();
            return;
        }
        destinations_.push_back(fromQHostAddress(*ipv4));
        openAndSend();
    });
}

void QtWaker::startLocalNetworks() {
    const QStringList addresses = provider_ ? provider_() : QStringList();
    for (const QString& text : addresses) {
        QHostAddress address;
        if (address.setAddress(text) && address.protocol() == QAbstractSocket::IPv4Protocol) {
            destinations_.push_back(fromQHostAddress(address));
        }
    }
    if (destinations_.empty()) {
        errors_ << QStringLiteral("this Mac is not connected to a local network that supports broadcast");
        finish();
        return;
    }
    openAndSend();
}

void QtWaker::openAndSend() {
    sender_ = std::make_unique<UdpSender>();
    if (!sender_->isOpen()) {
        errors_ << QStringLiteral("cannot open a UDP socket: %1").arg(QString::fromStdString(sender_->openError()));
        finish();
        return;
    }
    sendNext();
}

void QtWaker::sendNext() {
    if (done_) return;
    if (next_ >= plan_.size()) {
        finish();
        return;
    }
    const WakeStep step = plan_[next_++];
    // One step goes to every destination at once; the pause is between steps.
    for (const IPv4Address& destination : destinations_) {
        const auto error = sender_->send(destination, step.port, packet_.data(), packet_.size());
        if (error) {
            const QString where = destinations_.size() > 1 || config_.destinationMode == WakeDestination::LocalNetworks
                                      ? QStringLiteral("%1, ").arg(QString::fromStdString(destination.dotted))
                                      : QString();
            errors_ << QStringLiteral("%1port %2, attempt %3: %4")
                           .arg(where)
                           .arg(step.port)
                           .arg(step.attempt)
                           .arg(QString::fromStdString(*error));
        } else {
            ++sent_;
        }
    }
    if (next_ >= plan_.size()) {
        finish();
        return;
    }
    QTimer::singleShot(static_cast<int>(config_.target.interval.count()), this, [this] { sendNext(); });
}

QString QtWaker::portsText() const {
    return portsOf(config_.target);
}

void QtWaker::finish() {
    if (done_) return;
    done_ = true;
    if (plan_.empty()) errors_ << QStringLiteral("nothing to send: no ports or zero repeats configured");
    const std::size_t total = plan_.size() * std::max<std::size_t>(destinations_.size(), 1);
    const bool allSent = !plan_.empty() && !destinations_.empty() && errors_.isEmpty() && sent_ == total;
    QString summary;
    if (config_.destinationMode == WakeDestination::LocalNetworks) {
        QStringList addresses;
        for (const IPv4Address& destination : destinations_) addresses << QString::fromStdString(destination.dotted);
        const QString networks = destinations_.size() == 1
                                     ? QStringLiteral("1 local network (%1)").arg(addresses.join(QStringLiteral(", ")))
                                     : QStringLiteral("%1 local networks (%2)")
                                           .arg(destinations_.size())
                                           .arg(addresses.join(QStringLiteral(", ")));
        if (allSent) {
            summary = destinations_.size() == 1
                          ? QStringLiteral("%1 packets to %2, ports %3").arg(plan_.size()).arg(networks, portsText())
                          : QStringLiteral("%1 packets to each of %2, ports %3").arg(plan_.size()).arg(networks, portsText());
        } else if (destinations_.empty()) {
            summary = QStringLiteral("0 packets to local networks");
        } else {
            summary = QStringLiteral("%1 of %2 packets to %3").arg(sent_).arg(total).arg(networks);
        }
    } else {
        const QString requested = QString::fromStdString(config_.target.destination);
        const QString resolved = destinations_.empty() ? QString() : QString::fromStdString(destinations_.front().dotted);
        const QString where = resolved.isEmpty() || resolved == requested
                                  ? requested
                                  : QStringLiteral("%1 (%2)").arg(requested, resolved);
        summary = allSent ? QStringLiteral("%1 packets to %2 (ports %3)").arg(sent_).arg(where, portsText())
                          : QStringLiteral("%1 of %2 packets to %3").arg(sent_).arg(plan_.size()).arg(where);
    }
    if (!errors_.isEmpty()) summary += QStringLiteral("; ") + errors_.join(QStringLiteral("; "));
    emit finished(allSent, summary);
}

Waker* makeWaker(const WakeConfig& config, QObject* parent) {
    return new QtWaker(config, parent);
}

}  // namespace wakedeck
