#include <stdio.h>
#include <windows.h>
#include "../src/app.h"

static INPUT captured[16];
static UINT captured_count;
static HWND expected_window = (HWND)(INT_PTR)0x1234;

HWND WINAPI test_get_foreground_window(void) { return expected_window; }

UINT WINAPI test_send_input(UINT count, LPINPUT inputs, int input_size) {
    (void)input_size;
    if (captured_count + count > 16) return 0;
    memcpy(captured + captured_count, inputs, count * sizeof(INPUT));
    captured_count += count;
    return count;
}

void WINAPI test_sleep(DWORD milliseconds) { (void)milliseconds; }

void ui_append_log(const wchar_t *level, const wchar_t *message) { (void)level; (void)message; }
void ui_refresh_dependency_controls(void) {}
void ui_set_busy(int busy) { (void)busy; }
HWND find_game_window(const wchar_t *needle) { (void)needle; return expected_window; }

int main(void) {
    BOOL ok = send_chat_input_sequence(expected_window);
    WORD expected_keys[] = { VK_RETURN, VK_RETURN, VK_CONTROL, 'V', 'V', VK_CONTROL, VK_RETURN, VK_RETURN };
    DWORD expected_flags[] = { 0, KEYEVENTF_KEYUP, 0, 0, KEYEVENTF_KEYUP, KEYEVENTF_KEYUP, 0, KEYEVENTF_KEYUP };
    if (!ok || captured_count != 8) {
        printf("FAIL sequence length=%u\n", captured_count);
        return 1;
    }
    for (UINT i = 0; i < captured_count; ++i) {
        if (captured[i].type != INPUT_KEYBOARD || captured[i].ki.wVk != expected_keys[i] ||
            captured[i].ki.dwFlags != expected_flags[i]) {
            printf("FAIL event %u key=%u flags=%lu\n", i, captured[i].ki.wVk, captured[i].ki.dwFlags);
            return 2;
        }
    }
    puts("PASS Enter-Ctrl+V-Enter sequence");
    return 0;
}
