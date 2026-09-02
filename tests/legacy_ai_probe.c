#include <stdio.h>
#include "../src/app.h"
#include "../third_party/cjson/cJSON.h"

void ui_append_log(const wchar_t *level, const wchar_t *message) { (void)level; (void)message; }
void ui_refresh_dependency_controls(void) {}
void ui_set_busy(int busy) { (void)busy; }

static BOOL load_deepseek_config(const char *json) {
    cJSON *root = cJSON_Parse(json);
    cJSON *deepseek = root ? cJSON_GetObjectItemCaseSensitive(root, "deepseek") : NULL;
    cJSON *base_url = deepseek ? cJSON_GetObjectItemCaseSensitive(deepseek, "base_url") : NULL;
    cJSON *model = deepseek ? cJSON_GetObjectItemCaseSensitive(deepseek, "model") : NULL;
    cJSON *api_key = deepseek ? cJSON_GetObjectItemCaseSensitive(deepseek, "api_key") : NULL;
    BOOL valid = cJSON_IsString(base_url) && base_url->valuestring && base_url->valuestring[0] &&
                 cJSON_IsString(model) && model->valuestring && model->valuestring[0] &&
                 cJSON_IsString(api_key) && api_key->valuestring && api_key->valuestring[0];
    if (valid) {
        utf8_to_wide(base_url->valuestring, g_app.config.ai_base_url, 256);
        utf8_to_wide(model->valuestring, g_app.config.ai_model, 128);
        utf8_to_wide(api_key->valuestring, g_app.config.ai_key, 256);
        g_app.config.ai_key_env[0] = 0;
    }
    if (root) cJSON_Delete(root);
    return valid;
}

int wmain(int argc, wchar_t **argv) {
    if (argc < 2) return 2;
    InitializeCriticalSection(&g_app.log_lock);
    char *json = NULL;
    if (!read_text_file(argv[1], &json, NULL)) return 3;
    config_defaults(&g_app.config);
    BOOL deepseek_mode = argc > 2 && wcscmp(argv[2], L"--deepseek") == 0;
    if (deepseek_mode) {
        if (!load_deepseek_config(json)) {
            HeapFree(GetProcessHeap(), 0, json);
            DeleteCriticalSection(&g_app.log_lock);
            return 4;
        }
    } else {
        config_from_json(&g_app.config, json);
    }
    HeapFree(GetProcessHeap(), 0, json);
    wchar_t reply[512] = L"";
    BOOL ok = ai_chat(&g_app, L"只回复：连接成功", reply, 512);
    BOOL used_fallback = FALSE;
    if (!ok && deepseek_mode && wcscmp(g_app.config.ai_model, L"deepseek-chat") != 0) {
        wcscpy_s(g_app.config.ai_model, 128, L"deepseek-chat");
        reply[0] = 0;
        ok = ai_chat(&g_app, L"只回复：连接成功", reply, 512);
        used_fallback = ok;
    }
    DeleteCriticalSection(&g_app.log_lock);
    printf("%s legacy %s AI connection, reply_chars=%zu%s\n", ok ? "PASS" : "FAIL",
           deepseek_mode ? "DeepSeek" : "Zhipu", wcslen(reply),
           used_fallback ? " (configured model unavailable; deepseek-chat fallback passed)" : "");
    return ok && reply[0] ? 0 : 1;
}
