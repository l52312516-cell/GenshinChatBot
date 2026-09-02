#include "app.h"
#include <stdarg.h>
#include <dxgi.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <winhttp.h>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "winhttp.lib")

typedef LONG (WINAPI *RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);

static const wchar_t *DIRECTML_PACKAGE_HASH = L"9f07482559087088a4dba4ae76eeee1fad3f7077a92ccfbdb439c6bc2964c09";
static const wchar_t *DIRECTML_DLL_HASH = L"18c82a9c06141f7e96241adad1a35bebf4780a72d8eeeb107ec12f748391b3f5";

enum { DOWNLOAD_SOURCE_MODELSCOPE = 1, DOWNLOAD_SOURCE_HF_MIRROR, DOWNLOAD_SOURCE_OFFICIAL };

static int configured_source(const App *app) {
    if (wcscmp(app->config.download_source, L"modelscope") == 0) return DOWNLOAD_SOURCE_MODELSCOPE;
    if (wcscmp(app->config.download_source, L"hf-mirror") == 0) return DOWNLOAD_SOURCE_HF_MIRROR;
    if (wcscmp(app->config.download_source, L"official") == 0) return DOWNLOAD_SOURCE_OFFICIAL;
    return 0;
}

static void model_urls(int source, const wchar_t *tier, wchar_t *det, wchar_t *rec, wchar_t *dict) {
    if (source == DOWNLOAD_SOURCE_MODELSCOPE) {
        _snwprintf_s(det, 2048, _TRUNCATE, L"https://modelscope.cn/models/PaddlePaddle/PP-OCRv6_%s_det_onnx/resolve/master/inference.onnx", tier);
        _snwprintf_s(rec, 2048, _TRUNCATE, L"https://modelscope.cn/models/PaddlePaddle/PP-OCRv6_%s_rec_onnx/resolve/master/inference.onnx", tier);
        wcscpy_s(dict, 2048, L"https://ghfast.top/https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/main/ppocr/utils/dict/ppocrv6_dict.txt");
    } else if (source == DOWNLOAD_SOURCE_HF_MIRROR) {
        _snwprintf_s(det, 2048, _TRUNCATE, L"https://hf-mirror.com/PaddlePaddle/PP-OCRv6_%s_det_onnx/resolve/main/inference.onnx", tier);
        _snwprintf_s(rec, 2048, _TRUNCATE, L"https://hf-mirror.com/PaddlePaddle/PP-OCRv6_%s_rec_onnx/resolve/main/inference.onnx", tier);
        wcscpy_s(dict, 2048, L"https://ghfast.top/https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/main/ppocr/utils/dict/ppocrv6_dict.txt");
    } else {
        _snwprintf_s(det, 2048, _TRUNCATE, L"https://huggingface.co/PaddlePaddle/PP-OCRv6_%s_det_onnx/resolve/main/inference.onnx", tier);
        _snwprintf_s(rec, 2048, _TRUNCATE, L"https://huggingface.co/PaddlePaddle/PP-OCRv6_%s_rec_onnx/resolve/main/inference.onnx", tier);
        wcscpy_s(dict, 2048, L"https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/main/ppocr/utils/dict/ppocrv6_dict.txt");
    }
}

static int source_order(const App *app, int order[3]) {
    int selected = configured_source(app);
    if (selected == DOWNLOAD_SOURCE_MODELSCOPE) {
        order[0] = DOWNLOAD_SOURCE_MODELSCOPE; order[1] = DOWNLOAD_SOURCE_HF_MIRROR; order[2] = DOWNLOAD_SOURCE_OFFICIAL;
    } else if (selected == DOWNLOAD_SOURCE_HF_MIRROR) {
        order[0] = DOWNLOAD_SOURCE_HF_MIRROR; order[1] = DOWNLOAD_SOURCE_MODELSCOPE; order[2] = DOWNLOAD_SOURCE_OFFICIAL;
    } else if (selected == DOWNLOAD_SOURCE_OFFICIAL) {
        order[0] = DOWNLOAD_SOURCE_OFFICIAL; order[1] = DOWNLOAD_SOURCE_MODELSCOPE; order[2] = DOWNLOAD_SOURCE_HF_MIRROR;
    } else {
        order[0] = DOWNLOAD_SOURCE_MODELSCOPE; order[1] = DOWNLOAD_SOURCE_HF_MIRROR; order[2] = DOWNLOAD_SOURCE_OFFICIAL;
    }
    return 3;
}

static void runtime_source_url(const App *app, BOOL directml, wchar_t *url, int chars) {
    if (directml) {
        _snwprintf_s(url, (size_t)chars, _TRUNCATE,
                     L"https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime.directml/1.20.1/microsoft.ml.onnxruntime.directml.1.20.1.nupkg");
        return;
    }
    if (configured_source(app) != DOWNLOAD_SOURCE_OFFICIAL) {
        wcscpy_s(url, (size_t)chars, L"https://ghfast.top/https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/onnxruntime-win-x64-1.20.1.zip");
    } else {
        wcscpy_s(url, (size_t)chars, L"https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/onnxruntime-win-x64-1.20.1.zip");
    }
}

static const wchar_t *source_title(int source) {
    if (source == DOWNLOAD_SOURCE_MODELSCOPE) return L"ModelScope（国内）";
    if (source == DOWNLOAD_SOURCE_HF_MIRROR) return L"HF-Mirror（国内）";
    return L"官方源";
}

static BOOL probe_download_url(const wchar_t *url, DWORD *elapsed, DWORD *status_code) {
    URL_COMPONENTSW components = { sizeof(components) };
    wchar_t host[256] = L"", path[2048] = L"", extra[2048] = L"", request_path[4096] = L"";
    components.dwHostNameLength = 255; components.lpszHostName = host;
    components.dwUrlPathLength = 2047; components.lpszUrlPath = path;
    components.dwExtraInfoLength = 2047; components.lpszExtraInfo = extra;
    *elapsed = 0; *status_code = 0;
    if (!WinHttpCrackUrl(url, 0, 0, &components)) return FALSE;
    _snwprintf_s(request_path, 4096, _TRUNCATE, L"%s%s", path, extra);
    HINTERNET session = WinHttpOpen(APP_NAME, WINHTTP_ACCESS_TYPE_NO_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET connect = session ? WinHttpConnect(session, host, components.nPort, 0) : NULL;
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", request_path, NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : NULL;
    BOOL ok = FALSE;
    ULONGLONG started = GetTickCount64();
    if (request) {
        int timeout = 8000;
        WinHttpSetTimeouts(request, timeout, timeout, timeout, timeout);
        const wchar_t *range = L"Range: bytes=0-0\r\n";
        ok = WinHttpSendRequest(request, range, (DWORD)-1L,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
             WinHttpReceiveResponse(request, NULL);
        if (ok) {
            DWORD size = sizeof(*status_code);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, status_code, &size, WINHTTP_NO_HEADER_INDEX);
        }
    }
    *elapsed = (DWORD)min(60000ULL, GetTickCount64() - started);
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);
    return ok && *status_code >= 200 && *status_code < 400;
}

