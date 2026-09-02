#include "app.h"
#include "../third_party/onnxruntime/onnxruntime_c_api.h"
#include <math.h>

typedef const OrtApiBase* (ORT_API_CALL *GetApiBaseFn)(void);
typedef OrtStatus* (ORT_API_CALL *AppendDmlFn)(OrtSessionOptions *options, int device_id);
typedef struct ChatGIBotDmlApi {
    OrtStatus* (ORT_API_CALL *SessionOptionsAppendExecutionProvider_DML)(OrtSessionOptions *options, int device_id);
} ChatGIBotDmlApi;
typedef struct {
    HMODULE module;
    HMODULE dml_module;
    const OrtApi *api;
    OrtEnv *env;
    OrtSessionOptions *options;
    OrtMemoryInfo *memory;
    OrtSession *det;
    OrtSession *rec;
    char *det_input;
    char *det_output;
    char *rec_input;
    char *rec_output;
    wchar_t dict[18710][8];
    int dict_count;
    wchar_t loaded_tier[32];
    wchar_t loaded_device[32];
    BOOL ready;
    BOOL dml_active;
    int dml_device_index;
} OcrRuntime;

static OcrRuntime g_ocr;

static BOOL status_ok(const OrtApi *api, OrtStatus *status) {
    if (!status) return TRUE;
    if (api) api->ReleaseStatus(status);
    return FALSE;
}

