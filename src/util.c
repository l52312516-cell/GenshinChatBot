#include "app.h"

void background_reap(App *app) {
    if (app && app->task_thread && WaitForSingleObject(app->task_thread, 0) == WAIT_OBJECT_0) {
        CloseHandle(app->task_thread);
        app->task_thread = NULL;
    }
}

BOOL background_task_active(App *app) {
    background_reap(app);
    return app && app->task_thread != NULL;
}
#include <shlwapi.h>
#include <stdio.h>
#include <time.h>
#include <bcrypt.h>
#include <wctype.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "bcrypt.lib")

App g_app;

BOOL utf8_to_wide(const char *input, wchar_t *output, int output_chars) {
    return MultiByteToWideChar(CP_UTF8, 0, input, -1, output, output_chars) > 0;
}

BOOL wide_to_utf8(const wchar_t *input, char *output, int output_bytes) {
    return WideCharToMultiByte(CP_UTF8, 0, input, -1, output, output_bytes, NULL, NULL) > 0;
}

void local_time_text(wchar_t *output, int chars) {
    __time64_t now = time(NULL);
    struct tm tm_value;
    _localtime64_s(&tm_value, &now);
    _snwprintf_s(output, (size_t)chars, _TRUNCATE, L"%04d-%02d-%02d %02d:%02d:%02d",
                 tm_value.tm_year + 1900, tm_value.tm_mon + 1, tm_value.tm_mday,
                 tm_value.tm_hour, tm_value.tm_min, tm_value.tm_sec);
}

int system_logical_processor_count(void) {
    DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    return count ? (int)count : 1;
}

BOOL ocr_line_is_system(const wchar_t *text) {
    if (!text || !text[0]) return TRUE;
    static const wchar_t *markers[] = {
        L"系统", L"获得", L"加入了队伍", L"离开了队伍", L"邀请", L"申请",
        L"开始挑战", L"完成挑战", L"世界等级", L"进入了多人游戏", L"大厅邀请", L"组队邀请"
    };
    for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); ++i)
        if (wcsstr(text, markers[i])) return TRUE;
    if (wcsstr(text, L"年") && wcsstr(text, L"月") && wcsstr(text, L"日")) return TRUE;
    for (const wchar_t *cursor = text; cursor[0] && cursor[1]; ++cursor) {
        if (towlower(cursor[0]) == L'l' && towlower(cursor[1]) == L'v' &&
            (cursor[2] == L'.' || cursor[2] == L'·' || iswdigit(cursor[2]))) return TRUE;
    }
    int digits = 0, punctuation = 0, other = 0;
    BOOL has_time_separator = FALSE;
    const wchar_t *first = text;
    while (*first && iswspace(*first)) ++first;
    for (int i = 0; text[i]; ++i) {
        if (iswdigit(text[i])) ++digits;
        else if (text[i] == L':' || text[i] == L'-' || text[i] == L'—' || text[i] == L'~' ||
                 text[i] == L'～' || text[i] == L'.' || iswspace(text[i])) ++punctuation;
        else ++other;
        if (text[i] == L':' || text[i] == L'：') has_time_separator = TRUE;
    }
    if ((*first == L'-' || *first == L'—' || *first == L'~' || *first == L'～') &&
        digits >= 4 && has_time_separator && wcslen(first) <= 32) return TRUE;
    return !other && digits >= 3 && punctuation >= 1;
}

