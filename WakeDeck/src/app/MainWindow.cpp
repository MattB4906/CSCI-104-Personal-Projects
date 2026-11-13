#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QShowEvent>
#include <QSplitter>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <utility>

#include "ProfileEditorDialog.h"
#include "WaitProgress.h"
#include "engine/ClientLauncher.h"
#include "engine/Explain.h"
#include "engine/ProcessWatcher.h"
#include "engine/QtWaker.h"
#include "engine/ServiceProbe.h"

namespace wakedeck::app {
namespace {

constexpr const char* kExampleResource = ":/examples/example-profile.json";

QString stamp(const QDateTime& at) {
    return at.toString(QStringLiteral("HH:mm:ss"));
}

// The first line of a run's text as one sentence, for a notification.
QString oneSentence(const QString& text) {
    QString line = text.section(QLatin1Char('\n'), 0, 0).trimmed();
    if (!line.isEmpty() && !line.endsWith(QLatin1Char('.'))) line += QLatin1Char('.');
    return line;
}

}  // namespace

MainWindow::FactoriesFor MainWindow::defaultFactories() {
    return [](const Profile& profile) {
        SessionFactories factories;
        factories.launcher = makeLauncherFactory(profile.client, profile.check.host);
        return factories;
    };
}

MainWindow::MainWindow(ProfileStore store, FactoriesFor factoriesFor, QWidget* parent)
    : QMainWindow(parent), store_(std::move(store)), factoriesFor_(std::move(factoriesFor)) {
    quitHook_ = [] {
        // Queued, so the timeline line and the current signal finish first.
        QTimer::singleShot(0, QCoreApplication::instance(), [] { QCoreApplication::quit(); });
    };
    setNotifier(makeDefaultNotifier());
    setDockProgress(makeDefaultDockProgress());
    isAppActive_ = [] { return QGuiApplication::applicationState() == Qt::ApplicationActive; };
    clientWatcher_ = new ProcessWatcher(this);
    connect(clientWatcher_, &ProcessWatcher::exited, this, &MainWindow::onClientExited);
    setWindowTitle(QStringLiteral("WakeDeck"));
    resize(960, 640);
    buildUi();
    buildMenus();
    reloadFromStore();
}

// ---- Building the window ----------------------------------------------------

void MainWindow::buildUi() {
    auto* splitter = new QSplitter(this);
    setCentralWidget(splitter);

    // Left: the computers.
    auto* left = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(left);
    auto* computersTitle = new QLabel(QStringLiteral("Computers"), left);
    QFont titleFont = computersTitle->font();
    titleFont.setBold(true);
    computersTitle->setFont(titleFont);
    leftLayout->addWidget(computersTitle);
    list_ = new QListWidget(left);
    list_->setObjectName(QStringLiteral("profileList"));
    leftLayout->addWidget(list_, 1);
    auto* leftButtons = new QHBoxLayout;
    auto* addButton = new QPushButton(QStringLiteral("Add…"), left);
    addButton->setObjectName(QStringLiteral("addButton"));
    editButton_ = new QPushButton(QStringLiteral("Edit…"), left);
    deleteButton_ = new QPushButton(QStringLiteral("Delete"), left);
    leftButtons->addWidget(addButton);
    leftButtons->addWidget(editButton_);
    leftButtons->addWidget(deleteButton_);
    leftLayout->addLayout(leftButtons);
    connect(addButton, &QPushButton::clicked, this, &MainWindow::addNew);
    connect(editButton_, &QPushButton::clicked, this, &MainWindow::editCurrent);
    connect(deleteButton_, &QPushButton::clicked, this, &MainWindow::deleteCurrent);
    connect(list_, &QListWidget::currentRowChanged, this, [this](int) {
        // The timeline and the facts belong to one computer; selecting another
        // one must not leave them, or the Locate button, in place.
        const Profile* selected = currentProfile();
        if (!isRunning() && (!selected || !sameProfileName(selected->name, runProfileName_))) resetRunDisplay();
        refreshDetails();
    });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { startWakeAndConnect(); });

    // Right: the selected computer and the run.
    auto* right = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(right);

    emptyHint_ = new QLabel(QStringLiteral("No computers yet.\n\nClick Add… to describe one: how to wake it, which service to "
                                           "check, and which client to open. Or choose File ▸ Load Example to see a "
                                           "complete, fictional profile."),
                            right);
    emptyHint_->setWordWrap(true);
    emptyHint_->setAlignment(Qt::AlignCenter);
    rightLayout->addWidget(emptyHint_, 1);

    details_ = new QWidget(right);
    auto* detailsLayout = new QVBoxLayout(details_);
    detailsLayout->setContentsMargins(0, 0, 0, 0);

    nameLabel_ = new QLabel(details_);
    QFont nameFont = nameLabel_->font();
    nameFont.setPointSize(nameFont.pointSize() + 6);
    nameFont.setBold(true);
    nameLabel_->setFont(nameFont);
    detailsLayout->addWidget(nameLabel_);

    auto* summary = new QFormLayout;
    checkLabel_ = new QLabel(details_);
    wakeLabel_ = new QLabel(details_);
    clientLabel_ = new QLabel(details_);
    notesLabel_ = new QLabel(details_);
    notesLabel_->setWordWrap(true);
    summary->addRow(QStringLiteral("Check:"), checkLabel_);
    summary->addRow(QStringLiteral("Wake:"), wakeLabel_);
    summary->addRow(QStringLiteral("Client:"), clientLabel_);
    summary->addRow(QStringLiteral("Notes:"), notesLabel_);
    detailsLayout->addLayout(summary);

