#include "../src/app.h"
#include <stdio.h>

static wchar_t last_reply[2048];
static int send_calls;
static int ai_calls;
static BOOL fake_window_available = TRUE;
static BOOL fake_ocr_available = TRUE;

static DWORD WINAPI delayed_thread(LPVOID parameter) {
    (void)parameter;
    Sleep(500);
    return 0;
}

void ui_append_log(const wchar_t *level, const wchar_t *message) { (void)level; (void)message; }
void ui_refresh_dependency_controls(void) {}
void overlay_show(App *app) { (void)app; }
void overlay_hide(App *app) { (void)app; }

HWND find_game_window(const wchar_t *needle) {
    (void)needle;
    return fake_window_available ? (HWND)(INT_PTR)1 : NULL;
}

BOOL activate_game_window(const wchar_t *title) {
    (void)title;
    return fake_window_available;
}

BOOL ocr_prepare(App *app, wchar_t *detail, int detail_chars) {
    (void)app;
    wcscpy_s(detail, detail_chars, fake_ocr_available ? L"test OCR ready" : L"test OCR missing");
    return fake_ocr_available;
}

BOOL send_text_to_foreground(const wchar_t *text) {
    wcscpy_s(last_reply, 2048, text);
    ++send_calls;
    return TRUE;
}

BOOL ai_chat(App *app, const wchar_t *user_message, wchar_t *reply, int reply_chars) {
    (void)app; (void)user_message;
    ++ai_calls;
    wcscpy_s(reply, reply_chars, L"测试回复");
    return TRUE;
}

HBITMAP capture_game_window(const wchar_t *title, const int region[4], int mode, RECT *captured_rect) {
    (void)title; (void)region; (void)mode; (void)captured_rect;
    return NULL;
}

int ocr_read_bitmap(App *app, HBITMAP bitmap, OcrTextLine *lines, int max_lines) {
    (void)app; (void)bitmap; (void)lines; (void)max_lines;
    return 0;
}

void ui_set_busy(int busy) { (void)busy; }

BOOL bot_test_send_chunks(App *app, const wchar_t *text);
void bot_test_process_line(App *app, const wchar_t *text);
void bot_test_process_capture(App *app, BOOL *baseline, const OcrTextLine *lines, int count);
BOOL bot_test_extract_command(const wchar_t *line, wchar_t *command, int command_chars);
BOOL bot_test_seen_before(App *app, const wchar_t *text);

static void expect_command(const wchar_t *input, const wchar_t *expected, int *failures) {
    wchar_t command[512] = L"";
    if (!bot_test_extract_command(input, command, 512) || wcscmp(command, expected) != 0) {
        wprintf(L"FAIL OCR command correction: %s -> %s (expected %s)\n", input, command, expected);
        ++*failures;
    } else {
        wprintf(L"PASS OCR command correction: %s\n", input);
    }
}

