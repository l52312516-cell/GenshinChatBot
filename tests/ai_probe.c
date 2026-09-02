#include <stdio.h>
#include "../src/app.h"

void ui_append_log(const wchar_t *level, const wchar_t *message) { (void)level; wprintf(L"%s\n", message); }
void ui_refresh_dependency_controls(void) {}
void ui_set_busy(int busy) { (void)busy; }

int wmain(void) {
    config_defaults(&g_app.config);
    wcscpy_s(g_app.config.ai_base_url, 256, L"http://127.0.0.1:18080/v1?token=query-ok");
    wcscpy_s(g_app.config.ai_model, 128, L"mock-model");
    wcscpy_s(g_app.config.ai_key, 256, L"test-key");
    InitializeCriticalSection(&g_app.log_lock);
    wchar_t reply[2048] = L"";
    BOOL ok = ai_chat(&g_app, L"测试请求", reply, 2048);
    wchar_t first_user[512], first_assistant[512];
    wcscpy_s(first_user, 512, g_app.history_user[0]);
    wcscpy_s(first_assistant, 512, g_app.history_assistant[0]);
    BOOL test_started = ai_test_async(&g_app);
    ULONGLONG start = GetTickCount64();
    while (test_started && InterlockedCompareExchange(&g_app.busy, 0, 0) && GetTickCount64() - start < 10000) Sleep(10);
    BOOL history_unchanged = test_started && g_app.history_count == 1 &&
        wcscmp(g_app.history_user[0], first_user) == 0 &&
        wcscmp(g_app.history_assistant[0], first_assistant) == 0;
    DeleteCriticalSection(&g_app.log_lock);
    wprintf(L"ok=%d reply=%s history=%d test_history_unchanged=%d\n",
            ok, reply, g_app.history_count, history_unchanged);
    return ok && wcscmp(reply, L"连接成功") == 0 && history_unchanged ? 0 : 1;
}
