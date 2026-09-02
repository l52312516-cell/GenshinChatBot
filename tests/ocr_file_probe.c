#include <stdio.h>
#include "../src/app.h"

void ui_append_log(const wchar_t *level, const wchar_t *message) { (void)level; (void)message; }
void ui_refresh_dependency_controls(void) {}
void ui_set_busy(int busy) { (void)busy; }
void ui_refresh_ocr_preview(void) {}
void ui_layout(HWND hwnd) { (void)hwnd; }

static HBITMAP crop_bitmap(HBITMAP source, int left, int top, int right, int bottom) {
    BITMAP info;
    if (!GetObjectW(source, sizeof(info), &info)) return NULL;
    left = max(0, min(left, info.bmWidth));
    top = max(0, min(top, abs(info.bmHeight)));
    right = max(left, min(right, info.bmWidth));
    bottom = max(top, min(bottom, abs(info.bmHeight)));
    int width = right - left, height = bottom - top;
    if (width <= 0 || height <= 0) return NULL;
    HDC screen = GetDC(NULL);
    HDC source_dc = CreateCompatibleDC(screen);
    HDC target_dc = CreateCompatibleDC(screen);
    HBITMAP target = CreateCompatibleBitmap(screen, width, height);
    HGDIOBJ old_source = SelectObject(source_dc, source);
    HGDIOBJ old_target = SelectObject(target_dc, target);
    BOOL copied = BitBlt(target_dc, 0, 0, width, height, source_dc, left, top, SRCCOPY);
    SelectObject(source_dc, old_source);
    SelectObject(target_dc, old_target);
    DeleteDC(source_dc);
    DeleteDC(target_dc);
    ReleaseDC(NULL, screen);
    if (!copied) { DeleteObject(target); return NULL; }
    return target;
}

int wmain(int argc, wchar_t **argv) {
    if (argc < 6) return 2;
    InitializeCriticalSection(&g_app.log_lock);
    config_defaults(&g_app.config);
    BOOL require_directml = FALSE;
    const wchar_t *expected = NULL;
    for (int i = 6; i < argc; ++i) {
        if (wcscmp(argv[i], L"--directml") == 0 || wcscmp(argv[i], L"--require-directml") == 0) {
            wcscpy_s(g_app.config.inference_device, 32, L"directml");
            if (wcscmp(argv[i], L"--require-directml") == 0) require_directml = TRUE;
        } else if (wcsncmp(argv[i], L"--tier=", 7) == 0) {
            wcsncpy_s(g_app.config.model_tier, 32, argv[i] + 7, _TRUNCATE);
        } else expected = argv[i];
    }
    wchar_t detail[512] = L"";
    if (!ocr_prepare(&g_app, detail, 512)) {
        wprintf(L"OCR prepare: FAIL %s\n", detail);
        return 5;
    }
    wprintf(L"OCR prepare: PASS %s\n", detail);
    if (require_directml && (wcscmp(g_app.config.inference_device, L"directml") != 0 ||
                             !wcsstr(detail, L"DirectML"))) {
        puts("OCR DirectML: FAIL provider fallback detected");
        return 6;
    }
    HBITMAP source = (HBITMAP)LoadImageW(NULL, argv[1], IMAGE_BITMAP, 0, 0,
                                         LR_LOADFROMFILE | LR_CREATEDIBSECTION);
    if (!source) return 3;
    HBITMAP cropped = crop_bitmap(source, _wtoi(argv[2]), _wtoi(argv[3]), _wtoi(argv[4]), _wtoi(argv[5]));
    DeleteObject(source);
    if (!cropped) return 4;
    OcrTextLine lines[64] = {0};
    int count = ocr_read_bitmap(&g_app, cropped, lines, 64);
    DeleteObject(cropped);
    BOOL expected_found = expected == NULL;
    BOOL valid = count > 0;
    for (int i = 0; i < count; ++i) {
        wprintf(L"%s\t%.3f\t%d,%d,%d,%d\n", lines[i].text, lines[i].confidence,
                lines[i].left, lines[i].top, lines[i].right, lines[i].bottom);
        if (ocr_line_is_system(lines[i].text)) valid = FALSE;
        if (expected && wcsstr(lines[i].text, expected)) expected_found = TRUE;
    }
    printf("OCR file: %s lines=%d expected=%s\n",
           valid && expected_found ? "PASS" : "FAIL", count, expected_found ? "yes" : "no");
    DeleteCriticalSection(&g_app.log_lock);
    return valid && expected_found ? 0 : 1;
}