void ocr_clean_text(wchar_t *text, int text_chars) {
    if (!text || text_chars <= 0) return;
    wchar_t cleaned[256] = L"";
    int limit = min(text_chars, (int)(sizeof(cleaned) / sizeof(cleaned[0])));
    int out = 0;
    for (int i = 0; text[i] && out < limit - 1; ++i) {
        wchar_t *end = wcschr(text + i, L'>');
        if (end && end - (text + i) <= 48) {
            wchar_t *equal = wcschr(text + i, L'=');
            BOOL color_tag = FALSE;
            BOOL marker = text[i] == L'<' || text[i] == L'*' || text[i] == L'/' ||
                          (equal && equal < end);
            for (const wchar_t *cursor = text + i; marker && cursor + 3 < end; ++cursor) {
                if (towlower(cursor[0]) == L'o' && towlower(cursor[1]) == L'l' &&
                    towlower(cursor[2]) == L'o' && towlower(cursor[3]) == L'r') {
                    color_tag = TRUE;
                    break;
                }
            }
            if (color_tag) {
                i = (int)(end - text);
                continue;
            }
        }
        wchar_t ch = text[i];
        if (ch == L'<' || ch == L'>' || ch == L'*') continue;
        if (iswspace(ch)) ch = L' ';
        if (ch == L' ' && (!out || cleaned[out - 1] == L' ')) continue;
        cleaned[out++] = ch;
    }
    while (out && cleaned[out - 1] == L' ') --out;
    cleaned[out] = 0;
    wcscpy_s(text, text_chars, cleaned);
}

static wchar_t game_map_character(wchar_t ch) {
    switch (ch) {
        case 0x00A0: case 0x2007: case 0x202F: case 0x3000: return L' ';
        case 0x2018: case 0x2019: case 0x201B: case 0x2032: return L'\'';
        case 0x201C: case 0x201D: case 0x201F: case 0x2033: return L'"';
        case 0x2013: case 0x2014: case 0x2212: case 0xFF0D: return L'-';
        case 0x2026: return L'.';
        case 0x00B7: case 0x2022: return L'·';
        default: return ch;
    }
}

static BOOL game_character_supported(wchar_t ch) {
    if (ch >= 0x20 && ch <= 0x7E) return TRUE;
    if (iswalnum(ch)) return TRUE;
    if ((ch >= 0x2E80 && ch <= 0x9FFF) || (ch >= 0xA000 && ch <= 0xA4CF) ||
        (ch >= 0xAC00 && ch <= 0xD7AF) || (ch >= 0xF900 && ch <= 0xFAFF) ||
        (ch >= 0x3000 && ch <= 0x303F) || (ch >= 0xFF01 && ch <= 0xFF65)) return TRUE;
    return ch == L'·';
}

void game_clean_text(wchar_t *text, int text_chars) {
    if (!text || text_chars <= 0) return;
    wchar_t cleaned[2048] = L"";
    int limit = min(text_chars, (int)(sizeof(cleaned) / sizeof(cleaned[0])));
    int out = 0;
    for (int i = 0; text[i] && out < limit - 1; ++i) {
        wchar_t ch = text[i];
        if (ch >= 0xD800 && ch <= 0xDBFF) {
            if (text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) ++i;
            continue;
        }
        if (ch >= 0xDC00 && ch <= 0xDFFF) continue;
        ch = game_map_character(ch);
        if (ch == L'\r' || ch == L'\n' || ch == L'\t' || ch < 0x20) ch = L' ';
        if (!game_character_supported(ch)) continue;
        if (ch == L' ' && (!out || cleaned[out - 1] == L' ')) continue;
        cleaned[out++] = ch;
    }
    while (out && cleaned[out - 1] == L' ') --out;
    cleaned[out] = 0;
    wcscpy_s(text, text_chars, cleaned);
}

void get_exe_dir(wchar_t *path, DWORD size) {
    DWORD length = GetModuleFileNameW(NULL, path, size);
    if (length == 0 || length == size) {
        path[0] = L'.';
        path[1] = 0;
        return;
    }
    PathRemoveFileSpecW(path);
}

static BOOL directory_writable(const wchar_t *directory) {
    wchar_t probe[MAX_PATH];
    _snwprintf_s(probe, MAX_PATH, _TRUNCATE, L"%s\\.__chatgibot_write_%lu_%lu.tmp", directory, GetCurrentProcessId(), GetCurrentThreadId());
    HANDLE file = CreateFileW(probe, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    CloseHandle(file);
    DeleteFileW(probe);
    return TRUE;
}

static void rotate_log_if_needed(const wchar_t *path) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size = {0};
    BOOL rotate = GetFileSizeEx(file, &size) && size.QuadPart >= 5LL * 1024 * 1024;
    CloseHandle(file);
    if (!rotate) return;
    wchar_t first_backup[MAX_PATH], second_backup[MAX_PATH];
    _snwprintf_s(first_backup, MAX_PATH, _TRUNCATE, L"%s.1", path);
    _snwprintf_s(second_backup, MAX_PATH, _TRUNCATE, L"%s.2", path);
    DeleteFileW(second_backup);
    MoveFileExW(first_backup, second_backup, MOVEFILE_REPLACE_EXISTING);
    MoveFileExW(path, first_backup, MOVEFILE_REPLACE_EXISTING);
}

