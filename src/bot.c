#include "app.h"
#include "../third_party/cjson/cJSON.h"
#include <wctype.h>
#include <stdio.h>
#include <shellapi.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

#define SENT_FRAGMENT_TTL_MS 600000ULL

static BOOL send_chunks_ex(App *app, const wchar_t *text);
static BOOL send_chunks(App *app, const wchar_t *text);
static void process_line_now(App *app, const wchar_t *line);

static void append_truncated(wchar_t *destination, size_t destination_chars, const wchar_t *text) {
    if (!destination || destination_chars == 0 || !text || !text[0]) return;
    wcsncat_s(destination, destination_chars, text, _TRUNCATE);
}

static void bot_release_runtime(App *app) {
    if (!app) return;
    if (app->bot_thread) {
        WaitForSingleObject(app->bot_thread, INFINITE);
        CloseHandle(app->bot_thread);
        app->bot_thread = NULL;
    }
    if (app->message_thread) {
        WaitForSingleObject(app->message_thread, INFINITE);
        CloseHandle(app->message_thread);
        app->message_thread = NULL;
    }
    if (app->plugins_initialized) plugin_manager_shutdown(app);
    if (app->message_event) CloseHandle(app->message_event);
    app->message_event = NULL;
    if (app->message_queue_initialized) {
        DeleteCriticalSection(&app->message_queue_lock);
        app->message_queue_initialized = FALSE;
    }
    if (app->sent_cache_initialized) {
        DeleteCriticalSection(&app->sent_cache_lock);
        app->sent_cache_initialized = FALSE;
    }
}

#ifdef CHATGIBOT_TESTING
static int g_music_launch_result = -1;
static int g_music_launch_calls;
#endif

static BOOL bot_text_is_blocked(const App *app, const wchar_t *text) {
    for (int i = 0; i < app->config.bot_blacklist_count; ++i)
        if (app->config.bot_blacklist[i][0] && wcsstr(text, app->config.bot_blacklist[i])) return TRUE;
    return FALSE;
}

static void bot_sleep(App *app, DWORD milliseconds) {
    DWORD elapsed = 0;
    while (elapsed < milliseconds && InterlockedCompareExchange(&app->running, 1, 1)) {
        DWORD step = min(50, milliseconds - elapsed);
        Sleep(step);
        elapsed += step;
    }
}

static void music_url_encode(const wchar_t *input, wchar_t *output, int output_chars) {
    char utf8[2048] = {0};
    wide_to_utf8(input, utf8, sizeof(utf8));
    int out = 0;
    for (int i = 0; utf8[i] && out < output_chars - 4; ++i) {
        unsigned char ch = (unsigned char)utf8[i];
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            output[out++] = (wchar_t)ch;
        } else {
            wchar_t encoded[8];
            _snwprintf_s(encoded, 8, _TRUNCATE, L"%%%02X", ch);
            for (int j = 0; encoded[j] && out < output_chars - 1; ++j) output[out++] = encoded[j];
        }
    }
    output[out] = 0;
}

static BOOL music_http_get(const wchar_t *url, char **response) {
    *response = NULL;
    URL_COMPONENTSW parts = { sizeof(parts) };
    wchar_t host[256] = L"", path[4096] = L"", extra[4096] = L"", request_path[8192] = L"";
    parts.dwHostNameLength = 255; parts.lpszHostName = host;
    parts.dwUrlPathLength = 4095; parts.lpszUrlPath = path;
    parts.dwExtraInfoLength = 4095; parts.lpszExtraInfo = extra;
    if (!WinHttpCrackUrl(url, 0, 0, &parts)) return FALSE;
    HINTERNET session = WinHttpOpen(APP_NAME, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET connect = session ? WinHttpConnect(session, host, parts.nPort, 0) : NULL;
    _snwprintf_s(request_path, 8192, _TRUNCATE, L"%s%s", path, extra);
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", request_path, NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : NULL;
    BOOL ok = FALSE;
    if (request) {
        int timeout = 7000;
        WinHttpSetTimeouts(request, timeout, timeout, timeout, timeout);
        ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0) && WinHttpReceiveResponse(request, NULL);
        DWORD status = 0, status_size = sizeof(status);
        if (ok && WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX))
            ok = status >= 200 && status < 300;
        DWORD capacity = 65536, used = 0, read = 0;
        const DWORD maximum = 8 * 1024 * 1024;
        char *buffer = ok ? (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, capacity) : NULL;
        BOOL read_ok = buffer != NULL;
        while (read_ok) {
            if (!WinHttpReadData(request, buffer + used, capacity - used - 1, &read)) {
                read_ok = FALSE;
                break;
            }
            if (!read) break;
            used += read;
            if (used + 1 >= capacity) {
                DWORD next = capacity * 2;
                if (next > maximum) { read_ok = FALSE; break; }
                char *grown = (char*)HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, buffer, next);
                if (!grown) { HeapFree(GetProcessHeap(), 0, buffer); buffer = NULL; read_ok = FALSE; break; }
                buffer = grown; capacity = next;
            }
        }
        if (buffer && !read_ok) { HeapFree(GetProcessHeap(), 0, buffer); buffer = NULL; }
        *response = buffer;
        ok = ok && buffer != NULL;
    }
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);
    return ok;
}

static void music_copy_json_string(cJSON *object, const char *key, wchar_t *output, int chars, const wchar_t *fallback) {
    cJSON *item = object ? cJSON_GetObjectItemCaseSensitive(object, key) : NULL;
    if (!cJSON_IsString(item) || !item->valuestring || !utf8_to_wide(item->valuestring, output, chars)) wcscpy_s(output, chars, fallback);
}

static void music_copy_json_string_compat(cJSON *object, const char *primary, const char *legacy,
                                          wchar_t *output, int chars, const wchar_t *fallback) {
    cJSON *item = object ? cJSON_GetObjectItemCaseSensitive(object, primary) : NULL;
    if (!cJSON_IsString(item) && legacy) item = cJSON_GetObjectItemCaseSensitive(object, legacy);
    if (!cJSON_IsString(item) || !item->valuestring || !utf8_to_wide(item->valuestring, output, chars))
        wcscpy_s(output, chars, fallback);
}

static void music_trim_name(wchar_t *text) {
    wchar_t *tag = wcschr(text, L'<');
    if (tag) *tag = 0;
    wchar_t *start = text;
    while (*start && iswspace(*start)) ++start;
    if (start != text) memmove(text, start, (wcslen(start) + 1) * sizeof(wchar_t));
    size_t length = wcslen(text);
    while (length && iswspace(text[length - 1])) text[--length] = 0;
}

static const wchar_t *music_skip_space(const wchar_t *text) {
    while (text && iswspace(*text)) ++text;
    return text ? text : L"";
}

static void music_normalize_key(const wchar_t *text, wchar_t *output, int output_chars) {
    int out = 0;
    for (int i = 0; text && text[i] && out < output_chars - 1; ++i) {
        wchar_t ch = text[i];
        if (ch >= 0xFF01 && ch <= 0xFF5E) ch -= 0xFEE0;
        else if (ch == 0x3000) ch = L' ';
        ch = towlower(ch);
        if (iswalnum(ch) || (ch >= 0x2E80 && ch <= 0x9FFF) ||
            (ch >= 0xF900 && ch <= 0xFAFF)) output[out++] = ch;
    }
    output[out] = 0;
}

static const wchar_t *MUSIC_BUILTIN_BLACKLIST[] = {
    L"威風堂堂",
    L"威风堂堂",
    L"北京混子",
    L"不想上学",
    L"自杀日记",
    L"摇头玩",
    L"我的梦中情人"
};

static BOOL music_contains_ci(const wchar_t *text, const wchar_t *needle) {
    if (!text || !needle || !needle[0]) return FALSE;
    wchar_t normalized_text[512], normalized_needle[256];
    music_normalize_key(text, normalized_text, 512);
    music_normalize_key(needle, normalized_needle, 256);
    return normalized_needle[0] && wcsstr(normalized_text, normalized_needle) != NULL;
}

