#include "ClientLauncher.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <utility>

#include "AppLocator.h"

namespace wakedeck {
namespace {

bool isBundle(const QString& path) {
    return path.endsWith(QLatin1String(".app"), Qt::CaseInsensitive) && QFileInfo(path).isDir();
}

// Finds Moonlight: the configured path first, then Launch Services, then the
// usual folders. Records every place tried.
std::optional<QString> findMoonlight(const QString& configuredPath, QStringList& searched) {
    if (!configuredPath.isEmpty()) {
        searched << configuredPath;
        if (QFileInfo::exists(configuredPath)) return configuredPath;
    }
    searched << QStringLiteral("Launch Services, bundle id %1").arg(QLatin1String(kMoonlightBundleId));
    if (const auto registered = platform::applicationPathForBundleId(QLatin1String(kMoonlightBundleId))) {
        if (QFileInfo::exists(*registered)) return registered;
    }
    const QStringList folders = {QStringLiteral("/Applications"),
                                 QDir::homePath() + QStringLiteral("/Applications")};
    for (const QString& folder : folders) {
        const QString candidate = folder + QStringLiteral("/Moonlight.app");
        searched << candidate;
        if (QFileInfo::exists(candidate)) return candidate;
    }
    return std::nullopt;
}

// A plan for an application bundle: either open it, or run its executable with arguments.
std::optional<LaunchPlan> planForBundle(const QString& appPath, const QStringList& arguments, QString& error) {
    LaunchPlan plan;
    if (arguments.isEmpty()) {
        plan.program = appPath;
        plan.openBundle = true;
        return plan;
    }
    const auto executable = platform::bundleExecutable(appPath);
    if (!executable) {
        error = QStringLiteral("%1 is not a valid application bundle (no executable inside)").arg(appPath);
        return std::nullopt;
    }
    plan.program = *executable;
    plan.arguments = arguments;
    return plan;
}

}  // namespace

QStringList substituteHost(const QStringList& arguments, const QString& host) {
    QStringList out;
    for (const QString& argument : arguments) {
        out << QString(argument).replace(QLatin1String("{host}"), host);
    }
    return out;
}

LaunchResolution resolveLaunch(const ClientConfig& client, const QString& host) {
    LaunchResolution resolution;
    QString error;

    switch (client.preset) {
    case ClientConfig::Preset::None:
        resolution.error = QStringLiteral("no client is configured for this computer");
        return resolution;

    case ClientConfig::Preset::Moonlight: {
        const auto app = findMoonlight(client.path, resolution.searched);
        if (!app) {
            resolution.error = QStringLiteral("Moonlight was not found. Looked in: %1. Install it from "
                                              "moonlight-stream.org, or set the client path in the profile.")
                                   .arg(resolution.searched.join(QStringLiteral("; ")));
            return resolution;
        }
        const bool stream = client.launchMode == QLatin1String("stream");
        const QStringList arguments = stream ? QStringList{QStringLiteral("stream"), host, client.appName} : QStringList{};
        std::optional<LaunchPlan> plan;
        if (isBundle(*app)) {
            plan = planForBundle(*app, arguments, error);
        } else if (QFileInfo(*app).isExecutable()) {
            plan = LaunchPlan{*app, arguments, false, QString()};
        } else {
            error = QStringLiteral("%1 is neither an application bundle nor an executable").arg(*app);
        }
        if (!plan) {
            resolution.error = error;
            return resolution;
        }
        plan->description = stream ? QStringLiteral("Moonlight, stream \"%1\" on %2").arg(client.appName, host)
                                   : QStringLiteral("Moonlight");
        resolution.plan = *plan;
        return resolution;
    }

    case ClientConfig::Preset::Custom: {
        resolution.searched << client.path;
        const QFileInfo info(client.path);
        if (!info.exists()) {
            resolution.error = QStringLiteral("the client %1 was not found").arg(client.path);
            return resolution;
        }
        const QStringList arguments = substituteHost(client.arguments, host);
        std::optional<LaunchPlan> plan;
        if (isBundle(client.path)) {
            plan = planForBundle(client.path, arguments, error);
        } else if (info.isFile() && info.isExecutable()) {
            plan = LaunchPlan{client.path, arguments, false, QString()};
        } else {
            error = QStringLiteral("%1 is not executable").arg(client.path);
        }
        if (!plan) {
            resolution.error = error;
            return resolution;
        }
        plan->description = QStringLiteral("custom client %1%2")
                                .arg(info.fileName(), arguments.isEmpty() ? QString() : QStringLiteral(" ") + arguments.join(QLatin1Char(' ')));
        resolution.plan = *plan;
        return resolution;
    }
    }
    resolution.error = QStringLiteral("unknown client preset");
    return resolution;
}

ClientLauncher::ClientLauncher(ClientConfig client, QString host, QObject* parent)
    : Launcher(parent), client_(std::move(client)), host_(std::move(host)) {}

QString ClientLauncher::describe() const {
    const LaunchResolution resolution = resolveLaunch(client_, host_);
    if (resolution.plan) return resolution.plan->description;
    return QStringLiteral("%1 client").arg(presetName(client_.preset));
}

void ClientLauncher::start() {
    const LaunchResolution resolution = resolveLaunch(client_, host_);
    if (!resolution.plan) {
        const QString error = resolution.error;
        QTimer::singleShot(0, this, [this, error] { finish(false, error); });
        return;
    }
    const LaunchPlan plan = *resolution.plan;
    if (plan.openBundle) {
        platform::openApplication(plan.program, this, [this, plan](bool ok, const QString& error) {
            finish(ok, ok ? QStringLiteral("Opened %1").arg(plan.description)
                          : QStringLiteral("could not open %1: %2").arg(plan.program, error));
        });
        return;
    }
    qint64 pid = 0;
    const bool started = QProcess::startDetached(plan.program, plan.arguments, QDir::homePath(), &pid);
    const QString summary = started
        ? QStringLiteral("Launched %1 (process %2)").arg(plan.description).arg(pid)
        : QStringLiteral("could not start %1 %2").arg(plan.program, plan.arguments.join(QLatin1Char(' ')));
    QTimer::singleShot(0, this, [this, started, summary] { finish(started, summary); });
}

void ClientLauncher::finish(bool ok, const QString& summary) {
    if (done_) return;
    done_ = true;
    emit finished(ok, summary);
}

LauncherFactory makeLauncherFactory(const ClientConfig& client, const QString& host) {
    if (client.preset == ClientConfig::Preset::None) {
        return [](QObject* parent) -> Launcher* { return new NoLauncher(parent); };
    }
    return [client, host](QObject* parent) -> Launcher* { return new ClientLauncher(client, host, parent); };
}

}  // namespace wakedeck
