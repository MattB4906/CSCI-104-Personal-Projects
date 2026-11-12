// Tailscale: what Tailscale on this Mac says about itself and the computer
// WakeDeck checks. WakeDeck runs Tailscale's own command-line tool with
// "status --json" and reads its answer; nothing is stored or sent anywhere.
//
// Parsing and matching are pure functions; TailscaleReader runs the tool
// asynchronously with a time limit, so a missing, failing or slow tool never
// blocks anything and simply yields "unavailable".
#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <vector>

class QProcess;
class QTimer;

namespace wakedeck {

struct TailscalePeer {
    QString hostName;     // "HostName", as the device reports it
    QString dnsName;      // "DNSName", e.g. "living-room-pc.example-tailnet.ts.net." (trailing dot kept)
    QStringList addresses;  // "TailscaleIPs"
    bool online = false;  // "Online"
    QDateTime lastSeen;   // "LastSeen"; invalid when missing, unparsable or the zero time (year 1)

    // The name to show: the first label of the DNS name, or the host name.
    QString displayName() const;
};

struct TailscaleStatus {
    bool available = false;  // false: the tool is missing, failed, timed out or answered garbage
    QString error;           // why it is unavailable
    QString backendState;    // "Running", "Stopped", "NeedsLogin", ...
    std::vector<TailscalePeer> peers;

    bool running() const { return available && backendState == QLatin1String("Running"); }
};

// Reads the JSON printed by "tailscale status --json". A document that is not
// an object or has no string BackendState is unavailable; peers with missing
// fields keep their defaults, and entries that are not objects are skipped.
TailscaleStatus parseTailscaleStatus(const QByteArray& json);

// The peer the check host refers to: one of its addresses, its DNS name
// (ignoring case and a trailing dot) or, for a host without dots, the first
// label of its DNS name or its host name. nullptr when none matches.
const TailscalePeer* findTailscalePeer(const TailscaleStatus& status, const QString& host);

// "Tailscale is connected; it shows living-room-pc online", "... offline,
// last seen 3 h ago", "... <host> is not one of its devices", or for a state
// other than Running "Tailscale is not connected on this Mac (state: Stopped)".
// Empty when the status is unavailable.
QString describeTailscale(const TailscaleStatus& status, const QString& host,
                          const QDateTime& now = QDateTime::currentDateTimeUtc());

// "less than a minute ago", "12 min ago", "3 h ago", "4 days ago".
QString formatAgo(qint64 seconds);

// Where the tool may be, in order. When WAKEDECK_TAILSCALE_CLI is set it is the
// only candidate, even if that path does not exist (tests rely on that).
QStringList tailscaleCliCandidates();
// The first candidate that is an executable file; empty when there is none.
QString findTailscaleCli();

// Reads the status once. Emits finished() exactly once, never from inside start().
class TailscaleReader : public QObject {
    Q_OBJECT
public:
    explicit TailscaleReader(QObject* parent = nullptr) : QObject(parent) {}
    virtual void start() = 0;

signals:
    void finished(const wakedeck::TailscaleStatus& status);
};

// Runs "<tool> status --json" through QProcess. The process is killed when it
// has not finished within the limit, and the status is then unavailable.
class ProcessTailscaleReader final : public TailscaleReader {
    Q_OBJECT
public:
    static constexpr int kDefaultLimitMs = 3000;

    explicit ProcessTailscaleReader(QObject* parent = nullptr, int limitMs = kDefaultLimitMs);
    ~ProcessTailscaleReader() override;
    void start() override;

private:
    void finish(TailscaleStatus status);

    int limitMs_;
    QProcess* process_ = nullptr;
    QTimer* limit_ = nullptr;
    bool started_ = false;
    bool done_ = false;
};

// May return nullptr, which means "do not ask Tailscale at all".
using TailscaleReaderFactory = std::function<TailscaleReader*(QObject* parent)>;
TailscaleReader* makeTailscaleReader(QObject* parent);

}  // namespace wakedeck