static BOOL music_parse_number(const wchar_t *text, int *number) {
    wchar_t *end = NULL;
    const wchar_t *start = music_skip_space(text);
    long parsed = wcstol(start, &end, 10);
    if (end == start || parsed < 1 || parsed > 2147483647L) return FALSE;
    while (end && iswspace(*end)) ++end;
    if (end && *end) return FALSE;
    *number = (int)parsed;
    return TRUE;
}

static BOOL music_is_blocked(const App *app, const wchar_t *name, const wchar_t *singer) {
    for (size_t i = 0; i < sizeof(MUSIC_BUILTIN_BLACKLIST) / sizeof(MUSIC_BUILTIN_BLACKLIST[0]); ++i)
        if (music_contains_ci(name, MUSIC_BUILTIN_BLACKLIST[i]) || music_contains_ci(singer, MUSIC_BUILTIN_BLACKLIST[i])) return TRUE;
    for (int i = 0; i < app->config.music_blacklist_count; ++i) {
        if (music_contains_ci(name, app->config.music_blacklist[i]) || music_contains_ci(singer, app->config.music_blacklist[i])) return TRUE;
    }
    return FALSE;
}

static BOOL music_file_exists(const wchar_t *path) {
    DWORD attributes = path && path[0] ? GetFileAttributesW(path) : INVALID_FILE_ATTRIBUTES;
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static BOOL music_find_lx_path(App *app, wchar_t *path, int path_chars) {
    wchar_t candidates[8][MAX_PATH];
    int count = 0;
    if (app && app->config.lxmusic_path[0]) wcscpy_s(candidates[count++], MAX_PATH, app->config.lxmusic_path);
    wcscpy_s(candidates[count++], MAX_PATH, L"C:\\Program Files\\lx-music-desktop\\lx-music-desktop.exe");
    wcscpy_s(candidates[count++], MAX_PATH, L"C:\\Program Files (x86)\\lx-music-desktop\\lx-music-desktop.exe");
    wcscpy_s(candidates[count++], MAX_PATH, L"D:\\Program Files\\lx-music-desktop\\lx-music-desktop.exe");
    wchar_t local[MAX_PATH], expanded[MAX_PATH];
    ExpandEnvironmentStringsW(L"%LOCALAPPDATA%\\Programs\\lx-music-desktop\\lx-music-desktop.exe", local, MAX_PATH);
    wcscpy_s(candidates[count++], MAX_PATH, local);
    get_exe_dir(expanded, MAX_PATH);
    _snwprintf_s(candidates[count++], MAX_PATH, _TRUNCATE, L"%s\\lx-music-desktop.exe", expanded);
    for (int i = 0; i < count; ++i) {
        if (music_file_exists(candidates[i])) {
            wcscpy_s(path, path_chars, candidates[i]);
            if (app && !app->config.lxmusic_path[0]) {
                wcscpy_s(app->config.lxmusic_path, MAX_PATH, candidates[i]);
                config_save(&app->config);
                app_log_format(L"INFO", L"自动找到 LxMusic：%s", candidates[i]);
            }
            return TRUE;
        }
    }
    if (path && path_chars > 0) path[0] = 0;
    return FALSE;
}

static void music_make_display(MusicItem *item) {
    _snwprintf_s(item->display, 280, _TRUNCATE, L"%s - %s", item->name, item->singer);
}

static void music_clean_singer(wchar_t *text) {
    const wchar_t *separators = L",，;；、/&及";
    for (wchar_t *cursor = text; cursor && *cursor; ++cursor) {
        if (wcschr(separators, *cursor)) {
            *cursor = 0;
            break;
        }
    }
    music_trim_name(text);
}

static int music_search(App *app, const wchar_t *keyword, BOOL kugou) {
    wchar_t encoded[4096], url[8192];
    music_url_encode(keyword, encoded, 4096);
    if (kugou) _snwprintf_s(url, 8192, _TRUNCATE, L"https://songsearch.kugou.com/song_search_v2?keyword=%s&page=1&pagesize=4&platform=WebFilter", encoded);
    else _snwprintf_s(url, 8192, _TRUNCATE, L"https://music.163.com/api/search/get/web?s=%s&type=1&limit=4&offset=0", encoded);
    char *response = NULL;
    if (!music_http_get(url, &response) || !response) return 0;
    cJSON *root = cJSON_Parse(response);
    cJSON *songs = root ? cJSON_GetObjectItemCaseSensitive(root, kugou ? "data" : "result") : NULL;
    if (songs) {
        songs = cJSON_GetObjectItemCaseSensitive(songs, kugou ? "lists" : "songs");
        if (kugou && !cJSON_IsArray(songs)) {
            cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
            songs = data ? cJSON_GetObjectItemCaseSensitive(data, "info") : NULL;
        }
    }
    int count = 0;
    if (cJSON_IsArray(songs)) {
        cJSON *song = NULL;
        cJSON_ArrayForEach(song, songs) {
            if (count >= 4) break;
            MusicItem *item = &app->music_results[count];
            memset(item, 0, sizeof(*item));
            if (kugou) music_copy_json_string_compat(song, "SongName", "songname", item->name, 128, L"未知歌曲");
            else music_copy_json_string(song, "name", item->name, 128, L"未知歌曲");
            if (kugou) {
                music_copy_json_string_compat(song, "SingerName", "singername", item->singer, 128, L"未知歌手");
                cJSON *duration = cJSON_GetObjectItemCaseSensitive(song, "Duration");
                if (!cJSON_IsNumber(duration)) duration = cJSON_GetObjectItemCaseSensitive(song, "duration");
                item->duration_seconds = cJSON_IsNumber(duration) ? duration->valueint : 180;
            } else {
                cJSON *artists = cJSON_GetObjectItemCaseSensitive(song, "artists");
                cJSON *artist = cJSON_IsArray(artists) ? cJSON_GetArrayItem(artists, 0) : NULL;
                music_copy_json_string(artist, "name", item->singer, 128, L"未知歌手");
                cJSON *duration = cJSON_GetObjectItemCaseSensitive(song, "duration");
                item->duration_seconds = cJSON_IsNumber(duration) ? duration->valueint / 1000 : 180;
            }
            music_trim_name(item->name); music_clean_singer(item->singer);
            if (!item->name[0] || music_is_blocked(app, item->name, item->singer)) continue;
            wcscpy_s(item->source, 8, kugou ? L"kg" : L"wy");
            item->duration_seconds = max(5, item->duration_seconds);
            music_make_display(item);
            ++count;
        }
    }
    if (root) cJSON_Delete(root);
    HeapFree(GetProcessHeap(), 0, response);
    return count;
}

static BOOL music_launch_uri(App *app, const wchar_t *uri) {
    wchar_t lx_path[MAX_PATH], command[8192];
    if (!music_find_lx_path(app, lx_path, MAX_PATH)) return FALSE;
    _snwprintf_s(command, 8192, _TRUNCATE, L"\"%s\" \"%s\"", lx_path, uri);
    STARTUPINFOW startup = { sizeof(startup) }; PROCESS_INFORMATION process = {0};
    BOOL ok = CreateProcessW(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &startup, &process);
    if (ok) { CloseHandle(process.hThread); CloseHandle(process.hProcess); }
    if (!ok) {
        wchar_t parameters[5000];
        _snwprintf_s(parameters, 5000, _TRUNCATE, L"\"%s\"", uri);
        SHELLEXECUTEINFOW execute = { sizeof(execute) };
        execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
        execute.lpVerb = L"open";
        execute.lpFile = lx_path;
        execute.lpParameters = parameters;
        execute.nShow = SW_SHOWNORMAL;
        ok = ShellExecuteExW(&execute);
        if (execute.hProcess) CloseHandle(execute.hProcess);
    }
    if (!ok) app_log_format(L"ERROR", L"启动 LxMusic 失败：Windows 错误 %lu", GetLastError());
    return ok;
}

static BOOL music_launch(App *app, const MusicItem *item) {
#ifdef CHATGIBOT_TESTING
    ++g_music_launch_calls;
    if (g_music_launch_result >= 0) return g_music_launch_result != 0;
#endif
    cJSON *data = cJSON_CreateObject();
    if (!data) return FALSE;
    char name[512], singer[512];
    wide_to_utf8(item->name, name, sizeof(name)); wide_to_utf8(item->singer, singer, sizeof(singer));
    cJSON_AddStringToObject(data, "name", name); cJSON_AddStringToObject(data, "singer", singer);
    char *json = cJSON_PrintUnformatted(data); cJSON_Delete(data);
    if (!json) return FALSE;
    wchar_t json_wide[2048], encoded[4096], uri[4608];
    utf8_to_wide(json, json_wide, 2048); music_url_encode(json_wide, encoded, 4096); cJSON_free(json);
    _snwprintf_s(uri, 4608, _TRUNCATE, L"lxmusic://music/searchPlay?data=%s", encoded);
    return music_launch_uri(app, uri);
}

static void music_preload(App *app) {
#ifdef CHATGIBOT_TESTING
    (void)app;
#else
    wchar_t path[MAX_PATH];
    if (music_find_lx_path(app, path, MAX_PATH))
        app_log_format(L"INFO", L"LxMusic 已就绪：等待点歌调用");
#endif
}

BOOL bot_send_message(App *app, const wchar_t *text) {
    if (!app || !text || !text[0]) return FALSE;
    return send_chunks(app, text);
}

static BOOL music_play_next(App *app) {
    if (app->music_queue_count <= 0) return FALSE;
    MusicItem next = app->music_queue[0];
    if (!music_launch(app, &next)) {
        app_log_format(L"ERROR", L"无法启动 LxMusic 播放器");
        app->music_playback_tick = GetTickCount64();
        return FALSE;
    }
    app->music_current = next;
    memmove(app->music_queue, app->music_queue + 1, (app->music_queue_count - 1) * sizeof(app->music_queue[0]));
    --app->music_queue_count;
    app->music_playing = TRUE;
    app->music_current.duration_seconds = max(5, app->music_current.duration_seconds);
    app->music_playback_tick = GetTickCount64();
    app_log_format(L"INFO", L"开始播放：%s", app->music_current.display);
    return TRUE;
}

static void music_tick(App *app) {
    ULONGLONG now = GetTickCount64();
    if (app->music_playing) {
        if (now - app->music_playback_tick >= (ULONGLONG)(app->music_current.duration_seconds + 2) * 1000) {
            app->music_playing = FALSE;
            app->music_playback_tick = now;
            if (!music_play_next(app)) {
                app->music_playback_tick = now;
            }
        }
    } else if (app->music_queue_count > 0 && now - app->music_playback_tick >= 10000) {
        if (!music_play_next(app)) {
            app->music_playback_tick = now;
        }
    }
}

static BOOL music_send_search_results(App *app, int count) {
    wchar_t header[320];
    _snwprintf_s(header, _countof(header), _TRUNCATE,
                 L"找到歌曲，请在%d秒内发送 /选歌 序号：", app->config.music_search_timeout);
    BOOL sent = send_chunks_ex(app, header);
    for (int i = 0; i < count && sent; ++i) {
        wchar_t row[400];
        _snwprintf_s(row, _countof(row), _TRUNCATE, L"%d. %s", i + 1, app->music_results[i].display);
        sent = send_chunks_ex(app, row);
    }
    return sent;
}

BOOL music_handle_command(App *app, const wchar_t *text) {
    if (!app || !text || !app->config.music_enabled) return FALSE;
    const wchar_t *argument = NULL; BOOL kugou = FALSE;
    if (wcsncmp(text, L"/点歌", 3) == 0) argument = text + 3;
    else if (wcsncmp(text, L"/k点歌", 4) == 0) { argument = text + 4; kugou = TRUE; }
    if (argument) {
        argument = music_skip_space(argument);
        if (music_is_blocked(app, argument, L"")) return send_chunks_ex(app, L"歌曲关键词命中内置或自定义音乐黑名单，已禁止搜索。");
        wchar_t lx_path[MAX_PATH];
        if (!music_find_lx_path(app, lx_path, MAX_PATH)) {
#ifdef CHATGIBOT_TESTING
            if (g_music_launch_result < 0) return send_chunks_ex(app, L"请先在插件设置中配置或安装 LxMusic。");
#else
            return send_chunks_ex(app, L"请先在插件设置中配置或安装 LxMusic。");
#endif
        }
        if (!*argument) return send_chunks_ex(app, kugou ? L"用法：/k点歌 歌名" : L"用法：/点歌 歌名");
        if (app->music_result_count && GetTickCount64() <= app->music_results_expire)
            return send_chunks_ex(app, L"请先 /选歌 序号，或 /取消当前搜索。");
        app_log_format(L"INFO", L"%s搜索：%s", kugou ? L"酷狗" : L"网易云", argument);
        int count = music_search(app, argument, kugou);
        if (!count) return send_chunks_ex(app, L"没有找到相关歌曲，或音乐搜索服务暂时不可用。");
        app->music_result_count = count;
        app->music_results_expire = GetTickCount64() + (ULONGLONG)app->config.music_search_timeout * 1000;
        return music_send_search_results(app, count);
    }
    if (wcscmp(text, L"/取消") == 0 || wcscmp(text, L"/取消当前搜索") == 0) {
        if (!app->music_result_count) return send_chunks_ex(app, L"当前没有待选择歌曲。");
        app->music_result_count = 0; app->music_results_expire = 0;
        return send_chunks_ex(app, L"已取消当前搜索。");
    }
    if (wcsncmp(text, L"/选歌", 3) == 0) {
        if (!text[3]) return send_chunks_ex(app, L"用法：/选歌 序号");
        if (!app->music_result_count || GetTickCount64() > app->music_results_expire) { app->music_result_count = 0; return send_chunks_ex(app, L"没有可用的搜索结果，请重新点歌。"); }
        int number = 0;
        if (!music_parse_number(text + 3, &number)) return send_chunks_ex(app, L"用法：/选歌 序号");
        if (number > app->music_result_count) return send_chunks_ex(app, L"选歌序号超出范围。");
        if (app->music_queue_count >= app->config.music_max_queue_size) return send_chunks_ex(app, L"播放队列已满。");
        app->music_queue[app->music_queue_count++] = app->music_results[number - 1];
        wchar_t reply[320]; _snwprintf_s(reply, 320, _TRUNCATE, app->music_playing ? L"已加入队列：%s" : L"准备播放：%s", app->music_results[number - 1].display);
        app->music_result_count = 0;
        if (!app->music_playing && !music_play_next(app))
            return send_chunks_ex(app, L"启动 LxMusic 失败，歌曲已保留在队列中。");
        return send_chunks_ex(app, reply);
    }
    if (wcscmp(text, L"/排队列表") == 0) {
        wchar_t message[1200] = L"";
        if (app->music_playing) _snwprintf_s(message, 1200, _TRUNCATE, L"当前：%s\r\n", app->music_current.display);
        else wcscpy_s(message, 1200, L"当前没有播放。\r\n");
        for (int i = 0; i < app->music_queue_count && wcslen(message) < 1050; ++i) {
            wchar_t row[320];
            _snwprintf_s(row, 320, _TRUNCATE, L"%d. %s\r\n", i + 1, app->music_queue[i].display);
            append_truncated(message, _countof(message), row);
        }
        if (!app->music_queue_count) append_truncated(message, _countof(message), L"队列为空。");
        return send_chunks_ex(app, message);
    }
    if (wcscmp(text, L"/下一首") == 0) {
        if (app->music_queue_count == 0) {
            if (app->music_playing) {
                app->music_playing = FALSE;
                memset(&app->music_current, 0, sizeof(app->music_current));
                return send_chunks_ex(app, L"已停止当前歌曲，播放队列为空。");
            }
            return send_chunks_ex(app, L"播放队列为空，无法切换下一首。");
        }
        if (!music_play_next(app)) return send_chunks_ex(app, L"下一首启动失败，当前歌曲继续播放。");
        return send_chunks_ex(app, L"已切换到下一首。");
    }
    if (wcsncmp(text, L"/插队", 3) == 0) {
        const wchar_t *argument = music_skip_space(text + 3);
        int number = 0;
        if (!music_parse_number(argument, &number)) return send_chunks_ex(app, L"用法：/插队 序号");
        if (number < 1 || number > app->music_queue_count)
            return send_chunks_ex(app, app->music_queue_count ? L"插队序号超出范围。" : L"队列为空。");
        MusicItem selected = app->music_queue[number - 1];
        memmove(app->music_queue + 1, app->music_queue, (number - 1) * sizeof(app->music_queue[0]));
        app->music_queue[0] = selected;
        return send_chunks_ex(app, L"已将歌曲插队到下一首。");
    }
    if (wcscmp(text, L"/清空队列") == 0) { app->music_queue_count = 0; return send_chunks_ex(app, L"已清空播放队列。"); }
    if (wcsncmp(text, L"/音乐黑名单", 6) == 0) {
        const wchar_t *keyword = music_skip_space(text + 6);
        if (!*keyword) return send_chunks_ex(app, L"用法：/音乐黑名单 关键词");
        if (music_is_blocked(app, keyword, L"")) return send_chunks_ex(app, L"该关键词已在音乐黑名单中。");
        BOOL exists = FALSE;
        for (int i = 0; i < app->config.music_blacklist_count; ++i)
            if (_wcsicmp(app->config.music_blacklist[i], keyword) == 0) { exists = TRUE; break; }
        if (exists) return send_chunks_ex(app, L"该关键词已在音乐黑名单中。");
        if (app->config.music_blacklist_count >= 32)
            return send_chunks_ex(app, L"音乐黑名单已满，无法继续添加。");
        if (wcslen(keyword) >= _countof(app->config.music_blacklist[0]))
            return send_chunks_ex(app, L"黑名单关键词过长，请缩短后重试。");
        wcscpy_s(app->config.music_blacklist[app->config.music_blacklist_count],
                 _countof(app->config.music_blacklist[0]), keyword);
        ++app->config.music_blacklist_count;
        if (!config_save(&app->config))
            return send_chunks_ex(app, L"关键词已临时添加，但配置保存失败。");
        return send_chunks_ex(app, L"已添加音乐黑名单关键词。");
    }
    if (wcsncmp(text, L"/移除黑名单", 6) == 0) {
        const wchar_t *keyword = music_skip_space(text + 6);
        if (!*keyword) return send_chunks_ex(app, L"用法：/移除黑名单 关键词");
        for (size_t i = 0; i < sizeof(MUSIC_BUILTIN_BLACKLIST) / sizeof(MUSIC_BUILTIN_BLACKLIST[0]); ++i)
            if (music_contains_ci(keyword, MUSIC_BUILTIN_BLACKLIST[i])) return send_chunks_ex(app, L"该关键词属于内置音乐黑名单，不能移除。");
        for (int i = 0; i < app->config.music_blacklist_count; ++i) {
            if (wcscmp(app->config.music_blacklist[i], keyword) == 0) {
                memmove(app->config.music_blacklist + i, app->config.music_blacklist + i + 1,
                        (app->config.music_blacklist_count - i - 1) * sizeof(app->config.music_blacklist[0]));
                --app->config.music_blacklist_count;
                config_save(&app->config);
                return send_chunks_ex(app, L"已移除音乐黑名单关键词。");
            }
        }
        return send_chunks_ex(app, L"音乐黑名单中没有该关键词。");
    }
    if (wcscmp(text, L"/黑名单列表") == 0) {
        wchar_t message[1000] = L"音乐黑名单（内置：威風堂堂、威风堂堂、北京混子、不想上学、自杀日记、摇头玩、我的梦中情人；自定义：";
        if (!app->config.music_blacklist_count) append_truncated(message, _countof(message), L"空");
        for (int i = 0; i < app->config.music_blacklist_count && wcslen(message) < 900; ++i) {
            append_truncated(message, _countof(message), app->config.music_blacklist[i]);
            if (i + 1 < app->config.music_blacklist_count)
                append_truncated(message, _countof(message), L"、");
        }
        append_truncated(message, _countof(message), L"）");
        return send_chunks_ex(app, message);
    }
    return FALSE;
}

static BOOL is_system_line(const wchar_t *text) {
    return ocr_line_is_system(text);
}

static BOOL extract_command(const wchar_t *line, wchar_t *command, int command_chars) {
    wchar_t corrected[512];
    wcscpy_s(corrected, 512, line ? line : L"");
    static const struct { const wchar_t *source; const wchar_t *target; } corrections[] = {
        { L"1g点歌", L"/点歌" },
        { L"g点歌", L"/点歌" },
        { L"1k点歌", L"/k点歌" },
        { L"1m点歌", L"/m点歌" }
    };
    for (size_t i = 0; i < sizeof(corrections) / sizeof(corrections[0]); ++i) {
        wchar_t *match = wcsstr(corrected, corrections[i].source);
        if (!match) continue;
        wchar_t suffix[512];
        wcscpy_s(suffix, 512, match + wcslen(corrections[i].source));
        wcscpy_s(match, 512 - (size_t)(match - corrected), corrections[i].target);
        wcscat_s(corrected, 512, suffix);
        break;
    }

    static const wchar_t *hash_commands[] = {
        L"reset_confirm", L"stop_confirm", L"音乐黑名单", L"移除黑名单",
        L"k点歌", L"点歌", L"选歌", L"排队列表", L"下一首", L"清空队列", L"插队",
        L"取消当前搜索", L"取消", L"help", L"status", L"reset", L"plugins", L"persona",
        L"stop", L"cancel"
    };
    for (wchar_t *cursor = corrected; *cursor; ++cursor) {
        if (*cursor != L'#' && *cursor != L'＃') continue;
        if (cursor != corrected && !iswspace(cursor[-1])) continue;
        const wchar_t *token = cursor + 1;
        for (size_t i = 0; i < sizeof(hash_commands) / sizeof(hash_commands[0]); ++i) {
            size_t length = wcslen(hash_commands[i]);
            wchar_t next = token[length];
            BOOL valid_suffix = !next || iswspace(next) || iswdigit(next) ||
                                (next >= 0x2E80 && next <= 0x9FFF) ||
                                (next >= 0xFF01 && next <= 0xFF65);
            if (_wcsnicmp(token, hash_commands[i], length) == 0 && valid_suffix) {
                *cursor = L'/';
                break;
            }
        }
    }

    const wchar_t *slash = NULL;
    for (const wchar_t *cursor = corrected; *cursor; ++cursor) {
        if ((*cursor == L'/' || *cursor == L'／') &&
            (cursor == corrected || iswspace(cursor[-1]))) {
            slash = cursor;
            break;
        }
    }
    if (!slash) {
        const wchar_t *start = corrected;
        while (iswspace(*start)) ++start;
        if (wcsncmp(start, L"点歌", 2) == 0 && start[2]) {
            _snwprintf_s(command, command_chars, _TRUNCATE, L"/点歌%s", start + 2);
            return TRUE;
        }
        if (_wcsnicmp(start, L"k点歌", 3) == 0 && start[3]) {
            _snwprintf_s(command, command_chars, _TRUNCATE, L"/k点歌%s", start + 3);
            return TRUE;
        }
    }
    if (!slash) return FALSE;
    const wchar_t *token = slash + 1;
    if (!*token) return FALSE;
    static const wchar_t *known_commands[] = {
        L"reset_confirm", L"stop_confirm", L"音乐黑名单", L"移除黑名单",
        L"k点歌", L"点歌", L"选歌", L"排队列表", L"下一首", L"清空队列", L"插队",
        L"help", L"status", L"reset", L"plugins", L"persona", L"stop", L"cancel"
    };
    const wchar_t *matched = NULL;
    size_t matched_length = 0;
    for (size_t i = 0; i < sizeof(known_commands) / sizeof(known_commands[0]); ++i) {
        size_t length = wcslen(known_commands[i]);
        if (_wcsnicmp(token, known_commands[i], length) == 0 && length > matched_length) {
            wchar_t next = token[length];
            if (!next || iswspace(next) || (wcschr(L"0123456789", next) &&
                (_wcsicmp(known_commands[i], L"选歌") == 0 || _wcsicmp(known_commands[i], L"插队") == 0))) {
                matched = known_commands[i];
                matched_length = length;
            }
        }
    }
    if (!matched) {
        const wchar_t *end = token;
        while (*end && !iswspace(*end)) ++end;
        for (const wchar_t *cursor = token; cursor < end; ++cursor) {
            wchar_t ch = *cursor;
            BOOL supported = iswalnum(ch) || ch == L'_' ||
                (ch >= 0x2E80 && ch <= 0x9FFF) || (ch >= 0xFF01 && ch <= 0xFF65);
            if (!supported) return FALSE;
        }
        wcsncpy_s(command, command_chars, slash, _TRUNCATE);
        if (command[0] == L'／') command[0] = L'/';
        return command[1] != 0;
    }
    const wchar_t *args = token + matched_length;
    while (*args && iswspace(*args)) ++args;
    wchar_t normalized_args[384] = L"";
    if (_wcsicmp(matched, L"选歌") == 0 || _wcsicmp(matched, L"插队") == 0) {
        int out = 0;
        while (args[out] && iswdigit(args[out]) && out < (int)_countof(normalized_args) - 1) {
            normalized_args[out] = args[out];
            ++out;
        }
        normalized_args[out] = 0;
    } else if (_wcsicmp(matched, L"help") == 0 || _wcsicmp(matched, L"status") == 0 ||
               _wcsicmp(matched, L"reset") == 0 || _wcsicmp(matched, L"reset_confirm") == 0 ||
               _wcsicmp(matched, L"plugins") == 0 || _wcsicmp(matched, L"stop") == 0 ||
               _wcsicmp(matched, L"stop_confirm") == 0 || _wcsicmp(matched, L"cancel") == 0 ||
               _wcsicmp(matched, L"排队列表") == 0 || _wcsicmp(matched, L"下一首") == 0 ||
               _wcsicmp(matched, L"清空队列") == 0) {
        normalized_args[0] = 0;
    } else {
        wcsncpy_s(normalized_args, _countof(normalized_args), args, _TRUNCATE);
    }
    _snwprintf_s(command, command_chars, _TRUNCATE, L"/%s%s%s", matched,
                 normalized_args[0] ? L" " : L"", normalized_args);
    return command[1] != 0;
}

static BOOL command_equals(const wchar_t *text, const wchar_t *command) {
    return _wcsicmp(text, command) == 0;
}

static void expire_pending_actions(App *app) {
    if (app->pending_expires_tick && GetTickCount64() > app->pending_expires_tick) {
        app->pending_reset = FALSE;
        app->pending_stop = FALSE;
        app->pending_expires_tick = 0;
    }
}

static BOOL handle_core_command(App *app, const wchar_t *text) {
    expire_pending_actions(app);
    if (command_equals(text, L"/help")) {
        wchar_t plugin_help[1024] = L"";
        wchar_t help[1800] = L"核心：/help /status /reset /reset_confirm /plugins /persona /stop /stop_confirm /cancel；点歌：/点歌 /k点歌 /选歌 /取消 /取消当前搜索 /排队列表 /下一首 /清空队列 /插队 /音乐黑名单 /移除黑名单 /黑名单列表";
        plugin_manager_help(app, plugin_help, 1024);
        if (plugin_help[0]) {
            wcscat_s(help, 1800, L"；插件：");
            wcscat_s(help, 1800, plugin_help);
        }
        return send_chunks(app, help);
    }
    if (command_equals(text, L"/status")) {
        ULONGLONG elapsed = app->bot_started_tick ? (GetTickCount64() - app->bot_started_tick) / 1000 : 0;
        wchar_t status[512];
        _snwprintf_s(status, 512, _TRUNCATE, L"运行 %llu:%02llu:%02llu，处理 %d 条，识图 %s，模型 %s",
                     elapsed / 3600, (elapsed % 3600) / 60, elapsed % 60,
                     app->processed_message_count, app->config.vision_engine, app->config.ai_model);
        return send_chunks(app, status);
    }
    if (command_equals(text, L"/reset")) {
        app->pending_reset = TRUE;
        app->pending_stop = FALSE;
        app->pending_expires_tick = GetTickCount64() + 60000;
        return send_chunks(app, L"确认清空记忆？发送 /reset_confirm 或 /cancel。");
    }
    if (command_equals(text, L"/reset_confirm")) {
        if (!app->pending_reset) return send_chunks(app, L"没有待确认的重置操作。");
        app->history_count = 0;
        memset(app->history_user, 0, sizeof(app->history_user));
        memset(app->history_assistant, 0, sizeof(app->history_assistant));
        app->pending_reset = FALSE;
        app->pending_expires_tick = 0;
        return send_chunks(app, L"记忆已清空。");
    }
    if (command_equals(text, L"/plugins")) {
        wchar_t plugins[1024] = L"";
        plugin_manager_list(app, plugins, 1024);
        wchar_t message[1300];
        _snwprintf_s(message, 1300, _TRUNCATE, L"内置点歌：%s；动态插件：%s",
                     !app->config.music_enabled ? L"停用" : (app->config.lxmusic_path[0] ? L"已启用" : L"未配置 LxMusic"), plugins);
        if (app->config.music_enabled && !wcsstr(message, L"music_player")) {
            wchar_t with_music[1300];
            _snwprintf_s(with_music, 1300, _TRUNCATE, L"%s、music_player", message);
            wcscpy_s(message, 1300, with_music);
        }
        return send_chunks(app, message);
    }
    if (_wcsnicmp(text, L"/persona", 8) == 0 && (text[8] == 0 || iswspace(text[8]))) {
        const wchar_t *persona = text + 8;
        while (iswspace(*persona)) ++persona;
        if (!*persona) return send_chunks(app, L"用法：/persona 新人设");
        wcsncpy_s(app->config.personality, 1024, persona, 800);
        if (config_save(&app->config)) return send_chunks(app, L"人设已更新。");
        return send_chunks(app, L"人设已临时生效，但保存失败。");
    }
    if (command_equals(text, L"/stop")) {
        app->pending_stop = TRUE;
        app->pending_reset = FALSE;
        app->pending_expires_tick = GetTickCount64() + 60000;
        return send_chunks(app, L"确认停止？发送 /stop_confirm 或 /cancel。");
    }
    if (command_equals(text, L"/stop_confirm")) {
        if (!app->pending_stop) return send_chunks(app, L"没有待确认的停止操作。");
        BOOL sent = send_chunks(app, L"派蒙要休息了，再见旅行者~");
        app->pending_stop = FALSE;
        app->pending_expires_tick = 0;
        if (sent) InterlockedExchange(&app->running, 0);
        return sent;
    }
    if (command_equals(text, L"/cancel")) {
        if (!app->pending_reset && !app->pending_stop) return send_chunks(app, L"当前没有待取消操作。");
        app->pending_reset = FALSE;
        app->pending_stop = FALSE;
        app->pending_expires_tick = 0;
        return send_chunks(app, L"已取消操作。");
    }
    return FALSE;
}

static void normalize_message_key(const wchar_t *text, wchar_t *output, int output_chars) {
    int out = 0;
    for (int i = 0; text && text[i] && out < output_chars - 1; ++i) {
        wchar_t ch = text[i];
        if (iswspace(ch)) continue;
        if (ch == 0x2013 || ch == 0x2014 || ch == 0x2212 || ch == 0xFF0D) ch = L'-';
        switch (ch) {
            case L'／': ch = L'/'; break;
            case L'：': ch = L':'; break;
            case L'，': ch = L','; break;
            case L'。': ch = L'.'; break;
            case L'！': ch = L'!'; break;
            case L'？': ch = L'?'; break;
            case L'．': ch = L'.'; break;
            case L'﹒': ch = L'.'; break;
            default: break;
        }
        output[out++] = towlower(ch);
    }
    while (out && output[out - 1] == L' ') --out;
    output[out] = 0;
}

static BOOL seen_before(App *app, const wchar_t *text) {
    wchar_t key[256];
    normalize_message_key(text, key, 256);
    if (!key[0]) return TRUE;
    ULONGLONG now = GetTickCount64();
    for (int i = 0; i < app->seen_message_count; ++i)
        if (wcscmp(app->seen_messages[i], key) == 0 && now - app->seen_message_ticks[i] < 900000) return TRUE;
    for (int i = 0; i < app->seen_message_count; ++i) {
        if (now - app->seen_message_ticks[i] >= 900000) {
            wcscpy_s(app->seen_messages[i], 256, key);
            app->seen_message_ticks[i] = now;
            return FALSE;
        }
    }
    if (app->seen_message_count >= 1024) {
        memmove(app->seen_messages, app->seen_messages + 1, 1023 * sizeof(app->seen_messages[0]));
        memmove(app->seen_message_ticks, app->seen_message_ticks + 1, 1023 * sizeof(app->seen_message_ticks[0]));
        app->seen_message_count = 1023;
    }
    wcscpy_s(app->seen_messages[app->seen_message_count++], 256, key);
    app->seen_message_ticks[app->seen_message_count - 1] = now;
    return FALSE;
}

static BOOL seen_lookup_within(const App *app, const wchar_t *text, ULONGLONG ttl_ms) {
    wchar_t key[256];
    normalize_message_key(text, key, 256);
    if (!key[0]) return TRUE;
    ULONGLONG now = GetTickCount64();
    for (int i = 0; i < app->seen_message_count; ++i)
        if (wcscmp(app->seen_messages[i], key) == 0 && now - app->seen_message_ticks[i] < ttl_ms) return TRUE;
    return FALSE;
}


static BOOL was_sent(App *app, const wchar_t *text) {
    wchar_t compact[256];
    normalize_message_key(text, compact, _countof(compact));
    if (!compact[0]) return TRUE;
    if (app->sent_cache_initialized) EnterCriticalSection(&app->sent_cache_lock);
    ULONGLONG now = GetTickCount64();
    BOOL found = FALSE;
    for (int i = 0; i < app->sent_fragment_count; ++i)
        if (now - app->sent_fragment_ticks[i] < SENT_FRAGMENT_TTL_MS) {
            const wchar_t *previous = app->sent_fragments[i];
            if (wcscmp(previous, compact) == 0) { found = TRUE; break; }
        }
    if (app->sent_cache_initialized) LeaveCriticalSection(&app->sent_cache_lock);
    return found;
}

static void remember_sent(App *app, const wchar_t *text) {
    wchar_t compact[256];
    normalize_message_key(text, compact, _countof(compact));
    if (!compact[0]) return;
    if (app->sent_cache_initialized) EnterCriticalSection(&app->sent_cache_lock);
    if (app->sent_fragment_count >= 64) {
        memmove(app->sent_fragments, app->sent_fragments + 1, 63 * sizeof(app->sent_fragments[0]));
        memmove(app->sent_fragment_ticks, app->sent_fragment_ticks + 1, 63 * sizeof(app->sent_fragment_ticks[0]));
        app->sent_fragment_count = 63;
    }
    wcscpy_s(app->sent_fragments[app->sent_fragment_count++], 256, compact);
    app->sent_fragment_ticks[app->sent_fragment_count - 1] = GetTickCount64();
    if (app->sent_cache_initialized) LeaveCriticalSection(&app->sent_cache_lock);
}

static BOOL wait_for_send_interval(App *app) {
    if (!app->last_send_tick) return TRUE;
    ULONGLONG now = GetTickCount64();
    ULONGLONG interval = (ULONGLONG)max(600, app->config.send_interval_ms);
    ULONGLONG elapsed = now - app->last_send_tick;
    if (elapsed >= interval) return TRUE;
    ULONGLONG remaining = interval - elapsed;
    if (remaining > 0xFFFFFFFFULL) remaining = 0xFFFFFFFFULL;
    bot_sleep(app, (DWORD)remaining);
    return InterlockedCompareExchange(&app->running, 1, 1) != 0;
}

static BOOL send_chunks_ex(App *app, const wchar_t *text) {
    wchar_t safe_text[2048];
    wcsncpy_s(safe_text, 2048, text ? text : L"", _TRUNCATE);
    safe_text[2047] = 0;
    game_clean_text(safe_text, 2048);
    if (!safe_text[0]) {
        app_log_format(L"WARNING", L"发送取消：回复不包含原神支持的文本字符");
        return FALSE;
    }
    if (bot_text_is_blocked(app, safe_text)) {
        app_log_format(L"WARNING", L"发送取消：回复命中机器人黑名单");
        return FALSE;
    }
    int limit = max(1, min(app->config.max_chars, 120));
    int length = (int)wcslen(safe_text), offset = 0;
    BOOL acquired = FALSE;
    for (int attempt = 0; attempt < 500 && !acquired; ++attempt) {
        acquired = InterlockedCompareExchange(&app->io_busy, 1, 0) == 0;
        if (!acquired) bot_sleep(app, 10);
    }
    if (!acquired) {
        app_log_format(L"WARNING", L"发送取消：截图任务占用中，请稍后重试");
        return FALSE;
    }
    BOOL sent_any = FALSE;
    while (offset < length && InterlockedCompareExchange(&app->running, 1, 1)) {
        int count = min(limit, length - offset);
        wchar_t chunk[128]; wcsncpy_s(chunk, 128, safe_text + offset, count); chunk[count] = 0;
        if (!wait_for_send_interval(app) || !send_text_to_foreground(chunk)) break;
        remember_sent(app, chunk);
        offset += count;
        app->last_send_tick = GetTickCount64();
        sent_any = TRUE;
    }
    InterlockedExchange(&app->io_busy, 0);
    return sent_any;
}

static BOOL send_chunks(App *app, const wchar_t *text) {
    return send_chunks_ex(app, text);
}

static BOOL queue_message(App *app, const wchar_t *line) {
    if (!app || !line || !line[0] || !app->message_queue_initialized) return FALSE;
    EnterCriticalSection(&app->message_queue_lock);
    if (app->message_queue_count >= CHATGIBOT_MAX_MESSAGE_QUEUE) {
        LeaveCriticalSection(&app->message_queue_lock);
        app_log_format(L"WARNING", L"消息队列已满，跳过一条新聊天");
        return FALSE;
    }
    BotMessage *message = &app->message_queue[app->message_queue_tail];
    message->type = BOT_MESSAGE_CHAT;
    wcsncpy_s(message->text, 512, line, _TRUNCATE);
    app->message_queue_tail = (app->message_queue_tail + 1) % CHATGIBOT_MAX_MESSAGE_QUEUE;
    ++app->message_queue_count;
    LeaveCriticalSection(&app->message_queue_lock);
    SetEvent(app->message_event);
    return TRUE;
}

static BOOL dequeue_message(App *app, BotMessage *message) {
    BOOL available = FALSE;
    EnterCriticalSection(&app->message_queue_lock);
    if (app->message_queue_count > 0) {
        *message = app->message_queue[app->message_queue_head];
        app->message_queue_head = (app->message_queue_head + 1) % CHATGIBOT_MAX_MESSAGE_QUEUE;
        --app->message_queue_count;
        available = TRUE;
    }
    if (app->message_queue_count == 0) ResetEvent(app->message_event);
    LeaveCriticalSection(&app->message_queue_lock);
    return available;
}

static BOOL message_queue_has_items(App *app) {
    BOOL available;
    EnterCriticalSection(&app->message_queue_lock);
    available = app->message_queue_count > 0;
    LeaveCriticalSection(&app->message_queue_lock);
    return available;
}

static int frame_line_count(const FrameLineCount *lines, int count, const wchar_t *key) {
    for (int i = 0; i < count; ++i)
        if (wcscmp(lines[i].key, key) == 0) return lines[i].count;
    return 0;
}

static int frame_line_index(const FrameLineCount *lines, int count, const wchar_t *key) {
    for (int i = 0; i < count; ++i)
        if (wcscmp(lines[i].key, key) == 0) return i;
    return -1;
}

static void frame_line_add(FrameLineCount *lines, int *count, int capacity, const wchar_t *text) {
    wchar_t key[256];
    normalize_message_key(text, key, _countof(key));
    if (!key[0]) return;
    int index = frame_line_index(lines, *count, key);
    if (index >= 0) {
        ++lines[index].count;
        return;
    }
    if (*count >= capacity) return;
    wcscpy_s(lines[*count].key, _countof(lines[*count].key), key);
    lines[*count].count = 1;
    ++*count;
}

static void process_line_ex(App *app, const wchar_t *line, int current_count, int previous_count) {
    if (!line[0] || is_system_line(line) || was_sent(app, line)) return;
    if (wcslen(line) <= 1 || current_count <= previous_count) return;
    wchar_t command[512];
    ULONGLONG seen_ttl = extract_command(line, command, _countof(command)) ? 10000ULL : 900000ULL;
    if (previous_count <= 0 && seen_lookup_within(app, line, seen_ttl)) return;
    if (!queue_message(app, line)) return;
    seen_before(app, line);
    app_log_format(L"INFO", L"新聊天：%s", line);
}

#ifdef CHATGIBOT_TESTING
static void process_line(App *app, const wchar_t *line) {
    process_line_ex(app, line, 1, 0);
}
#endif

static void process_line_now(App *app, const wchar_t *line) {
    wchar_t command[512];
    if (extract_command(line, command, 512)) {
        if (music_handle_command(app, command) || handle_core_command(app, command)) return;
        wchar_t command_name[128], args[384] = L"";
        wcsncpy_s(command_name, 128, command, _TRUNCATE);
        wchar_t *space = command_name + 1;
        while (*space && !iswspace(*space)) ++space;
        if (*space) {
            wcsncpy_s(args, 384, space + 1, _TRUNCATE);
            *space = 0;
        }
        if (plugin_manager_dispatch(app, command_name, args)) return;
        if (command_name[0] == L'/') {
            send_chunks(app, L"未知指令，可用 /help 查看帮助。");
            return;
        }
    }
    const wchar_t *wake = app->config.wake_word;
    const wchar_t *message = wcsstr(line, wake);
    if (!message) return;
    message += wcslen(wake);
    while (*message && (iswspace(*message) || *message == L':' || *message == L'：' || *message == L',' || *message == L'，')) ++message;
    if (!*message) return;
    wchar_t reply[2048];
    if (!ai_chat(app, message, reply, 2048)) {
        app_log_format(L"WARNING", L"AI 请求失败，发送兜底回复");
        if (!send_chunks(app, L"派蒙好像迷路了...") ) return;
        return;
    }
    ++app->processed_message_count;
    send_chunks(app, reply);
}

static DWORD WINAPI message_thread_proc(LPVOID parameter) {
    App *app = (App*)parameter;
    while (InterlockedCompareExchange(&app->running, 1, 1) || message_queue_has_items(app)) {
        if (InterlockedCompareExchange(&app->paused, 0, 0) &&
            InterlockedCompareExchange(&app->running, 1, 1)) {
            Sleep(100);
            continue;
        }
        WaitForSingleObject(app->message_event, 100);
        BotMessage message;
        while (dequeue_message(app, &message)) process_line_now(app, message.text);
        if (InterlockedCompareExchange(&app->running, 1, 1)) music_tick(app);
    }
    return 0;
}

static void process_capture_lines(App *app, BOOL *baseline, const OcrTextLine *lines, int count) {
    FrameLineCount current_lines[32] = {0};
    int current_line_count = 0;
    for (int i = 0; i < count; ++i)
        frame_line_add(current_lines, &current_line_count, _countof(current_lines), lines[i].text);
    if (!*baseline) {
        for (int i = 0; i < count; ++i) seen_before(app, lines[i].text);
        memcpy(app->previous_frame_lines, current_lines, sizeof(current_lines));
        app->previous_frame_line_count = current_line_count;
        app->pending_ocr_count = 0;
        app->pending_ocr_stable_count = 0;
        *baseline = TRUE;
        return;
    }
    int emitted[32] = {0};
    for (int i = count - 1; i >= 0; --i) {
        wchar_t key[256];
        normalize_message_key(lines[i].text, key, _countof(key));
        int current = frame_line_count(current_lines, current_line_count, key);
        int previous = frame_line_count(app->previous_frame_lines, app->previous_frame_line_count, key);
        int index = frame_line_index(current_lines, current_line_count, key);
        if (index < 0 || emitted[index] >= current - previous) continue;
        ++emitted[index];
        process_line_ex(app, lines[i].text, current, previous);
    }
    memcpy(app->previous_frame_lines, current_lines, sizeof(current_lines));
    app->previous_frame_line_count = current_line_count;
    int saved_count = min(count, (int)_countof(app->pending_ocr_lines));
    memcpy(app->pending_ocr_lines, lines, (size_t)saved_count * sizeof(app->pending_ocr_lines[0]));
    app->pending_ocr_count = saved_count;
    app->pending_ocr_stable_count = saved_count ? 1 : 0;
}

static DWORD WINAPI bot_thread_proc(LPVOID parameter) {
    App *app = (App*)parameter;
    if (wcscmp(app->config.vision_engine, L"local") == 0) {
        wchar_t detail[512];
        if (!ocr_prepare(app, detail, 512)) {
            app_log_format(L"ERROR", L"机器人启动失败：%s；请先在依赖页安装运行库和模型", detail);
            overlay_hide(app);
            InterlockedExchange(&app->running, 0);
            PostMessageW(app->hwnd, WM_APP_STATUS, 0, 0);
            return 0;
        }
        app_log_format(L"INFO", L"识图准备完成：%s", detail);
    }
    app_log_format(L"INFO", L"机器人已启动：窗口 %s，识图引擎 %s", app->config.window_title, app->config.vision_engine);
    BOOL baseline = FALSE;
    BOOL capture_ready_logged = FALSE;
    ULONGLONG last_capture_warning = 0;
    ULONGLONG last_ocr_warning = 0;
    ULONGLONG next_ocr_tick = 0;
    while (InterlockedCompareExchange(&app->running, 1, 1)) {
        if (InterlockedCompareExchange(&app->paused, 0, 0)) { Sleep(150); continue; }
        ULONGLONG now = GetTickCount64();
        if (next_ocr_tick && now < next_ocr_tick) {
            bot_sleep(app, (DWORD)max(50, app->config.trigger_interval_ms));
            continue;
        }
        if (InterlockedCompareExchange(&app->io_busy, 2, 0) != 0) {
            bot_sleep(app, 50);
            continue;
        }
        next_ocr_tick = now + (ULONGLONG)max(500, app->config.ocr_interval_ms);
        RECT rect; int mode = wcscmp(app->config.screenshot_mode, L"printwindow") == 0 ? 1 :
                              wcscmp(app->config.screenshot_mode, L"duplication") == 0 ? 2 : 0;
        HBITMAP bitmap = capture_game_window(app->config.window_title, app->config.region, mode, &rect);
        InterlockedExchange(&app->io_busy, 0);
        if (bitmap) {
            if (!capture_ready_logged) {
                app_log_format(L"INFO", L"已捕获聊天区域：%ld×%ld，开始持续识别", rect.right - rect.left, rect.bottom - rect.top);
                capture_ready_logged = TRUE;
            }
            OcrTextLine lines[32]; int count = ocr_read_bitmap(app, bitmap, lines, 32);
            DeleteObject(bitmap);
            process_capture_lines(app, &baseline, lines, count);
            if (count > 0 && !last_ocr_warning) {
                app_log_format(L"INFO", L"OCR 已识别到 %d 个文本区域；首次扫描仅建立基线，新消息出现后才会处理", count);
                last_ocr_warning = GetTickCount64();
            } else if (count == 0 && (!last_ocr_warning || GetTickCount64() - last_ocr_warning >= 5000)) {
                app_log_format(L"WARNING", L"截图成功但未识别到文本：请检查聊天区域坐标、置信度阈值和聊天是否展开");
                last_ocr_warning = GetTickCount64();
            }
        } else if (!last_capture_warning || GetTickCount64() - last_capture_warning >= 5000) {
            app_log_format(L"WARNING", L"暂时无法捕获原神聊天区域：请确认截图模式和区域设置；BitBlt 失焦时需要游戏支持 PrintWindow");
            last_capture_warning = GetTickCount64();
        }
        bot_sleep(app, (DWORD)max(50, app->config.trigger_interval_ms));
    }
    app_log_format(L"INFO", L"机器人已停止");
    PostMessageW(app->hwnd, WM_APP_STATUS, 0, 0);
    return 0;
}

void bot_reap(App *app) {
    if (app->bot_thread && WaitForSingleObject(app->bot_thread, 0) == WAIT_OBJECT_0) {
        CloseHandle(app->bot_thread);
        app->bot_thread = NULL;
    }
    if (app->message_thread && WaitForSingleObject(app->message_thread, 0) == WAIT_OBJECT_0) {
        CloseHandle(app->message_thread);
        app->message_thread = NULL;
    }
    if (!app->bot_thread && !app->message_thread && app->message_queue_initialized) {
        if (app->plugins_initialized) plugin_manager_shutdown(app);
        if (app->message_event) CloseHandle(app->message_event);
        app->message_event = NULL;
        DeleteCriticalSection(&app->message_queue_lock);
        app->message_queue_initialized = FALSE;
        if (app->sent_cache_initialized) {
            DeleteCriticalSection(&app->sent_cache_lock);
            app->sent_cache_initialized = FALSE;
        }
        InterlockedExchange(&app->io_busy, 0);
    }
}

BOOL bot_thread_active(App *app) {
    bot_reap(app);
    return app->bot_thread != NULL || app->message_thread != NULL;
}

BOOL bot_start(App *app) {
    bot_reap(app);
    if (app->bot_thread || app->message_thread) {
        app_log_format(L"WARNING", L"机器人仍在停止，请稍后再启动");
        return FALSE;
    }
    HWND game = find_game_window(app->config.window_title);
    if (!game) game = find_game_window(L"原神");
    if (!game) {
        app_log_format(L"ERROR", L"启动失败：未找到原神窗口，请先启动游戏或修改窗口标题");
        return FALSE;
    }
    if (wcscmp(app->config.vision_engine, L"vision_ai") == 0 &&
        (!app->config.vision_base_url[0] || !app->config.vision_model[0] ||
         wcsstr(app->config.vision_base_url, L"api.example.com"))) {
        app_log_format(L"ERROR", L"启动失败：视觉 AI 地址或模型尚未配置");
        return FALSE;
    }
    if (wcscmp(app->config.vision_engine, L"vision_ai") == 0) {
        wchar_t key[256] = L"";
        if (app->config.vision_key_env[0]) GetEnvironmentVariableW(app->config.vision_key_env, key, 256);
        if (!key[0] && !app->config.vision_key[0]) {
            app_log_format(L"ERROR", L"启动失败：视觉 AI API Key 尚未配置");
            return FALSE;
        }
    }
    if (!activate_game_window(app->config.window_title)) {
        app_log_format(L"ERROR", L"启动失败：无法激活原神窗口，请手动切换到游戏后重试");
        return FALSE;
    }
    Sleep(250);
    if (!find_game_window(app->config.window_title) && !find_game_window(L"原神")) {
        app_log_format(L"ERROR", L"启动失败：激活后原神窗口已消失");
        return FALSE;
    }
    app_log_format(L"INFO", L"已自动激活原神窗口，准备开始识别");
    InterlockedExchange(&app->running, 1); InterlockedExchange(&app->paused, 0);
    app->bot_started_tick = GetTickCount64();
    app->processed_message_count = 0;
    app->pending_reset = app->pending_stop = FALSE;
    app->pending_expires_tick = 0;
    InitializeCriticalSection(&app->message_queue_lock);
    app->message_queue_initialized = TRUE;
    InitializeCriticalSection(&app->sent_cache_lock);
    app->sent_cache_initialized = TRUE;
    InterlockedExchange(&app->io_busy, 0);
    app->message_queue_head = app->message_queue_tail = app->message_queue_count = 0;
    memset(app->seen_messages, 0, sizeof(app->seen_messages));
    memset(app->seen_message_ticks, 0, sizeof(app->seen_message_ticks));
    app->seen_message_count = 0;
    memset(app->sent_fragments, 0, sizeof(app->sent_fragments));
    memset(app->sent_fragment_ticks, 0, sizeof(app->sent_fragment_ticks));
    app->sent_fragment_count = 0;
    memset(app->pending_ocr_lines, 0, sizeof(app->pending_ocr_lines));
    app->pending_ocr_count = 0;
    app->pending_ocr_stable_count = 0;
    app->last_send_tick = 0;
    app->message_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!app->message_event) {
        DeleteCriticalSection(&app->sent_cache_lock);
        app->sent_cache_initialized = FALSE;
        DeleteCriticalSection(&app->message_queue_lock);
        app->message_queue_initialized = FALSE;
        InterlockedExchange(&app->running, 0);
        app_log_format(L"ERROR", L"消息队列事件创建失败");
        return FALSE;
    }
    plugin_manager_init(app);
    if (app->config.music_enabled) music_preload(app);
    app->message_thread = CreateThread(NULL, 0, message_thread_proc, app, 0, NULL);
    app->bot_thread = CreateThread(NULL, 0, bot_thread_proc, app, 0, NULL);
    if (!app->bot_thread || !app->message_thread) {
        InterlockedExchange(&app->running, 0);
        SetEvent(app->message_event);
        overlay_hide(app);
        app_log_format(L"ERROR", L"机器人后台线程创建失败");
        bot_release_runtime(app);
        return FALSE;
    }
    overlay_show(app);
    PostMessageW(app->hwnd, WM_APP_STATUS, 0, 0);
    return TRUE;
}

