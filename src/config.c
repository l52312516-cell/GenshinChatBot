#include "app.h"
#include "../third_party/cjson/cJSON.h"

static void set_wide_from_json(const cJSON *object, const char *key, wchar_t *output, int chars, const wchar_t *fallback) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsString(item) && item->valuestring && *item->valuestring) {
        utf8_to_wide(item->valuestring, output, chars);
    } else {
        _snwprintf_s(output, (size_t)chars, _TRUNCATE, L"%s", fallback);
    }
}

static void set_wide_from_preferred_json(const cJSON *preferred, const char *key, const cJSON *fallback_object,
                                         wchar_t *output, int chars, const wchar_t *fallback) {
    cJSON *item = preferred ? cJSON_GetObjectItemCaseSensitive(preferred, key) : NULL;
    if (!cJSON_IsString(item)) item = fallback_object ? cJSON_GetObjectItemCaseSensitive(fallback_object, key) : NULL;
    if (cJSON_IsString(item) && item->valuestring && *item->valuestring) utf8_to_wide(item->valuestring, output, chars);
    else _snwprintf_s(output, (size_t)chars, _TRUNCATE, L"%s", fallback);
}

static int set_int_from_json(const cJSON *object, const char *key, int fallback) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

static double set_double_from_json(const cJSON *object, const char *key, double fallback) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsNumber(item) ? item->valuedouble : fallback;
}

static void add_string(cJSON *object, const char *key, const wchar_t *value) {
    char utf8[4096];
    wide_to_utf8(value, utf8, sizeof(utf8));
    cJSON_AddStringToObject(object, key, utf8);
}

static BOOL migrate_music_blacklist(AppConfig *config) {
    if (!config || config->music_blacklist_count > 0) return FALSE;
    wchar_t exe_dir[MAX_PATH], path[MAX_PATH];
    get_exe_dir(exe_dir, MAX_PATH);
    _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%s\\plugins\\blacklist.json", exe_dir);
    char *json = NULL;
    if (!read_text_file(path, &json, NULL)) return FALSE;
    cJSON *root = cJSON_Parse(json);
    cJSON *keywords = root ? cJSON_GetObjectItemCaseSensitive(root, "keywords") : NULL;
    BOOL migrated = FALSE;
    if (cJSON_IsArray(keywords)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, keywords) {
            if (config->music_blacklist_count >= 32 || !cJSON_IsString(item) || !item->valuestring) continue;
            wchar_t keyword[128];
            if (!utf8_to_wide(item->valuestring, keyword, 128) || !keyword[0]) continue;
            BOOL duplicate = FALSE;
            for (int i = 0; i < config->music_blacklist_count; ++i)
                if (_wcsicmp(config->music_blacklist[i], keyword) == 0) duplicate = TRUE;
            if (!duplicate) {
                wcscpy_s(config->music_blacklist[config->music_blacklist_count++], 128, keyword);
                migrated = TRUE;
            }
        }
    }
    if (root) cJSON_Delete(root);
    HeapFree(GetProcessHeap(), 0, json);
    return migrated;
}