static void get_directml_library_path(wchar_t *path, DWORD size) {
    wchar_t runtime[MAX_PATH];
    get_runtime_dir(runtime, MAX_PATH);
    _snwprintf_s(path, (size_t)size, _TRUNCATE, L"%s\\DirectML.dll", runtime);
}

static void set_dependency(App *app, int index, DepState state, const wchar_t *format, ...) {
    va_list args;
    EnterCriticalSection(&app->log_lock);
    app->deps[index].state = state;
    va_start(args, format);
    _vsnwprintf_s(app->deps[index].detail, 512, _TRUNCATE, format, args);
    va_end(args);
    local_time_text(app->deps[index].checked_at, 32);
    LeaveCriticalSection(&app->log_lock);
    ui_refresh_dependency_controls();
}

static void windows_detection(App *app) {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    RTL_OSVERSIONINFOW info = { sizeof(info) };
    RtlGetVersionPtr get_version = (RtlGetVersionPtr)GetProcAddress(ntdll, "RtlGetVersion");
    if (!get_version || get_version(&info) != 0) {
        set_dependency(app, 6, DEP_WARN, L"无法读取 Windows 版本");
        return;
    }
    if (info.dwBuildNumber >= 22000)
        set_dependency(app, 6, DEP_OK, L"Windows %lu build %lu；支持深色 UI 和 DirectML", info.dwMajorVersion, info.dwBuildNumber);
    else if (info.dwBuildNumber >= 19041)
        set_dependency(app, 6, DEP_OK, L"Windows 10 build %lu；兼容 BitBlt", info.dwBuildNumber);
    else
        set_dependency(app, 6, DEP_WARN, L"Windows 版本过旧：build %lu", info.dwBuildNumber);
}

static void runtime_detection(App *app) {
    wchar_t file[MAX_PATH], directml_file[MAX_PATH];
    BOOL use_directml = wcscmp(app->config.inference_device, L"directml") == 0;
    get_runtime_library_path(&app->config, file, MAX_PATH);
    HMODULE directml = NULL;
    if (use_directml) {
        get_directml_library_path(directml_file, MAX_PATH);
        directml = LoadLibraryW(directml_file);
        if (!directml) {
            set_dependency(app, 0, DEP_MISSING, L"DirectML 配套库缺失或无效：%s；请点击安装 / 修复", directml_file);
            return;
        }
    }
    HMODULE module = LoadLibraryW(file);
    BOOL has_api = module && GetProcAddress(module, "OrtGetApiBase") != NULL;
    if (module) FreeLibrary(module);
    if (directml) FreeLibrary(directml);
    if (has_api) {
        set_dependency(app, 0, DEP_OK, use_directml ? L"ONNX Runtime DirectML 已就绪：%s" : L"ONNX Runtime CPU 已就绪：%s", file);
        return;
    }
    set_dependency(app, 0, DEP_MISSING, L"ONNX Runtime 缺失或无效：%s；可点击“安装 / 修复运行库”", file);
}

static BOOL runtime_is_available(App *app) {
    wchar_t file[MAX_PATH], directml_file[MAX_PATH];
    BOOL use_directml = wcscmp(app->config.inference_device, L"directml") == 0;
    get_runtime_library_path(&app->config, file, MAX_PATH);
    HMODULE directml = NULL;
    if (use_directml) {
        get_directml_library_path(directml_file, MAX_PATH);
        directml = LoadLibraryW(directml_file);
        if (!directml) return FALSE;
    }
    HMODULE module = LoadLibraryW(file);
    if (!module) { if (directml) FreeLibrary(directml); return FALSE; }
    BOOL available = GetProcAddress(module, "OrtGetApiBase") != NULL;
    FreeLibrary(module);
    if (directml) FreeLibrary(directml);
    return available;
}

static BOOL hash_matches(const wchar_t *path, const wchar_t *expected) {
    if (!expected || !expected[0]) return TRUE;
    wchar_t actual[65];
    sha256_file(path, actual, 65);
    return actual[0] && _wcsicmp(actual, expected) == 0;
}

static BOOL regular_file_at_least(const wchar_t *path, LONGLONG minimum);

static const wchar_t *model_expected_hash(const wchar_t *tier, BOOL detector) {
    if (_wcsicmp(tier, L"tiny") == 0)
        return detector ? L"193bab7a04fca699a6c82e6abb5b81bdb28177f0abd4062552b04908dafb19f8" :
                          L"9ef676d6ed3c88256a2d92c640c44f25b0c40947e111b14b8be8f594091563e6";
    if (_wcsicmp(tier, L"small") == 0)
        return detector ? L"d73e0058b7a8086bbd57f3d10b8bcd4ff95363f67e06e2762b5e814fe9c9410e" :
                          L"5435fd747c9e0efe15a96d0b378d5bd157e9492ed8fd80edf08f30d02fa24634";
    if (_wcsicmp(tier, L"medium") == 0)
        return detector ? L"eb13b44b25bb36f89528b68720af8a61d9cf381176107f465db1757b65d086e1" :
                          L"9c09abf0957f7968c7586464b7397b84ad2387a0497a351af40e9acc71b673ba";
    return L"";
}

static BOOL model_file_ready(const wchar_t *path, LONGLONG minimum, const wchar_t *tier, BOOL detector) {
    if (!regular_file_at_least(path, minimum)) return FALSE;
    const wchar_t *expected = model_expected_hash(tier, detector);
    return !expected[0] || hash_matches(path, expected);
}

