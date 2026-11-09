// macOS implementation of AppLocator (Objective-C++, compiled with ARC).
// This is the only file in WakeDeck that speaks to AppKit.
#include "engine/AppLocator.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>

namespace wakedeck::platform {

std::optional<QString> applicationPathForBundleId(const QString& bundleId) {
    @autoreleasepool {
        NSURL* url = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier:bundleId.toNSString()];
        if (url == nil || url.path == nil) return std::nullopt;
        return QString::fromNSString(url.path);
    }
}

std::optional<QString> bundleExecutable(const QString& appPath) {
    @autoreleasepool {
        NSBundle* bundle = [NSBundle bundleWithPath:appPath.toNSString()];
        if (bundle == nil || bundle.executableURL == nil || bundle.executableURL.path == nil) return std::nullopt;
        return QString::fromNSString(bundle.executableURL.path);
    }
}

void openApplication(const QString& appPath, QObject* context,
                     std::function<void(bool ok, const QString& error)> done) {
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:appPath.toNSString()];
        NSWorkspaceOpenConfiguration* configuration = [NSWorkspaceOpenConfiguration configuration];
        configuration.activates = YES;

        // The completion handler runs on a system thread. Hop back to the Qt
        // thread through the application object, which outlives any caller, and
        // only there test whether the caller still exists: testing the QPointer on
        // the system thread would race with the caller's destructor.
        QPointer<QObject> guard(context);
        [[NSWorkspace sharedWorkspace] openApplicationAtURL:url
                                              configuration:configuration
                                          completionHandler:^(NSRunningApplication* application, NSError* error) {
                                              const bool ok = application != nil && error == nil;
                                              const QString message =
                                                  error != nil ? QString::fromNSString(error.localizedDescription) : QString();
                                              QObject* receiver = QCoreApplication::instance();
                                              if (receiver == nullptr) return;  // the application is shutting down
                                              QMetaObject::invokeMethod(
                                                  receiver,
                                                  [guard, done, ok, message] {
                                                      if (guard) done(ok, message);
                                                  },
                                                  Qt::QueuedConnection);
                                          }];
    }
}

}  // namespace wakedeck::platform
