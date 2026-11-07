#include "Explain.h"

#include <QHostAddress>
#include <QStringList>
#include <map>

namespace wakedeck {

QString formatDuration(int ms) {
    const int totalSeconds = ms < 0 ? 0 : ms / 1000;
    return QStringLiteral("%1:%2").arg(totalSeconds / 60).arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
}

bool looksLikeTailscale(const QString& host) {
    const QString lower = host.trimmed().toLower();
    if (lower.endsWith(QLatin1String(".ts.net"))) return true;
    QHostAddress address;
    if (!address.setAddress(lower) || address.protocol() != QAbstractSocket::IPv4Protocol) return false;
    const quint32 value = address.toIPv4Address();
    return (value & 0xFFC00000u) == 0x64400000u;  // 100.64.0.0/10
}

QString explainTimeout(const Observations& o) {
    std::map<ProbeStatus, int> counts;
    for (const Observation& check : o.checks) ++counts[check.result.status];

    const ProbeStatus order[] = {ProbeStatus::TimedOut, ProbeStatus::Unreachable, ProbeStatus::Refused,
                                 ProbeStatus::NotReady, ProbeStatus::ResolveFailed, ProbeStatus::Error};
    QStringList observed;
    for (ProbeStatus status : order) {
        const int n = counts[status];
        if (n > 0) observed << QStringLiteral("%1 %2").arg(n).arg(statusName(status));
    }

    QStringList lines;
    lines << QStringLiteral("What happened");
    lines << QStringLiteral("- Checked %1 for %2: %3 check%4, %5.")
                 .arg(describe(o.check), formatDuration(o.elapsedMs))
                 .arg(o.checks.size())
                 .arg(o.checks.size() == 1 ? QString() : QStringLiteral("s"))
                 .arg(observed.isEmpty() ? QStringLiteral("no result") : observed.join(QStringLiteral(", ")));
    if (!o.checks.empty() && !o.checks.back().result.detail.isEmpty()) {
        lines << QStringLiteral("  Last answer: %1.").arg(o.checks.back().result.detail);
    }
    if (o.wakeSent) {
        lines << QStringLiteral("- Wake request sent at %1: %2. That confirms the packets left this computer, "
                                "not that the PC received them.")
                     .arg(o.wakeAt.toString(QStringLiteral("HH:mm:ss")), o.wakeSummary);
    } else if (!o.wakeConfigured) {
        lines << QStringLiteral("- No wake request is configured for this computer, so nothing was sent to wake it.");
    }

    lines << QString();
    lines << QStringLiteral("Likely causes");
    const int silent = counts[ProbeStatus::TimedOut] + counts[ProbeStatus::Unreachable];
    const bool allSilent = !o.checks.empty() && silent == static_cast<int>(o.checks.size());
    const QString port = QString::number(o.check.port);

    if (counts[ProbeStatus::ResolveFailed] > 0) {
        lines << QStringLiteral("- The name %1 could not be resolved. Check the spelling, or whether the VPN that "
                                "provides this name is connected on this computer.")
                     .arg(o.check.host);
    }
    if (counts[ProbeStatus::Refused] > 0) {
        lines << QStringLiteral("- The computer is up: it refused the connection on port %1, so the service is not "
                                "listening there yet. Give it more time, or check that the service runs and uses "
                                "this port.")
                     .arg(port);
    }
    if (counts[ProbeStatus::NotReady] > 0) {
        lines << QStringLiteral("- Something answered on port %1, but not with the reply WakeDeck expects. Check the "
                                "port and the kind of check.")
                     .arg(port);
    }
    if (allSilent) {
        lines << QStringLiteral("- The computer never answered. Either it is still off, because the wake request did "
                                "not reach it or Wake-on-LAN is disabled in its firmware or network adapter, or it "
                                "is on but not reachable from here.");
        if (looksLikeTailscale(o.check.host)) {
            lines << QStringLiteral("- %1 looks like a Tailscale address: Tailscale must be connected on this computer "
                                    "and running on the PC before it can answer.")
                         .arg(o.check.host);
        }
        if (!o.wakeConfigured) {
            lines << QStringLiteral("- No wake request was sent. If the computer is asleep, configure how to wake it.");
        }
    } else if (counts[ProbeStatus::Unreachable] > 0) {
        lines << QStringLiteral("- Some checks found no route to the computer from here: check this computer's "
                                "network or VPN connection.");
    }
    if (counts[ProbeStatus::Error] > 0) {
        lines << QStringLiteral("- Some checks failed for another reason; the last answer above says what.");
    }
    return lines.join(QLatin1Char('\n'));
}

}  // namespace wakedeck