void config_defaults(AppConfig *config) {
    memset(config, 0, sizeof(*config));
    wcscpy_s(config->window_title, 128, L"原神");
    wcscpy_s(config->ai_base_url, 256, L"https://api.deepseek.com/v1");
    wcscpy_s(config->ai_model, 128, L"deepseek-chat");
    wcscpy_s(config->ai_key_env, 128, L"CHATGIBOT_AI_KEY");
    wcscpy_s(config->wake_word, 64, L"派蒙萌萌萌");
    wcscpy_s(config->vision_base_url, 256, L"https://api.example.com/v1");
    wcscpy_s(config->vision_model, 128, L"qwen-vl-plus");
    wcscpy_s(config->vision_key_env, 128, L"CHATGIBOT_VISION_KEY");
    wcscpy_s(config->vision_prompt, 1024, L"只输出截图中原神聊天区域内的新聊天文本，不要解释。每行一条。");
    wcscpy_s(config->vision_engine, 32, L"local");
    wcscpy_s(config->ocr_version, 32, L"PP-OCRv6");
    wcscpy_s(config->model_tier, 32, L"medium");
    wcscpy_s(config->screenshot_mode, 32, L"bitblt");
    wcscpy_s(config->inference_device, 32, L"cpu");
    wcscpy_s(config->download_source, 32, L"auto");
    config->region[0] = 410; config->region[1] = 720;
    config->region[2] = 1100; config->region[3] = 950;
    config->trigger_interval_ms = 250;
    config->ocr_interval_ms = 1500;
    config->ocr_threads = 2;
    config->send_interval_ms = 600;
    config->log_overlay_enabled = TRUE;
    config->max_chars = 35;
    config->history_turns = 8;
    config->temperature = 0.7;
    config->max_tokens = 80;
    config->score_threshold_percent = 55;
    config->music_search_timeout = 50;
    config->music_max_queue_size = 30;
    config->music_enabled = TRUE;
    wcscpy_s(config->enabled_plugins[0], 64, L"music_player");
    config->enabled_plugin_count = 1;
    wcscpy_s(config->personality, 1024, L"你叫派蒙，是旅行者最好的伙伴。请用活泼的派蒙语气，每次只给一句不超过25个字的中文回复。");
}

