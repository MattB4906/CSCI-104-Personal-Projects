// macOS implementation of the window's system services (Objective-C++, ARC):
// "ready" notifications through UserNotifications and the wait progress on the
// Dock icon through NSDockTile. See src/app/SystemServices.h.
#include "app/SystemServices.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <UserNotifications/UserNotifications.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <algorithm>
#include <utility>

namespace {

// Completion handlers of the notification center run on system threads. They
// hop back to the Qt thread through the application object, which outlives any
// caller; whatever they touch there is checked there, never on the system thread.
void onQtThread(std::function<void()> work) {
    QObject* receiver = QCoreApplication::instance();
    if (receiver == nullptr) return;  // the application is shutting down
    QMetaObject::invokeMethod(receiver, std::move(work), Qt::QueuedConnection);
}

// The Dock picture: the application icon, with a bar across its lower part.
// Shared by the Dock tile's view and the PNG written by --diagnose. The
// coordinates are AppKit's, with the origin at the bottom left.
void drawDockTile(NSRect bounds, double fraction) {
    NSImage* icon = NSApp != nil ? NSApp.applicationIconImage : nil;
    if (icon == nil) icon = [[NSWorkspace sharedWorkspace] iconForFile:[NSBundle mainBundle].bundlePath];
    [icon drawInRect:bounds fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0];

    const CGFloat width = NSWidth(bounds);
    const CGFloat height = NSHeight(bounds);
    const NSRect track = NSMakeRect(NSMinX(bounds) + width * 0.12, NSMinY(bounds) + height * 0.08, width * 0.76, height * 0.12);
    const CGFloat radius = NSHeight(track) / 2.0;
    NSBezierPath* trackPath = [NSBezierPath bezierPathWithRoundedRect:track xRadius:radius yRadius:radius];
    [[NSColor colorWithCalibratedWhite:0.0 alpha:0.7] setFill];
    [trackPath fill];

    const CGFloat inset = std::max<CGFloat>(1.0, height * 0.02);
    const NSRect inner = NSInsetRect(track, inset, inset);
    const double clamped = std::clamp(fraction, 0.0, 1.0);
    if (clamped > 0.0) {
        NSRect filled = inner;
        filled.size.width = std::max<CGFloat>(NSHeight(inner), NSWidth(inner) * static_cast<CGFloat>(clamped));
        const CGFloat innerRadius = NSHeight(inner) / 2.0;
        [[NSColor systemBlueColor] setFill];
        [[NSBezierPath bezierPathWithRoundedRect:filled xRadius:innerRadius yRadius:innerRadius] fill];
    }

    [[NSColor colorWithCalibratedWhite:1.0 alpha:0.9] setStroke];
    trackPath.lineWidth = std::max<CGFloat>(1.0, height * 0.012);
    [trackPath stroke];
}

QString permissionName(UNAuthorizationStatus status) {
    switch (status) {
    case UNAuthorizationStatusNotDetermined: return QStringLiteral("notDetermined");
    case UNAuthorizationStatusDenied: return QStringLiteral("denied");
    case UNAuthorizationStatusAuthorized:
    case UNAuthorizationStatusProvisional: return QStringLiteral("authorized");
    default: return QStringLiteral("unavailable");
    }
}

}  // namespace

// The Dock tile's content view. It draws everything itself, because a progress
// indicator inside a Dock tile is not reliably rendered.
@interface WakeDeckDockProgressView : NSView
@property(nonatomic) double fraction;
@end

@implementation WakeDeckDockProgressView
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    drawDockTile(self.bounds, self.fraction);
}
@end

namespace wakedeck::platform {
namespace {

class SystemNotifier final : public app::Notifier {
public:
    SystemNotifier() : usable_(!applicationBundleIdentifier().isEmpty()) {}

    bool available() const override { return usable_; }