    auto* actions = new QHBoxLayout;
    wakeConnectButton_ = new QPushButton(QStringLiteral("Wake && Connect"), details_);
    wakeConnectButton_->setObjectName(QStringLiteral("wakeConnectButton"));
    wakeConnectButton_->setDefault(true);
    QFont bigFont = wakeConnectButton_->font();
    bigFont.setPointSize(bigFont.pointSize() + 2);
    bigFont.setBold(true);
    wakeConnectButton_->setFont(bigFont);
    wakeConnectButton_->setMinimumHeight(40);
    checkButton_ = new QPushButton(QStringLiteral("Check now"), details_);
    wakeOnlyButton_ = new QPushButton(QStringLiteral("Wake only"), details_);
    openClientButton_ = new QPushButton(QStringLiteral("Open client"), details_);
    cancelButton_ = new QPushButton(QStringLiteral("Cancel"), details_);
    keepWaitingButton_ = new QPushButton(QStringLiteral("Keep waiting 1:00"), details_);
    actions->addWidget(wakeConnectButton_, 2);
    actions->addWidget(checkButton_);
    actions->addWidget(wakeOnlyButton_);
    actions->addWidget(openClientButton_);
    actions->addWidget(keepWaitingButton_);
    actions->addWidget(cancelButton_);
    detailsLayout->addLayout(actions);
    connect(wakeConnectButton_, &QPushButton::clicked, this, &MainWindow::startWakeAndConnect);
    connect(checkButton_, &QPushButton::clicked, this, &MainWindow::startCheckOnly);
    connect(wakeOnlyButton_, &QPushButton::clicked, this, &MainWindow::startWakeOnly);
    connect(openClientButton_, &QPushButton::clicked, this, &MainWindow::startOpenClient);
    connect(keepWaitingButton_, &QPushButton::clicked, this, &MainWindow::keepWaiting);
    connect(cancelButton_, &QPushButton::clicked, this, &MainWindow::cancelRun);

    // The two facts, always side by side so they are never confused.
    auto* facts = new QGroupBox(QStringLiteral("This run"), details_);
    auto* factsLayout = new QFormLayout(facts);
    wakeStatus_ = new QLabel(facts);
    wakeStatus_->setObjectName(QStringLiteral("wakeStatus"));
    serviceStatus_ = new QLabel(facts);
    serviceStatus_->setObjectName(QStringLiteral("serviceStatus"));
    serviceStatus_->setWordWrap(true);
    factsLayout->addRow(QStringLiteral("Wake request:"), wakeStatus_);
    factsLayout->addRow(QStringLiteral("Service:"), serviceStatus_);
    // The wait progress: shown while the run is waking or waiting for the service.
    factsLayout_ = factsLayout;
    progressRow_ = new QWidget(facts);
    auto* progressLayout = new QHBoxLayout(progressRow_);
    progressLayout->setContentsMargins(0, 0, 0, 0);
    progressBar_ = new QProgressBar(progressRow_);
    progressBar_->setObjectName(QStringLiteral("waitProgress"));
    progressBar_->setRange(0, 1000);
    progressBar_->setTextVisible(false);
    progressLabel_ = new QLabel(progressRow_);
    progressLabel_->setObjectName(QStringLiteral("waitProgressText"));
    progressLayout->addWidget(progressBar_, 1);
    progressLayout->addWidget(progressLabel_);
    factsLayout->addRow(QStringLiteral("Waiting:"), progressRow_);
    factsLayout->setRowVisible(progressRow_, false);
    progressTimer_ = new QTimer(this);
    progressTimer_->setInterval(1000);
    connect(progressTimer_, &QTimer::timeout, this, &MainWindow::updateProgress);
    detailsLayout->addWidget(facts);

    timeline_ = new QListWidget(details_);
    timeline_->setObjectName(QStringLiteral("timeline"));
    timeline_->setSelectionMode(QAbstractItemView::NoSelection);
    detailsLayout->addWidget(timeline_, 1);

    explanationPanel_ = new QFrame(details_);
    auto* explanationLayout = new QVBoxLayout(explanationPanel_);
    explanationLayout->setContentsMargins(0, 0, 0, 0);
    explanation_ = new QPlainTextEdit(explanationPanel_);
    explanation_->setObjectName(QStringLiteral("explanation"));
    explanation_->setReadOnly(true);
    explanation_->setMaximumHeight(170);
    explanationLayout->addWidget(explanation_);
    auto* explanationButtons = new QHBoxLayout;
    tryAgainButton_ = new QPushButton(QStringLiteral("Try again"), explanationPanel_);
    locateButton_ = new QPushButton(QStringLiteral("Locate client…"), explanationPanel_);
    auto* copyButton = new QPushButton(QStringLiteral("Copy details"), explanationPanel_);
    explanationButtons->addWidget(tryAgainButton_);
    explanationButtons->addWidget(locateButton_);
    explanationButtons->addWidget(copyButton);
    explanationButtons->addStretch();
    explanationLayout->addLayout(explanationButtons);
    detailsLayout->addWidget(explanationPanel_);
    connect(tryAgainButton_, &QPushButton::clicked, this, &MainWindow::startWakeAndConnect);
    connect(locateButton_, &QPushButton::clicked, this, &MainWindow::locateClient);
    connect(copyButton, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(timelineTexts().join(QLatin1Char('\n')) + QStringLiteral("\n\n") + explanation_->toPlainText());
    });

    rightLayout->addWidget(details_, 1);
    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);

    resetRunDisplay();
    setRunning(false);
}

