/*
 *  Adda GUI — a tiny Win32 front-end for the Adda interpreter.
 *
 *  Build:  gcc -mwindows -o adda-gui src/gui.c
 *  Place adda-gui.exe next to adda.exe and double-click to launch.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

/* ── control IDs ─────────────────────────────────────────────────── */
#define ID_CODE      1001
#define ID_OUTPUT    1002
#define ID_RUN       1003
#define ID_LBL_CODE  1004
#define ID_LBL_OUT   1005

/* ── colours ─────────────────────────────────────────────────────── */
#define BEIGE        RGB(245, 245, 220)
#define TEXT_COLOR   RGB(30,  30,  30)

/* ── globals ─────────────────────────────────────────────────────── */
static HWND    hwndCode, hwndOutput, hwndRun;
static HWND    hwndLblCode, hwndLblOut;
static HBRUSH  hBrushBeige;
static HFONT   hFontMono, hFontUI;

/* ── find adda.exe next to this executable ───────────────────────── */
static void get_adda_path(char *buf, int size)
{
    GetModuleFileNameA(NULL, buf, size);
    char *sep = strrchr(buf, '\\');
    if (sep) *(sep + 1) = '\0';
    else     buf[0] = '\0';
    strncat(buf, "adda.exe", size - (int)strlen(buf) - 1);
}

/* ── run adda on the editor contents ─────────────────────────────── */
static void run_code(HWND hwnd)
{
    (void)hwnd;

    /* grab code text */
    int len = GetWindowTextLengthA(hwndCode);
    char *code = (char *)malloc(len + 2);
    if (!code) return;
    GetWindowTextA(hwndCode, code, len + 1);

    /* write to a temp .adda file */
    char tmp_dir[MAX_PATH], tmp_file[MAX_PATH + 16];
    GetTempPathA(MAX_PATH, tmp_dir);
    snprintf(tmp_file, sizeof(tmp_file), "%s_adda_gui.adda", tmp_dir);

    FILE *f = fopen(tmp_file, "w");
    if (!f) { free(code); return; }
    fputs(code, f);
    fclose(f);
    free(code);

    /* build command line */
    char adda_exe[MAX_PATH];
    get_adda_path(adda_exe, MAX_PATH);

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\"", adda_exe, tmp_file);

    /* pipes for stdout + stderr */
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hReadOut, hWriteOut, hReadErr, hWriteErr;
    CreatePipe(&hReadOut, &hWriteOut, &sa, 0);
    SetHandleInformation(hReadOut, HANDLE_FLAG_INHERIT, 0);
    CreatePipe(&hReadErr, &hWriteErr, &sa, 0);
    SetHandleInformation(hReadErr, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = hWriteOut;
    si.hStdError  = hWriteErr;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, TRUE,
                             CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(hWriteOut);
    CloseHandle(hWriteErr);

    if (!ok) {
        SetWindowTextA(hwndOutput,
            "Could not run adda.exe\r\n"
            "Make sure adda.exe is in the same folder as adda-gui.exe.");
        CloseHandle(hReadOut);
        CloseHandle(hReadErr);
        remove(tmp_file);
        return;
    }

    WaitForSingleObject(pi.hProcess, 10000);   /* 10 s timeout */

    /* collect output */
    char raw[32768] = {0};
    DWORD n;
    int pos = 0;
    while (ReadFile(hReadOut, raw + pos,
                    (DWORD)(sizeof(raw) - pos - 1), &n, NULL) && n > 0)
        pos += (int)n;
    while (ReadFile(hReadErr, raw + pos,
                    (DWORD)(sizeof(raw) - pos - 1), &n, NULL) && n > 0)
        pos += (int)n;
    raw[pos] = '\0';

    /* convert bare \n → \r\n for the EDIT control */
    char display[65536];
    int j = 0;
    for (int i = 0; raw[i] && j < (int)sizeof(display) - 2; i++) {
        if (raw[i] == '\n' && (i == 0 || raw[i - 1] != '\r'))
            display[j++] = '\r';
        display[j++] = raw[i];
    }
    display[j] = '\0';

    SetWindowTextA(hwndOutput, display);

    CloseHandle(hReadOut);
    CloseHandle(hReadErr);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    remove(tmp_file);
}

