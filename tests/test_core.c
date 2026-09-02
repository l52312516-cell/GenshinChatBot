#include <stdio.h>
#include <string.h>
#include "../src/app.h"

static int failures = 0;

void ui_append_log(const wchar_t *level, const wchar_t *message) { (void)level; (void)message; }
void ui_refresh_dependency_controls(void) {}
void ui_set_busy(int busy) { (void)busy; }

static void expect(int condition, const char *name) {
    printf("%s: %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) failures++;
}

int main(void) {
    AppConfig config;
    config_defaults(&config);
    expect(wcscmp(config.ocr_version, L"PP-OCRv6") == 0, "default PP-OCRv6");
    expect(wcscmp(config.model_tier, L"medium") == 0, "default model tier");
    expect(wcscmp(config.screenshot_mode, L"bitblt") == 0, "default BitBlt");
    expect(wcscmp(config.inference_device, L"cpu") == 0, "default CPU");
    expect(wcscmp(config.download_source, L"auto") == 0, "default automatic download source");
    expect(config.trigger_interval_ms == 250, "default trigger interval");
    expect(config.ocr_interval_ms == 1500, "default OCR interval");
    expect(config.send_interval_ms == 600, "default send interval");
    expect(config.enabled_plugin_count == 1 && wcscmp(config.enabled_plugins[0], L"music_player") == 0,
           "default music plugin enabled");
    config_from_json(&config,
        "{\"game\":{\"window_title\":\"Test\"},"
        "\"vision\":{\"engine\":\"vision_ai\",\"screenshot_mode\":\"printwindow\","
        "\"inference_device\":\"directml\",\"trigger_interval_ms\":120,"
        "\"paddle\":{\"model_tier\":\"small\",\"score_threshold_percent\":70}},"
        "\"bot\":{\"max_chars\":42}}");
    expect(wcscmp(config.window_title, L"Test") == 0, "parse window title");
    expect(wcscmp(config.vision_engine, L"vision_ai") == 0, "parse vision AI engine");
    expect(wcscmp(config.screenshot_mode, L"printwindow") == 0, "parse PrintWindow");
    expect(wcscmp(config.inference_device, L"directml") == 0, "parse DirectML");
    expect(config.trigger_interval_ms == 120, "parse trigger interval");
    expect(wcscmp(config.model_tier, L"small") == 0, "parse model tier");
    expect(config.score_threshold_percent == 70, "parse confidence threshold");
    expect(config.max_chars == 42, "parse max chars");
    config_from_json(&config, "{\"vision\":{\"ocr_interval_ms\":2600}}");
    expect(config.ocr_interval_ms == 2600, "parse OCR interval");
    config_from_json(&config, "{\"vision\":{\"ocr_threads\":6}}");
    expect(config.ocr_threads == 6, "parse OCR threads");
    config_from_json(&config, "{\"vision\":{\"ocr_threads\":1}}");
    expect(config.ocr_threads == 2, "clamp OCR threads minimum");
    config_from_json(&config, "{\"vision\":{\"ocr_threads\":10}}");
    expect(config.ocr_threads == 8, "clamp OCR threads maximum");
    config_from_json(&config, "{\"bot\":{\"send_interval_ms\":500},\"vision\":{\"ocr_interval_ms\":100}}");
    expect(config.send_interval_ms == 600 && config.ocr_interval_ms == 500, "clamp send and OCR intervals");
    config_from_json(&config, "{\"downloads\":{\"source\":\"modelscope\"}}");
    expect(wcscmp(config.download_source, L"modelscope") == 0, "parse download source");
    config_from_json(&config, "{\"downloads\":{\"source\":\"invalid\"}}");
    expect(wcscmp(config.download_source, L"auto") == 0, "normalize invalid download source");
    config_from_json(&config, "{\"ai\":{\"base_url\":\"\",\"model\":\"\"},\"vision\":{\"engine\":\"bad\"}}");
    expect(wcscmp(config.ai_base_url, L"https://api.deepseek.com/v1") == 0, "restore empty AI URL");
    expect(wcscmp(config.ai_model, L"deepseek-chat") == 0, "restore empty AI model");
    expect(wcscmp(config.vision_engine, L"local") == 0, "normalize invalid vision engine");
    config_defaults(&config);
    config_from_json(&config,
        "{\"game\":{\"chat_box\":[11,22,333,444]},"
        "\"bot\":{\"check_interval\":9999}}");
    expect(config.region[0] == 11 && config.region[1] == 22 && config.region[2] == 333 && config.region[3] == 444,
           "parse legacy game chat box");
    expect(config.trigger_interval_ms == 60000, "clamp trigger interval");
    config_from_json(&config, "{\"vision\":{\"chat_region\":[-1,0,20001,1]}}");
    expect(config.region[0] == 410 && config.region[1] == 720 && config.region[2] == 1100 && config.region[3] == 950,
           "reset invalid chat region");
    config_defaults(&config);
    config_from_json(&config,
        "{\"vision\":{\"ai\":{\"base_url\":\"http://localhost:1234/v1\","
        "\"model\":\"vision-model\",\"api_key_env\":\"VISION_KEY\","
        "\"prompt\":\"return one line\"},\"paddle\":{\"model_tier\":\"tiny\"}},"
        "\"plugins\":{\"enabled\":[\"music_player\"]}}");
    expect(wcscmp(config.vision_base_url, L"http://localhost:1234/v1") == 0, "parse nested vision base URL");
    expect(wcscmp(config.vision_model, L"vision-model") == 0, "parse nested vision model");
    expect(wcscmp(config.vision_key_env, L"VISION_KEY") == 0, "parse nested vision key env");
    expect(wcscmp(config.vision_prompt, L"return one line") == 0, "parse nested vision prompt");
    expect(wcscmp(config.model_tier, L"tiny") == 0, "parse nested Paddle tier");
    config_defaults(&config);
    config_from_json(&config,
        "{\"game\":{\"chat_box\":[410,720,1100,950]},"
        "\"ai\":{\"api_key\":\"legacy-key\"},"
        "\"deepseek\":{\"base_url\":\"https://api.deepseek.com\","
        "\"model\":\"deepseek-v4-flash\"}}");
    expect(wcscmp(config.ai_base_url, L"https://api.deepseek.com") == 0, "parse legacy DeepSeek URL");
    expect(wcscmp(config.ai_model, L"deepseek-v4-flash") == 0, "parse legacy DeepSeek model");
    expect(wcscmp(config.ai_key, L"legacy-key") == 0, "preserve legacy AI key");
    config_defaults(&config);
    config_from_json(&config,
        "{\"ai\":{\"api_key\":\"zhipu-key\",\"model\":\"glm-4-flash\"},"
        "\"deepseek\":{\"base_url\":\"https://api.deepseek.com\",\"model\":\"deepseek-chat\"}}");
    expect(wcscmp(config.ai_base_url, L"https://open.bigmodel.cn/api/paas/v4") == 0, "infer legacy Zhipu URL");
    config_defaults(&config);
    config_from_json(&config, "{\"ai\":{\"api_key\":\"zhipu-key\",\"model\":\"glm-4-flash\"}}");
    expect(wcscmp(config.ai_base_url, L"https://open.bigmodel.cn/api/paas/v4") == 0,
           "infer legacy Zhipu URL without DeepSeek section");
    config_defaults(&config);
    config_from_json(&config,
        "{\"bot\":{\"wake_word\":\"测试唤醒\",\"blacklist\":[\"敏感词\"]},"
         "\"plugins\":{\"enabled\":[\"music_player\"],\"config\":{\"music_player\":{"
         "\"search_timeout\":75,\"max_queue_size\":12,\"blacklist\":[\"禁歌\"]}}}}");
    expect(wcscmp(config.wake_word, L"测试唤醒") == 0, "parse wake word");
    expect(config.bot_blacklist_count == 1 && wcscmp(config.bot_blacklist[0], L"敏感词") == 0,
           "parse bot blacklist");
    expect(config.music_blacklist_count == 1 && wcscmp(config.music_blacklist[0], L"禁歌") == 0,
           "parse music blacklist");
    expect(config.music_search_timeout == 75 && config.music_max_queue_size == 12 && config.music_enabled,
           "parse music plugin settings");
    config_defaults(&config);
    config_from_json(&config, "{\"bot\":{\"blacklist\":[\"仅机器人词\"]}}");
    expect(config.bot_blacklist_count == 1 && config.music_blacklist_count == 0,
           "keep bot and music blacklists separate");
    config_defaults(&config);
    config_from_json(&config, "{\"plugins\":{\"enabled\":[]}}");
    expect(!config.music_enabled, "parse disabled music plugin");
    expect(config.enabled_plugin_count == 0, "parse empty plugin list");
    config_from_json(&config, "{\"plugins\":{\"enabled\":[\"music_player\",\"demo\",\"demo\"]}}");
    expect(config.enabled_plugin_count == 2 && config.music_enabled &&
           wcscmp(config.enabled_plugins[1], L"demo") == 0, "parse unique plugin list");
    wchar_t cleaned[512];
    wcscpy_s(cleaned, 512, L"派蒙：**你好** &amp; 旅行者 https://example.com");
    ai_clean_reply(cleaned, 512);
    expect(wcscmp(cleaned, L"你好 & 旅行者 ") != 0 && wcscmp(cleaned, L"你好 & 旅行者") == 0,
           "clean AI markdown URL and HTML entity");
    wcscpy_s(cleaned, 512, L"请发送 /help 查看指令");
    ai_clean_reply(cleaned, 512);
    expect(cleaned[0] == 0, "reject AI command instruction");
    expect(ocr_line_is_system(L"-2026年8月30日23:09-"), "filter date system line");
    expect(ocr_line_is_system(L"-23:09-"), "filter time system line");
    expect(ocr_line_is_system(L"-2026x6x13x20:16-"), "filter damaged OCR date line");
    expect(ocr_line_is_system(L"玛拉妮LV.999"), "filter player level line");
    expect(!ocr_line_is_system(L"派蒙萌萌萌"), "keep normal chat line");
    wchar_t ocr_text[256];
    wcscpy_s(ocr_text, 256, L"**olor=#FFCC33>大家好 < **olor>");
    ocr_clean_text(ocr_text, 256);
    expect(wcscmp(ocr_text, L"大家好") == 0, "clean damaged game color tags");
    wcscpy_s(ocr_text, 256, L"<color=#FFCC33>旅行者</color>");
    ocr_clean_text(ocr_text, 256);
    expect(wcscmp(ocr_text, L"旅行者") == 0, "clean normal game color tags");
    wcscpy_s(ocr_text, 256, L"大家好 <");
    ocr_clean_text(ocr_text, 256);
    expect(wcscmp(ocr_text, L"大家好") == 0, "clean trailing markup fragment");
    wcscpy_s(ocr_text, 256, L"你好😀\t旅行者 — 真的\nOK");
    game_clean_text(ocr_text, 256);
    expect(wcscmp(ocr_text, L"你好 旅行者 - 真的 OK") == 0, "clean unsupported game characters");
    InitializeCriticalSection(&g_app.log_lock);
    g_app.log_text[0] = 0;
    app_log_format(L"INFO", L"log buffer test");
    expect(wcsstr(g_app.log_text, L"log buffer test") != NULL, "append log buffer");
    wchar_t log_path[MAX_PATH], log_backup[MAX_PATH];
    wchar_t log_data_dir[MAX_PATH];
    get_data_dir(log_data_dir, MAX_PATH);
    _snwprintf_s(log_path, MAX_PATH, _TRUNCATE, L"%s\\chatgibot-native.log", log_data_dir);
    _snwprintf_s(log_backup, MAX_PATH, _TRUNCATE, L"%s.1", log_path);
    HANDLE log_fixture = CreateFileW(log_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (log_fixture != INVALID_HANDLE_VALUE) {
        char block[65536];
        memset(block, 'x', sizeof(block));
        for (int i = 0; i < 81; ++i) {
            DWORD written = 0;
            WriteFile(log_fixture, block, sizeof(block), &written, NULL);
        }
        CloseHandle(log_fixture);
    }
    app_log_format(L"INFO", L"log rotation test");
    expect(GetFileAttributesW(log_backup) != INVALID_FILE_ATTRIBUTES, "rotate oversized log");
    DeleteFileW(log_backup);
    DeleteCriticalSection(&g_app.log_lock);
    config_defaults(&config);
    wcscpy_s(config.music_blacklist[0], 128, L"测试词");
    config.music_blacklist_count = 1;
    expect(config_save(&config), "save config");
    char *saved_json = NULL;
    wchar_t data_dir[ MAX_PATH ], saved_path[ MAX_PATH ];
    get_data_dir(data_dir, MAX_PATH);
    _snwprintf_s(saved_path, MAX_PATH, _TRUNCATE, L"%s\\config.json", data_dir);
    expect(read_text_file(saved_path, &saved_json, NULL), "read saved config");
    expect(saved_json && strstr(saved_json, "music_player") != NULL, "persist music plugin");
    if (saved_json) HeapFree(GetProcessHeap(), 0, saved_json);
    expect(write_text_file_atomic(saved_path, "{invalid-json"), "write invalid config fixture");
    wcscpy_s(config.ai_model, 128, L"must-reset");
    expect(!config_load(&config), "reject invalid config JSON");
    expect(wcscmp(config.ai_model, L"deepseek-chat") == 0, "fallback defaults after invalid config");
    config_save(&config);
    return failures;
}
