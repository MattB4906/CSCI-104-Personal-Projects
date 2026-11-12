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
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <functional>
#include <sys/types.h>
#include <memory>
#include <optional>
#include <vector>

#include "agent_bundle.h"
#include "app/Diagnose.h"
#include "app/MainWindow.h"
#include "app/ProfileEditorDialog.h"
#include "app/SystemServices.h"
#include "app/WaitProgress.h"
#include "engine/ClientLauncher.h"
#include "engine/Profile.h"
#include "engine/ProfileStore.h"
#include "engine/Session.h"
#include "engine/Tailscale.h"
#include "engine/TimingHistory.h"

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
    // Reports its process ids even when it fails, so the tests show that a
    // failed launch is never watched.
    FakeLauncher(bool ok, qint64 pid, qint64 runningPid, QObject* parent, std::function<void()> onStart = {})
        : Launcher(parent), ok_(ok), pid_(pid), runningPid_(runningPid), onStart_(std::move(onStart)) {}
    QString describe() const override { return QStringLiteral("fake client"); }
    qint64 launchedPid() const override { return pid_; }
    qint64 runningPid() const override { return runningPid_ > 0 ? runningPid_ : pid_; }
    void start() override {
        if (onStart_) onStart_();
        QTimer::singleShot(0, this, [this] {
            emit finished(ok_, ok_ ? QStringLiteral("Launched fake client") : QStringLiteral("the client /nonexistent was not found"));
        });
    }

private:
    bool ok_;
    qint64 pid_;
    qint64 runningPid_;
    std::function<void()> onStart_;
};

struct Script {
    std::vector<ProbeStatus> statuses{ProbeStatus::Ready};
    bool launchOk = true;
    bool wakeOk = true;
    int probeDelayMs = 0;
    qint64 launchPid = 0;   // what the fake launcher reports as the process it started
    qint64 runningPid = 0;  // the running copy it found, when it started none (0: same as launchPid)
    int probesCreated = 0;
    int wakersCreated = 0;
    QString tailscaleState;  // empty: no Tailscale reader; otherwise its state, with the computer online
    std::function<void()> onLaunch;  // runs when the fake launcher starts, e.g. a client taking the focus
};

class FakeTailscaleReader final : public TailscaleReader {
public:
    FakeTailscaleReader(TailscaleStatus status, QObject* parent) : TailscaleReader(parent), status_(std::move(status)) {}
    void start() override {
        QTimer::singleShot(0, this, [this] { emit finished(status_); });
    }

private:
    TailscaleStatus status_;
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
            factories.launcher = [script](QObject* parent) -> Launcher* { return new FakeLauncher(script->launchOk, script->launchPid, script->runningPid, parent, script->onLaunch); };
        }
        factories.tailscale = [script](QObject* parent) -> TailscaleReader* {
            if (script->tailscaleState.isEmpty()) return nullptr;
            TailscaleStatus status;
            status.available = true;
            status.backendState = script->tailscaleState;
            TailscalePeer peer;
            peer.dnsName = QStringLiteral("living-room-pc.example-tailnet.ts.net.");
            peer.addresses = {QStringLiteral("100.64.0.10")};
            peer.online = true;
            status.peers.push_back(peer);
            return new FakeTailscaleReader(status, parent);
        };
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

// What the fake notifier and the fake Dock progress were asked to do.
struct SystemLog {
    std::vector<std::pair<QString, QString>> notifications;  // title, body
    std::vector<double> dock;                                // fractions; kDockCleared for clear()
    bool refuse = false;                                     // the fake system says notifications are off
    bool active = false;                                     // WakeDeck is the active application
};
constexpr double kDockCleared = -1.0;

class FakeNotifier final : public wakedeck::app::Notifier {
public:
    explicit FakeNotifier(std::shared_ptr<SystemLog> log) : log_(std::move(log)) {}
    bool available() const override { return true; }
    void notify(const QString& title, const QString& body) override {
        log_->notifications.emplace_back(title, body);
        if (log_->refuse) {
            // Later, as the real one would; twice, to show the window says it once.
            QTimer::singleShot(0, this, [this] { emit refused(); });
            QTimer::singleShot(0, this, [this] { emit refused(); });
        }
    }

private:
    std::shared_ptr<SystemLog> log_;
};

class FakeDockProgress final : public wakedeck::app::DockProgress {
public:
    explicit FakeDockProgress(std::shared_ptr<SystemLog> log) : log_(std::move(log)) {}
    void setProgress(double fraction) override { log_->dock.push_back(fraction); }
    void clear() override { log_->dock.push_back(kDockCleared); }

private:
    std::shared_ptr<SystemLog> log_;
};

std::shared_ptr<SystemLog> useFakeSystem(MainWindow& window) {
    auto log = std::make_shared<SystemLog>();
    window.setNotifier(std::make_unique<FakeNotifier>(log));
    window.setDockProgress(std::make_unique<FakeDockProgress>(log));
    window.setAppActivePredicate([log] { return log->active; });
    window.setNotifyWhenReady(true);
    return log;
}

