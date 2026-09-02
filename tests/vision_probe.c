#include <stdio.h>
#include <windows.h>
#include "../src/app.h"

void ui_append_log(const wchar_t *level, const wchar_t *message) { (void)level; wprintf(L"%s\n", message); }
void ui_refresh_dependency_controls(void) {}
void ui_set_busy(int busy) { (void)busy; }

int wmain(int argc, wchar_t **argv) {
    if (argc < 2) return 2;
    config_defaults(&g_app.config);
    wcscpy_s(g_app.config.vision_engine, 32, L"vision_ai");
    wcscpy_s(g_app.config.vision_base_url, 256, L"http://127.0.0.1:18080/v1?token=query-ok");
    wcscpy_s(g_app.config.vision_model, 128, L"mock-vision");
    HBITMAP bitmap = (HBITMAP)LoadImageW(NULL, argv[1], IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
    if (!bitmap) return 3;
    OcrTextLine lines[8]; int count = vision_read_bitmap(&g_app, bitmap, lines, 8);
    DeleteObject(bitmap);
    printf("count=%d\n", count);
    for (int i = 0; i < count; ++i) wprintf(L"%s source=%s\n", lines[i].text, lines[i].source);
    return count == 1 && wcscmp(lines[0].source, L"vision_ai") == 0 ? 0 : 1;
}
