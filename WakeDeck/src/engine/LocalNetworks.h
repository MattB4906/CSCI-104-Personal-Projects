// LocalNetworks: which broadcast addresses "every local network this Mac is
// on" means. A pure function chooses them from plain interface descriptions,
// so the rules are tested with synthetic interfaces; a second function builds
// those descriptions from QNetworkInterface. Reading the interface list is a
// local system call, not network traffic.
#pragma once

#include <QHostAddress>
#include <QString>
#include <QStringList>
#include <vector>

namespace wakedeck {

struct InterfaceDescription {
    struct Entry {
        QHostAddress ip;
        QHostAddress broadcast;  // null when the entry has none
    };

    QString name;
    bool isUp = false;
    bool isRunning = false;
    bool canBroadcast = false;
    bool isLoopback = false;
    bool isPointToPoint = false;  // VPN tunnels and similar links
    std::vector<Entry> entries;
};

// The broadcast addresses of the interfaces that are up and running, can
// broadcast, and are neither loopback nor point-to-point; IPv4 entries only,
// entries without a broadcast address skipped, duplicates removed, in order.
QStringList chooseBroadcastAddresses(const std::vector<InterfaceDescription>& interfaces);

// This Mac's interfaces from QNetworkInterface, with their IPv4 entries only.
std::vector<InterfaceDescription> readLocalInterfaces();

// chooseBroadcastAddresses(readLocalInterfaces()): the default address provider.
QStringList localBroadcastAddresses();

}  // namespace wakedeck