int main(int argc, char **argv) {
    InitializeCriticalSection(&g_app.log_lock);
    config_defaults(&g_app.config);
    g_app.running = 1;
    g_app.config.max_chars = 120;
    g_app.config.music_search_timeout = 75;
    g_app.config.music_max_queue_size = 2;
    wcscpy_s(g_app.config.lxmusic_path, MAX_PATH, L"D:\\LX\\lx-music-desktop\\lx-music-desktop.exe");
    InitializeCriticalSection(&g_app.message_queue_lock);
    g_app.message_queue_initialized = TRUE;
    g_app.message_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    bot_test_set_music_launch_result(1);
    int failures = 0;

    expect_command(L"旅行者 g点歌 Hello", L"/点歌 Hello", &failures);
    expect_command(L"旅行者 1g点歌 Hello", L"/点歌 Hello", &failures);
    expect_command(L"旅行者 1k点歌 Hello", L"/k点歌 Hello", &failures);
    expect_command(L"旅行者 1m点歌 Hello", L"/m点歌 Hello", &failures);
    expect_command(L"点歌耀斑", L"/点歌耀斑", &failures);
    expect_command(L"k点歌耀斑", L"/k点歌耀斑", &failures);
    expect_command(L"#点歌恋人", L"/点歌恋人", &failures);
    expect_command(L"#选歌1", L"/选歌 1", &failures);
    wchar_t command[512] = L"";
    if (bot_test_extract_command(L"#随便聊聊", command, 512)) {
        puts("FAIL hash non-command text");
        ++failures;
    } else puts("PASS hash non-command text");
    if (bot_test_extract_command(L"旅行者说点歌很好用", command, 512)) {
        puts("FAIL dropped slash correction scope");
        ++failures;
    } else puts("PASS dropped slash correction scope");
    if (bot_test_extract_command(L"普通文本 /not-a-command", command, 512)) {
        puts("FAIL slash command boundary");
        ++failures;
    } else puts("PASS slash command boundary");

    g_app.seen_message_count = 0;
    if (bot_test_seen_before(&g_app, L"测试  消息！") ||
        !bot_test_seen_before(&g_app, L" 测试消息! ")) {
        puts("FAIL normalized duplicate cache");
        ++failures;
    } else puts("PASS normalized duplicate cache");

    last_reply[0] = 0;
    music_handle_command(&g_app, L"/选歌");
    if (wcscmp(last_reply, L"用法：/选歌 序号") != 0) { puts("FAIL select usage"); ++failures; }
    else puts("PASS select usage");

    last_reply[0] = 0;
    music_handle_command(&g_app, L"/下一首");
    if (wcscmp(last_reply, L"播放队列为空，无法切换下一首。") != 0) { puts("FAIL skip empty"); ++failures; }
    else puts("PASS skip empty");

    last_reply[0] = 0;
    BOOL searched = music_handle_command(&g_app, L"/点歌 Hello");
    printf("search=%d results=%d reply_chars=%zu\n", searched, g_app.music_result_count, wcslen(last_reply));
    if (!searched || g_app.music_result_count <= 0) { puts("FAIL music search"); ++failures; }
    else puts("PASS music search");
    g_app.music_result_count = 0;
    BOOL kugou_searched = music_handle_command(&g_app, L"/k点歌 Hello");
    if (!kugou_searched || g_app.music_result_count <= 0 || wcscmp(g_app.music_results[0].source, L"kg") != 0) {
        puts("FAIL Kugou source mapping"); ++failures;
    } else puts("PASS Kugou source mapping");

    ULONGLONG remaining = g_app.music_results_expire - GetTickCount64();
    if (remaining < 65000 || remaining > 76000) { puts("FAIL configurable search timeout"); ++failures; }
    else puts("PASS configurable search timeout");

    int pending_results = g_app.music_result_count;
    last_reply[0] = 0;
    music_handle_command(&g_app, L"/点歌 Another");
    if (g_app.music_result_count != pending_results || !wcsstr(last_reply, L"请先 /选歌")) {
        puts("FAIL pending search protection"); ++failures;
    } else puts("PASS pending search protection");

    last_reply[0] = 0;
    music_handle_command(&g_app, L"/选歌 abc");
    if (wcscmp(last_reply, L"用法：/选歌 序号") != 0) { puts("FAIL invalid select number"); ++failures; }
    else puts("PASS invalid select number");

    if (g_app.music_result_count > 0) {
        last_reply[0] = 0;
        BOOL selected = music_handle_command(&g_app, L"/选歌 1");
        if (!selected || !g_app.music_playing) { puts("FAIL music launch"); ++failures; }
        else puts("PASS music launch");
    }
    g_app.music_playing = FALSE;
    g_app.music_queue_count = 0;
    g_app.music_result_count = 1;
    g_app.music_results_expire = GetTickCount64() + 10000;
    wcscpy_s(g_app.music_results[0].display, 280, L"失败歌曲 - 测试歌手");
    bot_test_set_music_launch_result(0);
    music_handle_command(&g_app, L"/选歌 1");
    if (g_app.music_playing || g_app.music_queue_count != 1 || !wcsstr(last_reply, L"启动 LxMusic 失败")) {
        puts("FAIL music launch failure recovery"); ++failures;
    } else puts("PASS music launch failure recovery");
    bot_test_set_music_launch_result(1);
    g_app.music_queue_count = 0;
    music_handle_command(&g_app, L"/下一首");
    if (!wcsstr(last_reply, L"队列为空") || bot_test_music_launch_calls() != 0) {
        puts("FAIL music empty next handling"); ++failures;
    } else puts("PASS music empty next handling");

    g_app.music_queue_count = 2;
    wcscpy_s(g_app.music_queue[0].name, 128, L"第一首");
    wcscpy_s(g_app.music_queue[1].name, 128, L"第二首");
    music_handle_command(&g_app, L"/插队 2");
    if (wcscmp(g_app.music_queue[0].name, L"第二首") != 0) { puts("FAIL music move front"); ++failures; }
    else puts("PASS music move front");

    g_app.config.music_blacklist_count = 0;
    music_handle_command(&g_app, L"/音乐黑名单 测试禁歌");
    music_handle_command(&g_app, L"/音乐黑名单 测试禁歌");
    if (g_app.config.music_blacklist_count != 1) { puts("FAIL duplicate music blacklist"); ++failures; }
    else puts("PASS duplicate music blacklist");
    music_handle_command(&g_app, L"/移除黑名单 测试禁歌");
    if (g_app.config.music_blacklist_count != 0) { puts("FAIL music remove blacklist"); ++failures; }
    else puts("PASS music remove blacklist");

    last_reply[0] = 0;
    music_handle_command(&g_app, L"/取消当前搜索");
    if (wcscmp(last_reply, L"当前没有待选择歌曲。") != 0) { puts("FAIL empty search cancel"); ++failures; }
    else puts("PASS empty search cancel");

    g_app.config.music_enabled = FALSE;
    int disabled_calls = send_calls;
    if (music_handle_command(&g_app, L"/排队列表") || send_calls != disabled_calls) { puts("FAIL disabled music plugin"); ++failures; }
    else puts("PASS disabled music plugin");
    g_app.config.music_enabled = TRUE;

    memset(g_app.seen_messages, 0, sizeof(g_app.seen_messages));
    memset(g_app.seen_message_ticks, 0, sizeof(g_app.seen_message_ticks));
    memset(g_app.sent_fragments, 0, sizeof(g_app.sent_fragments));
    g_app.seen_message_count = g_app.sent_fragment_count = 0;
    send_calls = ai_calls = 0;
    wcscpy_s(g_app.config.wake_word, 64, L"测试唤醒");
    bot_test_send_chunks(&g_app, L"abcdefghijklmno");
    if (!bot_test_queue_line(&g_app, L"测试唤醒 xyzabcdefghij")) {
        puts("FAIL short normal message mistaken for sent echo"); ++failures;
    } else puts("PASS short normal message is not mistaken for sent echo");
    bot_test_drain_queue(&g_app);

    memset(g_app.seen_messages, 0, sizeof(g_app.seen_messages));
    memset(g_app.seen_message_ticks, 0, sizeof(g_app.seen_message_ticks));
    memset(g_app.sent_fragments, 0, sizeof(g_app.sent_fragments));
    g_app.seen_message_count = g_app.sent_fragment_count = 0;
    wcscpy_s(g_app.config.wake_word, 64, L"测试唤醒");
    send_calls = ai_calls = 0;
    bot_test_queue_line(&g_app, L"测试唤醒 你好");
    bot_test_queue_line(&g_app, L"测试唤醒 你好");
    bot_test_drain_queue(&g_app);
    if (ai_calls != 1 || send_calls != 1) { puts("FAIL duplicate suppression"); ++failures; }
    else puts("PASS duplicate suppression");

    memset(g_app.seen_messages, 0, sizeof(g_app.seen_messages));
    memset(g_app.seen_message_ticks, 0, sizeof(g_app.seen_message_ticks));
    g_app.seen_message_count = 0;
    send_calls = ai_calls = 0;
    BOOL baseline = FALSE;
    OcrTextLine first_line = {0};
    wcscpy_s(first_line.text, 256, L"测试唤醒 第一条新消息");
    bot_test_process_capture(&g_app, &baseline, NULL, 0);
    bot_test_process_capture(&g_app, &baseline, &first_line, 1);
    bot_test_process_capture(&g_app, &baseline, &first_line, 1);
    bot_test_drain_queue(&g_app);
    if (!baseline || ai_calls != 1 || send_calls != 1) { puts("FAIL first message after empty baseline"); ++failures; }
    else puts("PASS first message after empty baseline");

    BOOL selection_baseline = FALSE;
    OcrTextLine selection_line = {0};
    wcscpy_s(selection_line.text, 256, L"/选歌1");
    bot_test_process_capture(&g_app, &selection_baseline, NULL, 0);
    bot_test_process_capture(&g_app, &selection_baseline, &selection_line, 1);
    if (bot_test_queue_count(&g_app) == 0) { puts("FAIL immediate music selection OCR"); ++failures; }
    else puts("PASS immediate music selection OCR");
    bot_test_drain_queue(&g_app);

    g_app.config.bot_blacklist_count = 1;
    wcscpy_s(g_app.config.bot_blacklist[0], 128, L"禁止");
    int before_block = send_calls;
    if (bot_test_send_chunks(&g_app, L"禁止发送") || send_calls != before_block) { puts("FAIL bot blacklist"); ++failures; }
    else puts("PASS bot blacklist");

    g_app.config.bot_blacklist_count = 0;
    g_app.last_send_tick = 0;
    BOOL first_send = bot_test_send_chunks(&g_app, L"第一条");
    BOOL second_send = bot_test_send_chunks(&g_app, L"第二条");
    if (!first_send || !second_send) { puts("FAIL speaking interval only"); ++failures; }
    else puts("PASS speaking interval only");

    memset(g_app.seen_messages, 0, sizeof(g_app.seen_messages));
    memset(g_app.seen_message_ticks, 0, sizeof(g_app.seen_message_ticks));
    g_app.seen_message_count = 0;
    last_reply[0] = 0;
    bot_test_process_line(&g_app, L"旅行者 ／plugins");
    if (!wcsstr(last_reply, L"music_player")) { puts("FAIL embedded command extraction"); ++failures; }
    else puts("PASS embedded command extraction");

    g_app.history_count = 1;
    wcscpy_s(g_app.history_user[0], 512, L"旧问题");
    wcscpy_s(g_app.history_assistant[0], 512, L"旧回答");
    bot_test_process_line(&g_app, L"/reset");
    bot_test_process_line(&g_app, L"/reset_confirm");
    if (g_app.history_count != 0 || wcscmp(last_reply, L"记忆已清空。") != 0) { puts("FAIL reset confirmation"); ++failures; }
    else puts("PASS reset confirmation");

    bot_test_process_line(&g_app, L"/persona 测试新人设");
    if (wcscmp(g_app.config.personality, L"测试新人设") != 0 || wcscmp(last_reply, L"人设已更新。") != 0) { puts("FAIL persona command"); ++failures; }
    else puts("PASS persona command");

    bot_test_process_line(&g_app, L"/stop");
    bot_test_process_line(&g_app, L"/cancel");
    if (g_app.pending_stop || wcscmp(last_reply, L"已取消操作。") != 0) { puts("FAIL cancel stop"); ++failures; }
    else puts("PASS cancel stop");

    g_app.bot_started_tick = GetTickCount64() - 65000;
    g_app.config.max_chars = 120;
    bot_test_process_line(&g_app, L"/status");
    if (!wcsstr(last_reply, L"运行 0:01:05") || !wcsstr(last_reply, L"模型")) { puts("FAIL status command"); ++failures; }
    else puts("PASS status command");

    last_reply[0] = 0;
    int ai_before_system_line = ai_calls;
    bot_test_process_line(&g_app, L"-23:09-");
    if (ai_calls != ai_before_system_line) { puts("FAIL system time filter"); ++failures; }
    else puts("PASS system time filter");

    g_app.bot_thread = CreateThread(NULL, 0, delayed_thread, NULL, 0, NULL);
    ULONGLONG stop_started = GetTickCount64();
    bot_stop(&g_app);
    ULONGLONG stop_elapsed = GetTickCount64() - stop_started;
    if (stop_elapsed > 100 || !bot_thread_active(&g_app)) { puts("FAIL nonblocking stop"); ++failures; }
    else puts("PASS nonblocking stop");
    WaitForSingleObject(g_app.bot_thread, 2000);
    bot_reap(&g_app);
    if (g_app.bot_thread) { puts("FAIL stopped thread reap"); ++failures; }
    else puts("PASS stopped thread reap");

    fake_window_available = FALSE;
    if (bot_start(&g_app)) { puts("FAIL start without game window"); ++failures; }
    else puts("PASS start without game window blocked");
    fake_window_available = TRUE;
    fake_ocr_available = FALSE;
    if (!bot_start(&g_app)) { puts("FAIL OCR preflight thread start"); ++failures; }
    else {
        for (int i = 0; i < 100 && InterlockedCompareExchange(&g_app.running, 0, 0); ++i) Sleep(10);
        bot_reap(&g_app);
        if (g_app.running || g_app.bot_thread) { puts("FAIL missing OCR startup shutdown"); ++failures; }
        else puts("PASS missing OCR startup shutdown");
    }
    if (argc > 1 && strcmp(argv[1], "--real-launch") == 0) {
        bot_test_set_music_launch_result(-1);
        g_app.running = 1;
        g_app.music_playing = FALSE;
        g_app.music_queue_count = 0;
        g_app.music_result_count = 1;
        g_app.music_results_expire = GetTickCount64() + 10000;
        wcscpy_s(g_app.music_results[0].name, 128, L"耀斑");
        wcscpy_s(g_app.music_results[0].singer, 128, L"测试");
        wcscpy_s(g_app.music_results[0].display, 280, L"耀斑 - 测试");
        wcscpy_s(g_app.music_results[0].source, 8, L"wy");
        g_app.music_results[0].duration_seconds = 180;
        if (!music_handle_command(&g_app, L"/选歌 1") || !g_app.music_playing) {
            puts("FAIL real LxMusic launch");
            ++failures;
        } else puts("PASS real LxMusic launch");
    }
    CloseHandle(g_app.message_event);
    DeleteCriticalSection(&g_app.message_queue_lock);
    DeleteCriticalSection(&g_app.log_lock);
    return failures;
}
