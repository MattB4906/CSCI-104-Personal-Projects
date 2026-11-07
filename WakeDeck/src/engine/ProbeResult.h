// ProbeResult: what one readiness check observed, in words the interface can show.
#pragma once

#include <QString>

namespace wakedeck {

enum class ProbeStatus {
    Ready,          // the service answered as expected
    NotReady,       // something answered on the port, but not with the expected reply
    Refused,        // the computer is up and refused the connection: nothing listens there yet
    TimedOut,       // no answer at all within the time limit
    Unreachable,    // no route to the host, or the network is down on this side
    ResolveFailed,  // the name could not be turned into an address
    Error           // anything else; `detail` says what
};

struct ProbeResult {
    ProbeStatus status = ProbeStatus::Error;
    QString detail;  // human-readable, e.g. "hostname PC, state free" or "Connection refused"
    int elapsedMs = 0;

    bool ready() const { return status == ProbeStatus::Ready; }
};

// Lower-case words for a status: "ready", "refused", "timed out", ...
// (Not named toString: Qt Test looks that name up for its own purposes.)
QString statusName(ProbeStatus status);

}  // namespace wakedeck