void MainWindow::buildMenus() {
    QMenu* file = menuBar()->addMenu(QStringLiteral("&File"));
    QAction* add = file->addAction(QStringLiteral("&New Computer…"), this, &MainWindow::addNew);
    add->setShortcut(QKeySequence::New);
    file->addAction(QStringLiteral("&Edit Computer…"), this, &MainWindow::editCurrent);
    file->addAction(QStringLiteral("&Delete Computer"), this, &MainWindow::deleteCurrent);
    file->addSeparator();
    file->addAction(QStringLiteral("&Import Profile…"), this, &MainWindow::importProfile);
    file->addAction(QStringLiteral("E&xport Profile…"), this, &MainWindow::exportCurrent);
    file->addAction(QStringLiteral("Load E&xample"), this, [this] {
        QString error;
        if (!loadExample(&error)) showError(QStringLiteral("Could not load the example"), error);
    });

    QMenu* run = menuBar()->addMenu(QStringLiteral("&Run"));
    QAction* wakeConnect = run->addAction(QStringLiteral("&Wake && Connect"), this, &MainWindow::startWakeAndConnect);
    wakeConnect->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
    run->addAction(QStringLiteral("&Check Now"), this, &MainWindow::startCheckOnly);
    run->addAction(QStringLiteral("Wake &Only"), this, &MainWindow::startWakeOnly);
    run->addAction(QStringLiteral("&Open Client"), this, &MainWindow::startOpenClient);
    run->addSeparator();
    run->addAction(QStringLiteral("Cancel"), this, &MainWindow::cancelRun);
    run->addSeparator();
    quitWhenClientClosesAction_ = run->addAction(QStringLiteral("&Quit When Client Closes"));
    quitWhenClientClosesAction_->setObjectName(QStringLiteral("quitWhenClientClosesAction"));
    quitWhenClientClosesAction_->setCheckable(true);
    quitWhenClientClosesAction_->setChecked(QSettings().value(QLatin1String(kQuitWhenClientClosesKey), true).toBool());
    quitWhenClientClosesAction_->setToolTip(
        QStringLiteral("When the client WakeDeck opened closes, for example because the stream ended, WakeDeck quits too. "
                       "A client you opened yourself is never watched."));
    connect(quitWhenClientClosesAction_, &QAction::toggled, this, &MainWindow::onQuitOptionToggled);
    notifyWhenReadyAction_ = run->addAction(QStringLiteral("&Notify When Ready"));
    notifyWhenReadyAction_->setObjectName(QStringLiteral("notifyWhenReadyAction"));
    notifyWhenReadyAction_->setCheckable(true);
    notifyWhenReadyAction_->setChecked(QSettings().value(QLatin1String(kNotifyWhenReadyKey), true).toBool());
    notifyWhenReadyAction_->setToolTip(
        QStringLiteral("When a Wake & Connect run ends while you are in another application, a notification says how it "
                       "ended and the Dock icon bounces."));
    connect(notifyWhenReadyAction_, &QAction::toggled, this,
            [](bool on) { QSettings().setValue(QLatin1String(kNotifyWhenReadyKey), on); });

    QMenu* help = menuBar()->addMenu(QStringLiteral("&Help"));
    help->addAction(QStringLiteral("How Waking &Works"), this, &MainWindow::showHowWakingWorks);
    help->addAction(QStringLiteral("&About WakeDeck"), this, &MainWindow::showAbout);
}

// ---- Profiles ---------------------------------------------------------------

void MainWindow::reloadFromStore() {
    const ProfileStore::LoadResult loaded = store_.load();
    QString warning;
    if (!loaded.ok) {
        // The file is still there and was not read: it must not be replaced.
        warning = loaded.error;
        storeRefusal_ = loaded.error;
        profiles_.clear();
    } else {
        storeRefusal_.clear();
        profiles_ = loaded.profiles;
        warning = loaded.warning;
    }
    if (!warning.isEmpty()) {
        if (isVisible()) {
            showError(QStringLiteral("About your saved computers"), warning);
        } else {
            pendingWarnings_ = warning;  // shown as soon as the window appears, see showEvent
        }
    }
    refreshList();
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    if (pendingWarnings_.isEmpty()) return;
    const QString text = pendingWarnings_;
    pendingWarnings_.clear();
    refreshDetails();
    QTimer::singleShot(0, this, [this, text] { QMessageBox::warning(this, QStringLiteral("About your saved computers"), text); });
}

void MainWindow::refreshList() {
    const QString previous = list_->currentItem() ? list_->currentItem()->text() : QString();
    list_->clear();
    for (const Profile& profile : profiles_) list_->addItem(profile.name);
    int row = 0;
    for (int i = 0; i < list_->count(); ++i) {
        if (sameProfileName(list_->item(i)->text(), previous)) row = i;
    }
    if (list_->count() > 0) list_->setCurrentRow(row);
    refreshDetails();
}

void MainWindow::refreshDetails() {
    const Profile* profile = currentProfile();
    const bool have = profile != nullptr;
    details_->setVisible(have);
    emptyHint_->setVisible(!have);
    if (!have) {
        QString hint = QStringLiteral("No computers yet.\n\nClick Add… to describe one: how to wake it, which service to "
                                      "check, and which client to open. Or choose File ▸ Load Example to see a "
                                      "complete, fictional profile.");
        if (!pendingWarnings_.isEmpty()) hint = pendingWarnings_ + QStringLiteral("\n\n") + hint;
        emptyHint_->setText(hint);
        setRunning(isRunning());
        return;
    }
    nameLabel_->setText(profile->name);
    checkLabel_->setText(QStringLiteral("%1, every %2 s for up to %3")
                             .arg(describe(profile->check), QString::number(profile->pollIntervalMs / 1000.0),
                                  formatDuration(profile->overallTimeoutMs)));
    wakeLabel_->setText(profile->wake ? QStringLiteral("%1; %2").arg(describe(*profile->wake), describeResend(*profile->wake))
                                      : QStringLiteral("none configured (always-on computer)"));
    clientLabel_->setText(describe(profile->client));
    notesLabel_->setText(profile->notes.isEmpty() ? QStringLiteral("–") : profile->notes);
    setRunning(isRunning());  // one place decides which buttons are enabled
}

const Profile* MainWindow::currentProfile() const {
    const int row = list_->currentRow();
    if (row < 0 || row >= static_cast<int>(profiles_.size())) return nullptr;
    return &profiles_[static_cast<std::size_t>(row)];
}

const Profile* MainWindow::runProfile() const {
    return findProfile(profiles_, runProfileName_);
}

int MainWindow::listCount() const {
    return list_->count();
}

