#include <stdio.h>
#include <windows.h>
#include "../third_party/onnxruntime/onnxruntime_c_api.h"

typedef const OrtApiBase* (ORT_API_CALL *GetApiBaseFn)(void);
typedef OrtStatus* (ORT_API_CALL *AppendDmlFn)(OrtSessionOptions *options, int device_id);
typedef struct ChatGIBotDmlApi {
    OrtStatus* (ORT_API_CALL *SessionOptionsAppendExecutionProvider_DML)(OrtSessionOptions *options, int device_id);
} ChatGIBotDmlApi;

static void print_status(const OrtApi *api, const char *label, int device, OrtStatus *status) {
    if (!status) {
        printf("%s device=%d PASS\n", label, device);
        return;
    }
    const char *message = api->GetErrorMessage(status);
    printf("%s device=%d FAIL: %s\n", label, device, message ? message : "(no message)");
    api->ReleaseStatus(status);
}

int wmain(int argc, wchar_t **argv) {
    if (argc < 2) return 2;
    wchar_t directml_path[MAX_PATH];
    wcsncpy_s(directml_path, MAX_PATH, argv[1], _TRUNCATE);
    wchar_t *slash = wcsrchr(directml_path, L'\\');
    if (!slash) return 2;
    wcscpy_s(slash + 1, MAX_PATH - (size_t)(slash + 1 - directml_path), L"DirectML.dll");
    HMODULE directml_module = LoadLibraryW(directml_path);
    if (!directml_module) {
        printf("DirectML.dll LoadLibrary FAIL %lu\n", GetLastError());
        return 3;
    }
    HMODULE module = LoadLibraryW(argv[1]);
    if (!module) { printf("LoadLibrary FAIL %lu\n", GetLastError()); FreeLibrary(directml_module); return 4; }
    GetApiBaseFn get_base = (GetApiBaseFn)GetProcAddress(module, "OrtGetApiBase");
    AppendDmlFn exported_append = (AppendDmlFn)GetProcAddress(module, "OrtSessionOptionsAppendExecutionProvider_DML");
    const OrtApiBase *base = get_base ? get_base() : NULL;
    const OrtApi *api = base ? base->GetApi(ORT_API_VERSION) : NULL;
    printf("GetApi=%s exported_append=%s\n", api ? "PASS" : "FAIL", exported_append ? "yes" : "no");
    if (!api) { FreeLibrary(directml_module); return 5; }
    OrtEnv *env = NULL;
    OrtStatus *status = api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "DmlProbe", &env);
    if (status) { print_status(api, "CreateEnv", -1, status); FreeLibrary(directml_module); FreeLibrary(module); return 6; }
    const ChatGIBotDmlApi *dml_api = NULL;
    status = api->GetExecutionProviderApi("DML", ORT_API_VERSION, (const void **)&dml_api);
    print_status(api, "GetExecutionProviderApi", -1, status);
    printf("provider_api=%s\n", dml_api ? "yes" : "no");
    BOOL provider_success = FALSE;
    for (int device = 0; device < 8; ++device) {
        OrtSessionOptions *options = NULL;
        status = api->CreateSessionOptions(&options);
        if (status) { print_status(api, "CreateSessionOptions", device, status); continue; }
        if (dml_api) {
            OrtStatus *dml_status = dml_api->SessionOptionsAppendExecutionProvider_DML(options, device);
            if (!dml_status) provider_success = TRUE;
            print_status(api, "OrtDmlApi", device, dml_status);
        }
        api->ReleaseSessionOptions(options);
    }
    if (exported_append) {
        for (int device = 0; device < 8; ++device) {
            OrtSessionOptions *options = NULL;
            status = api->CreateSessionOptions(&options);
            if (status) { print_status(api, "CreateSessionOptions", device, status); continue; }
            OrtStatus *dml_status = exported_append(options, device);
            if (!dml_status) provider_success = TRUE;
            print_status(api, "DML export", device, dml_status);
            api->ReleaseSessionOptions(options);
        }
    }
    api->ReleaseEnv(env);
    FreeLibrary(directml_module);
    FreeLibrary(module);
    return provider_success ? 0 : 1;
}
