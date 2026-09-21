/*
 *  Adda GUI — a Cocoa front-end for the Adda interpreter, and the Mac twin of
 *  gui.c. Same window, same behaviour; where the two differ it is because the
 *  Mac has its own way (Cmd for Ctrl, the Trash for the Recycle Bin, full
 *  screen from the green button).
 *
 *  Build:  build.sh on a Mac makes Adda.app, with adda inside it
 *  Open:   double-click Adda.app, or  open Adda.app
 *
 *  Shape of the window, VS Code fashion:
 *
 *      +--------+-------------+---------------------------+
 *      | icons  | side panel  |                           |
 *      |        | (Explorer   +---------------------------+
 *      | run    |  or Search) | code editor               |
 *      | stop   |             |===== drag to resize ======|
 *      |        |             | console                   |
 *      | book   |             |                           |
 *      | cog    |             |                           |
 *      +--------+-------------+---------------------------+
 *
 *  The console is one text view that shows the program's output AND takes
 *  what you type: everything before a moving anchor is fixed, everything
 *  after it is the line you are typing. Return sends it down the pipe.
 *
 *  No nib and no Xcode project, so the command line tools are enough to build
 *  it. The icons are drawn with NSBezierPath, and the activity bar, the panel
 *  title and the frames are painted by the window's own content view.
 */

#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <spawn.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "gui_cheatsheet.h"

extern char **environ;

/* key codes, from HIToolbox/Events.h */
#define KEY_RETURN   36
#define KEY_ENTER    76         /* the one on the number pad */
#define KEY_DELETE   51         /* backspace, as the Mac labels it */
#define KEY_FWD_DEL  117
#define KEY_ESCAPE   53
#define KEY_F2       120

/* ── themes ──────────────────────────────────────────────────────── */
#define RGB(r, g, b) (((unsigned)(r) << 16) | ((unsigned)(g) << 8) | (unsigned)(b))

typedef struct {
    unsigned bg;          /* window and side panel                   */
    unsigned surface;     /* code and console panels                 */
    unsigned border;      /* hairlines                               */
    unsigned text;        /* code and console text                   */
    unsigned muted;       /* labels, descriptions                    */
    unsigned accent;      /* the third chip in Settings              */
    unsigned accentHot;
    unsigned accentDown;
    unsigned onAccent;
    unsigned ghostHot;    /* hover fill for quiet rows               */
    unsigned sel;         /* selected row                            */
    unsigned abBg;        /* activity bar                            */
    unsigned abHover;     /* activity bar, pointer over an icon      */
    unsigned abIcon;      /* active icon                             */
    unsigned abIconDim;   /* inactive icon                           */
    BOOL     dark;
} Theme;

enum { PICK_SYSTEM = 0, PICK_LIGHT, PICK_DARK, PICK_BEIGE, PICK_ABYSS, PICK_COUNT };

static const char *THEME_NAMES[PICK_COUNT] = {
    "Follow macOS", "Light", "Dark", "Beige", "Abyss"
};
static const char *THEME_ABOUT[PICK_COUNT] = {
    "Match whatever macOS is set to",
    "White panels on soft grey",
    "Dark grey panels, light text",
    "The original warm paper",
    "Deep blue night, after the VS Code theme"
};

/* the colours are gui.c's, value for value */
static const Theme THEME_LIGHT_V = {
    RGB(243,243,243), RGB(255,255,255), RGB(216,216,216),
    RGB(26,26,26),    RGB(97,97,97),
    RGB(0,103,192),   RGB(25,117,197), RGB(0,88,158), RGB(255,255,255),
    RGB(232,232,232), RGB(204,228,247),
    RGB(44,44,44),    RGB(60,60,60),   RGB(255,255,255), RGB(122,122,122),
    NO
};

static const Theme THEME_DARK_V = {
    RGB(32,32,32),    RGB(43,43,43),   RGB(61,61,61),
    RGB(232,232,232), RGB(160,160,160),
    RGB(0,120,212),   RGB(26,140,232), RGB(0,95,168), RGB(255,255,255),
    RGB(58,58,58),    RGB(4,57,94),
    RGB(24,24,24),    RGB(40,40,40),   RGB(255,255,255), RGB(134,134,134),
    YES
};

static const Theme THEME_BEIGE_V = {
    RGB(236,232,208), RGB(245,245,220), RGB(206,200,172),
    RGB(30,30,30),    RGB(110,104,80),
    RGB(140,94,42),   RGB(160,110,52), RGB(118,78,34), RGB(255,255,255),
    RGB(226,220,192), RGB(220,212,176),
    RGB(74,68,54),    RGB(92,85,68),   RGB(245,241,224), RGB(154,144,120),
    NO
};

static const Theme THEME_ABYSS_V = {
    RGB(0x06,0x06,0x21), RGB(0x00,0x0c,0x18), RGB(0x2b,0x2b,0x4a),
    RGB(0x66,0x88,0xcc), RGB(0x91,0x91,0x99),
    RGB(0x2b,0x3c,0x5d), RGB(0x3a,0x4e,0x75), RGB(0x22,0x30,0x4a), RGB(255,255,255),
    RGB(0x06,0x19,0x40), RGB(0x08,0x28,0x6b),
    RGB(0x05,0x13,0x36), RGB(0x08,0x28,0x6b), RGB(255,255,255), RGB(0x69,0x71,0x86),
    YES
};

static int   g_pick = PICK_SYSTEM;
static Theme g_t;

/* ── classes ─────────────────────────────────────────────────────── */
@interface MainView : NSView
@end

/* the code box: draws the whole-line yellow behind lines with a mistake */
@interface CodeView : NSTextView
@end

@interface ConsoleView : NSTextView
@end

@interface FileTable : NSOutlineView
@end

@interface CheatTable : NSTableView
@end

/* the strip of line numbers down the left of the code box */
@interface LineNumbers : NSRulerView
@end

@interface ThemedRow : NSTableRowView
@end

@interface Cell : NSTableCellView
@property (strong) NSTextField *sub;     /* the second line, cheat sheet only */
@end

@interface SettingsView : NSView
@end

@interface CheatsView : NSView
@end

@interface Popup : NSPanel
@end

@interface Adda : NSObject <NSApplicationDelegate, NSWindowDelegate,
                            NSTextViewDelegate, NSTextFieldDelegate,
                            NSTableViewDataSource, NSTableViewDelegate,
                            NSOutlineViewDataSource, NSOutlineViewDelegate,
                            NSMenuDelegate, NSMenuItemValidation>
@end

/* ── windows ─────────────────────────────────────────────────────── */
static NSWindow     *g_win;
static MainView     *g_main;
static NSScrollView *g_codeScroll, *g_consoleScroll, *g_filesScroll;
static NSTextView   *g_code;
static ConsoleView  *g_console;
static NSTextField  *g_find;
static FileTable    *g_files;
static Popup        *g_settings, *g_cheats;
static SettingsView *g_settingsView;
static CheatsView   *g_cheatsView;
static NSTextField  *g_cheatFind;
static CheatTable   *g_cheatList;
static Adda         *g_adda;

static NSFont *g_fontMono, *g_fontUI, *g_fontUIBold, *g_fontSmall;

/* ── activity bar ────────────────────────────────────────────────── */
enum { ICON_EXPLORER, ICON_SEARCH, ICON_PLAY, ICON_STOP, ICON_CHEAT, ICON_GEAR, ICON_PLUS,
       ICON_SAVE, ICON_IMPORT, ICON_CHECK, ICON_NEW };
/* Run and Stop sit under Search; Cheat sheet and Settings are pinned to the
 * bottom. Everything before AB_CHEAT stacks from the top. */
enum { AB_EXPLORER = 0, AB_SEARCH, AB_NEW, AB_RUN, AB_STOP, AB_CHECK, AB_CHEAT, AB_GEAR, AB_COUNT };

static const int AB_ICON[AB_COUNT] = {
    ICON_EXPLORER, ICON_SEARCH, ICON_NEW, ICON_PLAY, ICON_STOP, ICON_CHECK, ICON_CHEAT, ICON_GEAR
};

/* addToolTipRect keeps no reference to its owner, so these must be literals */
static NSString *const AB_TIP[AB_COUNT] = {
    @"Explorer", @"Search", @"New Project", @"Run  ⌘R", @"Stop  ⌘.", @"Check for mistakes", @"Cheat sheet", @"Settings"
};

static int    g_view = AB_EXPLORER;   /* which panel view, or -1 when collapsed */
static int    g_abHot = -1;           /* activity item under the pointer */
static NSRect g_abRect[AB_COUNT];

/* ── panes ───────────────────────────────────────────────────────── */
static double  g_split = 0.52;        /* share of the editor area given to code */
static BOOL    g_dragging = NO;
static CGFloat g_dragDY = 0;          /* grab offset, so the bar does not jump */
static NSRect  g_splitRect;
static NSRect  g_splitHit;            /* the whole gap between code and console */
static NSRect  g_panelRect;
static NSRect  g_findBox;             /* the frame drawn round the find field */

/* ── the running program ─────────────────────────────────────────── */
static pid_t          g_pid;
static int            g_outFd = -1;   /* its stdout and stderr, one pipe */
static int            g_inFd = -1;
static BOOL           g_running = NO;
static NSString      *g_tmpFile;
static NSUInteger     g_anchor = 0;   /* where the typed line starts */
static NSTimer       *g_poll;
static NSMutableData *g_carry;        /* the front half of a split character */
static NSMutableData *g_inq;          /* typed input the pipe has not taken yet */

/* ── files in the Explorer ───────────────────────────────────────── */
/* The tree is not held as one structure: each directory's immediate children
 * (subfolders, then .adda files, each alphabetical) are scanned on demand and
 * kept here, keyed by that directory's path. NSOutlineView asks for a level
 * at a time, and an NSString path serves as the item itself - two paths that
 * read the same ARE the same row, which is what lets a plain reloadData keep
 * whichever folders are open. */
static NSString *g_home;              /* the Explorer's root: beside the app, or a new project */
static NSMutableDictionary<NSString *, NSArray<NSString *> *> *g_treeCache;
static NSString *g_renamePath;        /* non-nil while a row is being renamed */
static BOOL      g_renameCommit;
static BOOL      g_quiet;             /* selecting from code, not by the user */

/* the Explorer's "new file or folder" button, top right of the panel */
static NSRect g_addRect;
static BOOL   g_addHot;

/* the Save and Import buttons along the bottom of the Explorer */
enum { BAR_SAVE, BAR_IMPORT, BAR_COUNT };
static NSRect g_barRect[BAR_COUNT];
static int    g_barHot = -1;

/* ── cheat sheet filtering ───────────────────────────────────────── */
static int g_cheatShown[CHEAT_COUNT];
static int g_cheatCount;

static void apply_theme(void);
static void layout(void);
static void open_settings(void);
static void open_cheats(void);
static void refresh_cheats(void);
static void insert_cheat(NSInteger shownIndex);
static void run_code(void);
static void check_code(void);
static void new_project(void);
static void clear_check(void);
static void stop_code(void);
static void send_line(void);
static void console_clear_pending(void);

/* ════════════════════════════════════════════════════════ theme ══ */

static NSColor *col(unsigned rgb)
{
    return [NSColor colorWithSRGBRed:((rgb >> 16) & 0xff) / 255.0
                               green:((rgb >> 8) & 0xff) / 255.0
                                blue:(rgb & 0xff) / 255.0
                               alpha:1.0];
}

/* halfway between two colours */
static unsigned mix(unsigned a, unsigned b)
{
    return RGB((((a >> 16) & 0xff) + ((b >> 16) & 0xff)) / 2,
               (((a >> 8) & 0xff) + ((b >> 8) & 0xff)) / 2,
               ((a & 0xff) + (b & 0xff)) / 2);
}

static unsigned blend(unsigned a, unsigned b, double t)
{
    int ar = (a >> 16) & 0xff, ag = (a >> 8) & 0xff, ab = a & 0xff;
    int br = (b >> 16) & 0xff, bg = (b >> 8) & 0xff, bb = b & 0xff;
    return RGB((int)(ar + (br - ar) * t), (int)(ag + (bg - ag) * t),
               (int)(ab + (bb - ab) * t));
}

/* Click animation: whatever was pressed last shrinks a little and flashes
 * toward the accent colour, then eases back over PRESS_SECS. One timer
 * drives every view and stops itself once nothing is moving. */
#define PRESS_SECS  0.22
enum { PRESS_NONE, PRESS_AB, PRESS_ADD, PRESS_ROW, PRESS_BAR };
static int      g_pressKind = PRESS_NONE;
static int      g_pressIdx;
static CFTimeInterval g_pressAt;
static NSTimer *g_anim;

/* 1 at the moment of the click, easing to 0 as it finishes */
static double press_amount(int kind, int idx)
{
    double t;
    if (g_pressKind != kind || g_pressIdx != idx) return 0;
    t = (CACurrentMediaTime() - g_pressAt) / PRESS_SECS;
    if (t >= 1) return 0;
    t = 1 - t;
    return t * t;
}

static void press(int kind, int idx);
static void animate(void);

/* Hover: each button's highlight fades in and out over HOVER_SECS rather
 * than snapping. 0 is untouched, 1 fully lit; the same timer as the click
 * animation walks each one toward where the pointer says it should be. */
#define HOVER_SECS  0.12
static double g_abGlow[AB_COUNT];
static double g_barGlow[BAR_COUNT];
static double g_addGlow;
static double g_rowGlow[PICK_COUNT];

static BOOL system_is_dark(void)
{
    NSAppearanceName best = [NSApp.effectiveAppearance bestMatchFromAppearancesWithNames:
                                @[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]];
    return [best isEqualToString:NSAppearanceNameDarkAqua];
}

/* Kept in the app's defaults, where Windows uses the registry. A theme never
 * picked reads back as 0, which is Follow macOS. */
static void load_pick(void)
{
    NSInteger v = [NSUserDefaults.standardUserDefaults integerForKey:@"Theme"];
    if (v < 0 || v >= PICK_COUNT) v = PICK_SYSTEM;
    g_pick = (int)v;
}

static void save_pick(void)
{
    [NSUserDefaults.standardUserDefaults setInteger:g_pick forKey:@"Theme"];
}

static Theme theme_for(int pick)
{
    switch (pick) {
        case PICK_LIGHT: return THEME_LIGHT_V;
        case PICK_DARK:  return THEME_DARK_V;
        case PICK_BEIGE: return THEME_BEIGE_V;
        case PICK_ABYSS: return THEME_ABYSS_V;
        default:         return system_is_dark() ? THEME_DARK_V : THEME_LIGHT_V;
    }
}

/* The title bar is transparent, so it takes the window's background colour,
 * and the appearance decides whether its title and scroll bars are dark. */
static void style_window(NSWindow *w, NSAppearance *look, unsigned bg)
{
    if (!w) return;
    w.appearance = look;
    w.backgroundColor = col(bg);
}

static void style_text(NSTextView *tv)
{
    if (!tv) return;
    tv.backgroundColor = col(g_t.surface);
    tv.enclosingScrollView.backgroundColor = col(g_t.surface);
    tv.font = g_fontMono;
    tv.textColor = col(g_t.text);
    tv.insertionPointColor = col(g_t.text);
    tv.typingAttributes = @{ NSFontAttributeName: g_fontMono,
                             NSForegroundColorAttributeName: col(g_t.text) };
    tv.selectedTextAttributes = @{ NSBackgroundColorAttributeName: col(g_t.sel),
                                   NSForegroundColorAttributeName: col(g_t.text) };
}