static BOOL model_paths(App *app, wchar_t *det, wchar_t *rec) {
    wchar_t dir[MAX_PATH]; get_model_dir(dir, MAX_PATH);
    _snwprintf_s(det, MAX_PATH, _TRUNCATE, L"%s\\PP-OCRv6_%s_det.onnx", dir, app->config.model_tier);
    _snwprintf_s(rec, MAX_PATH, _TRUNCATE, L"%s\\PP-OCRv6_%s_rec.onnx", dir, app->config.model_tier);
    DWORD det_attributes = GetFileAttributesW(det), rec_attributes = GetFileAttributesW(rec);
    return det_attributes != INVALID_FILE_ATTRIBUTES && rec_attributes != INVALID_FILE_ATTRIBUTES &&
           !(det_attributes & FILE_ATTRIBUTE_DIRECTORY) && !(rec_attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static void runtime_reset(void) {
    const OrtApi *api = g_ocr.api;
    if (api && g_ocr.det) api->ReleaseSession(g_ocr.det);
    if (api && g_ocr.rec) api->ReleaseSession(g_ocr.rec);
    if (api && g_ocr.memory) api->ReleaseMemoryInfo(g_ocr.memory);
    if (api && g_ocr.options) api->ReleaseSessionOptions(g_ocr.options);
    if (api && g_ocr.env) api->ReleaseEnv(g_ocr.env);
    if (g_ocr.det_input) HeapFree(GetProcessHeap(), 0, g_ocr.det_input);
    if (g_ocr.det_output) HeapFree(GetProcessHeap(), 0, g_ocr.det_output);
    if (g_ocr.rec_input) HeapFree(GetProcessHeap(), 0, g_ocr.rec_input);
    if (g_ocr.rec_output) HeapFree(GetProcessHeap(), 0, g_ocr.rec_output);
    if (g_ocr.module) FreeLibrary(g_ocr.module);
    if (g_ocr.dml_module) FreeLibrary(g_ocr.dml_module);
    memset(&g_ocr, 0, sizeof(g_ocr));
}

static char *session_name(const OrtApi *api, OrtSession *session, BOOL output) {
    OrtAllocator *allocator = NULL; size_t count = 0; char *name = NULL;
    if (api->GetAllocatorWithDefaultOptions(&allocator) ||
        (output ? api->SessionGetOutputCount(session, &count) : api->SessionGetInputCount(session, &count))) return NULL;
    if (!count) return NULL;
    if (output ? api->SessionGetOutputName(session, 0, allocator, &name) : api->SessionGetInputName(session, 0, allocator, &name)) return NULL;
    char *copy = (char*)HeapAlloc(GetProcessHeap(), 0, strlen(name) + 1);
    if (copy) strcpy_s(copy, strlen(name) + 1, name);
    (void)api->AllocatorFree(allocator, name);
    return copy;
}

static BOOL load_dictionary(void) {
    if (g_ocr.dict_count) return TRUE;
    wchar_t dir[MAX_PATH], path[MAX_PATH]; char *data = NULL; DWORD size = 0;
    get_model_dir(dir, MAX_PATH);
    _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%s\\ppocr_keys_v6.txt", dir);
    if (!read_text_file(path, &data, &size)) {
        _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%s\\ppocr_keys_v1.txt", dir);
        if (!read_text_file(path, &data, &size)) return FALSE;
    }
    char *cursor = data;
    while (cursor && *cursor && g_ocr.dict_count < 18709) {
        char *end = strchr(cursor, '\n'); if (end) *end = 0;
        while (*cursor && (cursor[strlen(cursor) - 1] == '\r' || cursor[strlen(cursor) - 1] == ' ')) cursor[strlen(cursor) - 1] = 0;
        if (*cursor) utf8_to_wide(cursor, g_ocr.dict[g_ocr.dict_count++], 8);
        cursor = end ? end + 1 : NULL;
    }
    HeapFree(GetProcessHeap(), 0, data);
    return g_ocr.dict_count > 0;
}

static BOOL runtime_init(App *app, wchar_t *detail, int detail_chars) {
    if (g_ocr.ready && wcscmp(g_ocr.loaded_tier, app->config.model_tier) == 0 &&
        wcscmp(g_ocr.loaded_device, app->config.inference_device) == 0) return TRUE;
    if (g_ocr.ready) runtime_reset();
    wchar_t det[MAX_PATH], rec[MAX_PATH], dll_path[MAX_PATH];
    BOOL use_dml = wcscmp(app->config.inference_device, L"directml") == 0;
    get_runtime_library_path(&app->config, dll_path, MAX_PATH);
    if (use_dml) {
        if (GetFileAttributesW(dll_path) == INVALID_FILE_ATTRIBUTES)
            app_log_format(L"WARNING", L"DirectML 运行库未安装，OCR 将回退 CPU");
        wchar_t directml_path[MAX_PATH];
        wcscpy_s(directml_path, MAX_PATH, dll_path);
        wchar_t *slash = wcsrchr(directml_path, L'\\');
        if (slash) wcscpy_s(slash + 1, MAX_PATH - (size_t)(slash + 1 - directml_path), L"DirectML.dll");
        g_ocr.dml_module = slash ? LoadLibraryW(directml_path) : NULL;
        if (!g_ocr.dml_module) {
            app_log_format(L"WARNING", L"配套 DirectML.dll 缺失或无法加载，OCR 将回退 CPU");
            wcscpy_s(app->config.inference_device, 32, L"cpu");
            get_runtime_library_path(NULL, dll_path, MAX_PATH);
            use_dml = FALSE;
        }
    }
    if (!model_paths(app, det, rec)) {
        _snwprintf_s(detail, detail_chars, _TRUNCATE, L"PP-OCRv6 模型不完整");
        runtime_reset();
        return FALSE;
    }
    g_ocr.module = LoadLibraryW(dll_path);
    if (!g_ocr.module && use_dml) {
        get_runtime_library_path(NULL, dll_path, MAX_PATH);
        g_ocr.module = LoadLibraryW(dll_path);
        use_dml = FALSE;
        app_log_format(L"WARNING", L"DirectML 运行库加载失败，OCR 已回退 CPU");
    }
    GetApiBaseFn get_base = g_ocr.module ? (GetApiBaseFn)GetProcAddress(g_ocr.module, "OrtGetApiBase") : NULL;
    const OrtApiBase *base = get_base ? get_base() : NULL;
    g_ocr.api = base ? base->GetApi(ORT_API_VERSION) : NULL;
    int threads = app ? app->config.ocr_threads : 2;
    threads = max(2, min(8, min(threads, system_logical_processor_count())));
    if (!g_ocr.api || !status_ok(g_ocr.api, g_ocr.api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "ChatGIBot", &g_ocr.env)) ||
        !status_ok(g_ocr.api, g_ocr.api->CreateSessionOptions(&g_ocr.options)) ||
        !status_ok(g_ocr.api, g_ocr.api->SetIntraOpNumThreads(g_ocr.options, threads))) {
        _snwprintf_s(detail, detail_chars, _TRUNCATE, L"ONNX Runtime 或 PP-OCRv6 模型加载失败");
        runtime_reset();
        return FALSE;
    }
    if (use_dml) {
        const ChatGIBotDmlApi *dml_api = NULL;
        OrtStatus *api_status = g_ocr.api->GetExecutionProviderApi("DML", ORT_API_VERSION,
                                                                   (const void **)&dml_api);
        BOOL provider_api_ok = status_ok(g_ocr.api, api_status) && dml_api;
        AppendDmlFn exported_append = (AppendDmlFn)GetProcAddress(g_ocr.module,
                                                                  "OrtSessionOptionsAppendExecutionProvider_DML");
        BOOL dml_ok = FALSE;
        wchar_t dml_error[512] = L"DirectML API 不可用";
        for (int api_mode = 0; api_mode < 2 && !dml_ok; ++api_mode) {
            if ((api_mode == 0 && !provider_api_ok) || (api_mode == 1 && !exported_append)) continue;
            for (int device_id = 0; device_id < 8; ++device_id) {
                OrtStatus *dml_status = api_mode == 0
                    ? dml_api->SessionOptionsAppendExecutionProvider_DML(g_ocr.options, device_id)
                    : exported_append(g_ocr.options, device_id);
                if (!dml_status) {
                    g_ocr.dml_device_index = device_id;
                    dml_ok = TRUE;
                    break;
                }
                const char *message = g_ocr.api->GetErrorMessage(dml_status);
                if (message) utf8_to_wide(message, dml_error, 512);
                g_ocr.api->ReleaseStatus(dml_status);
            }
        }
        if (dml_ok) {
            g_ocr.dml_active = TRUE;
        } else {
            app_log_format(L"WARNING", L"DirectML 执行提供器不可用，OCR 回退 CPU：%s", dml_error);
            wcscpy_s(app->config.inference_device, 32, L"cpu");
        }
    }
    if (!status_ok(g_ocr.api, g_ocr.api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &g_ocr.memory)) ||
        !status_ok(g_ocr.api, g_ocr.api->CreateSession(g_ocr.env, det, g_ocr.options, &g_ocr.det)) ||
        !status_ok(g_ocr.api, g_ocr.api->CreateSession(g_ocr.env, rec, g_ocr.options, &g_ocr.rec))) {
        _snwprintf_s(detail, detail_chars, _TRUNCATE, L"ONNX Runtime 或 PP-OCRv6 模型加载失败");
        runtime_reset();
        return FALSE;
    }
    g_ocr.det_input = session_name(g_ocr.api, g_ocr.det, FALSE); g_ocr.det_output = session_name(g_ocr.api, g_ocr.det, TRUE);
    g_ocr.rec_input = session_name(g_ocr.api, g_ocr.rec, FALSE); g_ocr.rec_output = session_name(g_ocr.api, g_ocr.rec, TRUE);
    g_ocr.ready = g_ocr.det_input && g_ocr.det_output && g_ocr.rec_input && g_ocr.rec_output;
    if (g_ocr.ready) {
        if (!load_dictionary()) {
            _snwprintf_s(detail, detail_chars, _TRUNCATE, L"PP-OCRv6 字典加载失败");
            runtime_reset();
            return FALSE;
        }
        wcscpy_s(g_ocr.loaded_tier, 32, app->config.model_tier);
        wcscpy_s(g_ocr.loaded_device, 32, app->config.inference_device);
        if (g_ocr.dml_active)
            _snwprintf_s(detail, detail_chars, _TRUNCATE, L"PP-OCRv6 %s 已加载（DirectML 设备 %d），字典 %d 项",
                         app->config.model_tier, g_ocr.dml_device_index, g_ocr.dict_count);
        else
            _snwprintf_s(detail, detail_chars, _TRUNCATE, L"PP-OCRv6 %s 已加载（CPU），字典 %d 项",
                         app->config.model_tier, g_ocr.dict_count);
    }
    if (!g_ocr.ready) runtime_reset();
    return g_ocr.ready;
}

