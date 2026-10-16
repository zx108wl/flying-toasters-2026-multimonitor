/*
 * MultiSaver - per-monitor .scr launcher, derived from Flying Toasters 2026.
 *
 * Build with scripts/build.ps1; see docs/BUILD.md.
 *
 * Behavior summary:
 *   /s          : enumerate every monitor, create one borderless WS_POPUP
 *                 host window per display, spawn the engine in /p HWND
 *                 mode against each.
 *   /p HWND     : preview DefaultSaver in the supplied parent window.
 *   /c[:HWND]   : generate monitor sections and edit multisaver.ini.
 *   (no args)   : same as /c.
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

#define INI_FILENAME       L"multisaver.ini"
#define APP_TITLE          L"MultiSaver"
#define MUTEX_NAME         L"Local\\MultiSaver_Singleton"
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

static WCHAR g_directory[MAX_PATH], g_ini[MAX_PATH];
static WCHAR g_paths[MAX_MONITORS][MAX_PATH];
static HANDLE g_job;
static int g_cursorHidden;
static int g_result = 1;
static HWND              g_hosts[MAX_MONITORS];

/* Device interface names survive changes to GDI DISPLAY numbers. Do not
 * silently fall back to an ordinal when the driver exposes no identity. */
static BOOL MonitorId(HMONITOR monitor, WCHAR *id, size_t count)
{
    MONITORINFOEXW mi = {0};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, (MONITORINFO *)&mi)) return FALSE;
    DISPLAY_DEVICEW device = {0};
    device.cb = sizeof(device);
    for (DWORD i = 0; EnumDisplayDevicesW(mi.szDevice, i, &device,
                                         EDD_GET_DEVICE_INTERFACE_NAME); ++i) {
        if ((device.StateFlags & DISPLAY_DEVICE_ACTIVE) && device.DeviceID[0]) {
            return swprintf(id, count, L"Monitor:%ls", device.DeviceID) > 0;
        }
        ZeroMemory(&device, sizeof(device));
        device.cb = sizeof(device);
    }
    return FALSE;
}