static void reload_quietly(NSTableView *tv)
{
    g_quiet = YES;
    [tv reloadData];
    /* autoresizesOutlineColumn (and NSTableViewLastColumnOnlyAutoresizingStyle
     * generally) only re-fits the column when the table view's own FRAME
     * changes; a reload with the frame untouched leaves the column at
     * whatever narrow width it last had, and every row's label clips down to
     * a sliver. This is the reload's half of keeping it full width - the
     * other half is sizing it once at creation. */
    [tv sizeLastColumnToFit];
    g_quiet = NO;
}

static void apply_theme(void)
{
    NSAppearance *look = nil;       /* nil follows the system */

    g_t = theme_for(g_pick);
    if (g_pick != PICK_SYSTEM)
        look = [NSAppearance appearanceNamed:g_t.dark ? NSAppearanceNameDarkAqua
                                                      : NSAppearanceNameAqua];

    style_window(g_win, look, g_t.bg);
    style_window(g_settings, look, g_t.surface);
    style_window(g_cheats, look, g_t.surface);

    style_text(g_code);
    style_text(g_console);
    g_codeScroll.verticalRulerView.needsDisplay = YES;

    g_find.textColor = col(g_t.text);
    g_find.backgroundColor = col(g_t.bg);

    g_files.backgroundColor = col(g_t.bg);
    g_filesScroll.backgroundColor = col(g_t.bg);
    if (!g_renamePath) reload_quietly(g_files);   /* recolours the rows */

    if (g_cheatFind) {
        g_cheatFind.textColor = col(g_t.text);
        g_cheatFind.backgroundColor = col(g_t.surface);
        g_cheatList.backgroundColor = col(g_t.surface);
        g_cheatList.enclosingScrollView.backgroundColor = col(g_t.surface);
        [g_cheatList reloadData];
    }

    g_main.needsDisplay = YES;
    g_settingsView.needsDisplay = YES;
    g_cheatsView.needsDisplay = YES;
}

/* ════════════════════════════════════════════════════════ fonts ══ */

static void build_fonts(void)
{
    g_fontMono   = [NSFont monospacedSystemFontOfSize:13 weight:NSFontWeightRegular];
    g_fontUI     = [NSFont systemFontOfSize:13];
    g_fontUIBold = [NSFont systemFontOfSize:13 weight:NSFontWeightSemibold];
    g_fontSmall  = [NSFont systemFontOfSize:11];
}

/* ════════════════════════════════════════════════════════ icons ══ */

/* Quartz antialiases on its own, so unlike gui.c there is no drawing at 4x.
 * The shapes are the same, on the same 0..100 grid. */

static NSPoint NP(NSRect b, CGFloat nx, CGFloat ny)   /* 0..100 -> the box */
{
    return NSMakePoint(NSMinX(b) + nx * NSWidth(b) / 100.0,
                       NSMinY(b) + ny * NSHeight(b) / 100.0);
}

static NSBezierPath *shape(NSRect b, const CGFloat *xy, int n, BOOL closed)
{
    NSBezierPath *p = [NSBezierPath bezierPath];
    int i;

    [p moveToPoint:NP(b, xy[0], xy[1])];
    for (i = 1; i < n; i++) [p lineToPoint:NP(b, xy[2 * i], xy[2 * i + 1])];
    if (closed) [p closePath];
    return p;
}

/* Fills with the cell colour first, as GDI's Polygon does, so the front page
 * of the Explorer icon hides the back one. */
static void ink(NSBezierPath *p, CGFloat stroke, NSColor *fg, NSColor *bg, BOOL fill)
{
    p.lineWidth = stroke;
    p.lineJoinStyle = NSLineJoinStyleRound;
    p.lineCapStyle = NSLineCapStyleRound;
    if (fill) { [bg setFill]; [p fill]; }
    [fg setStroke];
    [p stroke];
}

static void draw_icon(NSRect box, int kind, unsigned fgc, unsigned bgc)
{
    CGFloat side = NSWidth(box) < NSHeight(box) ? NSWidth(box) : NSHeight(box);
    NSRect b = NSMakeRect(NSMidX(box) - side / 2, NSMidY(box) - side / 2, side, side);
    CGFloat stroke = side / 13 < 1.5 ? 1.5 : side / 13;
    NSColor *fg = col(fgc), *bg = col(bgc);

    if (side <= 0) return;

    switch (kind) {

    case ICON_EXPLORER: {
        /* two pages, the front one overlapping the back, each with a folded
         * corner - recognisably "files" without copying anyone's icon */
        static const CGFloat back[]  = { 38,10, 70,10, 84,24, 84,64, 38,64 };
        static const CGFloat front[] = { 16,32, 50,32, 64,46, 64,90, 16,90 };
        static const CGFloat fold[]  = { 50,32, 50,46, 64,46 };
        ink(shape(b, back, 5, YES), stroke, fg, bg, YES);
        ink(shape(b, front, 5, YES), stroke, fg, bg, YES);
        ink(shape(b, fold, 3, NO), stroke, fg, bg, NO);
        break;
    }

    case ICON_SEARCH: {
        static const CGFloat handle[] = { 60,60, 86,86 };
        NSRect ring = NSMakeRect(NSMinX(b) + side * 0.14, NSMinY(b) + side * 0.14,
                                 side * 0.52, side * 0.52);
        ink([NSBezierPath bezierPathWithOvalInRect:ring], stroke, fg, bg, YES);
        ink(shape(b, handle, 2, NO), stroke * 1.5, fg, bg, NO);
        break;
    }

    case ICON_PLAY: {
        /* a play triangle, nudged right of centre so it looks centred -
         * a triangle's visual weight sits towards its flat side */
        static const CGFloat tri[] = { 30,18, 82,50, 30,82 };
        ink(shape(b, tri, 3, YES), stroke, fg, bg, YES);
        break;
    }

    case ICON_STOP: {
        NSRect sq = NSMakeRect(NSMinX(b) + side * 0.24, NSMinY(b) + side * 0.24,
                               side * 0.52, side * 0.52);
        ink([NSBezierPath bezierPathWithRoundedRect:sq xRadius:side / 20 yRadius:side / 20],
            stroke, fg, bg, YES);
        break;
    }

    case ICON_CHEAT: {
        /* an open book: two leaves meeting at a spine */
        static const CGFloat left[]  = { 10,24, 46,16, 46,80, 10,88 };
        static const CGFloat right[] = { 90,24, 54,16, 54,80, 90,88 };
        static const CGFloat spine[] = { 50,18, 50,84 };
        ink(shape(b, left, 4, YES), stroke, fg, bg, YES);
        ink(shape(b, right, 4, YES), stroke, fg, bg, YES);
        ink(shape(b, spine, 2, NO), stroke, fg, bg, NO);
        break;
    }

    case ICON_GEAR: {
        /* 8 teeth: each contributes 4 points, stepping inner-outer-outer-inner */
        const CGFloat outer = 0.46, inner = 0.33;
        const CGFloat step = 2.0 * M_PI / 8.0;
        CGFloat cx = NSMidX(b), cy = NSMidY(b), hole = side / 7;
        NSBezierPath *teeth = [NSBezierPath bezierPath];
        int i, k;

        for (i = 0; i < 8; i++) {
            CGFloat base = i * step;
            CGFloat a[4] = { base - step * 0.30, base - step * 0.16,
                             base + step * 0.16, base + step * 0.30 };
            CGFloat r[4] = { inner, outer, outer, inner };
            for (k = 0; k < 4; k++) {
                NSPoint pt = NSMakePoint(cx + cos(a[k]) * r[k] * side,
                                         cy + sin(a[k]) * r[k] * side);
                if (i == 0 && k == 0) [teeth moveToPoint:pt];
                else                  [teeth lineToPoint:pt];
            }
        }
        [teeth closePath];
        ink(teeth, stroke, fg, bg, YES);
        ink([NSBezierPath bezierPathWithOvalInRect:
                NSMakeRect(cx - hole, cy - hole, hole * 2, hole * 2)],
            stroke, fg, bg, YES);
        break;
    }

    case ICON_SAVE: {
        /* a floppy disk: the body with a clipped corner, the shutter at the
         * top and the label panel at the bottom */
        static const CGFloat body[]  = { 18,14, 72,14, 86,28, 86,86, 18,86 };
        static const CGFloat shut[]  = { 32,14, 32,36, 64,36, 64,14 };
        static const CGFloat label[] = { 30,86, 30,60, 74,60, 74,86 };
        ink(shape(b, body, 5, YES), stroke, fg, bg, YES);
        ink(shape(b, shut, 4, NO), stroke, fg, bg, NO);
        ink(shape(b, label, 4, NO), stroke, fg, bg, NO);
        break;
    }

    case ICON_IMPORT: {
        /* an arrow coming down into a tray */
        static const CGFloat tray[]  = { 14,58, 14,86, 86,86, 86,58 };
        static const CGFloat shaft[] = { 50,12, 50,64 };
        static const CGFloat head[]  = { 32,46, 50,64, 68,46 };
        ink(shape(b, tray, 4, NO), stroke, fg, bg, NO);
        ink(shape(b, shaft, 2, NO), stroke, fg, bg, NO);
        ink(shape(b, head, 3, NO), stroke, fg, bg, NO);
        break;
    }

    case ICON_NEW: {
        /* a folder with a plus on it */
        static const CGFloat folder[] = { 10,24, 38,24, 46,34, 90,34, 90,82, 10,82 };
        static const CGFloat v[] = { 50,46, 50,72 };
        static const CGFloat h[] = { 37,59, 63,59 };
        ink(shape(b, folder, 6, YES), stroke, fg, bg, YES);
        ink(shape(b, v, 2, NO), stroke, fg, bg, NO);
        ink(shape(b, h, 2, NO), stroke, fg, bg, NO);
        break;
    }

    case ICON_CHECK: {
        /* a warning triangle with a ! in it */
        static const CGFloat tri[]  = { 50,10, 92,86, 8,86 };
        static const CGFloat bang[] = { 50,36, 50,60 };
        CGFloat dot = side * 0.09;
        NSPoint c = NP(b, 50, 73);
        ink(shape(b, tri, 3, YES), stroke, fg, bg, YES);
        ink(shape(b, bang, 2, NO), stroke * 1.2, fg, bg, NO);
        [fg setFill];
        [[NSBezierPath bezierPathWithOvalInRect:
            NSMakeRect(c.x - dot / 2, c.y - dot / 2, dot, dot)] fill];
        break;
    }

    case ICON_PLUS: {
        static const CGFloat h[] = { 18,50, 82,50 };
        static const CGFloat v[] = { 50,18, 50,82 };
        ink(shape(b, h, 2, NO), stroke * 1.3, fg, bg, NO);
        ink(shape(b, v, 2, NO), stroke * 1.3, fg, bg, NO);
        break;
    }

    default: break;
    }
}

/* ═══════════════════════════════════════════════ small helpers ══ */

static void fill_rect(NSRect r, unsigned c)
{
    [col(c) setFill];
    NSRectFill(r);
}

#define RADIUS      8.0
#define RADIUS_BIG  12.0

static void round_fill(NSRect r, unsigned c, CGFloat radius)
{
    [col(c) setFill];
    [[NSBezierPath bezierPathWithRoundedRect:r xRadius:radius yRadius:radius] fill];
}

/* inset half a point so the 1pt stroke lands on whole pixels */
static void round_frame(NSRect r, unsigned c, CGFloat radius)
{
    NSBezierPath *p = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(r, 0.5, 0.5)
                                                      xRadius:radius yRadius:radius];
    p.lineWidth = 1;
    [col(c) setStroke];
    [p stroke];
}

enum { T_LEFT = 0, T_RIGHT = 1, T_VCENTER = 2 };

static void text_at(NSRect r, NSString *s, NSFont *font, unsigned colour, int flags)
{
    NSMutableParagraphStyle *para = [NSMutableParagraphStyle new];
    CGFloat lineH = ceil(font.ascender - font.descender + font.leading);

    para.lineBreakMode = NSLineBreakByTruncatingTail;
    para.alignment = (flags & T_RIGHT) ? NSTextAlignmentRight : NSTextAlignmentLeft;
    if (flags & T_VCENTER)
        r = NSMakeRect(NSMinX(r), NSMinY(r) + (NSHeight(r) - lineH) / 2, NSWidth(r), lineH);

    [s drawWithRect:r
            options:NSStringDrawingUsesLineFragmentOrigin | NSStringDrawingTruncatesLastVisibleLine
         attributes:@{ NSFontAttributeName: font,
                       NSForegroundColorAttributeName: col(colour),
                       NSParagraphStyleAttributeName: para }];
}

/* case-insensitive substring */
static BOOL has_text(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    const char *p;

    if (!n) return YES;
    for (p = hay; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == n) return YES;
    }
    return NO;
}

static void warn(NSString *text)
{
    NSAlert *a = [NSAlert new];
    a.alertStyle = NSAlertStyleWarning;
    a.messageText = text;
    [a runModal];
}

/* ═══════════════════════════════════════════════════ the child ══ */

/* The folder the app sits in: that is where the Explorer looks, the way gui.c
 * looks beside adda-gui.exe. Run bare (not as Adda.app) it is the binary's. */
static NSString *home_dir(void)
{
    NSString *b = NSBundle.mainBundle.bundlePath;
    return [b.pathExtension isEqualToString:@"app"] ? b.stringByDeletingLastPathComponent : b;
}

/* build.sh copies adda into the bundle beside the GUI; failing that, the one
 * next to the app will do. */
static NSString *adda_path(void)
{
    NSString *inside = [NSBundle.mainBundle.executablePath.stringByDeletingLastPathComponent
                           stringByAppendingPathComponent:@"adda"];
    if ([NSFileManager.defaultManager isExecutableFileAtPath:inside]) return inside;
    return [home_dir() stringByAppendingPathComponent:@"adda"];
}

/* ── console text ────────────────────────────────────────────────── */

static NSUInteger console_len(void)
{
    return g_console.textStorage.length;
}

/* Straight into the storage, which goes round the delegate that keeps people
 * from editing the history - this is the program's output, not typing. */
static void console_replace(NSRange r, NSString *s)
{
    NSAttributedString *a = [[NSAttributedString alloc] initWithString:s
        attributes:@{ NSFontAttributeName: g_fontMono,
                      NSForegroundColorAttributeName: col(g_t.text) }];
    [g_console.textStorage replaceCharactersInRange:r withAttributedString:a];
}

static void console_append(NSString *s)
{
    console_replace(NSMakeRange(console_len(), 0), s);
    g_console.selectedRange = NSMakeRange(console_len(), 0);
    [g_console scrollRangeToVisible:g_console.selectedRange];
}

/* Whatever the user has half-typed after the anchor, or nil. */
static NSString *console_pending(void)
{
    if (console_len() <= g_anchor) return nil;
    return [g_console.string substringFromIndex:g_anchor];
}

/* Output arriving while a line is half-typed must not eat it, so it goes in
 * at the anchor, underneath the typing, and the selection moves down with
 * it - the whole selection, not just a caret: collapsing it would turn a
 * pending overtype into an insert. */
