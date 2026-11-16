// Moonlight: reads Moonlight's preferences file to import configured hosts.
// Supports both macOS NativeFormat (.plist) and IniFormat (.ini, .conf).
#pragma once

#include <QByteArray>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

#include "core/MacAddress.h"
#include "engine/Profile.h"

namespace wakedeck {

struct MoonlightHost {
    QString name;
    QString manualAddress;
    int manualPort = 0;
    QString localAddress;
    int localPort = 0;
    QString remoteAddress;
    int remotePort = 0;
    std::optional<MacAddress> mac;
    QStringList apps;

    QString preferredHost() const;
    int preferredPort() const;
    Profile toProfile() const;
};

struct MoonlightReadResult {
    bool ok = false;
    QString error;
    std::vector<MoonlightHost> hosts;
};

// Returns $WAKEDECK_MOONLIGHT_PLIST if set; otherwise
// ~/Library/Preferences/com.moonlight-stream.Moonlight.plist.
QString defaultMoonlightPrefsPath();

// Reads hosts from QSettings (hosts array, or hostsbackup if hosts is empty).
MoonlightReadResult readMoonlightSettings(QSettings& settings, const QString& source = QString());

// Opens the given file (NativeFormat for .plist, IniFormat for .ini/.conf) and reads its hosts.
MoonlightReadResult readMoonlightFile(const QString& path = defaultMoonlightPrefsPath());

}  // namespace wakedeck
