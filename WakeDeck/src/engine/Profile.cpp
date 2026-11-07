#include "Profile.h"

#include <QJsonArray>
#include <QJsonValue>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace wakedeck {
namespace {

// Reads an optional whole-number field with bounds; absent means `fallback`.
std::optional<int> intField(const QJsonObject& object, const char* key, int fallback, int min, int max,
                            const QString& where, QString* error) {
    if (!object.contains(QLatin1String(key))) return fallback;
    const QJsonValue value = object.value(QLatin1String(key));
    const double number = value.toDouble();
    if (!value.isDouble() || number != std::floor(number) || number < min || number > max) {
        *error = QStringLiteral("%1.%2 must be a whole number from %3 to %4").arg(where, QLatin1String(key)).arg(min).arg(max);
        return std::nullopt;
    }
    return static_cast<int>(number);
}

// Reads an optional string field; absent means `fallback`.
std::optional<QString> stringField(const QJsonObject& object, const char* key, const QString& fallback,
                                   const QString& where, QString* error) {
    if (!object.contains(QLatin1String(key))) return fallback;
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isString()) {
        *error = QStringLiteral("%1.%2 must be text").arg(where, QLatin1String(key));
        return std::nullopt;
    }
    return value.toString();
}

std::optional<ProbeTarget> checkFromJson(const QJsonValue& value, QString* error) {
    if (!value.isObject()) {
        *error = QStringLiteral("check must be an object with at least a host");
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const QString where = QStringLiteral("check");
    ProbeTarget target;

    const auto host = stringField(object, "host", QString(), where, error);
    if (!host) return std::nullopt;
    target.host = host->trimmed();
    if (target.host.isEmpty()) {
        *error = QStringLiteral("check.host is required");
        return std::nullopt;
    }
    const auto kind = stringField(object, "kind", QStringLiteral("sunshine"), where, error);
    if (!kind) return std::nullopt;
    if (*kind == QLatin1String("sunshine")) {
        target.kind = ProbeKind::Sunshine;
    } else if (*kind == QLatin1String("tcp")) {
        target.kind = ProbeKind::Tcp;
    } else {
        *error = QStringLiteral("check.kind must be \"sunshine\" or \"tcp\"");
        return std::nullopt;
    }
    const auto port = intField(object, "port", 47989, 1, 65535, where, error);
    if (!port) return std::nullopt;
    target.port = static_cast<quint16>(*port);
    const auto timeout = intField(object, "timeoutMs", 2000, 100, 60000, where, error);
    if (!timeout) return std::nullopt;
    target.timeoutMs = *timeout;
    return target;
}

std::optional<WakeConfig> wakeFromJson(const QJsonValue& value, QString* error) {
    if (!value.isObject()) {
        *error = QStringLiteral("wake must be an object or null");
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const QString where = QStringLiteral("wake");

    const auto method = stringField(object, "method", QStringLiteral("magicPacket"), where, error);
    if (!method) return std::nullopt;
    if (*method != QLatin1String("magicPacket")) {
        *error = QStringLiteral("wake.method must be \"magicPacket\"");
        return std::nullopt;
    }
    const auto macText = stringField(object, "mac", QString(), where, error);
    if (!macText) return std::nullopt;
    const auto mac = MacAddress::parse(macText->toStdString());
    if (!mac) {
        *error = QStringLiteral("wake.mac is not a valid MAC address (example: 00:00:5E:00:53:2A)");
        return std::nullopt;
    }

    WakeTarget target;
    const auto destination = stringField(object, "destination", QString(), where, error);
    if (!destination) return std::nullopt;
    target.destination = destination->trimmed().toStdString();
    if (target.destination.empty()) {
        *error = QStringLiteral("wake.destination is required (an address or host name)");
        return std::nullopt;
    }
    if (object.contains(QLatin1String("ports"))) {
        const QJsonValue ports = object.value(QLatin1String("ports"));
        if (!ports.isArray() || ports.toArray().isEmpty()) {
            *error = QStringLiteral("wake.ports must be a non-empty list of port numbers");
            return std::nullopt;
        }
        target.ports.clear();
        for (const QJsonValue& entry : ports.toArray()) {
            const double number = entry.toDouble();
            if (!entry.isDouble() || number != std::floor(number) || number < 1 || number > 65535) {
                *error = QStringLiteral("wake.ports entries must be whole numbers from 1 to 65535");
                return std::nullopt;
            }
            target.ports.push_back(static_cast<std::uint16_t>(number));
        }
    }
    const auto repeat = intField(object, "repeat", 5, 1, 100, where, error);
    if (!repeat) return std::nullopt;
    target.repeat = static_cast<unsigned>(*repeat);
    const auto interval = intField(object, "intervalMs", 100, 0, 10000, where, error);
    if (!interval) return std::nullopt;
    target.interval = std::chrono::milliseconds(*interval);
    return WakeConfig{*mac, target};
}

std::optional<ClientConfig> clientFromJson(const QJsonValue& value, QString* error) {
    if (!value.isObject()) {
        *error = QStringLiteral("client must be an object");
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const QString where = QStringLiteral("client");
    ClientConfig client;

    const auto preset = stringField(object, "preset", QStringLiteral("none"), where, error);
    if (!preset) return std::nullopt;
    const auto parsedPreset = presetFromName(*preset);
    if (!parsedPreset) {
        *error = QStringLiteral("client.preset must be \"none\", \"moonlight\" or \"custom\"");
        return std::nullopt;
    }
    client.preset = *parsedPreset;
    const auto path = stringField(object, "path", QString(), where, error);
    if (!path) return std::nullopt;
    client.path = *path;
    const auto launchMode = stringField(object, "launchMode", QStringLiteral("open"), where, error);
    if (!launchMode) return std::nullopt;
    if (*launchMode != QLatin1String("open") && *launchMode != QLatin1String("stream")) {
        *error = QStringLiteral("client.launchMode must be \"open\" or \"stream\"");
        return std::nullopt;
    }
    client.launchMode = *launchMode;
    const auto appName = stringField(object, "appName", QString(), where, error);
    if (!appName) return std::nullopt;
    client.appName = *appName;
    if (object.contains(QLatin1String("arguments"))) {
        const QJsonValue arguments = object.value(QLatin1String("arguments"));
        if (!arguments.isArray()) {
            *error = QStringLiteral("client.arguments must be a list of text values");
            return std::nullopt;
        }
        for (const QJsonValue& entry : arguments.toArray()) {
            if (!entry.isString()) {
                *error = QStringLiteral("client.arguments must be a list of text values");
                return std::nullopt;
            }
            client.arguments << entry.toString();
        }
    }
    return client;
}

}  // namespace

QString presetName(ClientConfig::Preset preset) {
    switch (preset) {
    case ClientConfig::Preset::Moonlight: return QStringLiteral("moonlight");
    case ClientConfig::Preset::Custom: return QStringLiteral("custom");
    case ClientConfig::Preset::None: break;
    }
    return QStringLiteral("none");
}

std::optional<ClientConfig::Preset> presetFromName(const QString& name) {
    const QString lower = name.trimmed().toLower();
    if (lower == QLatin1String("none")) return ClientConfig::Preset::None;
    if (lower == QLatin1String("moonlight")) return ClientConfig::Preset::Moonlight;
    if (lower == QLatin1String("custom")) return ClientConfig::Preset::Custom;
    return std::nullopt;
}

QString describe(const ClientConfig& client) {
    switch (client.preset) {
    case ClientConfig::Preset::Moonlight:
        if (client.launchMode == QLatin1String("stream") && !client.appName.isEmpty()) {
            return QStringLiteral("Moonlight, stream \"%1\"").arg(client.appName);
        }
        return QStringLiteral("Moonlight, open");
    case ClientConfig::Preset::Custom:
        return QStringLiteral("custom: %1 %2").arg(client.path, client.arguments.join(QLatin1Char(' '))).trimmed();
    case ClientConfig::Preset::None: break;
    }
    return QStringLiteral("none");
}

SessionConfig Profile::toSessionConfig() const {
    SessionConfig config;
    config.check = check;
    config.wake = wake;
    config.pollIntervalMs = pollIntervalMs;
    config.overallTimeoutMs = overallTimeoutMs;
    return config;
}

QJsonObject toJson(const Profile& profile) {
    QJsonObject json;
    json[QStringLiteral("schemaVersion")] = Profile::kSchemaVersion;
    json[QStringLiteral("name")] = profile.name;
    json[QStringLiteral("notes")] = profile.notes;

    if (profile.wake) {
        QJsonObject wake;
        wake[QStringLiteral("method")] = QStringLiteral("magicPacket");
        wake[QStringLiteral("mac")] = QString::fromStdString(profile.wake->mac.toString());
        wake[QStringLiteral("destination")] = QString::fromStdString(profile.wake->target.destination);
        QJsonArray ports;
        for (std::uint16_t port : profile.wake->target.ports) ports.append(static_cast<int>(port));
        wake[QStringLiteral("ports")] = ports;
        wake[QStringLiteral("repeat")] = static_cast<int>(profile.wake->target.repeat);
        wake[QStringLiteral("intervalMs")] = static_cast<int>(profile.wake->target.interval.count());
        json[QStringLiteral("wake")] = wake;
    } else {
        json[QStringLiteral("wake")] = QJsonValue::Null;
    }

    QJsonObject check;
    check[QStringLiteral("kind")] = profile.check.kind == ProbeKind::Sunshine ? QStringLiteral("sunshine") : QStringLiteral("tcp");
    check[QStringLiteral("host")] = profile.check.host;
    check[QStringLiteral("port")] = static_cast<int>(profile.check.port);
    check[QStringLiteral("timeoutMs")] = profile.check.timeoutMs;
    json[QStringLiteral("check")] = check;
    json[QStringLiteral("pollIntervalMs")] = profile.pollIntervalMs;
    json[QStringLiteral("overallTimeoutMs")] = profile.overallTimeoutMs;

    QJsonObject client;
    client[QStringLiteral("preset")] = presetName(profile.client.preset);
    client[QStringLiteral("path")] = profile.client.path;
    client[QStringLiteral("launchMode")] = profile.client.launchMode;
    client[QStringLiteral("appName")] = profile.client.appName;
    client[QStringLiteral("arguments")] = QJsonArray::fromStringList(profile.client.arguments);
    json[QStringLiteral("client")] = client;
    return json;
}

std::optional<Profile> profileFromJson(const QJsonObject& json, QString* error) {
    QString ignored;
    QString* reason = error ? error : &ignored;
    const QString where = QStringLiteral("profile");

    if (json.contains(QLatin1String("schemaVersion"))) {
        const int version = json.value(QLatin1String("schemaVersion")).toInt(-1);
        if (version > Profile::kSchemaVersion) {
            *reason = QStringLiteral("this profile was written by a newer WakeDeck (schema %1); update WakeDeck").arg(version);
            return std::nullopt;
        }
        if (version < 1) {
            *reason = QStringLiteral("schemaVersion must be a positive whole number");
            return std::nullopt;
        }
    }

    Profile profile;
    const auto name = stringField(json, "name", QString(), where, reason);
    if (!name) return std::nullopt;
    profile.name = name->trimmed();
    if (profile.name.isEmpty()) {
        *reason = QStringLiteral("name is required");
        return std::nullopt;
    }
    const auto notes = stringField(json, "notes", QString(), where, reason);
    if (!notes) return std::nullopt;
    profile.notes = *notes;

    const auto check = checkFromJson(json.value(QLatin1String("check")), reason);
    if (!check) return std::nullopt;
    profile.check = *check;

    const auto poll = intField(json, "pollIntervalMs", 3000, 1, 600000, where, reason);
    if (!poll) return std::nullopt;
    profile.pollIntervalMs = *poll;
    const auto overall = intField(json, "overallTimeoutMs", 120000, 1, 3600000, where, reason);
    if (!overall) return std::nullopt;
    profile.overallTimeoutMs = *overall;

    const QJsonValue wake = json.value(QLatin1String("wake"));
    if (!wake.isUndefined() && !wake.isNull()) {
        const auto parsed = wakeFromJson(wake, reason);
        if (!parsed) return std::nullopt;
        profile.wake = *parsed;
    }

    if (json.contains(QLatin1String("client"))) {
        const auto client = clientFromJson(json.value(QLatin1String("client")), reason);
        if (!client) return std::nullopt;
        profile.client = *client;
    }
    return profile;
}

bool sameProfileName(const QString& a, const QString& b) {
    return a.trimmed().compare(b.trimmed(), Qt::CaseInsensitive) == 0;
}

const Profile* findProfile(const std::vector<Profile>& profiles, const QString& name) {
    const auto it = std::find_if(profiles.begin(), profiles.end(),
                                 [&name](const Profile& p) { return sameProfileName(p.name, name); });
    return it == profiles.end() ? nullptr : &*it;
}

bool removeProfile(std::vector<Profile>& profiles, const QString& name) {
    const auto it = std::remove_if(profiles.begin(), profiles.end(),
                                   [&name](const Profile& p) { return sameProfileName(p.name, name); });
    if (it == profiles.end()) return false;
    profiles.erase(it, profiles.end());
    return true;
}

void upsertProfile(std::vector<Profile>& profiles, Profile profile) {
    for (Profile& existing : profiles) {
        if (sameProfileName(existing.name, profile.name)) {
            existing = std::move(profile);
            return;
        }
    }
    profiles.push_back(std::move(profile));
}

}  // namespace wakedeck