static BOOL SaverPath(LPCWSTR section, WCHAR *result)
{
    WCHAR out[MAX_PATH];
    result[0] = 0;
    WCHAR value[MAX_PATH], fallback[MAX_PATH], expanded[MAX_PATH], combined[MAX_PATH * 2];
    GetPrivateProfileStringW(L"Launcher", L"DefaultSaver", L"", value,
                             MAX_PATH, g_ini);
    wcscpy(fallback, value);
    if (section) GetPrivateProfileStringW(section, L"Saver", fallback, value,
                                          MAX_PATH, g_ini);
    if (!value[0]) wcscpy(value, fallback);
    if (!value[0] || !_wcsicmp(value, L"none")) return FALSE;
    DWORD n = ExpandEnvironmentStringsW(value, expanded, MAX_PATH);
    if (!n || n > MAX_PATH || wcschr(expanded, L'"')) return FALSE;
    if (expanded[0] == L'\\' || (expanded[0] && expanded[1] == L':')) {
        wcscpy(combined, expanded);
    } else {
        if (swprintf(combined, _countof(combined), L"%ls\\%ls",
                     g_directory, expanded) < 0) return FALSE;
    }
    n = GetFullPathNameW(combined, MAX_PATH, out, NULL);
    if (!n || n >= MAX_PATH) return FALSE;
    WCHAR *ext = wcsrchr(out, L'.');
    if (!ext || _wcsicmp(ext, L".scr")) return FALSE;
    DWORD attr = GetFileAttributesW(out);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
        return FALSE;
    /* Reject self, including hard links and symbolic links. */
    WCHAR self[MAX_PATH];
    if (!GetModuleFileNameW(NULL, self, MAX_PATH)) return FALSE;
    HANDLE a = CreateFileW(out, 0, FILE_SHARE_READ | FILE_SHARE_WRITE |
                          FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    HANDLE b = CreateFileW(self, 0, FILE_SHARE_READ | FILE_SHARE_WRITE |
                          FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    BY_HANDLE_FILE_INFORMATION ai, bi;
    BOOL valid = a != INVALID_HANDLE_VALUE && b != INVALID_HANDLE_VALUE &&
        GetFileInformationByHandle(a, &ai) && GetFileInformationByHandle(b, &bi);
    if (valid && ai.dwVolumeSerialNumber == bi.dwVolumeSerialNumber &&
        ai.nFileIndexHigh == bi.nFileIndexHigh &&
        ai.nFileIndexLow == bi.nFileIndexLow) valid = FALSE;
    if (a != INVALID_HANDLE_VALUE) CloseHandle(a);
    if (b != INVALID_HANDLE_VALUE) CloseHandle(b);
    if (valid) wcscpy(result, out);
    return valid;
}
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
        case WM_MOUSEHWHEEL:
        case WM_MOUSEWHEEL:
        case WM_XBUTTONDOWN:
            TriggerExit();
            break;
        case WM_MOUSEMOVE: {
            DWORD now = GetTickCount();
            if (now - g_startedTick < INITIAL_QUIET_MS) break;
            MSLLHOOKSTRUCT *m = (MSLLHOOKSTRUCT *)lParam;
            LONGLONG dx = (LONGLONG)m->pt.x - g_startMouse.x;
            LONGLONG dy = (LONGLONG)m->pt.y - g_startMouse.y;
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
    case WM_MOUSEHWHEEL:
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
    case WM_DISPLAYCHANGE:
    case WM_CLOSE:
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

    WCHAR id[160];
    if (MonitorId(hMon, id, _countof(id)))
        SaverPath(id, g_paths[g_hostCount]);
    else
        SaverPath(NULL, g_paths[g_hostCount]);
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

static BOOL InitPaths(void)
{
    DWORD n = GetModuleFileNameW(NULL, g_directory, MAX_PATH);
    if (!n || n >= MAX_PATH) return FALSE;
    WCHAR *slash = wcsrchr(g_directory, L'\\');
    if (!slash) return FALSE;
    *slash = 0;
    int written = swprintf(g_ini, MAX_PATH, L"%ls\\%ls", g_directory, INI_FILENAME);
    return written > 0 && written < MAX_PATH;
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
        g_job ? CREATE_SUSPENDED : 0, NULL, g_directory, &si, &pi)) {
        return NULL;
    }
    if (g_job) {
        if (!AssignProcessToJobObject(g_job, pi.hProcess) ||
            ResumeThread(pi.hThread) == (DWORD)-1) {
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return NULL;
        }
    }
    CloseHandle(pi.hThread);
    return pi.hProcess;
}

static void EnableDpiAwareness(void)
{
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (!u32) return;
    union {
        FARPROC generic;
        PFN_SetProcessDpiAwarenessContext typed;
    } proc;
    proc.generic = GetProcAddress(u32, "SetProcessDpiAwarenessContext");
    if (proc.typed) {
        if (proc.typed(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
    }
    SetProcessDPIAware();
}

static BOOL CreateChildJob(void)
{
    g_job = CreateJobObjectW(NULL, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    return g_job && SetInformationJobObject(g_job, JobObjectExtendedLimitInformation,
                                           &limits, sizeof(limits));
}

/* ------------------------------------------------------------------ */

static int RunFullscreen(void)
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

    if (!CreateChildJob()) goto cleanup;

    /* Hide cursor (counter-based, may need multiple decrements). */
    while (ShowCursor(FALSE) >= 0) { /* loop until counter < 0 */ }
    g_cursorHidden = 1;

    /* Create one host per monitor */
    EnumDisplayMonitors(NULL, NULL, EnumMonitorsProc, 0);
    if (g_hostCount == 0) goto cleanup;

    /* Spawn an engine for each host */
    for (int i = 0; i < g_hostCount; i++) {
        WCHAR args[64];
        swprintf(args, _countof(args), L"/p %llu",
                 (unsigned long long)(uintptr_t)g_hosts[i]);
        HANDLE h = g_paths[i][0] ? SpawnEngine(g_paths[i], args) : NULL;
        if (h && g_engineCount < MAX_MONITORS) {
            g_engineHandles[g_engineCount++] = h;
        }
    }

    /* Low-level hooks */
    HINSTANCE hInst = GetModuleHandleW(NULL);
    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL,    MouseLLProc, hInst, 0);
    g_kbHook    = SetWindowsHookExW(WH_KEYBOARD_LL, KbLLProc,    hInst, 0);

    if (!g_mouseHook || !g_kbHook) goto cleanup;
    g_result = 0;

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

    if (g_job) { CloseHandle(g_job); g_job = NULL; }

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

    if (g_cursorHidden) while (ShowCursor(TRUE) < 0) { /* restore counter */ }


    CloseHandle(mutex);
    return g_result;
}

static int RunPreview(const wchar_t *enginePath, const wchar_t *hwndArg)
{
    EnableDpiAwareness();
    if (!CreateChildJob()) {
        if (g_job) { CloseHandle(g_job); g_job = NULL; }
        return 1;
    }
    WCHAR args[64];
    swprintf(args, _countof(args), L"/p %ls",
             (hwndArg && *hwndArg) ? hwndArg : L"0");
    HANDLE h = SpawnEngine(enginePath, args);
    if (!h) { CloseHandle(g_job); g_job = NULL; return 1; }
    HWND parent = (HWND)(uintptr_t)wcstoull(hwndArg, NULL, 10);
    while (WaitForSingleObject(h, 100) == WAIT_TIMEOUT) {
        if (!IsWindow(parent)) break;
    }
    CloseHandle(g_job);
    g_job = NULL;
    WaitForSingleObject(h, 1500);
    DWORD code = 0;
    GetExitCodeProcess(h, &code);
    CloseHandle(h);
    return (int)code;
}

static BOOL CALLBACK AddMonitorConfig(HMONITOR mon, HDC dc, LPRECT rect, LPARAM arg)
{
    (void)dc; (void)rect; (void)arg;
    WCHAR id[160], value[MAX_PATH];
    if (MonitorId(mon, id, _countof(id))) {
        GetPrivateProfileStringW(id, L"Saver", L"<missing>", value, MAX_PATH, g_ini);
        if (!wcscmp(value, L"<missing>"))
            if (!WritePrivateProfileStringW(id, L"Saver", L"", g_ini)) return FALSE;
        MONITORINFOEXW mi = {0};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(mon, (MONITORINFO *)&mi)) {
            swprintf(value, MAX_PATH, L"%ls: %ldx%ld at (%ld,%ld)%ls", mi.szDevice,
                mi.rcMonitor.right - mi.rcMonitor.left,
                mi.rcMonitor.bottom - mi.rcMonitor.top,
                mi.rcMonitor.left, mi.rcMonitor.top,
                (mi.dwFlags & MONITORINFOF_PRIMARY) ? L" primary" : L"");
            if (!WritePrivateProfileStringW(id, L"Info", value, g_ini)) return FALSE;
        }
    }
    return TRUE;
}

static int RunConfig(void)
{
    /* Seed a UTF-16 INI so non-ASCII paths round-trip through profile APIs. */
    if (GetFileAttributesW(g_ini) == INVALID_FILE_ATTRIBUTES) {
        HANDLE f = CreateFileW(g_ini, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                               CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f == INVALID_HANDLE_VALUE) return 2;
        static const WCHAR initial[] = L"\xFEFF; MultiSaver: blank Saver inherits DefaultSaver; none is black.\r\n[Launcher]\r\nDefaultSaver=\r\n";
        DWORD written;
        BOOL ok = WriteFile(f, initial, sizeof(initial) - sizeof(WCHAR), &written, NULL);
        CloseHandle(f);
        if (!ok || written != sizeof(initial) - sizeof(WCHAR)) return 2;
    }
    if (!EnumDisplayMonitors(NULL, NULL, AddMonitorConfig, 0)) return 2;
    WCHAR editor[MAX_PATH];
    if (!GetSystemDirectoryW(editor, MAX_PATH - 13)) return 2;
    wcscat(editor, L"\\notepad.exe");
    WCHAR args[MAX_PATH + 4];
    swprintf(args, _countof(args), L"\"%ls\"", g_ini);
    return (INT_PTR)ShellExecuteW(NULL, L"open", editor, args, g_directory,
                                  SW_SHOWNORMAL) > 32 ? 0 : 2;
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
    if (!InitPaths()) {
        if (argv) LocalFree(argv);
        return 2;
    }
    if (mode == 2 && !SaverPath(NULL, enginePath)) {
        if (argv) LocalFree(argv);
        return 2;
    }
    if (mode == 2) {
        WCHAR *end = NULL;
        unsigned long long handle = param ? wcstoull(param, &end, 10) : 0;
        if (!handle || !end || *end || handle > UINTPTR_MAX ||
            !IsWindow((HWND)(uintptr_t)handle)) {
            if (argv) LocalFree(argv);
            return 2;
        }
    }
    int rc;
    switch (mode) {
    case 1:  rc = RunFullscreen();          break;
    case 2:  rc = RunPreview(enginePath, param);      break;
    default:
        rc = RunConfig();
        if (rc) MessageBoxW(NULL,
            L"Could not create or edit multisaver.ini. Keep the launcher in a writable folder.",
            APP_TITLE, MB_OK | MB_ICONERROR);
        break;
    }

    if (argv) LocalFree(argv);
    return rc;
}
