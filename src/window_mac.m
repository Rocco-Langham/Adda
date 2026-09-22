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
#include <string.h>
#include "adda.h"

/* one shape: what it is, where it goes, its name if it has one, and the
 * text written in it */
@interface AddaShape : NSObject
@property int kind;
@property (strong) NSArray<NSNumber *> *spec;          /* SHAPE_SPEC numbers */
@property (copy) NSString *name;
@property (strong) NSMutableArray<NSDictionary *> *texts;   /* text, font, location */
@property (strong) NSTextField *field;                     /* a place to type */
@property BOOL fieldLow;                                   /* under a question */
@end
@implementation AddaShape
@end

@interface AddaCanvas : NSView
@property (strong) NSMutableArray<NSString *> *lines;
@property (strong) NSMutableArray<AddaShape *> *shapes;
@end

@interface AddaWatcher : NSObject <NSWindowDelegate>
- (void)entered:(id)sender;
@end

static NSWindow   *g_window;
static AddaCanvas *g_canvas;
static AddaWatcher *g_watcher;
static bool        g_closed;
static bool        g_entered;      /* Enter was pressed in the input box */

/* A font by name, in any capitals - "helvetica" finds Helvetica - or the
 * usual one when there is no such font. */
static NSFont *font_named(NSString *name, CGFloat size)
{
    NSFontManager *fm = NSFontManager.sharedFontManager;
    NSFont *f;

    if (!name.length) return [NSFont systemFontOfSize:size];
    f = [NSFont fontWithName:name size:size];
    if (f) return f;
    for (NSString *family in fm.availableFontFamilies)
        if ([family caseInsensitiveCompare:name] == NSOrderedSame)
            return [fm fontWithFamily:family traits:0 weight:5 size:size];
    for (NSString *ps in fm.availableFonts)
        if ([ps caseInsensitiveCompare:name] == NSOrderedSame)
            return [NSFont fontWithName:ps size:size];
    return [NSFont systemFontOfSize:size];
}

/* Each piece of a shape's text, placed inside it - left, centre or right,
 * top, middle or bottom - with a little room to the edge. Text too tall for
 * its shape is not cut off: it sits across the shape's middle instead. */
static void draw_texts(AddaShape *sh, NSRect box)
{
    NSRect in = NSInsetRect(box, box.size.width > 28 ? 10 : 0, box.size.height > 20 ? 6 : 0);

    if (!sh.texts.count || NSWidth(in) <= 0) return;
    for (NSDictionary *t in sh.texts) {
        int loc = [t[@"location"] intValue], h = loc % 3, v = loc / 3;
        NSMutableParagraphStyle *para = [NSMutableParagraphStyle new];
        NSDictionary *attrs;
        CGFloat th, y;

        para.alignment = h == TEXT_LEFT ? NSTextAlignmentLeft
                       : h == TEXT_RIGHT ? NSTextAlignmentRight : NSTextAlignmentCenter;
        para.lineBreakMode = NSLineBreakByWordWrapping;
        attrs = @{ NSFontAttributeName: t[@"font"],
                   NSForegroundColorAttributeName: NSColor.labelColor,
                   NSParagraphStyleAttributeName: para };
        th = ceil(NSHeight([t[@"text"] boundingRectWithSize:NSMakeSize(NSWidth(in), CGFLOAT_MAX)
                                                   options:NSStringDrawingUsesLineFragmentOrigin
                                                attributes:attrs]));
        y = th > NSHeight(in) ? NSMidY(box) - th / 2          /* too tall: centred */
          : v == TEXT_TOP ? NSMinY(in) : v == TEXT_BOTTOM ? NSMaxY(in) - th
                                                           : NSMidY(in) - th / 2;
        [t[@"text"] drawWithRect:NSMakeRect(NSMinX(in), y, NSWidth(in), th)
                         options:NSStringDrawingUsesLineFragmentOrigin attributes:attrs];
    }
}

/* Where a shape sits in the window, as drawn. */
static NSRect shape_box(AddaShape *sh, NSRect bounds)
{
    double in[SHAPE_SPEC], r[4];
    int k;
    for (k = 0; k < SHAPE_SPEC; k++) in[k] = sh.spec[(NSUInteger)k].doubleValue;
    adda_shape_rect(sh.kind, in, NSWidth(bounds), NSHeight(bounds), r);
    return NSMakeRect(r[0], r[1], r[2], r[3]);
}

/* An input box's field: one line, across the middle of its shape. */
static void place_field(AddaShape *sh, NSRect bounds)
{
    NSRect box = shape_box(sh, bounds);
    CGFloat pad = sh.kind == SHAPE_PILL ? NSHeight(box) / 2 : NSWidth(box) > 40 ? 12 : 2;
    CGFloat h = ceil(sh.field.intrinsicContentSize.height);
    CGFloat mid = sh.fieldLow ? NSMinY(box) + NSHeight(box) * 0.68 : NSMidY(box);
    if (pad * 2 > NSWidth(box) - 10) pad = 5;
    sh.field.frame = NSMakeRect(NSMinX(box) + pad, mid - h / 2, NSWidth(box) - pad * 2, h);
}