static BOOL runtime_init_with_fallback(App *app, wchar_t *detail, int detail_chars) {
    if (runtime_init(app, detail, detail_chars)) return TRUE;
    if (wcscmp(app->config.inference_device, L"directml") != 0) return FALSE;
    app_log_format(L"WARNING", L"DirectML 初始化失败，正在切换 CPU");
    wcscpy_s(app->config.inference_device, 32, L"cpu");
    runtime_reset();
    return runtime_init(app, detail, detail_chars);
}

BOOL ocr_prepare(App *app, wchar_t *detail, int detail_chars) {
    if (!detail || detail_chars <= 0) return FALSE;
    detail[0] = 0;
    return runtime_init_with_fallback(app, detail, detail_chars);
}

static BOOL bitmap_pixels(HBITMAP bitmap, BYTE **pixels, int *width, int *height) {
    BITMAP info; if (!GetObjectW(bitmap, sizeof(info), &info)) return FALSE;
    *width = info.bmWidth; *height = abs(info.bmHeight);
    BITMAPINFO header = {0}; header.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); header.bmiHeader.biWidth = *width;
    header.bmiHeader.biHeight = -*height; header.bmiHeader.biPlanes = 1; header.bmiHeader.biBitCount = 32; header.bmiHeader.biCompression = BI_RGB;
    *pixels = (BYTE*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)*width * *height * 4);
    HDC dc = GetDC(NULL); BOOL ok = *pixels && GetDIBits(dc, bitmap, 0, *height, *pixels, &header, DIB_RGB_COLORS) != 0; ReleaseDC(NULL, dc);
    if (!ok && *pixels) { HeapFree(GetProcessHeap(), 0, *pixels); *pixels = NULL; } return ok;
}

