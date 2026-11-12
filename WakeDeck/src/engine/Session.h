// Session: one Wake & Connect run as a small state machine driven by the Qt
// event loop. It checks the service, wakes the computer if needed, keeps
// checking until the service is ready or time runs out, then launches the
// client. Everything it does is reported as timeline events, and the parts that
// touch the network or the system come in through factories so tests can
// replace them with fakes.
#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <functional>
#include <optional>

#include "Explain.h"
#include "ProbeResult.h"
#include "QtWaker.h"
#include "ServiceProbe.h"

class QTimer;

namespace wakedeck {

struct SessionConfig {
    ProbeTarget check;
    std::optional<WakeConfig> wake;  // std::nullopt: never send a wake request
    // While waiting after a wake request, send the whole request again this long
    // after the previous send ended; 0 sends it once. Profiles set it from
    // WakeConfig::resendSeconds; milliseconds so tests can use small values.
    int resendIntervalMs = 0;
    int pollIntervalMs = 3000;
    int overallTimeoutMs = 120000;
};

enum class SessionState { Idle, Checking, Waking, Waiting, Launching, Done, Failed };
enum class Outcome { ReadyWithoutWake, ReadyAfterWake, TimedOut, WakeFailed, LaunchFailed, Cancelled };
QString stateName(SessionState state);
QString outcomeName(Outcome outcome);

enum class EventKind { Info, WakeSent, Ready, Failure };
struct TimelineEvent {
    QDateTime at;
    EventKind kind = EventKind::Info;
    QString text;
};

// Opens the client once the service is ready. ClientLauncher (ClientLauncher.h)
// is the real one; NoLauncher stands in when no client is configured.
class Launcher : public QObject {
    Q_OBJECT
public:
    explicit Launcher(QObject* parent = nullptr) : QObject(parent) {}
    virtual void start() = 0;
    virtual bool configured() const { return true; }      // false: nothing will be launched
    virtual QString describe() const { return QStringLiteral("the client"); }
    // Valid once finished(true, ...) has been emitted; 0 when none or unknown.
    // launchedPid: the process this launcher itself started; 0 when the system
    // only brought an already-running copy of the client to the front.
    // runningPid: the client's process either way, also that already-running copy.
    virtual qint64 launchedPid() const { return 0; }
    virtual qint64 runningPid() const { return launchedPid(); }

signals:
    void finished(bool ok, const QString& summary);
};

class NoLauncher final : public Launcher {
    Q_OBJECT
public:
    explicit NoLauncher(QObject* parent = nullptr) : Launcher(parent) {}
    bool configured() const override { return false; }
    QString describe() const override { return QStringLiteral("no client"); }
    void start() override;
};

using LauncherFactory = std::function<Launcher*(QObject* parent)>;

struct SessionFactories {
    ProbeFactory probe = makeProbe;
    WakerFactory waker = makeWaker;
    LauncherFactory launcher = [](QObject* parent) -> Launcher* { return new NoLauncher(parent); };
};

class Session final : public QObject {
    Q_OBJECT
public:
    explicit Session(SessionConfig config, SessionFactories factories = SessionFactories(),
                     QObject* parent = nullptr);

    void start();
    void cancel();
    void extendTimeout(int extraMs);  // "Keep waiting"

    SessionState state() const { return state_; }
    const Observations& observations() const { return observations_; }
    // After a successful launch: the client's process ids as its launcher reported
    // them (0: none or unknown, see Launcher) and its description of the client.
    qint64 launchedPid() const { return launchedPid_; }
    qint64 runningPid() const { return runningPid_; }
    QString launchedClient() const { return launchedClient_; }

signals:
    void stateChanged(wakedeck::SessionState state);
    void timelineEvent(const wakedeck::TimelineEvent& event);
    void finished(wakedeck::Outcome outcome, const QString& summary);

private:
    void setState(SessionState state);
    void emitEvent(EventKind kind, const QString& text);
    void runCheck();
    void onProbeFinished(const ProbeResult& result);
    void scheduleNextCheck();
    void sendWake();
    void onWakeFinished(bool allSent, const QString& summary);
    void scheduleResend();
    void launch();
    void onLaunchFinished(bool ok, const QString& summary);
    void finish(Outcome outcome, const QString& summary);
    int elapsedMs() const;

    SessionConfig config_;
    SessionFactories factories_;
    SessionState state_ = SessionState::Idle;
    QElapsedTimer elapsed_;
    Observations observations_;
    int checksRun_ = 0;
    bool wakeSent_ = false;
    bool finished_ = false;
    qint64 launchedPid_ = 0;
    qint64 runningPid_ = 0;
    QString launchedClient_;
    Probe* probe_ = nullptr;
    Waker* waker_ = nullptr;
    Launcher* launcher_ = nullptr;
    QTimer* resendTimer_ = nullptr;  // single shot; armed only while waiting with no waker in flight
};

// "1st", "2nd", "3rd", "4th", "11th", "21st"
QString ordinal(int n);

}  // namespace wakedeck