    void notify(const QString& title, const QString& body) override {
        if (!usable_) return;
        @autoreleasepool {
            @try {
                UNUserNotificationCenter* center = [UNUserNotificationCenter currentNotificationCenter];
                NSString* nsTitle = title.toNSString();
                NSString* nsBody = body.toNSString();
                QPointer<app::Notifier> guard(this);
                // Asks only the first time; afterwards it answers with the user's choice.
                [center requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                                      completionHandler:^(BOOL granted, NSError* error) {
                                          (void)error;
                                          if (!granted) {
                                              reportRefused(guard);
                                              return;
                                          }
                                          UNMutableNotificationContent* content = [[UNMutableNotificationContent alloc] init];
                                          content.title = nsTitle;
                                          content.body = nsBody;
                                          content.sound = [UNNotificationSound defaultSound];
                                          UNNotificationRequest* request =
                                              [UNNotificationRequest requestWithIdentifier:[NSUUID UUID].UUIDString
                                                                                   content:content
                                                                                   trigger:nil];
                                          @try {
                                              [center addNotificationRequest:request
                                                       withCompletionHandler:^(NSError* addError) {
                                                           if (addError != nil && [addError.domain isEqualToString:UNErrorDomain] &&
                                                               addError.code == UNErrorCodeNotificationsNotAllowed) {
                                                               reportRefused(guard);
                                                           }
                                                       }];
                                          } @catch (NSException*) {
                                              // Nothing to show; the run's result is in the window anyway.
                                          }
                                      }];
            } @catch (NSException*) {
                usable_ = false;  // the notification center is not usable in this process
            }
        }
    }

private:
    static void reportRefused(QPointer<app::Notifier> guard) {
        onQtThread([guard] {
            if (guard) emit guard->refused();
        });
    }

    bool usable_;
};

class SystemDockProgress final : public app::DockProgress {
public:
    ~SystemDockProgress() override { clear(); }

    void setProgress(double fraction) override {
        @autoreleasepool {
            if (NSApp == nil) return;
            NSDockTile* tile = NSApp.dockTile;
            if (tile == nil) return;
            if (view_ == nil) {
                const NSSize size = tile.size;
                view_ = [[WakeDeckDockProgressView alloc] initWithFrame:NSMakeRect(0, 0, size.width, size.height)];
            }
            view_.fraction = fraction;
            if (tile.contentView != view_) tile.contentView = view_;
            [view_ setNeedsDisplay:YES];
            [tile display];
        }
    }

    void clear() override {
        @autoreleasepool {
            if (NSApp == nil || view_ == nil) return;
            NSDockTile* tile = NSApp.dockTile;
            if (tile != nil && tile.contentView == view_) {
                tile.contentView = nil;  // the default tile: the plain application icon
                [tile display];
            }
        }
    }

private:
    WakeDeckDockProgressView* view_ = nil;
};

}  // namespace

QString applicationBundleIdentifier() {
    @autoreleasepool {
        NSBundle* bundle = [NSBundle mainBundle];
        if (![bundle.bundleURL.pathExtension isEqualToString:@"app"]) return QString();
        NSString* identifier = bundle.bundleIdentifier;
        return identifier.length > 0 ? QString::fromNSString(identifier) : QString();
    }
}

std::unique_ptr<app::Notifier> makeSystemNotifier() {
    return std::make_unique<SystemNotifier>();
}

void readNotificationPermission(std::function<void(const QString& permission)> done) {
    if (applicationBundleIdentifier().isEmpty()) {
        done(QStringLiteral("unavailable"));
        return;
    }
    @autoreleasepool {
        @try {
            [[UNUserNotificationCenter currentNotificationCenter]
                getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings* settings) {
                    const QString permission = permissionName(settings.authorizationStatus);
                    onQtThread([done, permission] { done(permission); });
                }];
        } @catch (NSException*) {
            done(QStringLiteral("unavailable"));
        }
    }
}

std::unique_ptr<app::DockProgress> makeSystemDockProgress() {
    return std::make_unique<SystemDockProgress>();
}

bool dockTileAvailable() {
    return NSApp != nil && NSApp.dockTile != nil;
}

bool renderDockProgressPicture(double fraction, const QString& pngPath, QString* error) {
    @autoreleasepool {
        const NSInteger side = 256;
        NSBitmapImageRep* bitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                                                           pixelsWide:side
                                                                           pixelsHigh:side
                                                                        bitsPerSample:8
                                                                      samplesPerPixel:4
                                                                             hasAlpha:YES
                                                                             isPlanar:NO
                                                                       colorSpaceName:NSDeviceRGBColorSpace
                                                                          bytesPerRow:0
                                                                         bitsPerPixel:0];
        NSGraphicsContext* context = bitmap != nil ? [NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap] : nil;
        if (context == nil) {
            if (error) *error = QStringLiteral("cannot create a drawing surface");
            return false;
        }
        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext:context];
        drawDockTile(NSMakeRect(0, 0, side, side), fraction);
        [context flushGraphics];
        [NSGraphicsContext restoreGraphicsState];

        NSData* png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        NSError* writeError = nil;
        if (png == nil || ![png writeToFile:pngPath.toNSString() options:NSDataWritingAtomic error:&writeError]) {
            if (error) {
                *error = writeError != nil ? QString::fromNSString(writeError.localizedDescription)
                                           : QStringLiteral("cannot encode the picture");
            }
            return false;
        }
        return true;
    }
}

}  // namespace wakedeck::platform