static void fill_tensor(const BYTE *pixels, int src_w, int src_h, float *tensor, int dst_w, int dst_h) {
    const float scale = 1.0f / 255.0f;
    for (int y = 0; y < dst_h; ++y) for (int x = 0; x < dst_w; ++x) {
        int sx = min(src_w - 1, (x * src_w) / dst_w), sy = min(src_h - 1, (y * src_h) / dst_h);
        const BYTE *p = pixels + ((SIZE_T)sy * src_w + sx) * 4;
        tensor[y * dst_w + x] = p[2] * scale * 2.0f - 1.0f;
        tensor[(SIZE_T)dst_w * dst_h + y * dst_w + x] = p[1] * scale * 2.0f - 1.0f;
        tensor[(SIZE_T)dst_w * dst_h * 2 + y * dst_w + x] = p[0] * scale * 2.0f - 1.0f;
    }
}

static void fill_crop_tensor(const BYTE *pixels, int src_w, int src_h, const OcrTextLine *line, float *tensor, int dst_w) {
    int left = max(0, min(src_w - 1, line->left)), top = max(0, min(src_h - 1, line->top));
    int right = max(left + 1, min(src_w, line->right)), bottom = max(top + 1, min(src_h, line->bottom));
    int crop_w = right - left, crop_h = bottom - top;
    const float scale = 1.0f / 255.0f;
    for (int y = 0; y < 48; ++y) for (int x = 0; x < dst_w; ++x) {
        int sx = left + min(crop_w - 1, (x * crop_w) / dst_w), sy = top + min(crop_h - 1, (y * crop_h) / 48);
        const BYTE *p = pixels + ((SIZE_T)sy * src_w + sx) * 4;
        tensor[y * dst_w + x] = (p[2] * scale - 0.5f) / 0.5f;
        tensor[(SIZE_T)dst_w * 48 + y * dst_w + x] = (p[1] * scale - 0.5f) / 0.5f;
        tensor[(SIZE_T)dst_w * 96 + y * dst_w + x] = (p[0] * scale - 0.5f) / 0.5f;
    }
}

