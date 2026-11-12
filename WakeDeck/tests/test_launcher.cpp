// Qt Test suite for the client launcher and the macOS platform layer. It asks
// Launch Services about applications that exist on every Mac, builds a fake
// .app bundle in a temporary folder, and launches only /usr/bin/true and that
// fake bundle's script. It never opens a real graphical application. The
// process watcher is tried on /bin/sleep and /usr/bin/true.
#include <QtTest>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <cerrno>
#include <csignal>
#include <optional>
#include <sys/types.h>

#include "engine/AppLocator.h"
#include "engine/ClientLauncher.h"
#include "engine/ProcessWatcher.h"
#include "engine/Profile.h"
#include "engine/Session.h"

using namespace wakedeck;

namespace {

// A minimal application bundle whose executable is a shell script that exits 0.
QString makeFakeBundle(const QDir& dir) {
    const QString app = dir.filePath(QStringLiteral("Fake.app"));
    QDir().mkpath(app + QStringLiteral("/Contents/MacOS"));
    QFile plist(app + QStringLiteral("/Contents/Info.plist"));
    if (!plist.open(QIODevice::WriteOnly)) return QString();
    plist.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                "<plist version=\"1.0\"><dict>\n"
                "<key>CFBundleExecutable</key><string>Fake</string>\n"
                "<key>CFBundleIdentifier</key><string>net.example.wakedeck.fake</string>\n"
                "<key>CFBundlePackageType</key><string>APPL</string>\n"
                "</dict></plist>\n");
    plist.close();
    QFile script(app + QStringLiteral("/Contents/MacOS/Fake"));
    if (!script.open(QIODevice::WriteOnly)) return QString();
    script.write("#!/bin/sh\nexit 0\n");
    script.close();
    script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup |
                          QFile::ReadOther | QFile::ExeOther);
    return app;
}

// A background-only bundle around the fake agent (tests/fake_agent.mm): a real
// AppKit process, so the system reports it as a running application, but with
// no window, no Dock icon and no menu bar. It ends by itself after 8 seconds.
QString makeAgentBundle(const QDir& dir) {
    const QString app = dir.filePath(QStringLiteral("WakeDeckFakeAgent.app"));
    QDir().mkpath(app + QStringLiteral("/Contents/MacOS"));
    QFile plist(app + QStringLiteral("/Contents/Info.plist"));
    if (!plist.open(QIODevice::WriteOnly)) return QString();
    plist.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                "<plist version=\"1.0\"><dict>\n"
                "<key>CFBundleExecutable</key><string>WakeDeckFakeAgent</string>\n"
                "<key>CFBundleIdentifier</key><string>net.example.wakedeck.fake-agent</string>\n"
                "<key>CFBundlePackageType</key><string>APPL</string>\n"
                "<key>LSBackgroundOnly</key><true/>\n"
                "</dict></plist>\n");
    plist.close();
    const QString executable = app + QStringLiteral("/Contents/MacOS/WakeDeckFakeAgent");
    if (!QFile::copy(QStringLiteral(WAKEDECK_FAKE_AGENT), executable)) return QString();
    QFile::setPermissions(executable, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return app;
}

struct LaunchOutcome {
    std::optional<bool> ok;
    QString summary;
    qint64 pid = 0;  // launchedPid() when finished arrived
};

LaunchOutcome runLauncher(const ClientConfig& client, const QString& host, int timeoutMs = 5000) {
    LaunchOutcome outcome;
    QObject parent;
    ClientLauncher launcher(client, host, &parent);
    QObject::connect(&launcher, &Launcher::finished, [&outcome, &launcher](bool ok, const QString& summary) {
        outcome.ok = ok;
        outcome.summary = summary;
        outcome.pid = launcher.launchedPid();
    });
    launcher.start();
    [[maybe_unused]] const bool finished = QTest::qWaitFor([&outcome] { return outcome.ok.has_value(); }, timeoutMs);
    return outcome;
}

// Starts a harmless process that is not WakeDeck's child, like a client.
qint64 startDetached(const QString& program, const QStringList& arguments = {}) {
    qint64 pid = 0;
    return QProcess::startDetached(program, arguments, QString(), &pid) ? pid : 0;
}

