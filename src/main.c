#include "app.h"
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")

static HBITMAP g_sidebar_grad = NULL;
static int g_sidebar_grad_h = -1;

LRESULT CALLBACK od_button_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR uidsub, DWORD_PTR ref) {
    (void)uidsub; (void)ref;
    if (msg == WM_ERASEBKGND) return TRUE;  /* 阻止自绘按钮先擦背景再画，避免闪烁 */
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

void build_console_page(void);
void build_vision_page(void);
void build_ai_page(void);
void build_robot_page(void);
void build_deps_page(void);
void build_plugin_page(void);
void build_about_page(void);
void switch_page(int page);
void save_vision_settings(void);
void save_robot_settings(void);
void save_plugin_settings(void);
void save_ai_settings(void);
void save_download_source(void);
void draw_modern_button(HWND hwnd, DRAWITEMSTRUCT *info);
void draw_modern_checkbox(HWND hwnd, DRAWITEMSTRUCT *info);

static BOOL g_snapshot_mode;
static int g_snapshot_page;
static BOOL g_closing;

static void draw_dark_combo(DRAWITEMSTRUCT *info) {
    RECT rect = info->rcItem;
    BOOL selected = (info->itemState & ODS_SELECTED) != 0;
    BOOL focused = (info->itemState & ODS_FOCUS) != 0;
    COLORREF fill = selected ? RGB(34, 55, 73) : g_input;
    HBRUSH brush = CreateSolidBrush(fill);
    FillRect(info->hDC, &rect, brush);
    DeleteObject(brush);
    if (focused) {
        HPEN pen = CreatePen(PS_SOLID, 1, g_accent);
        HGDIOBJ old_pen = SelectObject(info->hDC, pen);
        HGDIOBJ old_brush = SelectObject(info->hDC, GetStockObject(HOLLOW_BRUSH));
        Rectangle(info->hDC, rect.left, rect.top, rect.right, rect.bottom);
        SelectObject(info->hDC, old_brush);
        SelectObject(info->hDC, old_pen);
        DeleteObject(pen);
    }
    int item_index = info->itemID == (UINT)-1
        ? (int)SendMessageW(info->hwndItem, CB_GETCURSEL, 0, 0) : (int)info->itemID;
    if (item_index >= 0) {
        wchar_t text[256] = L"";
        SendMessageW(info->hwndItem, CB_GETLBTEXT, (WPARAM)item_index, (LPARAM)text);
        SetBkMode(info->hDC, TRANSPARENT);
        SetTextColor(info->hDC, g_text);
        SelectObject(info->hDC, g_font);
        RECT text_rect = rect;
        text_rect.left += 10;
        text_rect.right -= 8;
        DrawTextW(info->hDC, text, -1, &text_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

static void draw_nav_icon(HDC dc, int left, int top, int size, int index, COLORREF color) {
    static const wchar_t *glyphs[PAGE_COUNT] = {
        L"\uE756", /* 控制台: CommandPrompt */
        L"\uE81E", /* 识图: ReadingMode */
        L"\uE8BD", /* AI: Message */
        L"\uE945", /* 机器人: LightningBolt (automation) */
        L"\uE896", /* 依赖: Download */
        L"\uE90F", /* 插件: Wrench/component */
        L"\uE946", /* 关于: Info */
    };
    HFONT old = (HFONT)SelectObject(dc, g_icon_font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    RECT r = { left - 2, top - 2, left + size + 2, top + size + 2 };
    DrawTextW(dc, glyphs[index], -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, old);
}

static void draw_nav_button(DRAWITEMSTRUCT *info, int index) {
    RECT rect = info->rcItem;
    BOOL hovered = (info->itemState & ODS_HOTLIGHT) != 0;
    BOOL selected = g_current_page == index;

    if (g_sidebar_grad) {
        HDC mem = CreateCompatibleDC(info->hDC);
        if (mem) {
            HGDIOBJ oldb = SelectObject(mem, g_sidebar_grad);
            BitBlt(info->hDC, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, mem, rect.left, rect.top, SRCCOPY);
            SelectObject(mem, oldb);
            DeleteDC(mem);
        }
    } else {
        HBRUSH sb = CreateSolidBrush(RGB(15, 21, 32));
        FillRect(info->hDC, &rect, sb);
        DeleteObject(sb);
    }

    RECT pill = { rect.left + 6, rect.top + 3, rect.right - 6, rect.bottom - 3 };
    COLORREF pill_top, pill_bottom;
    if (selected) { pill_top = RGB(32, 52, 76); pill_bottom = RGB(26, 42, 62); }
    else if (hovered) { pill_top = RGB(25, 37, 53); pill_bottom = RGB(20, 31, 46); }
    else { pill_top = RGB(15, 21, 32); pill_bottom = RGB(13, 18, 28); }
    ui_fill_round_rect(info->hDC, pill, 10, pill_top, pill_bottom, TRUE, FALSE, 0);

    if (selected) {
        RECT accent = { rect.left + 6, rect.top + 11, rect.left + 10, rect.bottom - 11 };
        ui_fill_round_rect(info->hDC, accent, 4, g_accent, g_accent_dark, TRUE, FALSE, 0);
    }

    int icon_size = 18;
    int icon_x = pill.left + 9;
    int icon_y = rect.top + (rect.bottom - rect.top - icon_size) / 2;
    draw_nav_icon(info->hDC, icon_x, icon_y, icon_size, index, hovered || selected ? g_accent : g_muted);

    COLORREF text_color = selected ? RGB(255, 255, 255) : hovered ? RGB(205, 218, 232) : g_muted;
    SetBkMode(info->hDC, TRANSPARENT);
    SetTextColor(info->hDC, text_color);
    SelectObject(info->hDC, g_font);
    RECT text_rect = { icon_x + icon_size + 10, rect.top, rect.right - 8, rect.bottom };
    DrawTextW(info->hDC, NAV_LABELS[index], -1, &text_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static BOOL save_window_snapshot(HWND hwnd) {
    RECT rect;
    if (!GetWindowRect(hwnd, &rect)) return FALSE;
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    HDC screen_dc = GetDC(NULL);
    HDC memory_dc = CreateCompatibleDC(screen_dc);
    HBITMAP bitmap = CreateCompatibleBitmap(screen_dc, width, height);
    HGDIOBJ old_bitmap = SelectObject(memory_dc, bitmap);
    BOOL painted = BitBlt(memory_dc, 0, 0, width, height, screen_dc, rect.left, rect.top, SRCCOPY | CAPTUREBLT);

    BITMAPINFO info;
    ZeroMemory(&info, sizeof(info));
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    DWORD image_size = (DWORD)width * (DWORD)height * 4;
    BYTE *pixels = (BYTE *)HeapAlloc(GetProcessHeap(), 0, image_size);
    BOOL copied = painted && pixels && GetDIBits(memory_dc, bitmap, 0, (UINT)height, pixels, &info, DIB_RGB_COLORS);

    wchar_t exe_dir[MAX_PATH], output[MAX_PATH];
    get_exe_dir(exe_dir, MAX_PATH);
    _snwprintf_s(output, MAX_PATH, _TRUNCATE, L"%s\\ui-snapshot-%d.bmp", exe_dir, g_snapshot_page);
    HANDLE file = copied ? CreateFileW(output, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL) : INVALID_HANDLE_VALUE;
    BOOL saved = FALSE;
    if (file != INVALID_HANDLE_VALUE) {
        BITMAPFILEHEADER header;
        ZeroMemory(&header, sizeof(header));
        header.bfType = 0x4D42;
        header.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        header.bfSize = header.bfOffBits + image_size;
        DWORD written = 0;
        saved = WriteFile(file, &header, sizeof(header), &written, NULL) &&
                WriteFile(file, &info.bmiHeader, sizeof(info.bmiHeader), &written, NULL) &&
                WriteFile(file, pixels, image_size, &written, NULL);
        CloseHandle(file);
    }
    if (pixels) HeapFree(GetProcessHeap(), 0, pixels);
    SelectObject(memory_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(NULL, screen_dc);
    return saved;
}

void ui_refresh_dependency_controls(void) {
    if (!g_app.hwnd) return;
    if (GetCurrentThreadId() != g_app.ui_thread_id) {
        PostMessageW(g_app.hwnd, WM_APP_STATUS, 0, 0);
        return;
    }
    if (g_pages[PAGE_DEPS]) InvalidateRect(g_pages[PAGE_DEPS], NULL, FALSE);
    HWND progress = GetDlgItem(g_pages[PAGE_DEPS], IDC_DEP_PROGRESS);
    if (progress) SendMessageW(progress, PBM_SETPOS, (WPARAM)InterlockedCompareExchange(&g_app.download_progress, 0, 0), 0);
    HWND cancel = GetDlgItem(g_pages[PAGE_DEPS], IDC_BTN_DEP_CANCEL);
    if (cancel) EnableWindow(cancel, InterlockedCompareExchange(&g_app.download_active, 0, 0) != 0);
}

void ui_append_log(const wchar_t *level, const wchar_t *line) {
    (void)level; (void)line;
    static LONG displayed_revision = -1;
    static size_t displayed_chars;
    static wchar_t displayed_head[64];
    static size_t displayed_head_chars;
    HWND log = g_console_log ? g_console_log : GetDlgItem(g_pages[PAGE_CONSOLE], IDC_CONSOLE_LOG);
    if (!log) return;
    wchar_t snapshot[65536];
    LONG revision;
    EnterCriticalSection(&g_app.log_lock);
    wcscpy_s(snapshot, 65536, g_app.log_text);
    revision = g_app.log_revision;
    LeaveCriticalSection(&g_app.log_lock);
    if (!snapshot[0]) {
        wchar_t data_dir[MAX_PATH], path[MAX_PATH];
        get_data_dir(data_dir, MAX_PATH);
        _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%s\\chatgibot-native.log", data_dir);
        char *saved = NULL;
        if (read_text_file(path, &saved, NULL)) {
            utf8_to_wide(saved, snapshot, 65536);
            HeapFree(GetProcessHeap(), 0, saved);
        }
    }
    size_t snapshot_chars = wcslen(snapshot);
    if (revision == displayed_revision) return;
    size_t head_chars = min(snapshot_chars, sizeof(displayed_head) / sizeof(displayed_head[0]) - 1);
    BOOL same_head = displayed_revision >= 0 && displayed_head_chars == head_chars &&
                     wcsncmp(displayed_head, snapshot, head_chars) == 0;
    BOOL replace_all = displayed_revision < 0 || displayed_chars > snapshot_chars || !same_head;
    if (!replace_all) {
        SendMessageW(log, EM_SETSEL, (WPARAM)displayed_chars, (LPARAM)displayed_chars);
        SendMessageW(log, EM_REPLACESEL, FALSE, (LPARAM)(snapshot + displayed_chars));
    } else {
        SendMessageW(log, WM_SETREDRAW, FALSE, 0);
        SendMessageW(log, WM_SETTEXT, 0, (LPARAM)snapshot);
        SendMessageW(log, WM_SETREDRAW, TRUE, 0);
    }
    SendMessageW(log, EM_SETSEL, (WPARAM)snapshot_chars, (LPARAM)snapshot_chars);
    SendMessageW(log, EM_SCROLLCARET, 0, 0);
    if (replace_all) InvalidateRect(log, NULL, FALSE);
    displayed_revision = revision;
    displayed_chars = snapshot_chars;
    displayed_head_chars = head_chars;
    if (head_chars) memcpy(displayed_head, snapshot, head_chars * sizeof(wchar_t));
    displayed_head[head_chars] = 0;
}

void ui_set_busy(int busy) {
    PostMessageW(g_app.hwnd, WM_APP_STATUS, busy ? 1 : 0, 0);
}

static void rebuild_sidebar_gradient(int width, int height) {
    if (g_sidebar_grad && g_sidebar_grad_h == height) return;
    if (g_sidebar_grad) { DeleteObject(g_sidebar_grad); g_sidebar_grad = NULL; }
    g_sidebar_grad_h = height;
    HDC screen = GetDC(NULL);
    HDC mem = screen ? CreateCompatibleDC(screen) : NULL;
    HBITMAP bmp = mem ? CreateCompatibleBitmap(screen, width, height) : NULL;
    if (mem && bmp) {
        HGDIOBJ old = SelectObject(mem, bmp);
        COLORREF top = RGB(20, 28, 44), bottom = RGB(12, 17, 27);
        for (int y = 0; y < height; ++y) {
            int t = (height <= 1) ? 0 : (y * 255) / (height - 1);
            COLORREF c = RGB(
                (GetRValue(top) * (255 - t) + GetRValue(bottom) * t) / 255,
                (GetGValue(top) * (255 - t) + GetGValue(bottom) * t) / 255,
                (GetBValue(top) * (255 - t) + GetBValue(bottom) * t) / 255);
            HBRUSH br = CreateSolidBrush(c);
            RECT row = { 0, y, width, y + 1 };
            FillRect(mem, &row, br);
            DeleteObject(br);
        }
        SelectObject(mem, old);
        g_sidebar_grad = bmp;
    }
    if (mem) DeleteDC(mem);
    if (screen) ReleaseDC(NULL, screen);
}

static void draw_sidebar_gradient(HDC dc, int height) {
    rebuild_sidebar_gradient(215, height);
    if (g_sidebar_grad) {
        HDC mem = CreateCompatibleDC(dc);
        if (mem) {
            HGDIOBJ old = SelectObject(mem, g_sidebar_grad);
            BitBlt(dc, 0, 0, 215, height, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteDC(mem);
            return;
        }
    }
    RECT sidebar = { 0, 0, 215, height };
    HBRUSH bg = CreateSolidBrush(RGB(15, 21, 32));
    FillRect(dc, &sidebar, bg);
    DeleteObject(bg);
}

static void paint_background(HWND hwnd, HDC dc, PAINTSTRUCT *paint) {
    RECT client; GetClientRect(hwnd, &client);
    FillRect(dc, &client, g_background_brush);
    draw_sidebar_gradient(dc, client.bottom);
    RECT sidebar_line = { 215, 0, 216, client.bottom };
    HBRUSH sidebar_line_brush = CreateSolidBrush(RGB(40,52,68));
    FillRect(dc, &sidebar_line, sidebar_line_brush);
    DeleteObject(sidebar_line_brush);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(108,122,143));
    SelectObject(dc, g_font);
    TextOutW(dc, 20, client.bottom - 24, L"v1.0.0", 6);
    RECT header = { 216, 0, client.right, 78 };
    HBRUSH header_brush = CreateSolidBrush(RGB(17,23,34));
    FillRect(dc, &header, header_brush);
    DeleteObject(header_brush);
    RECT header_line = { 216, 78, client.right, 79 };
    HBRUSH header_line_brush = CreateSolidBrush(RGB(40,52,68));
    FillRect(dc, &header_line, header_line_brush);
    DeleteObject(header_line_brush);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(226,232,240));
    SelectObject(dc, g_title_font);
    TextOutW(dc, 250, 18, L"ChatGIBot", (int)wcslen(L"ChatGIBot"));
    SelectObject(dc, g_font);
    RECT status_dot = { 610, 30, 622, 42 };
    HBRUSH status_brush = CreateSolidBrush(g_app.paused ? RGB(250,204,21) : g_app.running ? RGB(74,222,128) : RGB(148,163,184));
    HGDIOBJ old_brush = SelectObject(dc, status_brush);
    HGDIOBJ old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, status_dot.left, status_dot.top, status_dot.right, status_dot.bottom);
    SelectObject(dc, old_pen); SelectObject(dc, old_brush);
    DeleteObject(status_brush);
    const wchar_t *status_text = g_app.paused ? L"已暂停" : g_app.running ? L"运行中" : L"待机";
    SetTextColor(dc, g_app.paused ? RGB(250,204,21) : g_app.running ? RGB(74,222,128) : RGB(148,163,184));
    TextOutW(dc, 636, 28, status_text, (int)wcslen(status_text));
    SetTextColor(dc, RGB(148,163,184));
    wchar_t status[256];
    _snwprintf_s(status, 256, _TRUNCATE, L"截图 %s · 推理 %s · 识图 PP-OCRv6 %s",
                 g_app.config.screenshot_mode, g_app.config.inference_device, g_app.config.model_tier);
    TextOutW(dc, 720, 28, status, (int)wcslen(status));
    (void)paint;
}

static void apply_ai_provider_preset(void) {
    wchar_t provider[64] = L"";
    HWND combo = GetDlgItem(g_pages[PAGE_AI], IDC_AI_PROVIDER);
    int selection = combo ? (int)SendMessageW(combo, CB_GETCURSEL, 0, 0) : -1;
    if (selection >= 0) SendMessageW(combo, CB_GETLBTEXT, (WPARAM)selection, (LPARAM)provider);
    if (wcscmp(provider, L"DeepSeek") == 0) {
        SetDlgItemTextW(g_pages[PAGE_AI], IDC_AI_BASE, L"https://api.deepseek.com/v1");
        SetDlgItemTextW(g_pages[PAGE_AI], IDC_AI_MODEL, L"deepseek-chat");
    } else if (wcscmp(provider, L"智谱") == 0) {
        SetDlgItemTextW(g_pages[PAGE_AI], IDC_AI_BASE, L"https://open.bigmodel.cn/api/paas/v4");
        SetDlgItemTextW(g_pages[PAGE_AI], IDC_AI_MODEL, L"glm-4-flash");
    } else if (wcscmp(provider, L"OpenAI") == 0) {
        SetDlgItemTextW(g_pages[PAGE_AI], IDC_AI_BASE, L"https://api.openai.com/v1");
        SetDlgItemTextW(g_pages[PAGE_AI], IDC_AI_MODEL, L"gpt-4o-mini");
    } else if (wcscmp(provider, L"Moonshot") == 0) {
        SetDlgItemTextW(g_pages[PAGE_AI], IDC_AI_BASE, L"https://api.moonshot.cn/v1");
        SetDlgItemTextW(g_pages[PAGE_AI], IDC_AI_MODEL, L"moonshot-v1-8k");
    } else if (wcscmp(provider, L"Ollama") == 0) {
        SetDlgItemTextW(g_pages[PAGE_AI], IDC_AI_BASE, L"http://127.0.0.1:11434/v1");
        SetDlgItemTextW(g_pages[PAGE_AI], IDC_AI_MODEL, L"llama3.1");
    }
}

static void command_handler(int id) {
    if (g_app.busy && id != IDC_BTN_STOP && id != IDC_BTN_DEP_CANCEL) return;
    if (g_app.running && (id == IDC_BTN_SAVE_VISION || id == IDC_BTN_SAVE_AI ||
        id == IDC_BTN_SAVE_ROBOT || id == IDC_BTN_SAVE_PLUGIN || id == IDC_BTN_AI_TEST)) {
        app_log_format(L"WARNING", L"请先停止机器人，再修改并保存设置");
        return;
    }
    switch (id) {
        case IDC_BTN_CAPTURE: capture_test_async(&g_app); break;
        case IDC_BTN_OCR: ocr_test_async(&g_app); break;
        case IDC_BTN_CLEAR:
            EnterCriticalSection(&g_app.log_lock);
            g_app.log_text[0] = 0;
            ++g_app.log_revision;
            LeaveCriticalSection(&g_app.log_lock);
            SetDlgItemTextW(g_pages[PAGE_CONSOLE], IDC_CONSOLE_LOG, L"");
            wchar_t data_dir[MAX_PATH], path[MAX_PATH];
            get_data_dir(data_dir, MAX_PATH);
            _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%s\\chatgibot-native.log", data_dir);
            write_text_file_atomic(path, "");
            break;
        case IDC_BTN_TEST_CAPTURE: capture_test_async(&g_app); break;
        case IDC_BTN_TEST_OCR: ocr_test_async(&g_app); break;
        case IDC_BTN_SELECT_REGION: capture_select_region_async(&g_app); break;
        case IDC_BTN_SAVE_VISION: save_vision_settings(); break;
        case IDC_BTN_AI_TEST: save_ai_settings(); ai_test_async(&g_app); break;
        case IDC_BTN_SAVE_AI: save_ai_settings(); break;
        case IDC_BTN_SAVE_ROBOT: save_robot_settings(); break;
        case IDC_BTN_DEP_REFRESH: dependency_detect_async(&g_app); break;
        case IDC_BTN_DEP_INSTALL: dependency_install_runtime_async(&g_app); break;
        case IDC_BTN_DEP_MODELS: dependency_repair_models_async(&g_app); break;
        case IDC_BTN_TEST_MIRRORS: dependency_test_mirrors_async(&g_app); break;
        case IDC_DOWNLOAD_SOURCE: save_download_source(); break;
        case IDC_BTN_DEP_CANCEL: dependency_cancel(&g_app); break;
        case IDC_BTN_DEP_LOG: open_log_file(); break;
        case IDC_BTN_PLUGIN_BROWSE: {
            wchar_t path[MAX_PATH]; path[0] = 0;
            OPENFILENAMEW dialog = { sizeof(dialog) };
            dialog.hwndOwner = g_app.hwnd; dialog.lpstrFilter = L"LxMusic (*.exe)\0*.exe\0所有文件\0*.*\0";
            dialog.lpstrFile = path; dialog.nMaxFile = MAX_PATH; dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameW(&dialog)) SetDlgItemTextW(g_pages[PAGE_PLUGIN], IDC_LXMUSIC_PATH, path);
            break;
        }
        case IDC_BTN_SAVE_PLUGIN: save_plugin_settings(); break;
        case IDC_BTN_START: bot_start(&g_app); break;
        case IDC_BTN_PAUSE: bot_pause(&g_app); break;
        case IDC_BTN_STOP: bot_stop(&g_app); break;
    }
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE: return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(hwnd, &paint);
            paint_background(hwnd, dc, &paint);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wparam);
            if (id >= IDC_NAV_BASE && id < IDC_NAV_BASE + PAGE_COUNT) switch_page(id - IDC_NAV_BASE);
            else if (id == IDC_AI_PROVIDER && HIWORD(wparam) == CBN_SELCHANGE) apply_ai_provider_preset();
            else if ((id == IDC_OVERLAY_ENABLED || id == IDC_PLUGIN_ENABLED) && HIWORD(wparam) == BN_CLICKED) {
                HWND chk = (HWND)lparam;
                ui_checkbox_set(id, !ui_checkbox_checked(id));
                InvalidateRect(chk, NULL, FALSE);
                return 0;
            }
            else command_handler(id);
            return 0;
        }
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT *info = (DRAWITEMSTRUCT*)lparam;
            if (info->CtlType == ODT_BUTTON || info->CtlType == ODT_COMBOBOX) {
                if (info->CtlType == ODT_COMBOBOX) {
                    draw_dark_combo(info);
                } else if (info->CtlID >= IDC_NAV_BASE && info->CtlID < IDC_NAV_BASE + PAGE_COUNT) {
                    draw_nav_button(info, info->CtlID - IDC_NAV_BASE);
                } else if (info->CtlID == IDC_OVERLAY_ENABLED || info->CtlID == IDC_PLUGIN_ENABLED) {
                    draw_modern_checkbox(info->hwndItem, info);
                } else draw_modern_button(info->hwndItem, info);
                return TRUE;
            }
            break;
        }
        case WM_MEASUREITEM: {
            MEASUREITEMSTRUCT *info = (MEASUREITEMSTRUCT*)lparam;
            if (info && info->CtlType == ODT_COMBOBOX) {
                info->itemHeight = 32;
                return TRUE;
            }
            break;
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = (HDC)wparam;
            HWND ed = (HWND)lparam;
            int id = GetDlgCtrlID(ed);
            BOOL loglike = (id == IDC_CONSOLE_LOG);
            if (loglike) {
                SetTextColor(dc, RGB(167,243,208));
                SetBkColor(dc, RGB(7,10,16));
                return (LRESULT)g_input_brush;
            }
            BOOL focused = (GetFocus() == ed);
            SetTextColor(dc, focused ? RGB(255, 255, 255) : g_text);
            SetBkColor(dc, focused ? g_card : g_input);
            return (LRESULT)(focused ? g_card_brush : g_input_brush);
        }
        case WM_CTLCOLORBTN: {
            HDC dc = (HDC)wparam;
            SetTextColor(dc, g_text);
            SetBkColor(dc, g_background);
            return (LRESULT)g_background_brush;
        }
        case WM_CTLCOLORLISTBOX: {
            HDC dc = (HDC)wparam;
            SetTextColor(dc, g_text);
            SetBkColor(dc, g_input);
            return (LRESULT)g_input_brush;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wparam;
            SetTextColor(dc, g_text);
            SetBkColor(dc, g_background);
            return (LRESULT)g_background_brush;
        }
        case WM_APP_STATUS:
            bot_reap(&g_app);
            background_reap(&g_app);
            ui_refresh_dependency_controls();
            ui_append_log(L"", L"");
            if (g_pages[PAGE_CONSOLE])
                SetDlgItemTextW(g_pages[PAGE_CONSOLE], IDC_BTN_PAUSE, g_app.paused ? L"继续" : L"暂停");
            InvalidateRect(hwnd, NULL, FALSE);
            if (g_closing && !InterlockedCompareExchange(&g_app.busy, 0, 0) &&
                !bot_thread_active(&g_app) && !background_task_active(&g_app)) {
                KillTimer(hwnd, 2);
                DestroyWindow(hwnd);
            } else if (g_closing) SetTimer(hwnd, 2, 50, NULL);
            return 0;
        case WM_APP_OCR:
            ui_refresh_ocr_preview();
            return 0;
        case WM_APP_REGION:
            ui_refresh_region_selection();
            return 0;
        case WM_APP_LOG: {
            wchar_t *line = (wchar_t*)lparam;
            if (line) {
                ui_append_log(L"", line);
                HeapFree(GetProcessHeap(), 0, line);
            }
            return 0;
        }
        case WM_TIMER:
            if (wparam == 1 && g_snapshot_mode) {
                KillTimer(hwnd, 1);
                save_window_snapshot(hwnd);
                DestroyWindow(hwnd);
                return 0;
            }
            if (wparam == 2 && g_closing) {
                bot_reap(&g_app);
                if (!InterlockedCompareExchange(&g_app.busy, 0, 0) &&
                    !bot_thread_active(&g_app) && !background_task_active(&g_app)) {
                    KillTimer(hwnd, 2);
                    DestroyWindow(hwnd);
                }
                return 0;
            }
            break;
        case WM_ERASEBKGND: return 1;
        case WM_GETMINMAXINFO: {
            MINMAXINFO *limits = (MINMAXINFO*)lparam;
            limits->ptMinTrackSize.x = 1180;
            limits->ptMinTrackSize.y = 740;
            return 0;
        }
        case WM_SIZE:
            ui_layout(hwnd);
            RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            return 0;
        case WM_CLOSE:
            if (g_closing) return 0;
            g_closing = TRUE;
            InterlockedExchange(&g_app.overlay_shutdown, 1);
            overlay_hide(&g_app);
            dependency_cancel(&g_app);
            bot_stop(&g_app);
            if (InterlockedCompareExchange(&g_app.busy, 0, 0) || bot_thread_active(&g_app) ||
                background_task_active(&g_app)) {
                EnableWindow(hwnd, FALSE);
                SetWindowTextW(hwnd, L"ChatGIBot Native - 正在完成后台任务...");
                SetTimer(hwnd, 2, 50, NULL);
                return 0;
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            overlay_destroy(&g_app);
            g_app.hwnd = NULL;
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

typedef BOOL (WINAPI *SetPreferredAppModeFn)(int);
typedef BOOL (WINAPI *AllowDarkModeForWindowFn)(HWND, BOOL);
static SetPreferredAppModeFn pSetPreferredAppMode = NULL;
static AllowDarkModeForWindowFn pAllowDarkModeForWindow = NULL;
static BOOL dark_mode_ready = FALSE;

static void ensure_dark_mode_apis(void) {
    if (dark_mode_ready) return;
    HMODULE uxtheme = LoadLibraryW(L"uxtheme.dll");
    if (uxtheme) {
        pSetPreferredAppMode = (SetPreferredAppModeFn)GetProcAddress(uxtheme, "SetPreferredAppMode");
        pAllowDarkModeForWindow = (AllowDarkModeForWindowFn)GetProcAddress(uxtheme, "AllowDarkModeForWindow");
    }
    if (pSetPreferredAppMode) pSetPreferredAppMode(1); /* APPMODE_ALLOWDARK */
    dark_mode_ready = TRUE;
}

static void apply_dark_mode(HWND hwnd) {
    ensure_dark_mode_apis();
    if (pAllowDarkModeForWindow) pAllowDarkModeForWindow(hwnd, TRUE);
}

static BOOL CALLBACK dark_mode_enum_proc(HWND hwnd, LPARAM lparam) {
    (void)lparam;
    apply_dark_mode(hwnd);
    SetWindowTheme(hwnd, L"DarkMode_Explorer", NULL);
    return TRUE;
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show) {
    (void)previous;
    g_snapshot_mode = command && wcsstr(command, L"--ui-snapshot") != NULL;
    const wchar_t *page_argument = command ? wcsstr(command, L"--page=") : NULL;
    g_snapshot_page = page_argument ? _wtoi(page_argument + 7) : PAGE_CONSOLE;
    if (g_snapshot_page < 0 || g_snapshot_page >= PAGE_COUNT) g_snapshot_page = PAGE_CONSOLE;
    g_app.instance = instance;
    g_app.ui_thread_id = GetCurrentThreadId();
    gdiplus_init();
    InitializeCriticalSection(&g_app.log_lock);
    if (!config_load(&g_app.config))
        app_log_format(L"WARNING", L"未找到或无法解析 config.json，已使用安全默认配置");
    dependency_init(&g_app);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    typedef BOOL (WINAPI *SetDpiContextFn)(HANDLE);
    SetDpiContextFn set_dpi_context = user32 ? (SetDpiContextFn)GetProcAddress(user32, "SetProcessDpiAwarenessContext") : NULL;
    if (set_dpi_context) set_dpi_context((HANDLE)-4);
    else SetProcessDPIAware();
    WNDCLASSW window_class = {0};
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = APP_CLASS;
    window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
    window_class.hIcon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    window_class.hbrBackground = NULL;
    RegisterClassW(&window_class);
    g_background_brush = CreateSolidBrush(g_background);
    g_card_brush = CreateSolidBrush(g_card);
    g_input_brush = CreateSolidBrush(g_input);
    g_font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    g_title_font = CreateFontW(-25, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_mono_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
    g_icon_font = CreateFontW(-18, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe MDL2 Assets");
    g_app.hwnd = CreateWindowExW(WS_EX_APPWINDOW, APP_CLASS, L"ChatGIBot Native", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1180, 740, NULL, NULL, instance, NULL);
    if (g_app.hwnd) {
        HICON large_icon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
        HICON small_icon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
        if (large_icon) SendMessageW(g_app.hwnd, WM_SETICON, ICON_BIG, (LPARAM)large_icon);
        if (small_icon) SendMessageW(g_app.hwnd, WM_SETICON, ICON_SMALL, (LPARAM)small_icon);
    }
    if (!overlay_init(&g_app))
        app_log_format(L"WARNING", L"游戏内日志遮罩初始化失败，将只显示主界面日志");
    build_console_page(); build_vision_page(); build_ai_page(); build_robot_page(); build_deps_page(); build_plugin_page(); build_about_page();
    for (int i = 0; i < PAGE_COUNT; ++i) {
        g_nav_buttons[i] = CreateWindowExW(0, L"BUTTON", NAV_LABELS[i], WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 15, 98 + i * 58, 185, 54, g_app.hwnd, (HMENU)(INT_PTR)(IDC_NAV_BASE + i), instance, NULL);
        SendMessageW(g_nav_buttons[i], WM_SETFONT, (WPARAM)g_font, TRUE);
        SetWindowSubclass(g_nav_buttons[i], od_button_proc, 0, 0);
    }
    apply_dark_mode(g_app.hwnd);
    EnumChildWindows(g_app.hwnd, dark_mode_enum_proc, 0);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(g_app.hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    switch_page(g_snapshot_mode ? g_snapshot_page : PAGE_CONSOLE);
    dependency_detect_async(&g_app);
    ShowWindow(g_app.hwnd, show);
    UpdateWindow(g_app.hwnd);
    if (g_snapshot_mode) {
        SetWindowPos(g_app.hwnd, HWND_TOPMOST, 40, 40, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(g_app.hwnd);
    }
    RedrawWindow(g_app.hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    if (g_snapshot_mode) SetTimer(g_app.hwnd, 1, 1200, NULL);
    MSG message;
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        BOOL dialog_message = message.message >= WM_KEYFIRST && message.message <= WM_KEYLAST &&
                              IsDialogMessageW(g_app.hwnd, &message);
        if (!dialog_message) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    DeleteCriticalSection(&g_app.log_lock);
    gdiplus_shutdown();
    return (int)message.wParam;
}
