#include <stdio.h>
#include "../src/app.h"
#include "../third_party/cjson/cJSON.h"

void ui_append_log(const wchar_t *level, const wchar_t *message) { (void)level; (void)message; }
void ui_refresh_dependency_controls(void) {}
void ui_set_busy(int busy) { (void)busy; }

static BOOL load_zhipu_key(const wchar_t *path) {
    char *json = NULL;
    if (!read_text_file(path, &json, NULL)) return FALSE;
    cJSON *root = cJSON_Parse(json);
    HeapFree(GetProcessHeap(), 0, json);
    cJSON *ai = root ? cJSON_GetObjectItemCaseSensitive(root, "ai") : NULL;
    cJSON *key = ai ? cJSON_GetObjectItemCaseSensitive(ai, "api_key") : NULL;
    BOOL valid = cJSON_IsString(key) && key->valuestring && key->valuestring[0];
    if (valid) utf8_to_wide(key->valuestring, g_app.config.vision_key, 256);
    if (root) cJSON_Delete(root);
    return valid;
}

int wmain(int argc, wchar_t **argv) {
    if (argc < 3) return 2;
    InitializeCriticalSection(&g_app.log_lock);
    config_defaults(&g_app.config);
    if (!load_zhipu_key(argv[1])) return 3;
    wcscpy_s(g_app.config.vision_engine, 32, L"vision_ai");
    wcscpy_s(g_app.config.vision_base_url, 256, L"https://open.bigmodel.cn/api/paas/v4");
    wcscpy_s(g_app.config.vision_model, 128, L"glm-4v-flash");
    g_app.config.vision_key_env[0] = 0;
    wcscpy_s(g_app.config.vision_prompt, 1024, L"只输出图片中的中文文字，不要解释，不要添加标点。");
    HBITMAP bitmap = (HBITMAP)LoadImageW(NULL, argv[2], IMAGE_BITMAP, 0, 0,
                                         LR_LOADFROMFILE | LR_CREATEDIBSECTION);
    if (!bitmap) return 4;
    OcrTextLine lines[8] = {0};
    int count = vision_read_bitmap(&g_app, bitmap, lines, 8);
    DeleteObject(bitmap);
    BOOL found = FALSE;
    for (int i = 0; i < count; ++i) {
        char utf8[2048] = {0};
        wide_to_utf8(lines[i].text, utf8, sizeof(utf8));
        printf("vision line: %s\n", utf8);
        if (wcsstr(lines[i].text, L"派蒙") || wcsstr(lines[i].text, L"旅行者")) found = TRUE;
    }
    printf("%s real Zhipu vision, lines=%d\n", found ? "PASS" : "FAIL", count);
    DeleteCriticalSection(&g_app.log_lock);
    return found ? 0 : 1;
}
