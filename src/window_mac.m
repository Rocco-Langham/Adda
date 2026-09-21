/* openApplication on a Mac: a blank window, and the program waits until it
 * is closed. The interpreter is a plain command-line tool, so it becomes a
 * proper app (a Dock icon, able to come to the front) only for as long as
 * the window is up. */
#import <Cocoa/Cocoa.h>
#include "adda.h"

@interface AddaBlankWindow : NSObject <NSWindowDelegate>
@end

@implementation AddaBlankWindow
- (void)windowWillClose:(NSNotification *)n { (void)n; [NSApp stopModal]; }
@end

bool adda_open_window(const char *title)
{
    @autoreleasepool {
        NSWindow *w;
        AddaBlankWindow *watch = [AddaBlankWindow new];
        NSString *t = title ? [NSString stringWithUTF8String:title] : nil;

        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        w = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 640, 420)
                                        styleMask:NSWindowStyleMaskTitled |
                                                  NSWindowStyleMaskClosable |
                                                  NSWindowStyleMaskMiniaturizable |
                                                  NSWindowStyleMaskResizable
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
        if (!w) return false;
        w.title = t.length ? t : @"Adda";
        w.releasedWhenClosed = NO;
        w.delegate = watch;
        [w center];

        [NSApp activateIgnoringOtherApps:YES];
        [w makeKeyAndOrderFront:nil];
        [NSApp runModalForWindow:w];          /* until the close button */

        w.delegate = nil;
        [w orderOut:nil];
        /* back to a command-line tool: no Dock icon for the rest of the run */
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
    }
    return true;
}