static void console_output(NSString *text)
{
    NSUInteger len = console_len();
    BOOL pending;
    NSRange sel = g_console.selectedRange;
    NSInteger caret, tail;

    if (!text.length) return;
    if (g_anchor > len) g_anchor = len;
    pending = len > g_anchor;

    caret = (NSInteger)sel.location - (NSInteger)g_anchor;
    tail  = (NSInteger)NSMaxRange(sel) - (NSInteger)g_anchor;
    if (caret < 0) caret = 0;
    if (tail < caret) tail = caret;

    console_replace(NSMakeRange(g_anchor, 0), text);
    g_anchor += text.length;

    if (pending)
        g_console.selectedRange = NSMakeRange(g_anchor + (NSUInteger)caret,
                                              (NSUInteger)(tail - caret));
    else
        g_console.selectedRange = NSMakeRange(console_len(), 0);
    [g_console scrollRangeToVisible:g_console.selectedRange];
}

/* Bytes can arrive with a character cut in half, so anything after the last
 * whole UTF-8 character waits in g_carry for the next read. `all` takes the
 * lot, for when the program has finished and nothing more is coming. */
static NSString *take_text(BOOL all)
{
    const unsigned char *b = g_carry.bytes;
    NSUInteger n = g_carry.length, cut = n, i = n, back = 0;
    NSString *s;

    while (!all && i > 0 && back < 4) {
        unsigned char c = b[--i];
        back++;
        if ((c & 0xC0) != 0x80) {           /* not a continuation byte */
            NSUInteger need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
            if (back < need) cut = i;
            break;
        }
    }

    s = [[NSString alloc] initWithBytes:b length:cut encoding:NSUTF8StringEncoding];
    if (!s)     /* not UTF-8 at all: show something rather than nothing */
        s = [[NSString alloc] initWithBytes:b length:cut encoding:NSISOLatin1StringEncoding];
    [g_carry replaceBytesInRange:NSMakeRange(0, cut) withBytes:NULL length:0];
    return s;
}

static void set_running(BOOL running)
{
    g_running = running;
    /* Run and Stop live in the activity bar and dim by state */
    g_main.needsDisplay = YES;
}

static void close_fd(int *fd)
{
    if (*fd >= 0) { close(*fd); *fd = -1; }
}

/* Reads what the program has written so far. `limit` keeps one timer tick
 * from reading for ever when a program prints in a tight loop, so Stop still
 * gets a look in. */
static void drain(size_t limit)
{
    char buf[4096];
    size_t got = 0;

    if (g_outFd < 0) return;
    while (got < limit) {
        ssize_t n = read(g_outFd, buf, sizeof buf);
        if (n > 0) {
            [g_carry appendBytes:buf length:(NSUInteger)n];
            got += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        break;              /* EAGAIN: nothing more yet;  0: it closed the pipe */
    }
    if (got) console_output(take_text(NO));
}

/* Typed input is queued rather than written straight out.
 *
 * A plain blocking write parks the UI thread whenever the pipe is full and
 * the program is not reading, and then nothing - Stop included - responds.
 * The pipe is therefore non-blocking and anything it will not take waits here
 * for the next timer tick. */
static void pump_stdin(void)
{
    ssize_t n;

    if (g_inFd < 0 || !g_inq.length) return;

    n = write(g_inFd, g_inq.bytes, g_inq.length);
    if (n < 0) {
        if (errno != EAGAIN && errno != EINTR) g_inq.length = 0;  /* it has gone */
        return;
    }
    [g_inq replaceBytesInRange:NSMakeRange(0, (NSUInteger)n) withBytes:NULL length:0];
}

static void finish_run(NSString *note)
{
    [g_poll invalidate];
    g_poll = nil;

    /* the program has exited, so this reads to the end of the pipe */
    drain(SIZE_MAX);
    if (g_carry.length) console_output(take_text(YES));

    close_fd(&g_outFd);
    close_fd(&g_inFd);
    g_inq.length = 0;
    g_pid = 0;

    if (g_tmpFile) { unlink(g_tmpFile.fileSystemRepresentation); g_tmpFile = nil; }

    set_running(NO);

    /* Anything typed but never sent is not history. Left in place, the note
     * would land after it and the transcript would read exactly like a line
     * that had been submitted, when it never was. */
    console_clear_pending();

    if (note) console_append(note);
    g_anchor = console_len();
    [g_win makeFirstResponder:g_code];
}

static void poll_run(void)
{
    int status;

    if (!g_running) return;
    pump_stdin();           /* whatever the pipe would not take last time */
    drain(64 * 1024);
    if (waitpid(g_pid, &status, WNOHANG) == g_pid)
        finish_run(@"\n");
}

static void run_code(void)
{
    NSString *dir = NSTemporaryDirectory();
    NSString *name = @"_adda_gui.adda";
    NSString *exe = adda_path();
    int outp[2] = { -1, -1 }, inp[2] = { -1, -1 };
    posix_spawn_file_actions_t fa;
    posix_spawnattr_t attr;
    sigset_t dflt;
    char *argv[3];
    pid_t pid;
    int err;

    if (g_running) return;

    g_tmpFile = [dir stringByAppendingPathComponent:name];
    if (![g_code.string writeToFile:g_tmpFile atomically:NO
                           encoding:NSUTF8StringEncoding error:NULL]) {
        g_tmpFile = nil;
        return;
    }

    if (pipe(outp) != 0 || pipe(inp) != 0) {
        close_fd(&outp[0]); close_fd(&outp[1]);
        close_fd(&inp[0]);  close_fd(&inp[1]);
        err = errno;
    } else {
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_adddup2(&fa, inp[0], 0);
        posix_spawn_file_actions_adddup2(&fa, outp[1], 1);
        /* errors down the same pipe, so they land in order with the output */
        posix_spawn_file_actions_adddup2(&fa, outp[1], 2);
        /* run from the temp folder and name the file bare, so an error says
         * _adda_gui.adda:3 rather than the whole of /var/folders/... */
        posix_spawn_file_actions_addchdir_np(&fa, dir.fileSystemRepresentation);

        /* we ignore SIGPIPE, and an ignored signal stays ignored across exec */
        posix_spawnattr_init(&attr);
        sigemptyset(&dflt);
        sigaddset(&dflt, SIGPIPE);
        posix_spawnattr_setsigdefault(&attr, &dflt);
        /* CLOEXEC_DEFAULT: the child gets 0, 1 and 2 and nothing else of ours */
        posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_CLOEXEC_DEFAULT);

        argv[0] = (char *)exe.fileSystemRepresentation;
        argv[1] = (char *)name.fileSystemRepresentation;
        argv[2] = NULL;
        err = posix_spawn(&pid, argv[0], &fa, &attr, argv, environ);

        posix_spawn_file_actions_destroy(&fa);
        posix_spawnattr_destroy(&attr);
        close_fd(&outp[1]);         /* the child's ends */
        close_fd(&inp[0]);
    }

    if (err) {
        close_fd(&outp[0]);
        close_fd(&inp[1]);
        console_replace(NSMakeRange(0, console_len()),
            @"Could not run adda\n"
            @"Build it with  sh build.sh  - that puts adda inside Adda.app.");
        g_anchor = console_len();
        unlink(g_tmpFile.fileSystemRepresentation);
        g_tmpFile = nil;
        return;
    }

    g_pid = pid;
    g_outFd = outp[0];
    g_inFd = inp[1];
    fcntl(g_outFd, F_SETFL, O_NONBLOCK);
    fcntl(g_inFd, F_SETFL, O_NONBLOCK);
    fcntl(g_outFd, F_SETFD, FD_CLOEXEC);
    fcntl(g_inFd, F_SETFD, FD_CLOEXEC);

    console_replace(NSMakeRange(0, console_len()), @"");
    g_carry.length = 0;
    g_anchor = 0;
    set_running(YES);
    [g_win makeFirstResponder:g_console];

    /* Poll, as gui.c does: one timer drains the output, feeds the input and
     * notices the exit, so they cannot happen out of order. Common modes keep
     * it ticking through a live resize or an open menu. */
    g_poll = [NSTimer timerWithTimeInterval:0.05 repeats:YES
                                      block:^(NSTimer *t) { (void)t; poll_run(); }];
    [NSRunLoop.mainRunLoop addTimer:g_poll forMode:NSRunLoopCommonModes];
}

static void stop_code(void)
{
    int status;

    if (!g_running) return;
    kill(g_pid, SIGKILL);
    while (waitpid(g_pid, &status, 0) < 0 && errno == EINTR) {}
    finish_run(@"\n[stopped]\n");
}

/* Sends everything typed after the anchor down the pipe. */
static void send_line(void)
{
    NSString *line;
    NSMutableData *bytes;

    if (!g_running || g_inFd < 0) return;
    line = console_pending();

    console_append(@"\n");
    g_anchor = console_len();

    bytes = [NSMutableData data];
    if (line) [bytes appendData:[line dataUsingEncoding:NSUTF8StringEncoding]];
    [bytes appendBytes:"\n" length:1];
    [g_inq appendData:bytes];
    pump_stdin();
}

static void console_clear_pending(void)
{
    NSUInteger len = console_len();
    if (len > g_anchor) console_replace(NSMakeRange(g_anchor, len - g_anchor), @"");
    g_console.selectedRange = NSMakeRange(console_len(), 0);
}

/* ═══════════════════════════════════════════════ the explorer ══ */

/* item==nil is the outline's invisible root, standing for the Explorer's own
 * root folder. */
static NSString *dir_for_item(NSString *item) { return item ? item : g_home; }

/* A dot-name (.git, .DS_Store, the project's own .claude) is never shown, at
 * any level: this is a place to browse programs, not the whole repository.
 * A folder is shown regardless of what is in it; a file only if it is an
 * Adda program - the one filter the Explorer has always applied. */
static NSArray<NSString *> *scan_children(NSString *dir)
{
    NSFileManager *fm = NSFileManager.defaultManager;
    NSArray<NSString *> *names = [fm contentsOfDirectoryAtPath:dir error:NULL];
    NSMutableArray<NSString *> *dirs = [NSMutableArray array];
    NSMutableArray<NSString *> *files = [NSMutableArray array];
    NSComparator byName = ^NSComparisonResult(NSString *a, NSString *b) {
        return [a.lastPathComponent localizedStandardCompare:b.lastPathComponent];
    };

    for (NSString *name in names) {
        NSString *path;
        BOOL isDir = NO;

        if ([name hasPrefix:@"."]) continue;
        path = [dir stringByAppendingPathComponent:name];
        if (![fm fileExistsAtPath:path isDirectory:&isDir]) continue;
        /* an app is a folder on disk, but nobody wants to browse inside one -
         * least of all Adda.app's own Explorer, browsing itself */
        if (isDir && [name.pathExtension caseInsensitiveCompare:@"app"] == NSOrderedSame) continue;
        if (isDir) [dirs addObject:path];
        else if ([name.pathExtension caseInsensitiveCompare:@"adda"] == NSOrderedSame)
            [files addObject:path];
    }
    [dirs sortUsingComparator:byName];
    [files sortUsingComparator:byName];
    [dirs addObjectsFromArray:files];
    return dirs;
}

/* Cached, so opening a folder does not re-read the disk on every keystroke of
 * a later search, and so a reload after one small change does not lose the
 * scan of everything else that is open. invalidate_tree() clears all of it,
 * for whenever the disk might have changed under it. */
/* Straight after an import the Explorer lists only what was imported; the
 * next full reload (rescan_files) brings everything else back. */
static NSArray<NSString *> *g_importOnly;

static NSArray<NSString *> *children_of(NSString *dir)
{
    if (g_importOnly && [dir isEqualToString:g_home]) return g_importOnly;
    NSArray<NSString *> *kids = g_treeCache[dir];
    if (!kids) g_treeCache[dir] = kids = scan_children(dir);
    return kids;
}

static void invalidate_tree(void) { [g_treeCache removeAllObjects]; }

/* Always clears the selection first; a caller that wants a particular row
 * selected afterwards - a rename, a delete, a new file - asks for it once the
 * reload is done. */
static void rescan_files(void)
{
    g_importOnly = nil;
    invalidate_tree();
    if (!g_files) return;
    g_quiet = YES;
    [g_files deselectAll:nil];
    [g_files reloadItem:nil reloadChildren:YES];
    [g_files sizeLastColumnToFit];   /* see the comment in reload_quietly */
    g_quiet = NO;
}

static void open_file(NSString *path)
{
    NSData *data;
    NSString *text;
    BOOL isDir = NO;

    if (!path) return;
    if ([NSFileManager.defaultManager fileExistsAtPath:path isDirectory:&isDir] && isDir) return;

    data = [NSData dataWithContentsOfFile:path];
    if (!data) return;
    text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    if (!text) text = [[NSString alloc] initWithData:data encoding:NSISOLatin1StringEncoding];

    g_code.string = text;
    g_codeScroll.verticalRulerView.needsDisplay = YES;
    clear_check();
    style_text(g_code);
    /* the old undo steps point into text that has gone */
    [g_code.undoManager removeAllActions];
    g_code.selectedRange = NSMakeRange(0, 0);
    [g_code scrollRangeToVisible:g_code.selectedRange];
}

/* ══════════════════════════════════════ renaming and deleting ══ */

static void select_quietly(NSInteger row)
{
    g_quiet = YES;
    [g_files selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row]
         byExtendingSelection:NO];
    [g_files scrollRowToVisible:row];
    g_quiet = NO;
}

/* Only finds a path if the folders above it are open - as when it was just
 * created or renamed under our own reveal() below, which opens them first. */
static void select_by_path(NSString *path)
{
    NSInteger row = [g_files rowForItem:path];
    if (row >= 0) select_quietly(row);
}

/* Opens every folder between the root and path's own folder, so a freshly
 * made or renamed item can be found and selected straight away. */
static void reveal(NSString *path)
{
    NSMutableArray<NSString *> *chain = [NSMutableArray array];
    NSString *dir = path.stringByDeletingLastPathComponent;

    while (dir.length > g_home.length && [dir hasPrefix:g_home]) {
        [chain insertObject:dir atIndex:0];
        dir = dir.stringByDeletingLastPathComponent;
    }
    for (NSString *d in chain) [g_files expandItem:d];
}

/* Windows will not have these in a file name, and the same files are opened
 * there, so the Mac is held to its rules. A path separator would also let a
 * rename walk out of the folder. */
static BOOL name_is_sane(NSString *s)
{
    NSCharacterSet *bad = [NSCharacterSet characterSetWithCharactersInString:@"\\/:*?\"<>|"];
    return s.length && [s rangeOfCharacterFromSet:bad].location == NSNotFound;
}