static BOOL recognize_line(const BYTE *pixels, int width, int height, OcrTextLine *line) {
    int crop_w = max(1, line->right - line->left), crop_h = max(1, line->bottom - line->top);
    int rec_w = max(32, min(640, ((crop_w * 48 + crop_h - 1) / crop_h + 3) / 4 * 4));
    SIZE_T elements = (SIZE_T)rec_w * 48 * 3; float *data = (float*)HeapAlloc(GetProcessHeap(), 0, elements * sizeof(float));
    if (!data) return FALSE; fill_crop_tensor(pixels, width, height, line, data, rec_w);
    int64_t dims[4] = {1, 3, 48, rec_w}; OrtValue *input = NULL, *output = NULL; const OrtApi *api = g_ocr.api;
    const char *input_name = g_ocr.rec_input, *output_name = g_ocr.rec_output;
    BOOL ok = status_ok(api, api->CreateTensorWithDataAsOrtValue(g_ocr.memory, data, elements * sizeof(float), dims, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input));
    if (ok) ok = status_ok(api, api->Run(g_ocr.rec, NULL, &input_name, (const OrtValue *const*)&input, 1, &output_name, 1, &output));
    void *raw = NULL; OrtTensorTypeAndShapeInfo *info = NULL; size_t rank = 0; int64_t shape[4] = {0};
    if (ok) ok = status_ok(api, api->GetTensorTypeAndShape(output, &info));
    if (ok) ok = status_ok(api, api->GetDimensionsCount(info, &rank));
    if (ok && rank == 3) ok = status_ok(api, api->GetDimensions(info, shape, rank));
    if (ok) ok = status_ok(api, api->GetTensorMutableData(output, &raw));
    if (ok && rank == 3 && raw) {
        int steps = (int)shape[1], classes = (int)shape[2], out = 0, recognized = 0, previous = -1; float confidence = 0;
        float *scores = (float*)raw;
        for (int step = 0; step < steps; ++step) {
            int best = 0; float best_score = scores[(SIZE_T)step * classes];
            for (int cls = 1; cls < classes; ++cls) if (scores[(SIZE_T)step * classes + cls] > best_score) { best = cls; best_score = scores[(SIZE_T)step * classes + cls]; }
            if (best && best != previous && out < 255) {
                if (best - 1 < g_ocr.dict_count && g_ocr.dict[best - 1][0]) {
                    wchar_t *token = g_ocr.dict[best - 1]; size_t token_len = wcslen(token);
                    if (out + (int)token_len < 255) {
                        wcscpy_s(line->text + out, 256 - out, token);
                        out += (int)token_len;
                        float probability = (best_score >= 0.0f && best_score <= 1.0f) ? best_score : 1.0f / (1.0f + expf(-best_score));
                        confidence += max(0.0f, min(1.0f, probability));
                        ++recognized;
                    }
                }
            }
            previous = best;
        }
        line->confidence = recognized ? confidence / recognized : line->confidence;
        line->text[out] = 0;
    }
    if (info) api->ReleaseTensorTypeAndShapeInfo(info); if (output) api->ReleaseValue(output); if (input) api->ReleaseValue(input); HeapFree(GetProcessHeap(), 0, data);
    return ok;
}

