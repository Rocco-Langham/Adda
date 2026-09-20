/*
 *  Adda GUI — a small Win32 front-end for the Adda interpreter.
 *
 *  Build:  see build.sh (the manifest in adda-gui.rc is what gives themed
 *          controls and DPI awareness)
 *  Place adda-gui.exe next to adda.exe and double-click to launch.
 *
 *  The program runs in the background while the window stays responsive: a
 *  timer drains its output pipes every 50ms and appends whatever has arrived.
 *  That is what lets `ask` work — the prompt appears, you type an answer into
 *  the input line, and it goes down the pipe to the waiting program.
 *
 *  The look is drawn by hand rather than left to the default controls: flat
 *  panels with hairline borders, owner-drawn buttons with a hover state, and
 *  a palette chosen from the cog in the top right.
 */

#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── control IDs ─────────────────────────────────────────────────── */
#define ID_CODE      1001
#define ID_OUTPUT    1002
#define ID_RUN       1003
#define ID_LBL_CODE  1004
#define ID_LBL_OUT   1005
#define ID_INPUT     1006
#define ID_SEND      1007
#define ID_STOP      1008
#define ID_LBL_IN    1009
#define ID_COG       1010
#define ID_POLL      1           /* timer */

/* cog menu */
#define IDM_SYSTEM   2001
#define IDM_LIGHT    2002
#define IDM_DARK     2003
#define IDM_BEIGE    2004
#define IDM_FULL     2010

/* ── themes ──────────────────────────────────────────────────────── */
typedef struct {
    COLORREF bg;          /* window behind everything          */
    COLORREF surface;     /* code / output / input panels      */
    COLORREF border;      /* hairline around the panels        */
    COLORREF text;        /* code and output                   */
    COLORREF muted;       /* section labels                    */
    COLORREF accent;      /* the Run button                    */
    COLORREF accentHot;
    COLORREF accentDown;
    COLORREF onAccent;    /* text on the accent                */
    COLORREF ghostHot;    /* hover fill for outlined buttons   */
    BOOL     dark;
} Theme;

enum { PICK_SYSTEM = 0, PICK_LIGHT, PICK_DARK, PICK_BEIGE };

static const Theme THEME_LIGHT_V = {
    RGB(243,243,243), RGB(255,255,255), RGB(216,216,216),
    RGB(26,26,26),    RGB(97,97,97),
    RGB(0,103,192),   RGB(25,117,197), RGB(0,88,158), RGB(255,255,255),
    RGB(232,232,232), FALSE
};

static const Theme THEME_DARK_V = {
    RGB(32,32,32),    RGB(43,43,43),   RGB(61,61,61),
    RGB(232,232,232), RGB(160,160,160),
    RGB(0,120,212),   RGB(26,140,232), RGB(0,95,168), RGB(255,255,255),
    RGB(58,58,58),    TRUE
};

/* the original beige, given flat surfaces and a warmer accent */
static const Theme THEME_BEIGE_V = {
    RGB(236,232,208), RGB(245,245,220), RGB(206,200,172),
    RGB(30,30,30),    RGB(110,104,80),
    RGB(140,94,42),   RGB(160,110,52), RGB(118,78,34), RGB(255,255,255),
    RGB(226,220,192), FALSE
};

static int   g_pick = PICK_SYSTEM;
static Theme g_t;

/* ── globals ─────────────────────────────────────────────────────── */
static HWND    hwndCode, hwndOutput, hwndRun, hwndStop;
static HWND    hwndInput, hwndSend, hwndCog;
static HWND    hwndLblCode, hwndLblOut, hwndLblIn;
static HBRUSH  hBrushBg, hBrushSurface;
static HFONT   hFontMono, hFontUI, hFontIcon;
static HWND    g_hot;            /* button the pointer is over */
static int     g_dpi = 96;

/* fullscreen state */
static BOOL            g_fullscreen = FALSE;
static WINDOWPLACEMENT g_wp;

