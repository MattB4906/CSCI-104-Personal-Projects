// ClientLauncher: the Launcher that opens the configured client once the
// service is ready. Moonlight is found through Launch Services or in the usual
// folders; a custom client is whatever executable or .app bundle the profile
// names. Arguments may contain {host}, replaced by the computer's address.
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <optional>

#include "Profile.h"
#include "Session.h"

namespace wakedeck {

constexpr const char* kMoonlightBundleId = "com.moonlight-stream.Moonlight";

// What would run, resolved without running anything. Also what --dry-run shows.
struct LaunchPlan {
    QString program;          // executable to start, or the .app bundle when openBundle is set
    QStringList arguments;    // after {host} substitution
    bool openBundle = false;  // true: open or activate the bundle through the system, no arguments
    QString description;      // e.g. "Moonlight, stream \"Desktop\" on pc.example.net"
};

struct LaunchResolution {
    std::optional<LaunchPlan> plan;
    QString error;        // when there is no plan: what was looked for
    QStringList searched; // every place that was tried, for the message
};

LaunchResolution resolveLaunch(const ClientConfig& client, const QString& host);

// "{host}" in each argument becomes the host.
QStringList substituteHost(const QStringList& arguments, const QString& host);

class ClientLauncher final : public Launcher {
    Q_OBJECT
public:
    ClientLauncher(ClientConfig client, QString host, QObject* parent = nullptr);
    bool configured() const override { return client_.preset != ClientConfig::Preset::None; }
    QString describe() const override;
    void start() override;
    // The executable's process id when it was run directly. For an opened
    // bundle, launchedPid is the new process, or 0 when a copy was already
    // running; runningPid is that copy's process.
    qint64 launchedPid() const override { return alreadyRunning_ ? 0 : pid_; }
    qint64 runningPid() const override { return pid_; }

private:
    void finish(bool ok, const QString& summary);

    ClientConfig client_;
    QString host_;
    qint64 pid_ = 0;
    bool alreadyRunning_ = false;
    bool done_ = false;
};

// For Session: a ClientLauncher, or NoLauncher when no client is configured.
LauncherFactory makeLauncherFactory(const ClientConfig& client, const QString& host);

}  // namespace wakedeck
