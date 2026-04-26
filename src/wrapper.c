/*
 * Flying Toasters 2026 - Multi-Monitor Wrapper (pure Win32 / C)
 *
 * Build:
 *   zig cc wrapper.c -o flyingtoasters2026.exe \
 *     -O3 -target x86_64-windows-gnu -mwindows \
 *     -Wl,--gc-sections -ffunction-sections -fdata-sections \
 *     -lkernel32 -luser32 -lshell32 -lgdi32 -ladvapi32
 *
 * Replaces the 75 MB .NET self-contained build with a ~50 KB native exe.
 *
 * Behavior summary:
 *   /s          : enumerate every monitor, create one borderless WS_POPUP
 *                 host window per display, spawn the engine in /p HWND
 *                 mode against each.
 *   /p HWND     : forward to engine unchanged (small preview in Settings).
 *   /c[:HWND]   : forward to engine unchanged (Configure button).
 *   (no args)   : forward to engine in /c mode.
 *
 * On any keypress, mouse click, wheel, or mouse movement (>6 px after a
 * 750 ms dead zone), the wrapper kills every spawned engine process and
 * destroys all host windows.
 */

#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#  define UNICODE
#endif
#ifndef _UNICODE
#  define _UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <wchar.h>

#define ENGINE_FILENAME    L"flyingtoasters2026_engine.scr"
#define APP_TITLE          L"Flying Toasters 2026"
#define MUTEX_NAME         L"Local\\FlyingToasters2026_Wrapper_Singleton"
#define WND_CLASS_NAME     L"FlyingToasters2026SaverHost"

#define MAX_MONITORS       32
#define MOVE_DEAD_ZONE_PX  6
#define INITIAL_QUIET_MS   750

#ifndef _countof
#  define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif

/* SetProcessDpiAwarenessContext is Windows 10 1703+; resolve at runtime. */
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
typedef HANDLE DPI_AWARENESS_CONTEXT;
#  define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif
typedef BOOL (WINAPI *PFN_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);

static HHOOK             g_mouseHook      = NULL;
static HHOOK             g_kbHook         = NULL;
static POINT             g_startMouse     = { 0, 0 };
static DWORD             g_startedTick    = 0;
static volatile LONG     g_exiting        = 0;

static HWND              g_hosts[MAX_MONITORS];
static int               g_hostCount      = 0;
static HANDLE            g_engineHandles[MAX_MONITORS];
static int               g_engineCount    = 0;

/* ------------------------------------------------------------------ */

static void TriggerExit(void)
{
    if (InterlockedCompareExchange(&g_exiting, 1, 0) != 0) return;
    PostQuitMessage(0);
}

static LRESULT CALLBACK MouseLLProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION) {
        switch (wParam) {
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_MOUSEWHEEL:
        case WM_XBUTTONDOWN:
            TriggerExit();
            break;
        case WM_MOUSEMOVE: {
            DWORD now = GetTickCount();
            if (now - g_startedTick < INITIAL_QUIET_MS) break;
            MSLLHOOKSTRUCT *m = (MSLLHOOKSTRUCT *)lParam;
            int dx = m->pt.x - g_startMouse.x;
            int dy = m->pt.y - g_startMouse.y;
            if (dx * dx + dy * dy >
                MOVE_DEAD_ZONE_PX * MOVE_DEAD_ZONE_PX) {
                TriggerExit();
            }
            break;
        }
        }
    }
    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
}

static LRESULT CALLBACK KbLLProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION) {
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            TriggerExit();
        }
    }
    return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
}

