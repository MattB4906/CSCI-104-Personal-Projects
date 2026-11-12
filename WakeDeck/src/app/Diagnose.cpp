#include "Diagnose.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QTimer>
#include <cstdio>
#include <memory>

#include "SystemServices.h"
#include "engine/Tailscale.h"

namespace wakedeck::app {
namespace {

constexpr double kPictureFraction = 0.4;
constexpr int kPermissionWaitMs = 5000;

// The notification permission, read without asking; "unavailable" off cocoa,
// without a bundle identifier, or when the system does not answer in time.
QString notificationPermission() {
    if (!systemServicesAvailable()) return QStringLiteral("unavailable");
    auto answer = std::make_shared<QString>();
    QEventLoop loop;
    QTimer limit;
    limit.setSingleShot(true);
    QObject::connect(&limit, &QTimer::timeout, &loop, &QEventLoop::quit);
    // The answer arrives on this thread through the event loop below; `answer`
    // is shared, so an answer that comes after the limit has somewhere to go.
    platform::readNotificationPermission([answer](const QString& permission) { *answer = permission; });
    if (answer->isEmpty()) {
        QTimer poll;
        QObject::connect(&poll, &QTimer::timeout, &loop, [&loop, answer] {
            if (!answer->isEmpty()) loop.quit();
        });
        poll.start(20);
        limit.start(kPermissionWaitMs);
        loop.exec();
    }
    return answer->isEmpty() ? QStringLiteral("unavailable") : *answer;
}

// Tailscale's own status, read the way a run reads it (same tool, same limit).
// Only the state and the number of devices are reported: no names, no addresses.
TailscaleStatus tailscaleStatus() {
    TailscaleStatus result;
    bool done = false;
    QEventLoop loop;
    auto* reader = makeTailscaleReader(nullptr);
    QObject::connect(reader, &TailscaleReader::finished, &loop, [&](const TailscaleStatus& status) {
        result = status;
        done = true;
        loop.quit();
    });
    reader->start();
    if (!done) loop.exec();  // the reader always finishes, at the latest at its limit
    delete reader;
    return result;
}

}  // namespace

std::optional<QString> diagnoseTarget(const QStringList& arguments) {
    const int at = arguments.indexOf(QStringLiteral("--diagnose"));
    if (at < 0) return std::nullopt;
    return at + 1 < arguments.size() ? arguments.at(at + 1) : QString();
}

int runDiagnose(const QString& path) {
    if (path.isEmpty()) {
        std::fprintf(stderr, "WakeDeck: --diagnose needs a file name\n");
        return 2;
    }
    const QString file = QFileInfo(path).absoluteFilePath();

    const QString picture = file + QStringLiteral(".dock.png");
    QString pictureError;
    if (!platform::renderDockProgressPicture(kPictureFraction, picture, &pictureError)) {
        std::fprintf(stderr, "WakeDeck: could not write %s: %s\n", qPrintable(picture), qPrintable(pictureError));
    }

    const QString bundleId = platform::applicationBundleIdentifier();
    QStringList lines;
    lines << QStringLiteral("version=%1").arg(QCoreApplication::applicationVersion());
    lines << QStringLiteral("platform=%1").arg(QGuiApplication::platformName());
    lines << QStringLiteral("bundleId=%1").arg(bundleId.isEmpty() ? QStringLiteral("none") : bundleId);
    lines << QStringLiteral("notifications=%1").arg(notificationPermission());
    lines << QStringLiteral("dockTile=%1").arg(systemServicesAvailable() && platform::dockTileAvailable()
                                                   ? QStringLiteral("available")
                                                   : QStringLiteral("unavailable"));
    const QString tailscaleCli = findTailscaleCli();
    const TailscaleStatus tailscale = tailscaleStatus();
    lines << QStringLiteral("tailscaleCli=%1").arg(tailscaleCli.isEmpty() ? QStringLiteral("not found") : tailscaleCli);
    lines << QStringLiteral("tailscaleState=%1")
                 .arg(tailscale.available ? tailscale.backendState : QStringLiteral("error: %1").arg(tailscale.error));
    lines << QStringLiteral("tailscalePeers=%1").arg(tailscale.peers.size());

    QFile out(file);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::fprintf(stderr, "WakeDeck: could not write %s: %s\n", qPrintable(file), qPrintable(out.errorString()));
        return 1;
    }
    const QByteArray text = (lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
    if (out.write(text) != text.size() || !out.flush()) {
        std::fprintf(stderr, "WakeDeck: could not write %s: %s\n", qPrintable(file), qPrintable(out.errorString()));
        return 1;
    }
    out.close();
    return 0;
}

}  // namespace wakedeck::app
