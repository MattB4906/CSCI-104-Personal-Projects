// MainWindow: the WakeDeck window. Saved computers on the left; on the right the
// selected computer's settings, the Wake & Connect button, the two facts that
// matter ("wake request" and "service") and the timeline of the current run.
// Everything network-related goes through the engine's Session, probes, waker
// and launcher; the window only displays what they report.
#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <functional>
#include <vector>

#include "engine/Profile.h"
#include "engine/ProfileStore.h"
#include "engine/Session.h"

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QWidget;

namespace wakedeck::app {

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    // Produces the factories for one run. Tests hand in fakes; the default uses
    // the real probes and waker and the profile's own client.
    using FactoriesFor = std::function<SessionFactories(const Profile&)>;

    explicit MainWindow(ProfileStore store, FactoriesFor factoriesFor = defaultFactories(), QWidget* parent = nullptr);
    static FactoriesFor defaultFactories();

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

    // Actions, also bound to the buttons and menus.
    bool addProfile(Profile profile, QString* error = nullptr);  // adds or replaces, then saves
    bool removeCurrentProfile(QString* error = nullptr);
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

    ProfileStore store_;
    FactoriesFor factoriesFor_;
    std::vector<Profile> profiles_;
    Session* session_ = nullptr;  // child of this window while a run is active
    bool stepInFlight_ = false;   // a single check, wake or open is in progress
    QString runProfileName_;      // which computer the displayed run belongs to
    QString pendingWarnings_;     // load warnings to show once the window is visible

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
};

}  // namespace wakedeck::app