bool MainWindow::storeWritable(QString* error) {
    if (storeRefusal_.isEmpty()) return true;
    const QString why = QStringLiteral("Nothing was changed, because the saved computers could not be read: %1").arg(storeRefusal_);
    if (error) *error = why;
    showError(QStringLiteral("Could not save the computers"), why);
    return false;
}

bool MainWindow::saveProfiles(QString* error) {
    QString why;
    if (!store_.save(profiles_, &why)) {
        if (error) *error = why;
        showError(QStringLiteral("Could not save the computers"), why);
        reloadFromStore();  // memory and list go back to what the file really holds
        return false;
    }
    return true;
}

bool MainWindow::addProfile(Profile profile, QString* error) {
    if (!storeWritable(error)) return false;
    const QString name = profile.name;
    upsertProfile(profiles_, std::move(profile));
    if (!saveProfiles(error)) return false;
    refreshList();
    selectProfile(name);
    return true;
}

bool MainWindow::removeCurrentProfile(QString* error) {
    const Profile* profile = currentProfile();
    if (!profile || !storeWritable(error)) return false;
    const QString name = profile->name;  // a copy: the element itself is about to go
    removeProfile(profiles_, name);
    if (!saveProfiles(error)) return false;
    forgetHistory(name);
    refreshList();
    return true;
}

bool MainWindow::loadExample(QString* error) {
    if (!storeWritable(error)) return false;
    QString why;
    const auto example = ProfileStore::readProfileFile(QLatin1String(kExampleResource), &why);
    if (!example) {
        if (error) *error = why;
        return false;
    }
    return addProfile(*example, error);
}

void MainWindow::selectProfile(const QString& name) {
    for (int i = 0; i < list_->count(); ++i) {
        if (sameProfileName(list_->item(i)->text(), name)) {
            list_->setCurrentRow(i);
            return;
        }
    }
}

void MainWindow::addNew() {
    if (!storeWritable(nullptr)) return;
    Profile blank;
    ProfileEditorDialog dialog(blank, true, this);
    if (dialog.exec() == QDialog::Accepted) addProfile(dialog.result());
}

void MainWindow::editCurrent() {
    const Profile* profile = currentProfile();
    if (!profile || isRunning()) return;
    const QString oldName = profile->name;
    ProfileEditorDialog dialog(*profile, false, this);
    if (dialog.exec() != QDialog::Accepted) return;
    Profile edited = dialog.result();
    if (!sameProfileName(edited.name, oldName)) {
        if (findProfile(profiles_, edited.name)) {
            const auto answer = QMessageBox::question(
                this, QStringLiteral("Replace computer"),
                QStringLiteral("Another computer is already called \"%1\". Replace it?").arg(edited.name));
            if (answer != QMessageBox::Yes) return;
        }
    }
    updateProfile(oldName, std::move(edited));
}

bool MainWindow::updateProfile(const QString& oldName, Profile edited, QString* error) {
    if (!storeWritable(error)) return false;
    const QString newName = edited.name;
    const bool renamed = !sameProfileName(newName, oldName);
    if (renamed) {
        removeProfile(profiles_, newName);  // the one being replaced, if any
        removeProfile(profiles_, oldName);
    }
    if (!addProfile(std::move(edited), error)) return false;
    if (renamed) moveHistory(oldName, newName);
    return true;
}

void MainWindow::deleteCurrent() {
    const Profile* profile = currentProfile();
    if (!profile || isRunning()) return;
    const auto answer = QMessageBox::question(this, QStringLiteral("Delete computer"),
                                              QStringLiteral("Delete \"%1\"? This cannot be undone.").arg(profile->name));
    if (answer == QMessageBox::Yes) removeCurrentProfile();
}

void MainWindow::importProfile() {
    if (!storeWritable(nullptr)) return;
    const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Import a profile"), QString(),
                                                      QStringLiteral("WakeDeck profiles (*.json);;All files (*)"));
    if (file.isEmpty()) return;
    QString error;
    const auto profile = ProfileStore::readProfileFile(file, &error);
    if (!profile) {
        showError(QStringLiteral("Could not import the profile"), error);
        return;
    }
    if (findProfile(profiles_, profile->name)) {
        const auto answer = QMessageBox::question(this, QStringLiteral("Replace computer"),
                                                  QStringLiteral("\"%1\" already exists. Replace it?").arg(profile->name));
        if (answer != QMessageBox::Yes) return;
    }
    addProfile(*profile);
}

void MainWindow::exportCurrent() {
    const Profile* profile = currentProfile();
    if (!profile) return;
    const QString suggested = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QLatin1Char('/') +
                              profile->name + QStringLiteral(".json");
    const QString file = QFileDialog::getSaveFileName(this, QStringLiteral("Export the profile"), suggested,
                                                      QStringLiteral("WakeDeck profiles (*.json)"));
    if (file.isEmpty()) return;
    QString error;
    if (!ProfileStore::writeProfileFile(*profile, file, &error)) showError(QStringLiteral("Could not export the profile"), error);
}

// ---- Runs -------------------------------------------------------------------

void MainWindow::setRunning(bool running) {
    const Profile* profile = currentProfile();
    wakeConnectButton_->setEnabled(!running && profile);
    checkButton_->setEnabled(!running && profile);
    wakeOnlyButton_->setEnabled(!running && profile && profile->wake.has_value());
    openClientButton_->setEnabled(!running && profile && profile->client.preset != ClientConfig::Preset::None);
    cancelButton_->setVisible(session_ != nullptr);  // only a full run can be cancelled
    keepWaitingButton_->setVisible(false);
    editButton_->setEnabled(!running && profile);
    deleteButton_->setEnabled(!running && profile);
    list_->setEnabled(!running);
}

void MainWindow::beginStep(const Profile& profile) {
    stopWatchingClient();  // a new step: the previous client no longer quits WakeDeck
    runProfileName_ = profile.name;
    resetRunDisplay();
    stepInFlight_ = true;
    setRunning(true);
}

void MainWindow::endStep() {
    stepInFlight_ = false;
    setRunning(false);
    emit stepFinished();
}

