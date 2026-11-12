// GUI tests on Qt's "offscreen" platform: the window and the dialog are built
// and driven for real, but nothing is drawn on any screen. Runs are driven by
// fake probes, wakers and launchers; the profiles live in a temporary folder,
// and so do the settings (see main), so the user's own are never touched.
#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <sys/types.h>
#include <memory>
#include <optional>
#include <vector>

#include "app/MainWindow.h"
#include "app/ProfileEditorDialog.h"
#include "engine/Profile.h"
#include "engine/ProfileStore.h"
#include "engine/Session.h"

using namespace wakedeck;
using wakedeck::app::MainWindow;
using wakedeck::app::ProfileEditorDialog;

namespace {

class FakeProbe final : public Probe {
public:
    FakeProbe(ProbeResult result, int delayMs, QObject* parent) : Probe(parent), result_(std::move(result)), delayMs_(delayMs) {}
    void start() override {
        QTimer::singleShot(delayMs_, this, [this] { emit finished(result_); });
    }

private:
    ProbeResult result_;
    int delayMs_;
};

class FakeWaker final : public Waker {
public:
    FakeWaker(bool ok, QObject* parent) : Waker(parent), ok_(ok) {}
    void start() override {
        QTimer::singleShot(0, this, [this] {
            emit finished(ok_, ok_ ? QStringLiteral("10 packets to 192.0.2.255 (ports 9 x5, 7 x5)")
                                   : QStringLiteral("0 of 10 packets to 192.0.2.255; cannot open a UDP socket"));
        });
    }

private:
    bool ok_;
};

class FakeLauncher final : public Launcher {
public:
    FakeLauncher(bool ok, qint64 pid, QObject* parent) : Launcher(parent), ok_(ok), pid_(pid) {}
    QString describe() const override { return QStringLiteral("fake client"); }
    qint64 launchedPid() const override { return ok_ ? pid_ : 0; }
    void start() override {
        QTimer::singleShot(0, this, [this] {
            emit finished(ok_, ok_ ? QStringLiteral("Launched fake client") : QStringLiteral("the client /nonexistent was not found"));
        });
    }

private:
    bool ok_;
    qint64 pid_;
};

struct Script {
    std::vector<ProbeStatus> statuses{ProbeStatus::Ready};
    bool launchOk = true;
    bool wakeOk = true;
    int probeDelayMs = 0;
    qint64 launchPid = 0;  // what the fake launcher reports as the client's process
    int probesCreated = 0;
    int wakersCreated = 0;
};

MainWindow::FactoriesFor fakeFactories(const std::shared_ptr<Script>& script) {
    return [script](const Profile& profile) {
        SessionFactories factories;
        factories.probe = [script](const ProbeTarget&, QObject* parent) -> Probe* {
            const std::size_t index = std::min(static_cast<std::size_t>(script->probesCreated), script->statuses.size() - 1);
            ++script->probesCreated;
            ProbeResult result;
            result.status = script->statuses[index];
            result.detail = statusName(result.status);
            return new FakeProbe(result, script->probeDelayMs, parent);
        };
        factories.waker = [script](const WakeConfig&, QObject* parent) -> Waker* {
            ++script->wakersCreated;
            return new FakeWaker(script->wakeOk, parent);
        };
        if (profile.client.preset == ClientConfig::Preset::None) {
            factories.launcher = [](QObject* parent) -> Launcher* { return new NoLauncher(parent); };
        } else {
            factories.launcher = [script](QObject* parent) -> Launcher* { return new FakeLauncher(script->launchOk, script->launchPid, parent); };
        }
        return factories;
    };
}

Profile sampleProfile(const QString& name, bool withWake, bool withClient) {
    Profile profile;
    profile.name = name;
    profile.check.host = QStringLiteral("192.0.2.10");
    profile.pollIntervalMs = 5;
    profile.overallTimeoutMs = 2000;
    if (withWake) {
        WakeTarget target;
        target.destination = "192.0.2.255";
        profile.wake = WakeConfig{*MacAddress::parse("00:00:5E:00:53:2A"), target};
    }
    if (withClient) {
        profile.client.preset = ClientConfig::Preset::Custom;
        profile.client.path = QStringLiteral("/usr/bin/true");
    }
    return profile;
}

// A harmless stand-in for a client process: /bin/sleep, not a child of the test.
qint64 startSleeper(int seconds) {
    qint64 pid = 0;
    return QProcess::startDetached(QStringLiteral("/bin/sleep"), {QString::number(seconds)}, QString(), &pid) ? pid : 0;
}

bool processGone(qint64 pid) {
    return ::kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH;
}

bool waitForRun(MainWindow& window, std::optional<Outcome>& outcome, int timeoutMs = 5000) {
    QObject::connect(&window, &MainWindow::runFinished, [&outcome](Outcome o) { outcome = o; });
    return QTest::qWaitFor([&outcome] { return outcome.has_value(); }, timeoutMs);
}

}  // namespace