/* ── window procedure ────────────────────────────────────────────── */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
                                WPARAM wParam, LPARAM lParam)
{
    switch (msg) {

    case WM_CREATE:
        hBrushBeige = CreateSolidBrush(BEIGE);

        hFontMono = CreateFontA(
            16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");

        hFontUI = CreateFontA(
            15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");

        /* labels */
        hwndLblCode = CreateWindowExA(0, "STATIC", "Code:",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
            hwnd, (HMENU)ID_LBL_CODE, NULL, NULL);

        hwndLblOut = CreateWindowExA(0, "STATIC", "Output:",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
            hwnd, (HMENU)ID_LBL_OUT, NULL, NULL);

        /* code editor */
        hwndCode = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN,
            0, 0, 0, 0, hwnd, (HMENU)ID_CODE, NULL, NULL);

        /* run button */
        hwndRun = CreateWindowExA(0, "BUTTON", "Run",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)ID_RUN, NULL, NULL);

        /* output (read-only) */
        hwndOutput = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY,
            0, 0, 0, 0, hwnd, (HMENU)ID_OUTPUT, NULL, NULL);

        SendMessage(hwndCode,    WM_SETFONT, (WPARAM)hFontMono, TRUE);
        SendMessage(hwndOutput,  WM_SETFONT, (WPARAM)hFontMono, TRUE);
        SendMessage(hwndRun,     WM_SETFONT, (WPARAM)hFontUI,   TRUE);
        SendMessage(hwndLblCode, WM_SETFONT, (WPARAM)hFontUI,   TRUE);
        SendMessage(hwndLblOut,  WM_SETFONT, (WPARAM)hFontUI,   TRUE);

        /* default example */
        SetWindowTextA(hwndCode,
            "name = World\r\nprint Hello {name}");
        return 0;

    /* ── beige backgrounds everywhere ────────────────────────────── */
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, TEXT_COLOR);
        SetBkColor(hdc, BEIGE);
        return (LRESULT)hBrushBeige;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, TEXT_COLOR);
        SetBkColor(hdc, BEIGE);
        return (LRESULT)hBrushBeige;
    }
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wParam, &rc, hBrushBeige);
        return 1;
    }

    /* ── layout ──────────────────────────────────────────────────── */
    case WM_SIZE: {
        int pad = 12;
        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right  - pad * 2;
        int h = rc.bottom - pad * 2;
        int labelH  = 20;
        int btnH    = 30;
        int gap     = 8;
        int codeH   = (h - labelH * 2 - btnH - gap * 4) / 2;
        int outH    = codeH;
        int y       = pad;

        MoveWindow(hwndLblCode, pad, y, w, labelH, TRUE);
        y += labelH + 2;
        MoveWindow(hwndCode, pad, y, w, codeH, TRUE);
        y += codeH + gap;
        MoveWindow(hwndRun, pad, y, 70, btnH, TRUE);
        y += btnH + gap;
        MoveWindow(hwndLblOut, pad, y, w, labelH, TRUE);
        y += labelH + 2;
        MoveWindow(hwndOutput, pad, y, w, outH, TRUE);
        return 0;
    }

    /* ── commands ─────────────────────────────────────────────────── */
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_RUN)
            run_code(hwnd);
        return 0;

    /* ── Ctrl+Enter to run ───────────────────────────────────────── */
    case WM_KEYDOWN:
        if (wParam == VK_RETURN && (GetKeyState(VK_CONTROL) & 0x8000))
            run_code(hwnd);
        return 0;

    case WM_DESTROY:
        DeleteObject(hBrushBeige);
        DeleteObject(hFontMono);
        DeleteObject(hFontUI);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

/* ── entry point ─────────────────────────────────────────────────── */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev,
                   LPSTR cmdLine, int cmdShow)
{
    (void)hPrev; (void)cmdLine;

    WNDCLASSA wc    = {0};
    wc.lpfnWndProc  = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(BEIGE);
    wc.lpszClassName = "AddaGUI";
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowExA(
        0, "AddaGUI", "Adda",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 700,
        NULL, NULL, hInst, NULL);

    ShowWindow(hwnd, cmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
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
