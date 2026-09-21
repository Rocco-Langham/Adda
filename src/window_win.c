/* openApplication on Windows: a blank window, and the program waits until it
 * is closed. */
#include <windows.h>
#include "adda.h"

static LRESULT CALLBACK blank_proc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, msg, w, l);
}

bool adda_open_window(const char *title)
{
    static bool registered;
    HINSTANCE inst = GetModuleHandleA(NULL);
    HWND h;
    MSG msg;

    if (!registered) {
        WNDCLASSA wc;
        ZeroMemory(&wc, sizeof wc);
        wc.lpfnWndProc = blank_proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = "AddaBlankWindow";
        if (!RegisterClassA(&wc)) return false;
        registered = true;
    }

    h = CreateWindowExA(0, "AddaBlankWindow", (title && *title) ? title : "Adda",
                        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 640, 420,
                        NULL, NULL, inst, NULL);
    if (!h) return false;
    ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);

    while (GetMessageA(&msg, NULL, 0, 0) > 0) {   /* until the close button */
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return true;
}