static BOOL run_detection(App *app, const BYTE *pixels, int width, int height, OcrTextLine *lines, int max_lines, int *line_count) {
    int target_w = min(960, max(32, width)), target_h = max(32, (int)((double)height * target_w / width));
    if (target_h > 960) { target_h = 960; target_w = max(32, (int)((double)width * target_h / height)); }
    target_w = (target_w + 31) / 32 * 32; target_h = (target_h + 31) / 32 * 32;
    SIZE_T elements = (SIZE_T)target_w * target_h * 3; float *input_data = (float*)HeapAlloc(GetProcessHeap(), 0, elements * sizeof(float));
    if (!input_data) return FALSE; fill_tensor(pixels, width, height, input_data, target_w, target_h);
    int64_t shape[4] = {1, 3, target_h, target_w}; OrtValue *input = NULL, *output = NULL;
    const OrtApi *api = g_ocr.api; const char *input_name = g_ocr.det_input, *output_name = g_ocr.det_output;
    BOOL ok = status_ok(api, api->CreateTensorWithDataAsOrtValue(g_ocr.memory, input_data, elements * sizeof(float), shape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input));
    if (ok) ok = status_ok(api, api->Run(g_ocr.det, NULL, &input_name, (const OrtValue *const*)&input, 1, &output_name, 1, &output));
    if (!ok) { HeapFree(GetProcessHeap(), 0, input_data); if (input) api->ReleaseValue(input); return FALSE; }
    OrtTensorTypeAndShapeInfo *shape_info = NULL; size_t rank = 0; int64_t dims[4] = {0}; void *raw = NULL;
    ok = status_ok(api, api->GetTensorTypeAndShape(output, &shape_info));
    if (ok) ok = status_ok(api, api->GetDimensionsCount(shape_info, &rank));
    if (ok && rank == 4) ok = status_ok(api, api->GetDimensions(shape_info, dims, rank));
    if (ok) ok = status_ok(api, api->GetTensorMutableData(output, &raw));
    if (!ok || rank != 4 || !raw) { if (shape_info) api->ReleaseTensorTypeAndShapeInfo(shape_info); api->ReleaseValue(output); api->ReleaseValue(input); HeapFree(GetProcessHeap(), 0, input_data); return FALSE; }
    int out_h = (int)dims[2], out_w = (int)dims[3]; float *map = (float*)raw; BYTE *visited = (BYTE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)out_w * out_h); int *queue = (int*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)out_w * out_h * sizeof(int));
    if (!visited || !queue) { if (queue) HeapFree(GetProcessHeap(), 0, queue); if (visited) HeapFree(GetProcessHeap(), 0, visited); api->ReleaseTensorTypeAndShapeInfo(shape_info); api->ReleaseValue(output); api->ReleaseValue(input); HeapFree(GetProcessHeap(), 0, input_data); return FALSE; }
    float threshold = max(0.1f, min(0.95f, app->config.score_threshold_percent / 100.0f));
    for (int y = 0; y < out_h && *line_count < max_lines; ++y) for (int x = 0; x < out_w && *line_count < max_lines; ++x) {
        int at = y * out_w + x; if (visited[at] || map[at] < threshold) continue;
        int head = 0, tail = 0, min_x = x, max_x = x, min_y = y, max_y = y, count = 0; queue[tail++] = at; visited[at] = 1; float confidence = 0;
        while (head < tail) { int current = queue[head++], cx = current % out_w, cy = current / out_w; ++count; confidence += map[current]; min_x = min(min_x, cx); max_x = max(max_x, cx); min_y = min(min_y, cy); max_y = max(max_y, cy);
            int nx[4] = { cx - 1, cx + 1, cx, cx }, ny[4] = { cy, cy, cy - 1, cy + 1 };
            for (int n = 0; n < 4; ++n) { if (nx[n] < 0 || nx[n] >= out_w || ny[n] < 0 || ny[n] >= out_h) continue; int next = ny[n] * out_w + nx[n]; if (!visited[next] && map[next] >= threshold) { visited[next] = 1; queue[tail++] = next; } }
        }
        if (count < 8 || max_x - min_x < 3 || max_y - min_y < 3) continue;
        OcrTextLine *line = &lines[(*line_count)++];
        int raw_left = min_x * width / out_w, raw_right = (max_x + 1) * width / out_w;
        int raw_top = min_y * height / out_h, raw_bottom = (max_y + 1) * height / out_h;
        int box_height = max(1, raw_bottom - raw_top);
        int horizontal_padding = max(3, box_height / 3);
        int vertical_padding = max(4, box_height * 3 / 4);
        line->left = max(0, raw_left - horizontal_padding);
        line->right = min(width, raw_right + horizontal_padding);
        line->top = max(0, raw_top - vertical_padding);
        line->bottom = min(height, raw_bottom + vertical_padding);
        line->confidence = confidence / count; wcscpy_s(line->source, 24, L"paddle"); line->text[0] = 0;
    }
    BOOL recognition_ok = TRUE;
    for (int i = 0; i < *line_count; ++i)
        if (!recognize_line(pixels, width, height, &lines[i])) recognition_ok = FALSE;
    HeapFree(GetProcessHeap(), 0, queue); HeapFree(GetProcessHeap(), 0, visited); api->ReleaseTensorTypeAndShapeInfo(shape_info); api->ReleaseValue(output); api->ReleaseValue(input); HeapFree(GetProcessHeap(), 0, input_data);
    (void)app; return recognition_ok;
}

static BOOL ocr_line_has_system_background(const BYTE *pixels, int width, int height, const OcrTextLine *line) {
    int left = max(0, line->left - 8), right = min(width, line->right + 8);
    int top = max(0, line->top - 5), bottom = min(height, line->bottom + 5);
    int green = 0, samples = 0;
    for (int y = top; y < bottom; y += 2) for (int x = left; x < right; x += 2) {
        const BYTE *pixel = pixels + ((SIZE_T)y * width + x) * 4;
        if (pixel[1] > pixel[2] + 15 && pixel[1] > pixel[0] + 20 && pixel[1] >= 70 && pixel[1] <= 180) ++green;
        ++samples;
    }
    return samples > 0 && green * 100 >= samples * 35;
}

