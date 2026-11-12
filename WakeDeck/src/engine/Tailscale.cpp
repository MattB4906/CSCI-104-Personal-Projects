#include "Tailscale.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <algorithm>
#include <utility>

namespace wakedeck {
namespace {

QString withoutTrailingDot(const QString& name) {
    QString out = name.trimmed();
    while (out.endsWith(QLatin1Char('.'))) out.chop(1);
    return out;
}

QString firstLabel(const QString& name) {
    return withoutTrailingDot(name).section(QLatin1Char('.'), 0, 0);
}

// Tailscale writes RFC 3339 times with up to nine fraction digits; Qt's ISO
// parser is given the time without the fraction. The zero time (year 1) means
// "never" and is returned as invalid, like anything unparsable.
QDateTime parseLastSeen(const QString& text) {
    static const QRegularExpression pattern(
        QStringLiteral("^(\\d{4}-\\d\\d-\\d\\dT\\d\\d:\\d\\d:\\d\\d)(\\.\\d+)?(Z|[+-]\\d\\d:\\d\\d)$"));
    const QRegularExpressionMatch match = pattern.match(text.trimmed());
    if (!match.hasMatch()) return {};
    const QDateTime at = QDateTime::fromString(match.captured(1) + match.captured(3), Qt::ISODate);
    if (!at.isValid() || at.date().year() <= 1) return {};
    return at;
}

TailscaleStatus unavailable(const QString& why) {
    TailscaleStatus status;
    status.error = why;
    return status;
}

}  // namespace

QString TailscalePeer::displayName() const {
    const QString label = firstLabel(dnsName);
    return label.isEmpty() ? hostName.trimmed() : label;
}

TailscaleStatus parseTailscaleStatus(const QByteArray& json) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (document.isNull() || !document.isObject()) {
        return unavailable(QStringLiteral("the answer is not a JSON object"));
    }
    const QJsonObject root = document.object();
    const QJsonValue state = root.value(QLatin1String("BackendState"));
    if (!state.isString() || state.toString().trimmed().isEmpty()) {
        return unavailable(QStringLiteral("the answer has no BackendState"));
    }
    TailscaleStatus status;
    status.available = true;
    status.backendState = state.toString().trimmed();
    const QJsonObject peers = root.value(QLatin1String("Peer")).toObject();
    for (auto it = peers.begin(); it != peers.end(); ++it) {
        if (!it.value().isObject()) continue;
        const QJsonObject entry = it.value().toObject();
        TailscalePeer peer;
        peer.hostName = entry.value(QLatin1String("HostName")).toString();
        peer.dnsName = entry.value(QLatin1String("DNSName")).toString();
        for (const QJsonValue& address : entry.value(QLatin1String("TailscaleIPs")).toArray()) {
            if (address.isString()) peer.addresses << address.toString().trimmed();
        }
        peer.online = entry.value(QLatin1String("Online")).toBool(false);
        peer.lastSeen = parseLastSeen(entry.value(QLatin1String("LastSeen")).toString());
        status.peers.push_back(std::move(peer));
    }
    return status;
}

const TailscalePeer* findTailscalePeer(const TailscaleStatus& status, const QString& host) {
    const QString wanted = withoutTrailingDot(host);
    if (wanted.isEmpty()) return nullptr;
    const bool bare = !wanted.contains(QLatin1Char('.')) && !wanted.contains(QLatin1Char(':'));
    for (const TailscalePeer& peer : status.peers) {
        for (const QString& address : peer.addresses) {
            if (address.compare(wanted, Qt::CaseInsensitive) == 0) return &peer;
        }
        if (!peer.dnsName.isEmpty() && withoutTrailingDot(peer.dnsName).compare(wanted, Qt::CaseInsensitive) == 0) {
            return &peer;
        }
        if (bare) {
            if (!peer.dnsName.isEmpty() && firstLabel(peer.dnsName).compare(wanted, Qt::CaseInsensitive) == 0) return &peer;
            if (!peer.hostName.isEmpty() && peer.hostName.trimmed().compare(wanted, Qt::CaseInsensitive) == 0) return &peer;
        }
    }
    return nullptr;
}

