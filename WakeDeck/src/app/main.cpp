// WakeDeck, the application. Everything it does goes through the same engine
// the command-line tool uses; this file only creates the window.
#include <QApplication>
#include <QFileOpenEvent>
#include <QTimer>
#include <QUrl>

#include "Diagnose.h"
#include "MainWindow.h"
#include "engine/ProfileStore.h"

#ifndef WAKEDECK_VERSION
#define WAKEDECK_VERSION "0.0.0-dev"
#endif

namespace {

class FileOpenFilter final : public QObject {
public:
    explicit FileOpenFilter(wakedeck::app::MainWindow* window, QObject* parent = nullptr)
        : QObject(parent), window_(window) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::FileOpen) {
            auto* fileEvent = static_cast<QFileOpenEvent*>(event);
            const QUrl url = fileEvent->url();
            if (url.isValid() && url.scheme().compare(QStringLiteral("wakedeck"), Qt::CaseInsensitive) == 0) {
                QTimer::singleShot(0, window_, [window = window_, url] {
                    window->openUrl(url);
                });
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    wakedeck::app::MainWindow* window_ = nullptr;
};

}  // namespace

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
    FileOpenFilter filter(&window);
    app.installEventFilter(&filter);
    window.show();
    return app.exec();
}
