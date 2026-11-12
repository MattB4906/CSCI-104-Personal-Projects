#include "WaitProgress.h"

#include <algorithm>
#include <limits>

#include "engine/Explain.h"

namespace wakedeck::app {
namespace {

int clampToInt(qint64 ms) {
    return static_cast<int>(std::clamp<qint64>(ms, 0, std::numeric_limits<int>::max()));
}

}  // namespace

WaitProgress waitProgress(qint64 elapsedMs, qint64 typicalMs, qint64 limitMs) {
    const qint64 elapsed = std::max<qint64>(elapsedMs, 0);
    WaitProgress out;
    double fraction = 0.0;
    if (typicalMs > 0) {
        fraction = static_cast<double>(elapsed) / static_cast<double>(typicalMs);
        out.text = QStringLiteral("usually about %1 — %2 so far")
                       .arg(formatDuration(clampToInt(typicalMs)), formatDuration(clampToInt(elapsed)));
    } else {
        fraction = limitMs > 0 ? static_cast<double>(elapsed) / static_cast<double>(limitMs) : 0.0;
        out.text = QStringLiteral("%1 of up to %2").arg(formatDuration(clampToInt(elapsed)), formatDuration(clampToInt(limitMs)));
    }
    out.fraction = std::clamp(fraction, 0.0, kMaxWaitFraction);
    return out;
}

}  // namespace wakedeck::app
