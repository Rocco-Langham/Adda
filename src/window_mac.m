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
@property (strong) NSMutableArray<NSArray<NSNumber *> *> *shapes;   /* kind, 4 insets */
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

    /* shapes first, so the text sits on top of them */
    for (NSArray<NSNumber *> *sh in self.shapes) {
        double in[4], r[4], xy[20];
        int k, kind = sh[0].intValue, corners;
        NSRect box;
        NSBezierPath *path;
        for (k = 0; k < 4; k++) in[k] = sh[(NSUInteger)k + 1].doubleValue;
        adda_shape_rect(kind, in, NSWidth(self.bounds), NSHeight(self.bounds), r);
        box = NSMakeRect(r[0], r[1], r[2], r[3]);

        corners = adda_shape_points(kind, r, xy);
        if (corners) {
            path = [NSBezierPath bezierPath];
            [path moveToPoint:NSMakePoint(xy[0], xy[1])];
            for (k = 1; k < corners; k++) [path lineToPoint:NSMakePoint(xy[2 * k], xy[2 * k + 1])];
            [path closePath];
            path.lineJoinStyle = NSLineJoinStyleRound;
        } else if (kind == SHAPE_LINE) {
            path = [NSBezierPath bezierPath];       /* corner to corner of its space */
            [path moveToPoint:NSMakePoint(r[0], r[1])];
            [path lineToPoint:NSMakePoint(r[0] + r[2], r[1] + r[3])];
            path.lineWidth = 2;
            path.lineCapStyle = NSLineCapStyleRound;
            [[NSColor.labelColor colorWithAlphaComponent:0.55] setStroke];
            [path stroke];
            continue;
        } else if (kind == SHAPE_CIRCLE || kind == SHAPE_OVAL) {
            if (kind == SHAPE_CIRCLE) {             /* the biggest circle that fits */
                CGFloat d = r[2] < r[3] ? r[2] : r[3];
                box = NSMakeRect(r[0] + (r[2] - d) / 2, r[1] + (r[3] - d) / 2, d, d);
            }
            path = [NSBezierPath bezierPathWithOvalInRect:box];
        } else {
            CGFloat radius = kind == SHAPE_ROUNDED_BOX ? 12
                           : kind == SHAPE_PILL ? (r[2] < r[3] ? r[2] : r[3]) / 2 : 0;
            if (radius * 2 > r[2]) radius = r[2] / 2;
            if (radius * 2 > r[3]) radius = r[3] / 2;
            path = [NSBezierPath bezierPathWithRoundedRect:box xRadius:radius yRadius:radius];
        }
        [[NSColor.labelColor colorWithAlphaComponent:0.10] setFill];
        [path fill];
        [[NSColor.labelColor colorWithAlphaComponent:0.30] setStroke];
        path.lineWidth = 1;
        [path stroke];
    }
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
    g_canvas.shapes = [NSMutableArray array];
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

void adda_window_shape(int kind, const double inset[4])
{
    [g_canvas.shapes addObject:@[ @(kind), @(inset[0]), @(inset[1]), @(inset[2]), @(inset[3]) ]];
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
