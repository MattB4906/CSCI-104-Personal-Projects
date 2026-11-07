// Explain: turns what a session observed into plain-language text for the
// moment it gives up, plus small formatting helpers shared by the interface.
#pragma once

#include <QDateTime>
#include <QString>
#include <vector>

#include "ProbeResult.h"
#include "ServiceProbe.h"

namespace wakedeck {

struct Observation {
    QDateTime at;
    ProbeResult result;
};

struct Observations {
    ProbeTarget check;
    bool wakeConfigured = false;
    bool wakeSent = false;
    QDateTime wakeAt;
    QString wakeSummary;
    std::vector<Observation> checks;
    int elapsedMs = 0;
};

// "0:05", "2:00", "12:34" (minutes:seconds, rounded down).
QString formatDuration(int ms);

// Tailscale hands out 100.64.0.0/10 addresses and *.ts.net names.
bool looksLikeTailscale(const QString& host);

// Multi-line text: what was observed, then likely causes. It never claims the
// computer is on or off; it says what answered and what did not.
QString explainTimeout(const Observations& observations);

}  // namespace wakedeck
