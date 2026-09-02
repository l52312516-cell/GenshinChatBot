#include "app.h"
#include <commctrl.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <shellapi.h>
#include <stdio.h>
#include <uxtheme.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "uxtheme.lib")

#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

const wchar_t *NAV_LABELS[PAGE_COUNT] = { L"控制台", L"识图", L"AI", L"机器人", L"依赖", L"插件", L"关于" };
HWND g_pages[PAGE_COUNT];
HWND g_console_log;
HWND g_region_preview;
HWND g_nav_buttons[PAGE_COUNT];
int g_current_page;
HFONT g_font, g_title_font, g_mono_font, g_icon_font;
HBRUSH g_background_brush, g_card_brush, g_input_brush;
COLORREF g_background = RGB(9, 13, 20), g_card = RGB(18, 26, 39), g_input = RGB(12, 19, 29);
COLORREF g_accent = RGB(56, 189, 248), g_text = RGB(229, 237, 247), g_muted = RGB(148, 163, 184);
COLORREF g_accent_hi = RGB(96, 208, 252), g_accent_dark = RGB(37, 150, 210);
COLORREF g_success = RGB(74, 222, 128), g_warning = RGB(250, 204, 21), g_error = RGB(248, 113, 113);

static BOOL g_overlay_check = FALSE;
static BOOL g_plugin_check = FALSE;

BOOL ui_checkbox_checked(int id) {
    if (id == IDC_OVERLAY_ENABLED) return g_overlay_check;
    if (id == IDC_PLUGIN_ENABLED) return g_plugin_check;
    return FALSE;
}

void ui_checkbox_set(int id, BOOL checked) {
    if (id == IDC_OVERLAY_ENABLED) g_overlay_check = checked;
    else if (id == IDC_PLUGIN_ENABLED) g_plugin_check = checked;
}

static void finish_region_drag(HWND preview_control, POINT point);
static LRESULT CALLBACK region_preview_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
void draw_dependency_page(HWND page, HDC dc);

static const wchar_t *download_source_label(const wchar_t *source) {
    if (source && wcscmp(source, L"modelscope") == 0) return L"ModelScope（国内）";
    if (source && wcscmp(source, L"hf-mirror") == 0) return L"HF-Mirror（国内）";
    if (source && wcscmp(source, L"official") == 0) return L"官方源";
    if (source && wcscmp(source, L"custom") == 0) return L"自定义环境变量";
    return L"自动选择（国内优先）";
}

static HWND create_label(HWND parent, const wchar_t *text, int x, int y, int width, int height) {
    HWND control = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, width, height, parent, NULL, g_app.instance, NULL);
    SendMessageW(control, WM_SETFONT, (WPARAM)g_font, TRUE);
    return control;
}

static LRESULT CALLBACK page_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_COMMAND:
        case WM_DRAWITEM:
        case WM_MEASUREITEM:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLORLISTBOX:
            return SendMessageW(g_app.hwnd, message, wparam, lparam);
        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(hwnd, &paint);
            RECT rect;
            GetClientRect(hwnd, &rect);
            HDC buffer_dc = CreateCompatibleDC(dc);
            HBITMAP buffer_bitmap = CreateCompatibleBitmap(dc, rect.right, rect.bottom);
            HDC draw_dc = dc;
            HBITMAP buffer_old = NULL;
            if (buffer_dc && buffer_bitmap) {
                buffer_old = SelectObject(buffer_dc, buffer_bitmap);
                draw_dc = buffer_dc;
            }
            HBRUSH brush = g_background_brush ? g_background_brush : CreateSolidBrush(g_background);
            FillRect(draw_dc, &rect, brush);
            if (brush != g_background_brush) DeleteObject(brush);
            RECT underline = { 28, 61, 232, 64 };
            HBRUSH underline_brush = CreateSolidBrush(g_accent);
            FillRect(draw_dc, &underline, underline_brush);
            DeleteObject(underline_brush);
            if (hwnd == g_pages[PAGE_DEPS]) {
                draw_dependency_page(hwnd, draw_dc);
            }
            if (draw_dc != dc) {
                BitBlt(dc, 0, 0, rect.right, rect.bottom, draw_dc, 0, 0, SRCCOPY);
                SelectObject(buffer_dc, buffer_old);
            }
            if (buffer_bitmap) DeleteObject(buffer_bitmap);
            if (buffer_dc) DeleteDC(buffer_dc);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static void draw_region_preview(HWND hwnd, HDC dc) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    HBRUSH background = CreateSolidBrush(RGB(7, 10, 16));
    FillRect(dc, &rect, background);
    DeleteObject(background);

    EnterCriticalSection(&g_app.log_lock);
    HBITMAP bitmap = g_app.preview_bitmap;
    BOOL selecting = g_app.region_selecting;
    BOOL dragging = g_app.region_dragging;
    POINT drag_start = g_app.region_drag_start;
    POINT drag_current = g_app.region_drag_current;
    int source_width = g_app.preview_source_width;
    int source_height = g_app.preview_source_height;
    if (bitmap) {
        BITMAP info;
        if (GetObjectW(bitmap, sizeof(info), &info)) {
            HDC source = CreateCompatibleDC(dc);
            if (source) {
                HGDIOBJ old = SelectObject(source, bitmap);
                SetStretchBltMode(dc, HALFTONE);
                StretchBlt(dc, 1, 1, max(1, rect.right - 2), max(1, rect.bottom - 2),
                           source, 0, 0, info.bmWidth, abs(info.bmHeight), SRCCOPY);
                SelectObject(source, old);
                DeleteDC(source);
            }
        }
    }
    LeaveCriticalSection(&g_app.log_lock);

    if (bitmap && selecting) {
        RECT selected = { drag_start.x, drag_start.y, drag_current.x, drag_current.y };
        if (!dragging && source_width > 0 && source_height > 0 &&
            g_app.config.region[2] > g_app.config.region[0] &&
            g_app.config.region[3] > g_app.config.region[1]) {
            selected.left = MulDiv(g_app.config.region[0], rect.right, source_width);
            selected.top = MulDiv(g_app.config.region[1], rect.bottom, source_height);
            selected.right = MulDiv(g_app.config.region[2], rect.right, source_width);
            selected.bottom = MulDiv(g_app.config.region[3], rect.bottom, source_height);
        }
        if (dragging) {
            selected.left = max(0, min(rect.right, selected.left));
            selected.right = max(0, min(rect.right, selected.right));
            selected.top = max(0, min(rect.bottom, selected.top));
            selected.bottom = max(0, min(rect.bottom, selected.bottom));
        }
        if (selected.left > selected.right) { int value = selected.left; selected.left = selected.right; selected.right = value; }
        if (selected.top > selected.bottom) { int value = selected.top; selected.top = selected.bottom; selected.bottom = value; }
        if (selected.right > selected.left && selected.bottom > selected.top) {
            HPEN pen = CreatePen(PS_SOLID, 2, g_accent);
            HGDIOBJ old_pen = SelectObject(dc, pen);
            HGDIOBJ old_brush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
            Rectangle(dc, selected.left, selected.top, selected.right, selected.bottom);
            SelectObject(dc, old_brush);
            SelectObject(dc, old_pen);
            DeleteObject(pen);
        }
    }
    HPEN border_pen = CreatePen(PS_SOLID, 1, RGB(58, 70, 88));
    HGDIOBJ old_pen = SelectObject(dc, border_pen);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(dc, 0, 0, rect.right, rect.bottom);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(border_pen);
}

