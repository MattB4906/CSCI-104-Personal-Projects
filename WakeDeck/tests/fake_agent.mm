// A stand-in for a client application in test_launcher: an AppKit process with
// no window, no Dock icon and no menu bar (the test's bundle marks it
// LSBackgroundOnly, and it prohibits activation itself). It checks in with the
// system like a real application, so NSWorkspace reports its process, and it
// ends by itself after a few seconds.
#import <AppKit/AppKit.h>

#include <cstdlib>

int main() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        [NSTimer scheduledTimerWithTimeInterval:8
                                        repeats:NO
                                          block:^(NSTimer*) {
                                              std::exit(0);
                                          }];
        [NSApp run];
    }
    return 0;
}
