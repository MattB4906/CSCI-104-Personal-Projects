// Profile: one saved computer, how to wake it, what to check and what to open.
// A plain value type plus its JSON form. Nothing here touches the disk; that is
// ProfileStore's job.
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

#include "QtWaker.h"
#include "ServiceProbe.h"
#include "Session.h"

namespace wakedeck {

// Which client to open once the service is ready. Milestone 4 adds the code
// that locates and launches it; the settings are stored from Milestone 3 on.
struct ClientConfig {
    enum class Preset { None, Moonlight, Custom };
    Preset preset = Preset::None;
    QString path;                                 // executable or .app; empty lets Moonlight be found automatically
    QString launchMode = QStringLiteral("open");  // Moonlight: "open" or "stream"
    QString appName;                              // Moonlight: app to stream when launchMode is "stream"
    QStringList arguments;                        // Custom: arguments; "{host}" is replaced by the check host
};

QString presetName(ClientConfig::Preset preset);  // "none", "moonlight", "custom"
std::optional<ClientConfig::Preset> presetFromName(const QString& name);
QString describe(const ClientConfig& client);  // "Moonlight, stream \"Desktop\"" / "none"

// The one set of rules for a consistent client, used for JSON and for the
// command line alike. Empty when the configuration is fine, otherwise the reason.
QString clientConfigError(const ClientConfig& client);

struct Profile {
    static constexpr int kSchemaVersion = 1;

    QString name;
    QString notes;
    ProbeTarget check;
    std::optional<WakeConfig> wake;  // std::nullopt: never send a wake request
    int pollIntervalMs = 3000;
    int overallTimeoutMs = 120000;
    ClientConfig client;

    SessionConfig toSessionConfig() const;
};

// The JSON form carries "schemaVersion" so future versions can convert old files.
QJsonObject toJson(const Profile& profile);

// Validates every field; on failure returns std::nullopt and a readable reason.
std::optional<Profile> profileFromJson(const QJsonObject& json, QString* error);

// Profile names are compared trimmed and without regard to case.
bool sameProfileName(const QString& a, const QString& b);
const Profile* findProfile(const std::vector<Profile>& profiles, const QString& name);
bool removeProfile(std::vector<Profile>& profiles, const QString& name);
void upsertProfile(std::vector<Profile>& profiles, Profile profile);  // replaces one with the same name

}  // namespace wakedeck