bool processGone(qint64 pid) {
    return ::kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH;
}

ClientConfig customClient(const QString& path, const QStringList& arguments = {}) {
    ClientConfig client;
    client.preset = ClientConfig::Preset::Custom;
    client.path = path;
    client.arguments = arguments;
    return client;
}

}  // namespace

class LauncherTest : public QObject {
    Q_OBJECT

private slots:
    // ---- Platform layer ----

    void launchServicesFindsTextEdit() {
        const auto path = platform::applicationPathForBundleId(QStringLiteral("com.apple.TextEdit"));
        QVERIFY(path.has_value());
        QVERIFY2(path->endsWith(QStringLiteral(".app")), qPrintable(*path));
        QVERIFY(QFileInfo::exists(*path));
        const auto executable = platform::bundleExecutable(*path);
        QVERIFY(executable.has_value());
        QVERIFY2(executable->endsWith(QStringLiteral("/Contents/MacOS/TextEdit")), qPrintable(*executable));
        QVERIFY(QFileInfo(*executable).isExecutable());
    }

    void unknownBundleIdIsNotFound() {
        QVERIFY(!platform::applicationPathForBundleId(QStringLiteral("net.example.wakedeck.does-not-exist")).has_value());
        QVERIFY(!platform::bundleExecutable(QStringLiteral("/nonexistent/Nothing.app")).has_value());
    }

    void fakeBundleExposesItsExecutable() {
        QTemporaryDir dir;
        const QString app = makeFakeBundle(QDir(dir.path()));
        QVERIFY(!app.isEmpty());
        const auto executable = platform::bundleExecutable(app);
        QVERIFY(executable.has_value());
        QCOMPARE(*executable, app + QStringLiteral("/Contents/MacOS/Fake"));
    }

    // ---- Resolution (nothing is launched) ----

    void hostSubstitution() {
        QCOMPARE(substituteHost({QStringLiteral("rdp://{host}"), QStringLiteral("x"), QStringLiteral("{host}:3389")},
                                QStringLiteral("pc.example.net")),
                 (QStringList{QStringLiteral("rdp://pc.example.net"), QStringLiteral("x"), QStringLiteral("pc.example.net:3389")}));
    }

    void resolvesCustomExecutable() {
        const LaunchResolution r = resolveLaunch(customClient(QStringLiteral("/usr/bin/true"), {QStringLiteral("{host}"), QStringLiteral("x")}),
                                                 QStringLiteral("127.0.0.1"));
        QVERIFY2(r.plan.has_value(), qPrintable(r.error));
        QCOMPARE(r.plan->program, QStringLiteral("/usr/bin/true"));
        QCOMPARE(r.plan->arguments, (QStringList{QStringLiteral("127.0.0.1"), QStringLiteral("x")}));
        QVERIFY(!r.plan->openBundle);
        QVERIFY2(r.plan->description.contains(QStringLiteral("custom client true 127.0.0.1 x")), qPrintable(r.plan->description));
    }

    void reportsMissingAndNonExecutableClients() {
        const LaunchResolution missing = resolveLaunch(customClient(QStringLiteral("/nonexistent/wakedeck-client")), QStringLiteral("h"));
        QVERIFY(!missing.plan.has_value());
        QVERIFY2(missing.error.contains(QStringLiteral("not found")), qPrintable(missing.error));
        QVERIFY(missing.searched.contains(QStringLiteral("/nonexistent/wakedeck-client")));

        QTemporaryDir dir;
        const QString plain = dir.filePath(QStringLiteral("notes.txt"));
        QFile file(plain);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("hello");
        file.close();
        const LaunchResolution notExecutable = resolveLaunch(customClient(plain), QStringLiteral("h"));
        QVERIFY(!notExecutable.plan.has_value());
        QVERIFY2(notExecutable.error.contains(QStringLiteral("not executable")), qPrintable(notExecutable.error));

        const LaunchResolution none = resolveLaunch(ClientConfig{}, QStringLiteral("h"));
        QVERIFY(!none.plan.has_value());
        QVERIFY(none.error.contains(QStringLiteral("no client")));
    }

