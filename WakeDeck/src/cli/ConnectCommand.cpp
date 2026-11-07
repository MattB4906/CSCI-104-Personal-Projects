#include "ConnectCommand.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <iostream>
#include <set>

#include "Flags.h"
#include "SessionFlags.h"
#include "engine/ProfileStore.h"
#include "engine/ServiceProbe.h"
#include "engine/Session.h"

namespace wakedeck::cli {
namespace {

std::string stamp(const QDateTime& at) {
    return at.toString(QStringLiteral("HH:mm:ss")).toStdString();
}

}  // namespace

void printCheckUsage(std::ostream& out) {
    out << "Usage:\n"
           "  wakedeck-cli check --host <host> [--port 47989] [--kind sunshine|tcp] [--timeout-ms 2000]\n"
           "\n"
           "Asks once whether the service answers. sunshine: GET /serverinfo over HTTP and read\n"
           "Sunshine's state. tcp: a plain connection to the port.\n"
           "\n"
           "Exit codes: 0 ready, 1 usage error, 2 not ready (the output says what was observed).\n";
}

void printConnectUsage(std::ostream& out) {
    out << "Usage:\n"
           "  wakedeck-cli connect --check-host <host> [--check-port 47989] [--check sunshine|tcp]\n"
           "                       [--probe-timeout-ms 2000] [--poll-s 3] [--timeout-s 120]\n"
           "                       [--mac <address> --to <host> [--ports 9,7] [--repeat 5] [--interval-ms 100]]\n"
           "  wakedeck-cli connect --profile <name> [--store <file>] [--poll-s 3] [--timeout-s 120]\n"
           "\n"
           "Checks the service. If it is not ready and a wake is configured, sends the wake request,\n"
           "then keeps checking until the service answers or --timeout-s runs out. Without a wake it\n"
           "only waits. With --profile the settings come from a saved profile (see `profile --help`);\n"
           "only --poll-s and --timeout-s may be given alongside it. Launching the client comes in a\n"
           "later milestone.\n"
           "\n"
           "Exit codes: 0 ready, 1 usage error, 2 timed out or profile not found, 3 wake request failed,\n"
           "            4 client not launched.\n";
}

int runCheck(int argc, char** argv, const std::vector<std::string>& args) {
    Flags flags;
    if (const auto error = parseFlags(args, {"--host", "--port", "--kind", "--timeout-ms"}, {}, flags)) {
        return usageError(*error, printCheckUsage);
    }
    if (flags.help) {
        printCheckUsage(std::cout);
        return 0;
    }
    if (!flags.positional.empty()) return usageError("unexpected argument '" + flags.positional.front() + "'", printCheckUsage);
    std::string error;
    const auto target = probeTargetFromFlags(flags, "--host", "--port", "--kind", "--timeout-ms", error);
    if (!target) return usageError(error, printCheckUsage);

    QCoreApplication app(argc, argv);
    std::cout << "Checking " << describe(*target).toStdString() << " ..." << std::endl;
    int exitCode = 2;
    Probe* probe = makeProbe(*target, &app);
    QObject::connect(probe, &Probe::finished, &app, [&](const ProbeResult& result) {
        if (result.ready()) {
            std::cout << "Ready: " << result.detail.toStdString();
        } else {
            std::cout << "Not ready: " << statusName(result.status).toStdString() << " (" << result.detail.toStdString() << ")";
        }
        std::cout << " after " << result.elapsedMs << " ms" << std::endl;
        exitCode = result.ready() ? 0 : 2;
        app.quit();
    });
    // Start from inside the event loop: a probe that fails synchronously would
    // otherwise call quit() before exec() begins, and exec() would never return.
    QTimer::singleShot(0, &app, [probe] { probe->start(); });
    app.exec();
    return exitCode;
}

int runConnect(int argc, char** argv, const std::vector<std::string>& args) {
    Flags flags;
    std::set<std::string> known = sessionValueFlags();
    known.insert("--profile");
    known.insert("--store");
    if (const auto error = parseFlags(args, known, {}, flags)) return usageError(*error, printConnectUsage);
    if (flags.help) {
        printConnectUsage(std::cout);
        return 0;
    }
    if (!flags.positional.empty()) return usageError("unexpected argument '" + flags.positional.front() + "'", printConnectUsage);

    std::string error;
    SessionConfig config;
    QString profileName;
    if (flags.has("--profile")) {
        if (hasCheckOrWakeFlags(flags)) {
            return usageError("with --profile only --poll-s and --timeout-s may be given; the rest comes from the profile",
                              printConnectUsage);
        }
        profileName = QString::fromStdString(flags.valueOr("--profile", "")).trimmed();
        if (profileName.isEmpty()) return usageError("--profile needs a name", printConnectUsage);
        const ProfileStore store(flags.has("--store") ? QString::fromStdString(flags.valueOr("--store", ""))
                                                      : ProfileStore::defaultPath());
        const ProfileStore::LoadResult loaded = store.load();
        if (!loaded.ok) {
            std::cerr << "error: " << loaded.error.toStdString() << "\n";
            return 2;
        }
        if (!loaded.warning.isEmpty()) std::cerr << "warning: " << loaded.warning.toStdString() << "\n";
        const Profile* profile = findProfile(loaded.profiles, profileName);
        if (!profile) {
            std::cerr << "error: profile \"" << profileName.toStdString() << "\" not found in " << store.path().toStdString()
                      << "\n";
            return 2;
        }
        profileName = profile->name;  // as saved, not as typed
        config = profile->toSessionConfig();
        if (!applyTimingOverrides(flags, config, error)) return usageError(error, printConnectUsage);
    } else {
        if (flags.has("--store")) return usageError("--store only makes sense together with --profile", printConnectUsage);
        const auto fromFlags = sessionConfigFromFlags(flags, error);
        if (!fromFlags) return usageError(error, printConnectUsage);
        config = *fromFlags;
    }

    QCoreApplication app(argc, argv);
    if (!profileName.isEmpty()) std::cout << "Profile: " << profileName.toStdString() << std::endl;
    Session session(config);
    int exitCode = 2;
    QObject::connect(&session, &Session::timelineEvent, &app, [](const TimelineEvent& e) {
        std::cout << stamp(e.at) << "  " << e.text.toStdString() << std::endl;
    });
    QObject::connect(&session, &Session::finished, &app, [&](Outcome outcome, const QString& summary) {
        std::cout << "\nResult: " << outcomeName(outcome).toStdString() << "\n" << summary.toStdString() << std::endl;
        switch (outcome) {
        case Outcome::ReadyWithoutWake:
        case Outcome::ReadyAfterWake: exitCode = 0; break;
        case Outcome::TimedOut: exitCode = 2; break;
        case Outcome::WakeFailed: exitCode = 3; break;
        case Outcome::LaunchFailed: exitCode = 4; break;
        case Outcome::Cancelled: exitCode = 5; break;
        }
        app.quit();
    });
    // Same reason as in runCheck: start only once the event loop is running.
    QTimer::singleShot(0, &app, [&session] { session.start(); });
    app.exec();
    return exitCode;
}

}  // namespace wakedeck::cli
