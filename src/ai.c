#include "app.h"
#include "../third_party/cjson/cJSON.h"
#include <winhttp.h>
#include <wctype.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "user32.lib")

#ifdef CHATGIBOT_INPUT_PROBE
extern HWND WINAPI test_get_foreground_window(void);
extern UINT WINAPI test_send_input(UINT count, LPINPUT inputs, int input_size);
extern void WINAPI test_sleep(DWORD milliseconds);
#define input_foreground_window test_get_foreground_window
#define input_send test_send_input
#define input_sleep test_sleep
#else
#define input_foreground_window GetForegroundWindow
#define input_send SendInput
#define input_sleep Sleep
#endif

static void add_wide(cJSON *object, const char *key, const wchar_t *value) {
    char utf8[4096] = {0};
    wide_to_utf8(value ? value : L"", utf8, sizeof(utf8));
    cJSON_AddStringToObject(object, key, utf8);
}

static BOOL get_api_key(const AppConfig *config, wchar_t *key, int key_chars) {
    key[0] = 0;
    if (config->ai_key_env[0]) GetEnvironmentVariableW(config->ai_key_env, key, key_chars);
    if (!key[0]) wcscpy_s(key, key_chars, config->ai_key);
    return key[0] != 0;
}

static BOOL ai_request(const AppConfig *config, const char *body, char **response, DWORD *status) {
    URL_COMPONENTSW parts = { sizeof(parts) };
    wchar_t host[256] = L"", path[2048] = L"", extra[2048] = L"", key[256] = L"";
    wchar_t headers[1024] = L"Content-Type: application/json\r\n";
    parts.dwHostNameLength = 255; parts.lpszHostName = host;
    parts.dwUrlPathLength = 2047; parts.lpszUrlPath = path;
    parts.dwExtraInfoLength = 2047; parts.lpszExtraInfo = extra;
    *response = NULL; *status = 0;
    if (!WinHttpCrackUrl(config->ai_base_url, 0, 0, &parts)) return FALSE;
    get_api_key(config, key, 256);
    if (key[0]) {
        wchar_t auth[384];
        _snwprintf_s(auth, 384, _TRUNCATE, L"Authorization: Bearer %s\r\n", key);
        wcscat_s(headers, 1024, auth);
    }
    HINTERNET session = WinHttpOpen(APP_NAME, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET connect = session ? WinHttpConnect(session, host, parts.nPort, 0) : NULL;
    wchar_t endpoint[4096], suffix[64] = L"/chat/completions";
    if (path[0] && path[wcslen(path) - 1] == L'/')
        _snwprintf_s(endpoint, 4096, _TRUNCATE, L"%schat/completions%s", path, extra);
    else
        _snwprintf_s(endpoint, 4096, _TRUNCATE, L"%s%s%s", path, suffix, extra);
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"POST", endpoint, NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : NULL;
    BOOL ok = FALSE;
    if (request) {
        int timeout = 30000;
        WinHttpSetOption(request, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        WinHttpSetOption(request, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
        WinHttpSetOption(request, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
        ok = WinHttpSendRequest(request, headers, (DWORD)-1, (LPVOID)body, (DWORD)strlen(body),
                                (DWORD)strlen(body), 0) && WinHttpReceiveResponse(request, NULL);
        if (ok) {
            DWORD size = sizeof(*status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, status, &size, WINHTTP_NO_HEADER_INDEX);
            DWORD capacity = 65536, used = 0, read = 0;
            const DWORD maximum = 8 * 1024 * 1024;
            char *buffer = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, capacity);
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
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);
    if (!ok && !*status) app_log_format(L"ERROR", L"WinHTTP 网络错误：%lu", GetLastError());
    return ok;
}

static BOOL append_json_text(cJSON *item, char *output, size_t output_bytes, size_t *used) {
    cJSON *text = cJSON_IsString(item) ? item : cJSON_GetObjectItemCaseSensitive(item, "text");
    if (!cJSON_IsString(text) || !text->valuestring) return FALSE;
    size_t length = strlen(text->valuestring);
    if (length >= output_bytes - *used) return FALSE;
    memcpy(output + *used, text->valuestring, length);
    *used += length;
    output[*used] = 0;
    return TRUE;
}

static BOOL json_content_to_wide(cJSON *content, wchar_t *output, int output_chars) {
    if (!content || !output || output_chars <= 0) return FALSE;
    char combined[8192] = {0};
    size_t used = 0;
    if (cJSON_IsArray(content)) {
        cJSON *item = NULL;
        BOOL appended = FALSE;
        cJSON_ArrayForEach(item, content) {
            if (append_json_text(item, combined, sizeof(combined), &used)) appended = TRUE;
        }
        if (!appended) return FALSE;
    } else if (!append_json_text(content, combined, sizeof(combined), &used)) {
        return FALSE;
    }
    return utf8_to_wide(combined, output, output_chars);
}

static BOOL extract_reply(const char *response, wchar_t *reply, int reply_chars) {
    cJSON *root = cJSON_Parse(response);
    cJSON *choices = root ? cJSON_GetObjectItemCaseSensitive(root, "choices") : NULL;
    cJSON *choice = cJSON_IsArray(choices) ? cJSON_GetArrayItem(choices, 0) : NULL;
    cJSON *message = choice ? cJSON_GetObjectItemCaseSensitive(choice, "message") : NULL;
    cJSON *content = message ? cJSON_GetObjectItemCaseSensitive(message, "content") : NULL;
    BOOL ok = json_content_to_wide(content, reply, reply_chars);
    if (!ok && choice) {
        cJSON *legacy_text = cJSON_GetObjectItemCaseSensitive(choice, "text");
        ok = json_content_to_wide(legacy_text, reply, reply_chars);
    }
    if (root) cJSON_Delete(root);
    return ok;
}

static BOOL starts_command_instruction(const wchar_t *text) {
    const wchar_t *prefixes[] = { L"发送", L"输入", L"使用", L"请发送", L"请先发送", L"请输入", L"请使用" };
    if (text[0] == L'/' || text[0] == L'／' || text[0] == L'!') return TRUE;
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
        size_t length = wcslen(prefixes[i]);
        if (wcsncmp(text, prefixes[i], length) != 0) continue;
        const wchar_t *slash = wcschr(text + length, L'/');
        const wchar_t *wide_slash = wcschr(text + length, L'／');
        if ((slash && slash - text < 24) || (wide_slash && wide_slash - text < 24)) return TRUE;
    }
    return FALSE;
}

static BOOL html_entity(const wchar_t *text, wchar_t *decoded, int *consumed) {
    struct Entity { const wchar_t *text; wchar_t value; };
    static const struct Entity entities[] = {
        { L"&amp;", L'&' }, { L"&lt;", L'<' }, { L"&gt;", L'>' },
        { L"&quot;", L'"' }, { L"&#39;", L'\'' }, { L"&nbsp;", L' ' }
    };
    for (size_t i = 0; i < sizeof(entities) / sizeof(entities[0]); ++i) {
        size_t length = wcslen(entities[i].text);
        if (_wcsnicmp(text, entities[i].text, length) == 0) {
            *decoded = entities[i].value;
            *consumed = (int)length;
            return TRUE;
        }
    }
    return FALSE;
}

void ai_clean_reply(wchar_t *text, int text_chars) {
    if (!text || text_chars <= 0) return;
    BOOL reject_command = starts_command_instruction(text);
    wchar_t clean[2048] = L"";
    int out = 0; int limit = min(text_chars - 1, 2047); BOOL in_tag = FALSE, in_script = FALSE;
    for (int i = 0; text[i] && out < limit; ++i) {
        wchar_t ch = text[i];
        if (in_script) {
            if (_wcsnicmp(text + i, L"</script>", 9) == 0) { i += 8; in_script = FALSE; }
            continue;
        }
        if (ch == L'/' && (i == 0 || iswspace(text[i - 1]))) {
            while (text[i] && text[i] != L'\r' && text[i] != L'\n') ++i;
            if (!text[i]) break;
            ch = text[i];
        }
        if (ch == L'<' && _wcsnicmp(text + i, L"<script", 7) == 0) { in_script = TRUE; in_tag = TRUE; continue; }
        if (ch == L'<') { in_tag = TRUE; continue; }
        if (in_tag) { if (ch == L'>') in_tag = FALSE; continue; }
        if (_wcsnicmp(text + i, L"https://", 8) == 0 || _wcsnicmp(text + i, L"http://", 7) == 0 ||
            _wcsnicmp(text + i, L"www.", 4) == 0) {
            while (text[i + 1] && !iswspace(text[i + 1])) ++i;
            continue;
        }
        int consumed = 0; wchar_t decoded = 0;
        if (ch == L'&' && html_entity(text + i, &decoded, &consumed)) { ch = decoded; i += consumed - 1; }
        if (ch == L'`' || ch == L'*' || ch == L'#' || ch == L'_' || ch == L'~' || ch == L'|' || ch == L'>') continue;
        if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
        if (ch < 32) continue;
        if (ch == L' ' && out && clean[out - 1] == L' ') continue;
        clean[out++] = ch;
    }
    while (out && clean[out - 1] == L' ') --out;
    clean[out] = 0;
    wchar_t *start = clean;
    while (*start == L' ') ++start;
    if (_wcsnicmp(start, L"派蒙：", 3) == 0 || _wcsnicmp(start, L"派蒙:", 3) == 0) {
        start += 3;
        while (*start == L' ') ++start;
    }
    if (reject_command || starts_command_instruction(start)) start[0] = 0;
    wcscpy_s(text, (size_t)text_chars, start);
}

BOOL ai_chat(App *app, const wchar_t *user_message, wchar_t *reply, int reply_chars) {
    if (!app || !user_message || !reply || reply_chars <= 0) return FALSE;
    reply[0] = 0;
    cJSON *root = cJSON_CreateObject(), *messages = cJSON_CreateArray();
    if (!root || !messages) { if (root) cJSON_Delete(root); if (messages) cJSON_Delete(messages); return FALSE; }
    add_wide(root, "model", app->config.ai_model);
    cJSON_AddNumberToObject(root, "temperature", app->config.temperature);
    cJSON_AddNumberToObject(root, "max_tokens", app->config.max_tokens);
    cJSON_AddItemToObject(root, "messages", messages);
    cJSON *system = cJSON_CreateObject();
    cJSON_AddStringToObject(system, "role", "system"); add_wide(system, "content", app->config.personality);
    cJSON_AddItemToArray(messages, system);
    int history_limit = max(1, min(8, app->config.history_turns));
    int history_start = max(0, app->history_count - history_limit);
    while (history_start < app->history_count) {
        int total = 0;
        for (int i = history_start; i < app->history_count; ++i)
            total += (int)wcslen(app->history_user[i]) + (int)wcslen(app->history_assistant[i]);
        if (total <= 4096 || history_start + 1 >= app->history_count) break;
        ++history_start;
    }
    for (int i = history_start; i < app->history_count; ++i) {
        cJSON *user = cJSON_CreateObject(), *assistant = cJSON_CreateObject();
        cJSON_AddStringToObject(user, "role", "user"); add_wide(user, "content", app->history_user[i]);
        cJSON_AddStringToObject(assistant, "role", "assistant"); add_wide(assistant, "content", app->history_assistant[i]);
        cJSON_AddItemToArray(messages, user); cJSON_AddItemToArray(messages, assistant);
    }
    wchar_t bounded_message[513];
    wcsncpy_s(bounded_message, 513, user_message, _TRUNCATE);
    bounded_message[512] = 0;
    cJSON *current = cJSON_CreateObject();
    cJSON_AddStringToObject(current, "role", "user"); add_wide(current, "content", bounded_message);
    cJSON_AddItemToArray(messages, current);
    char *body = cJSON_PrintUnformatted(root); cJSON_Delete(root);
    if (!body) return FALSE;
    char *response = NULL; DWORD status = 0;
    BOOL ok = ai_request(&app->config, body, &response, &status) && response && status >= 200 && status < 300;
    if (ok) ok = extract_reply(response, reply, reply_chars);
    if (!ok) app_log_format(L"ERROR", L"AI 请求失败，HTTP=%lu", status);
    if (response) HeapFree(GetProcessHeap(), 0, response);
    cJSON_free(body);
    if (ok) {
        ai_clean_reply(reply, reply_chars);
        if (reply[0]) {
            if (app->history_count >= 8) {
                memmove(app->history_user, app->history_user + 1, 7 * sizeof(app->history_user[0]));
                memmove(app->history_assistant, app->history_assistant + 1, 7 * sizeof(app->history_assistant[0]));
                app->history_count = 7;
            }
            wcscpy_s(app->history_user[app->history_count], 512, bounded_message);
            wcsncpy_s(app->history_assistant[app->history_count], 512, reply, 511);
            app->history_assistant[app->history_count][511] = 0;
            app->history_count++;
        }
    }
    return ok && reply[0] != 0;
}

static DWORD WINAPI ai_test_thread(LPVOID parameter) {
    App *app = (App*)parameter;
    InterlockedExchange(&app->busy, 1); ui_set_busy(1);
    int saved_history_count = app->history_count;
    wchar_t saved_history_user[8][512], saved_history_assistant[8][512];
    memcpy(saved_history_user, app->history_user, sizeof(saved_history_user));
    memcpy(saved_history_assistant, app->history_assistant, sizeof(saved_history_assistant));
    wchar_t reply[2048];
    app_log_format(L"INFO", L"测试 AI 服务：%s / %s", app->config.ai_base_url, app->config.ai_model);
    if (ai_chat(app, L"只回复：连接成功", reply, 2048)) app_log_format(L"INFO", L"AI 返回：%s", reply);
    else app_log_format(L"ERROR", L"AI 测试失败，请检查 Base URL、模型和 Key");
    app->history_count = saved_history_count;
    memcpy(app->history_user, saved_history_user, sizeof(saved_history_user));
    memcpy(app->history_assistant, saved_history_assistant, sizeof(saved_history_assistant));
    InterlockedExchange(&app->busy, 0); ui_set_busy(0);
    return 0;
}

BOOL ai_test_async(App *app) {
    if (InterlockedCompareExchange(&app->running, 1, 1)) return FALSE;
    background_reap(app);
    if (app->task_thread) return FALSE;
    if (InterlockedCompareExchange(&app->busy, 1, 0) != 0) return FALSE;
    HANDLE thread = CreateThread(NULL, 0, ai_test_thread, app, 0, NULL);
    if (!thread) { InterlockedExchange(&app->busy, 0); return FALSE; }
    app->task_thread = thread;
    ui_set_busy(1);
    return TRUE;
}

typedef struct ClipboardBackup {
    UINT format;
    HGLOBAL global;
    HANDLE handle;
    int kind;
} ClipboardBackup;

static void clipboard_free_backup(ClipboardBackup *items, int count);

static BOOL clipboard_backup_all(ClipboardBackup *items, int capacity, int *count) {
    if (!items || !count || capacity <= 0) return FALSE;
    *count = 0;
    UINT format = 0;
    BOOL too_many_formats = FALSE;
    while ((format = EnumClipboardFormats(format)) != 0) {
        if (*count >= capacity) {
            too_many_formats = TRUE;
            break;
        }
        HANDLE data = GetClipboardData(format);
        if (!data) continue;
        ClipboardBackup *item = &items[*count];
        memset(item, 0, sizeof(*item));
        item->format = format;
        if (format == CF_BITMAP || format == CF_DSPBITMAP) {
            item->handle = CopyImage(data, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
            item->kind = 1;
        } else if (format == CF_ENHMETAFILE || format == CF_DSPENHMETAFILE) {
            item->handle = CopyEnhMetaFileW((HENHMETAFILE)data, NULL);
            item->kind = 2;
        } else {
            SIZE_T bytes = GlobalSize(data);
            if (!bytes) continue;
            item->global = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (!item->global) continue;
            void *source = GlobalLock(data);
            void *target = GlobalLock(item->global);
            if (!source || !target) {
                if (source) GlobalUnlock(data);
                if (target) GlobalUnlock(item->global);
                GlobalFree(item->global);
                item->global = NULL;
                continue;
            }
            memcpy(target, source, bytes);
            GlobalUnlock(data);
            GlobalUnlock(item->global);
            item->kind = 0;
        }
        if ((item->kind == 0 && item->global) || (item->kind != 0 && item->handle)) ++*count;
    }
    if (too_many_formats) {
        clipboard_free_backup(items, *count);
        *count = 0;
        return FALSE;
    }
    return TRUE;
}

static BOOL clipboard_restore_all(ClipboardBackup *items, int count) {
    BOOL restored = TRUE;
    for (int i = 0; i < count; ++i) {
        ClipboardBackup *item = &items[i];
        HANDLE data = item->kind == 0 ? item->global : item->handle;
        if (!data || !SetClipboardData(item->format, data)) {
            /* 保留 item 的所有权，便于后续重试；最终由 clipboard_free_backup 释放 */
            restored = FALSE;
            continue;
        }
        item->global = NULL;
        item->handle = NULL;
    }
    return restored;
}

static void clipboard_free_backup(ClipboardBackup *items, int count) {
    for (int i = 0; i < count; ++i) {
        if (items[i].global) GlobalFree(items[i].global);
        if (items[i].kind == 1 && items[i].handle) DeleteObject(items[i].handle);
        if (items[i].kind == 2 && items[i].handle) DeleteEnhMetaFile((HENHMETAFILE)items[i].handle);
    }
}

static BOOL send_key(HWND game, WORD key) {
    if (input_foreground_window() != game) return FALSE;
    INPUT input[2] = {0};
    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = key;
    input[1] = input[0];
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    UINT sent = input_send(2, input, sizeof(INPUT));
    if (sent != 2) {
        app_log_format(L"ERROR", L"键盘输入注入失败（返回 %u）；请确认机器人与游戏以相同权限运行", sent);
    }
    return sent == 2;
}

static BOOL open_clipboard_retry(void) {
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (OpenClipboard(g_app.hwnd)) return TRUE;
        Sleep(20);
    }
    return FALSE;
}

BOOL send_chat_input_sequence(HWND game) {
    if (!game || input_foreground_window() != game) return FALSE;
    if (!send_key(game, VK_RETURN)) return FALSE;
    input_sleep(120);
    if (input_foreground_window() != game) return FALSE;
    INPUT paste[4] = {0};
    paste[0].type = INPUT_KEYBOARD; paste[0].ki.wVk = VK_CONTROL;
    paste[1].type = INPUT_KEYBOARD; paste[1].ki.wVk = 'V';
    paste[2] = paste[1]; paste[2].ki.dwFlags = KEYEVENTF_KEYUP;
    paste[3] = paste[0]; paste[3].ki.dwFlags = KEYEVENTF_KEYUP;
    if (input_send(4, paste, sizeof(INPUT)) != 4) return FALSE;
    input_sleep(80);
    return send_key(game, VK_RETURN);
}

BOOL send_text_to_foreground(const wchar_t *text) {
    if (!text) return FALSE;
    HWND game = find_game_window(g_app.config.window_title);
    if (!game) game = find_game_window(L"原神");
    if (!game || GetForegroundWindow() != game) {
        app_log_format(L"WARNING", L"发送取消：原神窗口未处于前台");
        return FALSE;
    }
    if (!open_clipboard_retry()) {
        app_log_format(L"WARNING", L"发送取消：剪贴板当前被其他程序占用");
        return FALSE;
    }
    ClipboardBackup backup[64];
    int backup_count = 0;
    if (!clipboard_backup_all(backup, 64, &backup_count)) {
        CloseClipboard();
        clipboard_free_backup(backup, backup_count);
        app_log_format(L"WARNING", L"发送取消：无法安全保存当前剪贴板内容");
        return FALSE;
    }
    if (!EmptyClipboard()) {
        CloseClipboard();
        clipboard_free_backup(backup, backup_count);
        app_log_format(L"WARNING", L"发送取消：无法清空剪贴板，已保留原内容");
        return FALSE;
    }
    SIZE_T bytes = (wcslen(text) + 1) * sizeof(wchar_t);
    HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, bytes);
    BOOL ok = FALSE;
    if (global) {
        void *locked = GlobalLock(global);
        if (locked) { memcpy(locked, text, bytes); GlobalUnlock(global); ok = SetClipboardData(CF_UNICODETEXT, global) != NULL; }
        if (!ok) GlobalFree(global);
    }
    CloseClipboard();
    if (ok) ok = send_chat_input_sequence(game);
    Sleep(100);
    BOOL cleared = FALSE;
    BOOL restored = FALSE;
    for (int attempt = 0; attempt < 3 && !restored; ++attempt) {
        if (attempt) Sleep(50);
        if (open_clipboard_retry()) {
            if (!cleared) {
                if (!EmptyClipboard()) {
                    CloseClipboard();
                    continue;
                }
                cleared = TRUE;
            }
            restored = clipboard_restore_all(backup, backup_count);
            CloseClipboard();
        }
    }
    if (!restored) app_log_format(L"WARNING", L"发送完成，但恢复剪贴板失败");
    clipboard_free_backup(backup, backup_count);
    return ok;
}