/* the program currently running, if any */
static PROCESS_INFORMATION g_pi;
static HANDLE  g_out = NULL, g_err = NULL, g_in = NULL;
static BOOL    g_running = FALSE;
static char    g_tmp_file[MAX_PATH * 2];

/* U+E713, the settings gear in Segoe MDL2 Assets. Written as a code unit so
 * the source stays plain ASCII. */
static const WCHAR COG_GLYPH[2] = { 0xE713, 0 };

#define S(x) MulDiv((x), g_dpi, 96)

/* ── theme plumbing ──────────────────────────────────────────────── */
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
    if (v > PICK_BEIGE) v = PICK_SYSTEM;
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

static void resolve_theme(void)
{
    switch (g_pick) {
        case PICK_LIGHT: g_t = THEME_LIGHT_V; break;
        case PICK_DARK:  g_t = THEME_DARK_V;  break;
        case PICK_BEIGE: g_t = THEME_BEIGE_V; break;
        default:         g_t = system_is_dark() ? THEME_DARK_V : THEME_LIGHT_V;
    }
}

/* Dark title bar. Attribute 20 on Windows 10 2004 and later, 19 before it. */
static void apply_titlebar(HWND hwnd)
{
    BOOL dark = g_t.dark;

    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark))))
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
}

/* Ask for the dark scrollbars and selection that go with a dark panel. */
static void theme_edit(HWND h)
{
    SetWindowTheme(h, g_t.dark ? L"DarkMode_Explorer" : L"Explorer", NULL);
}

static void apply_theme(HWND hwnd)
{
    resolve_theme();

    if (hBrushBg)      DeleteObject(hBrushBg);
    if (hBrushSurface) DeleteObject(hBrushSurface);
    hBrushBg      = CreateSolidBrush(g_t.bg);
    hBrushSurface = CreateSolidBrush(g_t.surface);

    apply_titlebar(hwnd);
    theme_edit(hwndCode);
    theme_edit(hwndOutput);
    theme_edit(hwndInput);

    InvalidateRect(hwnd, NULL, TRUE);
    RedrawWindow(hwnd, NULL, NULL,
                 RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_ERASE);
}

