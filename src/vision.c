#include "app.h"
#include "../third_party/cjson/cJSON.h"
#include <winhttp.h>
#include <wincodec.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "windowscodecs.lib")

static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

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

static BOOL json_content_to_utf8(cJSON *content, char *output, size_t output_bytes) {
    if (!content || !output || output_bytes == 0) return FALSE;
    output[0] = 0;
    size_t used = 0;
    if (cJSON_IsArray(content)) {
        cJSON *item = NULL;
        BOOL appended = FALSE;
        cJSON_ArrayForEach(item, content) {
            if (append_json_text(item, output, output_bytes, &used)) appended = TRUE;
        }
        return appended;
    }
    return append_json_text(content, output, output_bytes, &used);
}

static void clean_vision_line(wchar_t *text) {
    wchar_t clean[256] = L""; int out = 0; BOOL in_tag = FALSE, in_script = FALSE, in_command = FALSE;
    for (int i = 0; text[i] && out < 255; ++i) {
        wchar_t ch = text[i];
        if (in_script) {
            if (_wcsnicmp(text + i, L"</script>", 9) == 0) { i += 8; in_script = FALSE; }
            continue;
        }
        if (in_command) { if (ch == L' ') in_command = FALSE; else continue; }
        if (ch == L'<' && _wcsnicmp(text + i, L"<script", 7) == 0) { in_script = TRUE; in_tag = TRUE; continue; }
        if (ch == L'<') { in_tag = TRUE; continue; }
        if (in_tag) { if (ch == L'>') in_tag = FALSE; continue; }
        if (ch == L'`' || ch == L'*' || ch == L'#' || ch == L'_' || ch == L'~') continue;
        if (ch == L'/' && (i == 0 || text[i - 1] == L' ')) { in_command = TRUE; continue; }
        if (ch < 32 || ch == L'\r' || ch == L'\n' || ch == L'\t') continue;
        if (ch == L' ' && out && clean[out - 1] == L' ') continue;
        clean[out++] = ch;
    }
    while (out && clean[out - 1] == L' ') --out;
    clean[out] = 0; wcscpy_s(text, 256, clean);
    ocr_clean_text(text, 256);
}

