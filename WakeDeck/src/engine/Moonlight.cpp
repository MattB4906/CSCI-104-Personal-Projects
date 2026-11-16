#include "Moonlight.h"

#include <QDir>
#include <QFile>
#include <algorithm>
#include <cstring>

namespace wakedeck {

QString MoonlightHost::preferredHost() const {
    if (!manualAddress.trimmed().isEmpty()) return manualAddress.trimmed();
    if (!localAddress.trimmed().isEmpty()) return localAddress.trimmed();
    return remoteAddress.trimmed();
}

int MoonlightHost::preferredPort() const {
    if (!manualAddress.trimmed().isEmpty() && manualPort > 0) return manualPort;
    if (!localAddress.trimmed().isEmpty() && localPort > 0) return localPort;
    if (!remoteAddress.trimmed().isEmpty() && remotePort > 0) return remotePort;
    return 47989;
}

Profile MoonlightHost::toProfile() const {
    Profile p;
    p.name = name;
    p.check.kind = ProbeKind::Sunshine;
    p.check.host = preferredHost();
    p.check.port = static_cast<quint16>(preferredPort());
    p.check.timeoutMs = 2000;
    p.pollIntervalMs = 3000;
    p.overallTimeoutMs = 120000;

    if (mac.has_value()) {
        const auto& bytes = mac->bytes();
        const bool allZero = std::all_of(bytes.begin(), bytes.end(), [](uint8_t b) { return b == 0; });
        if (!allZero) {
            WakeTarget target;
            target.destination.clear();
            target.ports = {9, 7};
            target.repeat = 5;
            target.interval = std::chrono::milliseconds(100);
            p.wake = WakeConfig(*mac, std::move(target), WakeDestination::LocalNetworks, 15);
        }
    }

    p.client.preset = ClientConfig::Preset::Moonlight;
    auto it = std::find_if(apps.begin(), apps.end(), [](const QString& app) {
        return app.compare(QStringLiteral("Desktop"), Qt::CaseInsensitive) == 0;
    });
    if (it != apps.end()) {
        p.client.launchMode = QStringLiteral("stream");
        p.client.appName = *it;
    } else if (apps.size() == 1) {
        p.client.launchMode = QStringLiteral("stream");
        p.client.appName = apps.front();
    } else {
        p.client.launchMode = QStringLiteral("open");
        p.client.appName.clear();
    }
    return p;
}

QString defaultMoonlightPrefsPath() {
    const QString env = QString::fromLocal8Bit(qgetenv("WAKEDECK_MOONLIGHT_PLIST"));
    if (!env.isEmpty()) return env;
    return QDir::homePath() + QStringLiteral("/Library/Preferences/com.moonlight-stream.Moonlight.plist");
}

MoonlightReadResult readMoonlightSettings(QSettings& settings, const QString& source) {
    const QString src = source.isEmpty() ? settings.fileName() : source;
    int size = settings.beginReadArray(QStringLiteral("hosts"));
    if (size <= 0) {
        settings.endArray();
        size = settings.beginReadArray(QStringLiteral("hostsbackup"));
    }
    if (size <= 0) {
        settings.endArray();
        return {false, QStringLiteral("No Moonlight hosts found in \"%1\"").arg(src), {}};
    }

    std::vector<MoonlightHost> hosts;
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        MoonlightHost host;
        host.name = settings.value(QStringLiteral("hostname")).toString().trimmed();
        if (host.name.isEmpty()) {
            host.name = settings.value(QStringLiteral("name")).toString().trimmed();
        }
        host.manualAddress = settings.value(QStringLiteral("manualaddress")).toString().trimmed();
        host.manualPort = settings.value(QStringLiteral("manualport")).toInt();
        host.localAddress = settings.value(QStringLiteral("localaddress")).toString().trimmed();
        host.localPort = settings.value(QStringLiteral("localport")).toInt();
        host.remoteAddress = settings.value(QStringLiteral("remoteaddress")).toString().trimmed();
        host.remotePort = settings.value(QStringLiteral("remoteport")).toInt();

        const QVariant macVal = settings.value(QStringLiteral("mac"));
        std::optional<MacAddress> parsedMac;
        if (macVal.typeId() == QMetaType::QByteArray) {
            const QByteArray ba = macVal.toByteArray();
            if (ba.size() == 6) {
                MacAddress::Bytes arr;
                std::memcpy(arr.data(), ba.constData(), 6);
                parsedMac = MacAddress(arr);
            }
        } else if (!macVal.isNull()) {
            const QString s = macVal.toString().trimmed();
            if (!s.isEmpty()) {
                parsedMac = MacAddress::parse(s.toStdString());
            }
        }
        if (parsedMac.has_value()) {
            const auto& b = parsedMac->bytes();
            const bool allZero = std::all_of(b.begin(), b.end(), [](uint8_t byte) { return byte == 0; });
            if (!allZero) {
                host.mac = parsedMac;
            }
        }

        const int appCount = settings.beginReadArray(QStringLiteral("apps"));
        if (appCount > 0) {
            for (int a = 0; a < appCount; ++a) {
                settings.setArrayIndex(a);
                const QString appName = settings.value(QStringLiteral("name")).toString().trimmed();
                const bool hidden = settings.value(QStringLiteral("hidden")).toBool();
                if (!appName.isEmpty() && !hidden) {
                    host.apps.append(appName);
                }
            }
            settings.endArray();
        } else {
            settings.endArray();
            const QVariant appsVar = settings.value(QStringLiteral("apps"));
            if (appsVar.canConvert<QVariantList>()) {
                for (const auto& item : appsVar.toList()) {
                    if (item.canConvert<QVariantMap>()) {
                        const QVariantMap map = item.toMap();
                        const QString appName = map.value(QStringLiteral("name")).toString().trimmed();
                        const bool hidden = map.value(QStringLiteral("hidden")).toBool();
                        if (!appName.isEmpty() && !hidden) {
                            host.apps.append(appName);
                        }
                    }
                }
            }
        }

        if (!host.name.isEmpty()) {
            hosts.push_back(std::move(host));
        }
    }
    settings.endArray();

    if (hosts.empty()) {
        return {false, QStringLiteral("No Moonlight hosts found in \"%1\"").arg(src), {}};
    }
    return {true, QString(), std::move(hosts)};
}

MoonlightReadResult readMoonlightFile(const QString& path) {
    if (path.isEmpty() || !QFile::exists(path)) {
        return {false, QStringLiteral("Moonlight preferences file \"%1\" does not exist").arg(path), {}};
    }
    const bool isIni = path.endsWith(QStringLiteral(".ini"), Qt::CaseInsensitive) ||
                       path.endsWith(QStringLiteral(".conf"), Qt::CaseInsensitive);
    QSettings settings(path, isIni ? QSettings::IniFormat : QSettings::NativeFormat);
    return readMoonlightSettings(settings, path);
}

}  // namespace wakedeck