static LRESULT CALLBACK region_preview_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(hwnd, &paint);
            RECT rect;
            GetClientRect(hwnd, &rect);
            HDC buffer_dc = CreateCompatibleDC(dc);
            HBITMAP buffer_bitmap = rect.right > 0 && rect.bottom > 0
                ? CreateCompatibleBitmap(dc, rect.right, rect.bottom) : NULL;
            if (buffer_dc && buffer_bitmap) {
                HGDIOBJ old = SelectObject(buffer_dc, buffer_bitmap);
                draw_region_preview(hwnd, buffer_dc);
                BitBlt(dc, 0, 0, rect.right, rect.bottom, buffer_dc, 0, 0, SRCCOPY);
                SelectObject(buffer_dc, old);
            } else {
                draw_region_preview(hwnd, dc);
            }
            if (buffer_bitmap) DeleteObject(buffer_bitmap);
            if (buffer_dc) DeleteDC(buffer_dc);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_LBUTTONDOWN:
            if (g_app.region_selecting) {
                POINT point = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
                RECT rect;
                GetClientRect(hwnd, &rect);
                if (point.x >= 0 && point.y >= 0 && point.x < rect.right && point.y < rect.bottom) {
                    g_app.region_dragging = TRUE;
                    g_app.region_drag_start = point;
                    g_app.region_drag_current = point;
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }
            break;
        case WM_MOUSEMOVE:
            if (g_app.region_selecting && g_app.region_dragging) {
                RECT rect;
                GetClientRect(hwnd, &rect);
                POINT point = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
                point.x = max(0, min(rect.right, point.x));
                point.y = max(0, min(rect.bottom, point.y));
                g_app.region_drag_current = point;
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            if (g_app.region_selecting && g_app.region_dragging) {
                POINT point = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
                g_app.region_drag_current = point;
                g_app.region_dragging = FALSE;
                if (GetCapture() == hwnd) ReleaseCapture();
                finish_region_drag(hwnd, point);
                return 0;
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(NULL, g_app.region_selecting ? IDC_CROSS : IDC_ARROW));
            return TRUE;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static void register_region_preview_class(void) {
    static BOOL registered;
    if (registered) return;
    WNDCLASSW window_class = {0};
    window_class.lpfnWndProc = region_preview_proc;
    window_class.hInstance = g_app.instance;
    window_class.lpszClassName = L"ChatGIBotRegionPreview";
    window_class.hbrBackground = NULL;
    if (RegisterClassW(&window_class) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
        registered = TRUE;
}

static void register_page_class(void) {
    static BOOL registered;
    if (registered) return;
    WNDCLASSW window_class = {0};
    window_class.lpfnWndProc = page_proc;
    window_class.hInstance = g_app.instance;
    window_class.lpszClassName = L"ChatGIBotNativePage";
    window_class.hbrBackground = NULL;
    ATOM atom = RegisterClassW(&window_class);
    if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        app_log_format(L"ERROR", L"页面窗口类注册失败：%lu", GetLastError());
        return;
    }
    registered = TRUE;
}

static HWND create_edit(HWND parent, int id, int x, int y, int width, int height, DWORD extra_style) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | extra_style;
    if (!(extra_style & ES_MULTILINE)) style |= ES_AUTOHSCROLL;
    HWND control = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", style, x, y, width, height, parent, (HMENU)(INT_PTR)id, g_app.instance, NULL);
    SendMessageW(control, WM_SETFONT, (WPARAM)g_font, TRUE);
    SetWindowTheme(control, L"DarkMode_CFD", NULL);
    SendMessageW(control, EM_SETLIMITTEXT, 0, 0);
    return control;
}

static LRESULT CALLBACK combo_field_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR uidsub, DWORD_PTR ref) {
    (void)uidsub; (void)ref;
    if (msg == WM_NCPAINT) {
        LRESULT result = DefSubclassProc(hwnd, msg, wparam, lparam);
        if (GetFocus() == hwnd) {
            HDC dc = GetWindowDC(hwnd);
            RECT rc; GetWindowRect(hwnd, &rc);
            int w = rc.right - rc.left, h = rc.bottom - rc.top;
            HPEN pen = CreatePen(PS_SOLID, 2, g_accent);
            HGDIOBJ old_pen = SelectObject(dc, pen);
            HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, 1, 1, w - 1, h - 1);
            SelectObject(dc, old_brush);
            SelectObject(dc, old_pen);
            DeleteObject(pen);
            ReleaseDC(hwnd, dc);
        }
        return result;
    }
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

static HWND create_combo(HWND parent, int id, int x, int y, int width, int height) {
    HWND control = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST |
        WS_VSCROLL, x, y, width, height + 120,
        parent, (HMENU)(INT_PTR)id, g_app.instance, NULL);
    SendMessageW(control, WM_SETFONT, (WPARAM)g_font, TRUE);
    SetWindowTheme(control, L"DarkMode_Explorer", NULL);
    SetWindowSubclass(control, combo_field_proc, 0, 0);
    return control;
}

static BOOL get_combo_text(HWND parent, int id, wchar_t *text, int text_chars) {
    if (!text || text_chars <= 0) return FALSE;
    text[0] = 0;
    HWND combo = GetDlgItem(parent, id);
    int selection = combo ? (int)SendMessageW(combo, CB_GETCURSEL, 0, 0) : -1;
    if (selection < 0) return FALSE;
    return SendMessageW(combo, CB_GETLBTEXT, (WPARAM)selection, (LPARAM)text) >= 0;
}

static HWND create_button(HWND parent, const wchar_t *text, int id, int x, int y, int width, int height) {
    HWND control = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, x, y, width, height, parent, (HMENU)(INT_PTR)id, g_app.instance, NULL);
    SendMessageW(control, WM_SETFONT, (WPARAM)g_font, TRUE);
    SetWindowSubclass(control, od_button_proc, 0, 0);
    return control;
}

static HWND create_page(const wchar_t *title_text) {
    register_page_class();
    HWND page = CreateWindowExW(WS_EX_CONTROLPARENT, L"ChatGIBotNativePage", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 230, 95, 930, 600, g_app.hwnd, NULL, g_app.instance, NULL);
    if (!page) {
        app_log_format(L"ERROR", L"页面创建失败：%s，错误 %lu", title_text, GetLastError());
        return NULL;
    }
    HWND title_control = create_label(page, title_text, 28, 20, 600, 38);
    if (!title_control) app_log_format(L"ERROR", L"页面标题创建失败：%s，错误 %lu", title_text, GetLastError());
    else SendMessageW(title_control, WM_SETFONT, (WPARAM)g_title_font, TRUE);
    return page;
}

