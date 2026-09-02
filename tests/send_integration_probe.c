#include <stdio.h>
#include "../src/app.h"

static const wchar_t *TEST_CLASS = L"ChatGIBotSendIntegrationWindow";
static const wchar_t *TEST_TITLE = L"ChatGIBot Send Integration Test";
static HANDLE ready_event;
static HWND target_window;
static HWND target_edit;
static HWND guard_window;
static UINT extra_format;

void ui_append_log(const wchar_t *level, const wchar_t *message) { (void)level; (void)message; }
void ui_refresh_dependency_controls(void) {}
void ui_set_busy(int busy) { (void)busy; }

static LRESULT CALLBACK test_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static DWORD WINAPI ui_thread(LPVOID parameter) {
    (void)parameter;
    HINSTANCE instance = GetModuleHandleW(NULL);
    WNDCLASSW window_class = {0};
    window_class.lpfnWndProc = test_window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = TEST_CLASS;
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    RegisterClassW(&window_class);
    target_window = CreateWindowExW(WS_EX_APPWINDOW, TEST_CLASS, TEST_TITLE,
        WS_OVERLAPPEDWINDOW, 100, 100, 520, 240, NULL, NULL, instance, NULL);
    target_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, 20, 20, 460, 130,
        target_window, NULL, instance, NULL);
    guard_window = CreateWindowExW(WS_EX_APPWINDOW, TEST_CLASS, L"ChatGIBot Guard Window",
        WS_OVERLAPPEDWINDOW, 650, 100, 320, 180, NULL, NULL, instance, NULL);
    ShowWindow(target_window, SW_SHOW);
    SetForegroundWindow(target_window);
    SetFocus(target_edit);
    SetEvent(ready_event);
    MSG message;
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}

static BOOL set_clipboard_text(HWND owner, const wchar_t *text) {
    if (!OpenClipboard(owner)) return FALSE;
    EmptyClipboard();
    SIZE_T bytes = (wcslen(text) + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    BOOL ok = FALSE;
    if (memory) {
        void *locked = GlobalLock(memory);
        if (locked) {
            memcpy(locked, text, bytes);
            GlobalUnlock(memory);
            ok = SetClipboardData(CF_UNICODETEXT, memory) != NULL;
        }
        if (!ok) GlobalFree(memory);
    }
    CloseClipboard();
    return ok;
}

static BOOL set_clipboard_text_with_extra(HWND owner, const wchar_t *text, const char *extra) {
    if (!set_clipboard_text(owner, text)) return FALSE;
    if (!OpenClipboard(owner)) return FALSE;
    extra_format = RegisterClipboardFormatW(L"ChatGIBot.SendProbe.NonText");
    SIZE_T bytes = strlen(extra) + 1;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    BOOL ok = FALSE;
    if (memory) {
        void *locked = GlobalLock(memory);
        if (locked) {
            memcpy(locked, extra, bytes);
            GlobalUnlock(memory);
            ok = SetClipboardData(extra_format, memory) != NULL;
        }
        if (!ok) GlobalFree(memory);
    }
    CloseClipboard();
    return ok;
}

static BOOL clipboard_equals(const wchar_t *expected) {
    if (!OpenClipboard(NULL)) return FALSE;
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    const wchar_t *text = data ? (const wchar_t*)GlobalLock(data) : NULL;
    BOOL equal = text && wcscmp(text, expected) == 0;
    if (text) GlobalUnlock(data);
    CloseClipboard();
    return equal;
}

static BOOL clipboard_extra_equals(const char *expected) {
    if (!extra_format || !OpenClipboard(NULL)) return FALSE;
    HANDLE data = GetClipboardData(extra_format);
    SIZE_T bytes = data ? GlobalSize(data) : 0;
    const char *value = data ? (const char*)GlobalLock(data) : NULL;
    BOOL equal = value && bytes >= strlen(expected) + 1 && memcmp(value, expected, strlen(expected) + 1) == 0;
    if (value) GlobalUnlock(data);
    CloseClipboard();
    return equal;
}

static void pump_wait(DWORD milliseconds) {
    Sleep(milliseconds);
}

int wmain(void) {
    int failures = 0;
    ready_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE thread = CreateThread(NULL, 0, ui_thread, NULL, 0, NULL);
    if (!ready_event || !thread || WaitForSingleObject(ready_event, 5000) != WAIT_OBJECT_0) {
        puts("FAIL create integration window");
        return 2;
    }
    InitializeCriticalSection(&g_app.log_lock);
    config_defaults(&g_app.config);
    wcscpy_s(g_app.config.window_title, 128, TEST_TITLE);
    g_app.hwnd = target_window;
    const wchar_t *original = L"原剪贴板内容-ChatGIBot";
    const wchar_t *outgoing = L"中文发送测试-ChatGIBot";
    set_clipboard_text_with_extra(target_window, original, "non-text clipboard payload");
    SetForegroundWindow(target_window);
    SetFocus(target_edit);
    pump_wait(150);
    GUITHREADINFO gui = { sizeof(gui) };
    GetGUIThreadInfo(GetWindowThreadProcessId(target_window, NULL), &gui);
    wprintf(L"send setup foreground=%p focus=%p active=%p edit=%p\n", (void*)GetForegroundWindow(),
            (void*)gui.hwndFocus, (void*)gui.hwndActive, (void*)target_edit);
    BOOL sent = send_text_to_foreground(outgoing);
    pump_wait(250);
    wchar_t edit_text[1024] = L"";
    GetWindowTextW(target_edit, edit_text, 1024);
    if (!sent || !wcsstr(edit_text, outgoing)) {
        puts("FAIL SendInput text delivery");
        ++failures;
    } else puts("PASS SendInput text delivery");
    if (!clipboard_equals(original)) {
        puts("FAIL clipboard restoration");
        ++failures;
    } else puts("PASS clipboard restoration");
    if (!clipboard_extra_equals("non-text clipboard payload")) {
        puts("FAIL non-text clipboard restoration");
        ++failures;
    } else puts("PASS non-text clipboard restoration");

    SetWindowTextW(target_edit, L"");
    ShowWindow(guard_window, SW_SHOW);
    SetForegroundWindow(guard_window);
    pump_wait(150);
    BOOL background_sent = send_text_to_foreground(L"不应发送");
    pump_wait(100);
    GetWindowTextW(target_edit, edit_text, 1024);
    if (background_sent || edit_text[0]) {
        puts("FAIL background send guard");
        ++failures;
    } else puts("PASS background send guard");
    if (!clipboard_equals(original)) {
        puts("FAIL clipboard unchanged after blocked send");
        ++failures;
    } else puts("PASS clipboard unchanged after blocked send");

    DWORD thread_id = GetThreadId(thread);
    PostThreadMessageW(thread_id, WM_QUIT, 0, 0);
    WaitForSingleObject(thread, 3000);
    CloseHandle(thread);
    CloseHandle(ready_event);
    DeleteCriticalSection(&g_app.log_lock);
    return failures;
}
