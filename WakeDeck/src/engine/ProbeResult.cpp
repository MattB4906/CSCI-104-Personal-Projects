#include "ProbeResult.h"

namespace wakedeck {

QString statusName(ProbeStatus status) {
    switch (status) {
    case ProbeStatus::Ready: return QStringLiteral("ready");
    case ProbeStatus::NotReady: return QStringLiteral("not ready");
    case ProbeStatus::Refused: return QStringLiteral("refused");
    case ProbeStatus::TimedOut: return QStringLiteral("timed out");
    case ProbeStatus::Unreachable: return QStringLiteral("unreachable");
    case ProbeStatus::ResolveFailed: return QStringLiteral("name not resolved");
    case ProbeStatus::Error: break;
    }
    return QStringLiteral("error");
}

}  // namespace wakedeck