static LRESULT CALLBACK SaverWndProc(HWND hwnd, UINT msg,
                                     WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_MOUSEWHEEL:
        TriggerExit();
        return 0;
    case WM_SETCURSOR:
        SetCursor(NULL);
        return TRUE;
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT r;
        GetClientRect(hwnd, &r);
        FillRect(hdc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
        return 1;
    }
    case WM_DESTROY:
        TriggerExit();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static BOOL CALLBACK EnumMonitorsProc(HMONITOR hMon, HDC hdc,
                                      LPRECT lprcMon, LPARAM lParam)
{
    (void)hdc; (void)lprcMon; (void)lParam;
    if (g_hostCount >= MAX_MONITORS) return TRUE;

    MONITORINFO mi = { sizeof(mi) };
    if (!GetMonitorInfoW(hMon, &mi)) return TRUE;

    HWND h = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        WND_CLASS_NAME, APP_TITLE,
        WS_POPUP | WS_VISIBLE,
        mi.rcMonitor.left, mi.rcMonitor.top,
        mi.rcMonitor.right  - mi.rcMonitor.left,
        mi.rcMonitor.bottom - mi.rcMonitor.top,
        NULL, NULL, GetModuleHandleW(NULL), NULL);

    if (h) {
        g_hosts[g_hostCount++] = h;
        SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    return TRUE;
}

/* ------------------------------------------------------------------ */

static BOOL ResolveEnginePath(LPWSTR out, size_t out_chars)
{
    WCHAR exePath[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, exePath, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return FALSE;

    WCHAR *p = wcsrchr(exePath, L'\\');
    if (!p) return FALSE;
    *p = 0;

    int written = swprintf(out, out_chars, L"%ls\\%ls",
                           exePath, ENGINE_FILENAME);
    if (written < 0 || (size_t)written >= out_chars) return FALSE;
    return TRUE;
}

static HANDLE SpawnEngine(const wchar_t *enginePath, const wchar_t *args)
{
    WCHAR cmdLine[MAX_PATH * 2 + 64];
    swprintf(cmdLine, _countof(cmdLine), L"\"%ls\" %ls",
             enginePath, args ? args : L"");

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWNORMAL;
    PROCESS_INFORMATION pi = { 0 };

    if (!CreateProcessW(enginePath, cmdLine, NULL, NULL, FALSE,
        0, NULL, NULL, &si, &pi)) {
        return NULL;
    }
    CloseHandle(pi.hThread);
    return pi.hProcess;
}

static void EnableDpiAwareness(void)
{
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (!u32) return;
    PFN_SetProcessDpiAwarenessContext pSetCtx =
        (PFN_SetProcessDpiAwarenessContext)
        GetProcAddress(u32, "SetProcessDpiAwarenessContext");
    if (pSetCtx) {
        if (pSetCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
    }
    SetProcessDPIAware();
}

/* ------------------------------------------------------------------ */

static int RunFullscreen(const wchar_t *enginePath)
{
    /* Singleton */
    HANDLE mutex = CreateMutexW(NULL, FALSE, MUTEX_NAME);
    if (!mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 0;
    }

    EnableDpiAwareness();

    GetCursorPos(&g_startMouse);
    g_startedTick = GetTickCount();

    /* Window class */
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = SaverWndProc;
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.hCursor       = NULL;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = WND_CLASS_NAME;

    if (!RegisterClassExW(&wc)) goto cleanup;

    /* Hide cursor (counter-based, may need multiple decrements). */
    while (ShowCursor(FALSE) >= 0) { /* loop until counter < 0 */ }

    /* Create one host per monitor */
    EnumDisplayMonitors(NULL, NULL, EnumMonitorsProc, 0);
    if (g_hostCount == 0) goto cleanup;

    /* Spawn an engine for each host */
    for (int i = 0; i < g_hostCount; i++) {
        WCHAR args[64];
        swprintf(args, _countof(args), L"/p %llu",
                 (unsigned long long)(uintptr_t)g_hosts[i]);
        HANDLE h = SpawnEngine(enginePath, args);
        if (h && g_engineCount < MAX_MONITORS) {
            g_engineHandles[g_engineCount++] = h;
        }
    }

    /* Low-level hooks */
    HINSTANCE hInst = GetModuleHandleW(NULL);
    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL,    MouseLLProc, hInst, 0);
    g_kbHook    = SetWindowsHookExW(WH_KEYBOARD_LL, KbLLProc,    hInst, 0);

    /* Message loop */
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (g_exiting) break;
    }

cleanup:
    if (g_mouseHook) { UnhookWindowsHookEx(g_mouseHook); g_mouseHook = NULL; }
    if (g_kbHook)    { UnhookWindowsHookEx(g_kbHook);    g_kbHook    = NULL; }

    /* Kill engines */
    for (int i = 0; i < g_engineCount; i++) {
        TerminateProcess(g_engineHandles[i], 0);
    }
    if (g_engineCount > 0) {
        WaitForMultipleObjects((DWORD)g_engineCount,
                               g_engineHandles, TRUE, 1500);
    }
    for (int i = 0; i < g_engineCount; i++) {
        CloseHandle(g_engineHandles[i]);
    }

    /* Destroy hosts */
    for (int i = 0; i < g_hostCount; i++) {
        if (IsWindow(g_hosts[i])) DestroyWindow(g_hosts[i]);
    }

    while (ShowCursor(TRUE) < 0) { /* restore counter */ }

    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}

static int RunPreview(const wchar_t *enginePath, const wchar_t *hwndArg)
{
    WCHAR args[64];
    swprintf(args, _countof(args), L"/p %ls",
             (hwndArg && *hwndArg) ? hwndArg : L"0");
    HANDLE h = SpawnEngine(enginePath, args);
    if (!h) return 1;
    WaitForSingleObject(h, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(h, &code);
    CloseHandle(h);
    return (int)code;
}

static int RunConfig(const wchar_t *enginePath, const wchar_t *hwndArg)
{
    WCHAR args[64];
    if (hwndArg && *hwndArg) {
        swprintf(args, _countof(args), L"/c:%ls", hwndArg);
    } else {
        wcscpy(args, L"/c");
    }
    HANDLE h = SpawnEngine(enginePath, args);
    if (!h) return 1;
    WaitForSingleObject(h, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(h, &code);
    CloseHandle(h);
    return (int)code;
}

static void ShowError(LPCWSTR text)
{
    MessageBoxW(NULL, text, APP_TITLE L" (wrapper)",
                MB_ICONERROR | MB_OK | MB_SETFOREGROUND);
}

/* ------------------------------------------------------------------ */

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE hPrev,
                      LPWSTR lpCmdLine, int nShow)
{
    (void)hInst; (void)hPrev; (void)lpCmdLine; (void)nShow;

    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    int  mode  = 0;          /* 0=config, 1=fullscreen, 2=preview */
    const wchar_t *param = NULL;

    if (argc >= 2) {
        const wchar_t *a = argv[1];
        if ((a[0] == L'/' || a[0] == L'-') && a[1]) {
            wchar_t c = a[1];
            if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');

            if (c == L's') {
                mode = 1;
            } else if (c == L'p') {
                mode = 2;
                if (a[2] == L':' || a[2] == L' ')      param = &a[3];
                else if (a[2] != 0)                     param = &a[2];
                else if (argc >= 3)                     param = argv[2];
            } else if (c == L'c') {
                mode = 0;
                if (a[2] == L':')                       param = &a[3];
                else if (a[2] != 0)                     param = &a[2];
                else if (argc >= 3)                     param = argv[2];
            }
        }
    }

    WCHAR enginePath[MAX_PATH];
    if (!ResolveEnginePath(enginePath, MAX_PATH)) {
        ShowError(L"No se puede determinar la ruta del wrapper.");
        if (argv) LocalFree(argv);
        return 2;
    }

    DWORD attrs = GetFileAttributesW(enginePath);
    if (attrs == INVALID_FILE_ATTRIBUTES ||
        (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        WCHAR msg[MAX_PATH + 256];
        swprintf(msg, _countof(msg),
            L"No se encuentra el motor del salvapantallas:\n%ls\n\n"
            L"El wrapper espera un archivo llamado '" ENGINE_FILENAME
            L"' en su misma carpeta.", enginePath);
        ShowError(msg);
        if (argv) LocalFree(argv);
        return 2;
    }

    int rc;
    switch (mode) {
    case 1:  rc = RunFullscreen(enginePath);          break;
    case 2:  rc = RunPreview(enginePath, param);      break;
    default: rc = RunConfig(enginePath, param);       break;
    }

    if (argv) LocalFree(argv);
    return rc;
}