/* ── fonts ───────────────────────────────────────────────────────── */
static HFONT make_font(int points, int weight, const char *face)
{
    return CreateFontA(-MulDiv(points, g_dpi, 72), 0, 0, 0, weight,
                       FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

static void build_fonts(void)
{
    if (hFontMono) DeleteObject(hFontMono);
    if (hFontUI)   DeleteObject(hFontUI);
    if (hFontIcon) DeleteObject(hFontIcon);

    hFontMono = make_font(11, FW_NORMAL,   "Consolas");
    hFontUI   = make_font(9,  FW_SEMIBOLD, "Segoe UI");
    hFontIcon = make_font(11, FW_NORMAL,   "Segoe MDL2 Assets");
}

static void apply_fonts(void)
{
    SendMessage(hwndCode,    WM_SETFONT, (WPARAM)hFontMono, TRUE);
    SendMessage(hwndOutput,  WM_SETFONT, (WPARAM)hFontMono, TRUE);
    SendMessage(hwndInput,   WM_SETFONT, (WPARAM)hFontMono, TRUE);
    SendMessage(hwndRun,     WM_SETFONT, (WPARAM)hFontUI,   TRUE);
    SendMessage(hwndStop,    WM_SETFONT, (WPARAM)hFontUI,   TRUE);
    SendMessage(hwndSend,    WM_SETFONT, (WPARAM)hFontUI,   TRUE);
    SendMessage(hwndLblCode, WM_SETFONT, (WPARAM)hFontUI,   TRUE);
    SendMessage(hwndLblOut,  WM_SETFONT, (WPARAM)hFontUI,   TRUE);
    SendMessage(hwndLblIn,   WM_SETFONT, (WPARAM)hFontUI,   TRUE);
    SendMessage(hwndCog,     WM_SETFONT, (WPARAM)hFontIcon, TRUE);
}

/* ── find adda.exe next to this executable ───────────────────────── */
static void get_adda_path(char *buf, int size)
{
    char *sep;

    GetModuleFileNameA(NULL, buf, size);
    sep = strrchr(buf, '\\');
    if (sep) *(sep + 1) = '\0';
    else     buf[0] = '\0';
    strncat(buf, "adda.exe", size - (int)strlen(buf) - 1);
}

/* ── appending to the output box ─────────────────────────────────── */
static void append_text(const char *text)
{
    int len = GetWindowTextLengthA(hwndOutput);
    SendMessageA(hwndOutput, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageA(hwndOutput, EM_REPLACESEL, FALSE, (LPARAM)text);
    SendMessageA(hwndOutput, EM_SCROLLCARET, 0, 0);
}

/* An EDIT control needs \r\n. The previous byte is remembered across calls so
 * a \r\n split between two reads does not gain a second \r. */
static char g_prev_byte = '\0';

static void append_raw(const char *raw, int n)
{
    char out[8192];
    int i, j = 0;

    for (i = 0; i < n && j < (int)sizeof(out) - 2; i++) {
        if (raw[i] == '\n' && g_prev_byte != '\r') out[j++] = '\r';
        out[j++] = raw[i];
        g_prev_byte = raw[i];
    }
    out[j] = '\0';
    append_text(out);
}

/* ── running state ───────────────────────────────────────────────── */
static void set_running(BOOL running)
{
    g_running = running;
    EnableWindow(hwndRun,   !running);
    EnableWindow(hwndStop,   running);
    EnableWindow(hwndInput,  running);
    EnableWindow(hwndSend,   running);
    SetWindowTextA(hwndLblOut, running ? "OUTPUT   \xE2\x80\x94   running"
                                       : "OUTPUT");
}

static void close_handle(HANDLE *h)
{
    if (*h) { CloseHandle(*h); *h = NULL; }
}

/* Reads whatever is sitting in a pipe without ever blocking on it. */
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
        append_raw(buf, (int)got);
    }
}

static void finish_run(HWND hwnd, const char *note)
{
    KillTimer(hwnd, ID_POLL);

    drain(g_out);                 /* anything written just before exiting */
    drain(g_err);

    close_handle(&g_in);
    close_handle(&g_out);
    close_handle(&g_err);

    if (g_pi.hProcess) { CloseHandle(g_pi.hProcess); g_pi.hProcess = NULL; }
    if (g_pi.hThread)  { CloseHandle(g_pi.hThread);  g_pi.hThread  = NULL; }

    if (g_tmp_file[0]) { remove(g_tmp_file); g_tmp_file[0] = '\0'; }

    set_running(FALSE);
    if (note) append_text(note);
    SetFocus(hwndCode);
}

/* ── start adda on the editor contents ───────────────────────────── */
static void run_code(HWND hwnd)
{
    char tmp_dir[MAX_PATH];
    char adda_exe[MAX_PATH];
    char cmd[MAX_PATH * 4];      /* room for both quoted paths, worst case */
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOA si;
    HANDLE hWriteOut = NULL, hWriteErr = NULL, hReadIn = NULL;
    char *code;
    int len;
    FILE *f;
    BOOL ok;

    if (g_running) return;

    /* the editor contents go to a temp file */
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

    /* three pipes: output, errors, and input */
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    CreatePipe(&g_out, &hWriteOut, &sa, 0);
    SetHandleInformation(g_out, HANDLE_FLAG_INHERIT, 0);
    CreatePipe(&g_err, &hWriteErr, &sa, 0);
    SetHandleInformation(g_err, HANDLE_FLAG_INHERIT, 0);
    CreatePipe(&hReadIn, &g_in, &sa, 0);
    SetHandleInformation(g_in, HANDLE_FLAG_INHERIT, 0);

    ZeroMemory(&si, sizeof(si));
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = hReadIn;
    si.hStdOutput = hWriteOut;
    si.hStdError  = hWriteErr;

    ZeroMemory(&g_pi, sizeof(g_pi));

    ok = CreateProcessA(NULL, cmd, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &g_pi);

    /* the child owns its ends now */
    CloseHandle(hWriteOut);
    CloseHandle(hWriteErr);
    CloseHandle(hReadIn);

    if (!ok) {
        close_handle(&g_in);
        close_handle(&g_out);
        close_handle(&g_err);
        SetWindowTextA(hwndOutput,
            "Could not run adda.exe\r\n"
            "Make sure adda.exe is in the same folder as adda-gui.exe.");
        remove(g_tmp_file);
        g_tmp_file[0] = '\0';
        return;
    }

    SetWindowTextA(hwndOutput, "");
    g_prev_byte = '\0';
    set_running(TRUE);
    SetFocus(hwndInput);

    /* Poll rather than wait. Waiting for the process first deadlocks as soon
     * as a program writes more than the pipe will hold, because nothing is
     * draining it. */
    SetTimer(hwnd, ID_POLL, 50, NULL);
}

/* ── send a line of input to the waiting program ─────────────────── */
static void send_input(void)
{
    char line[1024];
    DWORD written;
    int len;

    if (!g_running || !g_in) return;

    len = GetWindowTextA(hwndInput, line, (int)sizeof(line) - 2);
    line[len] = '\0';

    /* Show what was typed, so the transcript reads the way it would in a
     * terminal - a pipe does not echo. */
    append_text(line);
    append_text("\r\n");

    line[len++] = '\n';
    WriteFile(g_in, line, (DWORD)len, &written, NULL);
    FlushFileBuffers(g_in);

    SetWindowTextA(hwndInput, "");
    SetFocus(hwndInput);
}

/* ── drawing ─────────────────────────────────────────────────────── */

/* A hairline around a panel, drawn just outside the control. */
static void panel_border(HDC hdc, HWND child)
{
    RECT r;
    HPEN pen, old_pen;
    HGDIOBJ old_brush;

    GetWindowRect(child, &r);
    MapWindowPoints(NULL, GetParent(child), (POINT *)&r, 2);
    InflateRect(&r, 1, 1);

    pen = CreatePen(PS_SOLID, 1, g_t.border);
    old_pen = (HPEN)SelectObject(hdc, pen);
    old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, r.left, r.top, r.right, r.bottom, S(6), S(6));
    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);
}