void build_console_page(void) {
    g_pages[PAGE_CONSOLE] = create_page(L"控制台");
    HWND page = g_pages[PAGE_CONSOLE];
    HWND log = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY, 28, 70, 855, 360, page, (HMENU)IDC_CONSOLE_LOG, g_app.instance, NULL);
    g_console_log = log;
    SendMessageW(log, WM_SETFONT, (WPARAM)g_mono_font, TRUE);
    SetWindowTheme(log, L"DarkMode_Explorer", NULL);
    create_button(page, L"启动", IDC_BTN_START, 28, 455, 125, 45);
    create_button(page, L"暂停", IDC_BTN_PAUSE, 168, 455, 125, 45);
    create_button(page, L"停止", IDC_BTN_STOP, 308, 455, 125, 45);
    create_button(page, L"捕获测试", IDC_BTN_CAPTURE, 448, 455, 125, 45);
    create_button(page, L"OCR 测试", IDC_BTN_OCR, 588, 455, 125, 45);
    create_button(page, L"清空日志", IDC_BTN_CLEAR, 743, 455, 140, 45);
}

static void populate_ocr_threads_combo(HWND combo) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    int cores = max(2, system_logical_processor_count());
    for (int value = 2; value <= 8 && value <= cores; value += 2) {
        wchar_t text[8];
        _itow(value, text, 10);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)text);
    }
}

void build_vision_page(void) {
    register_region_preview_class();
    g_pages[PAGE_VISION] = create_page(L"识图引擎");
    HWND page = g_pages[PAGE_VISION];
    int y = 75;
    create_label(page, L"截图模式", 28, y + 6, 160, 24);
    HWND mode = create_combo(page, IDC_CAPTURE_MODE, 205, y, 220, 32);
    SendMessageW(mode, CB_ADDSTRING, 0, (LPARAM)L"BitBlt");
    SendMessageW(mode, CB_ADDSTRING, 0, (LPARAM)L"PrintWindow");
    SendMessageW(mode, CB_ADDSTRING, 0, (LPARAM)L"Desktop Duplication（备用）");
    create_label(page, L"推理设备", 28, y + 48, 160, 24);
    HWND device = create_combo(page, IDC_DEVICE_MODE, 205, y + 42, 220, 32);
    SendMessageW(device, CB_ADDSTRING, 0, (LPARAM)L"CPU");
    SendMessageW(device, CB_ADDSTRING, 0, (LPARAM)L"DirectML");
    create_label(page, L"识图引擎", 28, y + 90, 160, 24);
    HWND engine = create_combo(page, IDC_ENGINE_MODE, 205, y + 84, 220, 32);
    SendMessageW(engine, CB_ADDSTRING, 0, (LPARAM)L"本地 PP-OCRv6");
    SendMessageW(engine, CB_ADDSTRING, 0, (LPARAM)L"自定义视觉 AI");
    create_label(page, L"模型档位", 28, y + 132, 160, 24);
    HWND tier = create_combo(page, IDC_MODEL_TIER, 205, y + 126, 220, 32);
    SendMessageW(tier, CB_ADDSTRING, 0, (LPARAM)L"tiny");
    SendMessageW(tier, CB_ADDSTRING, 0, (LPARAM)L"small");
    SendMessageW(tier, CB_ADDSTRING, 0, (LPARAM)L"medium");
    create_label(page, L"聊天区域 X1/Y1/X2/Y2", 28, y + 176, 180, 24);
    create_edit(page, IDC_REGION_X, 205, y + 172, 65, 32, ES_NUMBER);
    create_edit(page, IDC_REGION_Y, 280, y + 172, 65, 32, ES_NUMBER);
    create_edit(page, IDC_REGION_W, 355, y + 172, 65, 32, ES_NUMBER);
    create_edit(page, IDC_REGION_H, 430, y + 172, 65, 32, ES_NUMBER);
    create_label(page, L"置信度阈值 %", 28, y + 220, 160, 24);
    create_edit(page, IDC_THRESHOLD, 205, y + 216, 100, 32, ES_NUMBER);
    create_label(page, L"OCR 识别间隔毫秒", 28, y + 264, 160, 24);
    create_edit(page, IDC_OCR_INTERVAL, 205, y + 260, 100, 32, ES_NUMBER);
    wchar_t threads_label[160];
    _snwprintf_s(threads_label, _countof(threads_label), _TRUNCATE,
                 L"OCR线程数(%d)", system_logical_processor_count());
    create_label(page, threads_label, 320, y + 264, 120, 24);
    HWND ocr_threads = create_combo(page, IDC_OCR_THREADS, 440, y + 258, 75, 32);
    populate_ocr_threads_combo(ocr_threads);
    create_label(page, L"视觉 AI 提示词", 28, y + 308, 160, 24);
    create_edit(page, IDC_VISION_PROMPT, 205, y + 304, 650, 32, 0);
    g_region_preview = CreateWindowExW(0, L"ChatGIBotRegionPreview", L"",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                       520, y, 335, 225, page, NULL, g_app.instance, NULL);
    create_label(page, L"识别结果（文本 / 坐标 / 置信度 / 引擎）", 28, y + 350, 420, 24);
    HWND preview = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"尚未测试", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY, 28, y + 380, 835, 66, page, (HMENU)IDC_OCR_PREVIEW, g_app.instance, NULL);
    SendMessageW(preview, WM_SETFONT, (WPARAM)g_mono_font, TRUE);
    SetWindowTheme(preview, L"DarkMode_Explorer", NULL);
    create_button(page, L"测试捕获", IDC_BTN_TEST_CAPTURE, 28, 530, 145, 44);
    create_button(page, L"测试 OCR", IDC_BTN_TEST_OCR, 188, 530, 145, 44);
    create_button(page, L"框选区域", IDC_BTN_SELECT_REGION, 348, 530, 145, 44);
    create_button(page, L"保存识图", IDC_BTN_SAVE_VISION, 508, 530, 145, 44);
}

