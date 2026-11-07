#include "ProfileCommand.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <iostream>
#include <set>

#include "Flags.h"
#include "SessionFlags.h"
#include "engine/Explain.h"
#include "engine/Profile.h"
#include "engine/ProfileStore.h"

namespace wakedeck::cli {
namespace {

constexpr int kExitProblem = 2;  // not found, already exists, or a file problem

std::string text(const QString& value) { return value.toStdString(); }

int problem(const QString& message) {
    std::cerr << "error: " << text(message) << "\n";
    return kExitProblem;
}

// Loads the store or explains why it cannot; warnings go to stderr.
bool loadOrExplain(const ProfileStore& store, std::vector<Profile>& profiles) {
    const ProfileStore::LoadResult loaded = store.load();
    if (!loaded.warning.isEmpty()) std::cerr << "warning: " << text(loaded.warning) << "\n";
    if (!loaded.ok) {
        std::cerr << "error: " << text(loaded.error) << "\n";
        return false;
    }
    profiles = loaded.profiles;
    return true;
}

int saveOrExplain(const ProfileStore& store, const std::vector<Profile>& profiles) {
    QString error;
    if (!store.save(profiles, &error)) return problem(error);
    return 0;
}

void printProfile(const Profile& profile) {
    std::cout << text(profile.name) << "\n"
              << "  check:  " << text(describe(profile.check)) << " (poll "
              << text(QString::number(profile.pollIntervalMs / 1000.0)) << " s, up to "
              << text(formatDuration(profile.overallTimeoutMs)) << ")\n"
              << "  wake:   " << (profile.wake ? text(describe(*profile.wake)) : std::string("none configured")) << "\n"
              << "  client: " << text(describe(profile.client)) << "\n";
    if (!profile.notes.isEmpty()) std::cout << "  notes:  " << text(profile.notes) << "\n";
}

int list(const ProfileStore& store) {
    std::vector<Profile> profiles;
    if (!loadOrExplain(store, profiles)) return kExitProblem;
    if (profiles.empty()) {
        std::cout << "No profiles in " << text(store.path()) << ".\n"
                  << "Add one:            wakedeck-cli profile add --name \"My PC\" --check-host <host> [options]\n"
                  << "Or import the example: wakedeck-cli profile import examples/example-profile.json\n";
        return 0;
    }
    std::cout << profiles.size() << " profile" << (profiles.size() == 1 ? "" : "s") << " in " << text(store.path()) << "\n\n";
    for (const Profile& profile : profiles) {
        printProfile(profile);
        std::cout << "\n";
    }
    return 0;
}

int show(const ProfileStore& store, const QString& name) {
    std::vector<Profile> profiles;
    if (!loadOrExplain(store, profiles)) return kExitProblem;
    const Profile* profile = findProfile(profiles, name);
    if (!profile) return problem(QStringLiteral("profile \"%1\" not found in %2").arg(name, store.path()));
    std::cout << QJsonDocument(toJson(*profile)).toJson(QJsonDocument::Indented).toStdString();
    return 0;
}

int add(const ProfileStore& store, const Flags& flags) {
    std::string error;
    const QString name = QString::fromStdString(flags.valueOr("--name", "")).trimmed();
    if (name.isEmpty()) return usageError("--name is required", printProfileUsage);
    const auto session = sessionConfigFromFlags(flags, error);
    if (!session) return usageError(error, printProfileUsage);
    const auto client = clientConfigFromFlags(flags, error);
    if (!client) return usageError(error, printProfileUsage);

    Profile profile;
    profile.name = name;
    profile.notes = QString::fromStdString(flags.valueOr("--notes", ""));
    profile.check = session->check;
    profile.wake = session->wake;
    profile.pollIntervalMs = session->pollIntervalMs;
    profile.overallTimeoutMs = session->overallTimeoutMs;
    profile.client = *client;

    std::vector<Profile> profiles;
    if (!loadOrExplain(store, profiles)) return kExitProblem;
    if (findProfile(profiles, name) && !flags.has("--replace")) {
        return problem(QStringLiteral("profile \"%1\" already exists; add --replace to overwrite it").arg(name));
    }
    upsertProfile(profiles, profile);
    if (const int rc = saveOrExplain(store, profiles)) return rc;
    std::cout << "Saved profile \"" << text(name) << "\" to " << text(store.path()) << "\n";
    printProfile(profile);
    return 0;
}

int remove(const ProfileStore& store, const QString& name) {
    std::vector<Profile> profiles;
    if (!loadOrExplain(store, profiles)) return kExitProblem;
    if (!removeProfile(profiles, name)) return problem(QStringLiteral("profile \"%1\" not found in %2").arg(name, store.path()));
    if (const int rc = saveOrExplain(store, profiles)) return rc;
    std::cout << "Removed profile \"" << text(name) << "\" from " << text(store.path()) << "\n";
    return 0;
}

int importFile(const ProfileStore& store, const QString& file, bool replace) {
    QString error;
    const auto profile = ProfileStore::readProfileFile(file, &error);
    if (!profile) return problem(error);
    std::vector<Profile> profiles;
    if (!loadOrExplain(store, profiles)) return kExitProblem;
    if (findProfile(profiles, profile->name) && !replace) {
        return problem(QStringLiteral("profile \"%1\" already exists; add --replace to overwrite it").arg(profile->name));
    }
    upsertProfile(profiles, *profile);
    if (const int rc = saveOrExplain(store, profiles)) return rc;
    std::cout << "Imported profile \"" << text(profile->name) << "\" into " << text(store.path()) << "\n";
    printProfile(*profile);
    return 0;
}

int exportFile(const ProfileStore& store, const QString& name, const QString& file) {
    if (QFileInfo(file).absoluteFilePath() == QFileInfo(store.path()).absoluteFilePath()) {
        return problem(QStringLiteral("refusing to export over the profiles file itself (%1); choose another file name").arg(file));
    }
    std::vector<Profile> profiles;
    if (!loadOrExplain(store, profiles)) return kExitProblem;
    const Profile* profile = findProfile(profiles, name);
    if (!profile) return problem(QStringLiteral("profile \"%1\" not found in %2").arg(name, store.path()));
    QString error;
    if (!ProfileStore::writeProfileFile(*profile, file, &error)) return problem(error);
    std::cout << "Exported profile \"" << text(profile->name) << "\" to " << text(file) << "\n";
    return 0;
}

}  // namespace

void printProfileUsage(std::ostream& out) {
    out << "Usage:\n"
           "  wakedeck-cli profile list\n"
           "  wakedeck-cli profile show <name>\n"
           "  wakedeck-cli profile add --name <name> --check-host <host> [--check-port 47989] [--check sunshine|tcp]\n"
           "                           [--probe-timeout-ms 2000] [--poll-s 3] [--timeout-s 120]\n"
           "                           [--mac <address> --to <host> [--ports 9,7] [--repeat 5] [--interval-ms 100]]\n"
           "                           [--client none|moonlight|custom] [--client-path <path>]\n"
           "                           [--launch open|stream] [--app <name>] [--notes <text>] [--replace]\n"
           "  wakedeck-cli profile remove <name>\n"
           "  wakedeck-cli profile import <file.json> [--replace]\n"
           "  wakedeck-cli profile export <name> <file.json>\n"
           "\n"
           "Every form accepts --store <file> to use another profiles file. The default is\n"
           "~/Library/Application Support/WakeDeck/profiles.json. Names are matched without regard to case.\n"
           "\n"
           "Exit codes: 0 done, 1 usage error, 2 not found, already exists, or a file problem.\n";
}

int runProfile(const std::vector<std::string>& args) {
    Flags flags;
    std::set<std::string> valued = sessionValueFlags();
    valued.insert(clientValueFlags().begin(), clientValueFlags().end());
    valued.insert("--name");
    valued.insert("--notes");
    valued.insert("--store");
    if (const auto error = parseFlags(args, valued, {"--replace"}, flags)) return usageError(*error, printProfileUsage);
    if (flags.help) {
        printProfileUsage(std::cout);
        return 0;
    }
    if (flags.positional.empty()) return usageError("a subcommand is required: list, show, add, remove, import or export", printProfileUsage);

    const ProfileStore store(flags.has("--store") ? QString::fromStdString(flags.valueOr("--store", ""))
                                                  : ProfileStore::defaultPath());
    const std::string& sub = flags.positional.front();
    const std::size_t extra = flags.positional.size() - 1;
    const auto positional = [&flags](std::size_t index) { return QString::fromStdString(flags.positional.at(index)); };

    if (sub == "list") {
        if (extra != 0) return usageError("list takes no further arguments", printProfileUsage);
        return list(store);
    }
    if (sub == "show") {
        if (extra != 1) return usageError("show needs exactly one profile name", printProfileUsage);
        return show(store, positional(1));
    }
    if (sub == "add") {
        if (extra != 0) return usageError("add takes its settings as options; see --help", printProfileUsage);
        return add(store, flags);
    }
    if (sub == "remove") {
        if (extra != 1) return usageError("remove needs exactly one profile name", printProfileUsage);
        return remove(store, positional(1));
    }
    if (sub == "import") {
        if (extra != 1) return usageError("import needs exactly one file", printProfileUsage);
        return importFile(store, positional(1), flags.has("--replace"));
    }
    if (sub == "export") {
        if (extra != 2) return usageError("export needs a profile name and a file", printProfileUsage);
        return exportFile(store, positional(1), positional(2));
    }
    return usageError("unknown subcommand '" + sub + "'", printProfileUsage);
}

}  // namespace wakedeck::cli
