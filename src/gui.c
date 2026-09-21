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
#include <shellapi.h>     /* SHFileOperation, so deleting goes to the bin */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gui_cheatsheet.h"

/* ── ids ─────────────────────────────────────────────────────────── */
#define ID_CODE      1001
#define ID_CONSOLE   1002
#define ID_FIND      1005
#define ID_FILES     1006
#define ID_POLL      1           /* timer */

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
static int    g_dpi = 96;

/* ── activity bar ────────────────────────────────────────────────── */
enum { ICON_EXPLORER, ICON_SEARCH, ICON_PLAY, ICON_STOP, ICON_CHEAT, ICON_GEAR };
/* Run and Stop sit under Search; Cheat sheet and Settings are pinned to the
 * bottom. Everything before AB_CHEAT stacks from the top. */
enum { AB_EXPLORER = 0, AB_SEARCH, AB_RUN, AB_STOP, AB_CHEAT, AB_GEAR, AB_COUNT };

static const int AB_ICON[AB_COUNT] = {
    ICON_EXPLORER, ICON_SEARCH, ICON_PLAY, ICON_STOP, ICON_CHEAT, ICON_GEAR
};

static int  g_view = AB_EXPLORER;   /* which panel view, or -1 when collapsed */
static int  g_abHot = -1;           /* activity item under the pointer */
static RECT g_abRect[AB_COUNT];

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

/* ── cheat sheet filtering ───────────────────────────────────────── */
static int g_cheatShown[CHEAT_COUNT];
static int g_cheatCount;

#define S(x) MulDiv((x), g_dpi, 96)

static void apply_theme(HWND hwnd);
static void layout(HWND hwnd);
static void open_settings(HWND owner);
static void open_cheats(HWND owner);
static void refresh_cheats(void);
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