void MainWindow::resetRunDisplay() {
    timeline_->clear();
    wakeSendCount_ = 0;
    wakeStatus_->setText(QStringLiteral("not sent"));
    serviceStatus_->setText(QStringLiteral("not checked"));
    explanationPanel_->setVisible(false);
    explanation_->clear();
    locateButton_->setVisible(false);
    tryAgainButton_->setVisible(false);
}

void MainWindow::appendTimeline(const TimelineEvent& event) {
    auto* item = new QListWidgetItem(QStringLiteral("%1  %2").arg(stamp(event.at), event.text));
    if (event.kind == EventKind::Failure) item->setForeground(Qt::red);
    if (event.kind == EventKind::Ready || event.kind == EventKind::WakeSent) {
        QFont font = item->font();
        font.setBold(true);
        item->setFont(font);
    }
    timeline_->addItem(item);
    timeline_->scrollToBottom();
}

void MainWindow::startWakeAndConnect() {
    const Profile* profile = currentProfile();
    if (!profile || isRunning()) return;
    stopWatchingClient();  // a new run: the previous client no longer quits WakeDeck
    runProfileName_ = profile->name;
    resetRunDisplay();
    activeAtReady_.reset();
    runClock_.start();
    firstWakeClock_.invalidate();
    serviceStatus_->setText(QStringLiteral("checking…"));
    SessionConfig config = profile->toSessionConfig();
    config.learned = timingHistory().learned(profile->name);
    if (sessionConfigHook_) sessionConfigHook_(config);
    session_ = new Session(config, factoriesFor_(*profile), this);
    connect(session_, &Session::timelineEvent, this, &MainWindow::onSessionEvent);
    connect(session_, &Session::stateChanged, this, &MainWindow::onSessionState);
    connect(session_, &Session::finished, this, &MainWindow::onSessionFinished);
    setRunning(true);
    session_->start();
}

void MainWindow::onSessionEvent(const TimelineEvent& event) {
    appendTimeline(event);
    switch (event.kind) {
    case EventKind::WakeSent:
        ++wakeSendCount_;
        if (!firstWakeClock_.isValid()) firstWakeClock_.start();
        if (wakeSendCount_ > 1) {
            // Resent while waiting: how often, and when the last one left.
            wakeStatus_->setText(QStringLiteral("sent %1 times, last at %2 (%3)")
                                     .arg(wakeSendCount_)
                                     .arg(stamp(event.at), event.text.section(QLatin1String(": "), 1)));
            break;
        }
        wakeStatus_->setText(QStringLiteral("sent at %1 (%2)").arg(stamp(event.at), event.text.section(QLatin1String(": "), 1)));
        break;
    case EventKind::Ready:
        serviceStatus_->setText(QStringLiteral("ready at %1").arg(stamp(event.at)));
        // Before the client opens and takes the focus: whether WakeDeck was in
        // front decides the notification for the outcomes that follow.
        activeAtReady_ = isAppActive_ && isAppActive_();
        break;
    case EventKind::Info:
        if (event.text.startsWith(QLatin1String("Not ready")) || event.text.startsWith(QLatin1String("Check "))) {
            serviceStatus_->setText(QStringLiteral("not ready yet, still checking… (%1)").arg(event.text.section(QLatin1String(": "), 1)));
        }
        break;
    case EventKind::Failure: break;
    }
}

void MainWindow::onSessionState(SessionState state) {
    keepWaitingButton_->setVisible(state == SessionState::Waiting);
    if (state == SessionState::Waking || state == SessionState::Waiting) startProgress();  // once per run
}

void MainWindow::onSessionFinished(Outcome outcome, const QString& summary) {
    qint64 launchedPid = 0;
    qint64 runningPid = 0;
    QString launchedClient;
    if (session_) {
        if ((outcome == Outcome::ReadyAfterWake || outcome == Outcome::LaunchFailed) && session_->wakeToReadyMs()) {
            // Learned timing: a run whose service answered after a wake request is
            // remembered, also when the client then did not open (the time is the
            // computer's); the history is disposable, so a failure is only noted.
            QString why;
            if (!timingHistory().record(runProfileName_, *session_->wakeToReadyMs(), QDateTime::currentDateTimeUtc(), &why)) {
                note(QStringLiteral("Could not remember how long this took: %1").arg(why));
            }
        }
        launchedPid = session_->launchedPid();
        runningPid = session_->runningPid();
        launchedClient = session_->launchedClient();
        session_->deleteLater();
        session_ = nullptr;
    }
    stopProgress();
    progressStartedThisRun_ = false;
    setRunning(false);
    switch (outcome) {
    case Outcome::ReadyWithoutWake:
    case Outcome::ReadyAfterWake:
        serviceStatus_->setText(serviceStatus_->text() + QStringLiteral(". %1").arg(summary));
        if (!launchedClient.isEmpty()) watchClient(launchedPid, runningPid, launchedClient);
        break;
    case Outcome::TimedOut:
        serviceStatus_->setText(QStringLiteral("not ready: timed out"));
        showExplanation(summary, false);
        break;
    case Outcome::WakeFailed:
        wakeStatus_->setText(QStringLiteral("failed"));
        serviceStatus_->setText(QStringLiteral("not ready; no further checks because the wake request failed"));
        showExplanation(summary, false);
        break;
    case Outcome::LaunchFailed: {
        const Profile* profile = runProfile();
        showExplanation(QStringLiteral("The service is ready, but the client could not be opened.\n%1").arg(summary),
                        profile && profile->client.preset != ClientConfig::Preset::None);
        break;
    }
    case Outcome::Cancelled:
        serviceStatus_->setText(QStringLiteral("cancelled"));
        break;
    case Outcome::TailscaleOff:
        serviceStatus_->setText(QStringLiteral("not checked: Tailscale is off on this Mac"));
        showExplanation(summary, false);
        break;
    }
    notifyRunEnded(outcome, summary);
    emit runFinished(outcome);
}

