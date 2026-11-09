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

namespace wakedeck {

struct SessionConfig {
    ProbeTarget check;
    std::optional<WakeConfig> wake;  // std::nullopt: never send a wake request
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
    Probe* probe_ = nullptr;
    Waker* waker_ = nullptr;
    Launcher* launcher_ = nullptr;
};

}  // namespace wakedeck
