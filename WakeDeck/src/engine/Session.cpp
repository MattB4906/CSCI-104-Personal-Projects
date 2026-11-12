#include "Session.h"

#include <QTimer>
#include <algorithm>
#include <limits>
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
    case Outcome::TailscaleOff: return QStringLiteral("Tailscale not connected");
    case Outcome::Cancelled: break;
    }
    return QStringLiteral("cancelled");
}

QString ordinal(int n) {
    const int lastTwo = n % 100;
    const int last = n % 10;
    const char* suffix = "th";
    if (lastTwo < 11 || lastTwo > 13) {
        if (last == 1) suffix = "st";
        if (last == 2) suffix = "nd";
        if (last == 3) suffix = "rd";
    }
    return QString::number(n) + QLatin1String(suffix);
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
    // A Tailscale host: first ask Tailscale on this Mac, without blocking. Any
    // other host, or no answer from the tool, goes straight to the checks.
    if (looksLikeTailscale(config_.check.host) && factories_.tailscale) {
        tailscale_ = factories_.tailscale(this);
        if (tailscale_) {
            connect(tailscale_, &TailscaleReader::finished, this, &Session::onTailscaleRead);
            tailscale_->start();
            return;
        }
    }
    beginChecks();
}

void Session::beginChecks() {
    emitEvent(EventKind::Info, QStringLiteral("Checking %1").arg(describe(config_.check)));
    runCheck();
}

void Session::onTailscaleRead(const TailscaleStatus& status) {
    if (tailscale_) {
        tailscale_->deleteLater();
        tailscale_ = nullptr;
    }
    if (finished_) return;

    if (concluding_) {
        // The second read, at the time limit: what Tailscale shows now, attributed to it.
        if (status.available && !status.running()) {
            observations_.tailscale = TailscaleAtTimeout::NotRunning;
        } else if (const TailscalePeer* peer = status.available ? findTailscalePeer(status, config_.check.host) : nullptr) {
            observations_.tailscale = peer->online ? TailscaleAtTimeout::DeviceOnline : TailscaleAtTimeout::DeviceOffline;
        }
        finish(Outcome::TimedOut, explainTimeout(observations_));
        return;
    }

    if (!status.available) {  // missing, failing or slow tool: exactly as without Tailscale
        beginChecks();
        return;
    }
    tailscaleAnswered_ = true;
    if (!status.running()) {
        const QString line = describeTailscale(status, config_.check.host);
        emitEvent(EventKind::Failure, line);
        finish(Outcome::TailscaleOff, QStringLiteral("%1, so %2 cannot be reached. Turn Tailscale on and try again.")
                                          .arg(line, config_.check.host.trimmed()));
        return;
    }
    emitEvent(EventKind::Info, describeTailscale(status, config_.check.host));
    beginChecks();
}

void Session::readTailscale() {
    tailscale_ = factories_.tailscale ? factories_.tailscale(this) : nullptr;
    if (!tailscale_) {
        finish(Outcome::TimedOut, explainTimeout(observations_));
        return;
    }
    connect(tailscale_, &TailscaleReader::finished, this, &Session::onTailscaleRead);
    tailscale_->start();
}

void Session::concludeTimeout() {
    observations_.elapsedMs = elapsedMs();
    emitEvent(EventKind::Failure, QStringLiteral("Timed out after %1").arg(formatDuration(elapsedMs())));
    if (tailscaleAnswered_) {
        concluding_ = true;  // no more checks or resends; read Tailscale once more, then finish
        if (resendTimer_) resendTimer_->stop();
        readTailscale();
        return;
    }
    finish(Outcome::TimedOut, explainTimeout(observations_));
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
        if (wakeSent_ && wakeClock_.isValid()) wakeToReadyMs_ = wakeClock_.elapsed();
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
        concludeTimeout();
        return;
    }
    const int wait = std::min(config_.pollIntervalMs, remaining);
    QTimer::singleShot(wait, this, [this] { runCheck(); });
}

void Session::sendWake() {
    // The first request moves the run to Waking; a resend happens while it
    // waits, and the checks go on meanwhile.
    if (!wakeSent_) setState(SessionState::Waking);
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

    const bool resend = wakeSent_;  // the first request already went out
    if (!allSent) {
        if (resend) {
            // The first request did leave this Mac; a failed repeat does not end the run.
            emitEvent(EventKind::Failure, QStringLiteral("Could not send the wake request again: %1").arg(summary));
            scheduleResend();
            return;
        }
        emitEvent(EventKind::Failure, QStringLiteral("Wake request failed: %1").arg(summary));
        finish(Outcome::WakeFailed, QStringLiteral("The wake request could not be sent: %1").arg(summary));
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    ++observations_.wakeSends;
    observations_.lastWakeAt = now;
    observations_.wakeSummary = summary;
    if (resend) {
        emitEvent(EventKind::WakeSent, QStringLiteral("Wake request sent again (%1 time): %2")
                                           .arg(ordinal(observations_.wakeSends), summary));
        scheduleResend();
        return;
    }
    wakeSent_ = true;
    observations_.wakeSent = true;
    observations_.wakeAt = now;
    wakeClock_.start();
    emitEvent(EventKind::WakeSent, QStringLiteral("Wake request sent: %1").arg(summary));
    if (config_.learned && config_.learned->entries > 0) {
        emitEvent(EventKind::Info, describeLearnedTiming(*config_.learned));
    }
    if (config_.learned && config_.learned->entries > 0 && config_.learned->longerAfterMs > 0) {
        longerTimer_ = new QTimer(this);
        longerTimer_->setSingleShot(true);
        connect(longerTimer_, &QTimer::timeout, this, [this] {
            if (finished_ || concluding_ || state_ != SessionState::Waiting) return;
            emitEvent(EventKind::Info, QStringLiteral("Taking longer than usual"));
        });
        const qint64 after = config_.learned->longerAfterMs;
        longerTimer_->start(static_cast<int>(std::min<qint64>(after, std::numeric_limits<int>::max())));
    }
    emitEvent(EventKind::Info, QStringLiteral("Waiting for %1 (up to %2)")
                                   .arg(describe(config_.check), formatDuration(config_.overallTimeoutMs)));
    setState(SessionState::Waiting);
    scheduleNextCheck();
    scheduleResend();
}

void Session::scheduleResend() {
    // Measured from the end of the previous send, and never while one is in flight.
    if (finished_ || concluding_ || !config_.wake || config_.resendIntervalMs <= 0) return;
    if (state_ != SessionState::Waiting || waker_) return;
    if (!resendTimer_) {
        resendTimer_ = new QTimer(this);
        resendTimer_->setSingleShot(true);
        connect(resendTimer_, &QTimer::timeout, this, [this] {
            if (finished_ || concluding_ || state_ != SessionState::Waiting || waker_) return;
            sendWake();
        });
    }
    resendTimer_->start(config_.resendIntervalMs);
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
            runningPid_ = launcher_->runningPid();
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
    if (resendTimer_) resendTimer_->stop();
    if (longerTimer_) longerTimer_->stop();
    const bool good = outcome == Outcome::ReadyWithoutWake || outcome == Outcome::ReadyAfterWake;
    setState(good ? SessionState::Done : SessionState::Failed);
    emit finished(outcome, summary);
}

}  // namespace wakedeck
