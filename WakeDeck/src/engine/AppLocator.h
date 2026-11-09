// AppLocator: the platform-specific part of launching a client. The interface
// is plain C++; the macOS implementation in src/mac/AppLocator.mm asks Launch
// Services for an application by bundle identifier, looks inside .app bundles
// for their executable, and opens bundles through NSWorkspace.
#pragma once

#include <QObject>
#include <QString>
#include <functional>
#include <optional>

namespace wakedeck::platform {

// Full path of the application bundle registered for this identifier, if any.
std::optional<QString> applicationPathForBundleId(const QString& bundleId);

// The executable inside a .app bundle (Contents/MacOS/<name>), if the bundle is valid.
std::optional<QString> bundleExecutable(const QString& appPath);

// Opens, or activates if already running, an application bundle without
// arguments. Asynchronous: `done` is delivered later on `context`'s thread,
// and skipped if `context` has been deleted meanwhile.
void openApplication(const QString& appPath, QObject* context,
                     std::function<void(bool ok, const QString& error)> done);

}  // namespace wakedeck::platform
