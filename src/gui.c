/*
 *  Adda GUI — a Win32 front-end for the Adda interpreter.
 *
 *  Build:  see build.sh (the manifest in adda-gui.rc gives themed controls and
 *          DPI awareness)
 *  Place adda-gui.exe next to adda.exe and double-click to launch.
 *
 *  Shape of the window, VS Code fashion:
 *
 *      +--------+-------------+---------------------------+
 *      | icons  | side panel  | Run / Stop                |
 *      |        | (Explorer   +---------------------------+
 *      |        |  or Search) | code editor               |
 *      |        |             |===== drag to resize ======|
 *      |        |             | console                   |
 *      | book   |             |                           |
 *      | cog    |             |                           |
 *      +--------+-------------+---------------------------+
 *
 *  The console is one EDIT that shows the program's output AND takes what you
 *  type: everything before a moving anchor is fixed, everything after it is
 *  the line you are typing. Enter sends it down the pipe.
 *
 *  Nothing here uses a UI framework. The icons are drawn with GDI at 4x and
 *  scaled down for smooth edges, the activity bar is painted by the parent,
 *  and the buttons are owner-drawn.
 */

#include <windows.h>
#include <windowsx.h>      /* GET_X_LPARAM: LOWORD alone goes wrong on negative
                            * coordinates, which happens under mouse capture */
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <commdlg.h>      /* the Save As and Open dialogs */
#include <shellapi.h>     /* SHFileOperation, so deleting goes to the bin */
#include <shlobj.h>       /* SHBrowseForFolder, for importing a folder */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gui_cheatsheet.h"
#include "tidy.h"
#include "colour.h"

/* ── ids ─────────────────────────────────────────────────────────── */
#define ID_CODE      1001
#define ID_CONSOLE   1002
#define ID_FIND      1005
#define ID_FILES     1006
#define ID_POLL      1           /* timer */
#define ID_ANIM      2           /* timer: click animations */

/* settings / cheat popups */
#define ID_CHEATFIND 1101
#define ID_CHEATLIST 1102

/* ── themes ──────────────────────────────────────────────────────── */
typedef struct {
    COLORREF bg;          /* window and side panel                   */
    COLORREF surface;     /* code and console panels                 */
    COLORREF border;      /* hairlines                               */
    COLORREF text;        /* code and console text                   */
    COLORREF muted;       /* labels, descriptions                    */
    COLORREF accent;      /* Run button, the selected-item bar       */
    COLORREF accentHot;
    COLORREF accentDown;
    COLORREF onAccent;
    COLORREF ghostHot;    /* hover fill for quiet buttons and rows   */
    COLORREF sel;         /* selected row                            */
    COLORREF abBg;        /* activity bar                            */
    COLORREF abHover;     /* activity bar, pointer over an icon      */
    COLORREF abIcon;      /* active icon                             */
    COLORREF abIconDim;   /* inactive icon                           */
    BOOL     dark;
} Theme;

enum { PICK_SYSTEM = 0, PICK_LIGHT, PICK_DARK, PICK_BEIGE, PICK_ABYSS, PICK_COUNT };

static const char *THEME_NAMES[PICK_COUNT] = {
    "Follow Windows", "Light", "Dark", "Beige", "Abyss"
};
static const char *THEME_ABOUT[PICK_COUNT] = {
    "Match whatever Windows is set to",
    "White panels on soft grey",
    "Dark grey panels, light text",
    "The original warm paper",
    "Deep blue night, after the VS Code theme"
};

static const Theme THEME_LIGHT_V = {
    RGB(243,243,243), RGB(255,255,255), RGB(216,216,216),
    RGB(26,26,26),    RGB(97,97,97),
    RGB(0,103,192),   RGB(25,117,197), RGB(0,88,158), RGB(255,255,255),
    RGB(232,232,232), RGB(204,228,247),
    RGB(44,44,44),    RGB(60,60,60),   RGB(255,255,255), RGB(122,122,122),
    FALSE
};

static const Theme THEME_DARK_V = {
    RGB(32,32,32),    RGB(43,43,43),   RGB(61,61,61),
    RGB(232,232,232), RGB(160,160,160),
    RGB(0,120,212),   RGB(26,140,232), RGB(0,95,168), RGB(255,255,255),
    RGB(58,58,58),    RGB(4,57,94),
    RGB(24,24,24),    RGB(40,40,40),   RGB(255,255,255), RGB(134,134,134),
    TRUE
};

/* the original beige, given flat surfaces and a warmer accent */
static const Theme THEME_BEIGE_V = {
    RGB(236,232,208), RGB(245,245,220), RGB(206,200,172),
    RGB(30,30,30),    RGB(110,104,80),
    RGB(140,94,42),   RGB(160,110,52), RGB(118,78,34), RGB(255,255,255),
    RGB(226,220,192), RGB(220,212,176),
    RGB(74,68,54),    RGB(92,85,68),   RGB(245,241,224), RGB(154,144,120),
    FALSE
};

/* Abyss, taken from VS Code's own theme file:
 *   editor.background #000c18   editor.foreground #6688cc
 *   activityBar.background #051336   sideBar.background #060621
 *   panel.border #2b2b4a   button.background #2b3c5d
 *   list.hoverBackground #061940   list.activeSelectionBackground #08286b */
static const Theme THEME_ABYSS_V = {
    RGB(0x06,0x06,0x21), RGB(0x00,0x0c,0x18), RGB(0x2b,0x2b,0x4a),
    RGB(0x66,0x88,0xcc), RGB(0x91,0x91,0x99),
    RGB(0x2b,0x3c,0x5d), RGB(0x3a,0x4e,0x75), RGB(0x22,0x30,0x4a), RGB(255,255,255),
    RGB(0x06,0x19,0x40), RGB(0x08,0x28,0x6b),
    RGB(0x05,0x13,0x36), RGB(0x08,0x28,0x6b), RGB(255,255,255), RGB(0x69,0x71,0x86),
    TRUE
};

static int   g_pick = PICK_SYSTEM;
static Theme g_t;

/* ── windows ─────────────────────────────────────────────────────── */
static HWND  hwndMain;
static HWND  hwndCode, hwndConsole;
static HWND  hwndFind, hwndFiles;
static HWND  hwndSettings, hwndCheats;
static HWND  hwndCheatFind, hwndCheatList;

static HBRUSH hBrushBg, hBrushSurface, hBrushAb;
static HFONT  hFontMono, hFontUI, hFontUIBold, hFontSmall;
static HFONT  hFontTitle, hFontBody;      /* a cheat sheet entry's own page */
static int    g_dpi = 96;

/* ── activity bar ────────────────────────────────────────────────── */
enum { ICON_EXPLORER, ICON_SEARCH, ICON_PLAY, ICON_STOP, ICON_CHEAT, ICON_GEAR,
       ICON_SAVE, ICON_IMPORT, ICON_CHECK, ICON_NEW, ICON_TIDY };
/* New project, Run and Stop sit under Search; Check, Cheat sheet and Settings are pinned
 * to the bottom. Everything before AB_CHECK stacks from the top. */
enum { AB_EXPLORER = 0, AB_SEARCH, AB_NEW, AB_RUN, AB_STOP, AB_TIDY, AB_CHECK, AB_CHEAT, AB_GEAR, AB_COUNT };

static const int AB_ICON[AB_COUNT] = {
    ICON_EXPLORER, ICON_SEARCH, ICON_NEW, ICON_PLAY, ICON_STOP, ICON_TIDY, ICON_CHECK, ICON_CHEAT, ICON_GEAR
};

static int  g_view = AB_EXPLORER;   /* which panel view, or -1 when collapsed */
static int  g_abHot = -1;           /* activity item under the pointer */

/* Click animation: whatever was pressed last shrinks a little and flashes
 * toward the accent colour, then eases back over PRESS_MS. */
#define PRESS_MS  220
static int   g_abPress = -1;        /* activity item being animated        */
static DWORD g_abPressAt;
static int   g_rowPress = -1;       /* settings row being animated         */
static DWORD g_rowPressAt;
static RECT g_abRect[AB_COUNT];

/* the Save and Import buttons along the bottom of the Explorer */
enum { BAR_SAVE, BAR_IMPORT, BAR_COUNT };
static RECT  g_barRect[BAR_COUNT];
static int   g_barHot = -1;
static int   g_barPress = -1;
static DWORD g_barPressAt;

/* Hover: each button's highlight fades in and out over HOVER_MS rather than
 * snapping. 0 is untouched, 1 fully lit; the ID_ANIM timer walks each one
 * toward where the pointer says it should be. */
#define HOVER_MS  120
static double g_abGlow[AB_COUNT];
static double g_barGlow[BAR_COUNT];
static BOOL   g_animOn;             /* main window's ID_ANIM is running    */
static DWORD  g_animTick;

/* ── panes ───────────────────────────────────────────────────────── */
static double g_split = 0.52;       /* share of the editor area given to code */
static BOOL   g_dragging = FALSE;
static int    g_dragDY = 0;         /* grab offset, so the bar does not jump */
static RECT   g_splitRect;
static RECT   g_panelRect;
static HCURSOR g_curNS;

/* fullscreen state */
static BOOL            g_fullscreen = FALSE;
static WINDOWPLACEMENT g_wp;

/* ── the running program ─────────────────────────────────────────── */
static PROCESS_INFORMATION g_pi;
static HANDLE g_out = NULL, g_err = NULL, g_in = NULL;
static BOOL   g_running = FALSE;
static char   g_tmp_file[MAX_PATH * 2];
static char   g_curPath[MAX_PATH * 3];  /* the file in the editor, or "" */
static int    g_anchor = 0;         /* where the typed line starts */

/* ── files in the Explorer ───────────────────────────────────────── */
#define MAX_FILES 128
typedef struct { char name[64]; char path[MAX_PATH]; } FileRow;
static FileRow g_files[MAX_FILES];
static int     g_fileCount;

/* renaming happens in the list itself: an EDIT sits over the row */
static HWND hwndRename;
static int  g_renameIdx = -1;

#define IDM_RENAME 3001
#define IDM_DELETE 3002
#define IDM_RUN    3003
#define IDM_RUNALL 3004
#define WM_RUN_NEXT (WM_APP + 1)     /* Run All Files: start the next program */
#define WM_TIDY     (WM_APP + 2)     /* bring the tidy view up to date */

/* Run All Files: every program in the project, run one after another */
#define MAX_RUN 256
static char g_runPaths[MAX_RUN][MAX_PATH * 2];
static int  g_runCount, g_runNext;
static BOOL g_runAll;                /* a Run All Files is under way */

/* ── cheat sheet filtering ───────────────────────────────────────── */
static const Cheat *g_cheatShown[CHEAT_MAX];
static const Cheat *g_cheatOpen;    /* the entry whose own page is showing, or NULL */
static RECT  g_cheatBack;           /* the Back button on that page */
static BOOL  g_cheatBackHot;
static DWORD g_cheatTick;           /* when a click last turned a page or opened one */
static int g_cheatCount;

#define S(x) MulDiv((x), g_dpi, 96)

static void apply_theme(HWND hwnd);
static void layout(HWND hwnd);
static void open_settings(HWND owner);
static void open_cheats(HWND owner);
static void refresh_cheats(void);
static void save_current(void);
static void inq_clear(void);

/* ════════════════════════════════════════════════════════ theme ══ */

static BOOL system_is_dark(void)
{
    HKEY key;
    DWORD light = 1, size = sizeof(light);

    if (RegOpenKeyExA(HKEY_CURRENT_USER,
            "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &key) == ERROR_SUCCESS) {
        RegQueryValueExA(key, "AppsUseLightTheme", NULL, NULL,
                         (BYTE *)&light, &size);
        RegCloseKey(key);
    }
    return light == 0;
}

/* The tidy view: on unless it was turned off. The long arrow is an em dash
 * where the code box can show one (the usual Windows code page), else => */
static BOOL g_tidy = TRUE;
static BOOL g_tidying;                /* our own change to the text, not typing */
static const char *tidy_arrow(void) { return GetACP() == 1252 ? "\x97>" : "=>"; }

static void load_tidy(void)
{
    HKEY key;
    DWORD v = 1, size = sizeof(v);
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Adda", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        RegQueryValueExA(key, "TidyView", NULL, NULL, (BYTE *)&v, &size);
        RegCloseKey(key);
    }
    g_tidy = v != 0;
}

static void save_tidy(void)
{
    HKEY key;
    DWORD v = g_tidy ? 1 : 0;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Adda", 0, NULL, 0,
                        KEY_WRITE, NULL, &key, NULL) == ERROR_SUCCESS) {
        RegSetValueExA(key, "TidyView", 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
        RegCloseKey(key);
    }
}

static void load_pick(void)
{
    HKEY key;
    DWORD v = PICK_SYSTEM, size = sizeof(v);

    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Adda", 0, KEY_READ, &key)
        == ERROR_SUCCESS) {
        RegQueryValueExA(key, "Theme", NULL, NULL, (BYTE *)&v, &size);
        RegCloseKey(key);
    }
    if (v >= (DWORD)PICK_COUNT) v = PICK_SYSTEM;
    g_pick = (int)v;
}

static void save_pick(void)
{
    HKEY key;
    DWORD v = (DWORD)g_pick;

    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Adda", 0, NULL, 0,
                        KEY_WRITE, NULL, &key, NULL) == ERROR_SUCCESS) {
        RegSetValueExA(key, "Theme", 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
        RegCloseKey(key);
    }
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

/* Dark title bar. Attribute 20 on Windows 10 2004 and later, 19 before it. */
static void apply_titlebar(HWND hwnd)
{
    BOOL dark = g_t.dark;

    if (!hwnd) return;
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark))))
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    {
        /* DWMWA_WINDOW_CORNER_PREFERENCE = DWMWCP_ROUND. Windows 11 only;
         * older versions just say no and keep square corners. */
        DWORD corner = 2;
        DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));
    }
}

static void theme_edit(HWND h)
{
    if (h) SetWindowTheme(h, g_t.dark ? L"DarkMode_Explorer" : L"Explorer", NULL);
}

static void apply_theme(HWND hwnd)
{
    g_t = theme_for(g_pick);

    if (hBrushBg)      DeleteObject(hBrushBg);
    if (hBrushSurface) DeleteObject(hBrushSurface);
    if (hBrushAb)      DeleteObject(hBrushAb);
    hBrushBg      = CreateSolidBrush(g_t.bg);
    hBrushSurface = CreateSolidBrush(g_t.surface);
    hBrushAb      = CreateSolidBrush(g_t.abBg);

    apply_titlebar(hwnd);
    apply_titlebar(hwndSettings);
    apply_titlebar(hwndCheats);

    theme_edit(hwndCode);
    theme_edit(hwndConsole);
    theme_edit(hwndFind);
    theme_edit(hwndFiles);
    theme_edit(hwndCheatFind);
    theme_edit(hwndCheatList);

    RedrawWindow(hwnd, NULL, NULL,
                 RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_ERASE);
    if (hwndSettings)
        RedrawWindow(hwndSettings, NULL, NULL,
                     RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_ERASE);
    if (hwndCheats)
        RedrawWindow(hwndCheats, NULL, NULL,
                     RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_ERASE);
}

/* ════════════════════════════════════════════════════════ fonts ══ */

