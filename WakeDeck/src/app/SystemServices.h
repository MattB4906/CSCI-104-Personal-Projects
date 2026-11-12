// SystemServices: what the window asks of the system around it, behind two
// small interfaces so the GUI tests use fakes. A Notifier shows a "ready"
// notification; a DockProgress draws the wait progress on the Dock icon.
//
// The real ones live in src/mac (UserNotifications and NSDockTile). They are
// used only when the Qt platform is "cocoa"; on any other platform (the
// offscreen tests, for example) the defaults do nothing. The real notifier also
// checks that the process is an application bundle with an identifier before it
// touches the notification center, which raises an exception otherwise.
#pragma once

#include <QObject>
#include <QString>
#include <functional>
#include <memory>

namespace wakedeck::app {

class Notifier : public QObject {
    Q_OBJECT
public:
    explicit Notifier(QObject* parent = nullptr) : QObject(parent) {}
    // False when notifications cannot be shown at all (no bundle, wrong platform).
    virtual bool available() const = 0;
    // Shows one notification. The first one asks the user for permission (alert
    // and sound); if the system says notifications are not allowed, refused() is
    // emitted later on this object's thread.
    virtual void notify(const QString& title, const QString& body) = 0;

signals:
    void refused();
};

class NoNotifier final : public Notifier {
    Q_OBJECT
public:
    explicit NoNotifier(QObject* parent = nullptr) : Notifier(parent) {}
    bool available() const override { return false; }
    void notify(const QString&, const QString&) override {}
};

class DockProgress {
public:
    virtual ~DockProgress() = default;
    virtual void setProgress(double fraction) = 0;  // 0 to 1; draws the icon with a bar
    virtual void clear() = 0;                       // back to the plain icon
};

class NoDockProgress final : public DockProgress {
public:
    void setProgress(double) override {}
    void clear() override {}
};

// True when the Qt platform is "cocoa", the only one where the system services are used.
bool systemServicesAvailable();
// The production defaults: the macOS implementations on cocoa, otherwise the no-ops.
std::unique_ptr<Notifier> makeDefaultNotifier();
std::unique_ptr<DockProgress> makeDefaultDockProgress();

}  // namespace wakedeck::app

// The macOS side, in src/mac/SystemServices.mm (Objective-C++, ARC).
namespace wakedeck::platform {

// The main bundle's identifier when this process is an application bundle
// (.app) that has one; empty otherwise (test runners, the command-line tool).
QString applicationBundleIdentifier();

// UserNotifications. Without an application bundle identifier the notifier is
// unavailable and never touches the notification center.
std::unique_ptr<app::Notifier> makeSystemNotifier();

// Reads the notification permission without asking for it: "authorized",
// "denied", "notDetermined" or "unavailable". `done` runs later on the
// application's thread (or at once when the answer is "unavailable").
void readNotificationPermission(std::function<void(const QString& permission)> done);

// NSDockTile with a view that draws the application icon and the bar itself.
std::unique_ptr<app::DockProgress> makeSystemDockProgress();
bool dockTileAvailable();

// Draws the same picture as the Dock progress, at `fraction`, into a PNG file.
bool renderDockProgressPicture(double fraction, const QString& pngPath, QString* error);

}  // namespace wakedeck::platform