static void end_rename(NSString *from, NSString *typed)
{
    NSString *target;
    BOOL isDir = NO;

    /* focus is already right: Return and Esc handed it back to the list, and
     * a click away put it wherever the click was */
    if (!typed || !from) return;
    [NSFileManager.defaultManager fileExistsAtPath:from isDirectory:&isDir];
    if (!typed.length || [typed isEqualToString:from.lastPathComponent]) return;

    if (!name_is_sane(typed)) {
        warn(@"A name cannot contain  \\ / : * ? \" < > |");
        return;
    }

    /* keep it findable: the Explorer only lists .adda files - but a folder
     * has no such rule, so this is skipped for one */
    if (!isDir && [typed rangeOfString:@"."].location == NSNotFound)
        typed = [typed stringByAppendingString:@".adda"];

    target = [from.stringByDeletingLastPathComponent stringByAppendingPathComponent:typed];

    /* the Mac's disk ignores case, so hello -> Hello is the same file, not a clash */
    if ([target caseInsensitiveCompare:from] != NSOrderedSame &&
        [NSFileManager.defaultManager fileExistsAtPath:target]) {
        warn(@"There is already something with that name.");
        return;
    }

    if (rename(from.fileSystemRepresentation, target.fileSystemRepresentation) != 0) {
        warn(isDir ? @"The Mac would not rename that folder.\n"
                     @"It may be locked, or in use."
                   : @"The Mac would not rename that file.\n"
                     @"It may be locked, or in a folder you cannot change.");
        return;
    }

    rescan_files();
    reveal(target);
    select_by_path(target);
}

/* Renaming happens in the list itself: the row's own label turns into a text
 * box. Return keeps the new name; Esc, or clicking away, abandons it. */
static void begin_rename(NSInteger row)
{
    NSString *path;
    Cell *cell;
    NSTextField *f;
    BOOL isDir = NO;

    if (row < 0 || row >= g_files.numberOfRows || g_renamePath) return;
    path = [g_files itemAtRow:row];
    if (!path) return;

    select_quietly(row);
    cell = [g_files viewAtColumn:0 row:row makeIfNecessary:YES];
    f = cell.textField;
    if (!f) return;

    g_renamePath = path;
    g_renameCommit = NO;
    /* the row's label is an attributed string with the icon riding as its
     * first character; editing wants the plain name back, without it. Colour
     * comes along with it normally, baked into that attributed string rather
     * than the field's own .textColor - plain text falls back to whatever
     * that was last set to, wrong on some themes, so it is set here too. */
    f.stringValue = path.lastPathComponent;
    f.textColor = col(g_t.text);
    f.editable = YES;
    f.selectable = YES;
    f.drawsBackground = YES;
    f.backgroundColor = col(g_t.surface);
    /* the field's frame comes from constraints now (see make_cell), which
     * resolve on the next layout pass - the field editor about to take over
     * grabs whatever frame the field has the instant it becomes first
     * responder, so that pass has to happen first or it inherits a stale,
     * unresolved one and renders blank. */
    [cell layoutSubtreeIfNeeded];
    [g_win makeFirstResponder:f];

    /* the name without its .adda selected, as the Finder does - a folder has
     * no extension to spare, so its whole name is offered */
    [NSFileManager.defaultManager fileExistsAtPath:path isDirectory:&isDir];
    f.currentEditor.selectedRange = NSMakeRange(0,
        isDir ? f.stringValue.length : f.stringValue.stringByDeletingPathExtension.length);
}

static void delete_file(NSInteger row)
{
    NSAlert *a;
    NSString *path;
    BOOL isDir = NO;

    if (row < 0 || row >= g_files.numberOfRows) return;
    path = [g_files itemAtRow:row];
    if (!path) return;
    [NSFileManager.defaultManager fileExistsAtPath:path isDirectory:&isDir];

    a = [NSAlert new];
    a.messageText = [NSString stringWithFormat:@"Delete %@?", path.lastPathComponent];
    a.informativeText = isDir
        ? @"It goes to the Trash, so you can get it back. Everything inside it goes too."
        : @"It goes to the Trash, so you can get it back.";
    [a addButtonWithTitle:@"Move to Trash"];
    [a addButtonWithTitle:@"Cancel"];
    /* Cancel is the default, so a stray Return deletes nothing */
    a.buttons[0].keyEquivalent = @"";
    a.buttons[0].hasDestructiveAction = YES;
    a.buttons[1].keyEquivalent = @"\r";
    if ([a runModal] != NSAlertFirstButtonReturn) return;

    if (![NSFileManager.defaultManager trashItemAtURL:[NSURL fileURLWithPath:path]
                                     resultingItemURL:nil error:NULL])
        warn(isDir ? @"The Mac would not move that folder to the Trash.\n"
                     @"It may be locked, or in a folder you cannot change."
                   : @"The Mac would not move that file to the Trash.\n"
                     @"It may be locked, or in a folder you cannot change.");

    rescan_files();
    if (g_files.numberOfRows) {
        NSInteger last = g_files.numberOfRows - 1;
        select_quietly(row < last ? row : last);
    }
    [g_win makeFirstResponder:g_files];
}

/* ────────────────────────────────────── new files and folders ── */

/* The folder a new item is created in: the selected folder, the selected
 * file's own folder, or the Explorer's root if nothing is selected. */
static NSString *target_dir(void)
{
    NSInteger row = g_files.selectedRow;
    NSString *item;
    BOOL isDir = NO;

    if (row < 0) return g_home;
    item = [g_files itemAtRow:row];
    if (!item) return g_home;
    if ([NSFileManager.defaultManager fileExistsAtPath:item isDirectory:&isDir] && isDir)
        return item;
    return item.stringByDeletingLastPathComponent;
}

/* "untitled.adda", then "untitled 2.adda" and up, as the Finder numbers a
 * folder it cannot call "untitled" because one is already there. */
static NSString *unique_path(NSString *dir, NSString *base, NSString *ext)
{
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *name = ext.length ? [base stringByAppendingPathExtension:ext] : base;
    NSString *path = [dir stringByAppendingPathComponent:name];
    int n = 2;

    while ([fm fileExistsAtPath:path]) {
        NSString *numbered = [NSString stringWithFormat:@"%@ %d", base, n++];
        name = ext.length ? [numbered stringByAppendingPathExtension:ext] : numbered;
        path = [dir stringByAppendingPathComponent:name];
    }
    return path;
}

/* Creates the file or folder, opens the file into the editor (a folder has
 * nothing to open), and starts renaming it immediately - the name it was
 * given is only ever a placeholder, there to let it exist while it is typed
 * over. */
static void create_and_edit(NSString *path, BOOL isDir)
{
    NSFileManager *fm = NSFileManager.defaultManager;
    BOOL ok = isDir
        ? [fm createDirectoryAtPath:path withIntermediateDirectories:NO attributes:nil error:NULL]
        : [@"" writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:NULL];
    NSInteger row;

    if (!ok) {
        warn(isDir ? @"The Mac would not create that folder.\n"
                     @"It may be locked, or in a folder you cannot change."
                   : @"The Mac would not create that file.\n"
                     @"It may be locked, or in a folder you cannot change.");
        return;
    }

    invalidate_tree();
    [g_files reloadItem:nil reloadChildren:YES];
    [g_files sizeLastColumnToFit];   /* see the comment in reload_quietly */
    reveal(path);
    if (!isDir) open_file(path);

    row = [g_files rowForItem:path];
    if (row >= 0) { select_quietly(row); begin_rename(row); }
}

/* ═══════════════════════════════════════════ saving and importing ══ */

static int bar_hit(NSPoint p)
{
    int i;
    if (g_view != AB_EXPLORER) return -1;
    for (i = 0; i < BAR_COUNT; i++)
        if (NSPointInRect(p, g_barRect[i])) return i;
    return -1;
}

static NSArray<UTType *> *adda_types(void)
{
    UTType *t = [UTType typeWithFilenameExtension:@"adda"];
    return t ? @[ t ] : @[];
}

/* Writes what is in the editor to wherever the Finder's Save panel says. */
static void save_as(void)
{
    NSSavePanel *panel = [NSSavePanel savePanel];
    NSString *item = g_files.selectedRow >= 0 ? [g_files itemAtRow:g_files.selectedRow] : nil;
    BOOL isDir = NO;

    panel.title = @"Save";
    panel.allowedContentTypes = adda_types();
    panel.allowsOtherFileTypes = YES;
    panel.directoryURL = [NSURL fileURLWithPath:target_dir()];
    if (item && [NSFileManager.defaultManager fileExistsAtPath:item isDirectory:&isDir] && !isDir)
        panel.nameFieldStringValue = item.lastPathComponent;
    else
        panel.nameFieldStringValue = @"program.adda";

    [panel beginSheetModalForWindow:g_win completionHandler:^(NSModalResponse r) {
        NSError *err = nil;
        if (r != NSModalResponseOK) return;
        if (![g_code.string writeToURL:panel.URL atomically:YES
                              encoding:NSUTF8StringEncoding error:&err]) {
            warn(@"The Mac would not save the file there.\n"
                 @"It may be locked, or in a folder you cannot change.");
            return;
        }
        rescan_files();              /* it may have landed in the Explorer */
        reveal(panel.URL.path);
        select_by_path(panel.URL.path);
    }];
}

/* Copies the chosen files and folders into the Explorer's folder (or the
 * folder that is selected in it), numbering any whose name is already taken.
 * A folder comes with everything inside it. Opens the last file, or for a
 * folder its first.adda, else its first program. */
static void import_files(void)
{
    NSOpenPanel *panel = [NSOpenPanel openPanel];

    panel.title = @"Import";
    panel.prompt = @"Import";
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = YES;       /* a whole folder, too */
    panel.allowsMultipleSelection = YES;
    panel.allowedContentTypes = adda_types();
    panel.message = @"Choose .adda files, or a whole folder.";

    [panel beginSheetModalForWindow:g_win completionHandler:^(NSModalResponse r) {
        NSString *dir = target_dir(), *last = nil, *lastDir = nil;
        NSMutableArray<NSString *> *got = [NSMutableArray array];
        int failed = 0;

        if (r != NSModalResponseOK) return;
        for (NSURL *url in panel.URLs) {
            NSString *name = url.lastPathComponent;
            NSString *to = unique_path(dir, name.stringByDeletingPathExtension, name.pathExtension);
            BOOL isDir = NO;
            [NSFileManager.defaultManager fileExistsAtPath:url.path isDirectory:&isDir];
            if ([NSFileManager.defaultManager copyItemAtPath:url.path toPath:to error:NULL]) {
                if (isDir) lastDir = to; else last = to;
                [got addObject:to];
            }
            else
                failed++;
        }
        rescan_files();
        if (got.count) {
            /* show only what came in; nothing else is touched on disk */
            g_importOnly = [got copy];
            [g_files reloadItem:nil reloadChildren:YES];
            g_main.needsDisplay = YES;          /* the heading says IMPORTED */
        }
        if (!last && lastDir) {
            /* a folder: open it up, and open its first.adda or first program */
            BOOL sub = NO;
            [g_files expandItem:lastDir];
            for (NSString *kid in children_of(lastDir)) {
                if ([NSFileManager.defaultManager fileExistsAtPath:kid isDirectory:&sub] && sub)
                    continue;
                if (!last || [kid.lastPathComponent isEqualToString:@"first.adda"]) last = kid;
            }
        }
        if (last) {
            select_by_path(last);
            open_file(last);
        }
        if (failed)
            warn(@"Some files or folders could not be imported.\n"
                 @"They may be locked, or the folder may be read-only.");
    }];
}

/* ═══════════════════════════════════════════════════ new project ══ */

/* Asks where to save the new project, makes a folder of that name there with
 * first.adda and style.adda in it, points the Explorer at it and opens
 * first.adda. */