    void resolvesMoonlightStreamFromAnExplicitBundle() {
        QTemporaryDir dir;
        const QString app = makeFakeBundle(QDir(dir.path()));
        ClientConfig client;
        client.preset = ClientConfig::Preset::Moonlight;
        client.path = app;
        client.launchMode = QStringLiteral("stream");
        client.appName = QStringLiteral("Desktop");
        const LaunchResolution r = resolveLaunch(client, QStringLiteral("pc.example.net"));
        QVERIFY2(r.plan.has_value(), qPrintable(r.error));
        QCOMPARE(r.plan->program, app + QStringLiteral("/Contents/MacOS/Fake"));
        QCOMPARE(r.plan->arguments, (QStringList{QStringLiteral("stream"), QStringLiteral("pc.example.net"), QStringLiteral("Desktop")}));
        QVERIFY(!r.plan->openBundle);
        QCOMPARE(r.plan->description, QStringLiteral("Moonlight, stream \"Desktop\" on pc.example.net"));
    }

    void resolvesMoonlightOpenAsBundleActivation() {
        QTemporaryDir dir;
        const QString app = makeFakeBundle(QDir(dir.path()));
        ClientConfig client;
        client.preset = ClientConfig::Preset::Moonlight;
        client.path = app;
        const LaunchResolution r = resolveLaunch(client, QStringLiteral("pc.example.net"));
        QVERIFY2(r.plan.has_value(), qPrintable(r.error));
        QVERIFY(r.plan->openBundle);
        QCOMPARE(r.plan->program, app);
        QVERIFY(r.plan->arguments.isEmpty());
        QCOMPARE(r.plan->description, QStringLiteral("Moonlight"));
    }

    void moonlightLookupReportsWhereItLooked() {
        ClientConfig client;
        client.preset = ClientConfig::Preset::Moonlight;
        const LaunchResolution r = resolveLaunch(client, QStringLiteral("pc.example.net"));
        QVERIFY(!r.searched.isEmpty());
        QVERIFY2(r.searched.first().contains(QStringLiteral("Launch Services")), qPrintable(r.searched.first()));
        if (r.plan) {
            // Moonlight is installed on this Mac: the plan must point at a real bundle.
            QVERIFY2(r.plan->program.endsWith(QStringLiteral("Moonlight.app")), qPrintable(r.plan->program));
            QVERIFY(QFileInfo::exists(r.plan->program));
        } else {
            // Not installed: every place must have been tried and named.
            QVERIFY2(r.error.contains(QStringLiteral("Moonlight was not found")), qPrintable(r.error));
            QVERIFY(r.error.contains(QStringLiteral("/Applications/Moonlight.app")));
            QVERIFY(r.error.contains(QStringLiteral("Launch Services")));
            QVERIFY(r.searched.size() >= 3);
        }
    }

    void customBundleWithoutArgumentsIsOpenedWithArgumentsRun() {
        QTemporaryDir dir;
        const QString app = makeFakeBundle(QDir(dir.path()));
        const LaunchResolution open = resolveLaunch(customClient(app), QStringLiteral("h"));
        QVERIFY(open.plan.has_value());
        QVERIFY(open.plan->openBundle);
        const LaunchResolution run = resolveLaunch(customClient(app, {QStringLiteral("{host}")}), QStringLiteral("h"));
        QVERIFY(run.plan.has_value());
        QVERIFY(!run.plan->openBundle);
        QCOMPARE(run.plan->program, app + QStringLiteral("/Contents/MacOS/Fake"));
        QCOMPARE(run.plan->arguments, QStringList{QStringLiteral("h")});
    }

