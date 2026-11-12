// WaitProgress: how far along the wait for the service is, as one pure
// function that both the progress bar in the window and the Dock icon use.
#pragma once

#include <QString>

namespace wakedeck::app {

struct WaitProgress {
    double fraction = 0.0;  // 0 to kMaxWaitFraction; never "done" while still waiting
    QString text;           // "0:12 of up to 2:00", or with a typical time "usually about 0:30 — 0:12 so far"
};

// The bar never reaches the end while WakeDeck is still waiting: only the
// service answering ends the wait.
constexpr double kMaxWaitFraction = 0.95;

// elapsedMs: time since the run started waking or waiting. typicalMs: how long
// this computer usually takes (0 = unknown). limitMs: the run's time limit,
// including any "Keep waiting". Without a typical time the fraction is elapsed
// over limit; with one it is elapsed over the typical time. Either way it is
// capped at kMaxWaitFraction.
WaitProgress waitProgress(qint64 elapsedMs, qint64 typicalMs, qint64 limitMs);

}  // namespace wakedeck::app
