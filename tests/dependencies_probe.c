#include <stdio.h>
#include "../src/app.h"

void ui_append_log(const wchar_t *level, const wchar_t *message) { (void)level; (void)message; }
void ui_refresh_dependency_controls(void) {}
void ui_set_busy(int busy) { (void)busy; }

static BOOL wait_for_idle(DWORD timeout_ms) {
    ULONGLONG start = GetTickCount64();
    while (InterlockedCompareExchange(&g_app.busy, 0, 0)) {
        if (GetTickCount64() - start >= timeout_ms) return FALSE;
        Sleep(50);
    }
    return TRUE;
}

static DWORD WINAPI cancel_thread(LPVOID parameter) {
    DWORD delay = (DWORD)(ULONG_PTR)parameter;
    ULONGLONG start = GetTickCount64();
    while (!InterlockedCompareExchange(&g_app.download_active, 0, 0) && GetTickCount64() - start < 5000) Sleep(10);
    Sleep(delay);
    dependency_cancel(&g_app);
    return 0;
}

int wmain(int argc, wchar_t **argv) {
    InitializeCriticalSection(&g_app.log_lock);
    config_defaults(&g_app.config);
    BOOL directml = FALSE, expect_cancel = FALSE;
    DWORD cancel_after = 0;
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--directml") == 0) directml = TRUE;
        else if (wcsncmp(argv[i], L"--cancel-after=", 15) == 0) {
            expect_cancel = TRUE;
            cancel_after = (DWORD)_wtoi(argv[i] + 15);
        } else if (wcsncmp(argv[i], L"--tier=", 7) == 0)
            wcsncpy_s(g_app.config.model_tier, 32, argv[i] + 7, _TRUNCATE);
    }
    if (directml) wcscpy_s(g_app.config.inference_device, 32, L"directml");
    dependency_init(&g_app);
    BOOL started = TRUE;
    if (argc > 1 && wcscmp(argv[1], L"--install-runtime") == 0)
        started = dependency_install_runtime_async(&g_app);
    else if (argc > 1 && wcscmp(argv[1], L"--repair-models") == 0)
        started = dependency_repair_models_async(&g_app);
    else
        dependency_detect_async(&g_app);
    HANDLE cancel = expect_cancel ? CreateThread(NULL, 0, cancel_thread, (LPVOID)(ULONG_PTR)cancel_after, 0, NULL) : NULL;
    BOOL idle = started && wait_for_idle(240000);
    if (cancel) { WaitForSingleObject(cancel, 10000); CloseHandle(cancel); }
    int failures = idle ? 0 : 1;
    for (int i = 0; i < g_app.dep_count; ++i)
        wprintf(L"%s state=%d %s\n", g_app.deps[i].title, g_app.deps[i].state, g_app.deps[i].detail);
    if (argc > 1 && wcscmp(argv[1], L"--install-runtime") == 0) {
        if (expect_cancel) {
            if (!g_app.cancel_requested || g_app.deps[0].state == DEP_OK) ++failures;
        } else if (g_app.deps[0].state != DEP_OK) ++failures;
    }
    if (argc > 1 && wcscmp(argv[1], L"--repair-models") == 0 && g_app.deps[2].state != DEP_OK) ++failures;
    DeleteCriticalSection(&g_app.log_lock);
    return failures;
}