void MainWindow::showExplanation(const QString& text, bool offerLocate) {
    explanation_->setPlainText(text);
    explanationPanel_->setVisible(true);
    tryAgainButton_->setVisible(true);
    locateButton_->setVisible(offerLocate);
}

void MainWindow::startCheckOnly() {
    const Profile* profile = currentProfile();
    if (!profile || isRunning()) return;
    beginStep(*profile);
    serviceStatus_->setText(QStringLiteral("checking…"));
    TimelineEvent begin;
    begin.at = QDateTime::currentDateTime();
    begin.text = QStringLiteral("Checking %1").arg(describe(profile->check));
    appendTimeline(begin);
    Probe* probe = factoriesFor_(*profile).probe(profile->check, this);
    connect(probe, &Probe::finished, this, [this, probe](const ProbeResult& result) {
        probe->deleteLater();
        TimelineEvent event;
        event.at = QDateTime::currentDateTime();
        event.kind = result.ready() ? EventKind::Ready : EventKind::Info;
        event.text = result.ready() ? QStringLiteral("Ready: %1").arg(result.detail)
                                    : QStringLiteral("Not ready: %1 (%2)").arg(statusName(result.status), result.detail);
        appendTimeline(event);
        serviceStatus_->setText(result.ready() ? QStringLiteral("ready at %1 (%2)").arg(stamp(event.at), result.detail)
                                               : QStringLiteral("not ready: %1 (%2)").arg(statusName(result.status), result.detail));
        endStep();
        emit checkFinished(result.ready());
    });
    probe->start();
}

void MainWindow::startWakeOnly() {
    const Profile* profile = currentProfile();
    if (!profile || isRunning() || !profile->wake) return;
    beginStep(*profile);
    wakeStatus_->setText(QStringLiteral("sending…"));
    Waker* waker = factoriesFor_(*profile).waker(*profile->wake, this);
    connect(waker, &Waker::finished, this, [this, waker](bool allSent, const QString& summary) {
        waker->deleteLater();
        TimelineEvent event;
        event.at = QDateTime::currentDateTime();
        event.kind = allSent ? EventKind::WakeSent : EventKind::Failure;
        event.text = allSent ? QStringLiteral("Wake request sent: %1").arg(summary) : QStringLiteral("Wake request failed: %1").arg(summary);
        appendTimeline(event);
        wakeStatus_->setText(allSent ? QStringLiteral("sent at %1 (%2)").arg(stamp(event.at), summary) : QStringLiteral("failed: %1").arg(summary));
        TimelineEvent note;
        note.at = event.at;
        note.text = QStringLiteral("This confirms the packets left this Mac, not that the computer is awake. Use Check now or Wake & Connect to find out.");
        appendTimeline(note);
        endStep();
    });
    waker->start();
}

void MainWindow::startOpenClient() {
    const Profile* profile = currentProfile();
    if (!profile || isRunning() || profile->client.preset == ClientConfig::Preset::None) return;
    beginStep(*profile);
    Launcher* launcher = factoriesFor_(*profile).launcher(this);
    TimelineEvent begin;
    begin.at = QDateTime::currentDateTime();
    begin.text = QStringLiteral("Launching %1").arg(launcher->describe());
    appendTimeline(begin);
    connect(launcher, &Launcher::finished, this, [this, launcher](bool ok, const QString& summary) {
        launcher->deleteLater();
        TimelineEvent event;
        event.at = QDateTime::currentDateTime();
        event.kind = ok ? EventKind::Info : EventKind::Failure;
        event.text = summary;
        appendTimeline(event);
        if (ok) watchClient(launcher->launchedPid(), launcher->runningPid(), launcher->describe());
        endStep();
        if (!ok) {
            const Profile* profile = runProfile();
            showExplanation(summary, profile && profile->client.preset != ClientConfig::Preset::None);
        }
    });
    launcher->start();
}

// ---- Quit when the client closes ----------------------------------------------

bool MainWindow::quitWhenClientCloses() const {
    return quitWhenClientClosesAction_->isChecked();
}

void MainWindow::setQuitWhenClientCloses(bool on) {
    quitWhenClientClosesAction_->setChecked(on);  // toggled() saves it and stops a watch
}

bool MainWindow::isWatchingClient() const {
    return quitArmed_ && clientWatcher_->isWatching();
}

qint64 MainWindow::watchedClientPid() const {
    return isWatchingClient() ? clientWatcher_->pid() : 0;
}

qint64 MainWindow::ownedClientPid() const {
    return clientWatcher_->pid();
}

void MainWindow::onQuitOptionToggled(bool on) {
    QSettings().setValue(QLatin1String(kQuitWhenClientClosesKey), on);
    if (!on && isWatchingClient()) {
        const QString client = watchedClient_;
        stopWatchingClient();
        note(QStringLiteral("No longer watching %1; WakeDeck stays open when it closes").arg(client));
    }
}

void MainWindow::watchClient(qint64 launchedPid, qint64 runningPid, const QString& client) {
    stopWatchingClient();
    qint64 pid = 0;
    bool reused = false;
    if (launchedPid > 0) {
        // A process WakeDeck started: from now on it is the one WakeDeck owns,
        // whether or not the option is on.
        if (!clientWatcher_->watch(launchedPid)) {
            if (quitWhenClientCloses()) {
                note(QStringLiteral("Could not watch %1 (process %2); WakeDeck stays open when it closes")
                         .arg(client)
                         .arg(launchedPid));
            }
            return;
        }
        pid = launchedPid;
    } else if (runningPid > 0 && runningPid == clientWatcher_->pid()) {
        // The system brought a copy to the front that WakeDeck started earlier and
        // that has been running ever since: still WakeDeck's own.
        pid = runningPid;
        reused = true;
    }
    if (!quitWhenClientCloses()) return;
    if (pid <= 0) {
        // Only a process WakeDeck started itself is watched; a copy the user had
        // opened, which the system just brought to the front, is left alone.
        note(QStringLiteral("Not watching %1: WakeDeck did not start the running copy (it was open already), "
                            "so WakeDeck stays open")
                 .arg(client));
        return;
    }
    quitArmed_ = true;
    watchedClient_ = client;
    note(QStringLiteral("Watching %1 (process %2%3) — WakeDeck will quit when it closes")
             .arg(client)
             .arg(pid)
             .arg(reused ? QStringLiteral(", opened by WakeDeck earlier") : QString()));
}