    void relativePathsAreRejectedAndAbsolutised() {
        // The rules reject them, so neither a profile nor the command line can save one.
        QVERIFY(clientConfigError(customClient(QStringLiteral("ok.sh"))).contains(QStringLiteral("absolute")));
        QVERIFY(clientConfigError(customClient(QStringLiteral("./tools/ok.sh"))).contains(QStringLiteral("absolute")));
        ClientConfig moonlight;
        moonlight.preset = ClientConfig::Preset::Moonlight;
        moonlight.path = QStringLiteral("Moonlight.app");
        QVERIFY(clientConfigError(moonlight).contains(QStringLiteral("absolute")));
        QVERIFY(clientConfigError(customClient(QStringLiteral("/usr/bin/true"))).isEmpty());

        // And the resolver never hands a relative program to the process start,
        // which runs from the home folder.
        QTemporaryDir dir;
        QFile script(dir.filePath(QStringLiteral("ok.sh")));
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("#!/bin/sh\nexit 0\n");
        script.close();
        script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        const QString previous = QDir::currentPath();
        QVERIFY(QDir::setCurrent(dir.path()));
        const LaunchResolution r = resolveLaunch(customClient(QStringLiteral("./ok.sh")), QStringLiteral("h"));
        QVERIFY(QDir::setCurrent(previous));
        QVERIFY2(r.plan.has_value(), qPrintable(r.error));
        QVERIFY2(QDir::isAbsolutePath(r.plan->program), qPrintable(r.plan->program));
        QCOMPARE(QFileInfo(r.plan->program).canonicalFilePath(), QFileInfo(dir.filePath(QStringLiteral("ok.sh"))).canonicalFilePath());
    }

    // ---- Launching, harmless programs only ----

    void opensFakeBundleThroughTheSystem() {
        // A bundle without arguments goes through NSWorkspace; the fake bundle's
        // executable is a script, so nothing appears on screen.
        QTemporaryDir dir;
        const QString app = makeFakeBundle(QDir(dir.path()));
        const LaunchOutcome outcome = runLauncher(customClient(app), QStringLiteral("h"), 15000);
        QVERIFY2(outcome.ok.has_value(), "the open completion never arrived on the Qt thread");
        QVERIFY2(*outcome.ok, qPrintable(outcome.summary));
        QVERIFY2(outcome.summary.contains(QStringLiteral("Opened custom client Fake.app")), qPrintable(outcome.summary));
    }

    void completionIsSkippedWhenTheContextIsGone() {
        QTemporaryDir dir;
        const QString app = makeFakeBundle(QDir(dir.path()));
        bool called = false;
        auto* context = new QObject;
        platform::openApplication(app, context, [&called](bool, const QString&, qint64) { called = true; });
        delete context;  // gone before the system answers
        QTest::qWait(1500);
        QVERIFY(!called);
    }

    void launchesCustomExecutable() {
        const LaunchOutcome outcome = runLauncher(customClient(QStringLiteral("/usr/bin/true"), {QStringLiteral("{host}")}),
                                                  QStringLiteral("127.0.0.1"));
        QVERIFY(outcome.ok.has_value());
        QVERIFY2(*outcome.ok, qPrintable(outcome.summary));
        QVERIFY2(outcome.summary.contains(QStringLiteral("Launched custom client true 127.0.0.1")), qPrintable(outcome.summary));
        QVERIFY2(outcome.pid > 0, "a directly started client must report its process id");
        QVERIFY(outcome.summary.contains(QStringLiteral("(process %1)").arg(outcome.pid)));
    }

    void openReportsANewProcessButNotARunningOne() {
        // The first open starts the agent, so its process is reported and can be
        // watched; a second open only activates the running copy, which belongs
        // to whoever started it and must not be claimed.
        QTemporaryDir dir;
        const QString app = makeAgentBundle(QDir(dir.path()));
        QVERIFY(!app.isEmpty());
        const LaunchOutcome first = runLauncher(customClient(app), QStringLiteral("h"), 15000);
        QVERIFY2(first.ok.has_value() && *first.ok, qPrintable(first.summary));
        QVERIFY2(first.pid > 0, "the process started by the open must be reported");
        QVERIFY(!processGone(first.pid));

        const LaunchOutcome second = runLauncher(customClient(app), QStringLiteral("h"), 15000);
        const bool stillRunning = !processGone(first.pid);

        ProcessWatcher watcher;
        QSignalSpy spy(&watcher, &ProcessWatcher::exited);
        const bool watching = watcher.watch(first.pid);
        ::kill(static_cast<pid_t>(first.pid), SIGTERM);  // ends the agent now, also if a check fails
        QVERIFY(stillRunning);
        QVERIFY2(second.ok.has_value() && *second.ok, qPrintable(second.summary));
        QCOMPARE(second.pid, qint64(0));
        QVERIFY(watching);
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
        QCOMPARE(spy.at(0).at(0).toLongLong(), first.pid);
    }

    // ---- Process watcher ----

