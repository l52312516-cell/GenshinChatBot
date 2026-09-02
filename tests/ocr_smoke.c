#include "../src/app.h"
#include <stdio.h>

void ui_append_log(const wchar_t *level, const wchar_t *message) { (void)level; (void)message; }
void ui_refresh_dependency_controls(void) {}
void ui_set_busy(int busy) { (void)busy; }
void ui_refresh_ocr_preview(void) {}
void ui_layout(HWND hwnd) { (void)hwnd; }

static BOOL save_bitmap(HBITMAP bitmap, const wchar_t *path) {
    BITMAP bitmap_info;
    if (!GetObjectW(bitmap, sizeof(bitmap_info), &bitmap_info)) return FALSE;
    int width = bitmap_info.bmWidth, height = abs(bitmap_info.bmHeight);
    BITMAPINFO info = {0};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    DWORD image_size = (DWORD)width * (DWORD)height * 4;
    BYTE *pixels = (BYTE*)HeapAlloc(GetProcessHeap(), 0, image_size);
    HDC dc = GetDC(NULL);
    BOOL ok = pixels && GetDIBits(dc, bitmap, 0, height, pixels, &info, DIB_RGB_COLORS) != 0;
    ReleaseDC(NULL, dc);
    if (!ok) { if (pixels) HeapFree(GetProcessHeap(), 0, pixels); return FALSE; }
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (file == INVALID_HANDLE_VALUE) { HeapFree(GetProcessHeap(), 0, pixels); return FALSE; }
    BITMAPFILEHEADER header = {0};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + image_size;
    DWORD written = 0;
    ok = WriteFile(file, &header, sizeof(header), &written, NULL) &&
         WriteFile(file, &info.bmiHeader, sizeof(info.bmiHeader), &written, NULL) &&
         WriteFile(file, pixels, image_size, &written, NULL);
    CloseHandle(file);
    HeapFree(GetProcessHeap(), 0, pixels);
    return ok;
}

static BOOL bring_to_foreground(HWND window) {
    HWND foreground = GetForegroundWindow();
    DWORD foreground_thread = foreground ? GetWindowThreadProcessId(foreground, NULL) : 0;
    DWORD target_thread = GetWindowThreadProcessId(window, NULL);
    BOOL attached = foreground_thread && target_thread && foreground_thread != target_thread &&
                    AttachThreadInput(foreground_thread, target_thread, TRUE);
    BringWindowToTop(window);
    SetForegroundWindow(window);
    ShowWindow(window, SW_RESTORE);
    if (attached) AttachThreadInput(foreground_thread, target_thread, FALSE);
    return GetForegroundWindow() == window;
}

int wmain(int argc, wchar_t **argv) {
    InitializeCriticalSection(&g_app.log_lock);
    config_defaults(&g_app.config);
    BOOL directml = FALSE, game = FALSE, open_chat = FALSE;
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--directml") == 0) directml = TRUE;
        else if (wcscmp(argv[i], L"--game") == 0) game = TRUE;
        else if (wcscmp(argv[i], L"--open-chat") == 0) open_chat = TRUE;
        else if (wcscmp(argv[i], L"--duplication") == 0) wcscpy_s(g_app.config.screenshot_mode, 32, L"duplication");
        else if (wcscmp(argv[i], L"--printwindow") == 0) wcscpy_s(g_app.config.screenshot_mode, 32, L"printwindow");
    }
    if (directml) wcscpy_s(g_app.config.inference_device, 32, L"directml");
    wchar_t detail[512] = L"";
    BOOL ok = ocr_prepare(&g_app, detail, 512);
    wprintf(L"OCR smoke: %s%s%s\n", ok ? L"PASS" : L"FAIL", detail[0] ? L" - " : L"", detail);
    if (!ok || !game) {
        DeleteCriticalSection(&g_app.log_lock);
        return ok ? 0 : 1;
    }
    HWND game_window = find_game_window(g_app.config.window_title);
    if (!game_window) game_window = find_game_window(L"原神");
    if (game_window) {
        RECT window_rect = {0};
        GetWindowRect(game_window, &window_rect);
        wprintf(L"Game window: hwnd=%p rect=%ld,%ld-%ld,%ld foreground=%d\n", (void*)game_window,
                window_rect.left, window_rect.top, window_rect.right, window_rect.bottom,
                GetForegroundWindow() == game_window);
    } else {
        wprintf(L"Game window: NOT_FOUND\n");
    }
    if (game_window) {
        bring_to_foreground(game_window);
        Sleep(500);
        if (open_chat) {
            INPUT input[2] = {0};
            input[0].type = INPUT_KEYBOARD; input[0].ki.wVk = VK_RETURN;
            input[1] = input[0]; input[1].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(2, input, sizeof(INPUT));
            Sleep(500);
        }
    }
    RECT captured = {0};
    int mode = wcscmp(g_app.config.screenshot_mode, L"printwindow") == 0 ? 1 :
               wcscmp(g_app.config.screenshot_mode, L"duplication") == 0 ? 2 : 0;
    HBITMAP bitmap = capture_game_window(g_app.config.window_title, g_app.config.region, mode, &captured);
    if (!bitmap) { wprintf(L"OCR game capture: FAIL\n"); return 2; }
    wchar_t capture_path[MAX_PATH];
    get_data_dir(capture_path, MAX_PATH);
    wcscat_s(capture_path, MAX_PATH, L"\\ocr-game-capture.bmp");
    save_bitmap(bitmap, capture_path);
    wprintf(L"Capture image: %s\n", capture_path);
    OcrTextLine lines[32];
    int count = ocr_read_bitmap(&g_app, bitmap, lines, 32);
    DeleteObject(bitmap);
    BOOL quality_ok = count > 0;
    for (int i = 0; i < count; ++i)
        if (ocr_line_is_system(lines[i].text) || lines[i].confidence < 0.0f || lines[i].confidence > 1.0f) quality_ok = FALSE;
    wprintf(L"OCR game capture: %s (%d lines)\n", quality_ok ? L"PASS" : L"FAIL", count);
    wchar_t result_path[MAX_PATH];
    get_data_dir(result_path, MAX_PATH);
    wcscat_s(result_path, MAX_PATH, L"\\ocr-game-result.txt");
    char result[16384] = {0};
    for (int i = 0; i < count; ++i) {
        char text[2048] = {0}, row[2300] = {0};
        wide_to_utf8(lines[i].text, text, sizeof(text));
        _snprintf_s(row, sizeof(row), _TRUNCATE, "%s\t%.4f\t%d,%d,%d,%d\n", text,
                    lines[i].confidence, lines[i].left, lines[i].top, lines[i].right, lines[i].bottom);
        strcat_s(result, sizeof(result), row);
        wprintf(L"  %s\n", lines[i].text);
    }
    write_text_file_atomic(result_path, result);
    wprintf(L"OCR result: %s\n", result_path);
    DeleteCriticalSection(&g_app.log_lock);
    return quality_ok ? 0 : 3;
}