void MainWindow::stopWatchingClient() {
    // Ends the promise to quit, not the ownership: the process stays followed.
    quitArmed_ = false;
    watchedClient_.clear();
}

void MainWindow::onClientExited(qint64 pid) {
    if (!quitArmed_) return;  // a client WakeDeck owned closed while not watched: nothing to do
    const QString client = watchedClient_;
    stopWatchingClient();
    note(QStringLiteral("%1 closed (process %2); quitting WakeDeck").arg(client).arg(pid));
    if (quitHook_) quitHook_();
}

void MainWindow::note(const QString& text, EventKind kind) {
    TimelineEvent event;
    event.at = QDateTime::currentDateTime();
    event.kind = kind;
    event.text = text;
    appendTimeline(event);
}

void MainWindow::keepWaiting() {
    if (session_) {
        session_->extendTimeout(60000);
        TimelineEvent note;
        note.at = QDateTime::currentDateTime();
        note.text = QStringLiteral("Waiting one more minute");
        appendTimeline(note);
        if (waitClock_.isValid()) {
            progressLimitMs_ += 60000;
            updateProgress();
        }
    }
}

void MainWindow::cancelRun() {
    if (session_) session_->cancel();
}

void MainWindow::locateClient() {
    const Profile* profile = runProfile();  // the computer whose launch failed, never another one
    if (!profile || isRunning()) return;
    const QString start = profile->client.path.isEmpty() ? QStringLiteral("/Applications") : profile->client.path;
    const QString chosen = QFileDialog::getOpenFileName(this, QStringLiteral("Locate the client application"), start,
                                                        QStringLiteral("Applications (*.app);;All files (*)"));
    if (chosen.isEmpty()) return;
    Profile updated = *profile;
    updated.client.path = chosen;
    if (updated.client.preset == ClientConfig::Preset::None) updated.client.preset = ClientConfig::Preset::Custom;
    if (addProfile(updated)) {
        TimelineEvent note;
        note.at = QDateTime::currentDateTime();
        note.text = QStringLiteral("Client path saved: %1. Use Open client to try it.").arg(chosen);
        appendTimeline(note);
    }
}

// ---- Notification and progress ------------------------------------------------

MainWindow::~MainWindow() {
    stopProgress();  // the Dock icon goes back to normal with the window
}

void MainWindow::closeEvent(QCloseEvent* event) {
    stopProgress();
    QMainWindow::closeEvent(event);
}

void MainWindow::setNotifier(std::unique_ptr<Notifier> notifier) {
    notifier_ = notifier ? std::move(notifier) : std::make_unique<NoNotifier>();
    connect(notifier_.get(), &Notifier::refused, this, &MainWindow::onNotificationsRefused);
}

void MainWindow::setDockProgress(std::unique_ptr<DockProgress> dock) {
    if (dock_ && waitClock_.isValid()) dock_->clear();
    dock_ = dock ? std::move(dock) : std::make_unique<NoDockProgress>();
    if (waitClock_.isValid()) updateProgress();
}

bool MainWindow::notifyWhenReady() const {
    return notifyWhenReadyAction_->isChecked();
}

void MainWindow::setNotifyWhenReady(bool on) {
    notifyWhenReadyAction_->setChecked(on);  // toggled() saves it
}

void MainWindow::notifyRunEnded(Outcome outcome, const QString& summary) {
    if (!notifyWhenReady()) return;
    // A ready service is judged by the moment it answered (the client opened
    // afterwards takes the focus); every other ending by the state now.
    const bool afterReady =
        outcome == Outcome::ReadyWithoutWake || outcome == Outcome::ReadyAfterWake || outcome == Outcome::LaunchFailed;
    const bool active = afterReady && activeAtReady_ ? *activeAtReady_ : (isAppActive_ && isAppActive_());
    if (active) return;
    const QString name = runProfileName_;
    QString title;
    QString body;
    switch (outcome) {
    case Outcome::ReadyWithoutWake:
    case Outcome::ReadyAfterWake:
        title = QStringLiteral("%1 is ready").arg(name);
        body = oneSentence(summary);
        break;
    case Outcome::TimedOut: {
        const Profile* profile = runProfile();
        title = QStringLiteral("%1 did not answer").arg(name);
        body = profile ? QStringLiteral("No answer from %1 within %2.").arg(describe(profile->check), formatDuration(static_cast<int>(progressLimitMs_)))
                       : QStringLiteral("No answer within the time limit.");
        break;
    }
    case Outcome::WakeFailed:
        title = QStringLiteral("Could not wake %1").arg(name);
        body = oneSentence(summary);
        break;
    case Outcome::LaunchFailed:
        title = QStringLiteral("%1 is ready, but the client did not open").arg(name);
        body = oneSentence(QStringLiteral("The service answered, but the client could not be opened: %1").arg(summary));
        break;
    case Outcome::TailscaleOff:
        title = QStringLiteral("%1 cannot be reached").arg(name);
        body = summary;
        break;
    case Outcome::Cancelled:
        return;
    }
    notifier_->notify(title, body);
    QApplication::alert(this);
}

void MainWindow::onNotificationsRefused() {
    if (refusalNoted_) return;
    refusalNoted_ = true;
    note(QStringLiteral("Notifications are switched off for WakeDeck (System Settings ▸ Notifications)"));
}

TimingHistory MainWindow::timingHistory() const {
    return TimingHistory(TimingHistory::pathBeside(store_.path()));
}

