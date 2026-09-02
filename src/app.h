#ifndef CHATGIBOT_NATIVE_H
#define CHATGIBOT_NATIVE_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

#define APP_NAME L"ChatGIBot"
#define APP_CLASS L"ChatGIBotNativeWindow"
#define WM_APP_LOG (WM_APP + 1)
#define WM_APP_STATUS (WM_APP + 2)
#define WM_APP_OCR (WM_APP + 3)
#define WM_APP_REGION (WM_APP + 4)

#define CHATGIBOT_MAX_PLUGINS 32
#define CHATGIBOT_MAX_PLUGIN_COMMANDS 128
#define CHATGIBOT_MAX_MESSAGE_QUEUE 64

typedef enum {
    BOT_MESSAGE_COMMAND,
    BOT_MESSAGE_CHAT
} BotMessageType;

typedef struct {
    BotMessageType type;
    wchar_t text[512];
} BotMessage;

typedef enum {
    DEP_OK = 0,
    DEP_WARN,
    DEP_MISSING,
    DEP_CHECKING,
    DEP_INSTALLING
} DepState;

typedef struct {
    DepState state;
    wchar_t title[64];
    wchar_t detail[512];
    wchar_t checked_at[32];
} DependencyItem;

typedef struct {
    wchar_t text[256];
    int left;
    int top;
    int right;
    int bottom;
    float confidence;
    wchar_t source[24];
} OcrTextLine;

typedef struct {
    wchar_t name[128];
    wchar_t singer[128];
    wchar_t display[280];
    wchar_t source[8];
    int duration_seconds;
} MusicItem;

typedef struct {
    wchar_t key[256];
    int count;
} FrameLineCount;

typedef struct AppConfig {
    wchar_t window_title[128];
    wchar_t ai_base_url[256];
    wchar_t ai_model[128];
    wchar_t ai_key_env[128];
    wchar_t ai_key[256];
    wchar_t wake_word[64];
    wchar_t vision_base_url[256];
    wchar_t vision_model[128];
    wchar_t vision_key_env[128];
    wchar_t vision_key[256];
    wchar_t vision_prompt[1024];
    wchar_t vision_engine[32];
    wchar_t ocr_version[32];
    wchar_t model_tier[32];
    wchar_t screenshot_mode[32];
    wchar_t inference_device[32];
    wchar_t download_source[32];
    wchar_t lxmusic_path[MAX_PATH];
    int region[4];
    int trigger_interval_ms;
    int ocr_interval_ms;
    int ocr_threads;
    int send_interval_ms;
    BOOL log_overlay_enabled;
    int max_chars;
    int history_turns;
    double temperature;
    int max_tokens;
    int score_threshold_percent;
    int music_search_timeout;
    int music_max_queue_size;
    BOOL music_enabled;
    wchar_t bot_blacklist[32][128];
    int bot_blacklist_count;
    wchar_t music_blacklist[32][128];
    int music_blacklist_count;
    wchar_t enabled_plugins[CHATGIBOT_MAX_PLUGINS][64];
    int enabled_plugin_count;
    wchar_t personality[1024];
} AppConfig;

typedef struct App {
    HWND hwnd;
    HWND overlay_hwnd;
    volatile LONG overlay_shutdown;
    HINSTANCE instance;
    AppConfig config;
    DependencyItem deps[8];
    int dep_count;
    wchar_t log_text[65536];
    CRITICAL_SECTION log_lock;
    LONG log_revision;
    HBITMAP preview_bitmap;
    BOOL region_selecting;
    BOOL region_dragging;
    POINT region_drag_start;
    POINT region_drag_current;
    int preview_source_width;
    int preview_source_height;
    wchar_t preview_info[512];
    wchar_t ocr_preview[8192];
    volatile LONG running;
    volatile LONG paused;
    volatile LONG busy;
    volatile LONG io_busy;
    volatile LONG cancel_requested;
    volatile LONG download_active;
    volatile LONG download_progress;
    HANDLE bot_thread;
    HANDLE message_thread;
    HANDLE task_thread;
    HANDLE message_event;
    CRITICAL_SECTION message_queue_lock;
    BOOL message_queue_initialized;
    BotMessage message_queue[CHATGIBOT_MAX_MESSAGE_QUEUE];
    int message_queue_head;
    int message_queue_tail;
    int message_queue_count;
    BOOL plugins_initialized;
    DWORD ui_thread_id;
    wchar_t history_user[8][512];
    wchar_t history_assistant[8][512];
    int history_count;
    wchar_t seen_messages[1024][256];
    ULONGLONG seen_message_ticks[1024];
    int seen_message_count;
    wchar_t sent_fragments[64][256];
    ULONGLONG sent_fragment_ticks[64];
    int sent_fragment_count;
    CRITICAL_SECTION sent_cache_lock;
    BOOL sent_cache_initialized;
    FrameLineCount previous_frame_lines[32];
    int previous_frame_line_count;
    OcrTextLine pending_ocr_lines[32];
    int pending_ocr_count;
    int pending_ocr_stable_count;
    ULONGLONG last_send_tick;
    ULONGLONG bot_started_tick;
    ULONGLONG pending_expires_tick;
    int processed_message_count;
    BOOL pending_reset;
    BOOL pending_stop;
    MusicItem music_results[4];
    int music_result_count;
    ULONGLONG music_results_expire;
    MusicItem music_queue[200];
    int music_queue_count;
    MusicItem music_current;
    BOOL music_playing;
    ULONGLONG music_playback_tick;
} App;

