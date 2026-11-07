#include "QtWaker.h"

#include <QHostAddress>
#include <QHostInfo>
#include <QTimer>
#include <QtEndian>
#include <optional>

namespace wakedeck {

QString describe(const WakeConfig& config) {
    const WakeTarget& target = config.target;
    QStringList ports;
    for (std::uint16_t port : target.ports) {
        ports << QStringLiteral("%1 x%2").arg(port).arg(target.repeat);
    }
    const unsigned total = static_cast<unsigned>(target.ports.size()) * target.repeat;
    return QStringLiteral("%1 packets to %2 (ports %3)")
        .arg(total)
        .arg(QString::fromStdString(target.destination), ports.join(QStringLiteral(", ")));
}

QtWaker::QtWaker(const WakeConfig& config, QObject* parent)
    : Waker(parent), config_(config), packet_(config.mac), plan_(expandWakePlan(config.target)) {}

void QtWaker::start() {
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
        destination_.dotted = ipv4->toString().toStdString();
        destination_.networkOrder = qToBigEndian<quint32>(ipv4->toIPv4Address());

        sender_ = std::make_unique<UdpSender>();
        if (!sender_->isOpen()) {
            errors_ << QStringLiteral("cannot open a UDP socket: %1").arg(QString::fromStdString(sender_->openError()));
            finish();
            return;
        }
        sendNext();
    });
}

void QtWaker::sendNext() {
    if (done_) return;
    if (next_ >= plan_.size()) {
        finish();
        return;
    }
    const WakeStep step = plan_[next_++];
    const auto error = sender_->send(destination_, step.port, packet_.data(), packet_.size());
    if (error) {
        errors_ << QStringLiteral("port %1, attempt %2: %3").arg(step.port).arg(step.attempt).arg(QString::fromStdString(*error));
    } else {
        ++sent_;
    }
    if (next_ >= plan_.size()) {
        finish();
        return;
    }
    QTimer::singleShot(static_cast<int>(config_.target.interval.count()), this, [this] { sendNext(); });
}

void QtWaker::finish() {
    if (done_) return;
    done_ = true;
    const bool allSent = errors_.isEmpty() && sent_ == plan_.size();
    const QString requested = QString::fromStdString(config_.target.destination);
    const QString resolved = QString::fromStdString(destination_.dotted);
    const QString where = resolved.isEmpty() || resolved == requested
                              ? requested
                              : QStringLiteral("%1 (%2)").arg(requested, resolved);
    QStringList ports;
    for (std::uint16_t port : config_.target.ports) {
        ports << QStringLiteral("%1 x%2").arg(port).arg(config_.target.repeat);
    }
    QString summary = allSent
        ? QStringLiteral("%1 packets to %2 (ports %3)").arg(sent_).arg(where, ports.join(QStringLiteral(", ")))
        : QStringLiteral("%1 of %2 packets to %3").arg(sent_).arg(plan_.size()).arg(where);
    if (!errors_.isEmpty()) summary += QStringLiteral("; ") + errors_.join(QStringLiteral("; "));
    emit finished(allSent, summary);
}

Waker* makeWaker(const WakeConfig& config, QObject* parent) {
    return new QtWaker(config, parent);
}

}  // namespace wakedeck
