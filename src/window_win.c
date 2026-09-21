/* openApplication on Windows: a window whose printed lines sit, as one block,
 * in the dead centre. The program keeps running while it is open; its
 * messages are handled whenever the program prints or waits, and once the
 * program is finished the window stays up until it is closed. Closing it
 * early ends the program, as closing an app does. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "adda.h"

static HWND  g_window;
static char *g_text;            /* every printed line, joined with \r\n */
static size_t g_len;
static HFONT g_font;

typedef struct { int kind; double inset[SHAPE_SPEC]; } Shape;
static Shape *g_shapes;
static int    g_shapeCount;

static COLORREF mix(COLORREF a, COLORREF b, int pctB)
{
    return RGB((GetRValue(a) * (100 - pctB) + GetRValue(b) * pctB) / 100,
               (GetGValue(a) * (100 - pctB) + GetGValue(b) * pctB) / 100,
               (GetBValue(a) * (100 - pctB) + GetBValue(b) * pctB) / 100);
}

static LRESULT CALLBACK canvas_proc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc, box;
        HGDIOBJ old;

        GetClientRect(h, &rc);
        FillRect(dc, &rc, (HBRUSH)(COLOR_WINDOW + 1));

        /* shapes first, so the text sits on top of them */
        if (g_shapeCount) {
            COLORREF bg = GetSysColor(COLOR_WINDOW), fg = GetSysColor(COLOR_WINDOWTEXT);
            HBRUSH fill = CreateSolidBrush(mix(bg, fg, 10));
            HPEN edge = CreatePen(PS_SOLID, 1, mix(bg, fg, 30));
            HGDIOBJ ob = SelectObject(dc, fill), op = SelectObject(dc, edge);
            int i;
            for (i = 0; i < g_shapeCount; i++) {
                double r[4], xy[20];
                int x, y, w, ht, round, kind = g_shapes[i].kind, corners, k;
                adda_shape_rect(kind, g_shapes[i].inset, rc.right, rc.bottom, r);
                x = (int)r[0]; y = (int)r[1]; w = (int)r[2]; ht = (int)r[3];

                corners = adda_shape_points(kind, r, xy);
                if (corners) {
                    POINT pts[10];
                    for (k = 0; k < corners; k++) {
                        pts[k].x = (LONG)xy[2 * k];
                        pts[k].y = (LONG)xy[2 * k + 1];
                    }
                    Polygon(dc, pts, corners);
                } else if (kind == SHAPE_LINE) {    /* corner to corner of its space */
                    HPEN thick = CreatePen(PS_SOLID, 2, mix(bg, fg, 55));
                    HGDIOBJ was = SelectObject(dc, thick);
                    MoveToEx(dc, x, y, NULL);
                    LineTo(dc, x + w, y + ht);
                    SelectObject(dc, was);
                    DeleteObject(thick);
                } else if (kind == SHAPE_CIRCLE) {  /* the biggest circle that fits */
                    int d = w < ht ? w : ht;
                    Ellipse(dc, x + (w - d) / 2, y + (ht - d) / 2,
                            x + (w - d) / 2 + d, y + (ht - d) / 2 + d);
                } else if (kind == SHAPE_OVAL) {
                    Ellipse(dc, x, y, x + w, y + ht);
                } else {
                    round = kind == SHAPE_ROUNDED_BOX ? 24
                          : kind == SHAPE_PILL ? (w < ht ? w : ht) : 0;
                    RoundRect(dc, x, y, x + w, y + ht, round, round);
                }
            }
            SelectObject(dc, ob); SelectObject(dc, op);
            DeleteObject(fill); DeleteObject(edge);
        }

        if (g_len) {
            old = SelectObject(dc, g_font);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            /* measure the whole block, then put its middle on the window's middle */
            box = rc;
            InflateRect(&box, -20, 0);
            DrawTextA(dc, g_text, (int)g_len, &box, DT_CENTER | DT_WORDBREAK | DT_CALCRECT);
            OffsetRect(&box, 0, ((rc.bottom - rc.top) - (box.bottom - box.top)) / 2 - box.top);
            box.left = rc.left + 20;
            box.right = rc.right - 20;
            DrawTextA(dc, g_text, (int)g_len, &box, DT_CENTER | DT_WORDBREAK);
            SelectObject(dc, old);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_SIZE:
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_DESTROY:            /* the window was closed: so is the program */
        fflush(stdout);
        exit(0);
    }
    return DefWindowProcA(h, msg, w, l);
}

/* Handles whatever is waiting - clicks, resizes, redraws - and returns. */
static void pump(void)
{
    MSG m;
    while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
}

bool adda_open_window(const char *title)
{
    HINSTANCE inst = GetModuleHandleA(NULL);
    WNDCLASSA wc;
    const char *t = (title && *title) ? title : "Adda";

    if (g_window) {             /* a second openApplication just renames it */
        SetWindowTextA(g_window, t);
        return true;
    }

    ZeroMemory(&wc, sizeof wc);
    wc.lpfnWndProc = canvas_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.lpszClassName = "AddaApplication";
    if (!RegisterClassA(&wc)) return false;

    g_font = CreateFontA(-26, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    g_window = CreateWindowExA(0, "AddaApplication", t, WS_OVERLAPPEDWINDOW,
                               CW_USEDEFAULT, CW_USEDEFAULT, 640, 420,
                               NULL, NULL, inst, NULL);
    if (!g_window) return false;
    ShowWindow(g_window, SW_SHOW);
    SetForegroundWindow(g_window);
    UpdateWindow(g_window);
    pump();
    return true;
}

bool adda_window_is_open(void) { return g_window != NULL; }

void adda_window_print(const char *text, size_t len)
{
    char *grown = realloc(g_text, g_len + len + 3);
    if (!grown) return;
    g_text = grown;
    if (g_len) { g_text[g_len++] = '\r'; g_text[g_len++] = '\n'; }
    memcpy(g_text + g_len, text, len);
    g_len += len;
    g_text[g_len] = '\0';
    InvalidateRect(g_window, NULL, FALSE);
    UpdateWindow(g_window);
    pump();
}

void adda_window_shape(int kind, const double inset[SHAPE_SPEC])
{
    Shape *grown = realloc(g_shapes, sizeof *g_shapes * (size_t)(g_shapeCount + 1));
    if (!grown) return;
    g_shapes = grown;
    g_shapes[g_shapeCount].kind = kind;
    memcpy(g_shapes[g_shapeCount].inset, inset, sizeof g_shapes[0].inset);
    g_shapeCount++;
    InvalidateRect(g_window, NULL, FALSE);
    UpdateWindow(g_window);
    pump();
}

void adda_window_wait_ms(double ms)
{
    DWORD end = GetTickCount() + (DWORD)ms;
    for (;;) {
        DWORD now = GetTickCount();
        if ((int)(end - now) <= 0) break;
        MsgWaitForMultipleObjects(0, NULL, FALSE, end - now, QS_ALLINPUT);
        pump();
    }
}

void adda_window_run(void)
{
    MSG m;
    while (GetMessageA(&m, NULL, 0, 0) > 0) {   /* until WM_DESTROY exits */
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
}