static HFONT make_font(int points, int weight, const char *face)
{
    return CreateFontA(-MulDiv(points, g_dpi, 72), 0, 0, 0, weight,
                       FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

static void build_fonts(void)
{
    if (hFontMono)   DeleteObject(hFontMono);
    if (hFontUI)     DeleteObject(hFontUI);
    if (hFontUIBold) DeleteObject(hFontUIBold);
    if (hFontSmall)  DeleteObject(hFontSmall);
    if (hFontTitle)  DeleteObject(hFontTitle);
    if (hFontBody)   DeleteObject(hFontBody);

    hFontTitle  = make_font(14, FW_SEMIBOLD, "Segoe UI");
    hFontBody   = make_font(10, FW_NORMAL,   "Segoe UI");
    hFontMono   = make_font(11, FW_NORMAL,   "Consolas");
    hFontUI     = make_font(9,  FW_NORMAL,   "Segoe UI");
    hFontUIBold = make_font(9,  FW_SEMIBOLD, "Segoe UI");
    hFontSmall  = make_font(8,  FW_NORMAL,   "Segoe UI");
}

/* ════════════════════════════════════════════════════════ icons ══ */

/* GDI will not antialias, so each icon is drawn at 4x into a memory bitmap and
 * scaled down with HALFTONE, which box-filters the edges smooth. */
#define OVER 4

static POINT NP(int nx, int ny, int side)   /* 0..100 -> device */
{
    POINT p;
    p.x = MulDiv(nx, side, 100);
    p.y = MulDiv(ny, side, 100);
    return p;
}

static void gear_path(POINT *pts, int side, int cx, int cy)
{
    /* 8 teeth: each contributes 4 points, stepping inner-outer-outer-inner */
    const double outer = 0.46, inner = 0.33;
    int i, n = 0;

    for (i = 0; i < 8; i++) {
        double base = i * (2.0 * 3.14159265358979 / 8.0);
        double step = (2.0 * 3.14159265358979 / 8.0);
        double a[4];
        double r[4];
        int k;

        a[0] = base - step * 0.30; r[0] = inner;
        a[1] = base - step * 0.16; r[1] = outer;
        a[2] = base + step * 0.16; r[2] = outer;
        a[3] = base + step * 0.30; r[3] = inner;

        for (k = 0; k < 4; k++) {
            pts[n].x = cx + (int)(cos(a[k]) * r[k] * side);
            pts[n].y = cy + (int)(sin(a[k]) * r[k] * side);
            n++;
        }
    }
}

static void icon_shape(HDC dc, int kind, int side, COLORREF fg, COLORREF bg)
{
    int stroke = side / 13;
    HPEN pen, oldPen;
    HBRUSH bgBrush, oldBrush;

    if (stroke < 2) stroke = 2;

    pen = CreatePen(PS_SOLID, stroke, fg);
    bgBrush = CreateSolidBrush(bg);
    oldPen = (HPEN)SelectObject(dc, pen);
    oldBrush = (HBRUSH)SelectObject(dc, bgBrush);

    switch (kind) {

    case ICON_EXPLORER: {
        /* two pages, the front one overlapping the back, each with a folded
         * corner - recognisably "files" without copying anyone's icon */
        POINT back[5], front[5], fold[3];

        back[0] = NP(38, 10, side); back[1] = NP(70, 10, side);
        back[2] = NP(84, 24, side); back[3] = NP(84, 64, side);
        back[4] = NP(38, 64, side);
        Polygon(dc, back, 5);

        front[0] = NP(16, 32, side); front[1] = NP(50, 32, side);
        front[2] = NP(64, 46, side); front[3] = NP(64, 90, side);
        front[4] = NP(16, 90, side);
        Polygon(dc, front, 5);

        fold[0] = NP(50, 32, side); fold[1] = NP(50, 46, side);
        fold[2] = NP(64, 46, side);
        Polyline(dc, fold, 3);
        break;
    }

    case ICON_SEARCH: {
        POINT a, b;
        HPEN thick, oldThick;

        Ellipse(dc, MulDiv(14, side, 100), MulDiv(14, side, 100),
                    MulDiv(66, side, 100), MulDiv(66, side, 100));

        thick = CreatePen(PS_SOLID, stroke + stroke / 2, fg);
        oldThick = (HPEN)SelectObject(dc, thick);
        a = NP(60, 60, side);
        b = NP(86, 86, side);
        MoveToEx(dc, a.x, a.y, NULL);
        LineTo(dc, b.x, b.y);
        SelectObject(dc, oldThick);
        DeleteObject(thick);
        break;
    }

    case ICON_PLAY: {
        /* a play triangle, nudged right of centre so it looks centred -
         * a triangle's visual weight sits towards its flat side */
        POINT tri[3];
        tri[0] = NP(30, 18, side);
        tri[1] = NP(82, 50, side);
        tri[2] = NP(30, 82, side);
        Polygon(dc, tri, 3);
        break;
    }

    case ICON_STOP: {
        int a = MulDiv(24, side, 100), b = MulDiv(76, side, 100);
        RoundRect(dc, a, a, b, b, side / 10, side / 10);
        break;
    }

    case ICON_CHEAT: {
        /* an open book: two leaves meeting at a spine */
        POINT left[4], right[4], spine[2];

        left[0] = NP(10, 24, side); left[1] = NP(46, 16, side);
        left[2] = NP(46, 80, side); left[3] = NP(10, 88, side);
        Polygon(dc, left, 4);

        right[0] = NP(90, 24, side); right[1] = NP(54, 16, side);
        right[2] = NP(54, 80, side); right[3] = NP(90, 88, side);
        Polygon(dc, right, 4);

        spine[0] = NP(50, 18, side); spine[1] = NP(50, 84, side);
        Polyline(dc, spine, 2);
        break;
    }

    case ICON_GEAR: {
        POINT teeth[32];
        int c = side / 2;
        int hole = side / 7;

        gear_path(teeth, side, c, c);
        Polygon(dc, teeth, 32);
        Ellipse(dc, c - hole, c - hole, c + hole, c + hole);
        break;
    }

    case ICON_SAVE: {
        /* a floppy disk: the body with a clipped corner, the shutter at the
         * top and the label panel at the bottom */
        POINT body[5], shut[4], label[4];
        body[0] = NP(18, 14, side); body[1] = NP(72, 14, side);
        body[2] = NP(86, 28, side); body[3] = NP(86, 86, side);
        body[4] = NP(18, 86, side);
        Polygon(dc, body, 5);
        shut[0] = NP(32, 14, side); shut[1] = NP(32, 36, side);
        shut[2] = NP(64, 36, side); shut[3] = NP(64, 14, side);
        Polyline(dc, shut, 4);
        label[0] = NP(30, 86, side); label[1] = NP(30, 60, side);
        label[2] = NP(74, 60, side); label[3] = NP(74, 86, side);
        Polyline(dc, label, 4);
        break;
    }

    case ICON_NEW: {
        /* a folder with a plus on it */
        POINT folder[6], plus[2];
        folder[0] = NP(10, 24, side); folder[1] = NP(38, 24, side);
        folder[2] = NP(46, 34, side); folder[3] = NP(90, 34, side);
        folder[4] = NP(90, 82, side); folder[5] = NP(10, 82, side);
        Polygon(dc, folder, 6);
        plus[0] = NP(50, 46, side); plus[1] = NP(50, 72, side);
        Polyline(dc, plus, 2);
        plus[0] = NP(37, 59, side); plus[1] = NP(63, 59, side);
        Polyline(dc, plus, 2);
        break;
    }

    case ICON_TIDY: {
        /* lines of code, neatly ruled: a short arrow leading each one */
        POINT seg[3];
        int row;
        for (row = 0; row < 3; row++) {
            int y = 26 + row * 24, len = row == 1 ? 76 : row == 2 ? 84 : 88;
            seg[0] = NP(12, y, side); seg[1] = NP(30, y, side);
            Polyline(dc, seg, 2);
            seg[0] = NP(24, y - 6, side); seg[1] = NP(30, y, side); seg[2] = NP(24, y + 6, side);
            Polyline(dc, seg, 3);
            seg[0] = NP(42, y, side); seg[1] = NP(len, y, side);
            Polyline(dc, seg, 2);
        }
        break;
    }

    case ICON_CHECK: {
        /* a warning triangle with a ! in it */
        POINT tri[3], bang[2];
        int d = side / 11;
        POINT c = NP(50, 73, side);
        HBRUSH dot, oldDot;
        tri[0] = NP(50, 10, side); tri[1] = NP(92, 86, side);
        tri[2] = NP(8, 86, side);
        Polygon(dc, tri, 3);
        bang[0] = NP(50, 36, side); bang[1] = NP(50, 60, side);
        Polyline(dc, bang, 2);
        dot = CreateSolidBrush(fg);
        oldDot = (HBRUSH)SelectObject(dc, dot);
        Ellipse(dc, c.x - d / 2, c.y - d / 2, c.x + d / 2 + 1, c.y + d / 2 + 1);
        SelectObject(dc, oldDot);
        DeleteObject(dot);
        break;
    }

    case ICON_IMPORT: {
        /* an arrow coming down into a tray */
        POINT tray[4], shaft[2], head[3];
        tray[0] = NP(14, 58, side); tray[1] = NP(14, 86, side);
        tray[2] = NP(86, 86, side); tray[3] = NP(86, 58, side);
        Polyline(dc, tray, 4);
        shaft[0] = NP(50, 12, side); shaft[1] = NP(50, 64, side);
        Polyline(dc, shaft, 2);
        head[0] = NP(32, 46, side); head[1] = NP(50, 64, side);
        head[2] = NP(68, 46, side);
        Polyline(dc, head, 3);
        break;
    }

    default: break;
    }

    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(bgBrush);
}

static void draw_icon(HDC dst, RECT box, int kind, COLORREF fg, COLORREF bg)
{
    int w = box.right - box.left;
    int h = box.bottom - box.top;
    int side = (w < h ? w : h);
    int big = side * OVER;
    HDC mem;
    HBITMAP bm, oldBm;
    RECT full;
    HBRUSH fill;

    if (side <= 0) return;

    mem = CreateCompatibleDC(dst);
    bm = CreateCompatibleBitmap(dst, big, big);
    oldBm = (HBITMAP)SelectObject(mem, bm);

    full.left = 0; full.top = 0; full.right = big; full.bottom = big;
    fill = CreateSolidBrush(bg);
    FillRect(mem, &full, fill);
    DeleteObject(fill);

    icon_shape(mem, kind, big, fg, bg);

    SetStretchBltMode(dst, HALFTONE);
    SetBrushOrgEx(dst, 0, 0, NULL);
    StretchBlt(dst,
               box.left + (w - side) / 2, box.top + (h - side) / 2, side, side,
               mem, 0, 0, big, big, SRCCOPY);

    SelectObject(mem, oldBm);
    DeleteObject(bm);
    DeleteDC(mem);
}

/* ═══════════════════════════════════════════════ small helpers ══ */

static void fill_rect(HDC hdc, RECT r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    FillRect(hdc, &r, b);
    DeleteObject(b);
}

/* Rounded versions. GDI's RoundRect takes the full ellipse size, so the
 * corner radius is half of what gets passed. */
#define RADIUS      S(8)
#define GUTTER_W    S(44)       /* line-number strip in the code box */
#define RADIUS_BIG  S(12)

static void round_fill(HDC hdc, RECT r, COLORREF c, int radius)
{
    HBRUSH b = CreateSolidBrush(c);
    HPEN p = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ ob = SelectObject(hdc, b), op = SelectObject(hdc, p);
    RoundRect(hdc, r.left, r.top, r.right, r.bottom, radius * 2, radius * 2);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(b); DeleteObject(p);
}

static void round_frame(HDC hdc, RECT r, COLORREF c, int radius)
{
    HPEN p = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    HGDIOBJ op = SelectObject(hdc, p);
    RoundRect(hdc, r.left, r.top, r.right, r.bottom, radius * 2, radius * 2);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(p);
}

/* Clip a child control to a rounded shape so its square corners don't poke
 * out past the rounded outline paint_main draws around it. */
static void round_child(HWND h, int radius)
{
    RECT r;
    if (!h) return;
    GetClientRect(h, &r);
    SetWindowRgn(h, CreateRoundRectRgn(0, 0, r.right + 1, r.bottom + 1,
                                       radius * 2, radius * 2), TRUE);
}

static COLORREF blend(COLORREF a, COLORREF b, double t)
{
    return RGB((int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t),
               (int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t),
               (int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t));
}

/* moves one glow toward its target; TRUE while it still has somewhere to go */
static BOOL approach(double *g, BOOL on, double step)
{
    double to = on ? 1 : 0;
    if (*g < to) { *g += step; if (*g > to) *g = to; }
    else if (*g > to) { *g -= step; if (*g < to) *g = to; }
    return *g != to;
}

/* 1 at the moment of the click, easing to 0 as it finishes */
static double press_amount(DWORD at)
{
    double t = (double)(GetTickCount() - at) / PRESS_MS;
    if (t >= 1) return 0;
    t = 1 - t;
    return t * t;
}

static void text_at(HDC hdc, RECT r, const char *s, HFONT font,
                    COLORREF colour, UINT flags)
{
    HGDIOBJ old = SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, colour);
    DrawTextA(hdc, s, -1, &r, flags);
    SelectObject(hdc, old);
}

static BOOL contains(RECT r, POINT p)
{
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

/* case-insensitive substring */
static BOOL has_text(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    const char *p;

    if (!n) return TRUE;
    if (!hay) return FALSE;              /* a link has no snippet */
    for (p = hay; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == n) return TRUE;
    }
    return FALSE;
}

/* ═══════════════════════════════════════════════════ the child ══ */

static void get_adda_path(char *buf, int size)
{
    char *sep;

    GetModuleFileNameA(NULL, buf, size);
    sep = strrchr(buf, '\\');
    if (sep) *(sep + 1) = '\0';
    else     buf[0] = '\0';
    strncat(buf, "adda.exe", size - (int)strlen(buf) - 1);
}

/* The open project's folder, with a trailing backslash, or "" when there is
 * none. The Explorer shows this folder and nothing else, and which one it was
 * is remembered between launches. */
static char g_projectDir[MAX_PATH + 2];

/* where the Explorer looks, and where Import and Save As start: the project,
 * or "" when none is open */
static void explorer_dir(char *buf, int size)
{
    snprintf(buf, (size_t)size, "%s", g_projectDir);
}

static void remember_project(void)
{
    HKEY key;

    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Adda", 0, NULL, 0,
                        KEY_WRITE, NULL, &key, NULL) == ERROR_SUCCESS) {
        RegSetValueExA(key, "Project", 0, REG_SZ, (const BYTE *)g_projectDir,
                       (DWORD)strlen(g_projectDir) + 1);
        RegCloseKey(key);
    }
}

/* the project that was open last time, if it is still there; else "" */
static void recall_project(char *buf, DWORD size)
{
    HKEY key;
    DWORD type = 0, got = size - 1, attrs;

    buf[0] = '\0';
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Adda", 0, KEY_READ, &key) != ERROR_SUCCESS)
        return;
    if (RegQueryValueExA(key, "Project", NULL, &type, (BYTE *)buf, &got) != ERROR_SUCCESS ||
        type != REG_SZ)
        got = 0;
    buf[got < size ? got : size - 1] = '\0';
    RegCloseKey(key);

    attrs = buf[0] ? GetFileAttributesA(buf) : INVALID_FILE_ATTRIBUTES;
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) buf[0] = '\0';
}

/* ── console text ────────────────────────────────────────────────── */

static int console_len(void)
{
    return GetWindowTextLengthA(hwndConsole);
}

static void console_append(const char *text)
{
    int len = console_len();
    SendMessageA(hwndConsole, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageA(hwndConsole, EM_REPLACESEL, FALSE, (LPARAM)text);
    SendMessageA(hwndConsole, EM_SCROLLCARET, 0, 0);
}

/* Reads back whatever the user has half-typed after the anchor. Caller frees. */
static char *console_pending(void)
{
    int len = console_len();
    char *all, *copy;
    int n;

    if (len <= g_anchor) return NULL;

    all = (char *)malloc((size_t)len + 1);
    if (!all) return NULL;
    GetWindowTextA(hwndConsole, all, len + 1);

    n = len - g_anchor;
    copy = (char *)malloc((size_t)n + 1);
    if (copy) {
        memcpy(copy, all + g_anchor, (size_t)n);
        copy[n] = '\0';
    }
    free(all);
    return copy;
}

/* An EDIT control needs \r\n. The previous byte is remembered across calls so
 * a \r\n split between two reads does not gain a second \r. */
static char g_prev_byte = '\0';

/* Output arriving while a line is half-typed must not eat it, so the pending
 * text is lifted out, the output goes in underneath, and the typing is put
 * back with the caret where it was. */
static void console_output(const char *raw, int n)
{
    char out[8192];
    char *pending;
    int i, j = 0, caret = 0, tail = 0, len;
    DWORD selStart = 0, selEnd = 0;

    for (i = 0; i < n && j < (int)sizeof(out) - 2; i++) {
        if (raw[i] == '\n' && g_prev_byte != '\r') out[j++] = '\r';
        out[j++] = raw[i];
        g_prev_byte = raw[i];
    }
    out[j] = '\0';
    if (!j) return;

    pending = console_pending();
    if (pending) {
        SendMessageA(hwndConsole, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
        caret = (int)selStart - g_anchor;
        tail  = (int)selEnd   - g_anchor;
        if (caret < 0) caret = 0;
        if (tail < caret) tail = caret;

        len = console_len();
        SendMessageA(hwndConsole, EM_SETSEL, (WPARAM)g_anchor, (LPARAM)len);
        SendMessageA(hwndConsole, EM_REPLACESEL, FALSE, (LPARAM)"");
    }

    console_append(out);
    g_anchor = console_len();

    if (pending) {
        console_append(pending);
        /* restore the whole selection, not just a caret: collapsing it would
         * turn a pending overtype into an insert */
        SendMessageA(hwndConsole, EM_SETSEL,
                     (WPARAM)(g_anchor + caret), (LPARAM)(g_anchor + tail));
        SendMessageA(hwndConsole, EM_SCROLLCARET, 0, 0);
        free(pending);
    }
}

static void set_running(BOOL running)
{
    g_running = running;
    /* Run and Stop live in the activity bar and dim by state */
    InvalidateRect(hwndMain, NULL, FALSE);
}

static void close_handle(HANDLE *h)
{
    if (*h) { CloseHandle(*h); *h = NULL; }
}

static void drain(HANDLE h)
{
    if (!h) return;

    for (;;) {
        DWORD avail = 0, got = 0, want;
        char buf[4096];

        if (!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL)) return;
        if (avail == 0) return;

        want = avail < sizeof(buf) ? avail : sizeof(buf);
        if (!ReadFile(h, buf, want, &got, NULL) || got == 0) return;
        console_output(buf, (int)got);
    }
}

static void finish_run(HWND hwnd, const char *note)
{
    KillTimer(hwnd, ID_POLL);

    drain(g_out);
    drain(g_err);

    close_handle(&g_in);
    close_handle(&g_out);
    close_handle(&g_err);
    inq_clear();

    if (g_pi.hProcess) { CloseHandle(g_pi.hProcess); g_pi.hProcess = NULL; }
    if (g_pi.hThread)  { CloseHandle(g_pi.hThread);  g_pi.hThread  = NULL; }

    if (g_tmp_file[0]) { remove(g_tmp_file); g_tmp_file[0] = '\0'; }

    set_running(FALSE);

    /* Anything typed but never sent is not history. Left in place, the note
     * would land after it and the transcript would read exactly like a line
     * that had been submitted, when it never was. */
    if (console_len() > g_anchor) {
        SendMessageA(hwndConsole, EM_SETSEL,
                     (WPARAM)g_anchor, (LPARAM)console_len());
        SendMessageA(hwndConsole, EM_REPLACESEL, FALSE, (LPARAM)"");
    }

    if (note) console_append(note);
    g_anchor = console_len();
    SetFocus(hwndCode);

    /* Run All Files: on to the next one, once this one has fully wound down */
    if (g_runAll) PostMessageA(hwnd, WM_RUN_NEXT, 0, 0);
}

/* Runs `code` (len bytes) as the program called `name` - the name an error
 * will quote. `fresh` clears the console first; Run All Files keeps what came
 * before. */
static BOOL start_run(HWND hwnd, const char *code, size_t len, const char *name, BOOL fresh)
{
    char tmp_dir[MAX_PATH];
    char adda_exe[MAX_PATH];
    char cmd[MAX_PATH * 4];
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOA si;
    HANDLE hWriteOut = NULL, hWriteErr = NULL, hReadIn = NULL;
    FILE *f;
    BOOL ok;

    if (g_running) return FALSE;

    GetTempPathA(MAX_PATH, tmp_dir);
    snprintf(g_tmp_file, sizeof(g_tmp_file), "%s%s", tmp_dir, name);

    f = fopen(g_tmp_file, "wb");
    if (!f) { g_tmp_file[0] = '\0'; return FALSE; }
    fwrite(code, 1, len, f);
    fclose(f);

    get_adda_path(adda_exe, MAX_PATH);
    snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\"", adda_exe, g_tmp_file);

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    CreatePipe(&g_out, &hWriteOut, &sa, 0);
    SetHandleInformation(g_out, HANDLE_FLAG_INHERIT, 0);
    CreatePipe(&g_err, &hWriteErr, &sa, 0);
    SetHandleInformation(g_err, HANDLE_FLAG_INHERIT, 0);
    CreatePipe(&hReadIn, &g_in, &sa, 1 << 16);
    SetHandleInformation(g_in, HANDLE_FLAG_INHERIT, 0);
    {   /* so a full pipe returns instead of parking the UI thread */
        DWORD mode = PIPE_NOWAIT;
        SetNamedPipeHandleState(g_in, &mode, NULL, NULL);
    }

    ZeroMemory(&si, sizeof(si));
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = hReadIn;
    si.hStdOutput = hWriteOut;
    si.hStdError  = hWriteErr;

    ZeroMemory(&g_pi, sizeof(g_pi));

    ok = CreateProcessA(NULL, cmd, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &g_pi);

    CloseHandle(hWriteOut);
    CloseHandle(hWriteErr);
    CloseHandle(hReadIn);

    if (!ok) {
        close_handle(&g_in);
        close_handle(&g_out);
        close_handle(&g_err);
        SetWindowTextA(hwndConsole,
            "Could not run adda.exe\r\n"
            "Make sure adda.exe is in the same folder as adda-gui.exe.");
        g_anchor = console_len();
        remove(g_tmp_file);
        g_tmp_file[0] = '\0';
        return FALSE;
    }

    if (fresh) SetWindowTextA(hwndConsole, "");
    g_prev_byte = '\0';
    g_anchor = console_len();
    set_running(TRUE);
    SetFocus(hwndConsole);

    /* Poll rather than wait: waiting for the process first deadlocks as soon
     * as it writes more than the pipe will hold, because nothing drains it. */
    SetTimer(hwnd, ID_POLL, 50, NULL);
    return TRUE;
}

