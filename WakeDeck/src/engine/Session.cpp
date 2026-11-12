#include "Session.h"

#include <QTimer>
#include <algorithm>
#include <utility>

namespace wakedeck {

QString stateName(SessionState state) {
    switch (state) {
    case SessionState::Idle: return QStringLiteral("idle");
    case SessionState::Checking: return QStringLiteral("checking");
    case SessionState::Waking: return QStringLiteral("waking");
    case SessionState::Waiting: return QStringLiteral("waiting");
    case SessionState::Launching: return QStringLiteral("launching");
    case SessionState::Done: return QStringLiteral("done");
    case SessionState::Failed: break;
    }
    return QStringLiteral("failed");
}

QString outcomeName(Outcome outcome) {
    switch (outcome) {
    case Outcome::ReadyWithoutWake: return QStringLiteral("ready, no wake needed");
    case Outcome::ReadyAfterWake: return QStringLiteral("ready after wake");
    case Outcome::TimedOut: return QStringLiteral("timed out");
    case Outcome::WakeFailed: return QStringLiteral("wake request failed");
    case Outcome::LaunchFailed: return QStringLiteral("client not launched");
    case Outcome::Cancelled: break;
    }
    return QStringLiteral("cancelled");
}

void NoLauncher::start() {
    QTimer::singleShot(0, this, [this] {
        emit finished(true, QStringLiteral("No client configured for this computer; nothing launched."));
    });
}

Session::Session(SessionConfig config, SessionFactories factories, QObject* parent)
    : QObject(parent), config_(std::move(config)), factories_(std::move(factories)) {
    observations_.check = config_.check;
    observations_.wakeConfigured = config_.wake.has_value();
}

void Session::start() {
    if (state_ != SessionState::Idle) return;
    elapsed_.start();
    setState(SessionState::Checking);
    emitEvent(EventKind::Info, QStringLiteral("Checking %1").arg(describe(config_.check)));
    runCheck();
}

void Session::cancel() {
    if (finished_) return;
    emitEvent(EventKind::Failure, QStringLiteral("Cancelled after %1").arg(formatDuration(elapsedMs())));
    finish(Outcome::Cancelled, QStringLiteral("Cancelled after %1.").arg(formatDuration(elapsedMs())));
}

void Session::extendTimeout(int extraMs) {
    if (extraMs > 0) config_.overallTimeoutMs += extraMs;
}

int Session::elapsedMs() const {
    return elapsed_.isValid() ? static_cast<int>(elapsed_.elapsed()) : 0;
}

void Session::setState(SessionState state) {
    if (state_ == state) return;
    state_ = state;
    emit stateChanged(state);
}

void Session::emitEvent(EventKind kind, const QString& text) {
    TimelineEvent e;
    e.at = QDateTime::currentDateTime();
    e.kind = kind;
    e.text = text;
    emit timelineEvent(e);
}

void Session::runCheck() {
    if (finished_) return;
    ++checksRun_;
    probe_ = factories_.probe(config_.check, this);
    connect(probe_, &Probe::finished, this, &Session::onProbeFinished);
    probe_->start();
}

void Session::onProbeFinished(const ProbeResult& result) {
    if (probe_) {
        probe_->deleteLater();
        probe_ = nullptr;
    }
    if (finished_) return;

    observations_.checks.push_back(Observation{QDateTime::currentDateTime(), result});
    observations_.elapsedMs = elapsedMs();

    if (result.ready()) {
        emitEvent(EventKind::Ready, QStringLiteral("%1 ready: %2 (after %3)")
                                        .arg(describe(config_.check), result.detail, formatDuration(elapsedMs())));
        launch();
        return;
    }

    if (state_ == SessionState::Checking) {
        emitEvent(EventKind::Info, QStringLiteral("Not ready: %1 (%2)").arg(statusName(result.status), result.detail));
        if (config_.wake) {
            sendWake();
            return;
        }
        emitEvent(EventKind::Info, QStringLiteral("No wake request configured; waiting for %1 (up to %2)")
                                       .arg(describe(config_.check), formatDuration(config_.overallTimeoutMs)));
        setState(SessionState::Waiting);
        scheduleNextCheck();
        return;
    }

    emitEvent(EventKind::Info, QStringLiteral("Check %1: %2 (waiting %3 of %4)")
                                   .arg(checksRun_)
                                   .arg(statusName(result.status), formatDuration(elapsedMs()),
                                        formatDuration(config_.overallTimeoutMs)));
    scheduleNextCheck();
}

void Session::scheduleNextCheck() {
    if (finished_) return;
    const int remaining = config_.overallTimeoutMs - elapsedMs();
    if (remaining <= 0) {
        observations_.elapsedMs = elapsedMs();
        emitEvent(EventKind::Failure, QStringLiteral("Timed out after %1").arg(formatDuration(elapsedMs())));
        finish(Outcome::TimedOut, explainTimeout(observations_));
        return;
    }
    const int wait = std::min(config_.pollIntervalMs, remaining);
    QTimer::singleShot(wait, this, [this] { runCheck(); });
}

void Session::sendWake() {
    setState(SessionState::Waking);
    waker_ = factories_.waker(*config_.wake, this);
    connect(waker_, &Waker::finished, this, &Session::onWakeFinished);
    waker_->start();
}

void Session::onWakeFinished(bool allSent, const QString& summary) {
    if (waker_) {
        waker_->deleteLater();
        waker_ = nullptr;
    }
    if (finished_) return;

    if (!allSent) {
        emitEvent(EventKind::Failure, QStringLiteral("Wake request failed: %1").arg(summary));
        finish(Outcome::WakeFailed, QStringLiteral("The wake request could not be sent: %1").arg(summary));
        return;
    }
    wakeSent_ = true;
    observations_.wakeSent = true;
    observations_.wakeAt = QDateTime::currentDateTime();
    observations_.wakeSummary = summary;
    emitEvent(EventKind::WakeSent, QStringLiteral("Wake request sent: %1").arg(summary));
    emitEvent(EventKind::Info, QStringLiteral("Waiting for %1 (up to %2)")
                                   .arg(describe(config_.check), formatDuration(config_.overallTimeoutMs)));
    setState(SessionState::Waiting);
    scheduleNextCheck();
}

void Session::launch() {
    setState(SessionState::Launching);
    launcher_ = factories_.launcher(this);
    connect(launcher_, &Launcher::finished, this, &Session::onLaunchFinished);
    if (launcher_->configured()) {
        emitEvent(EventKind::Info, QStringLiteral("Launching %1").arg(launcher_->describe()));
    }
    launcher_->start();
}

void Session::onLaunchFinished(bool ok, const QString& summary) {
    if (launcher_) {
        if (ok && !finished_ && launcher_->configured()) {
            launchedPid_ = launcher_->launchedPid();
            launchedClient_ = launcher_->describe();
        }
        launcher_->deleteLater();
        launcher_ = nullptr;
    }
    if (finished_) return;

    if (!ok) {
        emitEvent(EventKind::Failure, QStringLiteral("Could not launch the client: %1").arg(summary));
        finish(Outcome::LaunchFailed, summary);
        return;
    }
    emitEvent(EventKind::Info, summary);
    if (wakeSent_) {
        finish(Outcome::ReadyAfterWake,
               QStringLiteral("Service ready %1 after the wake request.").arg(formatDuration(elapsedMs())));
    } else {
        finish(Outcome::ReadyWithoutWake, QStringLiteral("Service was already ready; no wake request was needed."));
    }
}

void Session::finish(Outcome outcome, const QString& summary) {
    if (finished_) return;
    finished_ = true;
    const bool good = outcome == Outcome::ReadyWithoutWake || outcome == Outcome::ReadyAfterWake;
    setState(good ? SessionState::Done : SessionState::Failed);
    emit finished(outcome, summary);
}

}  // namespace wakedeck
