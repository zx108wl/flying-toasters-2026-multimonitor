/* Run in build/test-work, never in the source or user's saver directory. */
#include "../src/wrapper.c"
#include <assert.h>

static void Put(LPCWSTR section, LPCWSTR key, LPCWSTR value)
{
    assert(WritePrivateProfileStringW(section, key, value, g_ini));
}

int wmain(int argc, wchar_t **argv)
{
    /* Copies of this test program act as predictable preview children. */
    if (argc > 1 && !wcscmp(argv[1], L"/p")) {
        Sleep(INFINITE);
        return 0;
    }
    assert(InitPaths());
    WCHAR self[MAX_PATH], child[MAX_PATH], path[MAX_PATH], link[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    swprintf(child, MAX_PATH, L"%ls\\fixture space \x5C4F.scr", g_directory);
    swprintf(link, MAX_PATH, L"%ls\\self-link.scr", g_directory);
    DeleteFileW(g_ini);
    HANDLE ini = CreateFileW(g_ini, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    assert(ini != INVALID_HANDLE_VALUE);
    WCHAR bom = 0xfeff;
    DWORD written;
    assert(WriteFile(ini, &bom, sizeof(bom), &written, NULL));
    CloseHandle(ini);
    assert(CopyFileW(self, child, FALSE));
    Put(L"Launcher", L"DefaultSaver", child);
    assert(SaverPath(NULL, path) && !wcscmp(path, child));
    assert(SaverPath(L"Monitor:test", path) && !wcscmp(path, child));
    Put(L"Monitor:test", L"Saver", L"none");
    assert(!SaverPath(L"Monitor:test", path) && !path[0]);
    Put(L"Monitor:test", L"Saver", L"");
    assert(SaverPath(L"Monitor:test", path) && !wcscmp(path, child));
    Put(L"Monitor:test", L"Saver", L"fixture space \x5C4F.scr");
    assert(SaverPath(L"Monitor:test", path) && !wcscmp(path, child));
    SetEnvironmentVariableW(L"MULTISAVER_TEST_CHILD", child);
    Put(L"Monitor:test", L"Saver", L"%MULTISAVER_TEST_CHILD%");
    assert(SaverPath(L"Monitor:test", path) && !wcscmp(path, child));
    Put(L"Monitor:test", L"Saver", L"missing.scr");
    assert(!SaverPath(L"Monitor:test", path) && !path[0]);
    Put(L"Monitor:test", L"Saver", self);
    assert(!SaverPath(L"Monitor:test", path) && !path[0]);
    DeleteFileW(link);
    assert(CreateHardLinkW(link, self, NULL));
    Put(L"Monitor:test", L"Saver", link);
    assert(!SaverPath(L"Monitor:test", path) && !path[0]);
    Put(L"Monitor:test", L"Saver", g_directory);
    assert(!SaverPath(L"Monitor:test", path) && !path[0]);
    Put(L"Launcher", L"DefaultSaver", L"");
    assert(!SaverPath(NULL, path) && !path[0]);

    /* Both preview children must die when the job is closed. */
    assert(CreateChildJob());
    HANDLE first = SpawnEngine(child, L"/p 123");
    HANDLE second = SpawnEngine(child, L"/p 456");
    assert(first && second);
    assert(WaitForSingleObject(first, 200) == WAIT_TIMEOUT);
    assert(WaitForSingleObject(second, 200) == WAIT_TIMEOUT);
    CloseHandle(g_job);
    g_job = NULL;
    assert(WaitForSingleObject(first, 3000) == WAIT_OBJECT_0);
    assert(WaitForSingleObject(second, 3000) == WAIT_OBJECT_0);
    CloseHandle(first);
    CloseHandle(second);

    /* Input grace period and dismissal without installing global hooks. */
    MSLLHOOKSTRUCT mouse = {0};
    g_startedTick = GetTickCount();
    mouse.pt.x = 100;
    MouseLLProc(HC_ACTION, WM_MOUSEMOVE, (LPARAM)&mouse);
    assert(!g_exiting);
    g_startedTick -= INITIAL_QUIET_MS + 1;
    MouseLLProc(HC_ACTION, WM_MOUSEMOVE, (LPARAM)&mouse);
    assert(g_exiting);
    g_exiting = 0;
    KbLLProc(HC_ACTION, WM_KEYDOWN, 0);
    assert(g_exiting);
    g_exiting = 0;
    MouseLLProc(HC_ACTION, WM_MOUSEHWHEEL, (LPARAM)&mouse);
    assert(g_exiting);

    DeleteFileW(link);
    DeleteFileW(child);
    DeleteFileW(g_ini);
    puts("PASS: configuration, path validation, job cleanup, input dismissal");
    return 0;
}