void config_normalize(AppConfig *config) {
    if (!config->ai_base_url[0]) wcscpy_s(config->ai_base_url, 256, L"https://api.deepseek.com/v1");
    if (!config->ai_model[0]) wcscpy_s(config->ai_model, 128, L"deepseek-chat");
    if (!config->ai_key_env[0]) wcscpy_s(config->ai_key_env, 128, L"CHATGIBOT_AI_KEY");
    if (!config->wake_word[0]) wcscpy_s(config->wake_word, 64, L"派蒙萌萌萌");
    if (!config->vision_base_url[0]) wcscpy_s(config->vision_base_url, 256, L"https://api.example.com/v1");
    if (!config->vision_model[0]) wcscpy_s(config->vision_model, 128, L"qwen-vl-plus");
    if (!config->vision_key_env[0]) wcscpy_s(config->vision_key_env, 128, L"CHATGIBOT_VISION_KEY");
    if (!config->vision_prompt[0]) wcscpy_s(config->vision_prompt, 1024, L"只输出截图中原神聊天区域内的新聊天文本，不要解释。每行一条。");
    if (!config->ocr_version[0]) wcscpy_s(config->ocr_version, 32, L"PP-OCRv6");
    if (!config->personality[0]) wcscpy_s(config->personality, 1024, L"你叫派蒙，是旅行者最好的伙伴。请用活泼的语气简短回复。");
    if (!config->window_title[0]) wcscpy_s(config->window_title, 128, L"原神");
    if (config->trigger_interval_ms < 50) config->trigger_interval_ms = 50;
    if (config->trigger_interval_ms > 60000) config->trigger_interval_ms = 60000;
    if (config->ocr_interval_ms < 500) config->ocr_interval_ms = 500;
    if (config->ocr_interval_ms > 60000) config->ocr_interval_ms = 60000;
    if (config->ocr_threads < 2) config->ocr_threads = 2;
    if (config->ocr_threads > 8) config->ocr_threads = 8;
    config->ocr_threads = (config->ocr_threads / 2) * 2;
    if (config->send_interval_ms < 600) config->send_interval_ms = 600;
    if (config->send_interval_ms > 60000) config->send_interval_ms = 60000;
    if (config->max_chars < 1) config->max_chars = 1;
    if (config->max_chars > 120) config->max_chars = 120;
    if (config->history_turns < 1) config->history_turns = 1;
    if (config->history_turns > 8) config->history_turns = 8;
    if (config->temperature < 0.0) config->temperature = 0.0;
    if (config->temperature > 2.0) config->temperature = 2.0;
    if (config->max_tokens < 1) config->max_tokens = 1;
    if (config->max_tokens > 4096) config->max_tokens = 4096;
    if (config->score_threshold_percent < 10) config->score_threshold_percent = 10;
    if (config->score_threshold_percent > 95) config->score_threshold_percent = 95;
    if (config->music_search_timeout < 5) config->music_search_timeout = 5;
    if (config->music_search_timeout > 300) config->music_search_timeout = 300;
    if (config->music_max_queue_size < 1) config->music_max_queue_size = 1;
    if (config->music_max_queue_size > 200) config->music_max_queue_size = 200;
    if (config->enabled_plugin_count < 0 || config->enabled_plugin_count > CHATGIBOT_MAX_PLUGINS)
        config->enabled_plugin_count = 0;
    int unique_plugins = 0;
    for (int i = 0; i < config->enabled_plugin_count; ++i) {
        if (!config->enabled_plugins[i][0]) continue;
        BOOL duplicate = FALSE;
        for (int j = 0; j < unique_plugins; ++j)
            if (_wcsicmp(config->enabled_plugins[j], config->enabled_plugins[i]) == 0) duplicate = TRUE;
        if (!duplicate) {
            if (unique_plugins != i) wcscpy_s(config->enabled_plugins[unique_plugins], 64, config->enabled_plugins[i]);
            ++unique_plugins;
        }
    }
    config->enabled_plugin_count = unique_plugins;
    if (config->music_enabled) {
        BOOL found_music = FALSE;
        for (int i = 0; i < config->enabled_plugin_count; ++i)
            if (_wcsicmp(config->enabled_plugins[i], L"music_player") == 0) found_music = TRUE;
        if (!found_music && config->enabled_plugin_count < CHATGIBOT_MAX_PLUGINS)
            wcscpy_s(config->enabled_plugins[config->enabled_plugin_count++], 64, L"music_player");
    }
    BOOL region_valid = TRUE;
    for (int i = 0; i < 4; ++i)
        if (config->region[i] < 0 || config->region[i] > 8192) region_valid = FALSE;
    if (!region_valid || config->region[2] <= config->region[0] || config->region[3] <= config->region[1]) {
        config->region[0] = 410; config->region[1] = 720;
        config->region[2] = 1100; config->region[3] = 950;
    }
    CharLowerW(config->model_tier);
    if (wcscmp(config->model_tier, L"tiny") != 0 && wcscmp(config->model_tier, L"small") != 0 && wcscmp(config->model_tier, L"medium") != 0)
        wcscpy_s(config->model_tier, 32, L"medium");
    if (wcscmp(config->screenshot_mode, L"printwindow") != 0 && wcscmp(config->screenshot_mode, L"duplication") != 0)
        wcscpy_s(config->screenshot_mode, 32, L"bitblt");
    if (wcscmp(config->inference_device, L"directml") != 0)
        wcscpy_s(config->inference_device, 32, L"cpu");
    if (wcscmp(config->vision_engine, L"vision_ai") != 0)
        wcscpy_s(config->vision_engine, 32, L"local");
    if (wcscmp(config->download_source, L"auto") != 0 &&
        wcscmp(config->download_source, L"modelscope") != 0 &&
        wcscmp(config->download_source, L"hf-mirror") != 0 &&
        wcscmp(config->download_source, L"official") != 0 &&
        wcscmp(config->download_source, L"custom") != 0)
        wcscpy_s(config->download_source, 32, L"auto");
}