void bot_pause(App *app) {
    if (!InterlockedCompareExchange(&app->running, 1, 1)) return;
    LONG paused = InterlockedCompareExchange(&app->paused, 1, 1) ? 0 : 1;
    InterlockedExchange(&app->paused, paused);
    app_log_format(L"INFO", paused ? L"机器人已暂停" : L"机器人已恢复");
    PostMessageW(app->hwnd, WM_APP_STATUS, 0, 0);
}

void bot_stop(App *app) {
    InterlockedExchange(&app->running, 0);
    if (app->message_queue_initialized) {
        EnterCriticalSection(&app->message_queue_lock);
        app->message_queue_head = app->message_queue_tail = app->message_queue_count = 0;
        LeaveCriticalSection(&app->message_queue_lock);
    }
    if (app->message_event) SetEvent(app->message_event);
    overlay_hide(app);
    bot_reap(app);
    app_log_format(L"INFO", app->bot_thread ? L"正在停止机器人..." : L"已停止");
    PostMessageW(app->hwnd, WM_APP_STATUS, 0, 0);
}

#ifdef CHATGIBOT_TESTING
void bot_test_set_music_launch_result(int result) { g_music_launch_result = result; g_music_launch_calls = 0; }
int bot_test_music_launch_calls(void) { return g_music_launch_calls; }
BOOL bot_test_send_chunks(App *app, const wchar_t *text) { return send_chunks(app, text); }
void bot_test_process_line(App *app, const wchar_t *text) { process_line_now(app, text); }
void bot_test_process_capture(App *app, BOOL *baseline, const OcrTextLine *lines, int count) {
    process_capture_lines(app, baseline, lines, count);
}
BOOL bot_test_extract_command(const wchar_t *line, wchar_t *command, int command_chars) {
    return extract_command(line, command, command_chars);
}
BOOL bot_test_seen_before(App *app, const wchar_t *text) { return seen_before(app, text); }
BOOL bot_test_queue_line(App *app, const wchar_t *text) {
    int before = bot_test_queue_count(app);
    process_line(app, text);
    return bot_test_queue_count(app) > before;
}
int bot_test_queue_count(App *app) {
    if (!app || !app->message_queue_initialized) return 0;
    EnterCriticalSection(&app->message_queue_lock);
    int count = app->message_queue_count;
    LeaveCriticalSection(&app->message_queue_lock);
    return count;
}
void bot_test_drain_queue(App *app) {
    BotMessage message;
    while (app && app->message_queue_initialized && dequeue_message(app, &message)) process_line_now(app, message.text);
}
#endif
