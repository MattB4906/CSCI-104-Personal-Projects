#include "ProfileEditorDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <chrono>

#include "core/MacAddress.h"
#include "core/WakePlan.h"
#include "engine/ClientLauncher.h"
#include "engine/QtWaker.h"
#include "engine/ServiceProbe.h"

namespace wakedeck::app {
namespace {

QString portsText(const std::vector<std::uint16_t>& ports) {
    QStringList parts;
    for (std::uint16_t port : ports) parts << QString::number(port);
    return parts.join(QStringLiteral(","));
}

}  // namespace

ProfileEditorDialog::ProfileEditorDialog(const Profile& initial, bool isNew, QWidget* parent) : QDialog(parent) {
    setWindowTitle(isNew ? QStringLiteral("Add a computer") : QStringLiteral("Edit %1").arg(initial.name));
    setModal(true);
    buildUi();
    setProfile(initial);
    updateEnabledFields();
}

void ProfileEditorDialog::buildUi() {
    auto* layout = new QVBoxLayout(this);

    auto* top = new QFormLayout;
    nameEdit_ = new QLineEdit(this);
    nameEdit_->setObjectName(QStringLiteral("nameEdit"));
    nameEdit_->setPlaceholderText(QStringLiteral("for example Living Room PC"));
    notesEdit_ = new QLineEdit(this);
    notesEdit_->setObjectName(QStringLiteral("notesEdit"));
    top->addRow(QStringLiteral("Name:"), nameEdit_);
    top->addRow(QStringLiteral("Notes:"), notesEdit_);
    layout->addLayout(top);

    // ---- Wake ----
    auto* wakeBox = new QGroupBox(QStringLiteral("Wake"), this);
    auto* wakeLayout = new QFormLayout(wakeBox);
    wakeEnabled_ = new QCheckBox(QStringLiteral("Send a wake request when the service is not ready"), wakeBox);
    wakeEnabled_->setObjectName(QStringLiteral("wakeEnabled"));
    wakeLayout->addRow(wakeEnabled_);
    macEdit_ = new QLineEdit(wakeBox);
    macEdit_->setObjectName(QStringLiteral("macEdit"));
    macEdit_->setPlaceholderText(QStringLiteral("00:00:5E:00:53:2A"));
    destinationEdit_ = new QLineEdit(wakeBox);
    destinationEdit_->setObjectName(QStringLiteral("destinationEdit"));
    destinationEdit_->setPlaceholderText(QStringLiteral("192.168.1.255, the broadcast address of the computer's network"));
    portsEdit_ = new QLineEdit(wakeBox);
    portsEdit_->setObjectName(QStringLiteral("portsEdit"));
    portsEdit_->setPlaceholderText(QStringLiteral("9,7"));
    repeatSpin_ = new QSpinBox(wakeBox);
    repeatSpin_->setRange(1, 100);
    intervalSpin_ = new QSpinBox(wakeBox);
    intervalSpin_->setRange(0, 10000);
    intervalSpin_->setSuffix(QStringLiteral(" ms"));
    wakeLayout->addRow(QStringLiteral("MAC address:"), macEdit_);
    wakeLayout->addRow(QStringLiteral("Send to:"), destinationEdit_);
    wakeLayout->addRow(QStringLiteral("UDP ports:"), portsEdit_);
    wakeLayout->addRow(QStringLiteral("Packets per port:"), repeatSpin_);
    wakeLayout->addRow(QStringLiteral("Pause between packets:"), intervalSpin_);
    auto* wakeTestRow = new QHBoxLayout;
    wakeTestButton_ = new QPushButton(QStringLiteral("Test wake"), wakeBox);
    wakeTestLabel_ = new QLabel(wakeBox);
    wakeTestLabel_->setWordWrap(true);
    wakeTestRow->addWidget(wakeTestButton_);
    wakeTestRow->addWidget(wakeTestLabel_, 1);
    wakeLayout->addRow(wakeTestRow);
    layout->addWidget(wakeBox);
    connect(wakeEnabled_, &QCheckBox::toggled, this, [this](bool) { updateEnabledFields(); });
    connect(wakeTestButton_, &QPushButton::clicked, this, &ProfileEditorDialog::testWake);

    // ---- Check ----
    auto* checkBox = new QGroupBox(QStringLiteral("Check"), this);
    auto* checkLayout = new QFormLayout(checkBox);
    kindCombo_ = new QComboBox(checkBox);
    kindCombo_->setObjectName(QStringLiteral("kindCombo"));
    kindCombo_->addItem(QStringLiteral("Sunshine (Moonlight host)"), QStringLiteral("sunshine"));
    kindCombo_->addItem(QStringLiteral("TCP port (Remote Desktop, SSH, anything)"), QStringLiteral("tcp"));
    hostEdit_ = new QLineEdit(checkBox);
    hostEdit_->setObjectName(QStringLiteral("hostEdit"));
    hostEdit_->setPlaceholderText(QStringLiteral("the computer's Tailscale name or address"));
    portSpin_ = new QSpinBox(checkBox);
    portSpin_->setRange(1, 65535);
    probeTimeoutSpin_ = new QSpinBox(checkBox);
    probeTimeoutSpin_->setRange(100, 60000);
    probeTimeoutSpin_->setSuffix(QStringLiteral(" ms"));
    pollSpin_ = new QSpinBox(checkBox);
    pollSpin_->setRange(1, 600);
    pollSpin_->setSuffix(QStringLiteral(" s"));
    overallSpin_ = new QSpinBox(checkBox);
    overallSpin_->setRange(1, 3600);
    overallSpin_->setSuffix(QStringLiteral(" s"));
    checkLayout->addRow(QStringLiteral("Service:"), kindCombo_);
    checkLayout->addRow(QStringLiteral("Host:"), hostEdit_);
    checkLayout->addRow(QStringLiteral("Port:"), portSpin_);
    checkLayout->addRow(QStringLiteral("Each check waits:"), probeTimeoutSpin_);
    checkLayout->addRow(QStringLiteral("Check every:"), pollSpin_);
    checkLayout->addRow(QStringLiteral("Give up after:"), overallSpin_);
    auto* checkTestRow = new QHBoxLayout;
    checkTestButton_ = new QPushButton(QStringLiteral("Test check"), checkBox);
    checkTestLabel_ = new QLabel(checkBox);
    checkTestLabel_->setWordWrap(true);
    checkTestRow->addWidget(checkTestButton_);
    checkTestRow->addWidget(checkTestLabel_, 1);
    checkLayout->addRow(checkTestRow);
    layout->addWidget(checkBox);
    connect(kindCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index == 0 && portSpin_->value() == 3389) portSpin_->setValue(47989);
        if (index == 1 && portSpin_->value() == 47989) portSpin_->setValue(3389);
    });
    connect(checkTestButton_, &QPushButton::clicked, this, &ProfileEditorDialog::testCheck);

    // ---- Connect ----
    auto* clientBox = new QGroupBox(QStringLiteral("Connect"), this);
    auto* clientLayout = new QFormLayout(clientBox);
    clientCombo_ = new QComboBox(clientBox);
    clientCombo_->setObjectName(QStringLiteral("clientCombo"));
    clientCombo_->addItem(QStringLiteral("Nothing, just tell me when it is ready"), QStringLiteral("none"));
    clientCombo_->addItem(QStringLiteral("Moonlight"), QStringLiteral("moonlight"));
    clientCombo_->addItem(QStringLiteral("Another application"), QStringLiteral("custom"));
    auto* pathRow = new QHBoxLayout;
    clientPathEdit_ = new QLineEdit(clientBox);
    clientPathEdit_->setObjectName(QStringLiteral("clientPathEdit"));
    clientPathEdit_->setPlaceholderText(QStringLiteral("leave empty to find Moonlight automatically"));
    browseButton_ = new QPushButton(QStringLiteral("Browse…"), clientBox);
    pathRow->addWidget(clientPathEdit_, 1);
    pathRow->addWidget(browseButton_);
    launchCombo_ = new QComboBox(clientBox);
    launchCombo_->setObjectName(QStringLiteral("launchCombo"));
    launchCombo_->addItem(QStringLiteral("Open Moonlight"), QStringLiteral("open"));
    launchCombo_->addItem(QStringLiteral("Start streaming an app"), QStringLiteral("stream"));
    appEdit_ = new QLineEdit(clientBox);
    appEdit_->setObjectName(QStringLiteral("appEdit"));
    appEdit_->setPlaceholderText(QStringLiteral("Desktop"));
    argsEdit_ = new QLineEdit(clientBox);
    argsEdit_->setObjectName(QStringLiteral("argsEdit"));
    argsEdit_->setPlaceholderText(QStringLiteral("arguments separated by spaces; {host} becomes the host"));
    clientLayout->addRow(QStringLiteral("Client:"), clientCombo_);
    clientLayout->addRow(QStringLiteral("Application:"), pathRow);
    clientLayout->addRow(QStringLiteral("When ready:"), launchCombo_);
    clientLayout->addRow(QStringLiteral("App to stream:"), appEdit_);
    clientLayout->addRow(QStringLiteral("Arguments:"), argsEdit_);
    auto* clientTestRow = new QHBoxLayout;
    clientTestButton_ = new QPushButton(QStringLiteral("Find client"), clientBox);
    clientTestLabel_ = new QLabel(clientBox);
    clientTestLabel_->setWordWrap(true);
    clientTestRow->addWidget(clientTestButton_);
    clientTestRow->addWidget(clientTestLabel_, 1);
    clientLayout->addRow(clientTestRow);
    layout->addWidget(clientBox);
    connect(clientCombo_, &QComboBox::currentIndexChanged, this, [this](int) { updateEnabledFields(); });
    connect(launchCombo_, &QComboBox::currentIndexChanged, this, [this](int) { updateEnabledFields(); });
    connect(browseButton_, &QPushButton::clicked, this, &ProfileEditorDialog::browseClient);
    connect(clientTestButton_, &QPushButton::clicked, this, &ProfileEditorDialog::findClient);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("buttons"));
    connect(buttons, &QDialogButtonBox::accepted, this, &ProfileEditorDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void ProfileEditorDialog::setProfile(const Profile& profile) {
    nameEdit_->setText(profile.name);
    notesEdit_->setText(profile.notes);

    wakeEnabled_->setChecked(profile.wake.has_value());
    if (profile.wake) {
        macEdit_->setText(QString::fromStdString(profile.wake->mac.toString()));
        destinationEdit_->setText(QString::fromStdString(profile.wake->target.destination));
        portsEdit_->setText(portsText(profile.wake->target.ports));
        repeatSpin_->setValue(static_cast<int>(profile.wake->target.repeat));
        intervalSpin_->setValue(static_cast<int>(profile.wake->target.interval.count()));
    } else {
        macEdit_->clear();
        destinationEdit_->clear();
        portsEdit_->setText(QStringLiteral("9,7"));
        repeatSpin_->setValue(5);
        intervalSpin_->setValue(100);
    }

    kindCombo_->setCurrentIndex(profile.check.kind == ProbeKind::Sunshine ? 0 : 1);
    hostEdit_->setText(profile.check.host);
    portSpin_->setValue(profile.check.port);
    probeTimeoutSpin_->setValue(profile.check.timeoutMs);
    pollSpin_->setValue(std::max(1, profile.pollIntervalMs / 1000));
    overallSpin_->setValue(std::max(1, profile.overallTimeoutMs / 1000));

    clientCombo_->setCurrentIndex(profile.client.preset == ClientConfig::Preset::None       ? 0
                                  : profile.client.preset == ClientConfig::Preset::Moonlight ? 1
                                                                                             : 2);
    clientPathEdit_->setText(profile.client.path);
    launchCombo_->setCurrentIndex(profile.client.launchMode == QLatin1String("stream") ? 1 : 0);
    appEdit_->setText(profile.client.appName);
    argsEdit_->setText(profile.client.arguments.join(QLatin1Char(' ')));
    updateEnabledFields();
}

void ProfileEditorDialog::updateEnabledFields() {
    const bool wake = wakeEnabled_->isChecked();
    for (QWidget* w : {static_cast<QWidget*>(macEdit_), static_cast<QWidget*>(destinationEdit_), static_cast<QWidget*>(portsEdit_),
                       static_cast<QWidget*>(repeatSpin_), static_cast<QWidget*>(intervalSpin_), static_cast<QWidget*>(wakeTestButton_)}) {
        w->setEnabled(wake);
    }
    const QString preset = clientCombo_->currentData().toString();
    const bool moonlight = preset == QLatin1String("moonlight");
    const bool custom = preset == QLatin1String("custom");
    clientPathEdit_->setEnabled(moonlight || custom);
    browseButton_->setEnabled(moonlight || custom);
    clientPathEdit_->setPlaceholderText(moonlight ? QStringLiteral("leave empty to find Moonlight automatically")
                                                  : QStringLiteral("full path to the application or executable"));
    launchCombo_->setEnabled(moonlight);
    appEdit_->setEnabled(moonlight && launchCombo_->currentData().toString() == QLatin1String("stream"));
    argsEdit_->setEnabled(custom);
    clientTestButton_->setEnabled(moonlight || custom);
}

std::optional<Profile> ProfileEditorDialog::collect(QString* error) const {
    QString ignored;
    QString* reason = error ? error : &ignored;
    Profile profile;

    profile.name = nameEdit_->text().trimmed();
    if (profile.name.isEmpty()) {
        *reason = QStringLiteral("Give the computer a name.");
        return std::nullopt;
    }
    profile.notes = notesEdit_->text();

    profile.check.kind = kindCombo_->currentData().toString() == QLatin1String("sunshine") ? ProbeKind::Sunshine : ProbeKind::Tcp;
    profile.check.host = hostEdit_->text().trimmed();
    if (profile.check.host.isEmpty()) {
        *reason = QStringLiteral("Enter the host to check: the computer's Tailscale name or address.");
        return std::nullopt;
    }
    profile.check.port = static_cast<quint16>(portSpin_->value());
    profile.check.timeoutMs = probeTimeoutSpin_->value();
    profile.pollIntervalMs = pollSpin_->value() * 1000;
    profile.overallTimeoutMs = overallSpin_->value() * 1000;

    if (wakeEnabled_->isChecked()) {
        const auto mac = MacAddress::parse(macEdit_->text().toStdString());
        if (!mac) {
            *reason = QStringLiteral("The MAC address is not valid. Example: 00:00:5E:00:53:2A.");
            return std::nullopt;
        }
        WakeTarget target;
        target.destination = destinationEdit_->text().trimmed().toStdString();
        if (target.destination.empty()) {
            *reason = QStringLiteral("Enter where to send the wake request, usually the broadcast address of the computer's network, such as 192.168.1.255.");
            return std::nullopt;
        }
        const auto ports = parsePortList(portsEdit_->text().toStdString());
        if (!ports) {
            *reason = QStringLiteral("The UDP ports must be numbers from 1 to 65535 separated by commas, for example 9,7.");
            return std::nullopt;
        }
        target.ports = *ports;
        target.repeat = static_cast<unsigned>(repeatSpin_->value());
        target.interval = std::chrono::milliseconds(intervalSpin_->value());
        profile.wake = WakeConfig{*mac, target};
    }

    const QString preset = clientCombo_->currentData().toString();
    profile.client.preset = preset == QLatin1String("moonlight") ? ClientConfig::Preset::Moonlight
                            : preset == QLatin1String("custom")  ? ClientConfig::Preset::Custom
                                                                  : ClientConfig::Preset::None;
    if (profile.client.preset != ClientConfig::Preset::None) {
        profile.client.path = clientPathEdit_->text().trimmed();
    }
    if (profile.client.preset == ClientConfig::Preset::Moonlight) {
        profile.client.launchMode = launchCombo_->currentData().toString();
        if (profile.client.launchMode == QLatin1String("stream")) profile.client.appName = appEdit_->text().trimmed();
    }
    if (profile.client.preset == ClientConfig::Preset::Custom) {
        profile.client.arguments = argsEdit_->text().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    }

    // The same validation the profiles file goes through, so what the dialog
    // accepts is exactly what the file accepts.
    QString why;
    const auto validated = profileFromJson(toJson(profile), &why);
    if (!validated) {
        *reason = why;
        return std::nullopt;
    }
    return *validated;
}

void ProfileEditorDialog::onAccept() {
    QString error;
    const auto profile = collect(&error);
    if (!profile) {
        QMessageBox::warning(this, QStringLiteral("Something is missing"), error);
        return;
    }
    result_ = *profile;
    accept();
}

void ProfileEditorDialog::testWake() {
    QString error;
    const auto profile = collect(&error);
    if (!profile || !profile->wake) {
        wakeTestLabel_->setText(profile ? QStringLiteral("Enable the wake request first.") : error);
        return;
    }
    wakeTestLabel_->setText(QStringLiteral("Sending…"));
    Waker* waker = makeWaker(*profile->wake, this);
    connect(waker, &Waker::finished, this, [this, waker](bool allSent, const QString& summary) {
        waker->deleteLater();
        wakeTestLabel_->setText(allSent ? QStringLiteral("Sent: %1. This confirms the packets left this Mac, not that the computer woke up.").arg(summary)
                                        : QStringLiteral("Failed: %1").arg(summary));
    });
    waker->start();
}

void ProfileEditorDialog::testCheck() {
    QString error;
    const auto profile = collect(&error);
    if (!profile) {
        checkTestLabel_->setText(error);
        return;
    }
    checkTestLabel_->setText(QStringLiteral("Checking…"));
    Probe* probe = makeProbe(profile->check, this);
    connect(probe, &Probe::finished, this, [this, probe](const ProbeResult& result) {
        probe->deleteLater();
        checkTestLabel_->setText(result.ready() ? QStringLiteral("Ready: %1 (%2 ms)").arg(result.detail).arg(result.elapsedMs)
                                                : QStringLiteral("Not ready: %1 (%2)").arg(statusName(result.status), result.detail));
    });
    probe->start();
}

void ProfileEditorDialog::findClient() {
    QString error;
    const auto profile = collect(&error);
    if (!profile) {
        clientTestLabel_->setText(error);
        return;
    }
    const LaunchResolution resolution = resolveLaunch(profile->client, profile->check.host);
    if (!resolution.plan) {
        clientTestLabel_->setText(resolution.error);
        return;
    }
    clientTestLabel_->setText(resolution.plan->openBundle
                                  ? QStringLiteral("Found. Would open %1").arg(resolution.plan->program)
                                  : QStringLiteral("Found. Would run %1 %2").arg(resolution.plan->program, resolution.plan->arguments.join(QLatin1Char(' '))));
}

void ProfileEditorDialog::browseClient() {
    const QString start = clientPathEdit_->text().isEmpty() ? QStringLiteral("/Applications") : clientPathEdit_->text();
    const QString chosen = QFileDialog::getOpenFileName(this, QStringLiteral("Choose the application"), start,
                                                        QStringLiteral("Applications (*.app);;All files (*)"));
    if (!chosen.isEmpty()) clientPathEdit_->setText(chosen);
}

}  // namespace wakedeck::app
