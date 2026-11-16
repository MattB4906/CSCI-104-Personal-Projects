#include "BatchChecker.h"

#include <QTimer>

namespace wakedeck {

BatchChecker::ProbeForProfile BatchChecker::defaultProbeForProfile() {
    return [](const Profile& p, QObject* parent) {
        return makeProbe(p.check, parent);
    };
}

BatchChecker::BatchChecker(std::vector<Profile> profiles,
                           ProbeForProfile probeForProfile,
                           int timeoutMsOverride,
                           QObject* parent)
    : QObject(parent),
      profiles_(std::move(profiles)),
      probeForProfile_(std::move(probeForProfile)),
      timeoutMsOverride_(timeoutMsOverride) {}

void BatchChecker::start() {
    QTimer::singleShot(0, this, [this] {
        if (profiles_.empty()) {
            if (!finishedEmitted_) {
                finishedEmitted_ = true;
                emit finished(entries_);
            }
            return;
        }

        entries_.resize(profiles_.size());
        for (std::size_t i = 0; i < profiles_.size(); ++i) {
            const Profile& p = profiles_[i];
            entries_[i].profileName = p.name;
            entries_[i].target = p.check;
            if (timeoutMsOverride_ > 0) {
                entries_[i].target.timeoutMs = timeoutMsOverride_;
            }
            Probe* probe = probeForProfile_ ? probeForProfile_(p, this) : nullptr;
            if (!probe) {
                ProbeResult res;
                res.status = ProbeStatus::NotReady;
                res.detail = QStringLiteral("could not create probe");
                onProbeFinished(static_cast<int>(i), res);
                continue;
            }
            const int idx = static_cast<int>(i);
            connect(probe, &Probe::finished, this, [this, idx, probe](const ProbeResult& result) {
                onProbeFinished(idx, result);
                probe->deleteLater();
            });
            probe->start();
        }
    });
}

void BatchChecker::onProbeFinished(int index, const ProbeResult& result) {
    if (index >= 0 && index < static_cast<int>(entries_.size())) {
        entries_[static_cast<std::size_t>(index)].result = result;
        emit itemFinished(index, entries_[static_cast<std::size_t>(index)]);
    }
    ++completedCount_;
    if (completedCount_ >= static_cast<int>(profiles_.size()) && !finishedEmitted_) {
        finishedEmitted_ = true;
        emit finished(entries_);
    }
}

}  // namespace wakedeck
