#include "app.h"

#include <stdio.h>

static const wchar_t OVERLAY_CLASS[] = L"ChatGIBotNativeLogOverlay";
static HFONT g_overlay_font;

static int overlay_scale(HWND hwnd, int value) {
    UINT dpi = 96;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
    GetDpiForWindowFn get_dpi = user32 ? (GetDpiForWindowFn)GetProcAddress(user32, "GetDpiForWindow") : NULL;
    if (get_dpi && hwnd) dpi = get_dpi(hwnd);
    return max(1, (value * (int)dpi + 48) / 96);
}

static BOOL overlay_game_is_active(HWND game, HWND overlay) {
    HWND foreground = GetForegroundWindow();
    if (foreground == game || foreground == overlay) return TRUE;
    return foreground && GetAncestor(foreground, GA_ROOT) == game;
}

static void overlay_position(App *app, BOOL show) {
    HWND overlay = app ? app->overlay_hwnd : NULL;
    if (!overlay) return;
    if (!show || InterlockedCompareExchange(&app->overlay_shutdown, 0, 0) ||
        !app->config.log_overlay_enabled || !InterlockedCompareExchange(&app->running, 0, 0)) {
        ShowWindow(overlay, SW_HIDE);
        return;
    }
    HWND game = find_game_window(app->config.window_title);
    if (!game) game = find_game_window(L"原神");
    RECT rect;
    BOOL usable = game && IsWindowVisible(game) && !IsIconic(game) && GetWindowRect(game, &rect);
    if (!usable || !overlay_game_is_active(game, overlay)) {
        ShowWindow(overlay, SW_HIDE);
        return;
    }
    int width = overlay_scale(app->hwnd, 430);
    int height = overlay_scale(app->hwnd, 205);
    int margin_x = overlay_scale(app->hwnd, 22);
    int margin_y = overlay_scale(app->hwnd, 30);
    int x = max(rect.left, rect.right - width - margin_x);
    int y = max(rect.top, rect.bottom - height - margin_y);
    SetWindowPos(overlay, HWND_TOPMOST, x, y, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static int overlay_collect_lines(wchar_t output[][256], int max_lines) {
    wchar_t snapshot[65536];
    EnterCriticalSection(&g_app.log_lock);
    wcscpy_s(snapshot, 65536, g_app.log_text);
    LeaveCriticalSection(&g_app.log_lock);
    int count = 0;
    wchar_t *end = snapshot + wcslen(snapshot);
    while (end > snapshot && count < max_lines) {
        wchar_t *start = end;
        while (start > snapshot && start[-1] != L'\n') --start;
        while (end > start && (end[-1] == L'\r' || end[-1] == L'\n')) --end;
        if (end > start) {
            size_t length = min((size_t)(end - start), (size_t)255);
            wcsncpy_s(output[count], 256, start, length);
            output[count][length] = 0;
            ++count;
        }
        if (start == snapshot) break;
        end = start - 1;
    }
    for (int left = 0, right = count - 1; left < right; ++left, --right) {
        wchar_t swap[256];
        wcscpy_s(swap, 256, output[left]);
        wcscpy_s(output[left], 256, output[right]);
        wcscpy_s(output[right], 256, swap);
    }
    return count;
}

static COLORREF overlay_line_color(const wchar_t *line) {
    if (wcsstr(line, L"[ERROR]")) return RGB(248, 113, 113);
    if (wcsstr(line, L"[WARNING]")) return RGB(250, 204, 21);
    if (wcsstr(line, L"[INFO]")) return RGB(203, 213, 225);
    return RGB(203, 213, 225);
}

static LRESULT CALLBACK overlay_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    (void)wparam;
    switch (message) {
        case WM_NCHITTEST: return HTTRANSPARENT;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_TIMER:
            if (lparam == 0 || wparam == 1) {
                if (!InterlockedCompareExchange(&g_app.overlay_shutdown, 0, 0) &&
                    InterlockedCompareExchange(&g_app.running, 0, 0)) {
                    overlay_position(&g_app, TRUE);
                    InvalidateRect(hwnd, NULL, FALSE);
                } else {
                    ShowWindow(hwnd, SW_HIDE);
                }
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(hwnd, &paint);
            RECT rect;
            GetClientRect(hwnd, &rect);
            HBRUSH background = CreateSolidBrush(RGB(11, 15, 23));
            FillRect(dc, &rect, background);
            DeleteObject(background);
            HPEN border = CreatePen(PS_SOLID, overlay_scale(g_app.hwnd, 1), RGB(76, 194, 255));
            HGDIOBJ old_pen = SelectObject(dc, border);
            HGDIOBJ old_brush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
            RoundRect(dc, 0, 0, rect.right - 1, rect.bottom - 1,
                      overlay_scale(g_app.hwnd, 10), overlay_scale(g_app.hwnd, 10));
            SelectObject(dc, old_brush);
            SelectObject(dc, old_pen);
            DeleteObject(border);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(76, 194, 255));
            SelectObject(dc, g_overlay_font);
            RECT header = { overlay_scale(g_app.hwnd, 16), overlay_scale(g_app.hwnd, 10),
                            rect.right - overlay_scale(g_app.hwnd, 16), overlay_scale(g_app.hwnd, 32) };
            DrawTextW(dc, L"ChatGIBot 运行日志", -1, &header, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            wchar_t lines[8][256];
            int count = overlay_collect_lines(lines, 8);
            int line_height = overlay_scale(g_app.hwnd, 20);
            for (int i = 0; i < count; ++i) {
                RECT line_rect = { overlay_scale(g_app.hwnd, 16), overlay_scale(g_app.hwnd, 38) + i * line_height,
                                   rect.right - overlay_scale(g_app.hwnd, 12), overlay_scale(g_app.hwnd, 38) + (i + 1) * line_height };
                SetTextColor(dc, overlay_line_color(lines[i]));
                DrawTextW(dc, lines[i], -1, &line_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            if (g_overlay_font) {
                DeleteObject(g_overlay_font);
                g_overlay_font = NULL;
            }
            if (g_app.overlay_hwnd == hwnd) g_app.overlay_hwnd = NULL;
            return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

BOOL overlay_init(App *app) {
    if (!app || app->overlay_hwnd) return app && app->overlay_hwnd;
    WNDCLASSW window_class = {0};
    window_class.lpfnWndProc = overlay_window_proc;
    window_class.hInstance = app->instance;
    window_class.lpszClassName = OVERLAY_CLASS;
    window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
    window_class.hbrBackground = NULL;
    RegisterClassW(&window_class);
    g_overlay_font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH, L"Microsoft YaHei UI");
    app->overlay_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        OVERLAY_CLASS, L"ChatGIBot 运行日志", WS_POPUP,
        0, 0, overlay_scale(app->hwnd, 430), overlay_scale(app->hwnd, 205),
        app->hwnd, NULL, app->instance, NULL);
    if (!app->overlay_hwnd) return FALSE;
    SetLayeredWindowAttributes(app->overlay_hwnd, 0, 225, LWA_ALPHA);
    SetTimer(app->overlay_hwnd, 1, 100, NULL);
    ShowWindow(app->overlay_hwnd, SW_HIDE);
    return TRUE;
}

void overlay_show(App *app) {
    if (!app || !app->overlay_hwnd || InterlockedCompareExchange(&app->overlay_shutdown, 0, 0) ||
        !app->config.log_overlay_enabled || !InterlockedCompareExchange(&app->running, 0, 0)) return;
    overlay_position(app, TRUE);
    InvalidateRect(app->overlay_hwnd, NULL, FALSE);
}

void overlay_hide(App *app) {
    if (app && app->overlay_hwnd) {
        ShowWindow(app->overlay_hwnd, SW_HIDE);
        SetWindowPos(app->overlay_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_HIDEWINDOW);
    }
}

void overlay_destroy(App *app) {
    if (!app) return;
    InterlockedExchange(&app->overlay_shutdown, 1);
    if (app->overlay_hwnd) {
        KillTimer(app->overlay_hwnd, 1);
        overlay_hide(app);
        DestroyWindow(app->overlay_hwnd);
        app->overlay_hwnd = NULL;
    }
}