void build_ai_page(void) {
    g_pages[PAGE_AI] = create_page(L"OpenAI 兼容 AI");
    HWND page = g_pages[PAGE_AI];
    create_label(page, L"聊天 AI", 28, 68, 220, 24);
    create_label(page, L"服务商预设", 28, 104, 150, 24);
    HWND provider = create_combo(page, IDC_AI_PROVIDER, 190, 99, 260, 32);
    SendMessageW(provider, CB_ADDSTRING, 0, (LPARAM)L"DeepSeek");
    SendMessageW(provider, CB_ADDSTRING, 0, (LPARAM)L"智谱");
    SendMessageW(provider, CB_ADDSTRING, 0, (LPARAM)L"OpenAI");
    SendMessageW(provider, CB_ADDSTRING, 0, (LPARAM)L"Moonshot");
    SendMessageW(provider, CB_ADDSTRING, 0, (LPARAM)L"Ollama");
    SendMessageW(provider, CB_ADDSTRING, 0, (LPARAM)L"自定义");
    create_label(page, L"Base URL", 28, 146, 150, 24);
    create_edit(page, IDC_AI_BASE, 190, 141, 260, 32, 0);
    create_label(page, L"模型", 28, 188, 150, 24);
    create_edit(page, IDC_AI_MODEL, 190, 183, 260, 32, 0);
    create_label(page, L"Key 环境变量", 28, 230, 150, 24);
    create_edit(page, IDC_AI_KEY_ENV, 190, 225, 260, 32, 0);
    create_label(page, L"兼容 API Key", 28, 272, 150, 24);
    HWND key = create_edit(page, IDC_AI_KEY, 190, 267, 260, 32, ES_PASSWORD);
    SendMessageW(key, EM_SETCUEBANNER, TRUE, (LPARAM)L"建议留空，使用环境变量");
    create_label(page, L"Temperature", 28, 314, 150, 24);
    create_edit(page, IDC_AI_TEMPERATURE, 190, 309, 100, 32, 0);
    create_label(page, L"Max Tokens", 28, 356, 150, 24);
    create_edit(page, IDC_AI_MAX_TOKENS, 190, 351, 100, 32, ES_NUMBER);
    create_label(page, L"人设", 28, 398, 150, 24);
    create_edit(page, IDC_AI_PERSONALITY, 190, 393, 260, 92, ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL);

    create_label(page, L"视觉 AI", 500, 68, 220, 24);
    create_label(page, L"Base URL", 500, 104, 140, 24);
    create_edit(page, IDC_VISION_AI_BASE, 645, 99, 230, 32, 0);
    create_label(page, L"模型", 500, 146, 140, 24);
    create_edit(page, IDC_VISION_AI_MODEL, 645, 141, 230, 32, 0);
    create_label(page, L"Key 环境变量", 500, 188, 140, 24);
    create_edit(page, IDC_VISION_AI_KEY_ENV, 645, 183, 230, 32, 0);
    create_label(page, L"兼容 API Key", 500, 230, 140, 24);
    HWND vision_key = create_edit(page, IDC_VISION_AI_KEY, 645, 225, 230, 32, ES_PASSWORD);
    SendMessageW(vision_key, EM_SETCUEBANNER, TRUE, (LPARAM)L"建议使用环境变量");
    create_button(page, L"测试 AI", IDC_BTN_AI_TEST, 500, 437, 150, 48);
    create_button(page, L"保存 AI", IDC_BTN_SAVE_AI, 665, 437, 150, 48);
}

void build_robot_page(void) {
    g_pages[PAGE_ROBOT] = create_page(L"机器人");
    HWND page = g_pages[PAGE_ROBOT];
    create_label(page, L"游戏窗口标题", 28, 80, 160, 24);
    create_edit(page, IDC_GAME_TITLE, 205, 75, 300, 32, 0);
    create_label(page, L"触发间隔毫秒", 28, 122, 160, 24);
    create_edit(page, IDC_TRIGGER_MS, 205, 117, 120, 32, ES_NUMBER);
    create_label(page, L"发言间隔毫秒", 28, 164, 160, 24);
    create_edit(page, IDC_SEND_INTERVAL, 205, 159, 120, 32, ES_NUMBER);
    create_label(page, L"最低 600", 340, 164, 100, 24);
    create_label(page, L"单条最大字数", 28, 206, 160, 24);
    create_edit(page, IDC_MAX_CHARS, 205, 201, 120, 32, ES_NUMBER);
    create_label(page, L"唤醒词", 28, 248, 160, 24);
    create_edit(page, IDC_WAKE_WORD, 205, 243, 300, 32, 0);
    create_label(page, L"历史轮数", 28, 290, 160, 24);
    create_edit(page, IDC_HISTORY_TURNS, 205, 285, 120, 32, ES_NUMBER);
    HWND overlay = CreateWindowExW(0, L"BUTTON", L"游戏内日志遮罩（右下角）",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                   28, 342, 260, 28, page, (HMENU)IDC_OVERLAY_ENABLED, g_app.instance, NULL);
    SendMessageW(overlay, WM_SETFONT, (WPARAM)g_font, TRUE);
    SetWindowTheme(overlay, L"", L"");
    create_button(page, L"保存机器人", IDC_BTN_SAVE_ROBOT, 28, 470, 170, 48);
}

void build_deps_page(void) {
    g_pages[PAGE_DEPS] = create_page(L"依赖与环境");
    HWND page = g_pages[PAGE_DEPS];
    create_label(page, L"下载源", 28, 66, 150, 24);
    HWND source = create_combo(page, IDC_DOWNLOAD_SOURCE, 190, 61, 300, 32);
    SendMessageW(source, CB_ADDSTRING, 0, (LPARAM)L"自动选择（国内优先）");
    SendMessageW(source, CB_ADDSTRING, 0, (LPARAM)L"ModelScope（国内）");
    SendMessageW(source, CB_ADDSTRING, 0, (LPARAM)L"HF-Mirror（国内）");
    SendMessageW(source, CB_ADDSTRING, 0, (LPARAM)L"官方源");
    SendMessageW(source, CB_ADDSTRING, 0, (LPARAM)L"自定义环境变量");
    create_label(page, L"可选国内镜像；测速结果写入控制台日志", 520, 66, 360, 24);
    HWND progress = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                                    28, 516, 832, 16, page, (HMENU)IDC_DEP_PROGRESS, g_app.instance, NULL);
    SendMessageW(progress, PBM_SETRANGE32, 0, 100);
    SendMessageW(progress, PBM_SETBARCOLOR, 0, (LPARAM)g_accent);
    SendMessageW(progress, PBM_SETBKCOLOR, 0, (LPARAM)RGB(20, 26, 36));
    SetWindowTheme(progress, L"", L"");
    create_button(page, L"重新检测", IDC_BTN_DEP_REFRESH, 28, 556, 110, 46);
    create_button(page, L"安装 / 修复运行库", IDC_BTN_DEP_INSTALL, 148, 556, 170, 46);
    create_button(page, L"校验并修复模型", IDC_BTN_DEP_MODELS, 328, 556, 160, 46);
    HWND cancel = create_button(page, L"取消下载", IDC_BTN_DEP_CANCEL, 498, 556, 100, 46);
    EnableWindow(cancel, FALSE);
    create_button(page, L"测试镜像", IDC_BTN_TEST_MIRRORS, 608, 556, 120, 46);
    create_button(page, L"打开日志", IDC_BTN_DEP_LOG, 738, 556, 120, 46);
}

void build_plugin_page(void) {
    g_pages[PAGE_PLUGIN] = create_page(L"插件");
    HWND page = g_pages[PAGE_PLUGIN];
    create_label(page, L"LxMusic 路径", 28, 80, 160, 24);
    create_edit(page, IDC_LXMUSIC_PATH, 205, 75, 500, 32, 0);
    create_button(page, L"浏览", IDC_BTN_PLUGIN_BROWSE, 720, 75, 90, 32);
    HWND enabled = CreateWindowExW(0, L"BUTTON", L"启用点歌插件", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                   28, 125, 180, 28, page, (HMENU)IDC_PLUGIN_ENABLED, g_app.instance, NULL);
    SendMessageW(enabled, WM_SETFONT, (WPARAM)g_font, TRUE);
    SetWindowTheme(enabled, L"", L"");
    create_label(page, L"搜索结果保留秒数", 28, 170, 160, 24);
    create_edit(page, IDC_MUSIC_SEARCH_TIMEOUT, 205, 165, 120, 32, ES_NUMBER);
    create_label(page, L"播放队列上限", 28, 212, 160, 24);
    create_edit(page, IDC_MUSIC_QUEUE_LIMIT, 205, 207, 120, 32, ES_NUMBER);
    create_button(page, L"保存插件", IDC_BTN_SAVE_PLUGIN, 28, 470, 150, 48);
}