enum {
    PAGE_CONSOLE, PAGE_VISION, PAGE_AI, PAGE_ROBOT, PAGE_DEPS, PAGE_PLUGIN, PAGE_ABOUT, PAGE_COUNT
};
enum {
    IDC_NAV_BASE = 100,
    IDC_CONSOLE_LOG = 200, IDC_BTN_START, IDC_BTN_STOP, IDC_BTN_PAUSE, IDC_BTN_CAPTURE, IDC_BTN_OCR, IDC_BTN_CLEAR,
    IDC_VISION_PAGE = 300, IDC_CAPTURE_MODE, IDC_DEVICE_MODE, IDC_ENGINE_MODE, IDC_MODEL_TIER,
    IDC_REGION_X, IDC_REGION_Y, IDC_REGION_W, IDC_REGION_H, IDC_THRESHOLD, IDC_OCR_INTERVAL, IDC_OCR_THREADS, IDC_VISION_PROMPT, IDC_OCR_PREVIEW,
    IDC_BTN_TEST_CAPTURE, IDC_BTN_TEST_OCR, IDC_BTN_SELECT_REGION, IDC_BTN_SAVE_VISION,
    IDC_AI_PAGE = 400, IDC_AI_PROVIDER, IDC_AI_BASE, IDC_AI_MODEL, IDC_AI_KEY_ENV, IDC_AI_KEY,
    IDC_AI_TEMPERATURE, IDC_AI_MAX_TOKENS, IDC_AI_PERSONALITY,
    IDC_VISION_AI_BASE, IDC_VISION_AI_MODEL, IDC_VISION_AI_KEY_ENV, IDC_VISION_AI_KEY,
    IDC_BTN_AI_TEST, IDC_BTN_SAVE_AI,
    IDC_ROBOT_PAGE = 500, IDC_GAME_TITLE, IDC_TRIGGER_MS, IDC_SEND_INTERVAL, IDC_MAX_CHARS,
    IDC_WAKE_WORD, IDC_HISTORY_TURNS, IDC_OVERLAY_ENABLED, IDC_BTN_SAVE_ROBOT,
    IDC_DEPS_PAGE = 600, IDC_DEP_STATUS, IDC_DEP_PROGRESS, IDC_BTN_DEP_REFRESH, IDC_BTN_DEP_INSTALL,
    IDC_BTN_DEP_MODELS, IDC_BTN_DEP_CANCEL, IDC_BTN_DEP_LOG, IDC_DOWNLOAD_SOURCE, IDC_BTN_TEST_MIRRORS,
    IDC_PLUGIN_PAGE = 700, IDC_LXMUSIC_PATH, IDC_PLUGIN_ENABLED, IDC_MUSIC_SEARCH_TIMEOUT,
    IDC_MUSIC_QUEUE_LIMIT, IDC_BTN_PLUGIN_BROWSE, IDC_BTN_SAVE_PLUGIN,
    IDC_ABOUT_PAGE = 800
};

extern App g_app;

BOOL utf8_to_wide(const char *input, wchar_t *output, int output_chars);
BOOL wide_to_utf8(const wchar_t *input, char *output, int output_bytes);
void app_log_format(const wchar_t *level, const wchar_t *format, ...);
void get_exe_dir(wchar_t *path, DWORD size);
void get_data_dir(wchar_t *path, DWORD size);
void get_runtime_dir(wchar_t *path, DWORD size);
void get_runtime_library_path(const AppConfig *config, wchar_t *path, DWORD size);
void get_model_dir(wchar_t *path, DWORD size);
BOOL ensure_directory(const wchar_t *path);
BOOL read_text_file(const wchar_t *path, char **data, DWORD *size);
BOOL write_text_file_atomic(const wchar_t *path, const char *data);
void sha256_file(const wchar_t *path, wchar_t *output, int output_chars);
void local_time_text(wchar_t *output, int chars);
int system_logical_processor_count(void);
BOOL ocr_line_is_system(const wchar_t *text);
void ocr_clean_text(wchar_t *text, int text_chars);
void game_clean_text(wchar_t *text, int text_chars);