void get_data_dir(wchar_t *path, DWORD size) {
    wchar_t override[MAX_PATH];
    if (GetEnvironmentVariableW(L"CHATGIBOT_DATA_DIR", override, MAX_PATH) > 0 && ensure_directory(override) && directory_writable(override)) {
        _snwprintf_s(path, (size_t)size, _TRUNCATE, L"%s", override);
        return;
    }
    wchar_t exe_dir[MAX_PATH];
    get_exe_dir(exe_dir, MAX_PATH);
    _snwprintf_s(path, (size_t)size, _TRUNCATE, L"%s", exe_dir);
    if (ensure_directory(path) && directory_writable(path)) return;
    wchar_t local[MAX_PATH];
    ExpandEnvironmentStringsW(L"%LOCALAPPDATA%\\ChatGIBot", local, MAX_PATH);
    _snwprintf_s(path, (size_t)size, _TRUNCATE, L"%s", local);
    ensure_directory(path);
}

void get_runtime_dir(wchar_t *path, DWORD size) {
    wchar_t data[MAX_PATH], local[MAX_PATH];
    get_data_dir(data, MAX_PATH);
    _snwprintf_s(path, (size_t)size, _TRUNCATE, L"%s\\runtime", data);
    if (ensure_directory(path) && directory_writable(path)) return;
    ExpandEnvironmentStringsW(L"%LOCALAPPDATA%\\ChatGIBot\\runtime", local, MAX_PATH);
    _snwprintf_s(path, (size_t)size, _TRUNCATE, L"%s", local);
    ensure_directory(path);
}

void get_runtime_library_path(const AppConfig *config, wchar_t *path, DWORD size) {
    wchar_t runtime[MAX_PATH];
    get_runtime_dir(runtime, MAX_PATH);
    const wchar_t *name = config && wcscmp(config->inference_device, L"directml") == 0
        ? L"onnxruntime_dml.dll" : L"onnxruntime.dll";
    _snwprintf_s(path, (size_t)size, _TRUNCATE, L"%s\\%s", runtime, name);
}

void get_model_dir(wchar_t *path, DWORD size) {
    wchar_t data[MAX_PATH];
    get_data_dir(data, MAX_PATH);
    _snwprintf_s(path, (size_t)size, _TRUNCATE, L"%s\\models", data);
    ensure_directory(path);
}

BOOL ensure_directory(const wchar_t *path) {
    if (!path || !*path) return FALSE;
    if (CreateDirectoryW(path, NULL)) return TRUE;
    if (GetLastError() == ERROR_ALREADY_EXISTS) return TRUE;
    wchar_t parent[MAX_PATH];
    _snwprintf_s(parent, MAX_PATH, _TRUNCATE, L"%s", path);
    if (!PathRemoveFileSpecW(parent) || wcscmp(parent, path) == 0) return FALSE;
    if (!ensure_directory(parent)) return FALSE;
    return CreateDirectoryW(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

BOOL read_text_file(const wchar_t *path, char **data, DWORD *size) {
    if (!data) return FALSE;
    *data = NULL;
    if (size) *size = 0;
    HANDLE handle = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) return FALSE;
    LARGE_INTEGER length;
    if (!GetFileSizeEx(handle, &length) || length.QuadPart > 16 * 1024 * 1024) {
        CloseHandle(handle);
        return FALSE;
    }
    char *buffer = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)length.QuadPart + 1);
    DWORD total = 0, read = 0;
    BOOL ok = buffer != NULL;
    while (ok && total < (DWORD)length.QuadPart) {
        ok = ReadFile(handle, buffer + total, (DWORD)length.QuadPart - total, &read, NULL);
        if (ok) total += read;
        if (ok && read == 0) break;
    }
    CloseHandle(handle);
    if (!ok || total != (DWORD)length.QuadPart) {
        if (buffer) HeapFree(GetProcessHeap(), 0, buffer);
        return FALSE;
    }
    *data = buffer;
    if (size) *size = total;
    return TRUE;
}