void config_from_json(AppConfig *config, const char *json) {
    cJSON *root = cJSON_Parse(json);
    if (!root) return;
    cJSON *game = cJSON_GetObjectItemCaseSensitive(root, "game");
    cJSON *bot = cJSON_GetObjectItemCaseSensitive(root, "bot");
    cJSON *ai = cJSON_GetObjectItemCaseSensitive(root, "ai");
    cJSON *vision = cJSON_GetObjectItemCaseSensitive(root, "vision");
    cJSON *vision_ai = cJSON_GetObjectItemCaseSensitive(vision, "ai");
    cJSON *paddle = cJSON_GetObjectItemCaseSensitive(vision, "paddle");
    cJSON *plugins = cJSON_GetObjectItemCaseSensitive(root, "plugins");
    cJSON *plugin_config = cJSON_GetObjectItemCaseSensitive(plugins, "config");
    cJSON *music = cJSON_GetObjectItemCaseSensitive(plugin_config, "music_player");
    cJSON *enabled = cJSON_GetObjectItemCaseSensitive(plugins, "enabled");
    cJSON *region = cJSON_GetObjectItemCaseSensitive(vision, "chat_region");
    cJSON *check_interval = cJSON_GetObjectItemCaseSensitive(bot, "check_interval");
    cJSON *blacklist = cJSON_GetObjectItemCaseSensitive(bot, "blacklist");
    cJSON *legacy_region = cJSON_GetObjectItemCaseSensitive(game, "chat_box");
    cJSON *legacy_deepseek = cJSON_GetObjectItemCaseSensitive(root, "deepseek");
    cJSON *downloads = cJSON_GetObjectItemCaseSensitive(root, "downloads");
    cJSON *modern_base = cJSON_GetObjectItemCaseSensitive(ai, "base_url");
    cJSON *modern_model = cJSON_GetObjectItemCaseSensitive(ai, "model");
    cJSON *modern_key = cJSON_GetObjectItemCaseSensitive(ai, "api_key");
    BOOL has_modern_base = cJSON_IsString(modern_base) && modern_base->valuestring && modern_base->valuestring[0];
    BOOL has_modern_model = cJSON_IsString(modern_model) && modern_model->valuestring && modern_model->valuestring[0];
    BOOL has_modern_key = cJSON_IsString(modern_key) && modern_key->valuestring && modern_key->valuestring[0];

    set_wide_from_json(game, "window_title", config->window_title, 128, L"原神");
    set_wide_from_json(ai, "base_url", config->ai_base_url, 256, L"https://api.deepseek.com/v1");
    set_wide_from_json(ai, "model", config->ai_model, 128, L"deepseek-chat");
    set_wide_from_json(ai, "api_key_env", config->ai_key_env, 128, L"CHATGIBOT_AI_KEY");
    set_wide_from_json(ai, "api_key", config->ai_key, 256, L"");
    set_wide_from_json(bot, "wake_word", config->wake_word, 64, L"派蒙萌萌萌");
    set_wide_from_json(ai, "personality", config->personality, 1024, L"你叫派蒙，是旅行者最好的伙伴。请用活泼的语气简短回复。");
    set_wide_from_json(vision, "engine", config->vision_engine, 32, L"local");
    set_wide_from_json(vision, "screenshot_mode", config->screenshot_mode, 32, L"bitblt");
    set_wide_from_json(vision, "inference_device", config->inference_device, 32, L"cpu");
    set_wide_from_json(paddle, "ocr_version", config->ocr_version, 32, L"PP-OCRv6");
    set_wide_from_json(paddle, "model_tier", config->model_tier, 32, L"medium");
    set_wide_from_preferred_json(vision_ai, "base_url", vision, config->vision_base_url, 256, L"https://api.example.com/v1");
    set_wide_from_preferred_json(vision_ai, "model", vision, config->vision_model, 128, L"qwen-vl-plus");
    set_wide_from_preferred_json(vision_ai, "api_key_env", vision, config->vision_key_env, 128, L"CHATGIBOT_VISION_KEY");
    set_wide_from_preferred_json(vision_ai, "api_key", vision, config->vision_key, 256, L"");
    set_wide_from_preferred_json(vision_ai, "prompt", vision, config->vision_prompt, 1024, L"只输出截图中原神聊天区域内的新聊天文本，不要解释。每行一条。");
    set_wide_from_json(music, "software_path", config->lxmusic_path, MAX_PATH, L"");
    set_wide_from_json(downloads, "source", config->download_source, 32, L"auto");

    if (!has_modern_base && has_modern_model && _strnicmp(modern_model->valuestring, "glm-", 4) == 0)
        wcscpy_s(config->ai_base_url, 256, L"https://open.bigmodel.cn/api/paas/v4");

    if (legacy_deepseek) {
        if (!has_modern_base) {
            if (has_modern_model && _strnicmp(modern_model->valuestring, "glm-", 4) == 0)
                wcscpy_s(config->ai_base_url, 256, L"https://open.bigmodel.cn/api/paas/v4");
            else
                set_wide_from_json(legacy_deepseek, "base_url", config->ai_base_url, 256, config->ai_base_url);
        }
        if (!has_modern_model) set_wide_from_json(legacy_deepseek, "model", config->ai_model, 128, config->ai_model);
        if (!has_modern_key) set_wide_from_json(legacy_deepseek, "api_key", config->ai_key, 256, config->ai_key);
    }
    if ((!cJSON_IsArray(region) || cJSON_GetArraySize(region) != 4) && cJSON_IsArray(legacy_region))
        region = legacy_region;
    if (cJSON_IsArray(region) && cJSON_GetArraySize(region) == 4) {
        int index = 0;
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, region) config->region[index++] = item->valueint;
    }
    if (cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(vision, "trigger_interval_ms")))
        config->trigger_interval_ms = set_int_from_json(vision, "trigger_interval_ms", 250);
    else if (cJSON_IsNumber(check_interval))
        config->trigger_interval_ms = max(50, (int)(check_interval->valuedouble * 1000.0));
    else config->trigger_interval_ms = 250;
    config->ocr_interval_ms = set_int_from_json(vision, "ocr_interval_ms", 1500);
    config->ocr_threads = set_int_from_json(vision, "ocr_threads", 2);
    config->max_chars = set_int_from_json(bot, "max_chars", 35);
    cJSON *log_overlay = cJSON_GetObjectItemCaseSensitive(bot, "log_overlay");
    if (cJSON_IsBool(log_overlay)) config->log_overlay_enabled = cJSON_IsTrue(log_overlay);
    config->history_turns = set_int_from_json(bot, "history_turns", 8);
    config->send_interval_ms = set_int_from_json(bot, "send_interval_ms", 600);
    config->temperature = set_double_from_json(ai, "temperature", 0.7);
    config->max_tokens = set_int_from_json(ai, "max_tokens", 80);
    config->score_threshold_percent = set_int_from_json(paddle, "score_threshold_percent", 55);
    config->music_search_timeout = set_int_from_json(music, "search_timeout", 50);
    config->music_max_queue_size = set_int_from_json(music, "max_queue_size", 30);
    if (cJSON_IsArray(enabled)) {
        config->music_enabled = FALSE;
        config->enabled_plugin_count = 0;
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, enabled) {
            if (cJSON_IsString(item) && item->valuestring && config->enabled_plugin_count < CHATGIBOT_MAX_PLUGINS) {
                wchar_t *slot = config->enabled_plugins[config->enabled_plugin_count];
                if (utf8_to_wide(item->valuestring, slot, 64) && slot[0]) {
                    BOOL duplicate = FALSE;
                    for (int i = 0; i < config->enabled_plugin_count; ++i) {
                        if (_wcsicmp(config->enabled_plugins[i], slot) == 0) {
                            duplicate = TRUE;
                            break;
                        }
                    }
                    if (!duplicate) {
                        ++config->enabled_plugin_count;
                        if (_wcsicmp(slot, L"music_player") == 0) config->music_enabled = TRUE;
                    }
                }
            }
        }
    }
    config->bot_blacklist_count = 0;
    if (cJSON_IsArray(blacklist)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, blacklist) {
            if (config->bot_blacklist_count >= 32) break;
            if (cJSON_IsString(item) && item->valuestring && *item->valuestring)
                utf8_to_wide(item->valuestring, config->bot_blacklist[config->bot_blacklist_count++], 128);
        }
    }
    config->music_blacklist_count = 0;
    cJSON *music_blacklist = cJSON_GetObjectItemCaseSensitive(music, "blacklist");
    if (cJSON_IsArray(music_blacklist)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, music_blacklist) {
            if (config->music_blacklist_count >= 32) break;
            if (cJSON_IsString(item) && item->valuestring && *item->valuestring)
                utf8_to_wide(item->valuestring, config->music_blacklist[config->music_blacklist_count++], 128);
        }
    }
    config_normalize(config);
    cJSON_Delete(root);
}

