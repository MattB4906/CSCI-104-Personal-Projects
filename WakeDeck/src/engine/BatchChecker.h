// BatchChecker: runs readiness probes across multiple profiles asynchronously.
// Never fires signals synchronously inside start(); emits itemFinished as each
// completes, and finished once when all probes have completed.
#pragma once

#include <QObject>
#include <functional>
#include <vector>

#include "Profile.h"
#include "ServiceProbe.h"

namespace wakedeck {

struct BatchCheckEntry {
    QString profileName;
    ProbeTarget target;
    ProbeResult result;
};

class BatchChecker : public QObject {
    Q_OBJECT
public:
    using ProbeForProfile = std::function<Probe*(const Profile&, QObject* parent)>;

    static ProbeForProfile defaultProbeForProfile();

    explicit BatchChecker(std::vector<Profile> profiles,
                          ProbeForProfile probeForProfile = defaultProbeForProfile(),
                          int timeoutMsOverride = 0,
                          QObject* parent = nullptr);

    void start();

signals:
    void itemFinished(int index, const wakedeck::BatchCheckEntry& entry);
    void finished(const std::vector<wakedeck::BatchCheckEntry>& entries);

private:
    void onProbeFinished(int index, const ProbeResult& result);

    std::vector<Profile> profiles_;
    ProbeForProfile probeForProfile_;
    int timeoutMsOverride_ = 0;
    std::vector<BatchCheckEntry> entries_;
    int completedCount_ = 0;
    bool finishedEmitted_ = false;
};

}  // namespace wakedeck