BOOL write_text_file_atomic(const wchar_t *path, const char *data) {
    wchar_t temp[MAX_PATH];
    _snwprintf_s(temp, MAX_PATH, _TRUNCATE, L"%s.tmp", path);
    HANDLE handle = CreateFileW(temp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) return FALSE;
    DWORD bytes = (DWORD)strlen(data);
    DWORD written = 0;
    BOOL ok = WriteFile(handle, data, bytes, &written, NULL) && bytes == written;
    CloseHandle(handle);
    if (!ok || !MoveFileExW(temp, path, MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(temp);
        return FALSE;
    }
    return TRUE;
}

void sha256_file(const wchar_t *path, wchar_t *output, int output_chars) {
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    BYTE hash_value[32];
    DWORD hash_length = 32;
    DWORD result_length = 0;
    BYTE buffer[65536];
    DWORD read = 0;
    BOOL read_ok = TRUE;
    output[0] = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0) return;
    if (BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, (PUCHAR)&hash_length, sizeof(hash_length), &result_length, 0) != 0 ||
        hash_length != 32 || BCryptCreateHash(algorithm, &hash, NULL, 0, NULL, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return;
    }
    HANDLE handle = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return;
    }
    while (TRUE) {
        if (!ReadFile(handle, buffer, sizeof(buffer), &read, NULL)) {
            read_ok = FALSE;
            break;
        }
        if (!read) break;
        if (BCryptHashData(hash, buffer, read, 0) != 0) { read_ok = FALSE; break; }
    }
    if (!read_ok) output[0] = 0;
    CloseHandle(handle);
    if (read_ok && BCryptFinishHash(hash, hash_value, sizeof(hash_value), 0) == 0) {
        int pos = 0;
        for (int i = 0; i < 32 && pos < output_chars; ++i)
            pos += _snwprintf_s(output + pos, (size_t)(output_chars - pos), _TRUNCATE, L"%02x", hash_value[i]);
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
}

void app_log_format(const wchar_t *level, const wchar_t *format, ...) {
    wchar_t message[2048];
    wchar_t line[2300];
    wchar_t timestamp[32];
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(message, 2048, _TRUNCATE, format, args);
    va_end(args);
    local_time_text(timestamp, 32);
    _snwprintf_s(line, 2300, _TRUNCATE, L"[%s] [%s] %s\r\n", timestamp, level, message);
    EnterCriticalSection(&g_app.log_lock);
    size_t used = wcslen(g_app.log_text);
    size_t add = wcslen(line);
    if (used + add >= 65536) {
        size_t cut = used + add + 1 - 65536;
        if (cut < used) {
            memmove(g_app.log_text, g_app.log_text + cut, (used - cut + 1) * sizeof(wchar_t));
            used = wcslen(g_app.log_text);
        }
    }
    _snwprintf_s(g_app.log_text + used, 65536 - used, _TRUNCATE, L"%s", line);
    ++g_app.log_revision;
    HWND window = g_app.hwnd;
    wchar_t data_dir[MAX_PATH], path[MAX_PATH];
    get_data_dir(data_dir, MAX_PATH);
    _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%s\\chatgibot-native.log", data_dir);
    rotate_log_if_needed(path);
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        char utf8[4600] = {0};
        int bytes = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, 4600, NULL, NULL);
        DWORD written = 0;
        if (bytes > 0) WriteFile(file, utf8, (DWORD)bytes - 1, &written, NULL);
        CloseHandle(file);
    }
    LeaveCriticalSection(&g_app.log_lock);

    if (window) {
        size_t line_chars = wcslen(line) + 1;
        wchar_t *queued = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, line_chars * sizeof(wchar_t));
        if (queued) {
            memcpy(queued, line, line_chars * sizeof(wchar_t));
            if (!PostMessageW(window, WM_APP_LOG, 0, (LPARAM)queued)) HeapFree(GetProcessHeap(), 0, queued);
        }
    }
}
