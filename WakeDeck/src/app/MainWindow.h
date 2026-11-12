// MainWindow: the WakeDeck window. Saved computers on the left; on the right the
// selected computer's settings, the Wake & Connect button, the two facts that
// matter ("wake request" and "service") and the timeline of the current run.
// Everything network-related goes through the engine's Session, probes, waker
// and launcher; the window only displays what they report.
#pragma once

#include <QElapsedTimer>
#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "SystemServices.h"
#include "engine/Profile.h"
#include "engine/ProfileStore.h"
#include "engine/Session.h"
#include "engine/TimingHistory.h"

class QAction;
class QCloseEvent;
class QFormLayout;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTimer;
class QWidget;

namespace wakedeck {
class ProcessWatcher;
}

namespace wakedeck::app {

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    // Produces the factories for one run. Tests hand in fakes; the default uses
    // the real probes and waker and the profile's own client.
    using FactoriesFor = std::function<SessionFactories(const Profile&)>;

    explicit MainWindow(ProfileStore store, FactoriesFor factoriesFor = defaultFactories(), QWidget* parent = nullptr);
    static FactoriesFor defaultFactories();

    // Called when the client WakeDeck launched has closed and "Quit When Client
    // Closes" is on. The default asks the application to quit on the next turn
    // of the event loop; tests replace it so the test runner keeps running.
    using QuitHook = std::function<void()>;
    void setQuitHook(QuitHook hook) { quitHook_ = std::move(hook); }

    // Adjusts the configuration of each Wake & Connect run before it starts.
    // Tests use it for millisecond intervals a profile cannot hold (resends).
    using SessionConfigHook = std::function<void(SessionConfig&)>;
    void setSessionConfigHook(SessionConfigHook hook) { sessionConfigHook_ = std::move(hook); }

    // "Quit When Client Closes" (Run menu): on by default, remembered with
    // QSettings under kQuitWhenClientClosesKey for the application's
    // organization and name. Turning it off stops a current watch.
    static constexpr const char* kQuitWhenClientClosesKey = "quitWhenClientCloses";
    bool quitWhenClientCloses() const;
    void setQuitWhenClientCloses(bool on);
    bool isWatchingClient() const;    // WakeDeck will quit when the watched client closes
    qint64 watchedClientPid() const;  // 0 when not watching
    qint64 ownedClientPid() const;    // the live client process WakeDeck itself started, or 0

    // The "ready" notification and the Dock progress. The defaults are the
    // system's on macOS (cocoa) and do nothing elsewhere; tests hand in fakes.
    // The predicate says whether WakeDeck is the active application (default:
    // QGuiApplication::applicationState() == Qt::ApplicationActive).
    ~MainWindow() override;
    void setNotifier(std::unique_ptr<Notifier> notifier);
    void setDockProgress(std::unique_ptr<DockProgress> dock);
    using ActivePredicate = std::function<bool()>;
    void setAppActivePredicate(ActivePredicate isActive) { isAppActive_ = std::move(isActive); }

    // "Notify When Ready" (Run menu): on by default, remembered with QSettings
    // under kNotifyWhenReadyKey. When a Wake & Connect run ends (ready, timed
    // out, wake failed, client not opened) while WakeDeck is not the active
    // application, one notification and a Dock bounce say so.
    static constexpr const char* kNotifyWhenReadyKey = "notifyWhenReady";
    bool notifyWhenReady() const;
    void setNotifyWhenReady(bool on);

    // The wait progress in the "This run" box (and on the Dock icon).
    bool progressVisible() const;
    QString progressText() const;
    int progressPermille() const;  // the bar's value, 0 to 1000

    // State, for tests and scripting.
    const std::vector<Profile>& profiles() const { return profiles_; }
    const Profile* currentProfile() const;
    // True while a Wake & Connect run or a single step (check, wake, open) is in flight.
    bool isRunning() const { return session_ != nullptr || stepInFlight_; }
    int listCount() const;
    QString pendingWarningText() const { return pendingWarnings_; }
    QStringList timelineTexts() const;
    QString wakeStatusText() const;
    QString serviceStatusText() const;
    QString explanationText() const;
    bool explanationVisible() const;
    bool locateOffered() const;
    QString wakeDetailsText() const;  // the selected computer's "Wake:" line in the details pane
    // How long each computer took from the wake request to ready, in the file
    // beside the profiles store (TimingHistory::pathBeside).
    TimingHistory timingHistory() const;