void config_defaults(AppConfig *config);
void config_normalize(AppConfig *config);
void config_from_json(AppConfig *config, const char *json);
BOOL config_load(AppConfig *config);
BOOL config_save(const AppConfig *config);

void dependency_init(App *app);
void dependency_detect_async(App *app);
BOOL dependency_install_runtime_async(App *app);
BOOL dependency_repair_models_async(App *app);
BOOL dependency_test_mirrors_async(App *app);
void dependency_cancel(App *app);
const DependencyItem *dependency_by_title(App *app, const wchar_t *title);

BOOL capture_test_async(App *app);
BOOL capture_select_region_async(App *app);
HBITMAP capture_game_window(const wchar_t *title, const int region[4], int mode, RECT *captured_rect);
HBITMAP capture_game_window_for_test(const wchar_t *title, const int region[4], int mode, HWND restore_window, RECT *captured_rect);
HWND find_game_window(const wchar_t *needle);
BOOL activate_game_window(const wchar_t *title);

BOOL ai_test_async(App *app);
BOOL ai_chat(App *app, const wchar_t *user_message, wchar_t *reply, int reply_chars);
void ai_clean_reply(wchar_t *text, int text_chars);
BOOL send_chat_input_sequence(HWND game);
BOOL ocr_test_async(App *app);
BOOL ocr_prepare(App *app, wchar_t *detail, int detail_chars);
int ocr_read_bitmap(App *app, HBITMAP bitmap, OcrTextLine *lines, int max_lines);
int vision_read_bitmap(App *app, HBITMAP bitmap, OcrTextLine *lines, int max_lines);
BOOL send_text_to_foreground(const wchar_t *text);
BOOL bot_send_message(App *app, const wchar_t *text);
void open_log_file(void);
BOOL overlay_init(App *app);
void overlay_show(App *app);
void overlay_hide(App *app);
void overlay_destroy(App *app);
void ui_refresh_region_selection(void);
BOOL ui_checkbox_checked(int id);
void ui_checkbox_set(int id, BOOL checked);
void gdiplus_init(void);
void gdiplus_shutdown(void);
void ui_fill_round_rect(HDC hdc, RECT r, int radius, COLORREF top, COLORREF bottom, BOOL gradient, BOOL border, COLORREF border_col);
void ui_fill_ellipse(HDC hdc, RECT r, COLORREF color);
LRESULT CALLBACK od_button_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR uidsub, DWORD_PTR ref);

BOOL bot_start(App *app);
void bot_pause(App *app);
void bot_stop(App *app);
BOOL bot_thread_active(App *app);
void bot_reap(App *app);
void background_reap(App *app);
BOOL background_task_active(App *app);
BOOL music_handle_command(App *app, const wchar_t *text);
BOOL plugin_manager_init(App *app);
void plugin_manager_shutdown(App *app);
BOOL plugin_manager_dispatch(App *app, const wchar_t *command, const wchar_t *args);
void plugin_manager_list(App *app, wchar_t *output, int output_chars);
void plugin_manager_help(App *app, wchar_t *output, int output_chars);
#ifdef CHATGIBOT_TESTING
BOOL bot_test_extract_command(const wchar_t *line, wchar_t *command, int command_chars);
void bot_test_set_music_launch_result(int result);
int bot_test_music_launch_calls(void);
BOOL bot_test_seen_before(App *app, const wchar_t *text);
BOOL bot_test_queue_line(App *app, const wchar_t *text);
int bot_test_queue_count(App *app);
void bot_test_drain_queue(App *app);
#endif

extern void ui_refresh_dependency_controls(void);
extern void ui_refresh_ocr_preview(void);
extern void ui_append_log(const wchar_t *level, const wchar_t *message);
extern void ui_set_busy(int busy);
extern void ui_layout(HWND hwnd);
extern HWND g_pages[PAGE_COUNT];
extern HWND g_console_log;
extern int g_current_page;
extern const wchar_t *NAV_LABELS[PAGE_COUNT];
extern HBRUSH g_background_brush, g_card_brush, g_input_brush;
extern COLORREF g_background, g_card, g_input, g_accent, g_text, g_muted;
extern COLORREF g_accent_hi, g_accent_dark, g_success, g_warning, g_error;
extern HFONT g_font, g_title_font, g_mono_font, g_icon_font;
extern HWND g_nav_buttons[PAGE_COUNT];
extern HWND g_region_preview;

#endif
