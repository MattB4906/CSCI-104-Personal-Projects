#include "SystemServices.h"

#include <QGuiApplication>

namespace wakedeck::app {

bool systemServicesAvailable() {
    return QGuiApplication::platformName() == QLatin1String("cocoa");
}

std::unique_ptr<Notifier> makeDefaultNotifier() {
    if (systemServicesAvailable()) return platform::makeSystemNotifier();
    return std::make_unique<NoNotifier>();
}

std::unique_ptr<DockProgress> makeDefaultDockProgress() {
    if (systemServicesAvailable()) return platform::makeSystemDockProgress();
    return std::make_unique<NoDockProgress>();
}

}  // namespace wakedeck::app