static BOOL bitmap_jpeg(HBITMAP bitmap, BYTE **result, DWORD *result_size) {
    BITMAP info;
    if (!GetObjectW(bitmap, sizeof(info), &info)) return FALSE;
    UINT width = (UINT)info.bmWidth, height = (UINT)abs(info.bmHeight);
    DWORD pixel_bytes = width * height * 4;
    BYTE *pixels = (BYTE*)HeapAlloc(GetProcessHeap(), 0, pixel_bytes);
    if (!pixels) return FALSE;
    BITMAPINFO bitmap_info = {0};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = (LONG)width;
    bitmap_info.bmiHeader.biHeight = -(LONG)height;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;
    HDC dc = GetDC(NULL);
    BOOL ok = GetDIBits(dc, bitmap, 0, height, pixels, &bitmap_info, DIB_RGB_COLORS) != 0;
    ReleaseDC(NULL, dc);
    if (!ok) { HeapFree(GetProcessHeap(), 0, pixels); return FALSE; }

    IWICImagingFactory *factory = NULL;
    IWICBitmap *source = NULL;
    IWICFormatConverter *converter = NULL;
    IWICBitmapEncoder *encoder = NULL;
    IWICBitmapFrameEncode *frame = NULL;
    IStream *stream = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IWICImagingFactory, (void**)&factory);
    if (SUCCEEDED(hr)) hr = factory->lpVtbl->CreateBitmapFromMemory(factory, width, height,
        &GUID_WICPixelFormat32bppBGRA, width * 4, pixel_bytes, pixels, &source);
    if (SUCCEEDED(hr)) hr = factory->lpVtbl->CreateFormatConverter(factory, &converter);
    if (SUCCEEDED(hr)) hr = converter->lpVtbl->Initialize(converter, (IWICBitmapSource*)source,
        &GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr)) hr = CreateStreamOnHGlobal(NULL, TRUE, &stream);
    if (SUCCEEDED(hr)) hr = factory->lpVtbl->CreateEncoder(factory, &GUID_ContainerFormatJpeg, NULL, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->lpVtbl->Initialize(encoder, stream, WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = encoder->lpVtbl->CreateNewFrame(encoder, &frame, NULL);
    if (SUCCEEDED(hr)) hr = frame->lpVtbl->Initialize(frame, NULL);
    if (SUCCEEDED(hr)) hr = frame->lpVtbl->SetSize(frame, width, height);
    if (SUCCEEDED(hr)) {
        WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
        hr = frame->lpVtbl->SetPixelFormat(frame, &format);
    }
    if (SUCCEEDED(hr)) hr = frame->lpVtbl->WriteSource(frame, (IWICBitmapSource*)converter, NULL);
    if (SUCCEEDED(hr)) hr = frame->lpVtbl->Commit(frame);
    if (SUCCEEDED(hr)) hr = encoder->lpVtbl->Commit(encoder);

    BYTE *jpeg = NULL; STATSTG stat = {0}; HGLOBAL global = NULL;
    if (SUCCEEDED(hr)) hr = stream->lpVtbl->Stat(stream, &stat, STATFLAG_NONAME);
    if (SUCCEEDED(hr) && stat.cbSize.QuadPart > 0 && stat.cbSize.QuadPart < 16 * 1024 * 1024 &&
        SUCCEEDED(GetHGlobalFromStream(stream, &global))) {
        void *locked = GlobalLock(global);
        if (locked) {
            *result_size = (DWORD)stat.cbSize.QuadPart;
            jpeg = (BYTE*)HeapAlloc(GetProcessHeap(), 0, *result_size);
            if (jpeg) memcpy(jpeg, locked, *result_size);
            GlobalUnlock(global);
        }
    }
    if (frame) frame->lpVtbl->Release(frame);
    if (encoder) encoder->lpVtbl->Release(encoder);
    if (stream) stream->lpVtbl->Release(stream);
    if (converter) converter->lpVtbl->Release(converter);
    if (source) source->lpVtbl->Release(source);
    if (factory) factory->lpVtbl->Release(factory);
    HeapFree(GetProcessHeap(), 0, pixels);
    if (!jpeg) return FALSE;

    DWORD encoded_size = ((*result_size + 2) / 3) * 4;
    BYTE *encoded = (BYTE*)HeapAlloc(GetProcessHeap(), 0, encoded_size + 1);
    if (!encoded) { HeapFree(GetProcessHeap(), 0, jpeg); return FALSE; }
    for (DWORD i = 0, o = 0; i < *result_size; i += 3) {
        DWORD value = jpeg[i] << 16;
        if (i + 1 < *result_size) value |= jpeg[i + 1] << 8;
        if (i + 2 < *result_size) value |= jpeg[i + 2];
        encoded[o++] = base64_table[(value >> 18) & 63];
        encoded[o++] = base64_table[(value >> 12) & 63];
        encoded[o++] = i + 1 < *result_size ? base64_table[(value >> 6) & 63] : '=';
        encoded[o++] = i + 2 < *result_size ? base64_table[value & 63] : '=';
    }
    encoded[encoded_size] = 0;
    HeapFree(GetProcessHeap(), 0, jpeg);
    *result = encoded;
    *result_size = encoded_size;
    return TRUE;
}

static BOOL vision_request(App *app, const char *body, char **response, DWORD *status) {
    URL_COMPONENTSW parts = { sizeof(parts) };
    wchar_t host[256] = L"", path[2048] = L"", extra[2048] = L"", key[256] = L"";
    wchar_t headers[1024] = L"Content-Type: application/json\r\n";
    parts.dwHostNameLength = 255;
    parts.lpszHostName = host;
    parts.dwUrlPathLength = 2047;
    parts.lpszUrlPath = path;
    parts.dwExtraInfoLength = 2047;
    parts.lpszExtraInfo = extra;
    *response = NULL;
    *status = 0;
    if (!WinHttpCrackUrl(app->config.vision_base_url, 0, 0, &parts)) return FALSE;
    if (app->config.vision_key_env[0]) GetEnvironmentVariableW(app->config.vision_key_env, key, 256);
    if (!key[0]) wcscpy_s(key, 256, app->config.vision_key);
    if (key[0]) {
        wchar_t auth[384];
        _snwprintf_s(auth, 384, _TRUNCATE, L"Authorization: Bearer %s\r\n", key);
        wcscat_s(headers, 1024, auth);
    }
    HINTERNET session = WinHttpOpen(APP_NAME, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET connect = session ? WinHttpConnect(session, host, parts.nPort, 0) : NULL;
    wchar_t endpoint[4096];
    if (path[0] && path[wcslen(path) - 1] == L'/')
        _snwprintf_s(endpoint, 4096, _TRUNCATE, L"%schat/completions%s", path, extra);
    else
        _snwprintf_s(endpoint, 4096, _TRUNCATE, L"%s/chat/completions%s", path, extra);
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"POST", endpoint, NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : NULL;
    BOOL ok = FALSE;
    if (request) {
        int timeout = 30000;
        WinHttpSetTimeouts(request, timeout, timeout, timeout, timeout);
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
                    if (!grown) {
                        HeapFree(GetProcessHeap(), 0, buffer);
                        buffer = NULL;
                        read_ok = FALSE;
                        break;
                    }
                    buffer = grown;
                    capacity = next;
                }
            }
            if (buffer && !read_ok) {
                HeapFree(GetProcessHeap(), 0, buffer);
                buffer = NULL;
            }
            *response = buffer;
            ok = buffer != NULL;
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);
    return ok;
}

