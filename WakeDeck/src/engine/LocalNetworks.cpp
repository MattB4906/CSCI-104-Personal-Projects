#include "LocalNetworks.h"

#include <QNetworkAddressEntry>
#include <QNetworkInterface>

namespace wakedeck {

QStringList chooseBroadcastAddresses(const std::vector<InterfaceDescription>& interfaces) {
    QStringList out;
    for (const InterfaceDescription& interface : interfaces) {
        if (!interface.isUp || !interface.isRunning || !interface.canBroadcast) continue;
        if (interface.isLoopback || interface.isPointToPoint) continue;
        for (const InterfaceDescription::Entry& entry : interface.entries) {
            if (entry.ip.protocol() != QAbstractSocket::IPv4Protocol) continue;
            if (entry.broadcast.isNull() || entry.broadcast.protocol() != QAbstractSocket::IPv4Protocol) continue;
            const QString text = entry.broadcast.toString();
            if (!out.contains(text)) out << text;
        }
    }
    return out;
}

std::vector<InterfaceDescription> readLocalInterfaces() {
    std::vector<InterfaceDescription> out;
    const QList<QNetworkInterface> all = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& interface : all) {
        InterfaceDescription description;
        description.name = interface.name();
        const QNetworkInterface::InterfaceFlags flags = interface.flags();
        description.isUp = flags.testFlag(QNetworkInterface::IsUp);
        description.isRunning = flags.testFlag(QNetworkInterface::IsRunning);
        description.canBroadcast = flags.testFlag(QNetworkInterface::CanBroadcast);
        description.isLoopback = flags.testFlag(QNetworkInterface::IsLoopBack);
        description.isPointToPoint = flags.testFlag(QNetworkInterface::IsPointToPoint);
        for (const QNetworkAddressEntry& entry : interface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            description.entries.push_back(InterfaceDescription::Entry{entry.ip(), entry.broadcast()});
        }
        out.push_back(description);
    }
    return out;
}

QStringList localBroadcastAddresses() {
    return chooseBroadcastAddresses(readLocalInterfaces());
}

}  // namespace wakedeck