static BOOL ocr_line_is_ui_noise(const BYTE *pixels, const OcrTextLine *line, const OcrTextLine *lines, int count, int width, int height) {
    size_t length = wcslen(line->text);
    int line_height = max(1, line->bottom - line->top);
    if (ocr_line_has_system_background(pixels, width, height, line)) return TRUE;
    if (length <= 1 && (line->confidence < 0.65f || line->top < height / 8 || line->right > width * 85 / 100)) return TRUE;
    if (line->top < max(12, height / 12) && line->left > width / 2 && length <= 24) return TRUE;
    for (int i = 0; i < count; ++i) {
        const OcrTextLine *next = &lines[i];
        if (next == line || next->top <= line->top || next->top - line->bottom > max(80, line_height * 2)) continue;
        int current_width = max(1, line->right - line->left);
        int next_width = max(1, next->right - next->left);
        if (length <= 24 && next_width >= current_width * 3 / 2 &&
            next->top - line->top <= max(100, line_height * 3)) return TRUE;
    }
    return FALSE;
}

int ocr_read_bitmap(App *app, HBITMAP bitmap, OcrTextLine *lines, int max_lines) {
    if (wcscmp(app->config.vision_engine, L"local") != 0) return vision_read_bitmap(app, bitmap, lines, max_lines);
    if (max_lines <= 0) return 0;
    wchar_t detail[512]; if (!runtime_init_with_fallback(app, detail, 512)) return 0;
    BYTE *pixels = NULL; int width = 0, height = 0; if (!bitmap_pixels(bitmap, &pixels, &width, &height)) return 0;
    int count = 0; BOOL directml_requested = wcscmp(app->config.inference_device, L"directml") == 0;
    BOOL ok = run_detection(app, pixels, width, height, lines, max_lines, &count);
    if (!ok && directml_requested) {
        app_log_format(L"WARNING", L"DirectML 推理失败，正在切换 CPU 重新识别");
        wcscpy_s(app->config.inference_device, 32, L"cpu");
        runtime_reset();
        wchar_t fallback_detail[512];
        if (runtime_init(app, fallback_detail, 512)) {
            count = 0;
            ok = run_detection(app, pixels, width, height, lines, max_lines, &count);
        }
    }
    if (!ok) { HeapFree(GetProcessHeap(), 0, pixels); return 0; }
    for (int i = 0; i < count; ++i) for (int j = i + 1; j < count; ++j) {
        if (lines[j].top < lines[i].top || (lines[j].top == lines[i].top && lines[j].left < lines[i].left)) {
            OcrTextLine swap = lines[i]; lines[i] = lines[j]; lines[j] = swap;
        }
    }
    int filtered = 0;
    for (int i = 0; i < count; ++i) {
        if (lines[i].top <= 1) continue;
        ocr_clean_text(lines[i].text, 256);
        if (lines[i].text[0] && !ocr_line_is_system(lines[i].text) &&
            !ocr_line_is_ui_noise(pixels, &lines[i], lines, count, width, height)) lines[filtered++] = lines[i];
    }
    count = filtered;
    int merged = 0;
    for (int i = 0; i < count; ++i) {
        if (!lines[i].text[0]) continue;
        BOOL same_line = FALSE;
        if (merged) {
            int previous_height = lines[merged - 1].bottom - lines[merged - 1].top;
            int current_height = lines[i].bottom - lines[i].top;
            int center_delta = abs((lines[i].top + lines[i].bottom) -
                                   (lines[merged - 1].top + lines[merged - 1].bottom));
            int line_tolerance = max(4, min(previous_height, current_height));
            int horizontal_gap = lines[i].left - lines[merged - 1].right;
            int max_horizontal_gap = max(24, line_tolerance * 8);
            same_line = center_delta <= line_tolerance * 2 && horizontal_gap <= max_horizontal_gap;
        }
        if (same_line) {
            size_t existing = wcslen(lines[merged - 1].text);
            if (existing && existing < 255) lines[merged - 1].text[existing++] = L' ';
            if (existing < 255) wcscpy_s(lines[merged - 1].text + existing, 256 - existing, lines[i].text);
            lines[merged - 1].right = max(lines[merged - 1].right, lines[i].right);
            lines[merged - 1].bottom = max(lines[merged - 1].bottom, lines[i].bottom);
        } else lines[merged++] = lines[i];
    }
    count = merged;
    if (count > max_lines) count = max_lines;
    HeapFree(GetProcessHeap(), 0, pixels);
    return count;
}

