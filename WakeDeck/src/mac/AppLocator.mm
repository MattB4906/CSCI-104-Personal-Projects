// macOS implementation of AppLocator (Objective-C++, compiled with ARC).
// This is the only file in WakeDeck that speaks to AppKit.
#include "engine/AppLocator.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>

namespace wakedeck::platform {
namespace {

// True if a copy of the application at `url` is running right now: one with the
// same bundle identifier, or, for a bundle without one, the same bundle path.
bool isAlreadyRunning(NSURL* url) {
    NSString* identifier = [NSBundle bundleWithURL:url].bundleIdentifier;
    if (identifier != nil && [NSRunningApplication runningApplicationsWithBundleIdentifier:identifier].count > 0) {
        return true;
    }
    NSString* path = url.URLByStandardizingPath.URLByResolvingSymlinksInPath.path;
    for (NSRunningApplication* running in [NSWorkspace sharedWorkspace].runningApplications) {
        NSURL* bundle = running.bundleURL;
        if (bundle != nil && [bundle.URLByStandardizingPath.URLByResolvingSymlinksInPath.path isEqualToString:path]) {
            return true;
        }
    }
    return false;
}

}  // namespace

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
                     std::function<void(bool ok, const QString& error, qint64 pid, bool alreadyRunning)> done) {
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:appPath.toNSString()];
        NSWorkspaceOpenConfiguration* configuration = [NSWorkspaceOpenConfiguration configuration];
        configuration.activates = YES;

        // Asked before the open: if a copy is already running, the open only
        // activates it and starts no process.
        const bool wasRunning = isAlreadyRunning(url);

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
                                              const qint64 pid = (ok && application.processIdentifier > 0)
                                                                     ? static_cast<qint64>(application.processIdentifier)
                                                                     : 0;
                                              QObject* receiver = QCoreApplication::instance();
                                              if (receiver == nullptr) return;  // the application is shutting down
                                              QMetaObject::invokeMethod(
                                                  receiver,
                                                  [guard, done, ok, message, pid, wasRunning] {
                                                      if (guard) done(ok, message, pid, wasRunning);
                                                  },
                                                  Qt::QueuedConnection);
                                          }];
    }
}

}  // namespace wakedeck::platform
