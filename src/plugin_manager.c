#include "app.h"
#include "plugin_api.h"
#include "../third_party/cjson/cJSON.h"
#include <stdio.h>
#include <wctype.h>

typedef struct {
    HMODULE module;
    wchar_t name[64];
    wchar_t path[MAX_PATH];
    ChatGIBotPluginShutdownFn shutdown;
} LoadedPlugin;

typedef struct {
    wchar_t command[64];
    BOOL (*handler)(void *handler_context, const wchar_t *args);
    void *handler_context;
    int plugin_index;
} RegisteredCommand;

static LoadedPlugin g_plugins[CHATGIBOT_MAX_PLUGINS];
static int g_plugin_count;
static RegisteredCommand g_commands[CHATGIBOT_MAX_PLUGIN_COMMANDS];
static int g_command_count;

static void plugin_log(void *context, const wchar_t *level, const wchar_t *message) {
    (void)context;
    app_log_format(level && level[0] ? level : L"INFO", L"[插件] %s", message ? message : L"");
}

static BOOL plugin_send(void *context, const wchar_t *message) {
    if (!context || !message || !message[0]) return FALSE;
    return bot_send_message((App*)context, message);
}

static BOOL plugin_get_config(void *context, const wchar_t *key, wchar_t *value, int value_chars) {
    App *app = (App*)context;
    if (!app || !key || !value || value_chars <= 0) return FALSE;
    value[0] = 0;
    wchar_t data_dir[MAX_PATH], path[MAX_PATH];
    get_data_dir(data_dir, MAX_PATH);
    _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%s\\config.json", data_dir);
    char *json = NULL;
    if (!read_text_file(path, &json, NULL)) return FALSE;
    cJSON *root = cJSON_Parse(json);
    cJSON *plugins = root ? cJSON_GetObjectItemCaseSensitive(root, "plugins") : NULL;
    cJSON *config = plugins ? cJSON_GetObjectItemCaseSensitive(plugins, "config") : NULL;
    cJSON *plugin = NULL;
    char key_utf8[256] = {0};
    BOOL ok = wide_to_utf8(key, key_utf8, sizeof(key_utf8));
    if (ok && config) {
        char *dot = strchr(key_utf8, '.');
        if (dot) {
            *dot++ = 0;
            plugin = cJSON_GetObjectItemCaseSensitive(config, key_utf8);
            cJSON *item = plugin ? cJSON_GetObjectItemCaseSensitive(plugin, dot) : NULL;
            if (cJSON_IsString(item) && item->valuestring)
                ok = utf8_to_wide(item->valuestring, value, value_chars);
            else ok = FALSE;
        } else ok = FALSE;
    } else ok = FALSE;
    if (root) cJSON_Delete(root);
    HeapFree(GetProcessHeap(), 0, json);
    return ok && value[0];
}

static BOOL plugin_register_command(void *context, const wchar_t *command,
                                    BOOL (*handler)(void *handler_context, const wchar_t *args),
                                    void *handler_context) {
    (void)context;
    if (!command || !handler || g_command_count >= CHATGIBOT_MAX_PLUGIN_COMMANDS) return FALSE;
    wchar_t normalized[64];
    wcsncpy_s(normalized, 64, command, _TRUNCATE);
    if (normalized[0] != L'/') {
        wchar_t prefixed[64];
        _snwprintf_s(prefixed, 64, _TRUNCATE, L"/%s", normalized);
        wcscpy_s(normalized, 64, prefixed);
    }
    while (wcslen(normalized) > 1 && iswspace(normalized[wcslen(normalized) - 1])) normalized[wcslen(normalized) - 1] = 0;
    for (int i = 0; i < g_command_count; ++i)
        if (_wcsicmp(g_commands[i].command, normalized) == 0) return FALSE;
    wcscpy_s(g_commands[g_command_count].command, 64, normalized);
    g_commands[g_command_count].handler = handler;
    g_commands[g_command_count].handler_context = handler_context;
    g_commands[g_command_count].plugin_index = g_plugin_count;
    ++g_command_count;
    return TRUE;
}

static BOOL plugin_is_enabled(const App *app, const wchar_t *name) {
    if (!app || !name || !name[0]) return FALSE;
    for (int i = 0; i < app->config.enabled_plugin_count; ++i)
        if (_wcsicmp(app->config.enabled_plugins[i], name) == 0) return TRUE;
    return FALSE;
}