static void run_code(HWND hwnd)
{
    char *code;
    const char *name;
    int len;

    if (g_running) return;
    g_runAll = FALSE;
    save_current();                 /* a run is a good moment to keep your work */

    code = code_text(&len);
    if (!code) return;

    /* the file's own name, so an error says style.adda:3 */
    name = strrchr(g_curPath, '\\');
    name = name ? name + 1 : "program.adda";
    start_run(hwnd, code, (size_t)len, name, TRUE);
    free(code);
}

/* first.adda leads; the rest in name order */
static int by_run_order(const void *a, const void *b)
{
    const char *x = strrchr((const char *)a, '\\'), *y = strrchr((const char *)b, '\\');
    int xf, yf;
    x = x ? x + 1 : (const char *)a;
    y = y ? y + 1 : (const char *)b;
    xf = lstrcmpiA(x, "first.adda") == 0;
    yf = lstrcmpiA(y, "first.adda") == 0;
    if (xf != yf) return yf - xf;
    return lstrcmpiA(x, y);
}

/* Every program in `dir` (with a trailing backslash): its own files first,
 * then each folder's. */
static void collect_programs(const char *dir)
{
    char pattern[MAX_PATH * 2], sub[MAX_PATH * 2];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int from = g_runCount;

    snprintf(pattern, sizeof pattern, "%s*.adda", dir);
    h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && g_runCount < MAX_RUN)
                snprintf(g_runPaths[g_runCount++], sizeof g_runPaths[0], "%s%s", dir, fd.cFileName);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    qsort(g_runPaths[from], (size_t)(g_runCount - from), sizeof g_runPaths[0], by_run_order);

    snprintf(pattern, sizeof pattern, "%s*", dir);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != '.') {
            snprintf(sub, sizeof sub, "%s%s\\", dir, fd.cFileName);
            collect_programs(sub);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

/* The next program in the queue, under a heading that says which it is. A
 * file that cannot be read is reported and skipped. */
static void run_next(HWND hwnd)
{
    size_t base = strlen(g_projectDir);

    while (g_runAll && g_runNext < g_runCount && !g_running) {
        const char *path = g_runPaths[g_runNext++];
        const char *leaf = strrchr(path, '\\');
        char heading[MAX_PATH * 3];
        FILE *f;
        long size;
        char *code;
        BOOL started;

        leaf = leaf ? leaf + 1 : path;
        snprintf(heading, sizeof heading, "-- %s --\r\n",
                 strncmp(path, g_projectDir, base) == 0 ? path + base : leaf);
        console_append(heading);
        g_anchor = console_len();

        f = fopen(path, "rb");
        if (!f) { console_append("[could not read this file]\r\n\r\n"); continue; }
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fseek(f, 0, SEEK_SET);
        code = (char *)malloc((size_t)(size > 0 ? size : 0) + 1);
        if (!code) { fclose(f); continue; }
        size = (long)fread(code, 1, (size_t)(size > 0 ? size : 0), f);
        fclose(f);
        started = start_run(hwnd, code, (size_t)size, leaf, FALSE);
        free(code);
        if (started) return;
        g_runAll = FALSE;           /* adda.exe itself is missing: no point going on */
        return;
    }
    if (g_runAll && !g_running) {
        char done[64];
        snprintf(done, sizeof done, "[finished - ran %d file%s]\r\n",
                 g_runCount, g_runCount == 1 ? "" : "s");
        console_append(done);
        g_anchor = console_len();
        g_runAll = FALSE;
    }
}

/* Runs every program in the project, one after another, each as a program of
 * its own - nothing one file sets is seen by the next. */
static void run_all_files(HWND hwnd)
{
    if (g_running) return;
    if (!g_projectDir[0]) {
        MessageBoxA(hwnd, "Open a project first.\n"
                          "New Project makes one, and Import Folder can open one you already have.",
                    "Run All Files", MB_OK | MB_ICONINFORMATION);
        return;
    }
    save_current();                 /* what you just typed is part of "all" */
    g_runCount = 0;
    g_runNext = 0;
    collect_programs(g_projectDir);
    if (!g_runCount) {
        MessageBoxA(hwnd, "There are no .adda files in this project to run.",
                    "Run All Files", MB_OK | MB_ICONINFORMATION);
        return;
    }
    SetWindowTextA(hwndConsole, "");
    g_anchor = 0;
    g_runAll = TRUE;
    run_next(hwnd);
}

/* right-click on Run: this file, or every file */
static void run_menu(HWND hwnd, int sx, int sy)
{
    HMENU m = CreatePopupMenu();
    UINT idle = g_running ? MF_GRAYED : MF_ENABLED;
    int pick;

    AppendMenuA(m, MF_STRING | idle, IDM_RUN, "Run This File\tCtrl+Enter");
    AppendMenuA(m, MF_STRING | (g_projectDir[0] ? idle : MF_GRAYED), IDM_RUNALL,
                "Run All Files\tCtrl+Shift+Enter");
    pick = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                               sx, sy, 0, hwnd, NULL);
    DestroyMenu(m);
    if (pick == IDM_RUN) run_code(hwnd);
    if (pick == IDM_RUNALL) run_all_files(hwnd);
}

/* Sends everything typed after the anchor down the pipe. */
/* Typed input is queued rather than written straight out.
 *
 * The obvious WriteFile-and-flush blocks the UI thread whenever the child is
 * not reading - and FlushFileBuffers on a pipe does not return until the
 * reader has drained it, so a program that never calls `ask` would freeze the
 * window outright, Stop button included. The pipe is therefore non-blocking
 * and anything it will not take waits here for the next timer tick. */
static char  *g_inq;
static size_t g_inq_len, g_inq_cap;

static void inq_push(const char *data, size_t n)
{
    if (!n) return;
    if (g_inq_len + n > g_inq_cap) {
        size_t cap = g_inq_cap ? g_inq_cap : 256;
        char *grown;
        while (cap < g_inq_len + n) cap *= 2;
        grown = (char *)realloc(g_inq, cap);
        if (!grown) return;              /* drop it rather than die */
        g_inq = grown;
        g_inq_cap = cap;
    }
    memcpy(g_inq + g_inq_len, data, n);
    g_inq_len += n;
}

static void inq_clear(void)
{
    free(g_inq);
    g_inq = NULL;
    g_inq_len = g_inq_cap = 0;
}

static void pump_stdin(void)
{
    DWORD written = 0;

    if (!g_in || !g_inq_len) return;

    if (!WriteFile(g_in, g_inq, (DWORD)g_inq_len, &written, NULL)) {
        g_inq_len = 0;                   /* the child is gone */
        return;
    }
    if (written >= g_inq_len) { g_inq_len = 0; return; }

    memmove(g_inq, g_inq + written, g_inq_len - written);
    g_inq_len -= written;
}

static void send_line(void)
{
    char *line;

    if (!g_running || !g_in) return;     /* before the alloc, or it leaks */
    line = console_pending();

    console_append("\r\n");
    g_anchor = console_len();

    if (line) inq_push(line, strlen(line));
    inq_push("\n", 1);
    pump_stdin();

    free(line);
}

/* ═══════════════════════════════════════════ console subclass ══ */

/* Pulls the selection forward so nothing before the anchor can be edited. */
static BOOL clamp_selection(void)
{
    DWORD a = 0, b = 0;

    SendMessageA(hwndConsole, EM_GETSEL, (WPARAM)&a, (LPARAM)&b);
    if ((int)a < g_anchor) {
        int end = console_len();
        SendMessageA(hwndConsole, EM_SETSEL, (WPARAM)end, (LPARAM)end);
        return TRUE;
    }
    return FALSE;
}

static LRESULT CALLBACK console_proc(HWND h, UINT msg, WPARAM w, LPARAM l,
                                     UINT_PTR id, DWORD_PTR ref)
{
    (void)ref;

    switch (msg) {

    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS;

    case WM_CHAR:
        if (!g_running) return 0;                  /* nothing is listening */
        if (w == '\r') { send_line(); return 0; }
        if (w == '\n') return 0;                   /* Ctrl+Enter runs instead */
        if (w == '\t') return 0;
        if (w == 0x7F) return 0;    /* Ctrl+Backspace, or a plain EDIT draws a box */
        if (w == '\b') {
            DWORD a = 0, b = 0;
            SendMessageA(h, EM_GETSEL, (WPARAM)&a, (LPARAM)&b);
            /* EM_GETSEL normalises to a <= b, so testing the END covers both a
             * bare caret at the anchor and a selection lying wholly in the
             * history above it - either way there is nothing of ours to eat. */
            if ((int)b <= g_anchor) return 0;
            if ((int)a < g_anchor)                 /* straddles: keep our part */
                SendMessageA(h, EM_SETSEL, (WPARAM)g_anchor, (LPARAM)b);
            break;
        }
        clamp_selection();
        break;

    case WM_KEYDOWN:
        /* Undo could resurrect output we trimmed, or unwind an append, so the
         * console simply has no undo. */
        if (w == 'Z' && (GetKeyState(VK_CONTROL) & 0x8000)) return 0;
        if (!g_running) {
            /* let people scroll, select and copy, but not move text about */
            if (w == VK_DELETE || w == VK_BACK) return 0;
            break;
        }
        if (w == VK_DELETE) {
            DWORD a = 0, b = 0;
            SendMessageA(h, EM_GETSEL, (WPARAM)&a, (LPARAM)&b);
            if ((int)a < g_anchor) {
                if ((int)b <= g_anchor) return 0;          /* all history */
                /* straddles: delete only our part, same as backspace does */
                SendMessageA(h, EM_SETSEL, (WPARAM)g_anchor, (LPARAM)b);
            }
        }
        if (w == VK_ESCAPE) {              /* wipe the half-typed line */
            int end = console_len();
            if (end > g_anchor) {
                SendMessageA(h, EM_SETSEL, (WPARAM)g_anchor, (LPARAM)end);
                SendMessageA(h, EM_REPLACESEL, FALSE, (LPARAM)"");
            }
            return 0;
        }
        break;

    case WM_UNDO:
    case EM_UNDO:
        return 0;

    case WM_PASTE:
        if (!g_running) return 0;
        clamp_selection();
        break;

    case WM_CUT:
    case WM_CLEAR: {
        DWORD a = 0, b = 0;
        if (!g_running) return 0;
        SendMessageA(h, EM_GETSEL, (WPARAM)&a, (LPARAM)&b);
        if ((int)a < g_anchor) {
            if ((int)b <= g_anchor) return 0;              /* all history */
            SendMessageA(h, EM_SETSEL, (WPARAM)g_anchor, (LPARAM)b);
        }
        break;
    }

    case WM_NCDESTROY:
        RemoveWindowSubclass(h, console_proc, id);
        break;
    }

    return DefSubclassProc(h, msg, w, l);
}

/* ═══════════════════════════════════════════════ the explorer ══ */

static void add_file(const char *dir, const char *name)
{
    if (g_fileCount >= MAX_FILES) return;
    snprintf(g_files[g_fileCount].name, sizeof(g_files[0].name), "%s", name);
    snprintf(g_files[g_fileCount].path, sizeof(g_files[0].path), "%s%s", dir, name);
    g_fileCount++;
}