void build_about_page(void) {
    g_pages[PAGE_ABOUT] = create_page(L"关于");
    HWND page = g_pages[PAGE_ABOUT];
    create_label(page, L"ChatGIBot Native\n纯 C / Win32 原生版\n默认本地 PP-OCRv6，可切换视觉 AI\n仅用于学习和研究，请遵守游戏服务条款。", 28, 85, 750, 200);
}

void load_ui_values(void) {
    const AppConfig *c = &g_app.config;
    #define SET_TEXT(id,value) SetDlgItemTextW(g_pages[g_current_page], id, value)
    int page = g_current_page; (void)page;
    SetDlgItemTextW(g_pages[PAGE_VISION], IDC_ENGINE_MODE, L"");
    SendDlgItemMessageW(g_pages[PAGE_VISION], IDC_ENGINE_MODE, CB_SELECTSTRING, -1, (LPARAM)(wcscmp(c->vision_engine, L"local") == 0 ? L"本地 PP-OCRv6" : L"自定义视觉 AI"));
    const wchar_t *capture_label = wcscmp(c->screenshot_mode, L"printwindow") == 0 ? L"PrintWindow" :
                                    wcscmp(c->screenshot_mode, L"duplication") == 0 ? L"Desktop Duplication（备用）" : L"BitBlt";
    SendDlgItemMessageW(g_pages[PAGE_VISION], IDC_CAPTURE_MODE, CB_SELECTSTRING, -1, (LPARAM)capture_label);
    SendDlgItemMessageW(g_pages[PAGE_VISION], IDC_DEVICE_MODE, CB_SELECTSTRING, -1, (LPARAM)c->inference_device);
    SendDlgItemMessageW(g_pages[PAGE_VISION], IDC_MODEL_TIER, CB_SELECTSTRING, -1, (LPARAM)c->model_tier);
    wchar_t number[32];
    for (int i=0;i<4;i++) { _itow(c->region[i], number, 10); SetDlgItemTextW(g_pages[PAGE_VISION], IDC_REGION_X+i, number); }
    _itow(c->score_threshold_percent, number, 10); SetDlgItemTextW(g_pages[PAGE_VISION], IDC_THRESHOLD, number);
    _itow(c->ocr_interval_ms, number, 10); SetDlgItemTextW(g_pages[PAGE_VISION], IDC_OCR_INTERVAL, number);
    {
        HWND combo = GetDlgItem(g_pages[PAGE_VISION], IDC_OCR_THREADS);
        if (combo) {
            populate_ocr_threads_combo(combo);
            _itow(c->ocr_threads, number, 10);
            SendMessageW(combo, CB_SELECTSTRING, -1, (LPARAM)number);
        }
    }
    SetDlgItemTextW(g_pages[PAGE_VISION], IDC_VISION_PROMPT, c->vision_prompt);
    EnterCriticalSection(&g_app.log_lock);
    wchar_t ocr_preview[8192]; wcscpy_s(ocr_preview, 8192, g_app.ocr_preview);
    LeaveCriticalSection(&g_app.log_lock);
    SetDlgItemTextW(g_pages[PAGE_VISION], IDC_OCR_PREVIEW, ocr_preview[0] ? ocr_preview : L"尚未测试");
    SetDlgItemTextW(g_pages[PAGE_AI], IDC_AI_BASE, c->ai_base_url);
    SetDlgItemTextW(g_pages[PAGE_AI], IDC_AI_MODEL, c->ai_model);
    SetDlgItemTextW(g_pages[PAGE_AI], IDC_AI_KEY_ENV, c->ai_key_env);
    SetDlgItemTextW(g_pages[PAGE_AI], IDC_AI_KEY, c->ai_key);
    _snwprintf_s(number, 32, _TRUNCATE, L"%.2f", c->temperature); SetDlgItemTextW(g_pages[PAGE_AI], IDC_AI_TEMPERATURE, number);
    _itow(c->max_tokens, number, 10); SetDlgItemTextW(g_pages[PAGE_AI], IDC_AI_MAX_TOKENS, number);
    SetDlgItemTextW(g_pages[PAGE_AI], IDC_AI_PERSONALITY, c->personality);
    SetDlgItemTextW(g_pages[PAGE_AI], IDC_VISION_AI_BASE, c->vision_base_url);
    SetDlgItemTextW(g_pages[PAGE_AI], IDC_VISION_AI_MODEL, c->vision_model);
    SetDlgItemTextW(g_pages[PAGE_AI], IDC_VISION_AI_KEY_ENV, c->vision_key_env);
    SetDlgItemTextW(g_pages[PAGE_AI], IDC_VISION_AI_KEY, c->vision_key);
    SendDlgItemMessageW(g_pages[PAGE_AI], IDC_AI_PROVIDER, CB_SELECTSTRING, -1,
                        (LPARAM)(wcsstr(c->ai_base_url, L"deepseek") ? L"DeepSeek" :
                                 wcsstr(c->ai_base_url, L"bigmodel") ? L"智谱" :
                                 wcsstr(c->ai_base_url, L"openai.com") ? L"OpenAI" :
                                 wcsstr(c->ai_base_url, L"moonshot") ? L"Moonshot" :
                                 wcsstr(c->ai_base_url, L"11434") ? L"Ollama" : L"自定义"));
    SendDlgItemMessageW(g_pages[PAGE_DEPS], IDC_DOWNLOAD_SOURCE, CB_SELECTSTRING, -1,
                        (LPARAM)download_source_label(c->download_source));
    SetDlgItemTextW(g_pages[PAGE_ROBOT], IDC_GAME_TITLE, c->window_title);
    _itow(c->trigger_interval_ms, number, 10); SetDlgItemTextW(g_pages[PAGE_ROBOT], IDC_TRIGGER_MS, number);
    _itow(c->send_interval_ms, number, 10); SetDlgItemTextW(g_pages[PAGE_ROBOT], IDC_SEND_INTERVAL, number);
    _itow(c->max_chars, number, 10); SetDlgItemTextW(g_pages[PAGE_ROBOT], IDC_MAX_CHARS, number);
    SetDlgItemTextW(g_pages[PAGE_ROBOT], IDC_WAKE_WORD, c->wake_word);
    _itow(c->history_turns, number, 10); SetDlgItemTextW(g_pages[PAGE_ROBOT], IDC_HISTORY_TURNS, number);
    ui_checkbox_set(IDC_OVERLAY_ENABLED, c->log_overlay_enabled);
    { HWND cb = GetDlgItem(g_pages[PAGE_ROBOT], IDC_OVERLAY_ENABLED); if (cb) InvalidateRect(cb, NULL, FALSE); }
    SetDlgItemTextW(g_pages[PAGE_PLUGIN], IDC_LXMUSIC_PATH, c->lxmusic_path);
    ui_checkbox_set(IDC_PLUGIN_ENABLED, c->music_enabled);
    { HWND cb = GetDlgItem(g_pages[PAGE_PLUGIN], IDC_PLUGIN_ENABLED); if (cb) InvalidateRect(cb, NULL, FALSE); }
    _itow(c->music_search_timeout, number, 10); SetDlgItemTextW(g_pages[PAGE_PLUGIN], IDC_MUSIC_SEARCH_TIMEOUT, number);
    _itow(c->music_max_queue_size, number, 10); SetDlgItemTextW(g_pages[PAGE_PLUGIN], IDC_MUSIC_QUEUE_LIMIT, number);
}

