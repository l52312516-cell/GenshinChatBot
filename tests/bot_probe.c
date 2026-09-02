#include "../src/app.h"
#include <stdio.h>

void bot_test_process_line(App *app, const wchar_t *text);
void bot_test_process_capture(App *app, BOOL *baseline, const OcrTextLine *lines, int count);
BOOL bot_test_send_chunks(App *app, const wchar_t *text);
void bot_test_set_music_launch_result(int result);

static int failures;
static int send_calls;
static int ai_calls;
static wchar_t last_reply[2048];

void ui_append_log(const wchar_t *level, const wchar_t *message) { (void)level; (void)message; }
void ui_refresh_dependency_controls(void) {}
void ui_set_busy(int busy) { (void)busy; }
void overlay_show(App *app) { (void)app; }
void overlay_hide(App *app) { (void)app; }
HWND find_game_window(const wchar_t *needle) { (void)needle; return (HWND)(INT_PTR)1; }
BOOL activate_game_window(const wchar_t *title) { (void)title; return TRUE; }
BOOL ocr_prepare(App *app, wchar_t *detail, int detail_chars) {
    (void)app;
    wcscpy_s(detail, detail_chars, L"test OCR ready");
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

static void expect(BOOL condition, const char *name) {
    printf("%s: %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) ++failures;
}

int main(void) {
    InitializeCriticalSection(&g_app.log_lock);
    config_defaults(&g_app.config);
    g_app.running = 1;
    g_app.config.send_interval_ms = 600;
    g_app.config.max_chars = 120;
    wcscpy_s(g_app.config.lxmusic_path, MAX_PATH, L"D:\\LX\\lx-music-desktop\\lx-music-desktop.exe");

    wchar_t command[512] = L"";
    expect(!bot_test_extract_command(L"普通文本 /not-a-command", command, 512), "ignore embedded slash text");
    expect(bot_test_extract_command(L"旅行者 ／plugins", command, 512) && wcscmp(command, L"/plugins") == 0,
           "extract full-width command");
    expect(bot_test_extract_command(L"/选歌 1", command, 512) && wcscmp(command, L"/选歌 1") == 0,
           "extract spaced music selection command");
    expect(bot_test_extract_command(L"/选歌1", command, 512) && wcscmp(command, L"/选歌 1") == 0,
           "extract compact music selection command");
    expect(bot_test_extract_command(L"/点歌耀斑", command, 512) && wcscmp(command, L"/点歌耀斑") == 0,
           "extract compact music search command");

    expect(!bot_test_seen_before(&g_app, L"测试  消息！") && bot_test_seen_before(&g_app, L" 测试消息! "),
           "normalize duplicate message key");

    InitializeCriticalSection(&g_app.message_queue_lock);
    g_app.message_queue_initialized = TRUE;
    g_app.message_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_app.message_queue_head = g_app.message_queue_tail = g_app.message_queue_count = 0;
    g_app.seen_message_count = 0;
    BOOL baseline = FALSE;
    OcrTextLine stable_line = {0};
    wcscpy_s(stable_line.text, 256, L"派蒙萌萌萌 稳定消息");
    bot_test_process_capture(&g_app, &baseline, NULL, 0);
    bot_test_process_capture(&g_app, &baseline, &stable_line, 1);
    expect(bot_test_queue_count(&g_app) == 1, "queue new OCR text on first visible frame");
    bot_test_process_capture(&g_app, &baseline, &stable_line, 1);
    expect(bot_test_queue_count(&g_app) == 1, "deduplicate repeated OCR frame");
    bot_test_drain_queue(&g_app);

    memset(g_app.seen_messages, 0, sizeof(g_app.seen_messages));
    memset(g_app.seen_message_ticks, 0, sizeof(g_app.seen_message_ticks));
    g_app.seen_message_count = 0;
    g_app.message_queue_head = g_app.message_queue_tail = g_app.message_queue_count = 0;
    BOOL duplicate_baseline = FALSE;
    OcrTextLine duplicate_lines[2] = {0};
    wcscpy_s(duplicate_lines[0].text, 256, L"/选歌1");
    bot_test_process_capture(&g_app, &duplicate_baseline, NULL, 0);
    bot_test_process_capture(&g_app, &duplicate_baseline, duplicate_lines, 1);
    expect(bot_test_queue_count(&g_app) == 1, "queue first command bubble");
    g_app.message_queue_head = g_app.message_queue_tail = g_app.message_queue_count = 0;
    bot_test_process_capture(&g_app, &duplicate_baseline, duplicate_lines, 1);
    expect(bot_test_queue_count(&g_app) == 0, "ignore persistent command bubble");
    wcscpy_s(duplicate_lines[1].text, 256, L"/选歌1");
    bot_test_process_capture(&g_app, &duplicate_baseline, duplicate_lines, 2);
    expect(bot_test_queue_count(&g_app) == 1, "process repeated command bubble while old one remains");
    g_app.message_queue_head = g_app.message_queue_tail = g_app.message_queue_count = 0;

    memset(g_app.seen_messages, 0, sizeof(g_app.seen_messages));
    memset(g_app.seen_message_ticks, 0, sizeof(g_app.seen_message_ticks));
    g_app.seen_message_count = 0;
    BOOL growing_baseline = FALSE;
    OcrTextLine growing_lines[2] = {0};
    wcscpy_s(growing_lines[0].text, 256, L"派蒙萌萌萌 旧消息");
    wcscpy_s(growing_lines[1].text, 256, L"派蒙萌萌萌 新消息");
    bot_test_process_capture(&g_app, &growing_baseline, growing_lines, 1);
    bot_test_process_capture(&g_app, &growing_baseline, growing_lines, 2);
    expect(bot_test_queue_count(&g_app) == 1, "queue only newly added OCR line");
    bot_test_process_capture(&g_app, &growing_baseline, growing_lines, 2);
    expect(bot_test_queue_count(&g_app) == 1, "do not repeat unchanged OCR lines");
    bot_test_drain_queue(&g_app);

    memset(g_app.seen_messages, 0, sizeof(g_app.seen_messages));
    memset(g_app.seen_message_ticks, 0, sizeof(g_app.seen_message_ticks));
    memset(g_app.sent_fragments, 0, sizeof(g_app.sent_fragments));
    g_app.seen_message_count = g_app.sent_fragment_count = 0;
    g_app.last_send_tick = 0;
    bot_test_send_chunks(&g_app, L"abcdefghijklmno");
    expect(bot_test_queue_line(&g_app, L"派蒙萌萌萌 xyzabcdefghij"),
           "do not suppress normal text containing a sent substring");
    expect(!bot_test_queue_line(&g_app, L"abcdefghijklmno"), "ignore exact sent echo");
    bot_test_drain_queue(&g_app);

    memset(g_app.seen_messages, 0, sizeof(g_app.seen_messages));
    memset(g_app.seen_message_ticks, 0, sizeof(g_app.seen_message_ticks));
    g_app.seen_message_count = 0;
    send_calls = ai_calls = 0;
    expect(bot_test_queue_line(&g_app, L"派蒙萌萌萌 你好"), "queue new chat line");
    expect(bot_test_queue_count(&g_app) == 1 && ai_calls == 0 && send_calls == 0,
           "queue keeps OCR thread nonblocking");
    bot_test_drain_queue(&g_app);
    expect(bot_test_queue_count(&g_app) == 0 && ai_calls == 1 && send_calls == 1,
           "message worker processes queued chat");
    expect(bot_test_seen_before(&g_app, L"派蒙萌萌萌 你好"), "cache records queued message");

    last_reply[0] = 0;
    bot_test_process_line(&g_app, L"/unknown");
    expect(wcscmp(last_reply, L"未知指令，可用 /help 查看帮助。") == 0, "unknown command fallback");
    int ai_before_sent_command = ai_calls;
    bot_test_send_chunks(&g_app, L"/help");
    bot_test_process_line(&g_app, L"/help");
    expect(ai_calls == ai_before_sent_command, "ignore bot command echoed by OCR");

    bot_test_set_music_launch_result(1);
    g_app.music_result_count = 0;
    g_app.last_send_tick = 0;
    last_reply[0] = 0;
    expect(music_handle_command(&g_app, L"/点歌 威風堂堂") &&
           wcsstr(last_reply, L"禁止搜索") != NULL, "built-in music blacklist blocks search");
    const wchar_t *built_in_blocked[] = {
        L"北京混子", L"不想上学", L"自杀日记", L"摇头玩", L"我的梦中情人"
    };
    for (size_t i = 0; i < sizeof(built_in_blocked) / sizeof(built_in_blocked[0]); ++i) {
        wchar_t command[160];
        _snwprintf_s(command, _countof(command), _TRUNCATE, L"/点歌 %s", built_in_blocked[i]);
        g_app.last_send_tick = 0;
        last_reply[0] = 0;
        expect(music_handle_command(&g_app, command) &&
               wcsstr(last_reply, L"禁止搜索") != NULL, "all confirmed built-in songs are blocked");
    }
    g_app.config.music_blacklist_count = 1;
    wcscpy_s(g_app.config.music_blacklist[0], 128, L"bad song");
    g_app.last_send_tick = 0;
    last_reply[0] = 0;
    expect(music_handle_command(&g_app, L"/点歌 BAD---SONG") &&
           wcsstr(last_reply, L"禁止搜索") != NULL, "normalized music blacklist blocks search");
    g_app.config.music_blacklist_count = 0;
    g_app.music_result_count = 1;
    g_app.music_results_expire = GetTickCount64() + 10000;
    wcscpy_s(g_app.music_results[0].name, 128, L"测试歌曲");
    wcscpy_s(g_app.music_results[0].singer, 128, L"测试歌手");
    wcscpy_s(g_app.music_results[0].display, 280, L"测试歌曲 - 测试歌手");
    wcscpy_s(g_app.music_results[0].source, 8, L"wy");
    g_app.music_results[0].duration_seconds = 180;
    g_app.music_queue_count = 0;
    g_app.music_playing = FALSE;
    g_app.last_send_tick = 0;
    expect(music_handle_command(&g_app, L"/选歌 1") && g_app.music_playing, "music selection starts playback");
    g_app.music_playing = FALSE;
    g_app.music_queue_count = 0;
    g_app.music_result_count = 1;
    g_app.music_results_expire = GetTickCount64() + 10000;
    expect(music_handle_command(&g_app, L"/选歌1") && g_app.music_playing, "OCR compact select command starts playback");
    g_app.last_send_tick = 0;
    expect(music_handle_command(&g_app, L"/下一首") && !g_app.music_playing &&
           wcscmp(last_reply, L"已停止当前歌曲，播放队列为空。") == 0, "next handles empty queue");

    CloseHandle(g_app.message_event);
    DeleteCriticalSection(&g_app.message_queue_lock);
    DeleteCriticalSection(&g_app.log_lock);
    return failures;
}