    // Actions, also bound to the buttons and menus.
    bool addProfile(Profile profile, QString* error = nullptr);  // adds or replaces, then saves
    bool removeCurrentProfile(QString* error = nullptr);
    // Saves an edited computer that was called `oldName`; a new name replaces any
    // computer already called that, and the learned timing moves with it.
    bool updateProfile(const QString& oldName, Profile edited, QString* error = nullptr);
    bool loadExample(QString* error = nullptr);
    void selectProfile(const QString& name);
    void startWakeAndConnect();
    void startCheckOnly();
    void startWakeOnly();
    void startOpenClient();
    void keepWaiting();
    void cancelRun();

signals:
    void runFinished(wakedeck::Outcome outcome);
    void checkFinished(bool ready);
    void stepFinished();

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    void buildMenus();
    void reloadFromStore();
    void refreshList();
    void refreshDetails();
    void setRunning(bool running);
    void beginStep(const Profile& profile);
    void endStep();
    void resetRunDisplay();
    const Profile* runProfile() const;  // the profile the displayed run belongs to
    void appendTimeline(const TimelineEvent& event);
    void onSessionEvent(const TimelineEvent& event);
    void onSessionState(SessionState state);
    void onSessionFinished(Outcome outcome, const QString& summary);
    void watchClient(qint64 launchedPid, qint64 runningPid, const QString& client);  // after a successful launch
    void stopWatchingClient();
    void onClientExited(qint64 pid);
    void onQuitOptionToggled(bool on);
    void note(const QString& text, EventKind kind = EventKind::Info);  // a timeline line stamped now
    void showExplanation(const QString& text, bool offerLocate);
    void addNew();
    void editCurrent();
    void deleteCurrent();
    void importProfile();
    void exportCurrent();
    void locateClient();
    void showHowWakingWorks();
    void showAbout();
    bool saveProfiles(QString* error);
    void showError(const QString& title, const QString& text);
    void notifyRunEnded(Outcome outcome, const QString& summary);  // the "ready" notification, when it applies
    void onNotificationsRefused();
    void startProgress();
    void updateProgress();
    void stopProgress();
    // How long this computer usually takes to answer after the wake request, in
    // ms, learned from earlier runs; 0 = unknown, which makes the progress run
    // against the time limit.
    qint64 typicalWaitMs(const Profile& profile) const;
    void forgetHistory(const QString& name);                      // a computer was deleted
    void moveHistory(const QString& from, const QString& to);     // a computer was renamed

    ProfileStore store_;
    FactoriesFor factoriesFor_;
    std::vector<Profile> profiles_;
    Session* session_ = nullptr;  // child of this window while a run is active
    bool stepInFlight_ = false;   // a single check, wake or open is in progress
    QString runProfileName_;      // which computer the displayed run belongs to
    int wakeSendCount_ = 0;       // wake requests sent in the displayed run, resends included
    QString pendingWarnings_;     // load warnings to show once the window is visible
    // Follows the client process WakeDeck itself started last, for as long as it
    // lives, so that a later launch that only finds it running can watch it again.
    ProcessWatcher* clientWatcher_ = nullptr;
    bool quitArmed_ = false;      // its exit quits WakeDeck (option on, no newer run or step)
    QString watchedClient_;       // its description, for the timeline
    QuitHook quitHook_;
    SessionConfigHook sessionConfigHook_;
    QAction* quitWhenClientClosesAction_ = nullptr;

    QListWidget* list_ = nullptr;
    QWidget* details_ = nullptr;
    QLabel* emptyHint_ = nullptr;
    QLabel* nameLabel_ = nullptr;
    QLabel* checkLabel_ = nullptr;
    QLabel* wakeLabel_ = nullptr;
    QLabel* clientLabel_ = nullptr;
    QLabel* notesLabel_ = nullptr;
    QPushButton* wakeConnectButton_ = nullptr;
    QPushButton* checkButton_ = nullptr;
    QPushButton* wakeOnlyButton_ = nullptr;
    QPushButton* openClientButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    QPushButton* keepWaitingButton_ = nullptr;
    QPushButton* editButton_ = nullptr;
    QPushButton* deleteButton_ = nullptr;
    QLabel* wakeStatus_ = nullptr;
    QLabel* serviceStatus_ = nullptr;
    QListWidget* timeline_ = nullptr;
    QWidget* explanationPanel_ = nullptr;
    QPlainTextEdit* explanation_ = nullptr;
    QPushButton* locateButton_ = nullptr;
    QPushButton* tryAgainButton_ = nullptr;

    // Notification and progress.
    std::unique_ptr<Notifier> notifier_;
    std::unique_ptr<DockProgress> dock_;
    ActivePredicate isAppActive_;
    QAction* notifyWhenReadyAction_ = nullptr;
    bool refusalNoted_ = false;      // the "switched off" line is shown once per launch (one window per launch)
    QFormLayout* factsLayout_ = nullptr;
    QWidget* progressRow_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QLabel* progressLabel_ = nullptr;
    QTimer* progressTimer_ = nullptr;
    QElapsedTimer waitClock_;        // valid while the progress is shown
    bool progressStartedThisRun_ = false;  // shown once per run; closing the window ends it for good
    qint64 progressTypicalMs_ = 0;
    qint64 progressLimitMs_ = 0;     // the run's limit, plus "Keep waiting"
};

}  // namespace wakedeck::app