static void plugin_load_one(App *app, const wchar_t *path) {
    wchar_t file_name[MAX_PATH];
    wcscpy_s(file_name, MAX_PATH, path);
    wchar_t *name = wcsrchr(file_name, L'\\');
    name = name ? name + 1 : file_name;
    wchar_t *extension = wcsrchr(name, L'.');
    if (extension) *extension = 0;
    if (!plugin_is_enabled(app, name) || g_plugin_count >= CHATGIBOT_MAX_PLUGINS) return;
    HMODULE module = LoadLibraryW(path);
    if (!module) {
        app_log_format(L"ERROR", L"插件加载失败：%s（错误 %lu）", path, GetLastError());
        return;
    }
    ChatGIBotPluginInitFn init = (ChatGIBotPluginInitFn)GetProcAddress(module, "ChatGIBotPluginInit");
    if (!init) {
        app_log_format(L"ERROR", L"插件缺少 ChatGIBotPluginInit：%s", path);
        FreeLibrary(module);
        return;
    }
    ChatGIBotPluginHost host = { CHATGIBOT_PLUGIN_API_VERSION, app, plugin_log, plugin_send,
                                 plugin_register_command, plugin_get_config };
    ChatGIBotPluginInfo info = { CHATGIBOT_PLUGIN_API_VERSION, NULL, NULL };
    int plugin_index = g_plugin_count;
    int command_start = g_command_count;
    if (!init(&host, &info) || !info.name || !info.name[0] || info.api_version != CHATGIBOT_PLUGIN_API_VERSION) {
        app_log_format(L"ERROR", L"插件初始化失败：%s", path);
        g_command_count = command_start;
        FreeLibrary(module);
        return;
    }
    g_plugins[plugin_index].module = module;
    wcscpy_s(g_plugins[plugin_index].name, 64, info.name);
    wcscpy_s(g_plugins[plugin_index].path, MAX_PATH, path);
    g_plugins[plugin_index].shutdown = (ChatGIBotPluginShutdownFn)GetProcAddress(module, "ChatGIBotPluginShutdown");
    ++g_plugin_count;
    app_log_format(L"INFO", L"已加载插件：%s%s%s", info.name,
                   info.version ? L" v" : L"", info.version ? info.version : L"");
}

BOOL plugin_manager_init(App *app) {
    if (!app) return FALSE;
    if (app->plugins_initialized) return TRUE;
    g_plugin_count = 0;
    g_command_count = 0;
    wchar_t exe_dir[MAX_PATH], directory[MAX_PATH], pattern[MAX_PATH];
    get_exe_dir(exe_dir, MAX_PATH);
    _snwprintf_s(directory, MAX_PATH, _TRUNCATE, L"%s\\plugins", exe_dir);
    ensure_directory(directory);
    _snwprintf_s(pattern, MAX_PATH, _TRUNCATE, L"%s\\*.dll", directory);
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(pattern, &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            wchar_t path[MAX_PATH];
            _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%s\\%s", directory, data.cFileName);
            plugin_load_one(app, path);
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    app->plugins_initialized = TRUE;
    return TRUE;
}

void plugin_manager_shutdown(App *app) {
    (void)app;
    for (int i = g_plugin_count - 1; i >= 0; --i) {
        if (g_plugins[i].shutdown) g_plugins[i].shutdown();
        if (g_plugins[i].module) FreeLibrary(g_plugins[i].module);
    }
    memset(g_plugins, 0, sizeof(g_plugins));
    memset(g_commands, 0, sizeof(g_commands));
    g_plugin_count = 0;
    g_command_count = 0;
    if (app) app->plugins_initialized = FALSE;
}

BOOL plugin_manager_dispatch(App *app, const wchar_t *command, const wchar_t *args) {
    (void)app;
    for (int i = 0; i < g_command_count; ++i) {
        if (_wcsicmp(g_commands[i].command, command) == 0)
            return g_commands[i].handler(g_commands[i].handler_context, args ? args : L"");
    }
    return FALSE;
}

void plugin_manager_list(App *app, wchar_t *output, int output_chars) {
    (void)app;
    output[0] = 0;
    if (g_plugin_count == 0) wcscpy_s(output, output_chars, L"无动态插件");
    for (int i = 0; i < g_plugin_count; ++i) {
        if (i) wcscat_s(output, output_chars, L"、");
        wcscat_s(output, output_chars, g_plugins[i].name);
    }
}

void plugin_manager_help(App *app, wchar_t *output, int output_chars) {
    (void)app;
    output[0] = 0;
    for (int i = 0; i < g_command_count; ++i) {
        if (i) wcscat_s(output, output_chars, L" ");
        wcscat_s(output, output_chars, g_commands[i].command);
    }
}
