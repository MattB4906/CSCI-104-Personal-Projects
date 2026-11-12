// Shared by test_launcher and test_gui: builds the fake agent's bundle.
#pragma once

#include <QDir>
#include <QFile>
#include <QString>

#ifndef WAKEDECK_FAKE_AGENT
#error "CMake defines WAKEDECK_FAKE_AGENT, the path of the built wakedeck_fake_agent"
#endif

// A background-only bundle around the fake agent (tests/fake_agent.mm): a real
// AppKit process, so the system reports it as a running application, but with
// no window, no Dock icon and no menu bar. It ends by itself after 8 seconds.
inline QString makeAgentBundle(const QDir& dir) {
    const QString app = dir.filePath(QStringLiteral("WakeDeckFakeAgent.app"));
    QDir().mkpath(app + QStringLiteral("/Contents/MacOS"));
    QFile plist(app + QStringLiteral("/Contents/Info.plist"));
    if (!plist.open(QIODevice::WriteOnly)) return QString();
    plist.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                "<plist version=\"1.0\"><dict>\n"
                "<key>CFBundleExecutable</key><string>WakeDeckFakeAgent</string>\n"
                "<key>CFBundleIdentifier</key><string>net.example.wakedeck.fake-agent</string>\n"
                "<key>CFBundlePackageType</key><string>APPL</string>\n"
                "<key>LSBackgroundOnly</key><true/>\n"
                "</dict></plist>\n");
    plist.close();
    const QString executable = app + QStringLiteral("/Contents/MacOS/WakeDeckFakeAgent");
    if (!QFile::copy(QStringLiteral(WAKEDECK_FAKE_AGENT), executable)) return QString();
    QFile::setPermissions(executable, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return app;
}