int countOf(const std::vector<double>& values, double value) {
    return static_cast<int>(std::count(values.begin(), values.end(), value));
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

    void aClientWakeDeckOpenedIsWatchedAgain() {
        // Open mode: the first run starts the client, which stays open after the
        // stream; the next run and Open client only bring it to the front. It is
        // still the copy WakeDeck started, so it is watched again each time.
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        QVERIFY(store.save({sampleProfile(QStringLiteral("PC"), true, true)}, nullptr));
        auto script = std::make_shared<Script>();
        const qint64 own = startSleeper(2);
        QVERIFY(own > 0);
        script->launchPid = own;
        MainWindow window(store, fakeFactories(script));
        window.setQuitWhenClientCloses(true);
        int quits = 0;
        window.setQuitHook([&quits] { ++quits; });
        std::optional<Outcome> outcome;
        window.startWakeAndConnect();
        QVERIFY(waitForRun(window, outcome));
        QCOMPARE(window.watchedClientPid(), own);

        script->launchPid = 0;
        script->runningPid = own;
        outcome.reset();
        window.startWakeAndConnect();
        QVERIFY(QTest::qWaitFor([&outcome] { return outcome.has_value(); }, 5000));
        QCOMPARE(*outcome, Outcome::ReadyWithoutWake);
        QCOMPARE(window.watchedClientPid(), own);
        QVERIFY2(window.timelineTexts().filter(QStringLiteral("opened by WakeDeck earlier) — WakeDeck will quit when it closes")).size() == 1,
                 qPrintable(window.timelineTexts().join(QLatin1Char('\n'))));

        QSignalSpy step(&window, &MainWindow::stepFinished);
        window.startOpenClient();
        QVERIFY(!window.isWatchingClient());
        QTRY_COMPARE_WITH_TIMEOUT(step.count(), 1, 5000);
        QCOMPARE(window.watchedClientPid(), own);

        QCOMPARE(quits, 0);
        QTRY_COMPARE_WITH_TIMEOUT(quits, 1, 5000);
        QCOMPARE(window.ownedClientPid(), qint64(0));
    }

    void aClientWakeDeckDidNotStartIsNeverWatched() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        QVERIFY(store.save({sampleProfile(QStringLiteral("PC"), true, true)}, nullptr));
        auto script = std::make_shared<Script>();
        const qint64 users = startSleeper(4);  // a copy the user opened
        const qint64 own = startSleeper(2);
        QVERIFY(users > 0 && own > 0);
        MainWindow window(store, fakeFactories(script));
        window.setQuitWhenClientCloses(true);
        int quits = 0;
        window.setQuitHook([&quits] { ++quits; });
        auto run = [&window] {
            std::optional<Outcome> outcome;
            QObject::connect(&window, &MainWindow::runFinished, &window, [&outcome](Outcome o) { outcome = o; },
                             Qt::SingleShotConnection);
            window.startWakeAndConnect();
            return QTest::qWaitFor([&outcome] { return outcome.has_value(); }, 5000) && *outcome == Outcome::ReadyWithoutWake;
        };

        // Already running before WakeDeck ever started a client.
        script->runningPid = users;
        QVERIFY(run());
        QVERIFY(!window.isWatchingClient());
        QCOMPARE(window.ownedClientPid(), qint64(0));
        QVERIFY(window.timelineTexts().filter(QStringLiteral("Not watching fake client: WakeDeck did not start")).size() == 1);

        // WakeDeck starts one; a later run finds the user's copy instead.
        script->launchPid = own;
        script->runningPid = 0;
        QVERIFY(run());
        QCOMPARE(window.watchedClientPid(), own);
        script->launchPid = 0;
        script->runningPid = users;
        QVERIFY(run());
        QVERIFY(!window.isWatchingClient());
        QCOMPARE(window.ownedClientPid(), own);  // still followed, but no longer quits WakeDeck

        // Once WakeDeck's copy has ended, its old process id means nothing.
        QVERIFY(QTest::qWaitFor([own] { return processGone(own); }, 5000));
        QTRY_COMPARE_WITH_TIMEOUT(window.ownedClientPid(), qint64(0), 2000);
        QCOMPARE(quits, 0);
        script->runningPid = own;
        QVERIFY(run());
        QVERIFY(!window.isWatchingClient());

        QVERIFY(QTest::qWaitFor([users] { return processGone(users); }, 7000));
        QTest::qWait(300);
        QCOMPARE(quits, 0);
    }

    void openedClientIsWatchedAgainEndToEnd() {
        // The real launcher and the real open path, with the windowless fake
        // agent as the client: the first run opens it and watches it; the second
        // only brings it to the front and watches it again, because WakeDeck
        // started it; when it ends, the quit hook runs.
        QTemporaryDir dir;
        const QString app = makeAgentBundle(QDir(dir.path()));
        QVERIFY(!app.isEmpty());
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        Profile profile = sampleProfile(QStringLiteral("PC"), false, true);
        profile.client.path = app;  // a custom bundle without arguments is opened
        QVERIFY(store.save({profile}, nullptr));
        auto script = std::make_shared<Script>();
        const MainWindow::FactoriesFor fakes = fakeFactories(script);
        MainWindow window(store, [fakes](const Profile& p) {
            SessionFactories factories = fakes(p);
            factories.launcher = makeLauncherFactory(p.client, p.check.host);
            return factories;
        });
        window.setQuitWhenClientCloses(true);
        int quits = 0;
        window.setQuitHook([&quits] { ++quits; });

        std::optional<Outcome> outcome;
        window.startWakeAndConnect();
        QVERIFY(waitForRun(window, outcome, 15000));
        QCOMPARE(*outcome, Outcome::ReadyWithoutWake);
        const qint64 agent = window.watchedClientPid();
        QVERIFY2(agent > 0, qPrintable(window.timelineTexts().join(QLatin1Char('\n'))));
        const auto endAgent = qScopeGuard([agent] { ::kill(static_cast<pid_t>(agent), SIGTERM); });

        outcome.reset();
        window.startWakeAndConnect();
        QVERIFY(QTest::qWaitFor([&outcome] { return outcome.has_value(); }, 15000));
        QCOMPARE(*outcome, Outcome::ReadyWithoutWake);
        QCOMPARE(window.watchedClientPid(), agent);
        QVERIFY2(window.timelineTexts().filter(QStringLiteral("opened by WakeDeck earlier")).size() == 1,
                 qPrintable(window.timelineTexts().join(QLatin1Char('\n'))));
        QCOMPARE(quits, 0);

        ::kill(static_cast<pid_t>(agent), SIGTERM);
        QTRY_COMPARE_WITH_TIMEOUT(quits, 1, 5000);
    }

    void ownershipIsKeptWhileTheOptionIsOff() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        QVERIFY(store.save({sampleProfile(QStringLiteral("PC"), true, true)}, nullptr));
        auto script = std::make_shared<Script>();
        const qint64 own = startSleeper(2);
        QVERIFY(own > 0);
        script->launchPid = own;
        MainWindow window(store, fakeFactories(script));
        window.setQuitWhenClientCloses(false);
        int quits = 0;
        window.setQuitHook([&quits] { ++quits; });
        std::optional<Outcome> outcome;
        window.startWakeAndConnect();
        QVERIFY(waitForRun(window, outcome));
        QVERIFY(!window.isWatchingClient());
        QCOMPARE(window.ownedClientPid(), own);
        QVERIFY(window.timelineTexts().filter(QStringLiteral("atching")).isEmpty());

        window.setQuitWhenClientCloses(true);
        script->launchPid = 0;
        script->runningPid = own;
        outcome.reset();
        window.startWakeAndConnect();
        QVERIFY(QTest::qWaitFor([&outcome] { return outcome.has_value(); }, 5000));
        QCOMPARE(window.watchedClientPid(), own);
        QTRY_COMPARE_WITH_TIMEOUT(quits, 1, 5000);
    }

    void singleStepsStopTheWatch() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        QVERIFY(store.save({sampleProfile(QStringLiteral("PC"), true, true)}, nullptr));
        auto script = std::make_shared<Script>();
        MainWindow window(store, fakeFactories(script));
        window.setQuitWhenClientCloses(true);
        int quits = 0;
        window.setQuitHook([&quits] { ++quits; });
        QSignalSpy steps(&window, &MainWindow::stepFinished);
        const std::vector<std::function<void()>> stepActions = {
            [&window] { window.startCheckOnly(); },
            [&window] { window.startWakeOnly(); },
            [&window] { window.startOpenClient(); },  // finds no process of its own below
        };
        std::vector<qint64> clients;
        for (std::size_t i = 0; i < stepActions.size(); ++i) {
            const qint64 pid = startSleeper(2);
            QVERIFY(pid > 0);
            clients.push_back(pid);
            script->launchPid = pid;
            std::optional<Outcome> outcome;
            QObject::connect(&window, &MainWindow::runFinished, &window, [&outcome](Outcome o) { outcome = o; },
                             Qt::SingleShotConnection);
            window.startWakeAndConnect();
            QVERIFY(QTest::qWaitFor([&outcome] { return outcome.has_value(); }, 5000));
            QCOMPARE(window.watchedClientPid(), pid);

            script->launchPid = 0;
            stepActions[i]();
            QVERIFY2(!window.isWatchingClient(), qPrintable(QStringLiteral("step %1").arg(i)));
            QTRY_COMPARE_WITH_TIMEOUT(steps.count(), static_cast<int>(i + 1), 5000);
            QVERIFY2(!window.isWatchingClient(), qPrintable(QStringLiteral("step %1").arg(i)));
        }
        for (const qint64 pid : clients) QVERIFY(QTest::qWaitFor([pid] { return processGone(pid); }, 5000));
        QTest::qWait(300);
        QCOMPARE(quits, 0);
    }

    void failedRunsStartNoWatch() {
        const qint64 pid = startSleeper(1);  // what every launcher below reports, even failing
        QVERIFY(pid > 0);
        int quits = 0;
        struct Case {
            Outcome expected;
            std::vector<ProbeStatus> statuses;
            bool launchOk;
            int overallTimeoutMs;
            bool cancel;
        };
        const std::vector<Case> cases = {
            {Outcome::TimedOut, {ProbeStatus::TimedOut}, true, 60, false},
            {Outcome::LaunchFailed, {ProbeStatus::Ready}, false, 2000, false},
            {Outcome::Cancelled, {ProbeStatus::TimedOut}, true, 10000, true},
        };
        for (const Case& c : cases) {
            QTemporaryDir dir;
            const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
            Profile profile = sampleProfile(QStringLiteral("PC"), true, true);
            profile.overallTimeoutMs = c.overallTimeoutMs;
            QVERIFY(store.save({profile}, nullptr));
            auto script = std::make_shared<Script>();
            script->statuses = c.statuses;
            script->launchOk = c.launchOk;
            script->launchPid = pid;
            MainWindow window(store, fakeFactories(script));
            window.setQuitWhenClientCloses(true);
            window.setQuitHook([&quits] { ++quits; });
            std::optional<Outcome> outcome;
            window.startWakeAndConnect();
            if (c.cancel) QTimer::singleShot(30, &window, [&window] { window.cancelRun(); });
            QVERIFY(waitForRun(window, outcome));
            QCOMPARE(*outcome, c.expected);
            QVERIFY(!window.isWatchingClient());
            QCOMPARE(window.ownedClientPid(), qint64(0));
            QVERIFY2(window.timelineTexts().filter(QStringLiteral("atching")).isEmpty(),
                     qPrintable(window.timelineTexts().join(QLatin1Char('\n'))));
        }
        QVERIFY(QTest::qWaitFor([pid] { return processGone(pid); }, 5000));
        QTest::qWait(300);
        QCOMPARE(quits, 0);
    }

    // ---- Resending and every local network (1.2) ----

    void wakeFactCountsResends() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        QVERIFY(store.save({sampleProfile(QStringLiteral("PC"), true, false)}, nullptr));
        auto script = std::make_shared<Script>();
        script->probeDelayMs = 10;
        script->statuses = std::vector<ProbeStatus>(15, ProbeStatus::TimedOut);
        script->statuses.push_back(ProbeStatus::Ready);
        MainWindow window(store, fakeFactories(script));
        window.setSessionConfigHook([](SessionConfig& config) { config.resendIntervalMs = 30; });
        std::optional<Outcome> outcome;
        window.startWakeAndConnect();
        QVERIFY(waitForRun(window, outcome));
        QCOMPARE(*outcome, Outcome::ReadyAfterWake);
        QVERIFY2(script->wakersCreated >= 3, qPrintable(QString::number(script->wakersCreated)));
        const QString fact = window.wakeStatusText();
        // A resend still in flight when the service answered may not be counted, so no exact number.
        QVERIFY2(fact.startsWith(QStringLiteral("sent ")) && fact.contains(QStringLiteral(" times, last at ")), qPrintable(fact));
        QVERIFY2(fact.endsWith(QStringLiteral("(10 packets to 192.0.2.255 (ports 9 x5, 7 x5))")), qPrintable(fact));
        QCOMPARE(window.timelineTexts().filter(QStringLiteral("Wake request sent again (2nd time)")).size(), 1);
        QCOMPARE(window.timelineTexts().filter(QStringLiteral("Wake request sent: ")).size(), 1);
    }

    void detailsPaneShowsResendAndDestination() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        Profile everyNetwork = sampleProfile(QStringLiteral("Local PC"), true, false);
        everyNetwork.wake->destinationMode = WakeDestination::LocalNetworks;
        everyNetwork.wake->target.destination.clear();
        everyNetwork.wake->resendSeconds = 0;
        QVERIFY(store.save({sampleProfile(QStringLiteral("PC"), true, false), everyNetwork}, nullptr));
        MainWindow window(store, fakeFactories(std::make_shared<Script>()));
        window.selectProfile(QStringLiteral("PC"));
        QCOMPARE(window.wakeDetailsText(), QStringLiteral("10 packets to 192.0.2.255 (ports 9 x5, 7 x5); again every 15 s while waiting"));
        window.selectProfile(QStringLiteral("Local PC"));
        QCOMPARE(window.wakeDetailsText(), QStringLiteral("10 packets to every local network of this Mac (ports 9 x5, 7 x5); sent once"));
    }

    void editorChoosesEveryLocalNetworkAndResend() {
        Profile profile = sampleProfile(QStringLiteral("Local PC"), true, false);
        profile.wake->destinationMode = WakeDestination::LocalNetworks;
        profile.wake->target.destination.clear();
        profile.wake->resendSeconds = 30;
        profile.pollIntervalMs = 4000;  // the editor works in whole seconds
        ProfileEditorDialog dialog(profile, false);
        auto* mode = dialog.findChild<QComboBox*>(QStringLiteral("destinationModeCombo"));
        auto* address = dialog.findChild<QLineEdit*>(QStringLiteral("destinationEdit"));
        auto* resend = dialog.findChild<QSpinBox*>(QStringLiteral("resendSpin"));
        QVERIFY(mode && address && resend);
        QCOMPARE(mode->itemText(0), QStringLiteral("Every local network this Mac is on"));
        QCOMPARE(mode->itemText(1), QStringLiteral("A specific address"));
        QCOMPARE(mode->currentData().toString(), QStringLiteral("localNetworks"));
        QVERIFY(!address->isEnabled());
        QCOMPARE(resend->value(), 30);
        QString error;
        auto collected = dialog.collect(&error);
        QVERIFY2(collected.has_value(), qPrintable(error));
        QCOMPARE(toJson(*collected), toJson(profile));

        // A specific address: the field is enabled and required again.
        mode->setCurrentIndex(1);
        QVERIFY(address->isEnabled());
        QVERIFY(!dialog.collect(&error).has_value());
        QVERIFY2(error.contains(QStringLiteral("send")), qPrintable(error));
        address->setText(QStringLiteral("192.168.1.255"));
        collected = dialog.collect(&error);
        QVERIFY2(collected.has_value(), qPrintable(error));
        QCOMPARE(collected->wake->destinationMode, WakeDestination::Address);
        QCOMPARE(collected->wake->target.destination, std::string("192.168.1.255"));

        // Back to every network: whatever is in the address field is not stored.
        mode->setCurrentIndex(0);
        collected = dialog.collect(&error);
        QVERIFY2(collected.has_value(), qPrintable(error));
        QVERIFY(collected->wake->target.destination.empty());

        // "Send again every": 0 reads "Only once"; 1 to 4 seconds are refused.
        resend->setValue(3);
        QVERIFY(!dialog.collect(&error).has_value());
        QVERIFY2(error.contains(QStringLiteral("Send again every")), qPrintable(error));
        resend->setValue(0);
        QCOMPARE(resend->text(), QStringLiteral("Only once"));
        collected = dialog.collect(&error);
        QVERIFY2(collected.has_value(), qPrintable(error));
        QCOMPARE(collected->wake->resendSeconds, 0);
    }

    void newComputerDefaultsToASpecificAddress() {
        ProfileEditorDialog dialog(Profile{}, true);
        QCOMPARE(dialog.findChild<QComboBox*>(QStringLiteral("destinationModeCombo"))->currentData().toString(), QStringLiteral("address"));
        QCOMPARE(dialog.findChild<QSpinBox*>(QStringLiteral("resendSpin"))->value(), 15);
        dialog.findChild<QCheckBox*>(QStringLiteral("wakeEnabled"))->setChecked(true);
        QVERIFY(dialog.findChild<QLineEdit*>(QStringLiteral("destinationEdit"))->isEnabled());
    }

    // ---- Notification and progress ----

    void notifyOptionIsInTheRunMenuAndRemembered() {
        QSettings().remove(QLatin1String(MainWindow::kNotifyWhenReadyKey));
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        {
            MainWindow window(store, fakeFactories(std::make_shared<Script>()));
            auto* action = window.findChild<QAction*>(QStringLiteral("notifyWhenReadyAction"));
            QVERIFY(action != nullptr);
            QVERIFY(action->isCheckable());
            QCOMPARE(action->text(), QStringLiteral("&Notify When Ready"));
            QVERIFY(action->isChecked());  // on by default
            action->trigger();
            QVERIFY(!window.notifyWhenReady());
        }
        QCOMPARE(QSettings().value(QLatin1String(MainWindow::kNotifyWhenReadyKey)).toBool(), false);
        {
            MainWindow window(store, fakeFactories(std::make_shared<Script>()));
            QVERIFY(!window.notifyWhenReady());
            window.setNotifyWhenReady(true);
        }
        MainWindow window(store, fakeFactories(std::make_shared<Script>()));
        QVERIFY(window.notifyWhenReady());
    }

    void notifiesHowEachRunEndedWhenInactive() {
        struct Case {
            Outcome expected;
            bool withWake;
            bool withClient;
            std::vector<ProbeStatus> statuses;
            bool wakeOk;
            bool launchOk;
            int overallTimeoutMs;
            QString title;
            QString bodyStart;
        };
        const std::vector<Case> cases = {
            {Outcome::ReadyWithoutWake, true, true, {ProbeStatus::Ready}, true, true, 2000, QStringLiteral("PC is ready"),
             QStringLiteral("Service was already ready")},
            {Outcome::ReadyAfterWake, true, false, {ProbeStatus::TimedOut, ProbeStatus::Ready}, true, true, 2000,
             QStringLiteral("PC is ready"), QStringLiteral("Service ready")},
            {Outcome::TimedOut, true, false, {ProbeStatus::TimedOut}, true, true, 60, QStringLiteral("PC did not answer"),
             QStringLiteral("No answer from")},
            {Outcome::WakeFailed, true, false, {ProbeStatus::TimedOut}, false, true, 2000, QStringLiteral("Could not wake PC"),
             QStringLiteral("The wake request could not be sent")},
            {Outcome::LaunchFailed, false, true, {ProbeStatus::Ready}, true, false, 2000,
             QStringLiteral("PC is ready, but the client did not open"), QStringLiteral("The service answered, but the client")},
        };
        for (const Case& c : cases) {
            QTemporaryDir dir;
            const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
            Profile profile = sampleProfile(QStringLiteral("PC"), c.withWake, c.withClient);
            profile.overallTimeoutMs = c.overallTimeoutMs;
            QVERIFY(store.save({profile}, nullptr));
            auto script = std::make_shared<Script>();
            script->statuses = c.statuses;
            script->wakeOk = c.wakeOk;
            script->launchOk = c.launchOk;
            MainWindow window(store, fakeFactories(script));
            const auto log = useFakeSystem(window);
            std::optional<Outcome> outcome;
            window.startWakeAndConnect();
            QVERIFY(waitForRun(window, outcome));
            QCOMPARE(*outcome, c.expected);
            QCOMPARE(log->notifications.size(), std::size_t(1));
            QCOMPARE(log->notifications[0].first, c.title);
            const QString body = log->notifications[0].second;
            QVERIFY2(body.startsWith(c.bodyStart), qPrintable(body));
            QVERIFY2(body.endsWith(QLatin1Char('.')) && !body.contains(QLatin1Char('\n')), qPrintable(body));
        }
    }

    void noNotificationWhenActiveOffCancelledOrForSingleSteps() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        Profile profile = sampleProfile(QStringLiteral("PC"), true, true);
        profile.overallTimeoutMs = 10000;
        QVERIFY(store.save({profile}, nullptr));
        auto script = std::make_shared<Script>();
        MainWindow window(store, fakeFactories(script));
        const auto log = useFakeSystem(window);
        auto run = [&window] {
            std::optional<Outcome> outcome;
            QObject::connect(&window, &MainWindow::runFinished, &window, [&outcome](Outcome o) { outcome = o; },
                             Qt::SingleShotConnection);
            window.startWakeAndConnect();
            return QTest::qWaitFor([&outcome] { return outcome.has_value(); }, 5000) ? outcome : std::nullopt;
        };

        log->active = true;  // the user is looking at WakeDeck
        QCOMPARE(run(), std::optional<Outcome>(Outcome::ReadyWithoutWake));
        log->active = false;
        window.setNotifyWhenReady(false);
        QCOMPARE(run(), std::optional<Outcome>(Outcome::ReadyWithoutWake));
        window.setNotifyWhenReady(true);

        script->statuses = {ProbeStatus::TimedOut};
        QTimer::singleShot(50, &window, [&window] { window.cancelRun(); });
        QCOMPARE(run(), std::optional<Outcome>(Outcome::Cancelled));

        QSignalSpy steps(&window, &MainWindow::stepFinished);
        window.startCheckOnly();
        QTRY_COMPARE_WITH_TIMEOUT(steps.count(), 1, 5000);
        window.startWakeOnly();
        QTRY_COMPARE_WITH_TIMEOUT(steps.count(), 2, 5000);
        window.startOpenClient();
        QTRY_COMPARE_WITH_TIMEOUT(steps.count(), 3, 5000);
        QTest::qWait(50);
        QVERIFY(log->notifications.empty());

        script->statuses = {ProbeStatus::Ready};  // the same window still notifies when it should
        QCOMPARE(run(), std::optional<Outcome>(Outcome::ReadyWithoutWake));
        QCOMPARE(log->notifications.size(), std::size_t(1));
    }

    void aReadyRunIsJudgedByTheMomentTheServiceAnswered() {
        // Review finding B: the client takes the focus before the run ends.
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        QVERIFY(store.save({sampleProfile(QStringLiteral("PC"), true, true)}, nullptr));
        auto script = std::make_shared<Script>();
        MainWindow window(store, fakeFactories(script));
        const auto log = useFakeSystem(window);

        // Active when the service answered, inactive at the end: no notification.
        log->active = true;
        script->onLaunch = [log] { log->active = false; };
        std::optional<Outcome> outcome;
        window.startWakeAndConnect();
        QVERIFY(waitForRun(window, outcome));
        QCOMPARE(*outcome, Outcome::ReadyWithoutWake);
        QVERIFY(!log->active);
        QVERIFY(log->notifications.empty());

        // Inactive when it answered, active at the end: one notification.
        log->active = false;
        script->onLaunch = [log] { log->active = true; };
        script->probesCreated = 0;
        outcome.reset();
        window.startWakeAndConnect();
        QVERIFY(waitForRun(window, outcome));
        QCOMPARE(*outcome, Outcome::ReadyWithoutWake);
        QCOMPARE(log->notifications.size(), std::size_t(1));
        QCOMPARE(log->notifications.front().first, QStringLiteral("PC is ready"));

        // A timeout is judged at the end, as before.
        script->statuses = {ProbeStatus::TimedOut};
        script->probesCreated = 0;
        script->onLaunch = {};
        log->active = true;
        Profile slow = sampleProfile(QStringLiteral("PC"), true, true);
        slow.overallTimeoutMs = 60;
        QVERIFY(window.addProfile(slow));
        outcome.reset();
        QTimer::singleShot(0, &window, [log] { log->active = false; });
        window.startWakeAndConnect();
        QVERIFY(waitForRun(window, outcome));
        QCOMPARE(*outcome, Outcome::TimedOut);
        QCOMPARE(log->notifications.size(), std::size_t(2));
        QCOMPARE(log->notifications.back().first, QStringLiteral("PC did not answer"));
    }

    void switchedOffNotificationsAreNotedOncePerLaunch() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        QVERIFY(store.save({sampleProfile(QStringLiteral("PC"), true, true)}, nullptr));
        auto script = std::make_shared<Script>();
        MainWindow window(store, fakeFactories(script));
        const auto log = useFakeSystem(window);
        log->refuse = true;
        const QString line = QStringLiteral("Notifications are switched off for WakeDeck (System Settings ▸ Notifications)");

        std::optional<Outcome> outcome;
        window.startWakeAndConnect();
        QVERIFY(waitForRun(window, outcome));
        QTRY_COMPARE_WITH_TIMEOUT(window.timelineTexts().filter(line).size(), 1, 2000);
        QTest::qWait(50);
        QCOMPARE(window.timelineTexts().filter(line).size(), 1);

        outcome.reset();
        window.startWakeAndConnect();  // a new timeline; the note is not repeated
        QVERIFY(QTest::qWaitFor([&outcome] { return outcome.has_value(); }, 5000));
        QTest::qWait(50);
        QCOMPARE(log->notifications.size(), std::size_t(2));
        QVERIFY2(window.timelineTexts().filter(line).isEmpty(), qPrintable(window.timelineTexts().join(QLatin1Char('\n'))));
    }

    void progressIsShownWhileWaitingAndClearedAtTheEnd() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        Profile profile = sampleProfile(QStringLiteral("PC"), true, false);
        profile.overallTimeoutMs = 1300;
        QVERIFY(store.save({profile}, nullptr));
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut};
        MainWindow window(store, fakeFactories(script));
        const auto log = useFakeSystem(window);
        window.setProgressIntervalMs(50);  // many updates within the run, however loaded the Mac is
        QVERIFY(!window.progressVisible());
        std::optional<Outcome> outcome;
        window.startWakeAndConnect();
        QTRY_VERIFY_WITH_TIMEOUT(window.progressVisible(), 2000);
        QVERIFY2(QRegularExpression(QStringLiteral("^0:0\\d of up to 0:01$")).match(window.progressText()).hasMatch(),
                 qPrintable(window.progressText()));
        QVERIFY(waitForRun(window, outcome));
        QCOMPARE(*outcome, Outcome::TimedOut);
        QVERIFY(!window.progressVisible());
        QVERIFY(window.progressText().isEmpty());
        QCOMPARE(window.progressPermille(), 0);

        QVERIFY2(log->dock.size() >= 3, qPrintable(QString::number(log->dock.size())));  // start, ticks, clear
        QCOMPARE(log->dock.back(), kDockCleared);
        QCOMPARE(countOf(log->dock, kDockCleared), 1);
        double previous = 0.0;
        for (std::size_t i = 0; i + 1 < log->dock.size(); ++i) {
            QVERIFY(log->dock[i] >= previous);
            QVERIFY(log->dock[i] <= wakedeck::app::kMaxWaitFraction);
            previous = log->dock[i];
        }
        QVERIFY2(previous > 0.5, qPrintable(QString::number(previous)));  // the last tick, near the end of 1.3 s
    }

    void progressCountsFromTheStartOfTheRun() {
        // Review finding F: without a learned time the bar runs against the limit,
        // which counts from the start of the run, so it reaches the limit.
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        Profile profile = sampleProfile(QStringLiteral("PC"), true, false);
        profile.overallTimeoutMs = 10000;
        QVERIFY(store.save({profile}, nullptr));
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut};
        script->probeDelayMs = 1100;  // the first check alone takes over a second
        MainWindow window(store, fakeFactories(script));
        useFakeSystem(window);
        window.setProgressIntervalMs(20);
        window.startWakeAndConnect();
        QTRY_VERIFY_WITH_TIMEOUT(window.progressVisible(), 5000);
        // The wake request went out after the first check, but the time shown
        // includes that check.
        QVERIFY2(window.progressText().startsWith(QStringLiteral("0:01 of up to 0:10")), qPrintable(window.progressText()));
        std::optional<Outcome> outcome;
        QTimer::singleShot(0, &window, [&window] { window.cancelRun(); });
        QVERIFY(waitForRun(window, outcome));
    }

    void progressAlsoWhenWaitingWithoutWake() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        QVERIFY(store.save({sampleProfile(QStringLiteral("PC"), false, false)}, nullptr));
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::Refused, ProbeStatus::Refused, ProbeStatus::Ready};
        script->probeDelayMs = 20;
        MainWindow window(store, fakeFactories(script));
        const auto log = useFakeSystem(window);
        bool seen = false;
        QTimer poll;
        QObject::connect(&poll, &QTimer::timeout, &window, [&] { seen = seen || window.progressVisible(); });
        poll.start(5);
        std::optional<Outcome> outcome;
        window.startWakeAndConnect();
        QVERIFY(waitForRun(window, outcome));
        QCOMPARE(*outcome, Outcome::ReadyWithoutWake);
        QVERIFY(seen);
        QVERIFY(!window.progressVisible());
        QVERIFY(log->dock.size() >= 2);
        QVERIFY(log->dock.front() < 0.1);
        QCOMPARE(log->dock.back(), kDockCleared);
    }

    void noProgressWhenTheFirstCheckSucceeds() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        QVERIFY(store.save({sampleProfile(QStringLiteral("PC"), true, true)}, nullptr));
        auto script = std::make_shared<Script>();
        MainWindow window(store, fakeFactories(script));
        const auto log = useFakeSystem(window);
        std::optional<Outcome> outcome;
        window.startWakeAndConnect();
        QVERIFY(waitForRun(window, outcome));
        QCOMPARE(*outcome, Outcome::ReadyWithoutWake);
        QVERIFY(log->dock.empty());
        QVERIFY(!window.progressVisible());
    }

    void closingTheWindowClearsTheProgress() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        Profile profile = sampleProfile(QStringLiteral("PC"), true, false);
        profile.overallTimeoutMs = 10000;
        QVERIFY(store.save({profile}, nullptr));
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut};
        MainWindow window(store, fakeFactories(script));
        const auto log = useFakeSystem(window);
        window.startWakeAndConnect();
        QTRY_VERIFY_WITH_TIMEOUT(window.progressVisible(), 2000);
        QVERIFY2(window.progressText().endsWith(QStringLiteral("of up to 0:10")), qPrintable(window.progressText()));
        window.keepWaiting();  // the limit grows by a minute
        QVERIFY2(window.progressText().endsWith(QStringLiteral("of up to 1:10")), qPrintable(window.progressText()));
        window.close();  // never shown, so the test application is not asked to quit
        QVERIFY(!window.progressVisible());
        QCOMPARE(log->dock.back(), kDockCleared);
        std::optional<Outcome> outcome;
        QTimer::singleShot(0, &window, [&window] { window.cancelRun(); });
        QVERIFY(waitForRun(window, outcome));
        QCOMPARE(countOf(log->dock, kDockCleared), 1);
    }

    void waitProgressFunction() {
        using wakedeck::app::waitProgress;
        QCOMPARE(waitProgress(0, 0, 120000).fraction, 0.0);
        QCOMPARE(waitProgress(0, 0, 120000).text, QStringLiteral("0:00 of up to 2:00"));
        QCOMPARE(waitProgress(12000, 0, 120000).fraction, 0.1);
        QCOMPARE(waitProgress(12000, 0, 120000).text, QStringLiteral("0:12 of up to 2:00"));
        QCOMPARE(waitProgress(114000, 0, 120000).fraction, 0.95);
        QCOMPARE(waitProgress(119999, 0, 120000).fraction, 0.95);  // never full while still waiting
        QCOMPARE(waitProgress(300000, 0, 120000).fraction, 0.95);
        QCOMPARE(waitProgress(300000, 0, 120000).text, QStringLiteral("5:00 of up to 2:00"));
        QCOMPARE(waitProgress(-500, 0, 120000).fraction, 0.0);
        QCOMPARE(waitProgress(5000, 0, 0).fraction, 0.0);
        // With a typical time the fraction runs against it instead of the limit.
        QCOMPARE(waitProgress(15000, 30000, 120000).fraction, 0.5);
        QCOMPARE(waitProgress(12000, 30000, 120000).text, QStringLiteral("usually about 0:30 — 0:12 so far"));
        QCOMPARE(waitProgress(45000, 30000, 120000).fraction, 0.95);
    }

    void diagnoseWritesItsReportWithoutTheSystemServices() {
        using wakedeck::app::diagnoseTarget;
        QCOMPARE(diagnoseTarget({QStringLiteral("WakeDeck")}), std::optional<QString>());
        QCOMPARE(diagnoseTarget({QStringLiteral("WakeDeck"), QStringLiteral("--diagnose"), QStringLiteral("/tmp/x.txt")}),
                 std::optional<QString>(QStringLiteral("/tmp/x.txt")));
        QCOMPARE(diagnoseTarget({QStringLiteral("WakeDeck"), QStringLiteral("--diagnose")}), std::optional<QString>(QString()));

        // On the offscreen platform: nothing of the system's is asked, the
        // picture is still drawn.
        QTemporaryDir dir;
        const QString report = dir.filePath(QStringLiteral("diagnose.txt"));
        QCOMPARE(wakedeck::app::runDiagnose(report), 0);
        QFile file(report);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 8);
        QVERIFY(lines[0].startsWith(QStringLiteral("version=")));
        QCOMPARE(lines[1], QStringLiteral("platform=offscreen"));
        QCOMPARE(lines[2], QStringLiteral("bundleId=none"));
        QCOMPARE(lines[3], QStringLiteral("notifications=unavailable"));
        QCOMPARE(lines[4], QStringLiteral("dockTile=unavailable"));
        // Tailscale: the suite points the tool at a missing path, so it is never run.
        QCOMPARE(lines[5], QStringLiteral("tailscaleCli=not found"));
        QVERIFY2(lines[6].startsWith(QStringLiteral("tailscaleState=error: ")), qPrintable(lines[6]));
        QCOMPARE(lines[7], QStringLiteral("tailscalePeers=0"));
        QFile picture(report + QStringLiteral(".dock.png"));
        QVERIFY(picture.open(QIODevice::ReadOnly));
        QCOMPARE(picture.read(8), QByteArray("\x89PNG\r\n\x1a\n", 8));
        QVERIFY(picture.size() > 1000);

        QVERIFY(wakedeck::app::runDiagnose(dir.filePath(QStringLiteral("no-such-folder/diagnose.txt"))) != 0);
        QVERIFY(wakedeck::app::runDiagnose(QString()) != 0);
    }

    // ---- A profiles file that was not read is never written (review finding A) ----

    void aRefusedStoreIsNeverWritten() {
        QTemporaryDir dir;
        struct Case {
            QString file;
            QByteArray content;
        };
        const std::vector<Case> cases = {
            {QStringLiteral("profiles.json"), R"({"schemaVersion": 3, "profiles": [{"name": "Future PC", "check": {"host": "h"}}]})"},
            {QStringLiteral("single.json"), R"({"schemaVersion": 2, "name": "Exported PC", "check": {"host": "h"}})"},
        };
        for (const Case& c : cases) {
            const QString path = dir.filePath(c.file);
            {
                QFile file(path);
                QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
                file.write(c.content);
            }
            MainWindow window{ProfileStore(path), fakeFactories(std::make_shared<Script>())};
            QVERIFY(window.profiles().empty());
            QVERIFY(!window.storeRefusal().isEmpty());
            QString error;
            QVERIFY(!window.addProfile(sampleProfile(QStringLiteral("New PC"), true, false), &error));
            QVERIFY2(error.contains(QStringLiteral("Nothing was changed")), qPrintable(error));
            QVERIFY(!window.loadExample(&error));
            QVERIFY(!window.updateProfile(QStringLiteral("Future PC"), sampleProfile(QStringLiteral("Other PC"), false, false), &error));
            QVERIFY(!window.removeCurrentProfile(&error));
            QVERIFY(window.profiles().empty());
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), c.content);
            QVERIFY(!QFile::exists(ProfileStore(path).v1BackupPath()));
        }
    }

    // ---- Learned timing and Tailscale in the window ----

    void aSecondRunShowsTheLearnedTime() {
        QTemporaryDir dir;
        const QString storePath = dir.filePath(QStringLiteral("profiles.json"));
        const ProfileStore store(storePath);
        QVERIFY(store.save({sampleProfile(QStringLiteral("Gaming PC"), true, false)}, nullptr));
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut, ProbeStatus::TimedOut, ProbeStatus::Ready};
        MainWindow window(store, fakeFactories(script));
        std::optional<Outcome> outcome;
        window.startWakeAndConnect();
        QVERIFY(waitForRun(window, outcome));
        QCOMPARE(*outcome, Outcome::ReadyAfterWake);
        QVERIFY(window.timelineTexts().filter(QStringLiteral("Last time")).isEmpty());  // nothing learned yet
        const QString historyPath = dir.filePath(QStringLiteral("history.json"));
        QVERIFY(QFile::exists(historyPath));
        QCOMPARE(window.timingHistory().path(), historyPath);
        QCOMPARE(window.timingHistory().entries(QStringLiteral("gaming pc")).size(), std::size_t(1));

        script->probesCreated = 0;
        outcome.reset();
        window.startWakeAndConnect();
        QVERIFY(waitForRun(window, outcome));
        QCOMPARE(*outcome, Outcome::ReadyAfterWake);
        const QStringList texts = window.timelineTexts();
        const QStringList learned = texts.filter(QRegularExpression(QStringLiteral("  Last time: \\d:\\d\\d$")));
        QCOMPARE(learned.size(), 1);
        int sent = -1;
        for (int i = 0; i < texts.size() && sent < 0; ++i) {
            if (texts[i].contains(QStringLiteral("  Wake request sent: "))) sent = i;
        }
        QVERIFY(sent >= 0);
        QCOMPARE(texts.indexOf(learned.first()), sent + 1);  // right after the wake request
        QCOMPARE(window.timingHistory().entries(QStringLiteral("Gaming PC")).size(), std::size_t(2));

        // A run that is ready at once, and one that times out, record nothing.
        script->statuses = {ProbeStatus::Ready};
        script->probesCreated = 0;
        outcome.reset();
        window.startWakeAndConnect();
        QVERIFY(waitForRun(window, outcome));
        QCOMPARE(*outcome, Outcome::ReadyWithoutWake);
        QCOMPARE(window.timingHistory().entries(QStringLiteral("Gaming PC")).size(), std::size_t(2));
    }

    void theLearnedTimeFeedsTheProgress() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        Profile profile = sampleProfile(QStringLiteral("PC"), true, false);
        profile.overallTimeoutMs = 10000;
        QVERIFY(store.save({profile}, nullptr));
        const TimingHistory history(dir.filePath(QStringLiteral("history.json")));
        for (int i = 0; i < 3; ++i) QVERIFY(history.record(QStringLiteral("PC"), 30000));
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut};
        MainWindow window(store, fakeFactories(script));
        const auto log = useFakeSystem(window);
        window.startWakeAndConnect();
        QTRY_VERIFY_WITH_TIMEOUT(window.progressVisible(), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(!window.timelineTexts().filter(QStringLiteral("Usually ready")).isEmpty(), 2000);
        QVERIFY2(window.timelineTexts().filter(QStringLiteral("Usually ready about 0:30 after the wake request (last 3 runs)")).size() == 1,
                 qPrintable(window.timelineTexts().join(QLatin1Char('\n'))));
        QVERIFY2(window.progressText().startsWith(QStringLiteral("usually about 0:30 — 0:0")), qPrintable(window.progressText()));
        QVERIFY(!log->dock.empty());
        std::optional<Outcome> outcome;
        QTimer::singleShot(0, &window, [&window] { window.cancelRun(); });
        QVERIFY(waitForRun(window, outcome));
        QCOMPARE(history.entries(QStringLiteral("PC")).size(), std::size_t(3));  // a cancelled run adds nothing
    }

    void deletingAndRenamingCarryTheHistory() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        QVERIFY(store.save({sampleProfile(QStringLiteral("Gaming PC"), true, false), sampleProfile(QStringLiteral("Office PC"), true, false)},
                           nullptr));
        const TimingHistory history(dir.filePath(QStringLiteral("history.json")));
        QVERIFY(history.record(QStringLiteral("Gaming PC"), 20000));
        QVERIFY(history.record(QStringLiteral("Office PC"), 40000));
        MainWindow window(store, fakeFactories(std::make_shared<Script>()));
        window.selectProfile(QStringLiteral("Gaming PC"));
        Profile renamed = *window.currentProfile();
        renamed.name = QStringLiteral("Den PC");
        QString error;
        QVERIFY2(window.updateProfile(QStringLiteral("Gaming PC"), renamed, &error), qPrintable(error));
        QVERIFY(findProfile(window.profiles(), QStringLiteral("Gaming PC")) == nullptr);
        QVERIFY(history.entries(QStringLiteral("Gaming PC")).empty());
        QCOMPARE(history.learned(QStringLiteral("Den PC"))->lastMs, qint64(20000));
        // An edit that keeps the name keeps the history where it is.
        Profile same = *window.currentProfile();
        same.notes = QStringLiteral("edited");
        QVERIFY(window.updateProfile(QStringLiteral("den pc"), same, &error));
        QCOMPARE(history.entries(QStringLiteral("Den PC")).size(), std::size_t(1));

        window.selectProfile(QStringLiteral("Office PC"));
        QVERIFY(window.removeCurrentProfile(&error));
        QVERIFY(history.entries(QStringLiteral("Office PC")).empty());
        QCOMPARE(history.entries(QStringLiteral("Den PC")).size(), std::size_t(1));
    }

    void tailscaleOffEndsTheRunAtOnce() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        Profile profile = sampleProfile(QStringLiteral("Living Room PC"), true, false);
        profile.check.host = QStringLiteral("100.64.0.10");
        QVERIFY(store.save({profile}, nullptr));
        auto script = std::make_shared<Script>();
        script->tailscaleState = QStringLiteral("Stopped");
        MainWindow window(store, fakeFactories(script));
        const auto log = useFakeSystem(window);
        std::optional<Outcome> outcome;
        window.startWakeAndConnect();
        QVERIFY(waitForRun(window, outcome));
        QCOMPARE(*outcome, Outcome::TailscaleOff);
        QCOMPARE(script->probesCreated, 0);
        QCOMPARE(script->wakersCreated, 0);
        QCOMPARE(window.serviceStatusText(), QStringLiteral("not checked: Tailscale is off on this Mac"));
        QCOMPARE(window.wakeStatusText(), QStringLiteral("not sent"));
        QVERIFY(window.explanationVisible());
        QVERIFY2(window.explanationText().contains(QStringLiteral("Turn Tailscale on and try again")), qPrintable(window.explanationText()));
        QPushButton* tryAgain = nullptr;
        for (QPushButton* button : window.findChildren<QPushButton*>()) {
            if (button->text() == QStringLiteral("Try again")) tryAgain = button;
        }
        QVERIFY(tryAgain != nullptr);
        QVERIFY(!tryAgain->isHidden());
        QVERIFY(!window.locateOffered());
        QVERIFY(window.timelineTexts().filter(QStringLiteral("Tailscale is not connected on this Mac (state: Stopped)")).size() == 1);
        QCOMPARE(log->notifications.size(), std::size_t(1));
        QCOMPARE(log->notifications.front().first, QStringLiteral("Living Room PC cannot be reached"));
        QVERIFY(log->notifications.front().second.startsWith(QStringLiteral("Tailscale is not connected on this Mac")));
        QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("history.json"))));

        // Running, with the computer shown online: the run goes on and says so.
        script->tailscaleState = QStringLiteral("Running");
        outcome.reset();
        window.startWakeAndConnect();
        QVERIFY(waitForRun(window, outcome));
        QCOMPARE(*outcome, Outcome::ReadyWithoutWake);
        QVERIFY2(window.timelineTexts().filter(QStringLiteral("Tailscale is connected; it shows living-room-pc online")).size() == 1,
                 qPrintable(window.timelineTexts().join(QLatin1Char('\n'))));
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
    // No test may run the real Tailscale tool: a missing path switches it off.
    qputenv("WAKEDECK_TAILSCALE_CLI", QByteArrayLiteral("/nonexistent/wakedeck-test-tailscale"));
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
