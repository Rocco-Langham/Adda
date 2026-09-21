/* openApplication on a Mac: a window whose printed lines sit, as one block,
 * in the dead centre. The program keeps running while it is open; its events
 * are handled whenever the program prints or waits, and once the program is
 * finished the window stays up until it is closed. Closing it early ends the
 * program, as closing an app does.
 *
 * The interpreter is a plain command-line tool, so it becomes a proper app
 * (a Dock icon, able to come to the front) only once a window is opened. */
#import <Cocoa/Cocoa.h>
#include <stdio.h>
#include <stdlib.h>
#include "adda.h"

@interface AddaCanvas : NSView
@property (strong) NSMutableArray<NSString *> *lines;
@end

@interface AddaWatcher : NSObject <NSWindowDelegate>
@end

static NSWindow   *g_window;
static AddaCanvas *g_canvas;
static AddaWatcher *g_watcher;
static bool        g_closed;

@implementation AddaCanvas

- (BOOL)isFlipped { return YES; }

- (void)drawRect:(NSRect)dirty
{
    NSDictionary *attrs;
    NSMutableParagraphStyle *para = [NSMutableParagraphStyle new];
    NSString *all = [self.lines componentsJoinedByString:@"\n"];
    NSRect box;
    NSSize size;

    (void)dirty;
    [NSColor.windowBackgroundColor setFill];
    NSRectFill(self.bounds);
    if (!all.length) return;

    para.alignment = NSTextAlignmentCenter;
    attrs = @{ NSFontAttributeName: [NSFont systemFontOfSize:20],
               NSForegroundColorAttributeName: NSColor.labelColor,
               NSParagraphStyleAttributeName: para };

    /* measure the whole block, then put its middle on the window's middle */
    box = [all boundingRectWithSize:NSMakeSize(NSWidth(self.bounds) - 40, CGFLOAT_MAX)
                            options:NSStringDrawingUsesLineFragmentOrigin
                         attributes:attrs];
    size = box.size;
    [all drawWithRect:NSMakeRect(20, (NSHeight(self.bounds) - size.height) / 2,
                                 NSWidth(self.bounds) - 40, size.height)
              options:NSStringDrawingUsesLineFragmentOrigin
           attributes:attrs];
}

@end

@implementation AddaWatcher
- (void)windowWillClose:(NSNotification *)n { (void)n; g_closed = true; }
@end

/* Handles whatever has happened to the window - clicks, resizes, redraws -
 * waiting up to `until` for something to arrive. */
static void pump(NSDate *until)
{
    for (;;) {
        NSEvent *e = [NSApp nextEventMatchingMask:NSEventMaskAny
                                        untilDate:until
                                           inMode:NSDefaultRunLoopMode
                                          dequeue:YES];
        if (!e) break;
        [NSApp sendEvent:e];
        if (g_closed) break;
    }
    if (g_closed) {                 /* the window was closed: so is the program */
        fflush(stdout);
        exit(0);
    }
}

bool adda_open_window(const char *title)
{
    NSString *t = title ? [NSString stringWithUTF8String:title] : nil;

    if (g_window) {                 /* a second openApplication just renames it */
        if (t.length) g_window.title = t;
        return true;
    }

    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];

    g_window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 640, 420)
                                           styleMask:NSWindowStyleMaskTitled |
                                                     NSWindowStyleMaskClosable |
                                                     NSWindowStyleMaskMiniaturizable |
                                                     NSWindowStyleMaskResizable
                                             backing:NSBackingStoreBuffered
                                               defer:NO];
    if (!g_window) return false;
    g_canvas = [[AddaCanvas alloc] initWithFrame:NSMakeRect(0, 0, 640, 420)];
    g_canvas.lines = [NSMutableArray array];
    g_watcher = [AddaWatcher new];

    g_window.title = t.length ? t : @"Adda";
    g_window.releasedWhenClosed = NO;
    g_window.delegate = g_watcher;
    g_window.contentView = g_canvas;
    [g_window center];

    [NSApp activateIgnoringOtherApps:YES];
    [g_window makeKeyAndOrderFront:nil];
    pump([NSDate date]);
    return true;
}

bool adda_window_is_open(void) { return g_window != nil; }

void adda_window_print(const char *text, size_t len)
{
    NSString *s = [[NSString alloc] initWithBytes:text length:len encoding:NSUTF8StringEncoding];
    [g_canvas.lines addObject:s ? s : @""];
    g_canvas.needsDisplay = YES;
    [g_canvas displayIfNeeded];
    pump([NSDate date]);
}

void adda_window_wait_ms(double ms)
{
    NSDate *end = [NSDate dateWithTimeIntervalSinceNow:ms / 1000.0];
    while ([end timeIntervalSinceNow] > 0) pump(end);
}

void adda_window_run(void)
{
    while (g_window) pump([NSDate distantFuture]);
}