static void new_project(void)
{
    NSSavePanel *panel = [NSSavePanel savePanel];

    panel.title = @"New Project";
    panel.message = @"Choose where to save the new project, and give it a name.";
    panel.prompt = @"Create";
    panel.nameFieldLabel = @"Project:";
    panel.nameFieldStringValue = @"My Project";
    panel.canCreateDirectories = YES;

    [panel beginSheetModalForWindow:g_win completionHandler:^(NSModalResponse r) {
        NSFileManager *fm = NSFileManager.defaultManager;
        NSString *dir = panel.URL.path, *first, *style;
        BOOL ok;

        if (r != NSModalResponseOK) return;
        if ([fm fileExistsAtPath:dir]) {
            warn(@"There is already something with that name there.\n"
                 @"Pick another name for the project.");
            return;
        }
        if (![fm createDirectoryAtPath:dir withIntermediateDirectories:NO
                            attributes:nil error:NULL]) {
            warn(@"The Mac would not create the project folder there.");
            return;
        }
        first = [dir stringByAppendingPathComponent:@"first.adda"];
        style = [dir stringByAppendingPathComponent:@"style.adda"];
        ok = [@"# first.adda - your program starts here.\n"
              @"[name] = ask What is your name?\n"
              @"print Hello, [name]\n"
                writeToFile:first atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        ok = [@"# style.adda - a second file for this project.\n"
                writeToFile:style atomically:YES encoding:NSUTF8StringEncoding error:NULL] && ok;
        if (!ok) warn(@"The Mac would not write the project's files.");

        g_home = dir;
        g_view = AB_EXPLORER;
        rescan_files();
        layout();
        select_by_path(first);
        open_file(first);
        g_win.title = [NSString stringWithFormat:@"Adda - %@", dir.lastPathComponent];
    }];
}

/* ═══════════════════════════════════════════════════ searching ══ */

static void find_in_code(BOOL forward)
{
    NSString *needle = g_find.stringValue;
    NSString *hay = g_code.string;
    NSUInteger start = forward ? NSMaxRange(g_code.selectedRange) : 0;
    NSRange r;

    if (!needle.length || needle.length > hay.length) return;
    if (start > hay.length) start = 0;

    r = [hay rangeOfString:needle options:NSCaseInsensitiveSearch
                     range:NSMakeRange(start, hay.length - start)];
    if (r.location == NSNotFound && start > 0)      /* wrap round to the top */
        r = [hay rangeOfString:needle options:NSCaseInsensitiveSearch];
    if (r.location == NSNotFound) return;

    g_code.selectedRange = r;
    [g_code scrollRangeToVisible:r];
    /* the code view is not focused, so its selection shows grey; this flashes
     * the match so it can be found */
    [g_code showFindIndicatorForRange:r];
}

/* ═════════════════════════════════════════════════════ layout ══ */

static NSRect sized(CGFloat x, CGFloat y, CGFloat w, CGFloat h)
{
    return NSMakeRect(x, y, w < 0 ? 0 : w, h < 0 ? 0 : h);
}

/* All in the main view's flipped coordinates, so y runs down as in gui.c. */
static void layout(void)
{
    NSRect rc = g_main.bounds;
    CGFloat W = NSWidth(rc), H = NSHeight(rc);
    CGFloat abW = 48, panelW = (g_view >= 0) ? 210 : 0, pad = 12;
    CGFloat toolH = 12, splitH = 7, below = 20;   /* below: room for the hint */
    CGFloat x, y, w, h, top, codeH, consoleH;
    int i;

    /* activity bar slots */
    top = 8;
    for (i = 0; i < AB_CHECK; i++) {
        g_abRect[i] = NSMakeRect(0, top, abW, 44);
        top += 44 + (i == AB_SEARCH ? 6 : 0);
    }
    g_abRect[AB_GEAR]  = NSMakeRect(0, H - 8 - 44, abW, 44);
    g_abRect[AB_CHEAT] = NSMakeRect(0, H - 8 - 88, abW, 44);
    g_abRect[AB_CHECK] = NSMakeRect(0, H - 8 - 132, abW, 44);

    g_panelRect = NSMakeRect(abW, 0, panelW, H);

    x = abW + panelW;
    w = W - x;
    if (w < 200) w = 200;

    /* editor area under the top padding */
    y = toolH;
    h = H - toolH;
    if (h < 120) h = 120;

    {
        CGFloat track = h - splitH;
        CGFloat minPane = 60;

        if (track < 2 * minPane) {
            codeH = track > 0 ? floor(track / 2) : 0;   /* too short to honour both */
        } else {
            codeH = floor(track * g_split + 0.5);
            if (codeH < minPane)         codeH = minPane;
            if (codeH > track - minPane) codeH = track - minPane;
        }
        consoleH = track - codeH;
        if (consoleH < 0) consoleH = 0;
    }

    g_splitRect = NSMakeRect(x, y + codeH, W - x, splitH);
    g_splitHit  = NSMakeRect(x, y + codeH - 6, W - x, splitH + 6);

    if (g_view == AB_EXPLORER) {
        g_filesScroll.frame = sized(abW + 8, 40, panelW - 16, H - 48 - 40);
        g_filesScroll.hidden = NO;
        g_find.hidden = YES;
        g_addRect = NSMakeRect(NSMaxX(g_panelRect) - 14 - 20, 8, 20, 20);
        for (i = 0; i < BAR_COUNT; i++)
            g_barRect[i] = NSMakeRect(abW + 10 + i * 34, H - 38, 30, 30);
    } else if (g_view == AB_SEARCH) {
        /* the field is borderless; paint_main draws its frame in the theme */
        g_findBox = sized(abW + 8, 40, panelW - 16, 28);
        g_find.frame = sized(NSMinX(g_findBox) + 6, NSMinY(g_findBox) + 5,
                             NSWidth(g_findBox) - 12, 18);
        g_find.hidden = NO;
        g_filesScroll.hidden = YES;
    } else {
        g_filesScroll.hidden = YES;
        g_find.hidden = YES;
    }

    g_codeScroll.frame    = sized(x + pad, y, w - pad * 2, codeH - 6);
    g_consoleScroll.frame = sized(x + pad, y + codeH + splitH, w - pad * 2, consoleH - below);

    [g_main removeAllToolTips];
    for (i = 0; i < AB_COUNT; i++)
        [g_main addToolTipRect:g_abRect[i] owner:AB_TIP[i] userData:NULL];
    if (g_view == AB_EXPLORER)
    {
        [g_main addToolTipRect:g_addRect owner:@"New File or Folder" userData:NULL];
        [g_main addToolTipRect:g_barRect[BAR_SAVE] owner:@"Save As..." userData:NULL];
        [g_main addToolTipRect:g_barRect[BAR_IMPORT] owner:@"Import Files..." userData:NULL];
    }

    [g_win invalidateCursorRectsForView:g_main];
    g_main.needsDisplay = YES;
}

/* ══════════════════════════════════════════════════ main paint ══ */

static void paint_main(void)
{
    NSRect rc = g_main.bounds;
    int i;

    fill_rect(rc, g_t.bg);

    /* activity bar */
    fill_rect(NSMakeRect(0, 0, 48, NSHeight(rc)), g_t.abBg);

    for (i = 0; i < AB_COUNT; i++) {
        NSRect box = g_abRect[i];
        BOOL active = (i == g_view);
        /* Run only makes sense while idle, Stop only while running */
        BOOL disabled = (i == AB_RUN && g_running) || (i == AB_STOP && !g_running);
        double glow = disabled ? 0 : g_abGlow[i];
        unsigned cell = blend(g_t.abBg, g_t.abHover, glow);
        unsigned fg = active ? g_t.abIcon : blend(g_t.abIconDim, g_t.abIcon, glow);

        if (disabled)                   /* halfway between dim and the bar */
            fg = mix(g_t.abIconDim, g_t.abBg);
        else if (i == AB_STOP)          /* something is running: make it obvious */
            fg = g_t.abIcon;

        if (i == AB_NEW)                /* a hairline between views and actions */
            fill_rect(NSMakeRect(NSMinX(box) + 12, NSMinY(box) - 3, NSWidth(box) - 24, 1),
                      g_t.abIconDim);

        double k = press_amount(PRESS_AB, i);
        if (k > 0) {
            cell = blend(cell, g_t.accent, 0.35 * k);
            round_fill(NSInsetRect(box, 5 + 4 * k, 3 + 4 * k), cell, RADIUS);
        } else if (glow > 0.01) {
            round_fill(NSInsetRect(box, 5, 3), cell, RADIUS);
        }

        if (active)
            round_fill(NSMakeRect(NSMinX(box) + 1, NSMinY(box) + 8, 3, NSHeight(box) - 16),
                       g_t.abIcon, 1.5);

        draw_icon(NSInsetRect(box, 12 + 2 * k, 12 + 2 * k), AB_ICON[i], fg, cell);
    }

    /* side panel */
    if (g_view >= 0) {
        CGFloat labelRight = (g_view == AB_EXPLORER) ? NSMinX(g_addRect) - 6
                                                     : NSMaxX(g_panelRect) - 14;
        fill_rect(g_panelRect, g_t.bg);

        text_at(NSMakeRect(NSMinX(g_panelRect) + 14, 12, labelRight - (NSMinX(g_panelRect) + 14), 20),
                (g_view == AB_SEARCH) ? @"SEARCH" : g_importOnly ? @"IMPORTED" : @"EXPLORER",
                g_fontSmall, g_t.muted, T_LEFT);

        if (g_view == AB_EXPLORER) {
            unsigned addFg = blend(g_t.muted, g_t.text, g_addGlow);
            double k = press_amount(PRESS_ADD, 0);
            unsigned addBg = blend(blend(g_t.bg, g_t.ghostHot, k > 0 ? 1 : g_addGlow),
                                   g_t.accent, 0.35 * k);
            if (addBg != g_t.bg) round_fill(NSInsetRect(g_addRect, -3 + 2 * k, -3 + 2 * k), addBg, 6);
            draw_icon(NSInsetRect(g_addRect, 2 + k, 2 + k), ICON_PLUS, addFg, addBg);

            /* Save and Import, under a hairline */
            fill_rect(NSMakeRect(NSMinX(g_panelRect) + 10, NSMinY(g_barRect[0]) - 6,
                                 NSWidth(g_panelRect) - 20, 1), g_t.border);
            for (int b = 0; b < BAR_COUNT; b++) {
                double kb = press_amount(PRESS_BAR, b);
                double gb = g_barGlow[b];
                unsigned bg = blend(blend(g_t.bg, g_t.ghostHot, kb > 0 ? 1 : gb),
                                    g_t.accent, 0.35 * kb);
                if (bg != g_t.bg)
                    round_fill(NSInsetRect(g_barRect[b], 3 * kb, 3 * kb), bg, 6);
                draw_icon(NSInsetRect(g_barRect[b], 7 + 2 * kb, 7 + 2 * kb),
                          b == BAR_SAVE ? ICON_SAVE : ICON_IMPORT,
                          blend(g_t.muted, g_t.text, gb), bg);
            }
        }

        fill_rect(NSMakeRect(NSMaxX(g_panelRect) - 1, 0, 1, NSHeight(rc)), g_t.border);

        if (g_view == AB_SEARCH)          /* frame for the borderless find box */
            round_frame(NSInsetRect(g_findBox, -1, -1), g_t.border, RADIUS);
    }

    /* splitter: a faint line, like a grip */
    fill_rect(NSMakeRect(NSMinX(g_splitRect) + 12, floor(NSMidY(g_splitRect)),
                         NSWidth(g_splitRect) - 24, 1), g_t.border);

    /* panel outlines */
    round_frame(NSInsetRect(g_codeScroll.frame, -1, -1), g_t.border, RADIUS_BIG);
    round_frame(NSInsetRect(g_consoleScroll.frame, -1, -1), g_t.border, RADIUS_BIG);

    /* a word about what the console is for */
    if (g_running)
        text_at(NSMakeRect(NSMinX(g_splitRect) + 12, NSHeight(rc) - 17,
                           NSWidth(g_splitRect) - 24, 14),
                @"type your answer and press Return",
                g_fontSmall, g_t.muted, T_RIGHT);
}

/* ═══════════════════════════════════════════════ settings popup ══ */

#define SET_NAV_W 140

static int g_setRowHot = -1;

static void settings_paint(NSRect rc)
{
    NSRect item;
    CGFloat y;
    int i, j;

    fill_rect(rc, g_t.surface);
    fill_rect(NSMakeRect(0, 0, SET_NAV_W, NSHeight(rc)), g_t.bg);
    fill_rect(NSMakeRect(SET_NAV_W - 1, 0, 1, NSHeight(rc)), g_t.border);

    item = NSMakeRect(10, 16, SET_NAV_W - 20, 28);
    round_fill(item, g_t.sel, RADIUS);
    text_at(NSMakeRect(NSMinX(item) + 10, NSMinY(item), NSWidth(item) - 10, NSHeight(item)),
            @"Themes", g_fontUIBold, g_t.text, T_VCENTER);

    /* the Themes page */
    y = 16;
    text_at(NSMakeRect(SET_NAV_W + 20, y, NSWidth(rc) - SET_NAV_W - 36, 24),
            @"Themes", g_fontUIBold, g_t.text, T_VCENTER);
    y += 34;

    for (i = 0; i < PICK_COUNT; i++) {
        Theme t = theme_for(i);
        NSRect row = NSMakeRect(SET_NAV_W + 12, y, NSWidth(rc) - SET_NAV_W - 24, 52);
        /* three chips that say what the theme looks like */
        unsigned chips[3] = { t.abBg, t.surface, t.accent };

        double k = press_amount(PRESS_ROW, i);
        if (i == g_pick || g_rowGlow[i] > 0.01 || k > 0) {
            unsigned base = (i == g_pick) ? g_t.sel
                          : blend(g_t.surface, g_t.ghostHot, k > 0 ? 1 : g_rowGlow[i]);
            round_fill(NSInsetRect(row, 4 * k, 2 * k), blend(base, g_t.accent, 0.3 * k), RADIUS_BIG);
        }

        for (j = 0; j < 3; j++) {
            NSRect sw = NSMakeRect(NSMinX(row) + 10 + j * 16, NSMinY(row) + 18, 14, 16);
            round_fill(sw, chips[j], 4);
            round_frame(sw, g_t.border, 4);
        }

        text_at(NSMakeRect(NSMinX(row) + 70, NSMinY(row) + 8, NSWidth(row) - 140, 18),
                @(THEME_NAMES[i]), g_fontUIBold, g_t.text, T_LEFT);
        text_at(NSMakeRect(NSMinX(row) + 70, NSMinY(row) + 27, NSWidth(row) - 140, 18),
                @(THEME_ABOUT[i]), g_fontSmall, g_t.muted, T_LEFT);

        if (i == g_pick)
            text_at(NSMakeRect(NSMaxX(row) - 70, NSMinY(row), 58, NSHeight(row)),
                    @"in use", g_fontSmall, g_t.muted, T_RIGHT | T_VCENTER);

        y = NSMaxY(row) + 4;
    }
}

static int settings_row_at(NSPoint p)
{
    CGFloat y = 16 + 34;
    int i;

    if (p.x < SET_NAV_W) return -1;
    for (i = 0; i < PICK_COUNT; i++) {
        if (p.y >= y && p.y < y + 52) return i;
        y += 52 + 4;
    }
    return -1;
}

/* Settings and the cheat sheet float over the main window, as gui.c's owned
 * popups do, and hide while another app is in front. */
static Popup *make_popup(NSString *title, NSView *content)
{
    Popup *p = [[Popup alloc] initWithContentRect:content.frame
                                        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                          backing:NSBackingStoreBuffered
                                            defer:YES];
    p.title = title;
    p.releasedWhenClosed = NO;
    p.floatingPanel = YES;
    p.hidesOnDeactivate = YES;
    p.titlebarAppearsTransparent = YES;
    p.contentView = content;
    return p;
}

static void centre_on(NSWindow *child)
{
    NSRect o = g_win.frame, f = child.frame;
    f.origin.x = floor(NSMidX(o) - NSWidth(f) / 2);
    f.origin.y = floor(NSMidY(o) - NSHeight(f) / 2);
    [child setFrame:f display:NO];
}

static void open_settings(void)
{
    if (!g_settings) {
        g_settingsView = [[SettingsView alloc] initWithFrame:NSMakeRect(0, 0, 600, 380)];
        g_settings = make_popup(@"Settings", g_settingsView);
        apply_theme();
    }
    if (!g_settings.visible) centre_on(g_settings);
    [g_settings makeKeyAndOrderFront:nil];
    [g_settings makeFirstResponder:g_settingsView];
}

/* ══════════════════════════════════════════════ cheat sheet ══ */

static void refresh_cheats(void)
{
    const char *needle = g_cheatFind ? g_cheatFind.stringValue.UTF8String : "";
    int i;

    g_cheatCount = 0;
    for (i = 0; i < CHEAT_COUNT; i++) {
        if (!needle[0] ||
            has_text(CHEATS[i].title, needle) ||
            has_text(CHEATS[i].about, needle) ||
            has_text(CHEATS[i].snippet, needle))
            g_cheatShown[g_cheatCount++] = i;
    }

    if (!g_cheatList) return;
    [g_cheatList reloadData];
    if (g_cheatCount) {
        [g_cheatList selectRowIndexes:[NSIndexSet indexSetWithIndex:0] byExtendingSelection:NO];
        [g_cheatList scrollRowToVisible:0];
    }
}

static void insert_cheat(NSInteger shownIndex)
{
    NSString *snip;
    NSRange r;

    if (shownIndex < 0 || shownIndex >= g_cheatCount) return;

    /* the snippets carry \r\n for the Windows EDIT control */
    snip = [@(CHEATS[g_cheatShown[shownIndex]].snippet)
               stringByReplacingOccurrencesOfString:@"\r\n" withString:@"\n"];

    /* through shouldChange/didChange, so Cmd+Z can take it back out */
    r = g_code.selectedRange;
    if ([g_code shouldChangeTextInRange:r replacementString:snip]) {
        [g_code.textStorage replaceCharactersInRange:r withAttributedString:
            [[NSAttributedString alloc] initWithString:snip attributes:g_code.typingAttributes]];
        [g_code didChangeText];
        g_code.selectedRange = NSMakeRange(r.location + snip.length, 0);
        [g_code scrollRangeToVisible:g_code.selectedRange];
    }

    [g_win makeKeyAndOrderFront:nil];
    [g_win makeFirstResponder:g_code];
}

static NSTableView *make_table(Class cls, CGFloat rowH)
{
    NSTableView *tv = [[cls alloc] initWithFrame:NSZeroRect];
    NSTableColumn *c = [[NSTableColumn alloc] initWithIdentifier:@"only"];

    c.resizingMask = NSTableColumnAutoresizingMask;
    [tv addTableColumn:c];
    tv.headerView = nil;
    tv.style = NSTableViewStylePlain;
    tv.rowHeight = rowH;
    tv.intercellSpacing = NSMakeSize(0, 0);
    tv.columnAutoresizingStyle = NSTableViewLastColumnOnlyAutoresizingStyle;
    tv.focusRingType = NSFocusRingTypeNone;
    tv.dataSource = g_adda;
    tv.delegate = g_adda;
    return tv;
}

static NSScrollView *scroll_round(NSView *doc)
{
    NSScrollView *sv = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    sv.hasVerticalScroller = YES;
    sv.autohidesScrollers = YES;
    sv.borderType = NSNoBorder;
    sv.documentView = doc;
    /* clip to rounded corners, matching the outline paint_main draws */
    sv.wantsLayer = YES;
    sv.layer.cornerRadius = RADIUS_BIG - 1;
    sv.layer.cornerCurve = kCACornerCurveContinuous;
    sv.layer.masksToBounds = YES;
    return sv;
}

static void open_cheats(void)
{
    if (!g_cheats) {
        NSRect frame = NSMakeRect(0, 0, 560, 500);
        NSScrollView *sv;

        g_cheatsView = [[CheatsView alloc] initWithFrame:frame];

        g_cheatFind = [[NSTextField alloc] initWithFrame:NSMakeRect(14, 14, NSWidth(frame) - 28, 24)];
        g_cheatFind.font = g_fontUI;
        g_cheatFind.bezelStyle = NSTextFieldRoundedBezel;
        g_cheatFind.drawsBackground = YES;
        g_cheatFind.placeholderString = @"What do you want to do?";
        g_cheatFind.cell.scrollable = YES;
        g_cheatFind.cell.wraps = NO;
        g_cheatFind.delegate = g_adda;

        g_cheatList = (CheatTable *)make_table([CheatTable class], 43);
        g_cheatList.target = g_adda;
        g_cheatList.doubleAction = @selector(insertClickedCheat:);

        sv = scroll_round(g_cheatList);
        sv.frame = NSMakeRect(14, 50, NSWidth(frame) - 28, NSHeight(frame) - 50 - 32);

        [g_cheatsView addSubview:g_cheatFind];
        [g_cheatsView addSubview:sv];
        g_cheats = make_popup(@"Cheat sheet", g_cheatsView);
        apply_theme();
    }

    if (!g_cheats.visible) {
        g_cheatFind.stringValue = @"";
        refresh_cheats();
        centre_on(g_cheats);
    }
    [g_cheats makeKeyAndOrderFront:nil];
    [g_cheats makeFirstResponder:g_cheatFind];
}

/* ═════════════════════════════════════════════════ main window ══ */

static int ab_hit(NSPoint p)
{
    int i;
    for (i = 0; i < AB_COUNT; i++)
        if (NSPointInRect(p, g_abRect[i])) return i;
    return -1;
}

static void ab_click(int item)
{
    switch (item) {
    case AB_EXPLORER:
    case AB_SEARCH:
        g_view = (g_view == item) ? -1 : item;   /* click again to collapse */
        if (g_view == AB_EXPLORER) rescan_files();
        layout();
        if (g_view == AB_SEARCH) [g_win makeFirstResponder:g_find];
        break;
    case AB_NEW:
        new_project();
        break;
    case AB_RUN:
        if (!g_running) run_code();
        break;
    case AB_STOP:
        stop_code();
        break;
    case AB_CHECK:
        check_code();
        break;
    case AB_CHEAT:
        open_cheats();
        break;
    case AB_GEAR:
        open_settings();
        break;
    default: break;
    }
}

static NSScrollView *make_text_view(Class cls)
{
    NSScrollView *sv = scroll_round(nil);
    NSSize size = NSMakeSize(400, 200);
    NSTextView *tv = [[cls alloc] initWithFrame:NSMakeRect(0, 0, size.width, size.height)];

    tv.minSize = NSMakeSize(0, size.height);
    tv.maxSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
    tv.verticallyResizable = YES;
    tv.horizontallyResizable = NO;
    tv.autoresizingMask = NSViewWidthSizable;
    tv.textContainer.widthTracksTextView = YES;
    tv.textContainer.containerSize = NSMakeSize(size.width, CGFLOAT_MAX);
    tv.textContainerInset = NSMakeSize(8, 6);

    /* code, not prose: no curly quotes, no -- turning into a dash, no
     * autocorrecting a variable name into a word */
    tv.richText = NO;
    tv.importsGraphics = NO;
    tv.usesFontPanel = NO;
    tv.automaticQuoteSubstitutionEnabled = NO;
    tv.automaticDashSubstitutionEnabled = NO;
    tv.automaticTextReplacementEnabled = NO;
    tv.automaticSpellingCorrectionEnabled = NO;
    tv.automaticLinkDetectionEnabled = NO;
    tv.automaticDataDetectionEnabled = NO;
    tv.automaticTextCompletionEnabled = NO;
    tv.continuousSpellCheckingEnabled = NO;
    tv.grammarCheckingEnabled = NO;
    tv.smartInsertDeleteEnabled = NO;

    sv.documentView = tv;
    return sv;
}

static void build_window(void)
{
    NSRect frame = NSMakeRect(0, 0, 1000, 720);
    NSMenu *fileMenu;

    g_win = [[NSWindow alloc] initWithContentRect:frame
                                        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                  NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
    g_win.title = @"Adda";
    g_win.releasedWhenClosed = NO;
    g_win.contentMinSize = NSMakeSize(560, 360);
    g_win.titlebarAppearsTransparent = YES;
    g_win.tabbingMode = NSWindowTabbingModeDisallowed;
    g_win.collectionBehavior |= NSWindowCollectionBehaviorFullScreenPrimary;
    g_win.delegate = g_adda;

    g_main = [[MainView alloc] initWithFrame:frame];
    g_win.contentView = g_main;

    g_codeScroll = make_text_view([CodeView class]);
    g_code = g_codeScroll.documentView;
    g_code.allowsUndo = YES;

    {
        LineNumbers *ln = [[LineNumbers alloc] initWithScrollView:g_codeScroll
                                                      orientation:NSVerticalRuler];
        ln.clientView = g_code;
        ln.ruleThickness = 44;
        g_codeScroll.verticalRulerView = ln;
        g_codeScroll.hasVerticalRuler = YES;
        g_codeScroll.rulersVisible = YES;
        /* a new or removed line renumbers everything below it */
        [[NSNotificationCenter defaultCenter]
            addObserverForName:NSTextDidChangeNotification object:g_code queue:nil
                    usingBlock:^(NSNotification *n) {
                        (void)n;
                        ln.needsDisplay = YES;
                        clear_check();       /* the marks no longer line up */
                    }];
    }

    /* Undo could resurrect output we trimmed, or unwind an append, so the
     * console simply has no undo. */
    g_consoleScroll = make_text_view([ConsoleView class]);
    g_console = g_consoleScroll.documentView;
    g_console.allowsUndo = NO;
    g_console.delegate = g_adda;

    g_find = [[NSTextField alloc] initWithFrame:NSZeroRect];
    g_find.bordered = NO;
    g_find.bezeled = NO;
    g_find.drawsBackground = YES;
    g_find.focusRingType = NSFocusRingTypeNone;
    g_find.font = g_fontUI;
    g_find.placeholderString = @"Find in your code";
    g_find.cell.scrollable = YES;
    g_find.cell.wraps = NO;
    g_find.delegate = g_adda;

    g_files = (FileTable *)make_table([FileTable class], 22);
    g_files.outlineTableColumn = g_files.tableColumns[0];
    g_files.indentationPerLevel = 14;
    /* the one column's own autoresizing style is not enough for an outline
     * view - without this its column keeps NSTableColumn's narrow default
     * width regardless, and every row's label is clipped down to a letter */
    g_files.autoresizesOutlineColumn = YES;
    fileMenu = [NSMenu new];
    fileMenu.delegate = g_adda;         /* filled in when it opens */
    g_files.menu = fileMenu;
    g_filesScroll = scroll_round(g_files);

    [g_main addSubview:g_filesScroll];
    [g_main addSubview:g_find];
    [g_main addSubview:g_codeScroll];
    [g_main addSubview:g_consoleScroll];

    g_code.string = @"[name] = ask What is your name?\n"
                    @"print Hello, [name]";

    apply_theme();
    layout();
    [g_win center];
}

/* ════════════════════════════════════════════════════ the menu bar ══ */

static NSMenu *menu_in(NSMenu *bar, NSString *title)
{
    NSMenuItem *top = [bar addItemWithTitle:title action:NULL keyEquivalent:@""];
    NSMenu *m = [[NSMenu alloc] initWithTitle:title];
    top.submenu = m;
    return m;
}

/* nil target: the action goes to whatever has the focus */
static void add(NSMenu *m, NSString *title, SEL action, NSString *key,
                NSEventModifierFlags mods, id target)
{
    NSMenuItem *i = [m addItemWithTitle:title action:action keyEquivalent:key];
    i.keyEquivalentModifierMask = mods;
    i.target = target;
}

/* The Explorer's + button: a plain two-item menu, popped up at the point
 * clicked rather than under a real NSButton, to match how every other icon
 * in this window is its own hand-drawn hit region rather than a control. */
static void show_add_menu(NSPoint p)
{
    NSMenu *m = [NSMenu new];
    add(m, @"New File", @selector(newFile:), @"", 0, g_adda);
    add(m, @"New Folder", @selector(newFolder:), @"", 0, g_adda);
    [m popUpMenuPositioningItem:nil atLocation:p inView:g_main];
}

static void build_menu(void)
{
    const NSEventModifierFlags CMD = NSEventModifierFlagCommand;
    const NSEventModifierFlags SHIFT = NSEventModifierFlagShift;
    NSMenu *bar = [NSMenu new], *m;

    m = menu_in(bar, @"Adda");
    add(m, @"About Adda", @selector(orderFrontStandardAboutPanel:), @"", 0, nil);
    [m addItem:NSMenuItem.separatorItem];
    add(m, @"Settings…", @selector(openSettings:), @",", CMD, g_adda);
    [m addItem:NSMenuItem.separatorItem];
    add(m, @"Hide Adda", @selector(hide:), @"h", CMD, nil);
    add(m, @"Hide Others", @selector(hideOtherApplications:), @"h",
        CMD | NSEventModifierFlagOption, nil);
    add(m, @"Show All", @selector(unhideAllApplications:), @"", 0, nil);
    [m addItem:NSMenuItem.separatorItem];
    add(m, @"Quit Adda", @selector(terminate:), @"q", CMD, nil);

    m = menu_in(bar, @"File");
    add(m, @"New File", @selector(newFile:), @"n", CMD, g_adda);
    add(m, @"New Folder", @selector(newFolder:), @"n", CMD | SHIFT, g_adda);
    [m addItem:NSMenuItem.separatorItem];
    add(m, @"Close Window", @selector(performClose:), @"w", CMD, nil);

    /* without these, Cmd+C and friends do nothing in a text view */
    m = menu_in(bar, @"Edit");
    add(m, @"Undo", @selector(undo:), @"z", CMD, nil);
    add(m, @"Redo", @selector(redo:), @"z", CMD | SHIFT, nil);
    [m addItem:NSMenuItem.separatorItem];
    add(m, @"Cut", @selector(cut:), @"x", CMD, nil);
    add(m, @"Copy", @selector(copy:), @"c", CMD, nil);
    add(m, @"Paste", @selector(paste:), @"v", CMD, nil);
    add(m, @"Select All", @selector(selectAll:), @"a", CMD, nil);
    [m addItem:NSMenuItem.separatorItem];
    add(m, @"Find…", @selector(showSearch:), @"f", CMD, g_adda);
    add(m, @"Find Next", @selector(findNext:), @"g", CMD, g_adda);

    m = menu_in(bar, @"Run");
    add(m, @"Run", @selector(runCode:), @"r", CMD, g_adda);
    add(m, @"Stop", @selector(stopCode:), @".", CMD, g_adda);

    m = menu_in(bar, @"View");
    add(m, @"Explorer", @selector(showExplorer:), @"e", CMD | SHIFT, g_adda);
    add(m, @"Search", @selector(showSearch:), @"f", CMD | SHIFT, g_adda);
    add(m, @"Cheat Sheet", @selector(openCheats:), @"", 0, g_adda);
    [m addItem:NSMenuItem.separatorItem];
    add(m, @"Enter Full Screen", @selector(toggleFullScreen:), @"f",
        CMD | NSEventModifierFlagControl, nil);

    m = menu_in(bar, @"Window");
    add(m, @"Minimize", @selector(performMiniaturize:), @"m", CMD, nil);
    add(m, @"Zoom", @selector(performZoom:), @"", 0, nil);
    NSApp.windowsMenu = m;

    NSApp.mainMenu = bar;
}

/* ════════════════════════════════════════════════════════ views ══ */

@implementation MainView {
    NSTrackingArea *_track;
}

- (BOOL)isFlipped { return YES; }

/* a click on an icon acts even when the window was not in front */
- (BOOL)acceptsFirstMouse:(NSEvent *)e { (void)e; return YES; }

- (void)drawRect:(NSRect)dirty { (void)dirty; paint_main(); }

- (void)resizeSubviewsWithOldSize:(NSSize)old { (void)old; layout(); }

- (void)updateTrackingAreas
{
    if (_track) [self removeTrackingArea:_track];
    _track = [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                          options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited |
                                                  NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
                                            owner:self userInfo:nil];
    [self addTrackingArea:_track];
    [super updateTrackingAreas];
}

- (void)resetCursorRects
{
    [self addCursorRect:g_splitHit cursor:NSCursor.resizeUpDownCursor];
}

- (void)mouseMoved:(NSEvent *)e
{
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    int hit = ab_hit(p);
    BOOL overAdd = (g_view == AB_EXPLORER) && NSPointInRect(p, g_addRect);

    if (hit != g_abHot) {
        g_abHot = hit;
        animate();
        /* only the strip changed */
        [self setNeedsDisplayInRect:NSMakeRect(0, 0, 48, NSHeight(self.bounds))];
    }
    if (overAdd != g_addHot) {
        g_addHot = overAdd;
        animate();
        [self setNeedsDisplayInRect:NSInsetRect(g_addRect, -4, -4)];
    }
    {
        int bar = bar_hit(p);
        if (bar != g_barHot) { g_barHot = bar; animate(); }
    }
}

- (void)mouseExited:(NSEvent *)e
{
    (void)e;
    g_abHot = -1;
    g_addHot = NO;
    g_barHot = -1;
    animate();
}

- (void)mouseDown:(NSEvent *)e
{
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    int at;

    at = bar_hit(p);
    if (at >= 0) {
        press(PRESS_BAR, at);
        if (at == BAR_SAVE) save_as(); else import_files();
        return;
    }
    if (g_view == AB_EXPLORER && NSPointInRect(p, g_addRect)) {
        press(PRESS_ADD, 0);
        show_add_menu(p);
        return;
    }
    if (NSPointInRect(p, g_splitHit)) {
        if (e.clickCount == 2) {          /* back to even */
            g_split = 0.5;
            layout();
            return;
        }
        g_dragging = YES;
        g_dragDY = p.y - NSMinY(g_splitRect);
        return;
    }
    at = ab_hit(p);
    if (at >= 0) {
        press(PRESS_AB, at);
        ab_click(at);
    }
}

- (void)mouseDragged:(NSEvent *)e
{
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    CGFloat toolH = 12, splitH = 7, minPane = 60, track, codeH;

    if (!g_dragging) return;
    track = NSHeight(self.bounds) - toolH - splitH;
    if (track < 2 * minPane) return;

    codeH = (p.y - g_dragDY) - toolH;
    if (codeH < minPane)         codeH = minPane;
    if (codeH > track - minPane) codeH = track - minPane;
    g_split = codeH / track;
    layout();
}

- (void)mouseUp:(NSEvent *)e
{
    (void)e;
    g_dragging = NO;
}

@end

@implementation ConsoleView

/* Return sends the line. Nothing else that makes a new line gets one in. */
- (void)insertNewline:(id)sender { (void)sender; send_line(); }
- (void)insertNewlineIgnoringFieldEditor:(id)sender { (void)sender; }
- (void)insertLineBreak:(id)sender { (void)sender; }
- (void)insertParagraphSeparator:(id)sender { (void)sender; }
- (void)insertTab:(id)sender { (void)sender; }
- (void)insertBacktab:(id)sender { (void)sender; }

/* Esc wipes the half-typed line */
- (void)cancelOperation:(id)sender
{
    (void)sender;
    if (g_running) console_clear_pending();
}

@end

@implementation FileTable

- (void)keyDown:(NSEvent *)e
{
    switch (e.keyCode) {
    case KEY_RETURN:
    case KEY_ENTER:
    case KEY_F2:        /* Return renames in the Finder; F2 is what gui.c uses */
        begin_rename(self.selectedRow);
        return;
    case KEY_DELETE:
    case KEY_FWD_DEL:
        delete_file(self.selectedRow);
        return;
    default:
        [super keyDown:e];
    }
}

@end

@implementation CheatTable

- (void)keyDown:(NSEvent *)e
{
    if (e.keyCode == KEY_RETURN || e.keyCode == KEY_ENTER) {
        insert_cheat(self.selectedRow);
        return;
    }
    [super keyDown:e];
}

@end

@implementation ThemedRow

- (void)drawSelectionInRect:(NSRect)dirty
{
    (void)dirty;
    round_fill(NSInsetRect(self.bounds, 4, 1), g_t.sel, 6);
}

/* Emphasised rows turn their text white, which is wrong on a pale theme's
 * selection colour. */
- (BOOL)isEmphasized { return NO; }

@end

@implementation Cell

- (BOOL)isFlipped { return YES; }

/* Only the cheat sheet's two-line rows still need this: a fixed cell that
 * never resizes live, so plain frame math is simpler than constraints for
 * its two stacked labels. A file row's single label is pinned with
 * constraints instead (see make_cell) - see that comment for why. */
- (void)layout
{
    NSRect b = self.bounds;

    [super layout];
    if (self.sub) {
        self.textField.frame = NSMakeRect(12, 6, NSWidth(b) - 24, 18);
        self.sub.frame = NSMakeRect(12, 24, NSWidth(b) - 24, 17);
    }
}

@end

@implementation SettingsView {
    NSTrackingArea *_track;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { (void)e; return YES; }

- (void)drawRect:(NSRect)dirty { (void)dirty; settings_paint(self.bounds); }

- (void)updateTrackingAreas
{
    if (_track) [self removeTrackingArea:_track];
    _track = [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                          options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited |
                                                  NSTrackingActiveAlways | NSTrackingInVisibleRect
                                            owner:self userInfo:nil];
    [self addTrackingArea:_track];
    [super updateTrackingAreas];
}

- (void)mouseMoved:(NSEvent *)e
{
    int was = g_setRowHot;
    g_setRowHot = settings_row_at([self convertPoint:e.locationInWindow fromView:nil]);
    if (g_setRowHot != was) animate();
}

- (void)mouseExited:(NSEvent *)e
{
    (void)e;
    if (g_setRowHot != -1) { g_setRowHot = -1; animate(); }
}

- (void)mouseDown:(NSEvent *)e
{
    int row = settings_row_at([self convertPoint:e.locationInWindow fromView:nil]);
    if (row >= 0) {
        press(PRESS_ROW, row);
        g_pick = row;
        save_pick();
        apply_theme();
    }
}

- (void)keyDown:(NSEvent *)e
{
    if (e.keyCode == KEY_ESCAPE) { [self.window close]; return; }
    [super keyDown:e];
}

@end

@implementation CheatsView

- (BOOL)isFlipped { return YES; }

- (void)drawRect:(NSRect)dirty
{
    NSRect rc = self.bounds;

    (void)dirty;
    fill_rect(rc, g_t.surface);
    text_at(NSMakeRect(14, NSHeight(rc) - 8 - 18, NSWidth(rc) - 28, 18),
            @"Double-click or press Return to put it in your code",
            g_fontSmall, g_t.muted, T_LEFT);
}

@end

@implementation Popup

/* Esc closes whichever popup has focus */
- (void)cancelOperation:(id)sender { (void)sender; [self close]; }

@end

/* moves one glow toward its target; YES while it still has somewhere to go */
static BOOL approach(double *g, BOOL on, double step)
{
    double to = on ? 1 : 0;
    if (*g < to) { *g += step; if (*g > to) *g = to; }
    else if (*g > to) { *g -= step; if (*g < to) *g = to; }
    return *g != to;
}

static CFTimeInterval g_lastTick;

static void animate(void)
{
    if (g_anim) return;
    g_lastTick = CACurrentMediaTime();
    g_anim = [NSTimer timerWithTimeInterval:1.0 / 60 repeats:YES block:^(NSTimer *t) {
        CFTimeInterval now = CACurrentMediaTime();
        double step = (now - g_lastTick) / HOVER_SECS;
        BOOL moving = NO;
        int i;

        g_lastTick = now;
        for (i = 0; i < AB_COUNT; i++) {
            BOOL disabled = (i == AB_RUN && g_running) || (i == AB_STOP && !g_running);
            moving |= approach(&g_abGlow[i], g_abHot == i && !disabled, step);
        }
        for (i = 0; i < BAR_COUNT; i++)
            moving |= approach(&g_barGlow[i], g_barHot == i, step);
        for (i = 0; i < PICK_COUNT; i++)
            moving |= approach(&g_rowGlow[i], g_setRowHot == i, step);
        moving |= approach(&g_addGlow, g_addHot, step);

        if (g_pressKind != PRESS_NONE) {
            if (now - g_pressAt >= PRESS_SECS) g_pressKind = PRESS_NONE;
            else moving = YES;
        }

        g_main.needsDisplay = YES;
        g_settingsView.needsDisplay = YES;
        if (!moving) {
            [t invalidate];
            g_anim = nil;
        }
    }];
    /* common modes, so it keeps going while a menu is tracking */
    [[NSRunLoop currentRunLoop] addTimer:g_anim forMode:NSRunLoopCommonModes];
}

static void press(int kind, int idx)
{
    g_pressKind = kind;
    g_pressIdx = idx;
    g_pressAt = CACurrentMediaTime();
    animate();
}

/* ═══════════════════════════════════════════ checking for mistakes ══ */

#define CHECK_LINE  0xFFE066u         /* the whole line with a mistake */
#define CHECK_SPOT  0xE5484Du         /* the exact thing that is wrong */

static NSMutableArray<NSValue *> *g_checkLines;   /* character ranges */

static void clear_check(void)
{
    NSLayoutManager *lm = g_code.layoutManager;
    if (!g_checkLines.count) return;
    [g_checkLines removeAllObjects];
    [lm removeTemporaryAttribute:NSBackgroundColorAttributeName
               forCharacterRange:NSMakeRange(0, g_code.string.length)];
    [lm removeTemporaryAttribute:NSForegroundColorAttributeName
               forCharacterRange:NSMakeRange(0, g_code.string.length)];
    g_code.needsDisplay = YES;
}

/* Byte offset within a line (what adda --check reports) to a character one. */
static NSUInteger chars_for_bytes(NSString *line, int bytes)
{
    NSData *d = [line dataUsingEncoding:NSUTF8StringEncoding];
    NSString *head;
    if (bytes <= 0) return 0;
    if ((NSUInteger)bytes >= d.length) return line.length;
    head = [[NSString alloc] initWithBytes:d.bytes length:(NSUInteger)bytes
                                  encoding:NSUTF8StringEncoding];
    return head ? head.length : (NSUInteger)bytes;
}

/* Runs adda --check over what is in the editor, paints each line with a
 * mistake yellow and the exact spot red, and lists them in the console. */
static void check_code(void)
{
    NSString *path = [NSTemporaryDirectory() stringByAppendingPathComponent:@"_adda_check.adda"];
    NSString *code = g_code.string, *out;
    NSArray<NSString *> *lines;
    NSMutableString *report = [NSMutableString string];
    NSLayoutManager *lm = g_code.layoutManager;
    NSTask *task = [NSTask new];
    NSPipe *pipe = [NSPipe pipe];
    NSData *data;
    int found = 0;

    clear_check();
    if (!g_checkLines) g_checkLines = [NSMutableArray array];

    if (![code writeToFile:path atomically:NO encoding:NSUTF8StringEncoding error:NULL])
        return;
    task.executableURL = [NSURL fileURLWithPath:adda_path()];
    task.arguments = @[ @"--check", path ];
    task.standardOutput = pipe;
    task.standardError = [NSFileHandle fileHandleWithNullDevice];
    if (![task launchAndReturnError:NULL]) {
        warn(@"Could not find the adda program to check your code with.");
        return;
    }
    data = [pipe.fileHandleForReading readDataToEndOfFile];
    [task waitUntilExit];
    unlink(path.fileSystemRepresentation);

    out = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    lines = [code componentsSeparatedByString:@"\n"];

    for (NSString *row in [out componentsSeparatedByString:@"\n"]) {
        NSScanner *sc = [NSScanner scannerWithString:row];
        int line, colB, lenB;
        NSUInteger start = 0, i, a, b;
        NSString *msg, *text;
        NSRange whole, spot;

        if (![sc scanInt:&line] || ![sc scanInt:&colB] || ![sc scanInt:&lenB]) continue;
        if (line < 1 || (NSUInteger)line > lines.count) continue;
        msg = sc.scanLocation + 1 < row.length ? [row substringFromIndex:sc.scanLocation + 1] : @"";

        for (i = 0; i + 1 < (NSUInteger)line; i++) start += lines[i].length + 1;
        text = lines[(NSUInteger)line - 1];
        if ([text hasSuffix:@"\r"]) text = [text substringToIndex:text.length - 1];

        whole = NSMakeRange(start, text.length);
        a = chars_for_bytes(text, colB);
        b = chars_for_bytes(text, colB + lenB);
        spot = NSMakeRange(start + a, b > a ? b - a : 0);

        [g_checkLines addObject:[NSValue valueWithRange:whole]];
        [lm addTemporaryAttribute:NSForegroundColorAttributeName value:col(0x1A1A1A)
                forCharacterRange:whole];
        if (spot.length) {
            [lm addTemporaryAttribute:NSBackgroundColorAttributeName value:col(CHECK_SPOT)
                    forCharacterRange:spot];
            [lm addTemporaryAttribute:NSForegroundColorAttributeName value:NSColor.whiteColor
                    forCharacterRange:spot];
        }
        [report appendFormat:@"  line %d: %@\n", line, msg];
        found++;
    }
    g_code.needsDisplay = YES;

    if (found && g_checkLines.count) {
        NSRange first = g_checkLines[0].rangeValue;
        [g_code scrollRangeToVisible:first];
    }
    if (!g_running) {
        if (found == 0)
            console_append(@"No mistakes found.\n");
        else
            console_append([NSString stringWithFormat:@"%d mistake%s found:\n%@",
                            found, found == 1 ? "" : "s", report]);
    }
}

@implementation CodeView

- (void)drawViewBackgroundInRect:(NSRect)r
{
    NSLayoutManager *lm = self.layoutManager;
    CGFloat x0 = NSMinX(self.bounds), w = NSWidth(self.bounds);
    NSPoint o = self.textContainerOrigin;

    [super drawViewBackgroundInRect:r];
    [col(CHECK_LINE) setFill];
    for (NSValue *v in g_checkLines) {
        NSRange g = [lm glyphRangeForCharacterRange:v.rangeValue actualCharacterRange:NULL];
        if (g.length == 0) continue;
        [lm enumerateLineFragmentsForGlyphRange:g
            usingBlock:^(NSRect frag, NSRect used, NSTextContainer *c, NSRange gr, BOOL *stop) {
            (void)used; (void)c; (void)gr; (void)stop;
            NSRectFill(NSMakeRect(x0, NSMinY(frag) + o.y, w, NSHeight(frag)));
        }];
    }
}

@end

/* ═════════════════════════════════════════════════ line numbers ══ */

@implementation LineNumbers

- (BOOL)isFlipped { return YES; }

/* The code box wraps long lines, so a number goes only on the first row of
 * each real line; the rows a long line wraps onto get none. */
- (void)drawHashMarksAndLabelsInRect:(NSRect)dirty
{
    NSTextView *tv = (NSTextView *)self.clientView;
    NSLayoutManager *lm = tv.layoutManager;
    NSString *text = tv.string;
    NSUInteger len = text.length, i;
    NSRect visible = [tv visibleRect];
    NSRange glyphs, chars;
    __block NSUInteger line = 1;
    CGFloat inset = tv.textContainerOrigin.y;
    NSDictionary *attrs = @{ NSFontAttributeName: g_fontMono,
                             NSForegroundColorAttributeName: col(g_t.muted) };
    CGFloat right = NSWidth(self.bounds) - 8;

    (void)dirty;
    fill_rect(self.bounds, g_t.surface);
    fill_rect(NSMakeRect(NSWidth(self.bounds) - 1, 6, 1, NSHeight(self.bounds) - 12),
              g_t.border);

    glyphs = [lm glyphRangeForBoundingRect:NSOffsetRect(visible, 0, -inset)
                           inTextContainer:tv.textContainer];
    chars = [lm characterRangeForGlyphRange:glyphs actualGlyphRange:NULL];

    for (i = 0; i < chars.location && i < len; i++)
        if ([text characterAtIndex:i] == '\n') line++;

    void (^label)(NSUInteger, CGFloat, CGFloat) = ^(NSUInteger n, CGFloat y, CGFloat h) {
        NSString *s = [NSString stringWithFormat:@"%lu", (unsigned long)n];
        NSSize sz = [s sizeWithAttributes:attrs];
        NSPoint at = [self convertPoint:NSMakePoint(0, y + inset) fromView:tv];
        [s drawAtPoint:NSMakePoint(right - sz.width, at.y + (h - sz.height) / 2)
        withAttributes:attrs];
    };

    __block BOOL firstFrag = YES;
    [lm enumerateLineFragmentsForGlyphRange:glyphs
        usingBlock:^(NSRect r, NSRect used, NSTextContainer *c, NSRange g, BOOL *stop) {
        NSUInteger at = [lm characterIndexForGlyphAtIndex:g.location];
        BOOL starts = (at == 0) || [text characterAtIndex:at - 1] == '\n';
        (void)used; (void)c; (void)stop;
        if (starts && !firstFrag) line++;
        firstFrag = NO;
        if (starts) label(line, NSMinY(r), NSHeight(r));
    }];

    /* the empty last line after a trailing newline has no glyphs of its own */
    if (!NSIsEmptyRect(lm.extraLineFragmentRect)) {
        NSRect r = lm.extraLineFragmentRect;
        NSUInteger n = 1;
        for (i = 0; i < len; i++)
            if ([text characterAtIndex:i] == '\n') n++;
        label(n, NSMinY(r), NSHeight(r));
    }
}

@end

/* ═══════════════════════════════════════════════ the controller ══ */

static Cell *make_cell(NSTableView *tv, BOOL twoLines)
{
    NSString *ident = twoLines ? @"cheat" : @"file";
    Cell *c = [tv makeViewWithIdentifier:ident owner:nil];
    NSTextField *f;

    if (c) return c;

    c = [[Cell alloc] initWithFrame:NSZeroRect];
    c.identifier = ident;

    f = [NSTextField labelWithString:@""];
    f.lineBreakMode = NSLineBreakByTruncatingTail;
    f.delegate = g_adda;                /* for renaming */
    [c addSubview:f];
    c.textField = f;

    if (twoLines) {
        /* the cheat sheet's rows never resize live, so -layout's plain
         * frame math (below) is all they need */
        f.translatesAutoresizingMaskIntoConstraints = YES;
        f = [NSTextField labelWithString:@""];
        f.lineBreakMode = NSLineBreakByTruncatingTail;
        f.translatesAutoresizingMaskIntoConstraints = YES;
        [c addSubview:f];
        c.sub = f;
    } else {
        /* a file row's cell resizes live while the Explorer's expand/collapse
         * animates a row open or shut. -layout only fires once for that,
         * against whatever width the row had at that passing instant, and a
         * frame set by hand then stays there - constraints are what track an
         * animating bounds correctly. */
        f.translatesAutoresizingMaskIntoConstraints = NO;
        [NSLayoutConstraint activateConstraints:@[
            [f.leadingAnchor constraintEqualToAnchor:c.leadingAnchor constant:6],
            [f.trailingAnchor constraintEqualToAnchor:c.trailingAnchor constant:-6],
            [f.centerYAnchor constraintEqualToAnchor:c.centerYAnchor],
        ]];
    }
    return c;
}

/* A generic document/folder glyph rather than this project's own hand-drawn
 * set: a row in a scrolling list is not one of the handful of fixed controls
 * the rest of the window draws itself. Baked as a plain (non-template) image
 * at the theme colour, because it goes into a text attachment - the field's
 * own tinting has no say over that. */
static NSImage *tinted_symbol(NSString *name, unsigned colour, CGFloat pointSize)
{
    NSImageSymbolConfiguration *cfg =
        [NSImageSymbolConfiguration configurationWithPointSize:pointSize
                                                          weight:NSFontWeightRegular];
    NSImage *base = [[NSImage imageWithSystemSymbolName:name accessibilityDescription:nil]
                        imageWithSymbolConfiguration:cfg];
    NSColor *tint = col(colour);

    return [NSImage imageWithSize:base.size flipped:NO drawingHandler:^BOOL(NSRect rect) {
        [base drawInRect:rect];
        [tint set];
        NSRectFillUsingOperation(rect, NSCompositingOperationSourceAtop);
        return YES;
    }];
}

/* The icon rides as the first character of the row's own label, rather than
 * a sibling image view: NSTableCellView's imageView outlet brings its own
 * Auto Layout, which fought the frames this cell sets by hand on reuse. */
static NSAttributedString *tree_label(NSString *name, BOOL isDir)
{
    NSTextAttachment *att = [NSTextAttachment new];
    NSMutableAttributedString *s;

    att.image = tinted_symbol(isDir ? @"folder.fill" : @"doc.text",
                              isDir ? g_t.text : g_t.muted, 12);
    att.bounds = NSMakeRect(0, -3, 15, 14);

    s = [[NSMutableAttributedString alloc]
            initWithAttributedString:[NSAttributedString attributedStringWithAttachment:att]];
    [s appendAttributedString:[[NSAttributedString alloc]
        initWithString:[@"  " stringByAppendingString:name]
             attributes:@{ NSFontAttributeName: g_fontUI,
                           NSForegroundColorAttributeName: col(g_t.text) }]];
    return s;
}

@implementation Adda

- (void)applicationDidFinishLaunching:(NSNotification *)n
{
    (void)n;

    /* a write to a program that has already gone must fail, not kill us */
    signal(SIGPIPE, SIG_IGN);

    g_carry = [NSMutableData data];
    g_inq = [NSMutableData data];
    g_treeCache = [NSMutableDictionary dictionary];
    g_home = home_dir();

    load_pick();
    build_fonts();
    g_t = theme_for(g_pick);
    build_menu();
    build_window();
    rescan_files();

    /* Follow macOS has to notice when macOS changes */
    [NSApp addObserver:self forKeyPath:@"effectiveAppearance" options:0 context:NULL];

    /* Cmd+Return runs from anywhere, as Ctrl+Enter does in gui.c */
    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                          handler:^NSEvent *(NSEvent *e) {
        NSEventModifierFlags mods = e.modifierFlags &
            (NSEventModifierFlagCommand | NSEventModifierFlagShift |
             NSEventModifierFlagOption | NSEventModifierFlagControl);
        if ((e.keyCode == KEY_RETURN || e.keyCode == KEY_ENTER) &&
            mods == NSEventModifierFlagCommand && !NSApp.modalWindow) {
            run_code();
            return nil;
        }
        return e;
    }];

    [g_win makeKeyAndOrderFront:nil];
    [g_win makeFirstResponder:g_code];
    [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationSupportsSecureRestorableState:(NSApplication *)app
{
    (void)app;
    return YES;
}

/* closing the main window quits, as it does on Windows */
- (void)windowWillClose:(NSNotification *)n
{
    if (n.object == g_win) [NSApp terminate:nil];
}

- (void)applicationWillTerminate:(NSNotification *)n
{
    (void)n;
    stop_code();
}

- (void)observeValueForKeyPath:(NSString *)key ofObject:(id)obj
                        change:(NSDictionary *)change context:(void *)ctx
{
    (void)key; (void)obj; (void)change; (void)ctx;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (g_pick == PICK_SYSTEM) apply_theme();
    });
}

/* ── menu actions ─────────────────────────────────────────────── */

- (void)runCode:(id)sender       { (void)sender; run_code(); }
- (void)stopCode:(id)sender      { (void)sender; stop_code(); }
- (void)openSettings:(id)sender  { (void)sender; open_settings(); }
- (void)openCheats:(id)sender    { (void)sender; open_cheats(); }
- (void)findNext:(id)sender      { (void)sender; find_in_code(YES); }

- (void)showExplorer:(id)sender
{
    (void)sender;
    if (g_view != AB_EXPLORER) ab_click(AB_EXPLORER);
    [g_win makeKeyAndOrderFront:nil];
    [g_win makeFirstResponder:g_files];
}

- (void)showSearch:(id)sender
{
    (void)sender;
    if (g_view != AB_SEARCH) ab_click(AB_SEARCH);
    [g_win makeKeyAndOrderFront:nil];
    [g_win makeFirstResponder:g_find];
}

- (void)insertClickedCheat:(id)sender
{
    (void)sender;
    insert_cheat(g_cheatList.clickedRow);
}

/* acts on the right-clicked row, which the Mac rings but does not select */
- (void)renameFile:(id)sender
{
    (void)sender;
    begin_rename(g_files.clickedRow >= 0 ? g_files.clickedRow : g_files.selectedRow);
}

- (void)trashFile:(id)sender
{
    (void)sender;
    delete_file(g_files.clickedRow >= 0 ? g_files.clickedRow : g_files.selectedRow);
}

/* The + button in the Explorer, and the matching File menu items. Either
 * makes a placeholder with a name nobody chose, inside the selected folder
 * (or the root, nothing being selected), and starts renaming it at once. */
- (void)newFile:(id)sender
{
    (void)sender;
    if (g_view != AB_EXPLORER) ab_click(AB_EXPLORER);
    create_and_edit(unique_path(target_dir(), @"untitled", @"adda"), NO);
}

- (void)newFolder:(id)sender
{
    (void)sender;
    if (g_view != AB_EXPLORER) ab_click(AB_EXPLORER);
    create_and_edit(unique_path(target_dir(), @"New Folder", @""), YES);
}

- (BOOL)validateMenuItem:(NSMenuItem *)item
{
    if (item.action == @selector(runCode:))  return !g_running;
    if (item.action == @selector(stopCode:)) return g_running;
    return YES;
}

/* The Explorer's right-click menu. Built as it opens, and left empty for a
 * click below the last file, so no menu shows at all. */
- (void)menuNeedsUpdate:(NSMenu *)menu
{
    [menu removeAllItems];
    if (menu != g_files.menu || g_files.clickedRow < 0) return;
    add(menu, @"Rename", @selector(renameFile:), @"", 0, self);
    add(menu, @"Move to Trash", @selector(trashFile:), @"", 0, self);
}

/* ── the console ──────────────────────────────────────────────── */

/* Every edit to the console comes through here first. Whatever lies before
 * the anchor is history and stays as it is. */
- (BOOL)textView:(NSTextView *)tv shouldChangeTextInRanges:(NSArray<NSValue *> *)ranges
                                          replacementStrings:(NSArray<NSString *> *)strings
{
    NSRange r;
    NSString *s;
    NSUInteger end;

    if (tv != g_console) return YES;
    if (!g_running) return NO;                      /* nothing is listening */
    if (!strings || ranges.count != 1) return NO;   /* restyling, or several places at once */

    r = ranges[0].rangeValue;
    s = strings[0];
    end = NSMaxRange(r);
    if (r.location >= g_anchor) return YES;         /* all in the line being typed */

    if (!s.length) {                                /* deleting */
        /* wholly in the history: there is nothing of ours to eat */
        if (end <= g_anchor) return NO;
        /* straddles: keep our part */
        console_replace(NSMakeRange(g_anchor, end - g_anchor), @"");
        g_console.selectedRange = NSMakeRange(g_anchor, 0);
        return NO;
    }

    /* typing or pasting with the caret up in the history goes on the end */
    console_replace(NSMakeRange(console_len(), 0), s);
    g_console.selectedRange = NSMakeRange(console_len(), 0);
    [g_console scrollRangeToVisible:g_console.selectedRange];
    return NO;
}

/* ── the find box, the cheat search and the rename box ────────── */

- (void)controlTextDidChange:(NSNotification *)n
{
    if (n.object == g_find) find_in_code(NO);
    else if (n.object == g_cheatFind) refresh_cheats();
}

- (BOOL)control:(NSControl *)c textView:(NSTextView *)tv doCommandBySelector:(SEL)cmd
{
    (void)tv;

    if (c == g_find) {
        /* Return jumps to the next match */
        if (cmd == @selector(insertNewline:)) { find_in_code(YES); return YES; }
        /* a text field's Esc offers word completions; nothing wants those here */
        if (cmd == @selector(cancelOperation:)) return YES;
        return NO;
    }

    if (c == g_cheatFind) {
        NSInteger row = g_cheatList.selectedRow;

        if (cmd == @selector(insertNewline:)) { insert_cheat(row); return YES; }
        if (cmd == @selector(cancelOperation:)) { [g_cheats close]; return YES; }
        /* the arrows walk the list without leaving the search box */
        if (cmd == @selector(moveDown:) || cmd == @selector(moveUp:)) {
            row += (cmd == @selector(moveDown:)) ? 1 : -1;
            if (row >= 0 && row < g_cheatCount) {
                [g_cheatList selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row]
                         byExtendingSelection:NO];
                [g_cheatList scrollRowToVisible:row];
            }
            return YES;
        }
        return NO;
    }

    if (g_renamePath) {
        if (cmd == @selector(insertNewline:)) {
            g_renameCommit = YES;
            [g_win makeFirstResponder:g_files];
            return YES;
        }
        if (cmd == @selector(cancelOperation:)) {
            g_renameCommit = NO;
            [g_win makeFirstResponder:g_files];
            return YES;
        }
    }
    return NO;
}

/* Ends a rename however it ended: Return set g_renameCommit, anything else -
 * Esc, a click elsewhere - leaves it NO and abandons the new name. */
- (void)controlTextDidEndEditing:(NSNotification *)n
{
    NSTextField *f = n.object;
    NSString *path = g_renamePath;
    NSString *typed = g_renameCommit ? f.stringValue : nil;

    if (f == g_find || f == g_cheatFind || !path) return;

    g_renamePath = nil;
    f.editable = NO;
    f.selectable = NO;
    f.drawsBackground = NO;
    f.stringValue = path.lastPathComponent;
    f.textColor = col(g_t.text);

    /* afterwards, not in the middle of the field finishing: renaming reloads
     * the list the field lives in */
    dispatch_async(dispatch_get_main_queue(), ^{ end_rename(path, typed); });
}

/* ── the file tree ────────────────────────────────────────────── */

- (NSInteger)outlineView:(NSOutlineView *)ov numberOfChildrenOfItem:(NSString *)item
{
    (void)ov;
    return (NSInteger)children_of(dir_for_item(item)).count;
}

- (BOOL)outlineView:(NSOutlineView *)ov isItemExpandable:(NSString *)item
{
    BOOL isDir = NO;
    (void)ov;
    [NSFileManager.defaultManager fileExistsAtPath:item isDirectory:&isDir];
    return isDir;
}

- (id)outlineView:(NSOutlineView *)ov child:(NSInteger)index ofItem:(NSString *)item
{
    (void)ov;
    return children_of(dir_for_item(item))[(NSUInteger)index];
}

- (NSView *)outlineView:(NSOutlineView *)ov viewForTableColumn:(NSTableColumn *)column
                    item:(NSString *)item
{
    Cell *c = make_cell(ov, NO);
    BOOL isDir = NO;

    (void)column;
    [NSFileManager.defaultManager fileExistsAtPath:item isDirectory:&isDir];
    c.textField.attributedStringValue = tree_label(item.lastPathComponent, isDir);
    return c;
}

/* ── the cheat sheet ──────────────────────────────────────────── */

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tv
{
    (void)tv;
    return g_cheatCount;
}

- (NSTableRowView *)tableView:(NSTableView *)tv rowViewForRow:(NSInteger)row
{
    (void)tv; (void)row;
    return [ThemedRow new];
}

- (NSView *)tableView:(NSTableView *)tv viewForTableColumn:(NSTableColumn *)column
                  row:(NSInteger)row
{
    Cell *c = make_cell(tv, YES);

    (void)column;
    c.textField.stringValue = @(CHEATS[g_cheatShown[row]].title);
    c.textField.font = g_fontUIBold;
    c.textField.textColor = col(g_t.text);
    c.sub.stringValue = @(CHEATS[g_cheatShown[row]].about);
    c.sub.font = g_fontSmall;
    c.sub.textColor = col(g_t.muted);
    return c;
}

/* A click or an arrow key on a file opens it; one on a folder just selects
 * it. Selections made in code - after a rename, a delete or a new item -
 * leave the editor alone. */
- (void)tableViewSelectionDidChange:(NSNotification *)n
{
    NSInteger row;
    NSString *item;
    BOOL isDir = NO;

    if (n.object != g_files || g_quiet || g_renamePath) return;
    row = g_files.selectedRow;
    if (row < 0) return;
    item = [g_files itemAtRow:row];
    if (item && [NSFileManager.defaultManager fileExistsAtPath:item isDirectory:&isDir] && !isDir)
        open_file(item);
}

@end

/* ══════════════════════════════════════════════════ entry point ══ */

int main(int argc, const char **argv)
{
    (void)argc; (void)argv;

    @autoreleasepool {
        NSApplication *app = NSApplication.sharedApplication;
        g_adda = [Adda new];
        app.delegate = g_adda;
        app.activationPolicy = NSApplicationActivationPolicyRegular;
        [app run];
    }
    return 0;
}
