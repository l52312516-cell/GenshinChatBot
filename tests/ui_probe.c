#include <stdio.h>
#include <windows.h>
#include "../src/app.h"

typedef struct { int visible_pages; HWND log; } Probe;
typedef struct { int id; HWND found; } ControlProbe;

static BOOL CALLBACK child_proc(HWND hwnd, LPARAM parameter) {
    Probe *probe = (Probe*)parameter;
    wchar_t class_name[64] = L"";
    GetClassNameW(hwnd, class_name, 64);
    if (wcscmp(class_name, L"ChatGIBotNativePage") == 0 && IsWindowVisible(hwnd)) ++probe->visible_pages;
    if (GetDlgCtrlID(hwnd) == IDC_CONSOLE_LOG) probe->log = hwnd;
    return TRUE;
}

static BOOL CALLBACK control_proc(HWND hwnd, LPARAM parameter) {
    ControlProbe *probe = (ControlProbe*)parameter;
    if (GetDlgCtrlID(hwnd) == probe->id) { probe->found = hwnd; return FALSE; }
    return TRUE;
}

static HWND find_control(HWND parent, int id) {
    ControlProbe probe = { id, NULL };
    EnumChildWindows(parent, control_proc, (LPARAM)&probe);
    return probe.found;
}

int wmain(int argc, wchar_t **argv) {
    HWND main_window = FindWindowW(APP_CLASS, NULL);
    if (!main_window) { puts("FAIL no main window"); return 1; }
    int failures = 0;
    SetWindowPos(main_window, NULL, 0, 0, 800, 500, SWP_NOMOVE | SWP_NOZORDER);
    RECT window_rect = {0};
    GetWindowRect(main_window, &window_rect);
    if (window_rect.right - window_rect.left < 1180 || window_rect.bottom - window_rect.top < 740) {
        puts("FAIL minimum window size"); ++failures;
    } else puts("minimum_window_size=PASS");
    for (int page = 0; page < PAGE_COUNT; ++page) {
        SendMessageW(main_window, WM_COMMAND, IDC_NAV_BASE + page, 0);
        Probe probe = {0}; EnumChildWindows(main_window, child_proc, (LPARAM)&probe);
        printf("page %d visible=%d %s\n", page, probe.visible_pages, probe.visible_pages == 1 ? "PASS" : "FAIL");
        if (probe.visible_pages != 1) ++failures;
    }
    SendMessageW(main_window, WM_COMMAND, IDC_NAV_BASE + PAGE_CONSOLE, 0);
    Probe probe = {0}; EnumChildWindows(main_window, child_proc, (LPARAM)&probe);
    printf("console_log_control=%s\n", probe.log ? "PASS" : "FAIL");
    SendMessageW(main_window, WM_COMMAND, IDC_NAV_BASE + PAGE_AI, 0);
    if (!find_control(main_window, IDC_AI_TEMPERATURE) || !find_control(main_window, IDC_VISION_AI_BASE)) {
        puts("FAIL extended AI controls"); ++failures;
    } else puts("extended_ai_controls=PASS");
    HWND provider = find_control(main_window, IDC_AI_PROVIDER);
    HWND base = find_control(main_window, IDC_AI_BASE);
    HWND model = find_control(main_window, IDC_AI_MODEL);
    LRESULT selected_index = SendMessageW(provider, CB_SETCURSEL, 4, 0);
    LRESULT command_result = SendMessageW(main_window, WM_COMMAND, MAKEWPARAM(IDC_AI_PROVIDER, CBN_SELCHANGE), (LPARAM)provider);
    (void)selected_index;
    (void)command_result;
    wchar_t provider_base[256] = L"", provider_model[128] = L"";
    GetWindowTextW(base, provider_base, 256); GetWindowTextW(model, provider_model, 128);
    if (!provider || !base || !model) { puts("FAIL AI provider controls"); ++failures; }
    else puts("ai_provider_controls=PASS");
    SendMessageW(main_window, WM_COMMAND, IDC_BTN_SAVE_AI, 0);
    SendMessageW(main_window, WM_COMMAND, IDC_NAV_BASE + PAGE_VISION, 0);
    HWND capture_mode = find_control(main_window, IDC_CAPTURE_MODE);
    HWND device_mode = find_control(main_window, IDC_DEVICE_MODE);
    HWND engine_mode = find_control(main_window, IDC_ENGINE_MODE);
    HWND model_tier = find_control(main_window, IDC_MODEL_TIER);
    SendMessageW(capture_mode, CB_SETCURSEL, 2, 0);
    SendMessageW(device_mode, CB_SETCURSEL, 1, 0);
    SendMessageW(engine_mode, CB_SETCURSEL, 1, 0);
    SendMessageW(model_tier, CB_SETCURSEL, 0, 0);
    SendMessageW(main_window, WM_COMMAND, IDC_BTN_SAVE_VISION, 0);
    if (!capture_mode || !device_mode || !engine_mode || !model_tier) {
        puts("FAIL vision settings controls"); ++failures;
    } else puts("vision_settings_controls=PASS");
    SendMessageW(main_window, WM_COMMAND, IDC_NAV_BASE + PAGE_ROBOT, 0);
    if (!find_control(main_window, IDC_WAKE_WORD) || !find_control(main_window, IDC_HISTORY_TURNS) ||
        !find_control(main_window, IDC_OVERLAY_ENABLED)) {
        puts("FAIL extended robot controls"); ++failures;
    } else puts("extended_robot_controls=PASS");
    SendMessageW(main_window, WM_COMMAND, IDC_NAV_BASE + PAGE_PLUGIN, 0);
    if (!find_control(main_window, IDC_PLUGIN_ENABLED) ||
        !find_control(main_window, IDC_MUSIC_SEARCH_TIMEOUT) ||
        !find_control(main_window, IDC_MUSIC_QUEUE_LIMIT)) {
        puts("FAIL music plugin controls"); ++failures;
    } else puts("music_plugin_controls=PASS");
    SendMessageW(main_window, WM_COMMAND, IDC_NAV_BASE + PAGE_DEPS, 0);
    if (!find_control(main_window, IDC_DEP_PROGRESS) || !find_control(main_window, IDC_BTN_DEP_CANCEL)) {
        puts("FAIL dependency progress controls"); ++failures;
    } else puts("dependency_progress_controls=PASS");
    HWND download_source = find_control(main_window, IDC_DOWNLOAD_SOURCE);
    if (!download_source || !find_control(main_window, IDC_BTN_TEST_MIRRORS)) {
        puts("FAIL download source controls"); ++failures;
    } else {
        SendMessageW(download_source, CB_SETCURSEL, 1, 0);
        SendMessageW(main_window, WM_COMMAND, MAKEWPARAM(IDC_DOWNLOAD_SOURCE, CBN_SELCHANGE), (LPARAM)download_source);
        puts("download_source_controls=PASS");
    }
    if (argc > 1 && wcscmp(argv[1], L"--close") == 0) {
        SendMessageW(main_window, WM_CLOSE, 0, 0);
        for (int i = 0; i < 400 && IsWindow(main_window); ++i) Sleep(50);
        if (IsWindow(main_window)) { puts("FAIL graceful close"); ++failures; }
        else puts("graceful_close=PASS");
    }
    return failures + (probe.log ? 0 : 1);
}
