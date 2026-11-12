// ProfileEditorDialog: add or edit one computer. Three groups, Wake, Check and
// Connect, each with a Test button that uses the real engine, and OK only when
// the same validation the file format uses is satisfied.
#pragma once

#include <QDialog>
#include <QString>
#include <optional>

#include "engine/Profile.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace wakedeck::app {

class ProfileEditorDialog final : public QDialog {
    Q_OBJECT
public:
    ProfileEditorDialog(const Profile& initial, bool isNew, QWidget* parent = nullptr);

    void setProfile(const Profile& profile);              // fills the fields
    std::optional<Profile> collect(QString* error) const;  // reads and validates the fields
    Profile result() const { return result_; }             // valid after accept()

private:
    void buildUi();
    void updateEnabledFields();
    void onAccept();
    void testWake();
    void testCheck();
    void findClient();
    void browseClient();

    QLineEdit* nameEdit_ = nullptr;
    QLineEdit* notesEdit_ = nullptr;

    QCheckBox* wakeEnabled_ = nullptr;
    QLineEdit* macEdit_ = nullptr;
    QComboBox* destinationModeCombo_ = nullptr;  // every local network, or a specific address
    QLineEdit* destinationEdit_ = nullptr;
    QSpinBox* resendSpin_ = nullptr;             // seconds; 0 is shown as "Only once"
    QLineEdit* portsEdit_ = nullptr;
    QSpinBox* repeatSpin_ = nullptr;
    QSpinBox* intervalSpin_ = nullptr;
    QPushButton* wakeTestButton_ = nullptr;
    QLabel* wakeTestLabel_ = nullptr;

    QComboBox* kindCombo_ = nullptr;
    QLineEdit* hostEdit_ = nullptr;
    QSpinBox* portSpin_ = nullptr;
    QSpinBox* probeTimeoutSpin_ = nullptr;
    QSpinBox* pollSpin_ = nullptr;
    QSpinBox* overallSpin_ = nullptr;
    QPushButton* checkTestButton_ = nullptr;
    QLabel* checkTestLabel_ = nullptr;

    QComboBox* clientCombo_ = nullptr;
    QLineEdit* clientPathEdit_ = nullptr;
    QPushButton* browseButton_ = nullptr;
    QComboBox* launchCombo_ = nullptr;
    QLineEdit* appEdit_ = nullptr;
    QLineEdit* argsEdit_ = nullptr;
    QPushButton* clientTestButton_ = nullptr;
    QLabel* clientTestLabel_ = nullptr;

    Profile result_;
};

}  // namespace wakedeck::app
