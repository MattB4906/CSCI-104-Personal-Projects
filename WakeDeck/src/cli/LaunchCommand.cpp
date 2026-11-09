#include "LaunchCommand.h"

#include <QCoreApplication>
#include <QObject>
#include <QTimer>
#include <iostream>
#include <set>

#include "Flags.h"
#include "SessionFlags.h"
#include "engine/ClientLauncher.h"
#include "engine/ProfileStore.h"

namespace wakedeck::cli {

void printLaunchUsage(std::ostream& out) {
    out << "Usage:\n"
           "  wakedeck-cli launch --profile <name> [--store <file>] [--dry-run]\n"
           "  wakedeck-cli launch --host <host> --client moonlight|custom [--client-path <path>]\n"
           "                      [--launch open|stream] [--app <name>] [--client-args \"<arguments>\"] [--dry-run]\n"
           "\n"
           "Opens the client exactly as Wake & Connect does once the service is ready, without checking\n"
           "or waking anything. --dry-run only shows what would be started and where the client was\n"
           "looked for. --client-args is split on spaces and {host} is replaced by the host; an argument\n"
           "that itself contains a space can only be written in a profile file. Client paths must be\n"
           "absolute.\n"
           "\n"
           "Exit codes: 0 launched (or would be), 1 usage error, 2 client not found or not started.\n";
}

int runLaunch(int argc, char** argv, const std::vector<std::string>& args) {
    Flags flags;
    std::set<std::string> valued = clientValueFlags();
    valued.insert("--host");
    valued.insert("--profile");
    valued.insert("--store");
    if (const auto error = parseFlags(args, valued, {"--dry-run"}, flags)) return usageError(*error, printLaunchUsage);
    if (flags.help) {
        printLaunchUsage(std::cout);
        return 0;
    }
    if (!flags.positional.empty()) return usageError("unexpected argument '" + flags.positional.front() + "'", printLaunchUsage);

    std::string error;
    ClientConfig client;
    QString host;
    if (flags.has("--profile")) {
        if (flags.has("--host") || hasClientFlags(flags)) {
            return usageError("with --profile the host and the client come from the profile", printLaunchUsage);
        }
        const ProfileStore store(flags.has("--store") ? QString::fromStdString(flags.valueOr("--store", ""))
                                                      : ProfileStore::defaultPath());
        const ProfileStore::LoadResult loaded = store.load();
        if (!loaded.ok) {
            std::cerr << "error: " << loaded.error.toStdString() << "\n";
            return 2;
        }
        if (!loaded.warning.isEmpty()) std::cerr << "warning: " << loaded.warning.toStdString() << "\n";
        const QString name = QString::fromStdString(flags.valueOr("--profile", "")).trimmed();
        const Profile* profile = findProfile(loaded.profiles, name);
        if (!profile) {
            std::cerr << "error: profile \"" << name.toStdString() << "\" not found in " << store.path().toStdString() << "\n";
            return 2;
        }
        client = profile->client;
        host = profile->check.host;
        std::cout << "Profile: " << profile->name.toStdString() << std::endl;
    } else {
        if (flags.has("--store")) return usageError("--store only makes sense together with --profile", printLaunchUsage);
        host = QString::fromStdString(flags.valueOr("--host", "")).trimmed();
        if (host.isEmpty()) return usageError("--host is required (the address the client connects to)", printLaunchUsage);
        if (!flags.has("--client")) return usageError("--client moonlight or --client custom is required", printLaunchUsage);
        const auto parsed = clientConfigFromFlags(flags, error);
        if (!parsed) return usageError(error, printLaunchUsage);
        client = *parsed;
    }
    if (client.preset == ClientConfig::Preset::None) return usageError("no client is configured; nothing to launch", printLaunchUsage);

    const LaunchResolution resolution = resolveLaunch(client, host);
    if (!resolution.plan) {
        std::cerr << "error: " << resolution.error.toStdString() << "\n";
        return 2;
    }
    const LaunchPlan& plan = *resolution.plan;
    std::cout << "Client: " << plan.description.toStdString() << "\n";
    if (plan.openBundle) {
        std::cout << (flags.has("--dry-run") ? "Would open: " : "Opening: ") << plan.program.toStdString() << std::endl;
    } else {
        std::cout << (flags.has("--dry-run") ? "Would run:  " : "Running:  ") << plan.program.toStdString();
        for (const QString& argument : plan.arguments) std::cout << " " << argument.toStdString();
        std::cout << std::endl;
    }
    if (flags.has("--dry-run")) return 0;

    QCoreApplication app(argc, argv);
    ClientLauncher launcher(client, host);
    int exitCode = 2;
    QObject::connect(&launcher, &Launcher::finished, &app, [&](bool ok, const QString& summary) {
        std::cout << summary.toStdString() << std::endl;
        exitCode = ok ? 0 : 2;
        app.quit();
    });
    QTimer::singleShot(0, &app, [&launcher] { launcher.start(); });
    app.exec();
    return exitCode;
}

}  // namespace wakedeck::cli