int vision_read_bitmap(App *app, HBITMAP bitmap, OcrTextLine *lines, int max_lines) {
    HRESULT com_status = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    BOOL com_owned = SUCCEEDED(com_status);
    BYTE *encoded = NULL; DWORD encoded_size = 0;
    if (!bitmap_jpeg(bitmap, &encoded, &encoded_size)) { if (com_owned) CoUninitialize(); return 0; }
    char model[256] = {0}, prompt[4096] = {0}; wide_to_utf8(app->config.vision_model, model, sizeof(model)); wide_to_utf8(app->config.vision_prompt[0] ? app->config.vision_prompt : L"只输出截图中原神聊天区域内的新聊天文本，每行一条，不要解释。", prompt, sizeof(prompt));
    cJSON *root = cJSON_CreateObject(), *messages = cJSON_CreateArray(), *message = cJSON_CreateObject(), *content = cJSON_CreateArray(), *text = cJSON_CreateObject(), *image = cJSON_CreateObject(), *image_url = cJSON_CreateObject();
    if (!root || !messages || !message || !content || !text || !image || !image_url) {
        if (root) cJSON_Delete(root);
        if (messages) cJSON_Delete(messages);
        if (message) cJSON_Delete(message);
        if (content) cJSON_Delete(content);
        if (text) cJSON_Delete(text);
        if (image) cJSON_Delete(image);
        if (image_url) cJSON_Delete(image_url);
        HeapFree(GetProcessHeap(), 0, encoded); if (com_owned) CoUninitialize(); return 0;
    }
    cJSON_AddStringToObject(root, "model", model); cJSON_AddNumberToObject(root, "max_tokens", 160); cJSON_AddItemToObject(root, "messages", messages); cJSON_AddStringToObject(message, "role", "user"); cJSON_AddItemToObject(message, "content", content); cJSON_AddStringToObject(text, "type", "text"); cJSON_AddStringToObject(text, "text", prompt); cJSON_AddItemToArray(content, text); cJSON_AddStringToObject(image, "type", "image_url");
    char *url = (char*)HeapAlloc(GetProcessHeap(), 0, strlen("data:image/jpeg;base64,") + encoded_size + 1); if (!url) { cJSON_Delete(root); HeapFree(GetProcessHeap(), 0, encoded); if (com_owned) CoUninitialize(); return 0; } strcpy_s(url, strlen("data:image/jpeg;base64,") + encoded_size + 1, "data:image/jpeg;base64,"); strcat_s(url, strlen("data:image/jpeg;base64,") + encoded_size + 1, (char*)encoded); cJSON_AddStringToObject(image_url, "url", url); cJSON_AddItemToObject(image, "image_url", image_url); cJSON_AddItemToArray(content, image); cJSON_AddItemToArray(messages, message);
    char *body = cJSON_PrintUnformatted(root); cJSON_Delete(root); HeapFree(GetProcessHeap(), 0, encoded); HeapFree(GetProcessHeap(), 0, url); if (!body) { if (com_owned) CoUninitialize(); return 0; }
    char *response = NULL; DWORD status = 0; BOOL ok = vision_request(app, body, &response, &status); cJSON_free(body); if (!ok || status < 200 || status >= 300 || !response) { if (response) HeapFree(GetProcessHeap(), 0, response); if (com_owned) CoUninitialize(); return 0; }
    cJSON *parsed = cJSON_Parse(response), *choices = parsed ? cJSON_GetObjectItem(parsed, "choices") : NULL, *choice = cJSON_IsArray(choices) ? cJSON_GetArrayItem(choices, 0) : NULL, *msg = choice ? cJSON_GetObjectItem(choice, "message") : NULL, *answer = msg ? cJSON_GetObjectItem(msg, "content") : NULL; int count = 0; char answer_text[8192] = {0}; if (!json_content_to_utf8(answer, answer_text, sizeof(answer_text)) && choice) { cJSON *legacy_text = cJSON_GetObjectItem(choice, "text"); json_content_to_utf8(legacy_text, answer_text, sizeof(answer_text)); } if (answer_text[0]) { wchar_t wide[4096] = L""; utf8_to_wide(answer_text, wide, 4096); wchar_t *cursor = wide; while (*cursor && count < max_lines) { wchar_t *end = wcschr(cursor, L'\n'); wchar_t *next = end ? end + 1 : NULL; if (end) *end = 0; while (*cursor == L' ' || *cursor == L'\r') ++cursor; clean_vision_line(cursor); if (*cursor && !ocr_line_is_system(cursor)) { wcscpy_s(lines[count].text, 256, cursor); lines[count].left = lines[count].top = lines[count].right = lines[count].bottom = 0; lines[count].confidence = 1.0f; wcscpy_s(lines[count].source, 24, L"vision_ai"); ++count; } if (!next) break; cursor = next; } }
    if (parsed) cJSON_Delete(parsed); HeapFree(GetProcessHeap(), 0, response); if (com_owned) CoUninitialize(); return count;
}