class GuiTest : public QObject {
    Q_OBJECT

private slots:
    // ---- Main window: profiles ----

    void emptyStoreShowsNoComputer() {
        QTemporaryDir dir;
        MainWindow window(ProfileStore(dir.filePath(QStringLiteral("profiles.json"))), fakeFactories(std::make_shared<Script>()));
        QVERIFY(window.profiles().empty());
        QVERIFY(window.currentProfile() == nullptr);
        QVERIFY(!window.isRunning());
    }

    void listsAndSelectsProfiles() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        QVERIFY(store.save({sampleProfile(QStringLiteral("Gaming PC"), true, false), sampleProfile(QStringLiteral("Office PC"), false, false)}, nullptr));
        MainWindow window(store, fakeFactories(std::make_shared<Script>()));
        QCOMPARE(window.profiles().size(), std::size_t(2));
        QVERIFY(window.currentProfile() != nullptr);
        QCOMPARE(window.currentProfile()->name, QStringLiteral("Gaming PC"));
        window.selectProfile(QStringLiteral("office pc"));
        QCOMPARE(window.currentProfile()->name, QStringLiteral("Office PC"));
    }

    void addAndRemovePersist() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        {
            MainWindow window(store, fakeFactories(std::make_shared<Script>()));
            QString error;
            QVERIFY2(window.addProfile(sampleProfile(QStringLiteral("New PC"), true, true), &error), qPrintable(error));
            QCOMPARE(window.currentProfile()->name, QStringLiteral("New PC"));
        }
        MainWindow reopened(store, fakeFactories(std::make_shared<Script>()));
        QCOMPARE(reopened.profiles().size(), std::size_t(1));
        QVERIFY(reopened.removeCurrentProfile());
        QVERIFY(reopened.profiles().empty());
        QVERIFY(store.load().profiles.empty());
    }

    void loadsTheExampleFromTheBundledResource() {
        QTemporaryDir dir;
        MainWindow window(ProfileStore(dir.filePath(QStringLiteral("profiles.json"))), fakeFactories(std::make_shared<Script>()));
        QString error;
        QVERIFY2(window.loadExample(&error), qPrintable(error));
        QVERIFY(window.currentProfile() != nullptr);
        QCOMPARE(window.currentProfile()->name, QStringLiteral("Living Room PC (example)"));
        QVERIFY(window.currentProfile()->wake.has_value());
    }

    // ---- Main window: runs ----

    void wakeAndConnectReadyAtOnce() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        QVERIFY(store.save({sampleProfile(QStringLiteral("PC"), true, true)}, nullptr));
        auto script = std::make_shared<Script>();
        MainWindow window(store, fakeFactories(script));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));  // laid out, so the click lands on the button
        std::optional<Outcome> outcome;
        QObject::connect(&window, &MainWindow::runFinished, [&outcome](Outcome o) { outcome = o; });
        auto* button = window.findChild<QPushButton*>(QStringLiteral("wakeConnectButton"));
        QVERIFY(button != nullptr);
        QVERIFY(button->isEnabled());
        QTest::mouseClick(button, Qt::LeftButton);
        QVERIFY(QTest::qWaitFor([&outcome] { return outcome.has_value(); }, 5000));
        QCOMPARE(*outcome, Outcome::ReadyWithoutWake);
        QCOMPARE(script->wakersCreated, 0);
        QVERIFY2(window.serviceStatusText().contains(QStringLiteral("ready at")), qPrintable(window.serviceStatusText()));
        QCOMPARE(window.wakeStatusText(), QStringLiteral("not sent"));
        QVERIFY(window.timelineTexts().filter(QStringLiteral("Launched fake client")).size() == 1);
        QVERIFY(!window.explanationVisible());
        QVERIFY(!window.isRunning());
    }

    void wakeAndConnectWakesThenReady() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        QVERIFY(store.save({sampleProfile(QStringLiteral("PC"), true, false)}, nullptr));
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut, ProbeStatus::TimedOut, ProbeStatus::Ready};
        MainWindow window(store, fakeFactories(script));
        std::optional<Outcome> outcome;
        window.startWakeAndConnect();
        QVERIFY(window.isRunning());
        QVERIFY(waitForRun(window, outcome));
        QCOMPARE(*outcome, Outcome::ReadyAfterWake);
        QCOMPARE(script->wakersCreated, 1);
        QVERIFY2(window.wakeStatusText().startsWith(QStringLiteral("sent at")), qPrintable(window.wakeStatusText()));
        QVERIFY(window.wakeStatusText().contains(QStringLiteral("10 packets")));
        QVERIFY2(window.serviceStatusText().contains(QStringLiteral("ready at")), qPrintable(window.serviceStatusText()));
        QVERIFY(window.timelineTexts().filter(QStringLiteral("Wake request sent")).size() == 1);
        QVERIFY(window.timelineTexts().filter(QStringLiteral("No client configured")).size() == 1);
    }

    void timeoutShowsWhatHappened() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        Profile profile = sampleProfile(QStringLiteral("PC"), true, false);
        profile.overallTimeoutMs = 60;
        QVERIFY(store.save({profile}, nullptr));
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut};
        MainWindow window(store, fakeFactories(script));
        std::optional<Outcome> outcome;
        window.startWakeAndConnect();
        QVERIFY(waitForRun(window, outcome));
        QCOMPARE(*outcome, Outcome::TimedOut);
        QVERIFY(window.explanationVisible());
        QVERIFY2(window.explanationText().contains(QStringLiteral("What happened")), qPrintable(window.explanationText()));
        QVERIFY(window.explanationText().contains(QStringLiteral("Wake request sent at")));
        QVERIFY(!window.locateOffered());
        QCOMPARE(window.serviceStatusText(), QStringLiteral("not ready: timed out"));
        QVERIFY(window.wakeStatusText().startsWith(QStringLiteral("sent at")));
    }

    void launchFailureOffersToLocateTheClient() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        QVERIFY(store.save({sampleProfile(QStringLiteral("PC"), false, true)}, nullptr));
        auto script = std::make_shared<Script>();
        script->launchOk = false;
        MainWindow window(store, fakeFactories(script));
        std::optional<Outcome> outcome;
        window.startWakeAndConnect();
        QVERIFY(waitForRun(window, outcome));
        QCOMPARE(*outcome, Outcome::LaunchFailed);
        QVERIFY(window.explanationVisible());
        QVERIFY(window.locateOffered());
        QVERIFY2(window.explanationText().contains(QStringLiteral("service is ready")), qPrintable(window.explanationText()));
        QVERIFY(window.serviceStatusText().contains(QStringLiteral("ready at")));  // the ready fact survives the failure
    }

    void cancelEndsTheRun() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        Profile profile = sampleProfile(QStringLiteral("PC"), true, false);
        profile.overallTimeoutMs = 10000;
        QVERIFY(store.save({profile}, nullptr));
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut};
        MainWindow window(store, fakeFactories(script));
        std::optional<Outcome> outcome;
        window.startWakeAndConnect();
        QTimer::singleShot(30, &window, [&window] { window.cancelRun(); });
        QVERIFY(waitForRun(window, outcome));
        QCOMPARE(*outcome, Outcome::Cancelled);
        QCOMPARE(window.serviceStatusText(), QStringLiteral("cancelled"));
        QVERIFY(!window.isRunning());
    }

    void checkNowUsesOneProbe() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        QVERIFY(store.save({sampleProfile(QStringLiteral("PC"), true, true)}, nullptr));
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::Refused};
        MainWindow window(store, fakeFactories(script));
        std::optional<bool> ready;
        QObject::connect(&window, &MainWindow::checkFinished, [&ready](bool r) { ready = r; });
        window.startCheckOnly();
        QVERIFY(QTest::qWaitFor([&ready] { return ready.has_value(); }, 5000));
        QVERIFY(!*ready);
        QCOMPARE(script->probesCreated, 1);
        QCOMPARE(script->wakersCreated, 0);
        QVERIFY2(window.serviceStatusText().contains(QStringLiteral("refused")), qPrintable(window.serviceStatusText()));
        QCOMPARE(window.wakeStatusText(), QStringLiteral("not sent"));
    }

    void wakeOnlyReportsSentNotAwake() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        QVERIFY(store.save({sampleProfile(QStringLiteral("PC"), true, false)}, nullptr));
        auto script = std::make_shared<Script>();
        MainWindow window(store, fakeFactories(script));
        window.startWakeOnly();
        QVERIFY(QTest::qWaitFor([&window] { return window.wakeStatusText().startsWith(QStringLiteral("sent at")); }, 5000));
        QCOMPARE(script->probesCreated, 0);
        QVERIFY(window.timelineTexts().join(QLatin1Char('\n')).contains(QStringLiteral("not that the computer is awake")));
        QCOMPARE(window.serviceStatusText(), QStringLiteral("not checked"));
    }

    void switchingComputerClearsAFinishedRun() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        QVERIFY(store.save({sampleProfile(QStringLiteral("A"), false, true), sampleProfile(QStringLiteral("B"), false, true)}, nullptr));
        auto script = std::make_shared<Script>();
        script->launchOk = false;
        MainWindow window(store, fakeFactories(script));
        std::optional<Outcome> outcome;
        window.startWakeAndConnect();  // on A
        QVERIFY(waitForRun(window, outcome));
        QCOMPARE(*outcome, Outcome::LaunchFailed);
        QVERIFY(window.locateOffered());

        window.selectProfile(QStringLiteral("B"));
        QCOMPARE(window.currentProfile()->name, QStringLiteral("B"));
        QVERIFY(!window.explanationVisible());  // A's failure must not be acted on from B
        QVERIFY(!window.locateOffered());
        QVERIFY(window.timelineTexts().isEmpty());
        QCOMPARE(window.serviceStatusText(), QStringLiteral("not checked"));
        QCOMPARE(window.wakeStatusText(), QStringLiteral("not sent"));
    }

    void aSingleStepBlocksOtherActions() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        QVERIFY(store.save({sampleProfile(QStringLiteral("PC"), true, true)}, nullptr));
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::Refused};
        script->probeDelayMs = 150;
        MainWindow window(store, fakeFactories(script));
        std::optional<bool> ready;
        QObject::connect(&window, &MainWindow::checkFinished, [&ready](bool r) { ready = r; });

        window.startCheckOnly();
        QVERIFY(window.isRunning());
        QVERIFY(!window.findChild<QPushButton*>(QStringLiteral("wakeConnectButton"))->isEnabled());
        window.startWakeAndConnect();  // must be ignored while the check is in flight
        window.startWakeOnly();
        window.startOpenClient();
        QCOMPARE(script->probesCreated, 1);
        QCOMPARE(script->wakersCreated, 0);

        QVERIFY(QTest::qWaitFor([&ready] { return ready.has_value(); }, 5000));
        QVERIFY(!window.isRunning());
        QVERIFY(window.findChild<QPushButton*>(QStringLiteral("wakeConnectButton"))->isEnabled());
        QVERIFY2(window.serviceStatusText().contains(QStringLiteral("refused")), qPrintable(window.serviceStatusText()));
    }

    void wakeFailureUpdatesBothFacts() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        QVERIFY(store.save({sampleProfile(QStringLiteral("PC"), true, false)}, nullptr));
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut};
        script->wakeOk = false;
        MainWindow window(store, fakeFactories(script));
        std::optional<Outcome> outcome;
        window.startWakeAndConnect();
        QVERIFY(waitForRun(window, outcome));
        QCOMPARE(*outcome, Outcome::WakeFailed);
        QCOMPARE(window.wakeStatusText(), QStringLiteral("failed"));
        QVERIFY2(!window.serviceStatusText().contains(QStringLiteral("still checking")), qPrintable(window.serviceStatusText()));
        QVERIFY2(window.serviceStatusText().contains(QStringLiteral("wake request failed")), qPrintable(window.serviceStatusText()));
        QVERIFY(window.explanationVisible());
    }

    void startupWarningIsKeptForTheUser() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("profiles.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("this is not json");
        file.close();
        MainWindow window(ProfileStore(path), fakeFactories(std::make_shared<Script>()));
        QVERIFY(window.profiles().empty());
        QVERIFY2(window.pendingWarningText().contains(QStringLiteral("moved")), qPrintable(window.pendingWarningText()));
    }

    void failedSaveKeepsListAndMemoryInStep() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        QVERIFY(store.save({sampleProfile(QStringLiteral("A"), false, false), sampleProfile(QStringLiteral("B"), false, false)}, nullptr));
        MainWindow window(store, fakeFactories(std::make_shared<Script>()));
        QCOMPARE(window.profiles().size(), std::size_t(2));

        // Make the folder read-only so the next save must fail.
        const QFile::Permissions original = QFile(dir.path()).permissions();
        QVERIFY(QFile::setPermissions(dir.path(), QFile::ReadOwner | QFile::ExeOwner));
        QString error;
        const bool removed = window.removeCurrentProfile(&error);
        QFile::setPermissions(dir.path(), original);  // restore before any assertion can bail out
        QVERIFY(!removed);
        QVERIFY(!error.isEmpty());
        QCOMPARE(window.profiles().size(), std::size_t(2));
        QCOMPARE(window.listCount(), 2);
        QVERIFY(window.currentProfile() != nullptr);
    }

    // ---- Quit when the client closes ----

    void quitOptionIsInTheRunMenuAndRemembered() {
        QVERIFY2(QSettings().fileName().startsWith(QDir::tempPath()) ||
                     QSettings().fileName().startsWith(QFileInfo(QDir::tempPath()).canonicalFilePath()),
                 qPrintable(QSettings().fileName()));  // never the user's own preferences
        QSettings().remove(QLatin1String(MainWindow::kQuitWhenClientClosesKey));
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        {
            MainWindow window(store, fakeFactories(std::make_shared<Script>()));
            auto* action = window.findChild<QAction*>(QStringLiteral("quitWhenClientClosesAction"));
            QVERIFY(action != nullptr);
            QVERIFY(action->isCheckable());
            QCOMPARE(action->text(), QStringLiteral("&Quit When Client Closes"));
            QVERIFY(action->isChecked());  // on by default
            QVERIFY(window.quitWhenClientCloses());
            action->trigger();             // as a click in the menu would
            QVERIFY(!window.quitWhenClientCloses());
        }
        {
            MainWindow window(store, fakeFactories(std::make_shared<Script>()));
            QVERIFY(!window.quitWhenClientCloses());
            window.setQuitWhenClientCloses(true);
        }
        MainWindow window(store, fakeFactories(std::make_shared<Script>()));
        QVERIFY(window.quitWhenClientCloses());
    }

    void quitsWhenTheLaunchedClientCloses() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        QVERIFY(store.save({sampleProfile(QStringLiteral("PC"), true, true)}, nullptr));
        auto script = std::make_shared<Script>();
        script->launchPid = startSleeper(1);
        QVERIFY(script->launchPid > 0);
        MainWindow window(store, fakeFactories(script));
        window.setQuitWhenClientCloses(true);
        int quits = 0;
        window.setQuitHook([&quits] { ++quits; });
        std::optional<Outcome> outcome;
        window.startWakeAndConnect();
        QVERIFY(waitForRun(window, outcome));
        QCOMPARE(*outcome, Outcome::ReadyWithoutWake);
        QVERIFY(window.isWatchingClient());
        QCOMPARE(window.watchedClientPid(), script->launchPid);
        QVERIFY2(window.timelineTexts().filter(QStringLiteral("Watching fake client (process %1) — WakeDeck will quit when it closes")
                                                   .arg(script->launchPid)).size() == 1,
                 qPrintable(window.timelineTexts().join(QLatin1Char('\n'))));
        QCOMPARE(quits, 0);
        QTRY_COMPARE_WITH_TIMEOUT(quits, 1, 5000);
        QVERIFY(processGone(script->launchPid));
        QVERIFY(!window.isWatchingClient());
        QVERIFY2(window.timelineTexts().filter(QStringLiteral("fake client closed (process %1); quitting WakeDeck")
                                                   .arg(script->launchPid)).size() == 1,
                 qPrintable(window.timelineTexts().join(QLatin1Char('\n'))));
        QTest::qWait(100);
        QCOMPARE(quits, 1);
    }

    void openClientIsWatchedToo() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        QVERIFY(store.save({sampleProfile(QStringLiteral("PC"), false, true)}, nullptr));
        auto script = std::make_shared<Script>();
        script->launchPid = startSleeper(1);
        QVERIFY(script->launchPid > 0);
        MainWindow window(store, fakeFactories(script));
        window.setQuitWhenClientCloses(true);
        int quits = 0;
        window.setQuitHook([&quits] { ++quits; });
        QSignalSpy step(&window, &MainWindow::stepFinished);
        window.startOpenClient();
        QTRY_COMPARE_WITH_TIMEOUT(step.count(), 1, 5000);
        QCOMPARE(window.watchedClientPid(), script->launchPid);
        QVERIFY(window.timelineTexts().filter(QStringLiteral("Watching fake client")).size() == 1);
        QTRY_COMPARE_WITH_TIMEOUT(quits, 1, 5000);
    }

    void staysOpenWhenTheOptionIsOff() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        QVERIFY(store.save({sampleProfile(QStringLiteral("PC"), true, true)}, nullptr));
        auto script = std::make_shared<Script>();
        script->launchPid = startSleeper(1);
        QVERIFY(script->launchPid > 0);
        MainWindow window(store, fakeFactories(script));
        window.setQuitWhenClientCloses(false);
        int quits = 0;
        window.setQuitHook([&quits] { ++quits; });
        std::optional<Outcome> outcome;
        window.startWakeAndConnect();
        QVERIFY(waitForRun(window, outcome));
        QCOMPARE(*outcome, Outcome::ReadyWithoutWake);
        QVERIFY(!window.isWatchingClient());
        QVERIFY(window.timelineTexts().filter(QStringLiteral("Watching")).isEmpty());
        QVERIFY(QTest::qWaitFor([&script] { return processGone(script->launchPid); }, 5000));
        QTest::qWait(300);
        QCOMPARE(quits, 0);
        window.setQuitWhenClientCloses(true);
    }

    void turningTheOptionOffStopsTheWatch() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        QVERIFY(store.save({sampleProfile(QStringLiteral("PC"), true, true)}, nullptr));
        auto script = std::make_shared<Script>();
        script->launchPid = startSleeper(1);
        QVERIFY(script->launchPid > 0);
        MainWindow window(store, fakeFactories(script));
        window.setQuitWhenClientCloses(true);
        int quits = 0;
        window.setQuitHook([&quits] { ++quits; });
        std::optional<Outcome> outcome;
        window.startWakeAndConnect();
        QVERIFY(waitForRun(window, outcome));
        QVERIFY(window.isWatchingClient());
        window.setQuitWhenClientCloses(false);
        QVERIFY(!window.isWatchingClient());
        QVERIFY(window.timelineTexts().filter(QStringLiteral("No longer watching fake client")).size() == 1);
        QVERIFY(QTest::qWaitFor([&script] { return processGone(script->launchPid); }, 5000));
        QTest::qWait(300);
        QCOMPARE(quits, 0);
        window.setQuitWhenClientCloses(true);
    }

    void aNewRunCancelsThePreviousWatch() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        QVERIFY(store.save({sampleProfile(QStringLiteral("PC"), true, true)}, nullptr));
        auto script = std::make_shared<Script>();
        const qint64 first = startSleeper(1);
        QVERIFY(first > 0);
        script->launchPid = first;
        MainWindow window(store, fakeFactories(script));
        window.setQuitWhenClientCloses(true);
        int quits = 0;
        window.setQuitHook([&quits] { ++quits; });
        std::optional<Outcome> outcome;
        window.startWakeAndConnect();
        QVERIFY(waitForRun(window, outcome));
        QCOMPARE(window.watchedClientPid(), first);

        // The second run's client reports no process of its own (already running).
        script->launchPid = 0;
        outcome.reset();
        window.startWakeAndConnect();
        QVERIFY(!window.isWatchingClient());  // stopped as soon as the run starts
        QVERIFY(QTest::qWaitFor([&outcome] { return outcome.has_value(); }, 5000));
        QVERIFY(!window.isWatchingClient());
        QVERIFY2(window.timelineTexts().filter(QStringLiteral("Not watching fake client")).size() == 1,
                 qPrintable(window.timelineTexts().join(QLatin1Char('\n'))));

        // A third run with a new client: only that one is watched.
        const qint64 third = startSleeper(3);
        QVERIFY(third > 0);
        script->launchPid = third;
        outcome.reset();
        window.startWakeAndConnect();
        QVERIFY(QTest::qWaitFor([&outcome] { return outcome.has_value(); }, 5000));
        QCOMPARE(window.watchedClientPid(), third);
        QVERIFY(QTest::qWaitFor([first] { return processGone(first); }, 5000));
        QTest::qWait(300);
        QCOMPARE(quits, 0);  // the first client's exit is no longer WakeDeck's business
        QTRY_COMPARE_WITH_TIMEOUT(quits, 1, 6000);
    }

    // ---- Editor ----

    void editorRoundTrip() {
        Profile profile = sampleProfile(QStringLiteral("Round Trip"), true, false);
        profile.notes = QStringLiteral("notes here");
        profile.client.preset = ClientConfig::Preset::Moonlight;
        profile.client.launchMode = QStringLiteral("stream");
        profile.client.appName = QStringLiteral("Desktop");
        profile.pollIntervalMs = 4000;
        profile.overallTimeoutMs = 90000;
        ProfileEditorDialog dialog(profile, false);
        QString error;
        const auto collected = dialog.collect(&error);
        QVERIFY2(collected.has_value(), qPrintable(error));
        QCOMPARE(toJson(*collected), toJson(profile));
    }

    void editorValidatesStepByStep() {
        ProfileEditorDialog dialog(Profile{}, true);
        QString error;
        QVERIFY(!dialog.collect(&error).has_value());
        QVERIFY2(error.contains(QStringLiteral("name")), qPrintable(error));

        dialog.findChild<QLineEdit*>(QStringLiteral("nameEdit"))->setText(QStringLiteral("PC"));
        QVERIFY(!dialog.collect(&error).has_value());
        QVERIFY2(error.contains(QStringLiteral("host")), qPrintable(error));

        dialog.findChild<QLineEdit*>(QStringLiteral("hostEdit"))->setText(QStringLiteral("pc.example.net"));
        QVERIFY2(dialog.collect(&error).has_value(), qPrintable(error));  // a check-only computer is complete

        dialog.findChild<QCheckBox*>(QStringLiteral("wakeEnabled"))->setChecked(true);
        dialog.findChild<QLineEdit*>(QStringLiteral("macEdit"))->setText(QStringLiteral("nope"));
        QVERIFY(!dialog.collect(&error).has_value());
        QVERIFY2(error.contains(QStringLiteral("MAC")), qPrintable(error));

        dialog.findChild<QLineEdit*>(QStringLiteral("macEdit"))->setText(QStringLiteral("00-00-5e-00-53-2a"));
        QVERIFY(!dialog.collect(&error).has_value());
        QVERIFY2(error.contains(QStringLiteral("send")), qPrintable(error));

        dialog.findChild<QLineEdit*>(QStringLiteral("destinationEdit"))->setText(QStringLiteral("192.168.1.255"));
        dialog.findChild<QLineEdit*>(QStringLiteral("portsEdit"))->setText(QStringLiteral("9,x"));
        QVERIFY(!dialog.collect(&error).has_value());
        QVERIFY2(error.contains(QStringLiteral("ports")), qPrintable(error));

        dialog.findChild<QLineEdit*>(QStringLiteral("portsEdit"))->setText(QStringLiteral("9,7"));
        dialog.findChild<QComboBox*>(QStringLiteral("clientCombo"))->setCurrentIndex(2);  // custom, no path
        QVERIFY(!dialog.collect(&error).has_value());
        QVERIFY2(error.contains(QStringLiteral("path")), qPrintable(error));

        dialog.findChild<QLineEdit*>(QStringLiteral("clientPathEdit"))->setText(QStringLiteral("/usr/bin/true"));
        const auto profile = dialog.collect(&error);
        QVERIFY2(profile.has_value(), qPrintable(error));
        QCOMPARE(QString::fromStdString(profile->wake->mac.toString()), QStringLiteral("00:00:5E:00:53:2A"));
        QCOMPARE(profile->client.preset, ClientConfig::Preset::Custom);
    }

    void editorMoonlightNeedsAnAppToStream() {
        Profile profile = sampleProfile(QStringLiteral("PC"), false, false);
        profile.client.preset = ClientConfig::Preset::Moonlight;
        ProfileEditorDialog dialog(profile, false);
        dialog.findChild<QComboBox*>(QStringLiteral("launchCombo"))->setCurrentIndex(1);  // stream
        QString error;
        QVERIFY(!dialog.collect(&error).has_value());
        QVERIFY2(error.contains(QStringLiteral("app")), qPrintable(error));
        dialog.findChild<QLineEdit*>(QStringLiteral("appEdit"))->setText(QStringLiteral("Desktop"));
        const auto collected = dialog.collect(&error);
        QVERIFY2(collected.has_value(), qPrintable(error));
        QCOMPARE(collected->client.appName, QStringLiteral("Desktop"));
        QCOMPARE(collected->client.launchMode, QStringLiteral("stream"));
    }
};

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    // Settings go to an INI file in a temporary folder under a test name, so the
    // window never reads or writes the user's own WakeDeck preferences.
    QTemporaryDir settingsDir;
    if (!settingsDir.isValid()) return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsDir.path());
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("WakeDeckTests"));
    QCoreApplication::setApplicationName(QStringLiteral("test_gui"));
    GuiTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_gui.moc"