void save_vision_settings(void) {
    wchar_t text[256];
    get_combo_text(g_pages[PAGE_VISION], IDC_CAPTURE_MODE, text, 256);
    if (wcsstr(text, L"Print")) wcscpy_s(g_app.config.screenshot_mode, 32, L"printwindow");
    else if (wcsstr(text, L"Desktop")) wcscpy_s(g_app.config.screenshot_mode, 32, L"duplication");
    else wcscpy_s(g_app.config.screenshot_mode, 32, L"bitblt");
    get_combo_text(g_pages[PAGE_VISION], IDC_DEVICE_MODE, text, 256);
    wcscpy_s(g_app.config.inference_device, 32, wcsstr(text, L"DirectML") ? L"directml" : L"cpu");
    get_combo_text(g_pages[PAGE_VISION], IDC_ENGINE_MODE, text, 256);
    wcscpy_s(g_app.config.vision_engine, 32, wcsstr(text, L"视觉 AI") ? L"vision_ai" : L"local");
    get_combo_text(g_pages[PAGE_VISION], IDC_MODEL_TIER, text, 256);
    wcscpy_s(g_app.config.model_tier, 32, text);
    GetDlgItemTextW(g_pages[PAGE_VISION], IDC_REGION_X, text, 32); g_app.config.region[0] = _wtoi(text);
    GetDlgItemTextW(g_pages[PAGE_VISION], IDC_REGION_Y, text, 32); g_app.config.region[1] = _wtoi(text);
    GetDlgItemTextW(g_pages[PAGE_VISION], IDC_REGION_W, text, 32); g_app.config.region[2] = _wtoi(text);
    GetDlgItemTextW(g_pages[PAGE_VISION], IDC_REGION_H, text, 32); g_app.config.region[3] = _wtoi(text);
    GetDlgItemTextW(g_pages[PAGE_VISION], IDC_THRESHOLD, text, 32); g_app.config.score_threshold_percent = _wtoi(text);
    GetDlgItemTextW(g_pages[PAGE_VISION], IDC_OCR_INTERVAL, text, 32); g_app.config.ocr_interval_ms = _wtoi(text);
    get_combo_text(g_pages[PAGE_VISION], IDC_OCR_THREADS, text, 256);
    g_app.config.ocr_threads = _wtoi(text);
    GetDlgItemTextW(g_pages[PAGE_VISION], IDC_VISION_PROMPT, g_app.config.vision_prompt, 1024);
    config_normalize(&g_app.config);
    if (config_save(&g_app.config)) app_log_format(L"INFO", L"识图设置已保存");
    dependency_detect_async(&g_app);
}

void save_robot_settings(void) {
    wchar_t text[256];
    GetDlgItemTextW(g_pages[PAGE_ROBOT], IDC_GAME_TITLE, g_app.config.window_title, 128);
    GetDlgItemTextW(g_pages[PAGE_ROBOT], IDC_TRIGGER_MS, text, 32); g_app.config.trigger_interval_ms = max(50, _wtoi(text));
    GetDlgItemTextW(g_pages[PAGE_ROBOT], IDC_SEND_INTERVAL, text, 32); g_app.config.send_interval_ms = _wtoi(text);
    GetDlgItemTextW(g_pages[PAGE_ROBOT], IDC_MAX_CHARS, text, 32); g_app.config.max_chars = max(1, _wtoi(text));
    GetDlgItemTextW(g_pages[PAGE_ROBOT], IDC_WAKE_WORD, g_app.config.wake_word, 64);
    GetDlgItemTextW(g_pages[PAGE_ROBOT], IDC_HISTORY_TURNS, text, 32); g_app.config.history_turns = max(1, _wtoi(text));
    g_app.config.log_overlay_enabled = ui_checkbox_checked(IDC_OVERLAY_ENABLED);
    config_normalize(&g_app.config);
    if (config_save(&g_app.config)) app_log_format(L"INFO", L"机器人设置已保存");
}

void save_plugin_settings(void) {
    GetDlgItemTextW(g_pages[PAGE_PLUGIN], IDC_LXMUSIC_PATH, g_app.config.lxmusic_path, MAX_PATH);
    g_app.config.music_enabled = ui_checkbox_checked(IDC_PLUGIN_ENABLED);
    wchar_t text[64];
    GetDlgItemTextW(g_pages[PAGE_PLUGIN], IDC_MUSIC_SEARCH_TIMEOUT, text, 64); g_app.config.music_search_timeout = _wtoi(text);
    GetDlgItemTextW(g_pages[PAGE_PLUGIN], IDC_MUSIC_QUEUE_LIMIT, text, 64); g_app.config.music_max_queue_size = _wtoi(text);
    config_normalize(&g_app.config);
    if (config_save(&g_app.config)) app_log_format(L"INFO", L"插件设置已保存");
    dependency_detect_async(&g_app);
}

void save_ai_settings(void) {
    GetDlgItemTextW(g_pages[PAGE_AI], IDC_AI_BASE, g_app.config.ai_base_url, 256);
    GetDlgItemTextW(g_pages[PAGE_AI], IDC_AI_MODEL, g_app.config.ai_model, 128);
    GetDlgItemTextW(g_pages[PAGE_AI], IDC_AI_KEY_ENV, g_app.config.ai_key_env, 128);
    GetDlgItemTextW(g_pages[PAGE_AI], IDC_AI_KEY, g_app.config.ai_key, 256);
    wchar_t text[64];
    GetDlgItemTextW(g_pages[PAGE_AI], IDC_AI_TEMPERATURE, text, 64); g_app.config.temperature = _wtof(text);
    GetDlgItemTextW(g_pages[PAGE_AI], IDC_AI_MAX_TOKENS, text, 64); g_app.config.max_tokens = _wtoi(text);
    GetDlgItemTextW(g_pages[PAGE_AI], IDC_AI_PERSONALITY, g_app.config.personality, 1024);
    GetDlgItemTextW(g_pages[PAGE_AI], IDC_VISION_AI_BASE, g_app.config.vision_base_url, 256);
    GetDlgItemTextW(g_pages[PAGE_AI], IDC_VISION_AI_MODEL, g_app.config.vision_model, 128);
    GetDlgItemTextW(g_pages[PAGE_AI], IDC_VISION_AI_KEY_ENV, g_app.config.vision_key_env, 128);
    GetDlgItemTextW(g_pages[PAGE_AI], IDC_VISION_AI_KEY, g_app.config.vision_key, 256);
    config_normalize(&g_app.config);
    if (!config_save(&g_app.config)) app_log_format(L"ERROR", L"AI 设置保存失败");
}