static void gpu_detection(App *app) {
    IDXGIFactory1 *factory = NULL;
    IDXGIAdapter1 *adapter = NULL;
    wchar_t description[256] = L"未找到 DirectML/GPU";
    int count = 0;
    if (CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory) == S_OK) {
        for (UINT i = 0; factory->lpVtbl->EnumAdapters1(factory, i, &adapter) == S_OK; ++i) {
            DXGI_ADAPTER_DESC1 desc;
            if (adapter->lpVtbl->GetDesc1(adapter, &desc) == S_OK && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                wcsncpy_s(description, 256, desc.Description, _TRUNCATE);
                ++count;
            }
            if (adapter) adapter->lpVtbl->Release(adapter);
        }
        factory->lpVtbl->Release(factory);
    }
    wchar_t directml_path[MAX_PATH];
    get_directml_library_path(directml_path, MAX_PATH);
    HMODULE directml = LoadLibraryW(directml_path);
    HMODULE dml_runtime_module = NULL;
    wchar_t runtime[MAX_PATH], dml_runtime[MAX_PATH];
    get_runtime_dir(runtime, MAX_PATH);
    _snwprintf_s(dml_runtime, MAX_PATH, _TRUNCATE, L"%s\\onnxruntime_dml.dll", runtime);
    if (directml) dml_runtime_module = LoadLibraryW(dml_runtime);
    BOOL directml_ok = directml != NULL && dml_runtime_module != NULL &&
                       GetProcAddress(dml_runtime_module, "OrtGetApiBase") != NULL;
    if (dml_runtime_module) FreeLibrary(dml_runtime_module);
    if (directml) FreeLibrary(directml);
    set_dependency(app, 1, count && directml_ok ? DEP_OK : DEP_WARN,
                   L"%s（适配器 %d 个；DirectML %s；CPU 推理始终可用）",
                   description, count, directml_ok ? L"可用" : L"不可用");
}