static void draw_button(DRAWITEMSTRUCT *di)
{
    RECT r = di->rcItem;
    BOOL disabled = (di->itemState & ODS_DISABLED) != 0;
    BOOL pressed  = (di->itemState & ODS_SELECTED) != 0;
    BOOL hot      = (g_hot == di->hwndItem) && !disabled;
    BOOL primary  = (di->CtlID == ID_RUN);
    BOOL icon     = (di->CtlID == ID_COG);
    BOOL outlined = !primary && !icon;
    COLORREF fill, ink, edge;
    HBRUSH brush;
    HPEN pen;
    HGDIOBJ old_brush, old_pen;

    if (primary) {
        fill = pressed ? g_t.accentDown : hot ? g_t.accentHot : g_t.accent;
        ink  = g_t.onAccent;
        edge = fill;
        if (disabled) { fill = g_t.ghostHot; ink = g_t.muted; edge = g_t.border; }
    } else if (icon) {
        fill = (pressed || hot) ? g_t.ghostHot : g_t.bg;
        ink  = disabled ? g_t.muted : g_t.text;
        edge = fill;
    } else {
        fill = (pressed || hot) ? g_t.ghostHot : g_t.surface;
        ink  = disabled ? g_t.muted : g_t.text;
        edge = g_t.border;
    }

    /* the corners fall outside the rounded shape, so start from the window */
    {
        HBRUSH bg = CreateSolidBrush(g_t.bg);
        FillRect(di->hDC, &r, bg);
        DeleteObject(bg);
    }

    brush = CreateSolidBrush(fill);
    pen   = CreatePen(PS_SOLID, 1, outlined ? g_t.border : edge);
    old_brush = SelectObject(di->hDC, brush);
    old_pen   = SelectObject(di->hDC, pen);
    RoundRect(di->hDC, r.left, r.top, r.right, r.bottom, S(6), S(6));
    SelectObject(di->hDC, old_brush);
    SelectObject(di->hDC, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);

    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, ink);

    if (icon) {
        HGDIOBJ old = SelectObject(di->hDC, hFontIcon);
        DrawTextW(di->hDC, COG_GLYPH, 1, &r,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(di->hDC, old);
    } else {
        char label[64];
        HGDIOBJ old = SelectObject(di->hDC, hFontUI);
        GetWindowTextA(di->hwndItem, label, sizeof(label));
        DrawTextA(di->hDC, label, -1, &r,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(di->hDC, old);
    }

    if (di->itemState & ODS_FOCUS) {
        RECT f = r;
        InflateRect(&f, -S(3), -S(3));
        DrawFocusRect(di->hDC, &f);
    }
}

/* Buttons do not tell their parent about hover, so each one is subclassed. */
static LRESULT CALLBACK btn_proc(HWND h, UINT msg, WPARAM w, LPARAM l,
                                 UINT_PTR id, DWORD_PTR ref)
{
    (void)ref;

    switch (msg) {
    case WM_MOUSEMOVE:
        if (g_hot != h) {
            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = h;
            tme.dwHoverTime = 0;
            TrackMouseEvent(&tme);
            g_hot = h;
            InvalidateRect(h, NULL, TRUE);
        }
        break;
    case WM_MOUSELEAVE:
        if (g_hot == h) { g_hot = NULL; InvalidateRect(h, NULL, TRUE); }
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(h, btn_proc, id);
        break;
    }
    return DefSubclassProc(h, msg, w, l);
}

/* ── full screen ─────────────────────────────────────────────────── */
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

/* ── the cog menu ────────────────────────────────────────────────── */
static void show_settings(HWND hwnd)
{
    HMENU menu = CreatePopupMenu();
    RECT r;

    AppendMenuA(menu, MF_STRING, IDM_SYSTEM, "Follow Windows");
    AppendMenuA(menu, MF_STRING, IDM_LIGHT,  "Light");
    AppendMenuA(menu, MF_STRING, IDM_DARK,   "Dark");
    AppendMenuA(menu, MF_STRING, IDM_BEIGE,  "Beige");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING, IDM_FULL,   "Full screen\tF11");

    CheckMenuRadioItem(menu, IDM_SYSTEM, IDM_BEIGE,
                       (UINT)(IDM_SYSTEM + g_pick), MF_BYCOMMAND);
    if (g_fullscreen)
        CheckMenuItem(menu, IDM_FULL, MF_BYCOMMAND | MF_CHECKED);

    GetWindowRect(hwndCog, &r);
    TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                   r.right, r.bottom + S(4), 0, hwnd, NULL);
    DestroyMenu(menu);
}