void save_download_source(void) {
    wchar_t text[128] = L"";
    if (!get_combo_text(g_pages[PAGE_DEPS], IDC_DOWNLOAD_SOURCE, text, 128)) return;
    if (wcsstr(text, L"ModelScope")) wcscpy_s(g_app.config.download_source, 32, L"modelscope");
    else if (wcsstr(text, L"HF-Mirror")) wcscpy_s(g_app.config.download_source, 32, L"hf-mirror");
    else if (wcsstr(text, L"官方")) wcscpy_s(g_app.config.download_source, 32, L"official");
    else if (wcsstr(text, L"自定义")) wcscpy_s(g_app.config.download_source, 32, L"custom");
    else wcscpy_s(g_app.config.download_source, 32, L"auto");
    config_normalize(&g_app.config);
    if (config_save(&g_app.config)) app_log_format(L"INFO", L"下载源已切换：%s", text);
}

void switch_page(int page) {
    if (page < 0 || page >= PAGE_COUNT) return;
    for (int i = 0; i < PAGE_COUNT; ++i) ShowWindow(g_pages[i], i == page ? SW_SHOW : SW_HIDE);
    if (!g_pages[page]) app_log_format(L"ERROR", L"页面句柄为空：%d", page);
    g_current_page = page;
    load_ui_values();
    for (int i = 0; i < PAGE_COUNT; ++i) InvalidateRect(g_nav_buttons[i], NULL, TRUE);
    InvalidateRect(g_app.hwnd, NULL, FALSE);
}

static void layout_deps_page(HWND page) {
    if (!page) return;
    RECT rc;
    GetClientRect(page, &rc);
    int ph = rc.bottom;
    int pw = rc.right - 28 - 28;
    if (pw > 832) pw = 832;
    if (pw < 200) pw = 200;
    HWND progress = GetDlgItem(page, IDC_DEP_PROGRESS);
    if (progress) MoveWindow(progress, 28, ph - 118, pw, 16, TRUE);
    struct { int id, x, w; } btns[] = {
        { IDC_BTN_DEP_REFRESH, 28, 110 },
        { IDC_BTN_DEP_INSTALL, 148, 170 },
        { IDC_BTN_DEP_MODELS, 328, 160 },
        { IDC_BTN_DEP_CANCEL, 498, 100 },
        { IDC_BTN_TEST_MIRRORS, 608, 120 },
        { IDC_BTN_DEP_LOG, 738, 120 },
    };
    for (int i = 0; i < 6; ++i) {
        HWND b = GetDlgItem(page, btns[i].id);
        if (b) MoveWindow(b, btns[i].x, ph - 92, btns[i].w, 46, TRUE);
    }
}

void ui_layout(HWND hwnd) {
    RECT client;
    GetClientRect(hwnd, &client);
    int page_width = max(500, client.right - 250);
    int page_height = max(420, client.bottom - 115);
    for (int i = 0; i < PAGE_COUNT; ++i)
        if (g_pages[i]) MoveWindow(g_pages[i], 230, 95, page_width, page_height, TRUE);
    layout_deps_page(g_pages[PAGE_DEPS]);
}

void ui_refresh_ocr_preview(void) {
    if (!g_pages[PAGE_VISION]) return;
    wchar_t preview[8192];
    EnterCriticalSection(&g_app.log_lock);
    wcscpy_s(preview, 8192, g_app.ocr_preview);
    LeaveCriticalSection(&g_app.log_lock);
    SetDlgItemTextW(g_pages[PAGE_VISION], IDC_OCR_PREVIEW, preview[0] ? preview : L"尚未测试");
}

void ui_refresh_region_selection(void) {
    HWND page = g_pages[PAGE_VISION];
    if (!page) return;
    HWND button = GetDlgItem(page, IDC_BTN_SELECT_REGION);
    if (button) SetWindowTextW(button, g_app.region_selecting ? L"重新框选" : L"框选区域");
    if (g_region_preview) InvalidateRect(g_region_preview, NULL, FALSE);
}

static void update_region_edit(int id, int value) {
    wchar_t text[32];
    _itow(value, text, 10);
    SetDlgItemTextW(g_pages[PAGE_VISION], id, text);
}

static void finish_region_drag(HWND preview_control, POINT point) {
    RECT preview;
    GetClientRect(preview_control, &preview);
    RECT selected = { g_app.region_drag_start.x, g_app.region_drag_start.y, point.x, point.y };
    if (selected.left > selected.right) { int value = selected.left; selected.left = selected.right; selected.right = value; }
    if (selected.top > selected.bottom) { int value = selected.top; selected.top = selected.bottom; selected.bottom = value; }
    selected.left = max(preview.left, min(preview.right, selected.left));
    selected.right = max(preview.left, min(preview.right, selected.right));
    selected.top = max(preview.top, min(preview.bottom, selected.top));
    selected.bottom = max(preview.top, min(preview.bottom, selected.bottom));
    if (selected.right - selected.left < 8 || selected.bottom - selected.top < 8) {
        app_log_format(L"WARNING", L"框选区域太小，请重新拖拽选择");
        return;
    }
    int source_width = g_app.preview_source_width;
    int source_height = g_app.preview_source_height;
    if (source_width <= 0 || source_height <= 0) return;
    int x1 = MulDiv(selected.left - preview.left, source_width, preview.right - preview.left);
    int y1 = MulDiv(selected.top - preview.top, source_height, preview.bottom - preview.top);
    int x2 = MulDiv(selected.right - preview.left, source_width, preview.right - preview.left);
    int y2 = MulDiv(selected.bottom - preview.top, source_height, preview.bottom - preview.top);
    x1 = max(0, min(source_width - 1, x1));
    y1 = max(0, min(source_height - 1, y1));
    x2 = max(x1 + 1, min(source_width, x2));
    y2 = max(y1 + 1, min(source_height, y2));
    update_region_edit(IDC_REGION_X, x1);
    update_region_edit(IDC_REGION_Y, y1);
    update_region_edit(IDC_REGION_W, x2);
    update_region_edit(IDC_REGION_H, y2);
    g_app.config.region[0] = x1;
    g_app.config.region[1] = y1;
    g_app.config.region[2] = x2;
    g_app.config.region[3] = y2;
    config_normalize(&g_app.config);
    if (config_save(&g_app.config)) app_log_format(L"INFO", L"聊天区域已框选并保存：(%d,%d)-(%d,%d)", x1, y1, x2, y2);
    else app_log_format(L"ERROR", L"聊天区域已更新，但配置保存失败");
    g_app.region_selecting = TRUE;
    ui_refresh_region_selection();
}

static BOOL is_primary_button(int id) {
    switch (id) {
        case IDC_BTN_START:
        case IDC_BTN_SAVE_VISION:
        case IDC_BTN_SAVE_AI:
        case IDC_BTN_SAVE_ROBOT:
        case IDC_BTN_SAVE_PLUGIN:
        case IDC_BTN_DEP_INSTALL:
        case IDC_BTN_DEP_MODELS:
            return TRUE;
        default:
            return FALSE;
    }
}