static void frame_rect(HDC hdc, RECT r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    FrameRect(hdc, &r, b);
    DeleteObject(b);
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

static void exe_dir(char *buf, int size)
{
    char *sep;

    GetModuleFileNameA(NULL, buf, size);
    sep = strrchr(buf, '\\');
    if (sep) *(sep + 1) = '\0';
    else     buf[0] = '\0';
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
}

static void run_code(HWND hwnd)
{
    char tmp_dir[MAX_PATH];
    char adda_exe[MAX_PATH];
    char cmd[MAX_PATH * 4];
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOA si;
    HANDLE hWriteOut = NULL, hWriteErr = NULL, hReadIn = NULL;
    char *code;
    int len;
    FILE *f;
    BOOL ok;

    if (g_running) return;

    len = GetWindowTextLengthA(hwndCode);
    code = (char *)malloc((size_t)len + 2);
    if (!code) return;
    GetWindowTextA(hwndCode, code, len + 1);

    GetTempPathA(MAX_PATH, tmp_dir);
    snprintf(g_tmp_file, sizeof(g_tmp_file), "%s_adda_gui.adda", tmp_dir);

    f = fopen(g_tmp_file, "w");
    if (!f) { free(code); return; }
    fputs(code, f);
    fclose(f);
    free(code);

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
        return;
    }

    SetWindowTextA(hwndConsole, "");
    g_prev_byte = '\0';
    g_anchor = 0;
    set_running(TRUE);
    SetFocus(hwndConsole);

    /* Poll rather than wait: waiting for the process first deadlocks as soon
     * as it writes more than the pipe will hold, because nothing drains it. */
    SetTimer(hwnd, ID_POLL, 50, NULL);
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

static void rescan_files(void)
{
    char dir[MAX_PATH], sub[MAX_PATH * 2];
    int i;

    g_fileCount = 0;
    exe_dir(dir, sizeof(dir));
    scan_dir(dir);

    snprintf(sub, sizeof(sub), "%sexamples\\", dir);
    scan_dir(sub);

    /* ..\examples too, since the exe usually sits in the repo root */
    snprintf(sub, sizeof(sub), "%s..\\examples\\", dir);
    scan_dir(sub);

    if (!hwndFiles) return;
    SendMessageA(hwndFiles, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < g_fileCount; i++)
        SendMessageA(hwndFiles, LB_ADDSTRING, 0, (LPARAM)g_files[i].name);
}

static void open_file(int index)
{
    FILE *f;
    long size;
    char *buf;

    if (index < 0 || index >= g_fileCount) return;

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
            SetWindowTextA(hwndCode, buf);
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
        if (i < AB_CHEAT) { r->top = top; top += S(44) + (i == AB_SEARCH ? S(6) : 0); }
        else              { r->top = bottom; bottom += S(44); }
        r->bottom = r->top + S(44);
    }
    /* the two bottom items were laid out downwards; push them to the bottom */
    {
        int shift = rc.bottom - S(8) - g_abRect[AB_GEAR].bottom;
        g_abRect[AB_CHEAT].top += shift; g_abRect[AB_CHEAT].bottom += shift;
        g_abRect[AB_GEAR].top  += shift; g_abRect[AB_GEAR].bottom  += shift;
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
        dwp = move_child(dwp, hwndFiles, abW + S(8), S(40),
                         panelW - S(16), rc.bottom - S(48), SWP_SHOWWINDOW);
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

    {
        RECT r;
        GetClientRect(hwndCode, &r);
        InflateRect(&r, -S(8), -S(6));
        SendMessageA(hwndCode, EM_SETRECT, 0, (LPARAM)&r);
        GetClientRect(hwndConsole, &r);
        InflateRect(&r, -S(8), -S(6));
        SendMessageA(hwndConsole, EM_SETRECT, 0, (LPARAM)&r);
    }
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
        RECT icon;
        BOOL active = (i == g_view);
        /* Run only makes sense while idle, Stop only while running */
        BOOL disabled = (i == AB_RUN && g_running) || (i == AB_STOP && !g_running);
        BOOL hot = (g_abHot == i) && !disabled;
        COLORREF cell = hot ? g_t.abHover : g_t.abBg;
        COLORREF fg = (active || hot) ? g_t.abIcon : g_t.abIconDim;

        if (disabled)                    /* halfway between dim and the bar */
            fg = RGB((GetRValue(g_t.abIconDim) + GetRValue(g_t.abBg)) / 2,
                     (GetGValue(g_t.abIconDim) + GetGValue(g_t.abBg)) / 2,
                     (GetBValue(g_t.abIconDim) + GetBValue(g_t.abBg)) / 2);
        else if (i == AB_STOP)           /* something is running: make it obvious */
            fg = g_t.abIcon;

        if (i == AB_RUN) {               /* a hairline between views and actions */
            RECT sep = box;
            sep.left += S(12); sep.right -= S(12);
            sep.top -= S(3); sep.bottom = sep.top + 1;
            fill_rect(hdc, sep, g_t.abIconDim);
        }

        if (hot) fill_rect(hdc, box, cell);

        if (active) {
            RECT bar = box;
            bar.right = bar.left + S(2);
            fill_rect(hdc, bar, g_t.abIcon);
        }

        icon = box;
        InflateRect(&icon, -S(12), -S(12));
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
        text_at(hdc, title,
                (g_view == AB_EXPLORER) ? "EXPLORER" : "SEARCH",
                hFontSmall, g_t.muted, DT_LEFT | DT_SINGLELINE);

        edge = g_panelRect;
        edge.left = edge.right - 1;
        fill_rect(hdc, edge, g_t.border);

        if (g_view == AB_SEARCH) {          /* frame for the borderless find box */
            RECT fr;
            GetWindowRect(hwndFind, &fr);
            MapWindowPoints(NULL, hwnd, (POINT *)&fr, 2);
            InflateRect(&fr, 1, 1);
            frame_rect(hdc, fr, g_t.border);
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
        frame_rect(hdc, cr, g_t.border);

        GetWindowRect(hwndConsole, &cr);
        MapWindowPoints(NULL, hwnd, (POINT *)&cr, 2);
        InflateRect(&cr, 1, 1);
        frame_rect(hdc, cr, g_t.border);
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
    item.bottom = item.top + S(28);
    fill_rect(hdc, item, g_t.sel);
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

        if (i == g_pick)        fill_rect(hdc, row, g_t.sel);
        else if (i == g_setRowHot) fill_rect(hdc, row, g_t.ghostHot);

        /* three chips that say what the theme looks like */
        chips[0] = t.abBg;
        chips[1] = t.surface;
        chips[2] = t.accent;
        for (j = 0; j < 3; j++) {
            sw.left = row.left + S(10) + j * S(16);
            sw.right = sw.left + S(14);
            sw.top = row.top + S(18);
            sw.bottom = sw.top + S(16);
            fill_rect(hdc, sw, chips[j]);
            frame_rect(hdc, sw, g_t.border);
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
        if (g_setRowHot != was) InvalidateRect(hwnd, NULL, FALSE);

        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        tme.dwHoverTime = 0;
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (g_setRowHot != -1) { g_setRowHot = -1; InvalidateRect(hwnd, NULL, FALSE); }
        return 0;

    case WM_LBUTTONDOWN: {
        POINT p;
        int row;
        p.x = GET_X_LPARAM(l); p.y = GET_Y_LPARAM(l);
        row = settings_row_at(hwnd, p);
        if (row >= 0) {
            g_pick = row;
            save_pick();
            apply_theme(hwndMain);
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;
    }

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
    for (i = 0; i < CHEAT_COUNT; i++) {
        if (!needle[0] ||
            has_text(CHEATS[i].title, needle) ||
            has_text(CHEATS[i].about, needle) ||
            has_text(CHEATS[i].snippet, needle))
            g_cheatShown[g_cheatCount++] = i;
    }

    if (!hwndCheatList) return;
    SendMessageA(hwndCheatList, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < g_cheatCount; i++)
        SendMessageA(hwndCheatList, LB_ADDSTRING, 0,
                     (LPARAM)CHEATS[g_cheatShown[i]].title);
    if (g_cheatCount) SendMessageA(hwndCheatList, LB_SETCURSEL, 0, 0);
}

static void insert_cheat(int shownIndex)
{
    if (shownIndex < 0 || shownIndex >= g_cheatCount) return;
    SendMessageA(hwndCode, EM_REPLACESEL, TRUE,
                 (LPARAM)CHEATS[g_cheatShown[shownIndex]].snippet);
    SetFocus(hwndCode);
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
    c = &CHEATS[g_cheatShown[di->itemID]];

    fill_rect(di->hDC, r, selected ? g_t.sel : g_t.surface);

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

    hint = rc;
    hint.left += S(14);
    hint.right -= S(14);
    hint.bottom -= S(8);
    hint.top = hint.bottom - S(18);
    text_at(hdc, hint,
            "Double-click or press Enter to put it in your code",
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
        if (LOWORD(w) == ID_CHEATLIST && HIWORD(w) == LBN_DBLCLK) {
            insert_cheat((int)SendMessageA(hwndCheatList, LB_GETCURSEL, 0, 0));
            return 0;
        }
        break;

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
    case AB_RUN:
        if (!g_running) run_code(hwnd);
        break;
    case AB_STOP:
        if (g_running) {
            TerminateProcess(g_pi.hProcess, 1);
            finish_run(hwnd, "\r\n[stopped]\r\n");
        }
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
        g_t = theme_for(g_pick);
        hBrushBg      = CreateSolidBrush(g_t.bg);
        hBrushSurface = CreateSolidBrush(g_t.surface);
        hBrushAb      = CreateSolidBrush(g_t.abBg);
        build_fonts();

        hwndCode = CreateWindowExA(0, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | ES_NOHIDESEL,
            0, 0, 0, 0, hwnd, (HMENU)(UINT_PTR)ID_CODE, inst, NULL);

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

        SetWindowTextA(hwndCode,
            "name = ask What is your name?\r\n"
            "print Hello, {name}");
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

        hit = ab_hit(p);
        if (hit != g_abHot) {
            RECT strip;
            g_abHot = hit;
            /* only the strip changed, so do not make the whole window,
             * icons and all, repaint on every hover */
            GetClientRect(hwnd, &strip);
            strip.right = S(48);
            InvalidateRect(hwnd, &strip, FALSE);
        }
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        tme.dwHoverTime = 0;
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (g_abHot != -1) { g_abHot = -1; InvalidateRect(hwnd, NULL, FALSE); }
        return 0;

    case WM_CONTEXTMENU:
        if ((HWND)wParam == hwndFiles) {
            file_menu(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        }
        break;

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
        at = ab_hit(p);
        if (at >= 0) { ab_click(hwnd, at); return 0; }
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
        /* Esc closes whichever popup has focus */
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            HWND top = GetAncestor(msg.hwnd, GA_ROOT);
            if (top == hwndCheats || top == hwndSettings) {
                DestroyWindow(top);
                continue;
            }
        }
        /* Enter in the cheat search or list inserts the selected snippet */
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN &&
            (msg.hwnd == hwndCheatFind || msg.hwnd == hwndCheatList)) {
            insert_cheat((int)SendMessageA(hwndCheatList, LB_GETCURSEL, 0, 0));
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
            run_code(hwnd);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return (int)msg.wParam;
}