/* ── layout ──────────────────────────────────────────────────────── */
static void pad_edit(HWND h, int pad)
{
    RECT r;
    GetClientRect(h, &r);
    InflateRect(&r, -pad, -pad);
    SendMessageA(h, EM_SETRECT, 0, (LPARAM)&r);
}

static void layout(HWND hwnd)
{
    RECT rc;
    int pad, gap, labelH, btnH, btnW, inputH, cogW, w, h, fixed, boxes;
    int codeH, outH, y;

    GetClientRect(hwnd, &rc);

    pad    = S(18);
    gap    = S(10);
    labelH = S(18);
    btnH   = S(34);
    btnW   = S(88);
    inputH = S(34);
    cogW   = S(34);

    w = rc.right  - pad * 2;
    h = rc.bottom - pad * 2;

    fixed = labelH * 3 + S(18) + btnH + inputH + gap * 4 + cogW;
    boxes = h - fixed;
    codeH = boxes / 2;
    outH  = boxes - codeH;
    if (codeH < S(60)) codeH = S(60);
    if (outH  < S(60)) outH  = S(60);

    y = pad;
    MoveWindow(hwndLblCode, pad, y + S(8), w - cogW - gap, labelH, TRUE);
    MoveWindow(hwndCog, pad + w - cogW, y, cogW, cogW, TRUE);
    y += cogW + S(2);

    MoveWindow(hwndCode, pad, y, w, codeH, TRUE);
    y += codeH + gap;

    MoveWindow(hwndRun,  pad, y, btnW, btnH, TRUE);
    MoveWindow(hwndStop, pad + btnW + S(8), y, btnW, btnH, TRUE);
    y += btnH + gap;

    MoveWindow(hwndLblOut, pad, y, w, labelH, TRUE);
    y += labelH + S(6);
    MoveWindow(hwndOutput, pad, y, w, outH, TRUE);
    y += outH + gap;

    MoveWindow(hwndLblIn, pad, y, w, labelH, TRUE);
    y += labelH + S(6);
    MoveWindow(hwndInput, pad, y, w - btnW - S(8), inputH, TRUE);
    MoveWindow(hwndSend,  pad + w - btnW, y, btnW, inputH, TRUE);

    pad_edit(hwndCode,   S(8));
    pad_edit(hwndOutput, S(8));
    pad_edit(hwndInput,  S(6));
}