void draw_modern_button(HWND hwnd, DRAWITEMSTRUCT *info) {
    RECT rect = info->rcItem;
    BOOL hovered = (info->itemState & ODS_HOTLIGHT) != 0;
    BOOL disabled = (info->itemState & ODS_DISABLED) != 0;
    BOOL selected = (info->itemState & ODS_SELECTED) != 0;
    BOOL primary = !disabled && is_primary_button(info->CtlID);

    COLORREF fill_top, fill_bottom, text_color, border;
    if (primary) {
        if (disabled) { fill_top = fill_bottom = RGB(36, 48, 62); text_color = RGB(120, 140, 160); border = RGB(36, 48, 62); }
        else if (selected) { fill_top = fill_bottom = g_accent_dark; text_color = RGB(8, 20, 32); border = g_accent_dark; }
        else if (hovered) { fill_top = RGB(120, 216, 254); fill_bottom = g_accent; text_color = RGB(8, 20, 32); border = g_accent_hi; }
        else { fill_top = g_accent_hi; fill_bottom = g_accent; text_color = RGB(8, 20, 32); border = g_accent; }
    } else {
        if (disabled) { fill_top = fill_bottom = RGB(16, 22, 32); text_color = RGB(100, 116, 139); border = RGB(38, 48, 61); }
        else if (selected) { fill_top = fill_bottom = RGB(24, 34, 47); text_color = g_text; border = RGB(60, 74, 92); }
        else if (hovered) { fill_top = RGB(35, 50, 70); fill_bottom = RGB(28, 40, 56); text_color = g_text; border = g_accent; }
        else { fill_top = RGB(25, 36, 52); fill_bottom = g_card; text_color = g_text; border = RGB(60, 74, 92); }
    }

    (void)border;
    FillRect(info->hDC, &rect, g_background_brush);
    ui_fill_round_rect(info->hDC, rect, 16, fill_top, fill_bottom, TRUE, FALSE, 0);

    SetBkMode(info->hDC, TRANSPARENT);
    SetTextColor(info->hDC, text_color);
    wchar_t text[128]; GetWindowTextW(hwnd, text, 128);
    HFONT old_font = (HFONT)SelectObject(info->hDC, g_font);
    RECT text_rect = rect;
    text_rect.right -= 2;
    DrawTextW(info->hDC, text, -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(info->hDC, old_font);
}

void draw_modern_checkbox(HWND hwnd, DRAWITEMSTRUCT *info) {
    RECT rect = info->rcItem;
    BOOL hovered = (info->itemState & ODS_HOTLIGHT) != 0;
    BOOL focused = (info->itemState & ODS_FOCUS) != 0;
    BOOL checked = ui_checkbox_checked(GetDlgCtrlID(hwnd));

    FillRect(info->hDC, &rect, g_background_brush);
    int box = 18;
    int bx = rect.left;
    int by = rect.top + (rect.bottom - rect.top - box) / 2;

    HBRUSH box_brush = CreateSolidBrush(checked ? g_accent : g_input);
    HPEN box_pen = CreatePen(PS_SOLID, 1, checked ? g_accent : hovered || focused ? g_accent : RGB(88, 104, 126));
    HGDIOBJ old_brush = SelectObject(info->hDC, box_brush);
    HGDIOBJ old_pen = SelectObject(info->hDC, box_pen);
    RoundRect(info->hDC, bx, by, bx + box, by + box, 5, 5);
    SelectObject(info->hDC, old_pen); SelectObject(info->hDC, old_brush);
    DeleteObject(box_pen); DeleteObject(box_brush);

    if (checked) {
        HPEN check_pen = CreatePen(PS_SOLID, 2, RGB(8, 20, 32));
        HGDIOBJ old_check_pen = SelectObject(info->hDC, check_pen);
        HGDIOBJ old_check_brush = SelectObject(info->hDC, GetStockObject(NULL_BRUSH));
        POINT pts[3] = { { bx + 4, by + 9 }, { bx + 8, by + 13 }, { bx + 14, by + 5 } };
        Polyline(info->hDC, pts, 3);
        SelectObject(info->hDC, old_check_brush);
        SelectObject(info->hDC, old_check_pen);
        DeleteObject(check_pen);
    }

    wchar_t text[256]; GetWindowTextW(hwnd, text, 256);
    SetBkMode(info->hDC, TRANSPARENT);
    SetTextColor(info->hDC, g_text);
    HFONT old_font = (HFONT)SelectObject(info->hDC, g_font);
    RECT text_rect = { bx + box + 8, rect.top, rect.right, rect.bottom };
    DrawTextW(info->hDC, text, -1, &text_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(info->hDC, old_font);
}

void draw_dependency_page(HWND page, HDC dc) {
    RECT page_rect; GetClientRect(page, &page_rect);
    const int card_x = 28;
    int card_w = page_rect.right - 28 * 2 - 8;
    if (card_w > 832) card_w = 832;
    if (card_w < 200) card_w = 200;
    const int card_h = 40, gap = 4;
    int area_bottom = page_rect.bottom - 130;
    if (area_bottom < 160) area_bottom = 160;
    int y = 106;
    SetBkMode(dc, TRANSPARENT);
    for (int i = 0; i < g_app.dep_count; ++i) {
        if (y + card_h > area_bottom) break;
        COLORREF state_color = g_muted;
        const wchar_t *state_text = L"未知";
        switch (g_app.deps[i].state) {
            case DEP_OK:        state_color = g_success;  state_text = L"正常";     break;
            case DEP_WARN:      state_color = g_warning;  state_text = L"警告";     break;
            case DEP_MISSING:   state_color = g_error;    state_text = L"缺失";     break;
            case DEP_INSTALLING:state_color = g_accent;   state_text = L"安装中";   break;
            case DEP_CHECKING:  state_color = g_muted;    state_text = L"检测中";   break;
            default:                                    state_text = L"未知";     break;
        }
        RECT card = { card_x, y, card_x + card_w, y + card_h };
        ui_fill_round_rect(dc, card, 10, RGB(25, 36, 53), g_card, TRUE, TRUE, RGB(40, 52, 68));

        RECT accent = { card.left, card.top + 8, card.left + 4, card.bottom - 8 };
        ui_fill_round_rect(dc, accent, 2, state_color, state_color, FALSE, FALSE, 0);

        RECT dot = { card.left + 14, card.top + (card_h - 12) / 2, card.left + 26, card.top + (card_h - 12) / 2 + 12 };
        ui_fill_ellipse(dc, dot, state_color);

        SelectObject(dc, g_font);
        SetTextColor(dc, g_text);
        RECT line1 = { card.left + 36, card.top + 1, card.right - 12, card.top + 21 };
        RECT m_t = line1;
        DrawTextW(dc, g_app.deps[i].title, -1, &m_t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_CALCRECT);
        DrawTextW(dc, g_app.deps[i].title, -1, &line1, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        int tx = m_t.right + 8;
        SetTextColor(dc, state_color);
        RECT st = { tx, card.top + 1, card.right - 12, card.top + 21 };
        DrawTextW(dc, state_text, -1, &st, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (g_app.deps[i].checked_at[0]) {
            SetTextColor(dc, RGB(110, 124, 145));
            RECT time_rect = { card.left + 36, card.top + 1, card.right - 12, card.top + 21 };
            DrawTextW(dc, g_app.deps[i].checked_at, -1, &time_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_RIGHT);
        }

        RECT detail_rect = { card.left + 36, card.top + 21, card.right - 12, card.top + 38 };
        SetTextColor(dc, g_muted);
        DrawTextW(dc, g_app.deps[i].detail, -1, &detail_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SetTextColor(dc, g_text);

        y += card_h + gap;
    }
}