    void watcherReportsTheExitOfAProcess() {
        const qint64 pid = startDetached(QStringLiteral("/bin/sleep"), {QStringLiteral("1")});
        QVERIFY(pid > 0);
        ProcessWatcher watcher;
        QSignalSpy spy(&watcher, &ProcessWatcher::exited);
        QElapsedTimer clock;
        clock.start();
        QVERIFY(watcher.watch(pid));
        QVERIFY(watcher.isWatching());
        QCOMPARE(watcher.pid(), pid);
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
        QVERIFY2(clock.elapsed() >= 500, "reported long before /bin/sleep 1 could have ended");
        QCOMPARE(spy.at(0).at(0).toLongLong(), pid);
        QVERIFY(processGone(pid));
        QVERIFY(!watcher.isWatching());
        QTest::qWait(200);
        QCOMPARE(spy.count(), 1);  // once per watch
    }

    void watcherReportsAProcessAlreadyGone() {
        const qint64 pid = startDetached(QStringLiteral("/usr/bin/true"));
        QVERIFY(pid > 0);
        QVERIFY(QTest::qWaitFor([pid] { return processGone(pid); }, 5000));
        ProcessWatcher watcher;
        QSignalSpy spy(&watcher, &ProcessWatcher::exited);
        QVERIFY(watcher.watch(pid));
        QCOMPARE(spy.count(), 0);  // never from inside watch()
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 2000);
        QCOMPARE(spy.at(0).at(0).toLongLong(), pid);
        QVERIFY(!watcher.isWatching());
    }

    void watcherStoppedOrDestroyedReportsNothing() {
        const qint64 pid = startDetached(QStringLiteral("/bin/sleep"), {QStringLiteral("1")});
        QVERIFY(pid > 0);
        int reports = 0;
        auto* destroyed = new ProcessWatcher;
        QObject::connect(destroyed, &ProcessWatcher::exited, [&reports](qint64) { ++reports; });
        QVERIFY(destroyed->watch(pid));
        ProcessWatcher stopped;
        QObject::connect(&stopped, &ProcessWatcher::exited, [&reports](qint64) { ++reports; });
        QVERIFY(stopped.watch(pid));
        delete destroyed;
        stopped.stop();
        QVERIFY(!stopped.isWatching());
        QVERIFY(QTest::qWaitFor([pid] { return processGone(pid); }, 5000));
        QTest::qWait(300);
        QCOMPARE(reports, 0);

        // A gone process stopped before its queued report: nothing either.
        const qint64 gone = startDetached(QStringLiteral("/usr/bin/true"));
        QVERIFY(QTest::qWaitFor([gone] { return processGone(gone); }, 5000));
        QVERIFY(stopped.watch(gone));
        stopped.stop();
        QTest::qWait(100);
        QCOMPARE(reports, 0);

        QVERIFY(!stopped.watch(0));
        QVERIFY(!stopped.watch(-5));
        QVERIFY(!stopped.isWatching());
    }

    void launchesFakeBundleExecutableWithArguments() {
        QTemporaryDir dir;
        const QString app = makeFakeBundle(QDir(dir.path()));
        ClientConfig client;
        client.preset = ClientConfig::Preset::Moonlight;
        client.path = app;
        client.launchMode = QStringLiteral("stream");
        client.appName = QStringLiteral("Desktop");
        const LaunchOutcome outcome = runLauncher(client, QStringLiteral("127.0.0.1"));
        QVERIFY(outcome.ok.has_value());
        QVERIFY2(*outcome.ok, qPrintable(outcome.summary));
        QVERIFY(outcome.summary.contains(QStringLiteral("Moonlight, stream \"Desktop\" on 127.0.0.1")));
    }

    void reportsMissingClientWithoutLaunching() {
        const LaunchOutcome outcome = runLauncher(customClient(QStringLiteral("/nonexistent/wakedeck-client")), QStringLiteral("h"));
        QVERIFY(outcome.ok.has_value());
        QVERIFY(!*outcome.ok);
        QVERIFY2(outcome.summary.contains(QStringLiteral("not found")), qPrintable(outcome.summary));
    }

    void launcherDescribesItself() {
        ClientLauncher launcher(customClient(QStringLiteral("/usr/bin/true"), {QStringLiteral("a")}), QStringLiteral("h"));
        QVERIFY(launcher.configured());
        QCOMPARE(launcher.describe(), QStringLiteral("custom client true a"));
        ClientLauncher missing(customClient(QStringLiteral("/nonexistent")), QStringLiteral("h"));
        QCOMPARE(missing.describe(), QStringLiteral("custom client"));
        ClientLauncher none(ClientConfig{}, QStringLiteral("h"));
        QVERIFY(!none.configured());
    }

    void factoryGivesNoLauncherWhenUnconfigured() {
        QObject parent;
        Launcher* none = makeLauncherFactory(ClientConfig{}, QStringLiteral("h"))(&parent);
        QVERIFY(none != nullptr);
        QVERIFY(!none->configured());
        Launcher* real = makeLauncherFactory(customClient(QStringLiteral("/usr/bin/true")), QStringLiteral("h"))(&parent);
        QVERIFY(real->configured());
    }

    // ---- Through the session ----

    void sessionLaunchesTheConfiguredClient() {
        SessionConfig config;
        config.check.host = QStringLiteral("127.0.0.1");
        config.pollIntervalMs = 5;
        config.overallTimeoutMs = 2000;
        SessionFactories factories;
        factories.probe = [](const ProbeTarget&, QObject* parent) -> Probe* {
            class ReadyProbe final : public Probe {
            public:
                using Probe::Probe;
                void start() override {
                    QTimer::singleShot(0, this, [this] {
                        ProbeResult result;
                        result.status = ProbeStatus::Ready;
                        result.detail = QStringLiteral("fake");
                        emit finished(result);
                    });
                }
            };
            return new ReadyProbe(parent);
        };
        factories.launcher = makeLauncherFactory(customClient(QStringLiteral("/usr/bin/true"), {QStringLiteral("{host}")}),
                                                 config.check.host);
        Session session(config, factories);
        std::optional<Outcome> outcome;
        QStringList events;
        QObject::connect(&session, &Session::timelineEvent, [&events](const TimelineEvent& e) { events << e.text; });
        QObject::connect(&session, &Session::finished, [&outcome](Outcome o, const QString&) { outcome = o; });
        session.start();
        QVERIFY(QTest::qWaitFor([&outcome] { return outcome.has_value(); }, 5000));
        QCOMPARE(*outcome, Outcome::ReadyWithoutWake);
        QVERIFY2(events.filter(QStringLiteral("Launching custom client true 127.0.0.1")).size() == 1, qPrintable(events.join(QLatin1Char('|'))));
        QVERIFY(events.filter(QStringLiteral("Launched custom client")).size() == 1);
        QVERIFY(session.launchedPid() > 0);
        QCOMPARE(session.launchedClient(), QStringLiteral("custom client true 127.0.0.1"));
    }

    void sessionReportsLaunchFailure() {
        SessionConfig config;
        config.check.host = QStringLiteral("127.0.0.1");
        SessionFactories factories;
        factories.probe = [](const ProbeTarget&, QObject* parent) -> Probe* {
            class ReadyProbe final : public Probe {
            public:
                using Probe::Probe;
                void start() override {
                    QTimer::singleShot(0, this, [this] {
                        ProbeResult result;
                        result.status = ProbeStatus::Ready;
                        emit finished(result);
                    });
                }
            };
            return new ReadyProbe(parent);
        };
        factories.launcher = makeLauncherFactory(customClient(QStringLiteral("/nonexistent/wakedeck-client")), config.check.host);
        Session session(config, factories);
        std::optional<Outcome> outcome;
        QString summary;
        QObject::connect(&session, &Session::finished, [&](Outcome o, const QString& s) {
            outcome = o;
            summary = s;
        });
        session.start();
        QVERIFY(QTest::qWaitFor([&outcome] { return outcome.has_value(); }, 5000));
        QCOMPARE(*outcome, Outcome::LaunchFailed);
        QVERIFY2(summary.contains(QStringLiteral("not found")), qPrintable(summary));
        QCOMPARE(session.launchedPid(), qint64(0));
        QVERIFY(session.launchedClient().isEmpty());
    }
};

QTEST_GUILESS_MAIN(LauncherTest)
#include "test_launcher.moc"