static void model_detection(App *app) {
    wchar_t model_dir[MAX_PATH], det[MAX_PATH], rec[MAX_PATH], dict[MAX_PATH];
    get_model_dir(model_dir, MAX_PATH);
    _snwprintf_s(det, MAX_PATH, _TRUNCATE, L"%s\\PP-OCRv6_%s_det.onnx", model_dir, app->config.model_tier);
    _snwprintf_s(rec, MAX_PATH, _TRUNCATE, L"%s\\PP-OCRv6_%s_rec.onnx", model_dir, app->config.model_tier);
    _snwprintf_s(dict, MAX_PATH, _TRUNCATE, L"%s\\ppocr_keys_v6.txt", model_dir);
    HANDLE a = CreateFileW(det, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    HANDLE r = CreateFileW(rec, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    HANDLE d = CreateFileW(dict, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    LARGE_INTEGER sa = {0}, sr = {0}, sd = {0};
    BOOL sizes_ok = a != INVALID_HANDLE_VALUE && r != INVALID_HANDLE_VALUE && d != INVALID_HANDLE_VALUE &&
                    GetFileSizeEx(a, &sa) && GetFileSizeEx(r, &sr) && GetFileSizeEx(d, &sd) &&
                    sa.QuadPart > 1024 * 1024 && sr.QuadPart > 1024 * 1024 && sd.QuadPart > 10000;
    const wchar_t *det_hash = model_expected_hash(app->config.model_tier, TRUE);
    const wchar_t *rec_hash = model_expected_hash(app->config.model_tier, FALSE);
    const wchar_t *dict_hash = L"b5f2bfe2bdd9448429e3e82b51c789775d9b42f2403d082b00662eb77e401c5d";
    BOOL hashes_ok = !sizes_ok || ((!det_hash[0] || hash_matches(det, det_hash)) &&
                                   (!rec_hash[0] || hash_matches(rec, rec_hash)) && hash_matches(dict, dict_hash));
    if (sizes_ok && hashes_ok) {
        set_dependency(app, 2, DEP_OK, L"PP-OCRv6 %s 就绪（检测 %.1f MB，识别 %.1f MB，字典已安装）",
                       app->config.model_tier, sa.QuadPart / 1048576.0, sr.QuadPart / 1048576.0);
    } else if (sizes_ok) {
        set_dependency(app, 2, DEP_MISSING, L"PP-OCRv6 %s 模型 SHA-256 校验失败；点击“校验并修复模型”", app->config.model_tier);
    } else {
        set_dependency(app, 2, DEP_MISSING, L"缺少 PP-OCRv6 %s 模型或字典；点击“校验并修复模型”", app->config.model_tier);
    }
    if (a != INVALID_HANDLE_VALUE) CloseHandle(a);
    if (r != INVALID_HANDLE_VALUE) CloseHandle(r);
    if (d != INVALID_HANDLE_VALUE) CloseHandle(d);
}

static void window_detection(App *app) {
    HWND hwnd = find_game_window(app->config.window_title);
    if (!hwnd) hwnd = find_game_window(L"原神");
    set_dependency(app, 3, hwnd ? DEP_OK : DEP_WARN,
                   hwnd ? L"已找到游戏窗口：%p" : L"未找到窗口；启动游戏后点“重新检测”", (void*)hwnd);
}

static void plugin_detection(App *app) {
    if (!app->config.music_enabled) {
        set_dependency(app, 4, DEP_WARN, L"点歌插件已停用；可在“插件”页重新启用");
        return;
    }
    DWORD attributes = app->config.lxmusic_path[0] ? GetFileAttributesW(app->config.lxmusic_path) : INVALID_FILE_ATTRIBUTES;
    if (!app->config.lxmusic_path[0])
        set_dependency(app, 4, DEP_WARN, L"未配置 LxMusic 路径；点歌插件停用");
    else if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        set_dependency(app, 4, DEP_OK, L"LxMusic 路径有效");
    else
        set_dependency(app, 4, DEP_MISSING, L"LxMusic 路径不存在");
}

static void ai_detection(App *app) {
    wchar_t env_value[256]; env_value[0] = 0;
    wchar_t env_name[128];
    wcscpy_s(env_name, 128, app->config.ai_key_env);
    GetEnvironmentVariableW(env_name, env_value, 256);
    BOOL has_key = env_value[0] || app->config.ai_key[0];
    set_dependency(app, 5, has_key ? DEP_OK : DEP_WARN,
                   has_key ? L"AI 地址与 Key 已配置：%s / 模型 %s" : L"API Key 未配置；环境变量 %s 为空",
                   has_key ? app->config.ai_base_url : app->config.ai_key_env,
                   has_key ? app->config.ai_model : L"");
}

static DWORD WINAPI detection_thread(LPVOID parameter) {
    App *app = (App*)parameter;
    InterlockedExchange(&app->busy, 1); ui_set_busy(1);
    app_log_format(L"INFO", L"开始依赖检测...");
    for (int i = 0; i < app->dep_count; ++i) set_dependency(app, i, DEP_CHECKING, L"检测中...");
    if (!InterlockedCompareExchange(&app->cancel_requested, 0, 0)) runtime_detection(app);
    if (!InterlockedCompareExchange(&app->cancel_requested, 0, 0)) gpu_detection(app);
    if (!InterlockedCompareExchange(&app->cancel_requested, 0, 0)) model_detection(app);
    if (!InterlockedCompareExchange(&app->cancel_requested, 0, 0)) window_detection(app);
    if (!InterlockedCompareExchange(&app->cancel_requested, 0, 0)) plugin_detection(app);
    if (!InterlockedCompareExchange(&app->cancel_requested, 0, 0)) ai_detection(app);
    if (!InterlockedCompareExchange(&app->cancel_requested, 0, 0)) windows_detection(app);
    if (!InterlockedCompareExchange(&app->cancel_requested, 0, 0))
        set_dependency(app, 7, DEP_OK, L"BitBlt / PrintWindow / Desktop Duplication 已启用；不支持时自动回退");
    app_log_format(L"INFO", InterlockedCompareExchange(&app->cancel_requested, 0, 0) ? L"依赖检测已取消" : L"依赖检测完成");
    InterlockedExchange(&app->busy, 0); ui_set_busy(0);
    return 0;
}

void dependency_init(App *app) {
    app->dep_count = 8;
    DepState initial[8] = { DEP_CHECKING, DEP_CHECKING, DEP_CHECKING, DEP_CHECKING, DEP_CHECKING, DEP_CHECKING, DEP_CHECKING, DEP_OK };
    const wchar_t *titles[8] = {
        L"ONNX Runtime", L"DirectML / GPU", L"PP-OCRv6 模型", L"原神窗口",
        L"点歌插件", L"AI 服务", L"Windows 环境", L"图像捕获"
    };
    for (int i = 0; i < app->dep_count; ++i) {
        memset(&app->deps[i], 0, sizeof(DependencyItem));
        app->deps[i].state = initial[i];
        _snwprintf_s(app->deps[i].title, 64, _TRUNCATE, L"%s", titles[i]);
        _snwprintf_s(app->deps[i].detail, 512, _TRUNCATE, L"等待检测");
    }
}

void dependency_detect_async(App *app) {
    background_reap(app);
    if (app->task_thread) return;
    if (InterlockedCompareExchange(&app->busy, 1, 0) != 0) return;
    InterlockedExchange(&app->cancel_requested, 0);
    HANDLE thread = CreateThread(NULL, 0, detection_thread, app, 0, NULL);
    if (!thread) { InterlockedExchange(&app->busy, 0); return; }
    app->task_thread = thread;
    ui_set_busy(1);
}

static LONGLONG file_size_or_zero(const wchar_t *path) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER size = {0};
    if (file != INVALID_HANDLE_VALUE) {
        GetFileSizeEx(file, &size);
        CloseHandle(file);
    }
    return size.QuadPart;
}

static void set_download_progress(App *app, LONG progress) {
    progress = max(0, min(100, progress));
    InterlockedExchange(&app->download_progress, progress);
    ui_refresh_dependency_controls();
}

static BOOL cancellable_delay(App *app, DWORD milliseconds) {
    DWORD elapsed = 0;
    while (elapsed < milliseconds) {
        if (InterlockedCompareExchange(&app->cancel_requested, 0, 0)) return FALSE;
        DWORD step = min(100, milliseconds - elapsed);
        Sleep(step);
        elapsed += step;
    }
    return TRUE;
}

static BOOL download_once(App *app, const wchar_t *url, const wchar_t *destination) {
    URL_COMPONENTSW components = { sizeof(components) };
    wchar_t host[256] = L"", path[2048] = L"", extra[2048] = L"", request_path[4096] = L"";
    components.dwHostNameLength = 255; components.lpszHostName = host;
    components.dwUrlPathLength = 2047; components.lpszUrlPath = path;
    components.dwExtraInfoLength = 2047; components.lpszExtraInfo = extra;
    if (!WinHttpCrackUrl(url, 0, 0, &components)) {
        app_log_format(L"ERROR", L"下载地址无效：%s", url);
        return FALSE;
    }
    _snwprintf_s(request_path, 4096, _TRUNCATE, L"%s%s", path, extra);
    LONGLONG existing = file_size_or_zero(destination);
    BOOL ok = FALSE;
    BOOL restart = FALSE;
    for (int round = 0; round < 2 && !ok; ++round) {
        restart = FALSE;
        HINTERNET session = WinHttpOpen(APP_NAME, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        HINTERNET connect = session ? WinHttpConnect(session, host, components.nPort, 0) : NULL;
        HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", request_path, NULL, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : NULL;
        if (request) {
            int timeout = 30000;
            WinHttpSetTimeouts(request, timeout, timeout, timeout, timeout);
            wchar_t range_header[128] = L"";
            if (existing > 0) _snwprintf_s(range_header, 128, _TRUNCATE, L"Range: bytes=%lld-\r\n", existing);
            ok = WinHttpSendRequest(request, range_header[0] ? range_header : WINHTTP_NO_ADDITIONAL_HEADERS,
                                    range_header[0] ? (DWORD)-1 : 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                 WinHttpReceiveResponse(request, NULL);
            DWORD status = 0, status_size = sizeof(status);
            if (ok) {
                WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX);
                if (status == 416 && existing > 0) {
                    WinHttpCloseHandle(request);
                    WinHttpCloseHandle(connect);
                    WinHttpCloseHandle(session);
                    request = NULL;
                    connect = NULL;
                    session = NULL;
                    DeleteFileW(destination);
                    existing = 0;
                    ok = FALSE;
                    restart = TRUE;
                    continue;
                }
                if (status != 200 && status != 206) ok = FALSE;
                if (status == 200) existing = 0;
            }
            if (ok) {
                HANDLE file = CreateFileW(destination, GENERIC_WRITE, 0, NULL,
                                          existing > 0 ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                ok = file != INVALID_HANDLE_VALUE;
                if (ok && existing > 0) {
                    LARGE_INTEGER offset; offset.QuadPart = existing;
                    ok = SetFilePointerEx(file, offset, NULL, FILE_BEGIN);
                }
                DWORD content_length = 0, content_length_size = sizeof(content_length);
                BOOL has_length = WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                                       WINHTTP_HEADER_NAME_BY_INDEX, &content_length,
                                                       &content_length_size, WINHTTP_NO_HEADER_INDEX);
                ULONGLONG expected = has_length ? (ULONGLONG)existing + content_length : 0;
                ULONGLONG total = (ULONGLONG)existing;
                LONG last_reported = -10;
                DWORD read = 0, written = 0;
                BYTE buffer[65536];
                while (ok) {
                    if (InterlockedCompareExchange(&app->cancel_requested, 0, 0)) { ok = FALSE; break; }
                    if (!WinHttpReadData(request, buffer, sizeof(buffer), &read)) { ok = FALSE; break; }
                    if (!read) break;
                    ok = WriteFile(file, buffer, read, &written, NULL) && read == written;
                    total += read;
                    LONG progress = expected ? (LONG)min(99, (total * 100) / expected) : 0;
                    if (progress >= last_reported + 5) {
                        set_download_progress(app, progress);
                        if (expected)
                            app_log_format(L"INFO", L"下载进度：%ld%%（%.1f / %.1f MB）",
                                           progress, total / 1048576.0, expected / 1048576.0);
                        else
                            app_log_format(L"INFO", L"已下载：%.1f MB", total / 1048576.0);
                        last_reported = progress;
                    }
                }
                if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
                if (ok && expected && total != expected) ok = FALSE;
                if (ok) {
                    set_download_progress(app, 100);
                    app_log_format(L"INFO", L"下载完成：%.1f MB → %s", total / 1048576.0, destination);
                }
            }
        }
        if (request) WinHttpCloseHandle(request);
        if (connect) WinHttpCloseHandle(connect);
        if (session) WinHttpCloseHandle(session);
        if (!ok && !restart) break;
    }
    return ok;
}

static BOOL download_to_file(App *app, const wchar_t *url, const wchar_t *destination) {
    InterlockedExchange(&app->download_active, 1);
    set_download_progress(app, 0);
    BOOL ok = FALSE;
    for (int attempt = 1; attempt <= 3 && !ok; ++attempt) {
        if (InterlockedCompareExchange(&app->cancel_requested, 0, 0)) break;
        ok = download_once(app, url, destination);
        if (!ok && !InterlockedCompareExchange(&app->cancel_requested, 0, 0) && attempt < 3) {
            app_log_format(L"WARNING", L"下载中断，1 秒后继续断点下载（%d/3）", attempt + 1);
            if (!cancellable_delay(app, 1000)) break;
        }
    }
    InterlockedExchange(&app->download_active, 0);
    ui_refresh_dependency_controls();
    if (!ok) {
        if (InterlockedCompareExchange(&app->cancel_requested, 0, 0))
            app_log_format(L"WARNING", L"下载已取消，临时文件已保留，可再次点击继续");
        else
            app_log_format(L"ERROR", L"下载失败：%s", url);
    }
    return ok;
}

void dependency_cancel(App *app) {
    if (!app || !app->task_thread) return;
    if (!InterlockedExchange(&app->cancel_requested, 1))
        app_log_format(L"WARNING", InterlockedCompareExchange(&app->download_active, 0, 0)
            ? L"正在取消下载，请稍候..." : L"正在取消后台检测，请稍候...");
}

static BOOL run_hidden(const wchar_t *command, DWORD timeout_ms) {
    STARTUPINFOW startup = { sizeof(startup) };
    PROCESS_INFORMATION process;
    wchar_t command_copy[4096];
    _snwprintf_s(command_copy, 4096, _TRUNCATE, L"%s", command);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    if (!CreateProcessW(NULL, command_copy, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &startup, &process))
        return FALSE;
    DWORD wait = WaitForSingleObject(process.hProcess, timeout_ms);
    DWORD exit_code = 1;
    if (wait == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &exit_code);
    else {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 5000);
    }
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return wait == WAIT_OBJECT_0 && exit_code == 0;
}

static BOOL delete_tree(const wchar_t *path) {
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) return TRUE;
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
        return DeleteFileW(path);
    }
    wchar_t search[MAX_PATH], child[MAX_PATH];
    WIN32_FIND_DATAW data;
    _snwprintf_s(search, MAX_PATH, _TRUNCATE, L"%s\\*", path);
    HANDLE find = FindFirstFileW(search, &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
            _snwprintf_s(child, MAX_PATH, _TRUNCATE, L"%s\\%s", path, data.cFileName);
            if (!delete_tree(child)) {
                FindClose(find);
                return FALSE;
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
    return RemoveDirectoryW(path);
}

static BOOL install_runtime(App *app) {
    wchar_t runtime[MAX_PATH], package_dir[MAX_PATH], directml_dir[MAX_PATH] = L"";
    wchar_t zip[MAX_PATH], zip_temp[MAX_PATH], command[4096];
    get_runtime_dir(runtime, MAX_PATH);
    _snwprintf_s(package_dir, MAX_PATH, _TRUNCATE, L"%s\\package", runtime);
    _snwprintf_s(zip, MAX_PATH, _TRUNCATE, L"%s\\onnxruntime.zip", runtime);
    _snwprintf_s(zip_temp, MAX_PATH, _TRUNCATE, L"%s.download", zip);
    if (runtime_is_available(app)) {
        set_dependency(app, 0, DEP_OK, L"当前设备的 ONNX Runtime 已安装，无需重复下载");
        return TRUE;
    }
    delete_tree(package_dir);
    ensure_directory(package_dir);
    BOOL use_directml = wcscmp(app->config.inference_device, L"directml") == 0;
    wchar_t runtime_url[2048] = L"";
    wchar_t runtime_hash[65] = L"78d447051e48bd2e1e778bba378bec4ece11191c9e538cf7b2c4a4565e8f5581";
    runtime_source_url(app, use_directml, runtime_url, 2048);
    if (use_directml)
        wcscpy_s(runtime_hash, 65, L"6763468507B7CFC777B1334B3E174C11A540DDACB7BD4354BC2E0EC89E56EEC2");
    GetEnvironmentVariableW(L"CHATGIBOT_RUNTIME_URL", runtime_url, 2048);
    GetEnvironmentVariableW(L"CHATGIBOT_RUNTIME_SHA256", runtime_hash, 65);
    set_dependency(app, 0, DEP_INSTALLING, use_directml ? L"正在下载 ONNX Runtime DirectML x64..." : L"正在下载 ONNX Runtime CPU x64...");
    BOOL runtime_downloaded = download_to_file(app, runtime_url, zip_temp);
    if (!runtime_downloaded && wcscmp(runtime_url, L"https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/onnxruntime-win-x64-1.20.1.zip") != 0 && !use_directml) {
        app_log_format(L"WARNING", L"镜像运行库下载失败，切换官方 GitHub 源重试");
        wcscpy_s(runtime_hash, 65, L"78d447051e48bd2e1e778bba378bec4ece11191c9e538cf7b2c4a4565e8f5581");
        runtime_downloaded = download_to_file(app,
            L"https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/onnxruntime-win-x64-1.20.1.zip", zip_temp);
    }
    if (!runtime_downloaded) {
        delete_tree(package_dir);
        set_dependency(app, 0, DEP_MISSING,
                       InterlockedCompareExchange(&app->cancel_requested, 0, 0) ? L"ONNX Runtime 下载已取消，可再次点击继续" : L"ONNX Runtime 下载失败");
        return FALSE;
    }
    if (!hash_matches(zip_temp, runtime_hash)) {
        app_log_format(L"ERROR", L"ONNX Runtime 下载包 SHA-256 校验失败，已丢弃");
        DeleteFileW(zip_temp);
        delete_tree(package_dir);
        set_dependency(app, 0, DEP_MISSING, L"ONNX Runtime 下载包校验失败");
        return FALSE;
    }
    if (!MoveFileExW(zip_temp, zip, MOVEFILE_REPLACE_EXISTING)) {
        delete_tree(package_dir);
        set_dependency(app, 0, DEP_MISSING, L"ONNX Runtime 临时文件提交失败");
        return FALSE;
    }
    if (use_directml) {
        _snwprintf_s(command, 4096, _TRUNCATE,
                     L"tar.exe -xf \"%s\" -C \"%s\" runtimes/win-x64/native/onnxruntime.dll",
                     zip, package_dir);
    } else {
        _snwprintf_s(command, 4096, _TRUNCATE,
                     L"tar.exe -xf \"%s\" -C \"%s\" "
                     L"onnxruntime-win-x64-1.20.1/lib/onnxruntime.dll "
                     L"onnxruntime-win-x64-1.20.1/lib/onnxruntime_providers_shared.dll",
                     zip, package_dir);
    }
    if (!run_hidden(command, 120000)) {
        DeleteFileW(zip);
        delete_tree(package_dir);
        set_dependency(app, 0, DEP_MISSING, L"运行库解压失败");
        return FALSE;
    }
    wchar_t found[MAX_PATH] = L"", found_shared[MAX_PATH] = L"", found_directml[MAX_PATH] = L"";
    BOOL ok = FALSE;
    if (use_directml) {
        _snwprintf_s(found, MAX_PATH, _TRUNCATE, L"%s\\runtimes\\win-x64\\native\\onnxruntime.dll", package_dir);
        ok = GetFileAttributesW(found) != INVALID_FILE_ATTRIBUTES;
        wchar_t directml_url[2048] = L"https://api.nuget.org/v3-flatcontainer/microsoft.ai.directml/1.15.2/microsoft.ai.directml.1.15.2.nupkg";
        wchar_t directml_hash[65], directml_zip[MAX_PATH], directml_temp[MAX_PATH];
        wcscpy_s(directml_hash, 65, DIRECTML_PACKAGE_HASH);
        GetEnvironmentVariableW(L"CHATGIBOT_DIRECTML_URL", directml_url, 2048);
        GetEnvironmentVariableW(L"CHATGIBOT_DIRECTML_SHA256", directml_hash, 65);
        _snwprintf_s(directml_zip, MAX_PATH, _TRUNCATE, L"%s\\directml.nupkg", runtime);
        _snwprintf_s(directml_temp, MAX_PATH, _TRUNCATE, L"%s.download", directml_zip);
        _snwprintf_s(directml_dir, MAX_PATH, _TRUNCATE, L"%s\\directml-package", runtime);
        delete_tree(directml_dir);
        ensure_directory(directml_dir);
        set_dependency(app, 0, DEP_INSTALLING, L"正在下载 Microsoft DirectML 1.15.2 x64...");
        if (!download_to_file(app, directml_url, directml_temp)) {
            DeleteFileW(zip);
            delete_tree(package_dir);
            delete_tree(directml_dir);
            set_dependency(app, 0, DEP_MISSING,
                           InterlockedCompareExchange(&app->cancel_requested, 0, 0) ? L"Microsoft DirectML 下载已取消，可再次点击继续" : L"Microsoft DirectML 配套包下载失败");
            return FALSE;
        }
        if (!hash_matches(directml_temp, directml_hash) ||
            !MoveFileExW(directml_temp, directml_zip, MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileW(directml_temp);
            DeleteFileW(zip);
            delete_tree(package_dir);
            delete_tree(directml_dir);
            set_dependency(app, 0, DEP_MISSING, L"Microsoft DirectML 配套包校验或提交失败");
            return FALSE;
        }
        _snwprintf_s(command, 4096, _TRUNCATE,
                     L"tar.exe -xf \"%s\" -C \"%s\" bin/x64-win/DirectML.dll",
                     directml_zip, directml_dir);
        if (!run_hidden(command, 120000)) {
            DeleteFileW(directml_zip);
            DeleteFileW(zip);
            delete_tree(package_dir);
            delete_tree(directml_dir);
            set_dependency(app, 0, DEP_MISSING, L"Microsoft DirectML 配套包解压失败");
            return FALSE;
        }
        _snwprintf_s(found_directml, MAX_PATH, _TRUNCATE, L"%s\\bin\\x64-win\\DirectML.dll", directml_dir);
        ok = ok && hash_matches(found_directml, DIRECTML_DLL_HASH);
        DeleteFileW(directml_zip);
    } else {
        wchar_t search[MAX_PATH];
        WIN32_FIND_DATAW data;
        _snwprintf_s(search, MAX_PATH, _TRUNCATE, L"%s\\onnxruntime-win-x64-*", package_dir);
        HANDLE find = FindFirstFileW(search, &data);
        while (find != INVALID_HANDLE_VALUE) {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                wcscmp(data.cFileName, L".") != 0 && wcscmp(data.cFileName, L"..") != 0) {
                wchar_t lib_dir[MAX_PATH];
                _snwprintf_s(lib_dir, MAX_PATH, _TRUNCATE, L"%s\\%s\\lib", package_dir, data.cFileName);
                _snwprintf_s(found, MAX_PATH, _TRUNCATE, L"%s\\onnxruntime.dll", lib_dir);
                _snwprintf_s(found_shared, MAX_PATH, _TRUNCATE, L"%s\\onnxruntime_providers_shared.dll", lib_dir);
                if (GetFileAttributesW(found) != INVALID_FILE_ATTRIBUTES) { ok = TRUE; break; }
            }
            if (!FindNextFileW(find, &data)) break;
        }
        if (find != INVALID_HANDLE_VALUE) FindClose(find);
    }
    if (ok) {
        wchar_t target[MAX_PATH], target_temp[MAX_PATH], shared_target[MAX_PATH];
        _snwprintf_s(target, MAX_PATH, _TRUNCATE, L"%s\\%s", runtime, use_directml ? L"onnxruntime_dml.dll" : L"onnxruntime.dll");
        _snwprintf_s(target_temp, MAX_PATH, _TRUNCATE, L"%s.download", target);
        _snwprintf_s(shared_target, MAX_PATH, _TRUNCATE, L"%s\\onnxruntime_providers_shared.dll", runtime);
        DeleteFileW(target_temp);
        ok = CopyFileW(found, target_temp, FALSE);
        wchar_t directml_target[MAX_PATH] = L"", directml_target_temp[MAX_PATH] = L"";
        if (use_directml && ok) {
            get_directml_library_path(directml_target, MAX_PATH);
            _snwprintf_s(directml_target_temp, MAX_PATH, _TRUNCATE, L"%s.download", directml_target);
            DeleteFileW(directml_target_temp);
            ok = CopyFileW(found_directml, directml_target_temp, FALSE) &&
                 hash_matches(directml_target_temp, DIRECTML_DLL_HASH) &&
                 MoveFileExW(directml_target_temp, directml_target, MOVEFILE_REPLACE_EXISTING);
            DeleteFileW(directml_target_temp);
        }
        if (ok) ok = MoveFileExW(target_temp, target, MOVEFILE_REPLACE_EXISTING);
        if (!use_directml && ok && GetFileAttributesW(found_shared) != INVALID_FILE_ATTRIBUTES)
            ok = CopyFileW(found_shared, shared_target, FALSE);
        DeleteFileW(target_temp);
    }
    DeleteFileW(zip);
    delete_tree(package_dir);
    if (directml_dir[0]) delete_tree(directml_dir);
    if (!ok) set_dependency(app, 0, DEP_MISSING, L"运行库 DLL 未找到；请重试安装");
    runtime_detection(app);
    return g_app.deps[0].state == DEP_OK;
}

static BOOL download_model(const wchar_t *url, const wchar_t *destination, const wchar_t *expected_hash) {
    wchar_t temp[MAX_PATH];
    _snwprintf_s(temp, MAX_PATH, _TRUNCATE, L"%s.download", destination);
    if (!download_to_file(&g_app, url, temp)) {
        if (!InterlockedCompareExchange(&g_app.cancel_requested, 0, 0)) DeleteFileW(temp);
        return FALSE;
    }
    HANDLE file = CreateFileW(temp, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    LARGE_INTEGER size = {0};
    BOOL valid = file != INVALID_HANDLE_VALUE && GetFileSizeEx(file, &size) && size.QuadPart > 1024 * 1024;
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (!valid || !hash_matches(temp, expected_hash)) {
        DeleteFileW(temp);
        app_log_format(L"ERROR", L"模型文件不完整或 SHA-256 不匹配，已丢弃：%s", url);
        return FALSE;
    }
    if (!MoveFileExW(temp, destination, MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(temp);
        return FALSE;
    }
    return TRUE;
}

static BOOL regular_file_at_least(const wchar_t *path, LONGLONG minimum) {
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return FALSE;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER size = {0};
    BOOL valid = file != INVALID_HANDLE_VALUE && GetFileSizeEx(file, &size) && size.QuadPart >= minimum;
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    return valid;
}

static BOOL download_dictionary(const wchar_t *url, const wchar_t *destination, const wchar_t *expected_hash) {
    wchar_t temp[MAX_PATH]; _snwprintf_s(temp, MAX_PATH, _TRUNCATE, L"%s.download", destination);
    if (!download_to_file(&g_app, url, temp)) {
        if (!InterlockedCompareExchange(&g_app.cancel_requested, 0, 0)) DeleteFileW(temp);
        return FALSE;
    }
    HANDLE file = CreateFileW(temp, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    LARGE_INTEGER size = {0}; BOOL valid = file != INVALID_HANDLE_VALUE && GetFileSizeEx(file, &size) && size.QuadPart > 10000;
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (!valid || !hash_matches(temp, expected_hash)) { DeleteFileW(temp); return FALSE; }
    if (!MoveFileExW(temp, destination, MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(temp); return FALSE; }
    return TRUE;
}

static BOOL repair_models(App *app) {
    wchar_t dir[MAX_PATH], det[MAX_PATH], rec[MAX_PATH], dict[MAX_PATH], tier[32];
    get_model_dir(dir, MAX_PATH);
    _snwprintf_s(tier, 32, _TRUNCATE, L"%s", app->config.model_tier);
    CharLowerW(tier);
    _snwprintf_s(det, MAX_PATH, _TRUNCATE, L"%s\\PP-OCRv6_%s_det.onnx", dir, app->config.model_tier);
    _snwprintf_s(rec, MAX_PATH, _TRUNCATE, L"%s\\PP-OCRv6_%s_rec.onnx", dir, app->config.model_tier);
    _snwprintf_s(dict, MAX_PATH, _TRUNCATE, L"%s\\ppocr_keys_v6.txt", dir);
    BOOL det_ready = model_file_ready(det, 1024 * 1024, tier, TRUE);
    BOOL rec_ready = model_file_ready(rec, 1024 * 1024, tier, FALSE);
    BOOL dict_ready = regular_file_at_least(dict, 10000) &&
                      hash_matches(dict, L"b5f2bfe2bdd9448429e3e82b51c789775d9b42f2403d082b00662eb77e401c5d");
    if (det_ready && rec_ready && dict_ready) {
        model_detection(app);
        return app->deps[2].state == DEP_OK;
    }
    set_dependency(app, 2, DEP_INSTALLING, L"正在准备 PP-OCRv6 %s 模型...", tier);
    wchar_t custom_det[2048] = L"", custom_rec[2048] = L"", custom_dict[2048] = L"";
    GetEnvironmentVariableW(L"CHATGIBOT_MODEL_DET_URL", custom_det, 2048);
    GetEnvironmentVariableW(L"CHATGIBOT_MODEL_REC_URL", custom_rec, 2048);
    GetEnvironmentVariableW(L"CHATGIBOT_MODEL_DICT_URL", custom_dict, 2048);
    const wchar_t *det_expected = model_expected_hash(tier, TRUE);
    const wchar_t *rec_expected = model_expected_hash(tier, FALSE);
    const wchar_t *dict_expected = L"b5f2bfe2bdd9448429e3e82b51c789775d9b42f2403d082b00662eb77e401c5d";
    int order[3], order_count = source_order(app, order);
    BOOL detector_downloaded = det_ready || (custom_det[0] && download_model(custom_det, det, det_expected));
    BOOL recognizer_downloaded = rec_ready || (custom_rec[0] && download_model(custom_rec, rec, rec_expected));
    wchar_t source_det[2048], source_rec[2048], source_dict[2048];
    for (int i = 0; i < order_count && !detector_downloaded; ++i) {
        model_urls(order[i], tier, source_det, source_rec, source_dict);
        app_log_format(L"INFO", L"尝试下载检测模型：%s", source_det);
        detector_downloaded = download_model(source_det, det, det_expected);
    }
    if (!detector_downloaded) {
        set_dependency(app, 2, DEP_MISSING,
                       InterlockedCompareExchange(&app->cancel_requested, 0, 0) ? L"检测模型下载已取消，可再次点击继续" : L"检测模型下载失败；请更换镜像源后重试");
        return FALSE;
    }
    set_dependency(app, 2, DEP_INSTALLING, L"正在下载识别模型...");
    for (int i = 0; i < order_count && !recognizer_downloaded; ++i) {
        model_urls(order[i], tier, source_det, source_rec, source_dict);
        app_log_format(L"INFO", L"尝试下载识别模型：%s", source_rec);
        recognizer_downloaded = download_model(source_rec, rec, rec_expected);
    }
    if (!recognizer_downloaded) {
        set_dependency(app, 2, DEP_MISSING,
                       InterlockedCompareExchange(&app->cancel_requested, 0, 0) ? L"识别模型下载已取消，可再次点击继续" : L"识别模型下载失败；请更换镜像源后重试");
        return FALSE;
    }
    set_dependency(app, 2, DEP_INSTALLING, dict_ready ? L"正在检查 PP-OCRv6 字典..." : L"正在下载 PP-OCRv6 字典...");
    BOOL dictionary_downloaded = dict_ready || (custom_dict[0] && download_dictionary(custom_dict, dict, dict_expected));
    for (int i = 0; i < order_count && !dictionary_downloaded; ++i) {
        model_urls(order[i], tier, source_det, source_rec, source_dict);
        app_log_format(L"INFO", L"尝试下载 OCR 字典：%s", source_dict);
        dictionary_downloaded = download_dictionary(source_dict, dict, dict_expected);
    }
    if (!dictionary_downloaded) {
        set_dependency(app, 2, DEP_MISSING, L"字典下载失败；请检查网络后重试");
        return FALSE;
    }
    model_detection(app);
    return app->deps[2].state == DEP_OK;
}

static DWORD WINAPI install_thread(LPVOID parameter) {
    App *app = (App*)parameter;
    InterlockedExchange(&app->busy, 1); ui_set_busy(1);
    if (install_runtime(app)) detection_thread(app);
    else app_log_format(L"ERROR", L"ONNX Runtime 安装未完成");
    InterlockedExchange(&app->busy, 0); ui_set_busy(0);
    return 0;
}

BOOL dependency_install_runtime_async(App *app) {
    if (InterlockedCompareExchange(&app->running, 1, 1)) return FALSE;
    background_reap(app);
    if (app->task_thread) return FALSE;
    if (InterlockedCompareExchange(&app->busy, 1, 0) != 0) return FALSE;
    InterlockedExchange(&app->cancel_requested, 0);
    InterlockedExchange(&app->download_progress, 0);
    HANDLE thread = CreateThread(NULL, 0, install_thread, app, 0, NULL);
    if (!thread) { InterlockedExchange(&app->busy, 0); return FALSE; }
    app->task_thread = thread;
    ui_set_busy(1);
    return TRUE;
}

static DWORD WINAPI models_thread(LPVOID parameter) {
    App *app = (App*)parameter;
    InterlockedExchange(&app->busy, 1); ui_set_busy(1);
    if (repair_models(app)) detection_thread(app);
    else app_log_format(L"ERROR", L"模型修复未完成");
    InterlockedExchange(&app->busy, 0); ui_set_busy(0);
    return 0;
}

BOOL dependency_repair_models_async(App *app) {
    if (InterlockedCompareExchange(&app->running, 1, 1)) return FALSE;
    background_reap(app);
    if (app->task_thread) return FALSE;
    if (InterlockedCompareExchange(&app->busy, 1, 0) != 0) return FALSE;
    InterlockedExchange(&app->cancel_requested, 0);
    InterlockedExchange(&app->download_progress, 0);
    HANDLE thread = CreateThread(NULL, 0, models_thread, app, 0, NULL);
    if (!thread) { InterlockedExchange(&app->busy, 0); return FALSE; }
    app->task_thread = thread;
    ui_set_busy(1);
    return TRUE;
}

static DWORD WINAPI mirror_test_thread(LPVOID parameter) {
    App *app = (App*)parameter;
    InterlockedExchange(&app->busy, 1);
    ui_set_busy(1);
    app_log_format(L"INFO", L"开始测试下载源延迟...");
    wchar_t tier[32], det[2048], rec[2048], dict[2048], runtime[2048];
    _snwprintf_s(tier, 32, _TRUNCATE, L"%s", app->config.model_tier);
    BOOL directml = wcscmp(app->config.inference_device, L"directml") == 0;
    for (int source = DOWNLOAD_SOURCE_MODELSCOPE; source <= DOWNLOAD_SOURCE_OFFICIAL; ++source) {
        if (InterlockedCompareExchange(&app->cancel_requested, 0, 0)) break;
        model_urls(source, tier, det, rec, dict);
        if (directml) {
            _snwprintf_s(runtime, 2048, _TRUNCATE,
                         L"https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime.directml/1.20.1/microsoft.ml.onnxruntime.directml.1.20.1.nupkg");
        } else if (source == DOWNLOAD_SOURCE_OFFICIAL) {
            wcscpy_s(runtime, 2048, L"https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/onnxruntime-win-x64-1.20.1.zip");
        } else {
            wcscpy_s(runtime, 2048, L"https://ghfast.top/https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/onnxruntime-win-x64-1.20.1.zip");
        }
        DWORD model_ms = 0, runtime_ms = 0, model_status = 0, runtime_status = 0;
        BOOL model_ok = probe_download_url(det, &model_ms, &model_status);
        BOOL runtime_ok = probe_download_url(runtime, &runtime_ms, &runtime_status);
        app_log_format(model_ok && runtime_ok ? L"INFO" : L"WARNING",
                       L"%s：模型 %s（%lu ms，HTTP %lu），运行库 %s（%lu ms，HTTP %lu）",
                       source_title(source), model_ok ? L"可达" : L"失败", model_ms, model_status,
                       runtime_ok ? L"可达" : L"失败", runtime_ms, runtime_status);
    }
    app_log_format(L"INFO", L"下载源延迟测试完成；安装时优先使用当前选择并自动回退");
    InterlockedExchange(&app->busy, 0);
    ui_set_busy(0);
    return 0;
}

BOOL dependency_test_mirrors_async(App *app) {
    if (!app || InterlockedCompareExchange(&app->running, 1, 1)) return FALSE;
    background_reap(app);
    if (app->task_thread) return FALSE;
    if (InterlockedCompareExchange(&app->busy, 1, 0) != 0) return FALSE;
    InterlockedExchange(&app->cancel_requested, 0);
    HANDLE thread = CreateThread(NULL, 0, mirror_test_thread, app, 0, NULL);
    if (!thread) {
        InterlockedExchange(&app->busy, 0);
        return FALSE;
    }
    app->task_thread = thread;
    ui_set_busy(1);
    return TRUE;
}

const DependencyItem *dependency_by_title(App *app, const wchar_t *title) {
    for (int i = 0; i < app->dep_count; ++i)
        if (wcscmp(app->deps[i].title, title) == 0) return &app->deps[i];
    return NULL;
}