static void scan_dir(const char *dir)
{
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;

    snprintf(pattern, sizeof(pattern), "%s*.adda", dir);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            add_file(dir, fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

/* The programs in the open project, and nothing else; none when no project
 * is open. */
static void rescan_files(void)
{
    int i;

    g_fileCount = 0;
    if (g_projectDir[0]) scan_dir(g_projectDir);

    if (!hwndFiles) return;
    SendMessageA(hwndFiles, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < g_fileCount; i++)
        SendMessageA(hwndFiles, LB_ADDSTRING, 0, (LPARAM)g_files[i].name);
}

/* Each file is its own document: the editor holds whichever one is open, and
 * what is typed goes back into that file - when another is opened, before a
 * run, and on closing - so typing in one never turns up in another. */
static BOOL g_dirty;                  /* typed in since it was loaded or saved */
static BOOL g_loading;                /* the EN_CHANGE is ours, not typing */

/* The code box's text as it really is - the tidy view turned back to raw,
 * since it is only a way of showing the code - with the EDIT control's \r\n
 * put back to \n. Anything saved or run goes through here. Caller frees. */
static char *code_text(int *outLen)
{
    int len = GetWindowTextLengthA(hwndCode), i, j;
    char *shown = malloc((size_t)len + 1), *text;
    if (!shown) return NULL;
    GetWindowTextA(hwndCode, shown, len + 1);
    text = tidy_raw(shown, tidy_arrow());
    free(shown);
    if (!text) return NULL;
    len = (int)strlen(text);
    for (i = j = 0; i < len; i++)
        if (!(text[i] == '\r' && text[i + 1] == '\n')) text[j++] = text[i];
    text[j] = '\0';
    *outLen = j;
    return text;
}

/* the project folder's own name, without the path or the trailing backslash */
static void project_name(char *buf, size_t size)
{
    char dir[MAX_PATH + 2];
    size_t n;
    const char *leaf;

    snprintf(dir, sizeof dir, "%s", g_projectDir);
    n = strlen(dir);
    while (n > 0 && dir[n - 1] == '\\') dir[--n] = '\0';
    leaf = strrchr(dir, '\\');
    snprintf(buf, size, "%s", leaf ? leaf + 1 : dir);
}

static void show_current(void)
{
    char title[MAX_PATH * 5], project[MAX_PATH + 2];
    const char *name = strrchr(g_curPath, '\\');

    name = name ? name + 1 : g_curPath;
    project_name(project, sizeof project);
    if (g_curPath[0] && project[0]) snprintf(title, sizeof title, "%s - %s", name, project);
    else if (g_curPath[0])          snprintf(title, sizeof title, "%s - Adda", name);
    else if (project[0])            snprintf(title, sizeof title, "%s - Adda", project);
    else                            snprintf(title, sizeof title, "Adda");
    SetWindowTextA(hwndMain, title);
}

static void save_current(void)
{
    int len;
    char *text;
    FILE *f;
    BOOL ok;

    if (!g_curPath[0] || !g_dirty) return;
    text = code_text(&len);
    if (!text) return;
    f = fopen(g_curPath, "wb");
    ok = f && fwrite(text, 1, (size_t)len, f) == (size_t)len;
    if (f && fclose(f) != 0) ok = FALSE;
    free(text);
    if (ok) g_dirty = FALSE;
    else MessageBoxA(hwndMain, "Your changes could not be saved.\n"
                               "The file may be read-only, or open in another program.",
                     "Save", MB_OK | MB_ICONWARNING);
}

static void open_file(int index)
{
    FILE *f;
    long size;
    char *buf;

    if (index < 0 || index >= g_fileCount) return;
    if (strcmp(g_files[index].path, g_curPath) == 0) return;   /* already open */
    save_current();

    f = fopen(g_files[index].path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);

    buf = (char *)malloc((size_t)size * 2 + 2);
    if (buf) {
        char *raw = (char *)malloc((size_t)size + 1);
        if (raw) {
            size_t got = fread(raw, 1, (size_t)size, f);
            size_t i, j = 0;
            raw[got] = '\0';
            for (i = 0; i < got; i++) {          /* \n -> \r\n for the EDIT */
                if (raw[i] == '\n' && (i == 0 || raw[i - 1] != '\r'))
                    buf[j++] = '\r';
                buf[j++] = raw[i];
            }
            buf[j] = '\0';
            g_loading = TRUE;
            SetWindowTextA(hwndCode, buf);
            g_loading = FALSE;
            snprintf(g_curPath, sizeof g_curPath, "%s", g_files[index].path);
            g_dirty = FALSE;
            show_current();
            PostMessageA(hwndMain, WM_TIDY, 0, 0);
            free(raw);
        }
        free(buf);
    }
    fclose(f);
}

/* ══════════════════════════════════════ renaming and deleting ══ */

static void select_by_name(const char *name)
{
    int i;
    for (i = 0; i < g_fileCount; i++)
        if (strcmp(g_files[i].name, name) == 0) {
            SendMessageA(hwndFiles, LB_SETCURSEL, (WPARAM)i, 0);
            return;
        }
}

/* Windows will not have these in a file name, and a path separator would let a
 * rename walk out of the folder. */
static BOOL name_is_sane(const char *s)
{
    const char *bad = "\\/:*?\"<>|";
    if (!*s) return FALSE;
    for (; *s; s++)
        if (strchr(bad, *s)) return FALSE;
    return TRUE;
}

static void end_rename(BOOL commit)
{
    int idx = g_renameIdx;
    char typed[64], folder[MAX_PATH], target[MAX_PATH + 96];
    const char *slash;

    if (idx < 0) return;
    g_renameIdx = -1;                  /* first, so hiding cannot re-enter */
    if (hwndRename) ShowWindow(hwndRename, SW_HIDE);

    if (!commit || idx >= g_fileCount) { SetFocus(hwndFiles); return; }

    GetWindowTextA(hwndRename, typed, sizeof(typed));
    if (!typed[0] || strcmp(typed, g_files[idx].name) == 0) {
        SetFocus(hwndFiles);
        return;
    }
    if (!name_is_sane(typed)) {
        MessageBoxA(hwndMain,
            "A file name cannot contain  \\ / : * ? \" < > |",
            "Rename", MB_OK | MB_ICONWARNING);
        SetFocus(hwndFiles);
        return;
    }

    /* keep it findable: the Explorer only lists .adda files */
    if (!strchr(typed, '.')) {
        size_t n = strlen(typed);
        if (n + 5 < sizeof(typed)) memcpy(typed + n, ".adda", 6);
    }

    snprintf(folder, sizeof(folder), "%s", g_files[idx].path);
    slash = strrchr(folder, '\\');
    if (slash) folder[slash - folder + 1] = '\0';
    else folder[0] = '\0';

    snprintf(target, sizeof(target), "%s%s", folder, typed);

    if (GetFileAttributesA(target) != INVALID_FILE_ATTRIBUTES) {
        MessageBoxA(hwndMain, "There is already a file with that name.",
                    "Rename", MB_OK | MB_ICONWARNING);
        SetFocus(hwndFiles);
        return;
    }

    if (!MoveFileA(g_files[idx].path, target)) {
        MessageBoxA(hwndMain,
            "Windows would not rename that file.\n"
            "It may be open in another program, or read-only.",
            "Rename", MB_OK | MB_ICONWARNING);
        SetFocus(hwndFiles);
        return;
    }
    if (strcmp(g_files[idx].path, g_curPath) == 0) {   /* the open file moves with it */
        snprintf(g_curPath, sizeof g_curPath, "%s", target);
        show_current();
    }

    rescan_files();
    select_by_name(typed);
    SetFocus(hwndFiles);
}

static LRESULT CALLBACK rename_proc(HWND h, UINT msg, WPARAM w, LPARAM l,
                                    UINT_PTR id, DWORD_PTR ref)
{
    (void)ref;

    switch (msg) {
    case WM_KEYDOWN:
        if (w == VK_RETURN) { end_rename(TRUE);  return 0; }
        if (w == VK_ESCAPE) { end_rename(FALSE); return 0; }
        break;
    case WM_CHAR:
        if (w == '\r' || w == '\n' || w == 0x1B) return 0;  /* no beep */
        break;
    case WM_KILLFOCUS:
        /* clicking away abandons the rename rather than silently doing it */
        end_rename(FALSE);
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(h, rename_proc, id);
        break;
    }
    return DefSubclassProc(h, msg, w, l);
}

static void begin_rename(void)
{
    int sel = (int)SendMessageA(hwndFiles, LB_GETCURSEL, 0, 0);
    RECT r;

    if (sel < 0 || sel >= g_fileCount) return;
    if (SendMessageA(hwndFiles, LB_GETITEMRECT, (WPARAM)sel, (LPARAM)&r) == LB_ERR)
        return;

    /* A child of the main window rather than of the list, so that
     * WM_CTLCOLOREDIT reaches our handler and the box follows the theme. */
    MapWindowPoints(hwndFiles, hwndMain, (POINT *)&r, 2);

    if (!hwndRename) {
        hwndRename = CreateWindowExA(0, "EDIT", "",
            WS_CHILD | ES_AUTOHSCROLL,
            0, 0, 0, 0, hwndMain, NULL, GetModuleHandleA(NULL), NULL);
        if (!hwndRename) return;
        SetWindowSubclass(hwndRename, rename_proc, 1, 0);
    }

    SendMessage(hwndRename, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    MoveWindow(hwndRename, r.left, r.top,
               r.right - r.left, r.bottom - r.top, TRUE);
    SetWindowTextA(hwndRename, g_files[sel].name);

    g_renameIdx = sel;
    SetWindowPos(hwndRename, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetFocus(hwndRename);
    SendMessageA(hwndRename, EM_SETSEL, 0, (LPARAM)-1);
}

static void delete_selected(void)
{
    int sel = (int)SendMessageA(hwndFiles, LB_GETCURSEL, 0, 0);
    char question[MAX_PATH + 128];
    char from[MAX_PATH + 2];
    SHFILEOPSTRUCTA op;

    if (sel < 0 || sel >= g_fileCount) return;

    snprintf(question, sizeof(question),
             "Delete %s?\n\nIt goes to the Recycle Bin, so you can get it back.",
             g_files[sel].name);
    if (MessageBoxA(hwndMain, question, "Delete",
                    MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2) != IDOK)
        return;

    /* the open file is going: it must not be written back afterwards */
    if (strcmp(g_files[sel].path, g_curPath) == 0) {
        g_curPath[0] = '\0';
        g_dirty = FALSE;
        show_current();
    }

    /* pFrom is a list, so it has to end with TWO NULs */
    memset(from, 0, sizeof(from));
    snprintf(from, MAX_PATH, "%s", g_files[sel].path);

    memset(&op, 0, sizeof(op));
    op.hwnd   = hwndMain;
    op.wFunc  = FO_DELETE;
    op.pFrom  = from;
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;

    if (SHFileOperationA(&op) != 0 || op.fAnyOperationsAborted) {
        MessageBoxA(hwndMain,
            "Windows would not delete that file.\n"
            "It may be open in another program, or read-only.",
            "Delete", MB_OK | MB_ICONWARNING);
    }

    rescan_files();
    if (g_fileCount) {
        int next = sel < g_fileCount ? sel : g_fileCount - 1;
        SendMessageA(hwndFiles, LB_SETCURSEL, (WPARAM)next, 0);
    }
    SetFocus(hwndFiles);
}

static void file_menu(HWND hwnd, int sx, int sy)
{
    HMENU menu;
    int chosen;

    if (sx == -1 && sy == -1) {        /* came from the keyboard */
        RECT r;
        int sel = (int)SendMessageA(hwndFiles, LB_GETCURSEL, 0, 0);
        if (sel < 0) return;
        SendMessageA(hwndFiles, LB_GETITEMRECT, (WPARAM)sel, (LPARAM)&r);
        MapWindowPoints(hwndFiles, NULL, (POINT *)&r, 2);
        sx = r.left + S(20);
        sy = r.bottom;
    } else {
        POINT p;
        DWORD hit;
        p.x = sx; p.y = sy;
        ScreenToClient(hwndFiles, &p);
        hit = (DWORD)SendMessageA(hwndFiles, LB_ITEMFROMPOINT, 0,
                                  MAKELPARAM(p.x, p.y));
        if (HIWORD(hit)) return;       /* clicked past the last row */
        SendMessageA(hwndFiles, LB_SETCURSEL, (WPARAM)LOWORD(hit), 0);
    }

    if ((int)SendMessageA(hwndFiles, LB_GETCURSEL, 0, 0) < 0) return;

    menu = CreatePopupMenu();
    AppendMenuA(menu, MF_STRING, IDM_RENAME, "Rename\tF2");
    AppendMenuA(menu, MF_STRING, IDM_DELETE, "Delete\tDel");

    chosen = (int)TrackPopupMenu(menu,
                 TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
                 sx, sy, 0, hwnd, NULL);
    DestroyMenu(menu);

    if (chosen == IDM_RENAME) begin_rename();
    else if (chosen == IDM_DELETE) delete_selected();
}

/* ═══════════════════════════════════════════════════ searching ══ */

static BOOL starts_with_ci(const char *s, const char *needle, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        if (!s[i]) return FALSE;
        if (tolower((unsigned char)s[i]) != tolower((unsigned char)needle[i]))
            return FALSE;
    }
    return TRUE;
}

static void find_in_code(BOOL forward)
{
    char needle[128];
    int len, start, i, n, span;
    char *hay;
    DWORD selStart = 0, selEnd = 0;

    GetWindowTextA(hwndFind, needle, sizeof(needle));
    n = (int)strlen(needle);
    if (!n) return;

    len = GetWindowTextLengthA(hwndCode);
    span = len - n + 1;
    if (span <= 0) return;          /* needle longer than the code: nothing to find,
                                     * and the wrap-around below would divide by zero */

    hay = (char *)malloc((size_t)len + 1);
    if (!hay) return;
    GetWindowTextA(hwndCode, hay, len + 1);

    SendMessageA(hwndCode, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    start = forward ? (int)selEnd : 0;
    if (start < 0) start = 0;

    for (i = 0; i < span; i++) {
        int at = (start + i) % span;      /* wrap round to the top */
        if (starts_with_ci(hay + at, needle, n)) {
            SendMessageA(hwndCode, EM_SETSEL, (WPARAM)at, (LPARAM)(at + n));
            SendMessageA(hwndCode, EM_SCROLLCARET, 0, 0);
            break;
        }
    }
    free(hay);
}

/* ═════════════════════════════════════════════════════ layout ══ */

/* Moves one child through the deferred chain, falling back to MoveWindow if
 * the chain could not be started or has already failed - passing a NULL HDWP
 * back into DeferWindowPos is undefined. Sizes are clamped to zero so a
 * squeezed window cannot ask for a negative one. */
static HDWP move_child(HDWP dwp, HWND h, int x, int y, int w, int ht, UINT extra)
{
    if (w < 0) w = 0;
    if (ht < 0) ht = 0;
    if (!dwp) {
        MoveWindow(h, x, y, w, ht, TRUE);
        if (extra & SWP_SHOWWINDOW) ShowWindow(h, SW_SHOW);
        if (extra & SWP_HIDEWINDOW) ShowWindow(h, SW_HIDE);
        return NULL;
    }
    return DeferWindowPos(dwp, h, NULL, x, y, w, ht, SWP_NOZORDER | extra);
}

static void layout(HWND hwnd)
{
    RECT rc;
    int abW, panelW, pad, toolH, splitH, x, y, w, h;
    int codeH, consoleH;
    int i, top, bottom;
    HDWP dwp;

    GetClientRect(hwnd, &rc);

    abW    = S(48);
    panelW = (g_view >= 0) ? S(210) : 0;
    pad    = S(12);
    toolH  = S(12);   /* just padding now Run and Stop live in the bar */
    splitH = S(7);

    /* activity bar slots */
    top = S(8);
    bottom = rc.bottom - S(8) - S(44);
    for (i = 0; i < AB_COUNT; i++) {
        RECT *r = &g_abRect[i];
        r->left = 0;
        r->right = abW;
        if (i < AB_TIDY) { r->top = top; top += S(44) + (i == AB_SEARCH ? S(6) : 0); }
        else              { r->top = bottom; bottom += S(44); }
        r->bottom = r->top + S(44);
    }
    /* the bottom items were laid out downwards; push them to the bottom */
    {
        int shift = rc.bottom - S(8) - g_abRect[AB_GEAR].bottom;
        for (i = AB_TIDY; i < AB_COUNT; i++) {
            g_abRect[i].top += shift;
            g_abRect[i].bottom += shift;
        }
    }

    g_panelRect.left   = abW;
    g_panelRect.top    = 0;
    g_panelRect.right  = abW + panelW;
    g_panelRect.bottom = rc.bottom;

    x = abW + panelW;
    w = rc.right - x;
    if (w < S(200)) w = S(200);

    /* editor area under the toolbar */
    y = toolH;
    h = rc.bottom - toolH;
    if (h < S(120)) h = S(120);

    {
        int track = h - splitH;
        int minPane = S(60);

        if (track < 2 * minPane) {
            codeH = track > 0 ? track / 2 : 0;       /* too short to honour both */
        } else {
            codeH = (int)((double)track * g_split + 0.5);
            if (codeH < minPane)           codeH = minPane;
            if (codeH > track - minPane)   codeH = track - minPane;
        }
        consoleH = track - codeH;
        if (consoleH < 0) consoleH = 0;
    }

    g_splitRect.left   = x;
    g_splitRect.right  = rc.right;
    g_splitRect.top    = y + codeH;
    g_splitRect.bottom = y + codeH + splitH;

    dwp = BeginDeferWindowPos(6);

    if (g_view == AB_EXPLORER) {
        /* with no project there is nothing to list; paint_main says so instead */
        dwp = move_child(dwp, hwndFiles, abW + S(8), S(40),
                         panelW - S(16), rc.bottom - S(48) - S(40),
                         g_projectDir[0] ? SWP_SHOWWINDOW : SWP_HIDEWINDOW);
        {
            int b;
            for (b = 0; b < BAR_COUNT; b++) {
                g_barRect[b].left = abW + S(10) + b * S(34);
                g_barRect[b].right = g_barRect[b].left + S(30);
                g_barRect[b].top = rc.bottom - S(38);
                g_barRect[b].bottom = g_barRect[b].top + S(30);
            }
        }
        ShowWindow(hwndFind, SW_HIDE);
    } else if (g_view == AB_SEARCH) {
        dwp = move_child(dwp, hwndFind, abW + S(8), S(40),
                         panelW - S(16), S(28), SWP_SHOWWINDOW);
        ShowWindow(hwndFiles, SW_HIDE);
    } else {
        ShowWindow(hwndFiles, SW_HIDE);
        ShowWindow(hwndFind, SW_HIDE);
    }

    dwp = move_child(dwp, hwndCode, x + pad, y,
                     w - pad * 2, codeH - S(6), 0);
    dwp = move_child(dwp, hwndConsole, x + pad, y + codeH + splitH,
                     w - pad * 2, consoleH - S(10), 0);

    if (dwp) EndDeferWindowPos(dwp);

    round_child(hwndCode, RADIUS_BIG - 1);
    round_child(hwndConsole, RADIUS_BIG - 1);
    round_child(hwndFind, RADIUS - 1);
    round_child(hwndFiles, RADIUS - 1);

    {
        RECT r;
        GetClientRect(hwndCode, &r);
        InflateRect(&r, -S(8), -S(6));
        r.left = GUTTER_W + S(8);           /* room for the line numbers */
        SendMessageA(hwndCode, EM_SETRECT, 0, (LPARAM)&r);
        GetClientRect(hwndConsole, &r);
        InflateRect(&r, -S(8), -S(6));
        SendMessageA(hwndConsole, EM_SETRECT, 0, (LPARAM)&r);
    }
}

/* ═════════════════════════════════════════════════ line numbers ══ */

/* The code box wraps long lines, so a number goes only on the first row of
 * each real line; the rows a long line wraps onto get none. The numbers
 * are painted into the edit control itself, straight after it paints. */
/* ═════════════════════════════════════════ checking for mistakes ══ */

#define CHECK_LINE  RGB(0xFF, 0xE0, 0x66)   /* the whole line with a mistake */
#define CHECK_SPOT  RGB(0xE5, 0x48, 0x4D)   /* the exact thing that is wrong */
#define MAX_CHECK   20

typedef struct { int line, col, len; } CheckMark;   /* line 1-based, col in chars */
static CheckMark g_chk[MAX_CHECK];
static int       g_chkCount;

/* Brings the code box's tidy view up to date: finished named shapes tidied,
 * the one the caret is in shown raw. Only the display changes - the file and
 * the dirty flag are left alone. */
static void tidy_update(void)
{
    int len, n, k, first;
    char *text;
    DWORD selA = 0, selB = 0;
    long caretLine = 0, caretCol, i, lineStart = 0;
    TidyEdit ed[128];

    if (g_tidying || !hwndCode) return;
    /* while the check marks are up the text must stay as they were made on */
    if (g_tidy && g_chkCount) return;

    len = GetWindowTextLengthA(hwndCode);
    text = malloc((size_t)len + 1);
    if (!text) return;
    GetWindowTextA(hwndCode, text, len + 1);
    SendMessageA(hwndCode, EM_GETSEL, (WPARAM)&selA, (LPARAM)&selB);
    for (i = 0; i < (long)selA && i < len; i++)
        if (text[i] == '\n') { caretLine++; lineStart = i + 1; }
    caretCol = (long)selA - lineStart;

    n = tidy_edits(text, tidy_arrow(), GetFocus() == hwndCode ? caretLine : -1, g_tidy, ed, 128);
    if (!n) { free(text); return; }

    g_tidying = TRUE;
    g_loading = TRUE;                 /* not typing, so not an edit to save */
    first = (int)SendMessageA(hwndCode, EM_GETFIRSTVISIBLELINE, 0, 0);
    SendMessageA(hwndCode, WM_SETREDRAW, FALSE, 0);
    for (k = n - 1; k >= 0; k--) {
        SendMessageA(hwndCode, EM_SETSEL, (WPARAM)ed[k].start, (LPARAM)(ed[k].start + ed[k].len));
        SendMessageA(hwndCode, EM_REPLACESEL, FALSE, (LPARAM)ed[k].with);
    }
    tidy_free_edits(ed, n);
    free(text);

    /* the same line and, as near as it can be, the same place on it: the
     * change never adds or removes a line */
    len = GetWindowTextLengthA(hwndCode);
    text = malloc((size_t)len + 1);
    if (text) {
        long line = caretLine, end;
        GetWindowTextA(hwndCode, text, len + 1);
        lineStart = 0;
        for (i = 0; i < len && line; i++)
            if (text[i] == '\n') { line--; lineStart = i + 1; }
        for (end = lineStart; end < len && text[end] != '\r' && text[end] != '\n'; end++) {}
        if (caretCol > end - lineStart) caretCol = end - lineStart;
        SendMessageA(hwndCode, EM_SETSEL, (WPARAM)(lineStart + caretCol), (LPARAM)(lineStart + caretCol));
        free(text);
    }
    SendMessageA(hwndCode, EM_LINESCROLL, 0,
                 (LPARAM)(first - (int)SendMessageA(hwndCode, EM_GETFIRSTVISIBLELINE, 0, 0)));
    SendMessageA(hwndCode, EM_EMPTYUNDOBUFFER, 0, 0);   /* its undo would be of text now gone */
    SendMessageA(hwndCode, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hwndCode, NULL, TRUE);
    g_loading = FALSE;
    g_tidying = FALSE;
}

static void paint_colours(HWND h);

static void toggle_tidy(HWND hwnd)
{
    g_tidy = !g_tidy;
    save_tidy();
    if (g_tidy && g_chkCount) {        /* the marks were holding it back */
        g_chkCount = 0;
        InvalidateRect(hwndCode, NULL, FALSE);
    }
    tidy_update();
    InvalidateRect(hwnd, NULL, FALSE);
}

/* Runs adda --check over what is in the editor, keeps where each mistake is
 * for paint_check, and lists them in the console. */
static void check_code(void)
{
    char tmp_dir[MAX_PATH], path[MAX_PATH * 2], exe[MAX_PATH], cmd[MAX_PATH * 4];
    char out[8192], *line;
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE rd = NULL, wr = NULL;
    DWORD got, total = 0;
    int len;
    char *code;
    FILE *f;

    g_chkCount = 0;

    /* the marks go on the raw code, so show it; tidy view waits until they go */
    if (g_tidy) {
        g_tidy = FALSE;
        tidy_update();
        g_tidy = TRUE;
    }

    len = GetWindowTextLengthA(hwndCode);
    code = malloc((size_t)len + 1);
    if (!code) return;
    GetWindowTextA(hwndCode, code, len + 1);
    GetTempPathA(MAX_PATH, tmp_dir);
    snprintf(path, sizeof path, "%s_adda_check.adda", tmp_dir);
    f = fopen(path, "wb");                /* as-is, so columns match the editor */
    if (!f) { free(code); return; }
    fwrite(code, 1, (size_t)len, f);
    fclose(f);
    free(code);

    get_adda_path(exe, MAX_PATH);
    snprintf(cmd, sizeof cmd, "\"%s\" --check \"%s\"", exe, path);

    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&rd, &wr, &sa, 0)) { remove(path); return; }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = wr;
    si.hStdError = wr;
    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                        NULL, NULL, &si, &pi)) {
        CloseHandle(rd); CloseHandle(wr); remove(path);
        MessageBoxA(hwndMain, "Could not find adda.exe to check your code with.",
                    "Check", MB_OK | MB_ICONWARNING);
        return;
    }
    CloseHandle(wr);
    while (total < sizeof out - 1 &&
           ReadFile(rd, out + total, (DWORD)(sizeof out - 1 - total), &got, NULL) && got)
        total += got;
    out[total] = '\0';
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(rd);
    remove(path);

    if (!g_running) console_append("\r\n");
    for (line = strtok(out, "\r\n"); line && g_chkCount < MAX_CHECK; line = strtok(NULL, "\r\n")) {
        CheckMark m;
        int used = 0;
        char msg[320];
        if (sscanf(line, "%d %d %d %n", &m.line, &m.col, &m.len, &used) < 3 || used == 0) continue;
        g_chk[g_chkCount++] = m;
        if (!g_running) {
            snprintf(msg, sizeof msg, "  line %d: %s\r\n", m.line, line + used);
            console_append(msg);
        }
    }
    if (!g_running) {
        char head[64];
        if (g_chkCount == 0) snprintf(head, sizeof head, "No mistakes found.\r\n");
        else snprintf(head, sizeof head, "%d mistake%s found (above).\r\n",
                      g_chkCount, g_chkCount == 1 ? "" : "s");
        console_append(head);
    }
    InvalidateRect(hwndCode, NULL, FALSE);
}

