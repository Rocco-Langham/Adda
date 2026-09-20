/*
 *  Adda GUI — a tiny Win32 front-end for the Adda interpreter.
 *
 *  Build:  gcc -mwindows -o adda-gui src/gui.c
 *  Place adda-gui.exe next to adda.exe and double-click to launch.
 *
 *  The program runs in the background while the window stays responsive: a
 *  timer drains its output pipes every 50ms and appends whatever has arrived.
 *  That is what lets `ask` work — the prompt appears, you type an answer into
 *  the input line, and it goes down the pipe to the waiting program.
 */

#include <windows.h>
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
#define ID_POLL      1           /* timer */

/* ── colours ─────────────────────────────────────────────────────── */
#define BEIGE        RGB(245, 245, 220)
#define TEXT_COLOR   RGB(30,  30,  30)

/* ── globals ─────────────────────────────────────────────────────── */
static HWND    hwndCode, hwndOutput, hwndRun, hwndStop;
static HWND    hwndInput, hwndSend;
static HWND    hwndLblCode, hwndLblOut, hwndLblIn;
static HBRUSH  hBrushBeige;
static HFONT   hFontMono, hFontUI;

/* the program currently running, if any */
static PROCESS_INFORMATION g_pi;
static HANDLE  g_out = NULL, g_err = NULL, g_in = NULL;
static BOOL    g_running = FALSE;
static char    g_tmp_file[MAX_PATH * 2];

/* ── find adda.exe next to this executable ───────────────────────── */
static void get_adda_path(char *buf, int size)
{
    GetModuleFileNameA(NULL, buf, size);
    char *sep = strrchr(buf, '\\');
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
    SetWindowTextA(hwndLblOut, running ? "Output:  (running)" : "Output:");
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

    /* three pipes: output, errors, and now input */
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

    /* Poll rather than wait. Waiting for the process first - which is what
     * this used to do - deadlocks as soon as a program writes more than the
     * pipe will hold, because nothing is draining it. */
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

        hwndLblIn = CreateWindowExA(0, "STATIC", "Input:",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
            hwnd, (HMENU)ID_LBL_IN, NULL, NULL);

        /* code editor */
        hwndCode = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN,
            0, 0, 0, 0, hwnd, (HMENU)ID_CODE, NULL, NULL);

        /* buttons */
        hwndRun = CreateWindowExA(0, "BUTTON", "Run",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)ID_RUN, NULL, NULL);

        hwndStop = CreateWindowExA(0, "BUTTON", "Stop",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED,
            0, 0, 0, 0, hwnd, (HMENU)ID_STOP, NULL, NULL);

        /* output (read-only) */
        hwndOutput = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY,
            0, 0, 0, 0, hwnd, (HMENU)ID_OUTPUT, NULL, NULL);

        /* input line, only usable while something is running */
        hwndInput = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_DISABLED,
            0, 0, 0, 0, hwnd, (HMENU)ID_INPUT, NULL, NULL);

        hwndSend = CreateWindowExA(0, "BUTTON", "Send",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED,
            0, 0, 0, 0, hwnd, (HMENU)ID_SEND, NULL, NULL);

        SendMessage(hwndCode,    WM_SETFONT, (WPARAM)hFontMono, TRUE);
        SendMessage(hwndOutput,  WM_SETFONT, (WPARAM)hFontMono, TRUE);
        SendMessage(hwndInput,   WM_SETFONT, (WPARAM)hFontMono, TRUE);
        SendMessage(hwndRun,     WM_SETFONT, (WPARAM)hFontUI,   TRUE);
        SendMessage(hwndStop,    WM_SETFONT, (WPARAM)hFontUI,   TRUE);
        SendMessage(hwndSend,    WM_SETFONT, (WPARAM)hFontUI,   TRUE);
        SendMessage(hwndLblCode, WM_SETFONT, (WPARAM)hFontUI,   TRUE);
        SendMessage(hwndLblOut,  WM_SETFONT, (WPARAM)hFontUI,   TRUE);
        SendMessage(hwndLblIn,   WM_SETFONT, (WPARAM)hFontUI,   TRUE);

        /* default example - now that input works, show it off */
        SetWindowTextA(hwndCode,
            "name = ask What is your name?\r\n"
            "print Hello, {name}");
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

    /* ── collect output while the program runs ───────────────────── */
    case WM_TIMER:
        if (wParam == ID_POLL && g_running) {
            drain(g_out);
            drain(g_err);

            if (WaitForSingleObject(g_pi.hProcess, 0) == WAIT_OBJECT_0)
                finish_run(hwnd, NULL);
        }
        return 0;

    /* ── layout ──────────────────────────────────────────────────── */
    case WM_SIZE: {
        int pad = 12;
        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right  - pad * 2;
        int h = rc.bottom - pad * 2;
        int labelH  = 20;
        int btnH    = 30;
        int inputH  = 26;
        int gap     = 8;
        int sendW   = 70;
        int fixed   = labelH * 3 + 6 + btnH + inputH + gap * 4;
        int boxes   = h - fixed;
        int codeH   = boxes / 2;
        int outH    = boxes - codeH;
        int y       = pad;

        if (codeH < 40) codeH = 40;
        if (outH  < 40) outH  = 40;

        MoveWindow(hwndLblCode, pad, y, w, labelH, TRUE);
        y += labelH + 2;
        MoveWindow(hwndCode, pad, y, w, codeH, TRUE);
        y += codeH + gap;
        MoveWindow(hwndRun,  pad,      y, 70, btnH, TRUE);
        MoveWindow(hwndStop, pad + 78, y, 70, btnH, TRUE);
        y += btnH + gap;
        MoveWindow(hwndLblOut, pad, y, w, labelH, TRUE);
        y += labelH + 2;
        MoveWindow(hwndOutput, pad, y, w, outH, TRUE);
        y += outH + gap;
        MoveWindow(hwndLblIn, pad, y, w, labelH, TRUE);
        y += labelH + 2;
        MoveWindow(hwndInput, pad, y, w - sendW - gap, inputH, TRUE);
        MoveWindow(hwndSend,  pad + w - sendW, y, sendW, inputH, TRUE);
        return 0;
    }

    /* ── commands ─────────────────────────────────────────────────── */
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_RUN:  run_code(hwnd); return 0;
        case ID_SEND: send_input();   return 0;
        case ID_STOP:
            if (g_running) {
                TerminateProcess(g_pi.hProcess, 1);
                finish_run(hwnd, "\r\n[stopped]\r\n");
            }
            return 0;
        }
        return 0;

    case WM_DESTROY:
        if (g_running) {
            TerminateProcess(g_pi.hProcess, 1);
            finish_run(hwnd, NULL);
        }
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
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 760,
        NULL, NULL, hInst, NULL);

    ShowWindow(hwnd, cmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        /* Enter in the input line sends it; an EDIT would otherwise just beep */
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN
            && msg.hwnd == hwndInput) {
            send_input();
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