qint64 MainWindow::typicalWaitMs(const Profile& profile) const {
    // The learned time runs from the wake request: without one it says nothing.
    if (!profile.wake) return 0;
    const auto learned = timingHistory().learned(profile.name);
    return learned ? learned->typicalMs : 0;  // 0: not learned yet
}

void MainWindow::forgetHistory(const QString& name) {
    QString why;
    if (!timingHistory().remove(name, &why)) note(QStringLiteral("Could not remove the timing history of %1: %2").arg(name, why));
}

void MainWindow::moveHistory(const QString& from, const QString& to) {
    QString why;
    if (!timingHistory().rename(from, to, &why)) note(QStringLiteral("Could not move the timing history to %1: %2").arg(to, why));
}

void MainWindow::startProgress() {
    if (waitClock_.isValid() || progressStartedThisRun_) return;
    progressStartedThisRun_ = true;
    const Profile* profile = runProfile();
    progressTypicalMs_ = profile ? typicalWaitMs(*profile) : 0;
    progressLimitMs_ = profile ? profile->overallTimeoutMs : 0;
    waitClock_.start();
    factsLayout_->setRowVisible(progressRow_, true);
    progressTimer_->start();
    updateProgress();
}

void MainWindow::updateProgress() {
    if (!waitClock_.isValid()) return;
    // Without a learned time the bar runs against the limit, which counts from
    // the start of the run; with one, from the first wake request, as the
    // history measures it.
    qint64 elapsed = runClock_.isValid() ? runClock_.elapsed() : waitClock_.elapsed();
    if (progressTypicalMs_ > 0) elapsed = firstWakeClock_.isValid() ? firstWakeClock_.elapsed() : 0;
    const WaitProgress progress = waitProgress(elapsed, progressTypicalMs_, progressLimitMs_);
    progressBar_->setValue(qRound(progress.fraction * 1000.0));
    progressLabel_->setText(progress.text);
    dock_->setProgress(progress.fraction);
}

void MainWindow::stopProgress() {
    if (progressTimer_) progressTimer_->stop();
    if (!waitClock_.isValid()) return;
    waitClock_.invalidate();
    factsLayout_->setRowVisible(progressRow_, false);
    progressBar_->setValue(0);
    progressLabel_->clear();
    dock_->clear();
}

void MainWindow::setProgressIntervalMs(int ms) {
    progressTimer_->setInterval(ms > 0 ? ms : 1000);
}

bool MainWindow::progressVisible() const {
    return waitClock_.isValid() && !progressRow_->isHidden();
}

QString MainWindow::progressText() const {
    return progressLabel_->text();
}

int MainWindow::progressPermille() const {
    return progressBar_->value();
}

// ---- State for tests ----------------------------------------------------------

QStringList MainWindow::timelineTexts() const {
    QStringList out;
    for (int i = 0; i < timeline_->count(); ++i) out << timeline_->item(i)->text();
    return out;
}

QString MainWindow::wakeStatusText() const { return wakeStatus_->text(); }
QString MainWindow::serviceStatusText() const { return serviceStatus_->text(); }
QString MainWindow::explanationText() const { return explanation_->toPlainText(); }
bool MainWindow::explanationVisible() const { return !explanationPanel_->isHidden(); }
bool MainWindow::locateOffered() const { return !locateButton_->isHidden(); }
QString MainWindow::wakeDetailsText() const { return wakeLabel_->text(); }

// ---- Help -------------------------------------------------------------------

void MainWindow::showHowWakingWorks() {
    QMessageBox::information(
        this, QStringLiteral("How waking works"),
        QStringLiteral(
            "A wake request is a \"magic packet\": a small UDP message that holds the computer's MAC address sixteen "
            "times. A sleeping computer has no working network address, so the packet has to reach its own network "
            "segment, normally as a broadcast on that LAN, where the network card recognises it and powers the machine on.\n\n"
            "Same network: send to the network's broadcast address, for example 192.168.1.255, or choose \"Every local "
            "network this Mac is on\" and WakeDeck finds the broadcast addresses itself each time it sends. Most Wi-Fi "
            "adapters cannot wake a powered-off computer; Ethernet usually can.\n\n"
            "While WakeDeck waits for the service it sends the wake request again every 15 seconds by default (\"Send "
            "again every\" in the editor), in case the first packets were missed.\n\n"
            "From elsewhere: the internet and VPNs route packets to running hosts and do not carry LAN broadcasts, and a "
            "sleeping computer is not on your VPN at all. Something always on inside the home network has to emit the "
            "broadcast for you: a router with a wake feature or a port forward, or a small always-on device that relays "
            "the packet. WakeDeck sends to whatever destination you configure.\n\n"
            "WakeDeck always shows two separate facts: whether the wake request left this Mac, and whether the service "
            "answered. Only the second one means the computer is ready."));
}

void MainWindow::showAbout() {
    QMessageBox::about(this, QStringLiteral("About WakeDeck"),
                       QStringLiteral("<b>WakeDeck %1</b><br>Wake a computer, wait until its service is ready, open your "
                                      "client.<br><br>MIT License. Uses the Qt framework %2 under the GNU LGPL v3, "
                                      "linked dynamically. The licence texts and the notice on how to obtain Qt's "
                                      "source and replace its libraries are in WakeDeck.app/Contents/Resources/Licenses. "
                                      "Source code: <a href=\"https://github.com/MattB4906/CSCI-104-Personal-Projects\">"
                                      "github.com/MattB4906/CSCI-104-Personal-Projects</a>.")
                           .arg(QCoreApplication::applicationVersion(), QString::fromLatin1(qVersion())));
}

void MainWindow::showError(const QString& title, const QString& text) {
    if (!isVisible()) {
        // Tests and scripts: keep errors in the timeline instead of a modal box.
        TimelineEvent event;
        event.at = QDateTime::currentDateTime();
        event.kind = EventKind::Failure;
        event.text = QStringLiteral("%1: %2").arg(title, text);
        appendTimeline(event);
        return;
    }
    QMessageBox::warning(this, title, text);
}

}  // namespace wakedeck::app