/* x of character i in the code box, or of the end of the row when i is the
 * first character of the next one */
static int char_x(HWND h, int i)
{
    LRESULT r = SendMessageA(h, EM_POSFROMCHAR, (WPARAM)i, 0);
    return (short)LOWORD(r);
}

/* The colour for each kind of piece of code, in the theme's light or dark
 * set - the dark one after VS Code's, which Abyss is too. */
static COLORREF code_colour(ColourKind k)
{
    static const unsigned DARK[COL_KINDS]  = { 0x569CD6, 0xC586C0, 0x9CDCFE, 0xB5CEA8,
                                               0xCE9178, 0x6A9955, 0 };
    static const unsigned LIGHT[COL_KINDS] = { 0x0000FF, 0xAF00DB, 0x001080, 0x098658,
                                               0xA31515, 0x008000, 0 };
    unsigned c;
    if (k == COL_PUNCT) return g_t.muted;
    c = g_t.dark ? DARK[k] : LIGHT[k];
    return RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

/* Colours the code. The EDIT control draws everything in one colour, so the
 * coloured pieces are drawn again on top, a character at a time where the
 * control put them - skipping the selection, which it highlights itself. */
static void paint_colours(HWND h)
{
    RECT rc;
    HDC dc;
    HGDIOBJ oldFont;
    TEXTMETRICA tm;
    int len, first, last, lineH, n, k;
    DWORD selA = 0, selB = 0;
    char *text;
    ColourSpan *sp;

    len = GetWindowTextLengthA(h);
    if (!len) return;
    text = malloc((size_t)len + 1);
    sp = malloc(sizeof *sp * 4096);
    if (!text || !sp) { free(text); free(sp); return; }
    GetWindowTextA(h, text, len + 1);
    n = colour_spans(text, sp, 4096);
    SendMessageA(h, EM_GETSEL, (WPARAM)&selA, (LPARAM)&selB);

    GetClientRect(h, &rc);
    dc = GetDC(h);
    oldFont = SelectObject(dc, hFontMono);
    GetTextMetricsA(dc, &tm);
    lineH = tm.tmHeight ? tm.tmHeight : 1;

    /* only what can be seen: from the first visible row to just past the last */
    first = (int)SendMessageA(h, EM_LINEINDEX,
                              (WPARAM)SendMessageA(h, EM_GETFIRSTVISIBLELINE, 0, 0), 0);
    last = (int)SendMessageA(h, EM_LINEINDEX,
                             (WPARAM)(SendMessageA(h, EM_GETFIRSTVISIBLELINE, 0, 0) +
                                      rc.bottom / lineH + 1), 0);
    if (last < 0 || last > len) last = len;

    HideCaret(h);
    SetBkMode(dc, OPAQUE);
    SetBkColor(dc, g_t.surface);
    for (k = 0; k < n; k++) {
        int i, from = (int)sp[k].start, to = (int)(sp[k].start + sp[k].len);
        if (to <= first || from >= last) continue;
        SetTextColor(dc, code_colour(sp[k].kind));
        for (i = from < first ? first : from; i < to && i < last; i++) {
            LRESULT pos;
            if ((DWORD)i >= selA && (DWORD)i < selB) continue;   /* highlighted already */
            if (text[i] == '\r' || text[i] == '\n' || text[i] == '\t') continue;
            pos = SendMessageA(h, EM_POSFROMCHAR, (WPARAM)i, 0);
            if (pos == -1) continue;
            if ((short)LOWORD(pos) < GUTTER_W) continue;          /* scrolled under the numbers */
            TextOutA(dc, (short)LOWORD(pos), (short)HIWORD(pos), text + i, 1);
        }
    }
    ShowCaret(h);

    SelectObject(dc, oldFont);
    ReleaseDC(h, dc);
    free(sp);
    free(text);
}

/* Paints over the edit control's own drawing: each row of a line with a
 * mistake gets a yellow band, the exact spot a red one, and the text is
 * drawn again on top in a colour that reads on them. */
static void paint_check(HWND h)
{
    RECT rc, fmt;
    HDC dc;
    HGDIOBJ oldFont;
    TEXTMETRICA tm;
    int len, first, count, v, k;
    char *text;

    if (!g_chkCount) return;
    GetClientRect(h, &rc);
    SendMessageA(h, EM_GETRECT, 0, (LPARAM)&fmt);
    len = GetWindowTextLengthA(h);
    text = malloc((size_t)len + 1);
    if (!text) return;
    GetWindowTextA(h, text, len + 1);

    dc = GetDC(h);
    oldFont = SelectObject(dc, hFontMono);
    GetTextMetricsA(dc, &tm);
    SetBkMode(dc, TRANSPARENT);

    first = (int)SendMessageA(h, EM_GETFIRSTVISIBLELINE, 0, 0);
    count = (int)SendMessageA(h, EM_GETLINECOUNT, 0, 0);

    for (k = 0; k < g_chkCount; k++) {
        const CheckMark *m = &g_chk[k];
        int ls = 0, le, n = 1, a, b;

        while (ls < len && n < m->line) { if (text[ls] == '\n') n++; ls++; }
        if (n != m->line) continue;
        for (le = ls; le < len && text[le] != '\r' && text[le] != '\n'; le++) {}
        a = ls + m->col;
        b = a + m->len;
        if (a > le) a = le;
        if (b > le) b = le;

        for (v = first; v < count; v++) {
            int rs = (int)SendMessageA(h, EM_LINEINDEX, (WPARAM)v, 0);
            int re = (v + 1 < count) ? (int)SendMessageA(h, EM_LINEINDEX, (WPARAM)v + 1, 0) : len;
            int y = fmt.top + (v - first) * tm.tmHeight, i;
            RECT band;

            if (y >= rc.bottom) break;
            if (re > le) re = le;
            if (rs < ls || rs > le || (rs == le && rs != ls)) continue;

            band.left = GUTTER_W;
            band.right = rc.right;
            band.top = y;
            band.bottom = y + tm.tmHeight;
            fill_rect(dc, band, CHECK_LINE);

            for (i = rs; i < re; i++) {
                int x = char_x(h, i);
                int w = (i + 1 < re) ? char_x(h, i + 1) - x : tm.tmAveCharWidth;
                BOOL spot = (i >= a && i < b);
                if (spot) {
                    RECT sr;
                    sr.left = x; sr.right = x + w; sr.top = y; sr.bottom = y + tm.tmHeight;
                    fill_rect(dc, sr, CHECK_SPOT);
                }
                if (text[i] != '\t') {
                    SetTextColor(dc, spot ? RGB(255, 255, 255) : RGB(0x1A, 0x1A, 0x1A));
                    TextOutA(dc, x, y, text + i, 1);
                }
            }
        }
    }

    SelectObject(dc, oldFont);
    ReleaseDC(h, dc);
    free(text);
}

static void paint_gutter(HWND h)
{
    RECT rc, fmt, g, num;
    HDC dc;
    HGDIOBJ oldFont;
    TEXTMETRICA tm;
    int len, first, count, v, line, y;
    char *text, buf[16];

    GetClientRect(h, &rc);
    SendMessageA(h, EM_GETRECT, 0, (LPARAM)&fmt);

    len = GetWindowTextLengthA(h);
    text = malloc((size_t)len + 1);
    if (!text) return;
    GetWindowTextA(h, text, len + 1);

    dc = GetDC(h);
    g = rc;
    g.right = GUTTER_W;
    fill_rect(dc, g, g_t.surface);
    g.left = g.right - 1;
    InflateRect(&g, 0, -S(6));
    fill_rect(dc, g, g_t.border);

    oldFont = SelectObject(dc, hFontMono);
    GetTextMetricsA(dc, &tm);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, g_t.muted);

    first = (int)SendMessageA(h, EM_GETFIRSTVISIBLELINE, 0, 0);
    count = (int)SendMessageA(h, EM_GETLINECOUNT, 0, 0);

    /* the real line the first visible row belongs to */
    line = 1;
    {
        int start = (int)SendMessageA(h, EM_LINEINDEX, (WPARAM)first, 0), i;
        for (i = 0; i < start && i < len; i++)
            if (text[i] == '\n') line++;
    }

    for (v = first, y = fmt.top; v < count && y < rc.bottom; v++, y += tm.tmHeight) {
        int idx = (int)SendMessageA(h, EM_LINEINDEX, (WPARAM)v, 0);
        BOOL starts = (v == 0) || (idx > 0 && idx <= len && text[idx - 1] == '\n');

        if (v > first && starts) line++;
        if (!starts) continue;

        num.left = 0;
        num.right = GUTTER_W - S(8);
        num.top = y;
        num.bottom = y + tm.tmHeight;
        snprintf(buf, sizeof buf, "%d", line);
        DrawTextA(dc, buf, -1, &num, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
    }

    SelectObject(dc, oldFont);
    ReleaseDC(h, dc);
    free(text);
}

static LRESULT CALLBACK CodeProc(HWND h, UINT msg, WPARAM w, LPARAM l,
                                 UINT_PTR id, DWORD_PTR ref)
{
    LRESULT res;
    (void)id; (void)ref;
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(h, CodeProc, 0);
    res = DefSubclassProc(h, msg, w, l);
    if (msg == WM_PAINT) {
        paint_colours(h);           /* first, so the check marks are drawn over it */
        paint_check(h);
        paint_gutter(h);
    }
    /* the caret may have moved onto a tidy line, or off one */
    if ((msg == WM_KEYUP || msg == WM_LBUTTONUP || msg == WM_SETFOCUS || msg == WM_KILLFOCUS) &&
        !g_tidying)
        PostMessageA(hwndMain, WM_TIDY, 0, 0);
    return res;
}

/* ══════════════════════════════════════════════════ main paint ══ */

static void paint_main(HWND hwnd, HDC hdc)
{
    RECT rc, r;
    int i;

    GetClientRect(hwnd, &rc);
    fill_rect(hdc, rc, g_t.bg);

    /* activity bar */
    r = rc;
    r.right = S(48);
    fill_rect(hdc, r, g_t.abBg);

    for (i = 0; i < AB_COUNT; i++) {
        RECT box = g_abRect[i];
        RECT icon, pill;
        int shrink = 0;
        BOOL active = (i == g_view) || (i == AB_TIDY && g_tidy);   /* lit while it is on */
        /* Run only makes sense while idle, Stop only while running */
        BOOL disabled = (i == AB_RUN && g_running) || (i == AB_STOP && !g_running);
        double glow = disabled ? 0 : g_abGlow[i];
        COLORREF cell = blend(g_t.abBg, g_t.abHover, glow);
        COLORREF fg = active ? g_t.abIcon : blend(g_t.abIconDim, g_t.abIcon, glow);

        if (disabled)                    /* halfway between dim and the bar */
            fg = RGB((GetRValue(g_t.abIconDim) + GetRValue(g_t.abBg)) / 2,
                     (GetGValue(g_t.abIconDim) + GetGValue(g_t.abBg)) / 2,
                     (GetBValue(g_t.abIconDim) + GetBValue(g_t.abBg)) / 2);
        else if (i == AB_STOP)           /* something is running: make it obvious */
            fg = g_t.abIcon;

        if (i == AB_NEW) {               /* a hairline between views and actions */
            RECT sep = box;
            sep.left += S(12); sep.right -= S(12);
            sep.top -= S(3); sep.bottom = sep.top + 1;
            fill_rect(hdc, sep, g_t.abIconDim);
        }

        {
            double k = (i == g_abPress) ? press_amount(g_abPressAt) : 0;
            if (k > 0) {
                int in = (int)(S(4) * k);
                cell = blend(cell, g_t.accent, 0.35 * k);
                pill = box;
                InflateRect(&pill, -S(5) - in, -S(3) - in);
                round_fill(hdc, pill, cell, RADIUS);
                shrink = (int)(S(2) * k);
            } else if (glow > 0.01) {
                pill = box;
                InflateRect(&pill, -S(5), -S(3));
                round_fill(hdc, pill, cell, RADIUS);
            }
        }

        if (active) {
            RECT bar = box;
            bar.left += S(1);
            bar.right = bar.left + S(3);
            InflateRect(&bar, 0, -S(8));
            round_fill(hdc, bar, g_t.abIcon, S(2));
        }

        icon = box;
        InflateRect(&icon, -S(12) - shrink, -S(12) - shrink);
        /* the icon is blitted opaque, so it must be given the very colour the
         * cell was just filled with or it shows as a square patch */
        draw_icon(hdc, icon, AB_ICON[i], fg, cell);
    }

    /* side panel */
    if (g_view >= 0) {
        RECT title = g_panelRect;
        RECT edge;

        fill_rect(hdc, g_panelRect, g_t.bg);

        title.left += S(14);
        title.top  += S(12);
        title.bottom = title.top + S(20);
        {
            char heading[MAX_PATH + 2];
            snprintf(heading, sizeof heading, "%s", (g_view == AB_SEARCH) ? "SEARCH" : "EXPLORER");
            if (g_view == AB_EXPLORER && g_projectDir[0]) {
                project_name(heading, sizeof heading);
                CharUpperA(heading);
            }
            text_at(hdc, title, heading, hFontSmall, g_t.muted,
                    DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }

        if (g_view == AB_EXPLORER && !g_projectDir[0]) {
            /* nothing to list: say how to get a project */
            RECT line = g_panelRect;
            line.left += S(14);
            line.right -= S(14);
            line.top = S(48);
            line.bottom = line.top + S(18);
            text_at(hdc, line, "No project open", hFontUIBold, g_t.text, DT_LEFT | DT_SINGLELINE);
            line.top = S(72);
            line.bottom = g_panelRect.bottom - S(60);
            text_at(hdc, line, "New Project makes one. Import can open a folder you already have.",
                    hFontSmall, g_t.muted, DT_LEFT | DT_WORDBREAK);
        }

        edge = g_panelRect;
        edge.left = edge.right - 1;
        fill_rect(hdc, edge, g_t.border);

        if (g_view == AB_EXPLORER) {        /* Save and Import, under a hairline */
            RECT line;
            int b;
            line.left = g_panelRect.left + S(10);
            line.right = g_panelRect.right - S(10);
            line.top = g_barRect[0].top - S(6);
            line.bottom = line.top + 1;
            fill_rect(hdc, line, g_t.border);
            for (b = 0; b < BAR_COUNT; b++) {
                double k = (b == g_barPress) ? press_amount(g_barPressAt) : 0;
                double glow = g_barGlow[b];
                COLORREF bg = g_t.bg;
                RECT pill = g_barRect[b], icon = g_barRect[b];
                if (glow > 0.01 || k > 0) {
                    bg = blend(blend(g_t.bg, g_t.ghostHot, k > 0 ? 1 : glow),
                               g_t.accent, 0.35 * k);
                    InflateRect(&pill, -(int)(S(3) * k), -(int)(S(3) * k));
                    round_fill(hdc, pill, bg, S(6));
                }
                InflateRect(&icon, -S(7) - (int)(S(2) * k), -S(7) - (int)(S(2) * k));
                draw_icon(hdc, icon, b == BAR_SAVE ? ICON_SAVE : ICON_IMPORT,
                          blend(g_t.muted, g_t.text, glow), bg);
            }
        }

        if (g_view == AB_SEARCH) {          /* frame for the borderless find box */
            RECT fr;
            GetWindowRect(hwndFind, &fr);
            MapWindowPoints(NULL, hwnd, (POINT *)&fr, 2);
            InflateRect(&fr, 1, 1);
            round_frame(hdc, fr, g_t.border, RADIUS);
        }
    }

    /* splitter: a couple of faint lines, like a grip */
    {
        RECT grip = g_splitRect;
        int midY = (grip.top + grip.bottom) / 2;
        RECT line;
        line.left = grip.left + S(12);
        line.right = grip.right - S(12);
        line.top = midY;
        line.bottom = midY + 1;
        fill_rect(hdc, line, g_t.border);
    }

    /* panel outlines */
    {
        RECT cr;
        GetWindowRect(hwndCode, &cr);
        MapWindowPoints(NULL, hwnd, (POINT *)&cr, 2);
        InflateRect(&cr, 1, 1);
        round_frame(hdc, cr, g_t.border, RADIUS_BIG);

        GetWindowRect(hwndConsole, &cr);
        MapWindowPoints(NULL, hwnd, (POINT *)&cr, 2);
        InflateRect(&cr, 1, 1);
        round_frame(hdc, cr, g_t.border, RADIUS_BIG);
    }

    /* a word about what the console is for */
    if (g_running) {
        RECT hint;
        hint.left = g_splitRect.left + S(12);
        hint.right = g_splitRect.right - S(12);
        hint.bottom = rc.bottom - S(1);
        hint.top = hint.bottom - S(14);
        text_at(hdc, hint, "type your answer and press Enter",
                hFontSmall, g_t.muted, DT_RIGHT | DT_SINGLELINE);
    }
}

/* ═══════════════════════════════════════════════ settings popup ══ */

#define SET_NAV_W  S(140)

static int g_setRowHot = -1;
static double g_rowGlow[PICK_COUNT];
static DWORD  g_rowTick;

static void settings_paint(HWND hwnd, HDC hdc)
{
    RECT rc, nav, item;
    int i, y;

    GetClientRect(hwnd, &rc);
    fill_rect(hdc, rc, g_t.surface);

    nav = rc;
    nav.right = SET_NAV_W;
    fill_rect(hdc, nav, g_t.bg);
    {
        RECT edge = nav;
        edge.left = edge.right - 1;
        fill_rect(hdc, edge, g_t.border);
    }

    item = nav;
    item.left  += S(10);
    item.top   += S(16);
    item.right -= S(10);
    item.bottom = item.top + S(28);
    round_fill(hdc, item, g_t.sel, RADIUS);
    {
        RECT label = item;
        label.left += S(10);
        text_at(hdc, label, "Themes", hFontUIBold, g_t.text,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    /* the Themes page */
    y = S(16);
    {
        RECT head;
        head.left = SET_NAV_W + S(20);
        head.right = rc.right - S(16);
        head.top = y;
        head.bottom = y + S(24);
        text_at(hdc, head, "Themes", hFontUIBold, g_t.text,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        y += S(34);
    }

    for (i = 0; i < PICK_COUNT; i++) {
        Theme t = theme_for(i);
        RECT row, sw, label, about;
        int j;
        COLORREF chips[3];

        row.left = SET_NAV_W + S(12);
        row.right = rc.right - S(12);
        row.top = y;
        row.bottom = y + S(52);

        {
            double k = (i == g_rowPress) ? press_amount(g_rowPressAt) : 0;
            COLORREF base = (i == g_pick) ? g_t.sel
                          : blend(g_t.surface, g_t.ghostHot, k > 0 ? 1 : g_rowGlow[i]);
            RECT pill = row;
            if (k > 0) {
                InflateRect(&pill, -(int)(S(4) * k), -(int)(S(2) * k));
                base = blend(base, g_t.accent, 0.3 * k);
            }
            if (i == g_pick || k > 0 || g_rowGlow[i] > 0.01)
                round_fill(hdc, pill, base, RADIUS_BIG);
        }

        /* three chips that say what the theme looks like */
        chips[0] = t.abBg;
        chips[1] = t.surface;
        chips[2] = t.accent;
        for (j = 0; j < 3; j++) {
            sw.left = row.left + S(10) + j * S(16);
            sw.right = sw.left + S(14);
            sw.top = row.top + S(18);
            sw.bottom = sw.top + S(16);
            round_fill(hdc, sw, chips[j], S(4));
            round_frame(hdc, sw, g_t.border, S(4));
        }

        label = row;
        label.left += S(70);
        label.top  += S(8);
        label.bottom = label.top + S(18);
        text_at(hdc, label, THEME_NAMES[i], hFontUIBold, g_t.text,
                DT_LEFT | DT_SINGLELINE);

        about = row;
        about.left += S(70);
        about.top  += S(27);
        about.bottom = about.top + S(18);
        text_at(hdc, about, THEME_ABOUT[i], hFontSmall, g_t.muted,
                DT_LEFT | DT_SINGLELINE);

        if (i == g_pick) {
            RECT tick = row;
            tick.left = row.right - S(30);
            text_at(hdc, tick, "in use", hFontSmall, g_t.muted,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        y = row.bottom + S(4);
    }
}

static int settings_row_at(HWND hwnd, POINT p)
{
    RECT rc;
    int i, y = S(16) + S(34);

    GetClientRect(hwnd, &rc);
    if (p.x < SET_NAV_W) return -1;

    for (i = 0; i < PICK_COUNT; i++) {
        if (p.y >= y && p.y < y + S(52)) return i;
        y += S(52) + S(4);
    }
    return -1;
}

static LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        settings_paint(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        POINT p;
        int was = g_setRowHot;
        TRACKMOUSEEVENT tme;

        p.x = GET_X_LPARAM(l); p.y = GET_Y_LPARAM(l);
        g_setRowHot = settings_row_at(hwnd, p);
        if (g_setRowHot != was) {
            g_rowTick = GetTickCount();
            SetTimer(hwnd, ID_ANIM, 15, NULL);
        }

        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        tme.dwHoverTime = 0;
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (g_setRowHot != -1) {
            g_setRowHot = -1;
            g_rowTick = GetTickCount();
            SetTimer(hwnd, ID_ANIM, 15, NULL);
        }
        return 0;

    case WM_LBUTTONDOWN: {
        POINT p;
        int row;
        p.x = GET_X_LPARAM(l); p.y = GET_Y_LPARAM(l);
        row = settings_row_at(hwnd, p);
        if (row >= 0) {
            g_rowPress = row;
            g_rowPressAt = GetTickCount();
            g_rowTick = GetTickCount();
            SetTimer(hwnd, ID_ANIM, 15, NULL);
            g_pick = row;
            save_pick();
            apply_theme(hwndMain);
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;
    }

    case WM_TIMER:
        if (w == ID_ANIM) {
            DWORD now = GetTickCount();
            double step = (double)(now - g_rowTick) / HOVER_MS;
            BOOL moving = FALSE;
            int i;
            g_rowTick = now;
            for (i = 0; i < PICK_COUNT; i++)
                moving |= approach(&g_rowGlow[i], g_setRowHot == i, step);
            if (press_amount(g_rowPressAt) <= 0) g_rowPress = -1;
            else moving = TRUE;
            if (!moving) KillTimer(hwnd, ID_ANIM);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        break;

    case WM_KEYDOWN:
        if (w == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        hwndSettings = NULL;
        return 0;
    }
    return DefWindowProcA(hwnd, msg, w, l);
}

static void centre_on(HWND child, HWND owner, int w, int h)
{
    RECT o;
    int x, y;

    GetWindowRect(owner, &o);
    x = o.left + ((o.right - o.left) - w) / 2;
    y = o.top + ((o.bottom - o.top) - h) / 2;
    SetWindowPos(child, HWND_TOP, x, y, w, h, SWP_NOZORDER);
}

static void open_settings(HWND owner)
{
    int w = S(600), h = S(430);

    if (hwndSettings) { SetForegroundWindow(hwndSettings); return; }

    hwndSettings = CreateWindowExA(
        WS_EX_DLGMODALFRAME, "AddaSettings", "Settings",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        0, 0, w, h, owner, NULL, GetModuleHandleA(NULL), NULL);
    if (!hwndSettings) return;

    centre_on(hwndSettings, owner, w, h);
    apply_titlebar(hwndSettings);
    ShowWindow(hwndSettings, SW_SHOW);
}

/* ══════════════════════════════════════════════ cheat sheet ══ */

static void refresh_cheats(void)
{
    char needle[128];
    int i;

    needle[0] = '\0';
    if (hwndCheatFind) GetWindowTextA(hwndCheatFind, needle, sizeof(needle));

    g_cheatCount = 0;
    if (g_cheatPage == 0 && needle[0]) {
        /* searching from the list of topics looks through every topic */
        int pg;
        for (pg = 1; pg < CHEAT_PAGE_COUNT; pg++)
            for (i = 0; i < CHEAT_PAGE_SIZES[pg]; i++) {
                const Cheat *c = &CHEAT_PAGES[pg][i];
                if (c->snippet && (has_text(c->title, needle) || has_text(c->about, needle) ||
                                   has_text(c->snippet, needle)))
                    g_cheatShown[g_cheatCount++] = c;
            }
    } else {
        for (i = 0; i < CHEAT_PAGE_SIZES[g_cheatPage]; i++) {
            const Cheat *c = &CHEAT_PAGES[g_cheatPage][i];
            if (!needle[0] || !c->snippet ||      /* the way back always shows */
                has_text(c->title, needle) || has_text(c->about, needle) ||
                has_text(c->snippet, needle))
                g_cheatShown[g_cheatCount++] = c;
        }
    }

    if (!hwndCheatList) return;
    SendMessageA(hwndCheatList, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < g_cheatCount; i++)
        SendMessageA(hwndCheatList, LB_ADDSTRING, 0,
                     (LPARAM)g_cheatShown[i]->title);
    if (g_cheatCount) SendMessageA(hwndCheatList, LB_SETCURSEL, 0, 0);
}

/* Shows an entry's own page - what it does, and an example to read and type
 * out - or, given NULL, goes back to the list. Nothing is ever pasted into the
 * program: the cheat sheet explains, and the typing is left to the reader. */
static void show_cheat(const Cheat *c)
{
    g_cheatOpen = c;
    g_cheatBackHot = FALSE;
    if (!hwndCheats) return;
    ShowWindow(hwndCheatFind, c ? SW_HIDE : SW_SHOW);
    ShowWindow(hwndCheatList, c ? SW_HIDE : SW_SHOW);
    InvalidateRect(hwndCheats, NULL, TRUE);
    SetFocus(c ? hwndCheats : hwndCheatFind);
}

/* A click on a row: a topic (or the way back) turns the page, anything else
 * opens its own page. */
static void open_cheat(int shownIndex)
{
    const Cheat *c;

    if (shownIndex < 0 || shownIndex >= g_cheatCount) return;
    c = g_cheatShown[shownIndex];
    g_cheatTick = GetTickCount();
    if (!c->snippet) {                          /* a link: turn the page */
        g_cheatPage = c->page;
        if (hwndCheatFind) SetWindowTextA(hwndCheatFind, "");   /* a new page starts unfiltered */
        refresh_cheats();
        return;
    }
    show_cheat(c);
}

/* wrapped text; returns the height it took */
static int text_wrapped(HDC hdc, RECT r, const char *s, HFONT font, COLORREF colour)
{
    HGDIOBJ old = SelectObject(hdc, font);
    RECT m = r;
    int h;

    DrawTextA(hdc, s, -1, &m, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
    SelectObject(hdc, old);
    h = m.bottom - m.top;
    r.bottom = r.top + h;
    text_at(hdc, r, s, font, colour, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    return h;
}

/* An entry's own page: the way back, what it does, and an example. */
static void paint_cheat_page(HDC hdc, RECT rc)
{
    const Cheat *c = g_cheatOpen;
    RECT r, box;
    TEXTMETRICA tm;
    HGDIOBJ old;
    const char *line;
    int y, lineH, lines = 1;

    g_cheatBack.left = S(14);
    g_cheatBack.top = S(14);
    g_cheatBack.right = g_cheatBack.left + S(74);
    g_cheatBack.bottom = g_cheatBack.top + S(26);
    round_fill(hdc, g_cheatBack, g_cheatBackHot ? g_t.sel : g_t.ghostHot, S(13));
    text_at(hdc, g_cheatBack, "<- Back", hFontUI, g_t.text,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    r.left = S(20);
    r.right = rc.right - S(20);
    y = S(58);

    r.top = y; r.bottom = rc.bottom;
    y += text_wrapped(hdc, r, c->title, hFontTitle, g_t.text) + S(16);

    r.top = y; r.bottom = y + S(16);
    text_at(hdc, r, "WHAT IT DOES", hFontSmall, g_t.muted, DT_LEFT | DT_SINGLELINE);
    y += S(22);
    r.top = y; r.bottom = rc.bottom;
    y += text_wrapped(hdc, r, c->detail, hFontBody, g_t.text) + S(22);

    r.top = y; r.bottom = y + S(16);
    text_at(hdc, r, "EXAMPLE", hFontSmall, g_t.muted, DT_LEFT | DT_SINGLELINE);
    y += S(22);

    old = SelectObject(hdc, hFontMono);
    GetTextMetricsA(hdc, &tm);
    SelectObject(hdc, old);
    lineH = tm.tmHeight + S(3);
    for (line = c->snippet; (line = strstr(line, "\r\n")) != NULL; line += 2) lines++;

    box.left = r.left;
    box.right = r.right;
    box.top = y;
    box.bottom = y + lines * lineH + S(24);
    round_fill(hdc, box, g_t.bg, RADIUS_BIG);
    round_frame(hdc, box, g_t.border, RADIUS_BIG);

    /* one line of the example at a time: the \r\n between them ends each */
    y += S(12);
    for (line = c->snippet; *line; ) {
        const char *end = strstr(line, "\r\n");
        int len = end ? (int)(end - line) : (int)strlen(line);
        HGDIOBJ was = SelectObject(hdc, hFontMono);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, g_t.text);
        TextOutA(hdc, box.left + S(16), y, line, len);
        SelectObject(hdc, was);
        y += lineH;
        line += len;
        if (end) line += 2;
    }
}

static void draw_cheat_item(DRAWITEMSTRUCT *di)
{
    RECT r = di->rcItem;
    RECT line;
    const Cheat *c;
    BOOL selected = (di->itemState & ODS_SELECTED) != 0;

    if ((int)di->itemID < 0 || (int)di->itemID >= g_cheatCount) {
        fill_rect(di->hDC, r, g_t.surface);
        return;
    }
    c = g_cheatShown[di->itemID];

    fill_rect(di->hDC, r, g_t.surface);
    if (selected) {
        RECT pill = r;
        InflateRect(&pill, -S(6), -S(2));
        round_fill(di->hDC, pill, g_t.sel, RADIUS);
    }

    line = r;
    line.left += S(12);
    line.right -= S(12);
    line.top += S(6);
    line.bottom = line.top + S(18);
    text_at(di->hDC, line, c->title, hFontUIBold, g_t.text,
            DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    line.top = line.bottom;
    line.bottom = line.top + S(17);
    text_at(di->hDC, line, c->about, hFontSmall, g_t.muted,
            DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void cheats_paint(HWND hwnd, HDC hdc)
{
    RECT rc, hint;

    GetClientRect(hwnd, &rc);
    fill_rect(hdc, rc, g_t.surface);
    if (g_cheatOpen) paint_cheat_page(hdc, rc);

    hint = rc;
    hint.left += S(14);
    hint.right -= S(14);
    hint.bottom -= S(8);
    hint.top = hint.bottom - S(18);
    text_at(hdc, hint,
            g_cheatOpen ? "Type it into your own program to try it out"
                        : "Click a topic, then click anything in it to see how it is done",
            hFontSmall, g_t.muted, DT_LEFT | DT_SINGLELINE);
}

static LRESULT CALLBACK CheatsProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        cheats_paint(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)w;
        SetTextColor(hdc, g_t.text);
        SetBkColor(hdc, g_t.surface);
        return (LRESULT)hBrushSurface;
    }

    case WM_DRAWITEM:
        draw_cheat_item((DRAWITEMSTRUCT *)l);
        return TRUE;

    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *mi = (MEASUREITEMSTRUCT *)l;
        mi->itemHeight = (UINT)S(43);
        return TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(w) == ID_CHEATFIND && HIWORD(w) == EN_CHANGE) {
            refresh_cheats();
            return 0;
        }
        /* one click opens a row. The second click of a double-click is not
         * meant for whatever row the first one put under the pointer, so a
         * click that comes that soon after a page has turned is let go. */
        if (LOWORD(w) == ID_CHEATLIST && HIWORD(w) == LBN_SELCHANGE &&
            GetKeyState(VK_LBUTTON) < 0) {
            if (GetTickCount() - g_cheatTick < GetDoubleClickTime()) {
                SendMessageA(hwndCheatList, LB_SETCURSEL, 0, 0);
                return 0;
            }
            open_cheat((int)SendMessageA(hwndCheatList, LB_GETCURSEL, 0, 0));
            return 0;
        }
        break;

    case WM_MOUSEMOVE: {
        POINT p;
        BOOL hot;
        TRACKMOUSEEVENT tme;

        p.x = GET_X_LPARAM(l); p.y = GET_Y_LPARAM(l);
        hot = g_cheatOpen && contains(g_cheatBack, p);
        if (hot != g_cheatBackHot) {
            g_cheatBackHot = hot;
            InvalidateRect(hwnd, &g_cheatBack, FALSE);
        }
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        tme.dwHoverTime = 0;
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (g_cheatBackHot) { g_cheatBackHot = FALSE; InvalidateRect(hwnd, &g_cheatBack, FALSE); }
        return 0;

    case WM_LBUTTONDOWN: {
        POINT p;
        p.x = GET_X_LPARAM(l); p.y = GET_Y_LPARAM(l);
        /* not the tail of the double-click that opened this page */
        if (g_cheatOpen && contains(g_cheatBack, p) &&
            GetTickCount() - g_cheatTick >= GetDoubleClickTime())
            show_cheat(NULL);
        return 0;
    }

    case WM_SIZE: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        MoveWindow(hwndCheatFind, S(14), S(14),
                   rc.right - S(28), S(28), TRUE);
        MoveWindow(hwndCheatList, S(14), S(50),
                   rc.right - S(28), rc.bottom - S(50) - S(32), TRUE);
        return 0;
    }

    case WM_KEYDOWN:
        if (w == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        hwndCheats = NULL;
        hwndCheatFind = NULL;
        hwndCheatList = NULL;
        g_cheatOpen = NULL;
        return 0;
    }
    return DefWindowProcA(hwnd, msg, w, l);
}

static void open_cheats(HWND owner)
{
    int w = S(560), h = S(520);
    HINSTANCE inst = GetModuleHandleA(NULL);

    if (hwndCheats) { SetForegroundWindow(hwndCheats); return; }

    hwndCheats = CreateWindowExA(
        WS_EX_DLGMODALFRAME, "AddaCheats", "Cheat sheet",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        0, 0, w, h, owner, NULL, inst, NULL);
    if (!hwndCheats) return;

    hwndCheatFind = CreateWindowExA(0, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        0, 0, 0, 0, hwndCheats, (HMENU)(UINT_PTR)ID_CHEATFIND, inst, NULL);

    hwndCheatList = CreateWindowExA(0, "LISTBOX", "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
        LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY,
        0, 0, 0, 0, hwndCheats, (HMENU)(UINT_PTR)ID_CHEATLIST, inst, NULL);

    SendMessage(hwndCheatFind, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    SendMessage(hwndCheatList, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    theme_edit(hwndCheatFind);
    theme_edit(hwndCheatList);

    g_cheatPage = 0;                 /* always opens on the list of topics */
    g_cheatOpen = NULL;
    SetWindowTextA(hwndCheatFind, "");
    refresh_cheats();

    centre_on(hwndCheats, owner, w, h);
    apply_titlebar(hwndCheats);
    ShowWindow(hwndCheats, SW_SHOW);
    SetFocus(hwndCheatFind);
}

/* ═══════════════════════════════════════════════ full screen ══ */

static void toggle_fullscreen(HWND hwnd)
{
    DWORD style = GetWindowLongA(hwnd, GWL_STYLE);

    if (!g_fullscreen) {
        MONITORINFO mi;
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        g_wp.length = sizeof(g_wp);
        GetWindowPlacement(hwnd, &g_wp);
        SetWindowLongA(hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
        GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
        SetWindowPos(hwnd, HWND_TOP,
            mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right - mi.rcMonitor.left,
            mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        g_fullscreen = TRUE;
    } else {
        SetWindowLongA(hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd, &g_wp);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        g_fullscreen = FALSE;
    }
}

/* ═════════════════════════════════════════════════ main window ══ */

/* starts the main window's animation timer, unless it is already going */
static void start_anim(HWND hwnd)
{
    if (g_animOn) return;
    g_animOn = TRUE;
    g_animTick = GetTickCount();
    SetTimer(hwnd, ID_ANIM, 15, NULL);
}

/* ═══════════════════════════════════════ saving and importing ══ */

static int bar_hit(POINT p)
{
    int i;
    if (g_view != AB_EXPLORER) return -1;
    for (i = 0; i < BAR_COUNT; i++)
        if (contains(g_barRect[i], p)) return i;
    return -1;
}

/* Writes what is in the editor to wherever the Save As dialog says, turning
 * the edit control's \r\n back into plain \n like the files on disk. */
static void save_as(HWND hwnd)
{
    OPENFILENAMEA ofn;
    char path[MAX_PATH * 2] = "program.adda", dir[MAX_PATH];
    int sel = (int)SendMessageA(hwndFiles, LB_GETCURSEL, 0, 0);
    int len, i, j;
    char *text;
    FILE *f;

    if (sel >= 0 && sel < g_fileCount)
        snprintf(path, sizeof path, "%s", g_files[sel].name);
    explorer_dir(dir, sizeof dir);

    ZeroMemory(&ofn, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "Adda programs (*.adda)\0*.adda\0All files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = sizeof path;
    ofn.lpstrInitialDir = dir[0] ? dir : NULL;
    ofn.lpstrDefExt = "adda";
    ofn.lpstrTitle = "Save";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameA(&ofn)) return;

    len = GetWindowTextLengthA(hwndCode);
    text = malloc((size_t)len + 1);
    if (!text) return;
    GetWindowTextA(hwndCode, text, len + 1);
    for (i = j = 0; i < len; i++)
        if (!(text[i] == '\r' && text[i + 1] == '\n')) text[j++] = text[i];

    f = fopen(path, "wb");
    if (!f || fwrite(text, 1, (size_t)j, f) != (size_t)j) {
        MessageBoxA(hwnd, "Windows would not save the file there.\n"
                          "It may be read-only, or in a folder you cannot change.",
                    "Save", MB_OK | MB_ICONWARNING);
    }
    else {                           /* what you type now goes into the saved file */
        snprintf(g_curPath, sizeof g_curPath, "%s", path);
        g_dirty = FALSE;
        show_current();
    }
    if (f) fclose(f);
    free(text);
    rescan_files();                  /* it may have landed in the Explorer */
}

/* Copies the chosen files next to adda-gui.exe, where the Explorer looks,
 * numbering any whose name is already taken, and opens the last one. */
static void import_files(HWND hwnd);
static void import_folder(HWND hwnd);

/* The Open dialog cannot pick a folder, so Import asks which it is first: a
 * folder opens as the project, files are added to the open project. */
static void import_menu(HWND hwnd)
{
    HMENU m = CreatePopupMenu();
    POINT at;
    int pick;

    AppendMenuA(m, MF_STRING, 2, "Import Folder (open it as the project)...");
    AppendMenuA(m, MF_STRING, 1, "Import Files into this project...");
    at.x = g_barRect[BAR_IMPORT].left;
    at.y = g_barRect[BAR_IMPORT].top;
    ClientToScreen(hwnd, &at);
    pick = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                               at.x, at.y, 0, hwnd, NULL);
    DestroyMenu(m);
    if (pick == 1) import_files(hwnd);
    if (pick == 2) import_folder(hwnd);
}

/* Puts the open file away: saved, and out of the editor. */
static void close_file(void)
{
    save_current();
    g_curPath[0] = '\0';
    g_loading = TRUE;
    SetWindowTextA(hwndCode, "");
    g_loading = FALSE;
    g_dirty = FALSE;
    g_chkCount = 0;
    show_current();
}

/* Makes `dir` the project: the Explorer shows it, and its first.adda - or else
 * its first program - is opened. The folder is used where it is; nothing is
 * copied. */
static void open_project(HWND hwnd, const char *dir)
{
    size_t n = strlen(dir);
    int i, first = -1;

    close_file();
    snprintf(g_projectDir, sizeof g_projectDir, "%s%s", dir,
             (n && dir[n - 1] == '\\') ? "" : "\\");
    remember_project();

    g_view = AB_EXPLORER;
    layout(hwnd);
    rescan_files();
    for (i = 0; i < g_fileCount; i++)
        if (first < 0 || lstrcmpiA(g_files[i].name, "first.adda") == 0) first = i;
    if (first >= 0) {
        SendMessageA(hwndFiles, LB_SETCURSEL, (WPARAM)first, 0);
        open_file(first);
    }
    show_current();
    InvalidateRect(hwnd, NULL, TRUE);
}

static void need_project(HWND hwnd)
{
    MessageBoxA(hwnd, "Open a project first.\n"
                      "New Project makes one, and Import Folder can open one you already have.",
                "Adda", MB_OK | MB_ICONINFORMATION);
}

/* A folder becomes the project, used where it is - importing it again just
 * opens it again, and never makes a numbered copy. */
static void import_folder(HWND hwnd)
{
    BROWSEINFOA bi;
    LPITEMIDLIST picked;
    char from[MAX_PATH + 2];

    ZeroMemory(&bi, sizeof bi);
    bi.hwndOwner = hwnd;
    bi.lpszTitle = "Choose the project folder to open";
    bi.ulFlags = BIF_RETURNONLYFSDIRS;
    picked = SHBrowseForFolderA(&bi);
    if (!picked) return;
    from[0] = '\0';
    if (!SHGetPathFromIDListA(picked, from)) { CoTaskMemFree(picked); return; }
    CoTaskMemFree(picked);
    if (from[0]) open_project(hwnd, from);
}

/* Loose files are copied into the open project, a name already taken getting
 * a number; a file that is already in the project is simply opened. */
static void import_files(HWND hwnd)
{
    OPENFILENAMEA ofn;
    static char list[8192];
    static char from[sizeof list + MAX_PATH];
    static char folder[sizeof list + 2];
    char dir[MAX_PATH + 2], to[MAX_PATH * 3], last[MAX_PATH * 3] = "";
    const char *name;
    int failed = 0, i;
    BOOL multi;

    if (!g_projectDir[0]) { need_project(hwnd); return; }

    list[0] = '\0';
    ZeroMemory(&ofn, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "Adda programs (*.adda)\0*.adda\0All files\0*.*\0";
    ofn.lpstrFile = list;
    ofn.nMaxFile = sizeof list;
    ofn.lpstrTitle = "Import into this project";
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST |
                OFN_NOCHANGEDIR;
    if (!GetOpenFileNameA(&ofn)) return;

    explorer_dir(dir, sizeof dir);

    /* One file: the full path, with nFileOffset pointing at its name.
     * Several: the folder, then each name, each ended by a NUL and the list
     * by a second one - which shows as a NUL just before nFileOffset. */
    multi = (list[ofn.nFileOffset - 1] == '\0');

    /* the folder they come from, with a trailing backslash like `dir` */
    if (multi) snprintf(folder, sizeof folder, "%s\\", list);
    else       snprintf(folder, sizeof folder, "%.*s", (int)ofn.nFileOffset, list);

    for (name = list + ofn.nFileOffset; *name; name += strlen(name) + 1) {
        char base[MAX_PATH], *dot;
        const char *ext = strrchr(name, '.');

        if (lstrcmpiA(folder, dir) == 0) {           /* already in the project */
            lstrcpynA(last, name, (int)sizeof last);
            if (!multi) break;
            continue;
        }

        if (multi) snprintf(from, sizeof from, "%s\\%s", list, name);
        else       snprintf(from, sizeof from, "%s", list);

        snprintf(base, sizeof base, "%s", name);
        dot = strrchr(base, '.');
        if (dot) *dot = '\0';
        snprintf(to, sizeof to, "%s%s", dir, name);
        for (i = 2; GetFileAttributesA(to) != INVALID_FILE_ATTRIBUTES; i++)
            snprintf(to, sizeof to, "%s%s %d%s", dir, base, i, ext ? ext : "");

        if (CopyFileA(from, to, TRUE)) {
            const char *slash = strrchr(to, '\\');
            snprintf(last, sizeof last, "%s", slash ? slash + 1 : to);
        } else {
            failed++;
        }
        if (!multi) break;
    }

    rescan_files();
    if (last[0]) {
        select_by_name(last);
        open_file((int)SendMessageA(hwndFiles, LB_GETCURSEL, 0, 0));
    }
    if (failed)
        MessageBoxA(hwnd, "Some files could not be imported.\n"
                          "They may be locked, or the project folder may be read-only.",
                    "Import", MB_OK | MB_ICONWARNING);
}

/* ═══════════════════════════════════════════════ new project ══ */

static const char FIRST_ADDA[] =
    "# first.adda - your program starts here.\n"
    "[name] = ask What is your name?\n"
    "print Hello, [name]\n";

static const char STYLE_ADDA[] =
    "# style.adda - a second file for this project.\n";

static BOOL write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    BOOL ok;
    if (!f) return FALSE;
    ok = fputs(text, f) >= 0;
    return (fclose(f) == 0) && ok;
}

/* Asks where to save the new project, makes a folder of that name there with
 * first.adda and style.adda in it, and opens it as the project. */
static void new_project(HWND hwnd)
{
    OPENFILENAMEA ofn;
    char path[MAX_PATH] = "My Project", file[MAX_PATH * 2];
    BOOL ok;

    ZeroMemory(&ofn, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "Project folder\0*.\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = sizeof path;
    ofn.lpstrTitle = "New Project - choose where to save it and name it";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameA(&ofn)) return;

    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
        MessageBoxA(hwnd, "There is already something with that name there.\n"
                          "Pick another name for the project.",
                    "New Project", MB_OK | MB_ICONWARNING);
        return;
    }
    if (!CreateDirectoryA(path, NULL)) {
        MessageBoxA(hwnd, "Windows would not create the project folder there.",
                    "New Project", MB_OK | MB_ICONWARNING);
        return;
    }

    snprintf(file, sizeof file, "%s\\first.adda", path);
    ok = write_text(file, FIRST_ADDA);
    snprintf(file, sizeof file, "%s\\style.adda", path);
    ok = write_text(file, STYLE_ADDA) && ok;
    if (!ok)
        MessageBoxA(hwnd, "Windows would not write the project's files.",
                    "New Project", MB_OK | MB_ICONWARNING);

    open_project(hwnd, path);
}

static int ab_hit(POINT p)
{
    int i;
    for (i = 0; i < AB_COUNT; i++)
        if (contains(g_abRect[i], p)) return i;
    return -1;
}

static void ab_click(HWND hwnd, int item)
{
    switch (item) {
    case AB_EXPLORER:
    case AB_SEARCH:
        g_view = (g_view == item) ? -1 : item;   /* click again to collapse */
        if (g_view == AB_EXPLORER) rescan_files();
        layout(hwnd);
        InvalidateRect(hwnd, NULL, TRUE);
        if (g_view == AB_SEARCH) SetFocus(hwndFind);
        break;
    case AB_NEW:
        new_project(hwnd);
        break;
    case AB_RUN:
        if (!g_running) run_code(hwnd);
        break;
    case AB_STOP:
        g_runAll = FALSE;               /* Stop ends Run All Files too */
        if (g_running) {
            TerminateProcess(g_pi.hProcess, 1);
            finish_run(hwnd, "\r\n[stopped]\r\n");
        }
        break;
    case AB_TIDY:
        toggle_tidy(hwnd);
        break;
    case AB_CHECK:
        check_code();
        break;
    case AB_CHEAT:
        open_cheats(hwnd);
        break;
    case AB_GEAR:
        open_settings(hwnd);
        break;
    default: break;
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {

    case WM_CREATE: {
        HDC hdc = GetDC(hwnd);
        HINSTANCE inst = ((CREATESTRUCTA *)lParam)->hInstance;

        g_dpi = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(hwnd, hdc);

        hwndMain = hwnd;
        g_curNS = LoadCursorA(NULL, IDC_SIZENS);
        load_pick();
        load_tidy();
        g_t = theme_for(g_pick);
        hBrushBg      = CreateSolidBrush(g_t.bg);
        hBrushSurface = CreateSolidBrush(g_t.surface);
        hBrushAb      = CreateSolidBrush(g_t.abBg);
        build_fonts();

        hwndCode = CreateWindowExA(0, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | ES_NOHIDESEL,
            0, 0, 0, 0, hwnd, (HMENU)(UINT_PTR)ID_CODE, inst, NULL);
        SetWindowSubclass(hwndCode, CodeProc, 0, 0);

        hwndConsole = CreateWindowExA(0, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL,
            0, 0, 0, 0, hwnd, (HMENU)(UINT_PTR)ID_CONSOLE, inst, NULL);

        /* no WS_BORDER: the system frame ignores the palette, so paint_main
         * draws one in the theme colour instead */
        hwndFind = CreateWindowExA(0, "EDIT", "",
            WS_CHILD | ES_AUTOHSCROLL,
            0, 0, 0, 0, hwnd, (HMENU)(UINT_PTR)ID_FIND, inst, NULL);

        hwndFiles = CreateWindowExA(0, "LISTBOX", "",
            WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS,
            0, 0, 0, 0, hwnd, (HMENU)(UINT_PTR)ID_FILES, inst, NULL);

        SendMessage(hwndCode,    WM_SETFONT, (WPARAM)hFontMono, TRUE);
        SendMessage(hwndConsole, WM_SETFONT, (WPARAM)hFontMono, TRUE);
        SendMessage(hwndFind,    WM_SETFONT, (WPARAM)hFontUI, TRUE);
        SendMessage(hwndFiles,   WM_SETFONT, (WPARAM)hFontUI, TRUE);

        /* the EDIT default of 30000 characters is easy to hit in a console */
        SendMessageA(hwndConsole, EM_SETLIMITTEXT, 0, 0);
        SendMessageA(hwndCode,    EM_SETLIMITTEXT, 0, 0);

        SetWindowSubclass(hwndConsole, console_proc, ID_CONSOLE, 0);

        apply_titlebar(hwnd);
        theme_edit(hwndCode);
        theme_edit(hwndConsole);
        theme_edit(hwndFind);
        theme_edit(hwndFiles);

        rescan_files();

        g_loading = TRUE;                /* the starter is not anyone's typing */
        SetWindowTextA(hwndCode,
            "[name] = ask What is your name?\r\n"
            "print Hello, [name]");
        g_loading = FALSE;

        /* carry on with the project that was open last time, if it is still there */
        {
            char last[MAX_PATH + 2];
            recall_project(last, sizeof last);
            if (last[0]) open_project(hwnd, last);
        }
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, g_t.muted);
        SetBkColor(hdc, g_t.bg);
        return (LRESULT)hBrushBg;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        HWND from = (HWND)lParam;
        SetTextColor(hdc, g_t.text);
        if (from == hwndFind) {
            SetBkColor(hdc, g_t.bg);
            return (LRESULT)hBrushBg;
        }
        SetBkColor(hdc, g_t.surface);
        return (LRESULT)hBrushSurface;
    }
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, g_t.text);
        SetBkColor(hdc, g_t.bg);
        return (LRESULT)hBrushBg;
    }

    case WM_ERASEBKGND:
        return 1;                      /* WM_PAINT covers everything */

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        paint_main(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    /* ── activity bar and splitter mousing ───────────────────────── */
    case WM_MOUSEMOVE: {
        POINT p;
        int hit;
        TRACKMOUSEEVENT tme;

        p.x = GET_X_LPARAM(lParam);
        p.y = GET_Y_LPARAM(lParam);

        if (g_dragging) {
            RECT rc;
            int toolH = S(12), splitH = S(7), track, codeH, minPane = S(60);
            GetClientRect(hwnd, &rc);
            track = rc.bottom - toolH - splitH;
            if (track >= 2 * minPane) {
                codeH = (p.y - g_dragDY) - toolH;
                if (codeH < minPane)          codeH = minPane;
                if (codeH > track - minPane)  codeH = track - minPane;
                g_split = (double)codeH / (double)track;
                layout(hwnd);
                UpdateWindow(hwnd);      /* redraw now, not at the next idle */
            }
            return 0;
        }

        {
            int bar = bar_hit(p);
            if (bar != g_barHot) {
                g_barHot = bar;
                start_anim(hwnd);
            }
        }

        hit = ab_hit(p);
        if (hit != g_abHot) {
            g_abHot = hit;
            start_anim(hwnd);
        }
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        tme.dwHoverTime = 0;
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        g_abHot = -1;
        g_barHot = -1;
        start_anim(hwnd);
        return 0;

    case WM_CONTEXTMENU:
        if ((HWND)wParam == hwndFiles) {
            file_menu(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        }
        if ((HWND)wParam == hwnd && lParam != -1) {     /* a right-click on Run */
            POINT p;
            p.x = GET_X_LPARAM(lParam);
            p.y = GET_Y_LPARAM(lParam);
            ScreenToClient(hwnd, &p);
            if (ab_hit(p) == AB_RUN) {
                run_menu(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                return 0;
            }
        }
        break;

    case WM_RUN_NEXT:
        run_next(hwnd);
        return 0;

    case WM_TIDY:
        tidy_update();
        /* the EDIT redraws a line it has just changed in its one colour, not
         * through WM_PAINT, so the colours go back on here too */
        if (hwndCode) paint_colours(hwndCode);
        return 0;

    case WM_SETCURSOR: {
        POINT p;
        RECT hit;

        if (g_dragging) { SetCursor(g_curNS); return TRUE; }

        /* Only claim the cursor for our OWN client area. A child EDIT forwards
         * this message up with wParam still holding the child's handle; if the
         * parent returned TRUE there, the child would never get to set its
         * I-beam and the text cursor would vanish. */
        if ((HWND)wParam != hwnd || LOWORD(lParam) != HTCLIENT) break;

        GetCursorPos(&p);
        ScreenToClient(hwnd, &p);
        hit = g_splitRect;
        InflateRect(&hit, 0, S(3));          /* generous to grab, thin to look at */
        if (contains(hit, p)) { SetCursor(g_curNS); return TRUE; }
        break;
    }

    case WM_LBUTTONDOWN: {
        POINT p;
        RECT hit;
        int at;

        p.x = GET_X_LPARAM(lParam);
        p.y = GET_Y_LPARAM(lParam);

        hit = g_splitRect;
        InflateRect(&hit, 0, S(3));
        if (contains(hit, p)) {
            g_dragging = TRUE;
            g_dragDY = p.y - g_splitRect.top;
            SetCapture(hwnd);
            return 0;
        }
        at = bar_hit(p);
        if (at >= 0) {
            g_barPress = at;
            g_barPressAt = GetTickCount();
            start_anim(hwnd);
            InvalidateRect(hwnd, &g_barRect[at], FALSE);
            UpdateWindow(hwnd);
            if (at == BAR_SAVE) save_as(hwnd); else import_menu(hwnd);
            return 0;
        }
        at = ab_hit(p);
        if (at >= 0) {
            g_abPress = at;
            g_abPressAt = GetTickCount();
            start_anim(hwnd);
            ab_click(hwnd, at);
            InvalidateRect(hwnd, &g_abRect[at], FALSE);
            return 0;
        }
        break;
    }

    case WM_LBUTTONDBLCLK: {
        POINT p;
        p.x = GET_X_LPARAM(lParam);
        p.y = GET_Y_LPARAM(lParam);
        if (contains(g_splitRect, p)) {      /* back to even */
            g_split = 0.5;
            layout(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP:
        if (g_dragging) ReleaseCapture();    /* the flag is cleared below */
        return 0;

    /* Covers ReleaseCapture, Alt+Tab, a message box - anything that takes the
     * mouse away mid-drag. Clearing the flag only in WM_LBUTTONUP leaves the
     * splitter stuck to the pointer. */
    case WM_CAPTURECHANGED:
        g_dragging = FALSE;
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lParam;
        mm->ptMinTrackSize.x = S(560);
        mm->ptMinTrackSize.y = S(360);
        return 0;
    }

    /* ── output while the program runs ───────────────────────────── */
    case WM_TIMER:
        if (wParam == ID_ANIM) {
            DWORD now = GetTickCount();
            double step = (double)(now - g_animTick) / HOVER_MS;
            BOOL moving = FALSE;
            RECT strip;
            int i;

            g_animTick = now;
            for (i = 0; i < AB_COUNT; i++) {
                BOOL disabled = (i == AB_RUN && g_running) || (i == AB_STOP && !g_running);
                moving |= approach(&g_abGlow[i], g_abHot == i && !disabled, step);
            }
            for (i = 0; i < BAR_COUNT; i++)
                moving |= approach(&g_barGlow[i], g_barHot == i, step);
            if (press_amount(g_abPressAt) > 0 || press_amount(g_barPressAt) > 0)
                moving = TRUE;

            /* only the strip and the Explorer's buttons move, so do not make
             * the whole window, editor and all, repaint every frame */
            GetClientRect(hwnd, &strip);
            strip.right = S(48);
            InvalidateRect(hwnd, &strip, FALSE);
            for (i = 0; i < BAR_COUNT; i++)
                InvalidateRect(hwnd, &g_barRect[i], FALSE);

            if (!moving) {
                KillTimer(hwnd, ID_ANIM);
                g_animOn = FALSE;
                g_abPress = -1;
                g_barPress = -1;
            }
            return 0;
        }
        if (wParam == ID_POLL && g_running) {
            pump_stdin();          /* whatever the pipe would not take last time */
            drain(g_out);
            drain(g_err);
            if (WaitForSingleObject(g_pi.hProcess, 0) == WAIT_OBJECT_0)
                finish_run(hwnd, "\r\n");
        }
        return 0;

    case WM_SIZE:
        layout(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_DPICHANGED: {
        RECT *r = (RECT *)lParam;
        g_dpi = HIWORD(wParam);
        build_fonts();
        SendMessage(hwndCode,    WM_SETFONT, (WPARAM)hFontMono, TRUE);
        SendMessage(hwndConsole, WM_SETFONT, (WPARAM)hFontMono, TRUE);
        SendMessage(hwndFind,    WM_SETFONT, (WPARAM)hFontUI, TRUE);
        SendMessage(hwndFiles,   WM_SETFONT, (WPARAM)hFontUI, TRUE);
        SetWindowPos(hwnd, NULL, r->left, r->top,
                     r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        layout(hwnd);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_FILES:
            /* selection change alone - LBN_DBLCLK would load it a second time */
            if (HIWORD(wParam) == LBN_SELCHANGE)
                open_file((int)SendMessageA(hwndFiles, LB_GETCURSEL, 0, 0));
            return 0;
        case ID_FIND:
            if (HIWORD(wParam) == EN_CHANGE) find_in_code(FALSE);
            return 0;
        case ID_CODE:
            /* a new or removed line renumbers everything below it */
            if (HIWORD(wParam) == EN_CHANGE && !g_loading) {
                g_dirty = TRUE;              /* belongs to the open file now */
                PostMessageA(hwnd, WM_TIDY, 0, 0);
            }
            if (HIWORD(wParam) == EN_CHANGE && g_chkCount) {
                g_chkCount = 0;              /* the marks no longer line up */
                InvalidateRect(hwndCode, NULL, FALSE);
            }
            if (HIWORD(wParam) == EN_CHANGE || HIWORD(wParam) == EN_VSCROLL) {
                RECT g;
                GetClientRect(hwndCode, &g);
                g.right = GUTTER_W;
                InvalidateRect(hwndCode, &g, FALSE);
            }
            return 0;
        }
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_F11) { toggle_fullscreen(hwnd); return 0; }
        break;

    case WM_SETTINGCHANGE:
        if (g_pick == PICK_SYSTEM && lParam &&
            strcmp((const char *)lParam, "ImmersiveColorSet") == 0)
            apply_theme(hwnd);
        return 0;

    case WM_DESTROY:
        save_current();
        g_runAll = FALSE;
        if (g_running) {
            TerminateProcess(g_pi.hProcess, 1);
            finish_run(hwnd, NULL);
        }
        DeleteObject(hBrushBg);
        DeleteObject(hBrushSurface);
        DeleteObject(hBrushAb);
        DeleteObject(hFontMono);
        DeleteObject(hFontUI);
        DeleteObject(hFontUIBold);
        DeleteObject(hFontSmall);
        DeleteObject(hFontTitle);
        DeleteObject(hFontBody);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

/* ══════════════════════════════════════════════════ entry point ══ */

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int cmdShow)
{
    WNDCLASSA wc;
    HWND hwnd;
    MSG msg;
    INITCOMMONCONTROLSEX icc;

    (void)hPrev; (void)cmdLine;

    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpszClassName = "AddaGUI";
    RegisterClassA(&wc);

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = SettingsProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = "AddaSettings";
    RegisterClassA(&wc);

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = CheatsProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = "AddaCheats";
    RegisterClassA(&wc);

    hwnd = CreateWindowExA(
        0, "AddaGUI", "Adda",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1000, 760,
        NULL, NULL, hInst, NULL);

    ShowWindow(hwnd, cmdShow);
    UpdateWindow(hwnd);

    while (GetMessageA(&msg, NULL, 0, 0)) {
        /* Esc closes whichever popup has focus - or, on a cheat sheet entry's
         * own page, goes back to the list first */
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            HWND top = GetAncestor(msg.hwnd, GA_ROOT);
            if (top == hwndCheats && g_cheatOpen) {
                show_cheat(NULL);
                continue;
            }
            if (top == hwndCheats || top == hwndSettings) {
                DestroyWindow(top);
                continue;
            }
        }
        /* Enter in the cheat search or list opens the selected row */
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN &&
            (msg.hwnd == hwndCheatFind || msg.hwnd == hwndCheatList)) {
            open_cheat((int)SendMessageA(hwndCheatList, LB_GETCURSEL, 0, 0));
            continue;
        }
        /* F2 and Delete act on the file list */
        if (msg.message == WM_KEYDOWN && msg.hwnd == hwndFiles) {
            if (msg.wParam == VK_F2)     { begin_rename();     continue; }
            if (msg.wParam == VK_DELETE) { delete_selected();   continue; }
        }
        /* Enter in the search box jumps to the next match */
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN &&
            msg.hwnd == hwndFind) {
            find_in_code(TRUE);
            continue;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_F11) {
            SendMessageA(hwnd, WM_KEYDOWN, VK_F11, 0);
            continue;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN
            && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (GetKeyState(VK_SHIFT) & 0x8000) run_all_files(hwnd);
            else run_code(hwnd);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return (int)msg.wParam;
}