@implementation AddaCanvas

- (BOOL)isFlipped { return YES; }

- (void)resizeSubviewsWithOldSize:(NSSize)old
{
    (void)old;
    for (AddaShape *sh in self.shapes)
        if (sh.field) place_field(sh, self.bounds);
}

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
    for (AddaShape *sh in self.shapes) {
        double in[SHAPE_SPEC], r[4], xy[20];
        int k, kind = sh.kind, corners;
        NSRect box;
        NSBezierPath *path;
        for (k = 0; k < SHAPE_SPEC; k++) in[k] = sh.spec[(NSUInteger)k].doubleValue;
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
            draw_texts(sh, box);
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
        draw_texts(sh, box);
        if (sh.field && sh.fieldLow) {              /* where the answer goes */
            NSRect f = sh.field.frame;
            NSBezierPath *u = [NSBezierPath bezierPath];
            CGFloat inset = NSWidth(f) > 80 ? NSWidth(f) * 0.15 : 0;
            [u moveToPoint:NSMakePoint(NSMinX(f) + inset, NSMaxY(f) + 2)];
            [u lineToPoint:NSMakePoint(NSMaxX(f) - inset, NSMaxY(f) + 2)];
            u.lineWidth = 1;
            [[NSColor.labelColor colorWithAlphaComponent:0.35] setStroke];
            [u stroke];
        }
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
- (void)entered:(id)sender { (void)sender; g_entered = true; }
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

void adda_window_shape(int kind, const double spec[SHAPE_SPEC], const char *name)
{
    AddaShape *sh = [AddaShape new];
    NSMutableArray<NSNumber *> *nums = [NSMutableArray array];
    int k;
    for (k = 0; k < SHAPE_SPEC; k++) [nums addObject:@(spec[k])];
    sh.kind = kind;
    sh.spec = nums;
    sh.name = name ? @(name) : nil;
    sh.texts = [NSMutableArray array];
    [g_canvas.shapes addObject:sh];
    g_canvas.needsDisplay = YES;
    [g_canvas displayIfNeeded];
    pump([NSDate date]);
}

bool adda_window_shape_text(const char *name, const char *text, const char *font,
                            double size, int location)
{
    AddaShape *found = nil;
    NSString *want = @(name), *words;

    /* the latest shape with that name, if it was used twice */
    for (AddaShape *sh in g_canvas.shapes)
        if (sh.name && [sh.name caseInsensitiveCompare:want] == NSOrderedSame) found = sh;
    if (!found) return false;

    words = [[NSString alloc] initWithUTF8String:text];
    [found.texts addObject:@{ @"text": words ? words : @"",
                              @"font": font_named(font ? @(font) : nil, (CGFloat)size),
                              @"location": @(location) }];
    g_canvas.needsDisplay = YES;
    [g_canvas displayIfNeeded];
    pump([NSDate date]);
    return true;
}

const char *adda_window_input(const char *name, const char *hint, const char *question)
{
    static char *typed;
    AddaShape *found = nil;
    NSString *want = @(name);
    NSTextField *f;

    for (AddaShape *sh in g_canvas.shapes)       /* the latest shape with that name */
        if (sh.name && [sh.name caseInsensitiveCompare:want] == NSOrderedSame) found = sh;
    if (!found) return NULL;

    /* a question: written at the top of the shape, the answer typed under it -
     * or, in a shape too short for both, shown as the hint */
    if (question && *question) {
        if (NSHeight(shape_box(found, g_canvas.bounds)) >= 70) {
            adda_window_shape_text(name, question, NULL, 17, TEXT_TOP * 3 + TEXT_CENTRE);
            found.fieldLow = YES;
            if (!hint) hint = "Type here";
        } else if (!hint) {
            hint = question;
        }
    }

    f = [NSTextField new];
    f.bezeled = NO;
    f.bordered = NO;
    f.drawsBackground = NO;
    f.focusRingType = NSFocusRingTypeNone;
    f.font = [NSFont systemFontOfSize:18];
    f.textColor = NSColor.labelColor;
    f.alignment = NSTextAlignmentCenter;
    f.placeholderString = hint ? [NSString stringWithUTF8String:hint] : nil;
    f.usesSingleLineMode = YES;
    f.cell.scrollable = YES;
    f.target = g_watcher;
    f.action = @selector(entered:);
    found.field = f;
    [g_canvas addSubview:f];
    place_field(found, g_canvas.bounds);
    [g_window makeKeyAndOrderFront:nil];
    [g_window makeFirstResponder:f];

    g_entered = false;
    while (!g_entered) pump([NSDate dateWithTimeIntervalSinceNow:0.05]);

    /* done: what was typed stays in the box, but it takes no more */
    f.editable = NO;
    f.selectable = NO;
    [g_window makeFirstResponder:nil];
    free(typed);
    typed = strdup(f.stringValue.UTF8String ? f.stringValue.UTF8String : "");
    return typed;
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
