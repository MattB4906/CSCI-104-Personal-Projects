// Diagnose: the windowless "--diagnose <file>" mode of the app. It reports what
// the system services look like from inside the running bundle (useful when the
// app is started by LaunchServices, as Finder does) and exits. It creates no
// window, reads and writes no profiles and no settings, and never asks for
// notification permission.
#pragma once

#include <QString>
#include <QStringList>
#include <optional>

namespace wakedeck::app {

// The file named after "--diagnose" in the arguments; an empty string when the
// option is given without a file; std::nullopt when the option is absent.
std::optional<QString> diagnoseTarget(const QStringList& arguments);

// Needs a QGuiApplication. Writes `path` (UTF-8 lines "key=value": version,
// platform, bundleId, notifications, dockTile, tailscaleCli, tailscaleState,
// tailscalePeers; never a device name or address) and renders the Dock progress at
// 40 % into "<path>.dock.png". Returns the process exit code: 0 when the text
// file was written, non-zero otherwise.
int runDiagnose(const QString& path);

}  // namespace wakedeck::app
