#include <stdio.h>
#include "../src/app.h"

void ui_append_log(const wchar_t *level, const wchar_t *message) { (void)level; (void)message; }
void ui_refresh_dependency_controls(void) {}
void ui_set_busy(int busy) { (void)busy; }

int wmain(int argc, wchar_t **argv) {
    if (argc < 2) return 2;
    char *json = NULL;
    if (!read_text_file(argv[1], &json, NULL)) return 3;
    AppConfig config;
    config_defaults(&config);
    config_from_json(&config, json);
    HeapFree(GetProcessHeap(), 0, json);
    int failures = 0;
    if (wcscmp(config.window_title, L"原神") != 0) ++failures;
    if (config.region[0] != 410 || config.region[1] != 720 || config.region[2] != 1100 || config.region[3] != 950) ++failures;
    if (config.trigger_interval_ms != 2000 || config.max_chars != 35) ++failures;
    if (wcscmp(config.ai_base_url, L"https://open.bigmodel.cn/api/paas/v4") != 0) ++failures;
    if (wcscmp(config.ai_model, L"glm-4-flash") != 0 || !config.ai_key[0]) ++failures;
    if (wcscmp(config.lxmusic_path, L"D:\\LX\\lx-music-desktop\\lx-music-desktop.exe") != 0) ++failures;
    printf("%s legacy config migration\n", failures ? "FAIL" : "PASS");
    return failures;
}
