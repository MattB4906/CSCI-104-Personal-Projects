// WakeDeck, the application. Everything it does goes through the same engine
// the command-line tool uses; this file only creates the window.
#include <QApplication>

#include "Diagnose.h"
#include "MainWindow.h"
#include "engine/ProfileStore.h"

#ifndef WAKEDECK_VERSION
#define WAKEDECK_VERSION "0.0.0-dev"
#endif

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("WakeDeck"));
    QCoreApplication::setApplicationName(QStringLiteral("WakeDeck"));
    QCoreApplication::setApplicationVersion(QStringLiteral(WAKEDECK_VERSION));

    // "--diagnose <file>": report on the system services and exit, before any
    // window, profile or setting is touched.
    if (const auto diagnose = wakedeck::app::diagnoseTarget(QCoreApplication::arguments())) {
        return wakedeck::app::runDiagnose(*diagnose);
    }

    wakedeck::app::MainWindow window(wakedeck::ProfileStore{});
    window.show();
    return app.exec();
}