QString formatAgo(qint64 seconds) {
    if (seconds < 60) return QStringLiteral("less than a minute ago");
    if (seconds < 3600) return QStringLiteral("%1 min ago").arg(seconds / 60);
    if (seconds < 48 * 3600) return QStringLiteral("%1 h ago").arg(seconds / 3600);
    return QStringLiteral("%1 days ago").arg(seconds / 86400);
}

QString describeTailscale(const TailscaleStatus& status, const QString& host, const QDateTime& now) {
    if (!status.available) return {};
    if (!status.running()) {
        return QStringLiteral("Tailscale is not connected on this Mac (state: %1)").arg(status.backendState);
    }
    const TailscalePeer* peer = findTailscalePeer(status, host);
    if (!peer) return QStringLiteral("Tailscale is connected; %1 is not one of its devices").arg(host.trimmed());
    if (peer->online) return QStringLiteral("Tailscale is connected; it shows %1 online").arg(peer->displayName());
    if (peer->lastSeen.isValid()) {
        return QStringLiteral("Tailscale is connected; it shows %1 offline, last seen %2")
            .arg(peer->displayName(), formatAgo(std::max<qint64>(0, peer->lastSeen.secsTo(now))));
    }
    return QStringLiteral("Tailscale is connected; it shows %1 offline").arg(peer->displayName());
}

QStringList tailscaleCliCandidates() {
    const QByteArray chosen = qgetenv("WAKEDECK_TAILSCALE_CLI");
    if (!chosen.isEmpty()) return {QString::fromLocal8Bit(chosen)};
    return {QStringLiteral("/Applications/Tailscale.app/Contents/MacOS/Tailscale"), QStringLiteral("/usr/local/bin/tailscale"),
            QStringLiteral("/opt/homebrew/bin/tailscale")};
}

QString findTailscaleCli() {
    for (const QString& candidate : tailscaleCliCandidates()) {
        const QFileInfo info(candidate);
        if (info.isFile() && info.isExecutable()) return info.absoluteFilePath();
    }
    return {};
}

ProcessTailscaleReader::ProcessTailscaleReader(QObject* parent, int limitMs) : TailscaleReader(parent), limitMs_(limitMs) {}

ProcessTailscaleReader::~ProcessTailscaleReader() {
    if (process_ && process_->state() != QProcess::NotRunning) {
        process_->disconnect(this);
        process_->kill();
        process_->waitForFinished(1000);
    }
}

void ProcessTailscaleReader::start() {
    if (started_) return;
    started_ = true;
    const QString tool = findTailscaleCli();
    if (tool.isEmpty()) {
        QTimer::singleShot(0, this, [this] { finish(unavailable(QStringLiteral("Tailscale's command-line tool was not found"))); });
        return;
    }
    process_ = new QProcess(this);
    process_->setProgram(tool);
    process_->setArguments({QStringLiteral("status"), QStringLiteral("--json")});
    process_->setStandardInputFile(QProcess::nullDevice());
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        // Queued: QProcess may report this from inside start().
        if (error != QProcess::FailedToStart) return;
        QTimer::singleShot(0, this, [this] { finish(unavailable(QStringLiteral("Tailscale's command-line tool could not be started"))); });
    });
    connect(process_, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        TailscaleStatus status = parseTailscaleStatus(process_->readAllStandardOutput());
        if (!status.available) {
            if (exitStatus != QProcess::NormalExit) {
                status.error = QStringLiteral("Tailscale's command-line tool crashed");
            } else if (exitCode != 0) {
                status.error = QStringLiteral("Tailscale's command-line tool exited with code %1").arg(exitCode);
            }
        }
        finish(std::move(status));
    });
    limit_ = new QTimer(this);
    limit_->setSingleShot(true);
    connect(limit_, &QTimer::timeout, this, [this] {
        finish(unavailable(QStringLiteral("Tailscale's command-line tool did not answer within %1 ms").arg(limitMs_)));
    });
    limit_->start(limitMs_);
    process_->start();
}

void ProcessTailscaleReader::finish(TailscaleStatus status) {
    if (done_) return;
    done_ = true;
    if (limit_) limit_->stop();
    if (process_ && process_->state() != QProcess::NotRunning) {
        process_->disconnect(this);
        process_->kill();  // the destructor waits for it
    }
    emit finished(status);
}

TailscaleReader* makeTailscaleReader(QObject* parent) {
    return new ProcessTailscaleReader(parent);
}

}  // namespace wakedeck