/* ── window procedure ────────────────────────────────────────────── */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
                                WPARAM wParam, LPARAM lParam)
{
    switch (msg) {

    case WM_CREATE: {
        HDC hdc = GetDC(hwnd);
        g_dpi = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(hwnd, hdc);

        load_pick();
        resolve_theme();
        hBrushBg      = CreateSolidBrush(g_t.bg);
        hBrushSurface = CreateSolidBrush(g_t.surface);
        build_fonts();

        hwndLblCode = CreateWindowExA(0, "STATIC", "CODE",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
            hwnd, (HMENU)ID_LBL_CODE, NULL, NULL);

        hwndLblOut = CreateWindowExA(0, "STATIC", "OUTPUT",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
            hwnd, (HMENU)ID_LBL_OUT, NULL, NULL);

        hwndLblIn = CreateWindowExA(0, "STATIC", "INPUT",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
            hwnd, (HMENU)ID_LBL_IN, NULL, NULL);

        hwndCode = CreateWindowExA(0, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
            0, 0, 0, 0, hwnd, (HMENU)ID_CODE, NULL, NULL);

        hwndRun = CreateWindowExA(0, "BUTTON", "Run",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 0, 0, hwnd, (HMENU)ID_RUN, NULL, NULL);

        hwndStop = CreateWindowExA(0, "BUTTON", "Stop",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_DISABLED,
            0, 0, 0, 0, hwnd, (HMENU)ID_STOP, NULL, NULL);

        hwndCog = CreateWindowExA(0, "BUTTON", "",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 0, 0, hwnd, (HMENU)ID_COG, NULL, NULL);

        hwndOutput = CreateWindowExA(0, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            0, 0, 0, 0, hwnd, (HMENU)ID_OUTPUT, NULL, NULL);

        hwndInput = CreateWindowExA(0, "EDIT", "",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_DISABLED,
            0, 0, 0, 0, hwnd, (HMENU)ID_INPUT, NULL, NULL);

        hwndSend = CreateWindowExA(0, "BUTTON", "Send",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_DISABLED,
            0, 0, 0, 0, hwnd, (HMENU)ID_SEND, NULL, NULL);

        SetWindowSubclass(hwndRun,  btn_proc, ID_RUN,  0);
        SetWindowSubclass(hwndStop, btn_proc, ID_STOP, 0);
        SetWindowSubclass(hwndSend, btn_proc, ID_SEND, 0);
        SetWindowSubclass(hwndCog,  btn_proc, ID_COG,  0);

        apply_fonts();
        apply_titlebar(hwnd);
        theme_edit(hwndCode);
        theme_edit(hwndOutput);
        theme_edit(hwndInput);

        SetWindowTextA(hwndCode,
            "name = ask What is your name?\r\n"
            "print Hello, {name}");
        return 0;
    }

    /* ── colours ─────────────────────────────────────────────────── */
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, g_t.muted);
        SetBkColor(hdc, g_t.bg);
        return (LRESULT)hBrushBg;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, g_t.text);
        SetBkColor(hdc, g_t.surface);
        return (LRESULT)hBrushSurface;
    }
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wParam, &rc, hBrushBg);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        panel_border(hdc, hwndCode);
        panel_border(hdc, hwndOutput);
        panel_border(hdc, hwndInput);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DRAWITEM:
        draw_button((DRAWITEMSTRUCT *)lParam);
        return TRUE;

    /* ── collect output while the program runs ───────────────────── */
    case WM_TIMER:
        if (wParam == ID_POLL && g_running) {
            drain(g_out);
            drain(g_err);
            if (WaitForSingleObject(g_pi.hProcess, 0) == WAIT_OBJECT_0)
                finish_run(hwnd, NULL);
        }
        return 0;

    case WM_SIZE:
        layout(hwnd);
        return 0;

    case WM_DPICHANGED: {
        RECT *r = (RECT *)lParam;
        g_dpi = HIWORD(wParam);
        build_fonts();
        apply_fonts();
        SetWindowPos(hwnd, NULL, r->left, r->top,
                     r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        layout(hwnd);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }

    /* ── commands ─────────────────────────────────────────────────── */
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_RUN:  run_code(hwnd);      return 0;
        case ID_SEND: send_input();        return 0;
        case ID_COG:  show_settings(hwnd); return 0;
        case ID_STOP:
            if (g_running) {
                TerminateProcess(g_pi.hProcess, 1);
                finish_run(hwnd, "\r\n[stopped]\r\n");
            }
            return 0;

        case IDM_SYSTEM: case IDM_LIGHT: case IDM_DARK: case IDM_BEIGE:
            g_pick = (int)LOWORD(wParam) - IDM_SYSTEM;
            save_pick();
            apply_theme(hwnd);
            return 0;

        case IDM_FULL:
            toggle_fullscreen(hwnd);
            return 0;
        }
        return 0;

    /* ── F11 full screen ─────────────────────────────────────────── */
    case WM_KEYDOWN:
        if (wParam == VK_F11) { toggle_fullscreen(hwnd); return 0; }
        break;

    /* the palette can change while the window is open */
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
        DeleteObject(hFontMono);
        DeleteObject(hFontUI);
        DeleteObject(hFontIcon);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

/* ── entry point ─────────────────────────────────────────────────── */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev,
                   LPSTR cmdLine, int cmdShow)
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
    wc.hbrBackground = NULL;          /* WM_ERASEBKGND paints it */
    wc.lpszClassName = "AddaGUI";
    RegisterClassA(&wc);

    hwnd = CreateWindowExA(
        0, "AddaGUI", "Adda",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 720, 860,
        NULL, NULL, hInst, NULL);

    ShowWindow(hwnd, cmdShow);
    UpdateWindow(hwnd);

    while (GetMessageA(&msg, NULL, 0, 0)) {
        /* Enter in the input line sends it; an EDIT would otherwise just beep */
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN
            && msg.hwnd == hwndInput) {
            send_input();
            continue;
        }
        /* forward F11 from child controls to the main window */
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_F11) {
            SendMessageA(hwnd, WM_KEYDOWN, VK_F11, 0);
            continue;
        }
        /* forward Ctrl+Enter from child controls to the main window */
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