static DWORD WINAPI ocr_test_thread(LPVOID parameter) {
    App *app = (App*)parameter; RECT rect; InterlockedExchange(&app->busy, 1); ui_set_busy(1);
    app_log_format(L"INFO", L"开始 PP-OCRv6 %s 识别测试", app->config.model_tier);
    int mode = wcscmp(app->config.screenshot_mode, L"printwindow") == 0 ? 1 :
               wcscmp(app->config.screenshot_mode, L"duplication") == 0 ? 2 : 0; HBITMAP bitmap = capture_game_window_for_test(app->config.window_title, app->config.region, mode,
                                                                                                                 app->hwnd, &rect);
    if (!bitmap) app_log_format(L"ERROR", L"OCR 测试失败：无法捕获游戏窗口");
    else {
        BITMAP bitmap_info;
        if (GetObjectW(bitmap, sizeof(bitmap_info), &bitmap_info))
            app_log_format(L"INFO", L"截图捕获完成：%ld×%ld，开始加载 OCR 运行时", bitmap_info.bmWidth, abs(bitmap_info.bmHeight));
        else
            app_log_format(L"INFO", L"截图捕获完成，开始加载 OCR 运行时");
        wchar_t runtime_detail[512] = L"";
        if (!ocr_prepare(app, runtime_detail, 512)) {
            app_log_format(L"ERROR", L"OCR 运行时不可用：%s", runtime_detail[0] ? runtime_detail : L"未知错误");
            DeleteObject(bitmap);
            InterlockedExchange(&app->busy, 0); ui_set_busy(0);
            return 0;
        }
        app_log_format(L"INFO", L"OCR 运行时就绪：%s，开始推理", runtime_detail);
        OcrTextLine lines[32];
        int count = ocr_read_bitmap(app, bitmap, lines, 32);
        DeleteObject(bitmap);
        wchar_t preview[8192] = L"";
        if (count == 0) {
            wcscpy_s(preview, 8192, L"未识别到有效聊天文本。\r\n请检查聊天区域、模型文件和置信度阈值。");
        } else {
            size_t used = 0;
            for (int i = 0; i < count && used < 8000; ++i) {
                int written = _snwprintf_s(preview + used, 8192 - used, _TRUNCATE,
                    L"[%02d] %s\r\n     坐标：(%d, %d) - (%d, %d)  置信度：%.1f%%  引擎：%s\r\n",
                    i + 1, lines[i].text, lines[i].left, lines[i].top, lines[i].right,
                    lines[i].bottom, lines[i].confidence * 100.0f, lines[i].source);
                if (written < 0) break;
                used += (size_t)written;
            }
        }
        EnterCriticalSection(&app->log_lock);
        wcscpy_s(app->ocr_preview, 8192, preview);
        LeaveCriticalSection(&app->log_lock);
        PostMessageW(app->hwnd, WM_APP_OCR, 0, 0);
        app_log_format(count ? L"INFO" : L"WARNING", L"OCR 测试完成：检测到 %d 个文本区域", count);
    }
    InterlockedExchange(&app->busy, 0); ui_set_busy(0); return 0;
}

BOOL ocr_test_async(App *app) {
    if (InterlockedCompareExchange(&app->running, 1, 1)) return FALSE;
    background_reap(app);
    if (app->task_thread) return FALSE;
    if (InterlockedCompareExchange(&app->busy, 1, 0) != 0) return FALSE;
    HANDLE thread = CreateThread(NULL, 0, ocr_test_thread, app, 0, NULL);
    if (!thread) { InterlockedExchange(&app->busy, 0); return FALSE; }
    app->task_thread = thread;
    ui_set_busy(1);
    return TRUE;
}

void open_log_file(void) { wchar_t data[MAX_PATH], log[MAX_PATH], command[MAX_PATH * 2]; get_data_dir(data, MAX_PATH); _snwprintf_s(log, MAX_PATH, _TRUNCATE, L"%s\\chatgibot-native.log", data); _snwprintf_s(command, MAX_PATH * 2, _TRUNCATE, L"notepad.exe \"%s\"", log); STARTUPINFOW startup = { sizeof(startup) }; PROCESS_INFORMATION process; if (CreateProcessW(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) { CloseHandle(process.hThread); CloseHandle(process.hProcess); } }