BOOL config_load(AppConfig *config) {
    config_defaults(config);
    wchar_t data_dir[MAX_PATH], path[MAX_PATH];
    get_data_dir(data_dir, MAX_PATH);
    _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%s\\config.json", data_dir);
    char *json = NULL;
    if (!read_text_file(path, &json, NULL)) return FALSE;
    cJSON *validation = cJSON_Parse(json);
    if (!validation || !cJSON_IsObject(validation)) {
        if (validation) cJSON_Delete(validation);
        HeapFree(GetProcessHeap(), 0, json);
        return FALSE;
    }
    cJSON_Delete(validation);
    config_from_json(config, json);
    HeapFree(GetProcessHeap(), 0, json);
    if (migrate_music_blacklist(config)) config_save(config);
    return TRUE;
}

BOOL config_save(const AppConfig *config) {
    AppConfig normalized = *config;
    config_normalize(&normalized);
    config = &normalized;
    cJSON *root = cJSON_CreateObject();
    cJSON *game = cJSON_CreateObject(), *bot = cJSON_CreateObject(), *ai = cJSON_CreateObject();
    cJSON *vision = cJSON_CreateObject(), *paddle = cJSON_CreateObject(), *plugins = cJSON_CreateObject();
    cJSON *plugin_config = cJSON_CreateObject(), *music = cJSON_CreateObject(), *region = cJSON_CreateArray();
    cJSON *downloads = cJSON_CreateObject();
    add_string(game, "window_title", config->window_title);
    cJSON_AddItemToObject(root, "game", game);
    cJSON_AddNumberToObject(bot, "check_interval", config->trigger_interval_ms / 1000.0);
    cJSON_AddNumberToObject(bot, "max_chars", config->max_chars);
    cJSON_AddNumberToObject(bot, "history_turns", config->history_turns);
    cJSON_AddNumberToObject(bot, "send_interval_ms", config->send_interval_ms);
    cJSON_AddBoolToObject(bot, "log_overlay", config->log_overlay_enabled);
    add_string(bot, "wake_word", config->wake_word);
    cJSON *blacklist = cJSON_CreateArray();
    for (int i = 0; i < config->bot_blacklist_count; ++i) {
        char keyword[512];
        if (wide_to_utf8(config->bot_blacklist[i], keyword, sizeof(keyword)))
            cJSON_AddItemToArray(blacklist, cJSON_CreateString(keyword));
    }
    cJSON_AddItemToObject(bot, "blacklist", blacklist);
    cJSON_AddItemToObject(root, "bot", bot);
    add_string(ai, "provider", L"custom");
    add_string(ai, "base_url", config->ai_base_url);
    add_string(ai, "api_key_env", config->ai_key_env);
    add_string(ai, "api_key", config->ai_key);
    add_string(ai, "model", config->ai_model);
    cJSON_AddNumberToObject(ai, "temperature", config->temperature);
    cJSON_AddNumberToObject(ai, "max_tokens", config->max_tokens);
    add_string(ai, "personality", config->personality);
    cJSON_AddItemToObject(root, "ai", ai);
    add_string(downloads, "source", config->download_source);
    cJSON_AddItemToObject(root, "downloads", downloads);
    add_string(vision, "engine", config->vision_engine);
    add_string(vision, "screenshot_mode", config->screenshot_mode);
    add_string(vision, "inference_device", config->inference_device);
    cJSON_AddNumberToObject(vision, "trigger_interval_ms", config->trigger_interval_ms);
    cJSON_AddNumberToObject(vision, "ocr_interval_ms", config->ocr_interval_ms);
    cJSON_AddNumberToObject(vision, "ocr_threads", config->ocr_threads);
    for (int i = 0; i < 4; ++i) cJSON_AddItemToArray(region, cJSON_CreateNumber(config->region[i]));
    cJSON_AddItemToObject(vision, "chat_region", region);
    add_string(paddle, "ocr_version", config->ocr_version);
    add_string(paddle, "model_tier", config->model_tier);
    cJSON_AddNumberToObject(paddle, "score_threshold_percent", config->score_threshold_percent);
    cJSON_AddItemToObject(vision, "paddle", paddle);
    cJSON *vision_ai = cJSON_CreateObject();
    add_string(vision_ai, "base_url", config->vision_base_url);
    add_string(vision_ai, "model", config->vision_model);
    add_string(vision_ai, "api_key_env", config->vision_key_env);
    add_string(vision_ai, "api_key", config->vision_key);
    add_string(vision_ai, "prompt", config->vision_prompt);
    cJSON_AddItemToObject(vision, "ai", vision_ai);
    cJSON_AddItemToObject(root, "vision", vision);
    add_string(music, "software_path", config->lxmusic_path);
    cJSON_AddNumberToObject(music, "search_timeout", config->music_search_timeout);
    cJSON_AddNumberToObject(music, "max_queue_size", config->music_max_queue_size);
    cJSON *music_blacklist = cJSON_CreateArray();
    for (int i = 0; i < config->music_blacklist_count; ++i) {
        char keyword[512];
        if (wide_to_utf8(config->music_blacklist[i], keyword, sizeof(keyword)))
            cJSON_AddItemToArray(music_blacklist, cJSON_CreateString(keyword));
    }
    cJSON_AddItemToObject(music, "blacklist", music_blacklist);
    cJSON_AddItemToObject(plugin_config, "music_player", music);
    cJSON_AddItemToObject(plugins, "config", plugin_config);
    cJSON *enabled = cJSON_CreateArray();
    BOOL music_written = FALSE;
    for (int i = 0; i < config->enabled_plugin_count; ++i) {
        char plugin_name[256];
        if (!wide_to_utf8(config->enabled_plugins[i], plugin_name, sizeof(plugin_name))) continue;
        if (_stricmp(plugin_name, "music_player") == 0) {
            if (!config->music_enabled || music_written) continue;
            music_written = TRUE;
        }
        cJSON_AddItemToArray(enabled, cJSON_CreateString(plugin_name));
    }
    if (config->music_enabled && !music_written)
        cJSON_AddItemToArray(enabled, cJSON_CreateString("music_player"));
    cJSON_AddItemToObject(plugins, "enabled", enabled);
    cJSON_AddItemToObject(root, "plugins", plugins);
    char *json = cJSON_PrintBuffered(root, 16384, 1);
    cJSON_Delete(root);
    if (!json) return FALSE;
    wchar_t data_dir[MAX_PATH], path[MAX_PATH];
    get_data_dir(data_dir, MAX_PATH);
    _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%s\\config.json", data_dir);
    BOOL ok = write_text_file_atomic(path, json);
    cJSON_free(json);
    return ok;
}
