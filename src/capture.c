#include "app.h"
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <shlwapi.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "dxgi.lib")

struct FindContext {
    const wchar_t *needle;
    HWND result;
};

static BOOL title_contains(HWND hwnd, const wchar_t *needle) {
    wchar_t title[256] = L"";
    return IsWindowVisible(hwnd) && GetWindowTextW(hwnd, title, 256) && StrStrIW(title, needle) != NULL;
}

static BOOL CALLBACK find_window_proc(HWND hwnd, LPARAM parameter) {
    struct FindContext *context = (struct FindContext*)parameter;
    if (title_contains(hwnd, context->needle)) {
        context->result = hwnd;
        return FALSE;
    }
    return TRUE;
}

HWND find_game_window(const wchar_t *needle) {
    struct FindContext context = { needle, NULL };
    EnumWindows(find_window_proc, (LPARAM)&context);
    return context.result;
}

static BOOL window_bounds(HWND hwnd, RECT *rect) {
    if (DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, rect, sizeof(RECT)) == S_OK) return TRUE;
    return GetWindowRect(hwnd, rect) != FALSE;
}

static BOOL bitmap_has_visible_content(HBITMAP bitmap) {
    BITMAP object;
    if (!bitmap || !GetObjectW(bitmap, sizeof(object), &object)) return FALSE;
    int width = object.bmWidth, height = abs(object.bmHeight);
    if (width <= 0 || height <= 0) return FALSE;
    SIZE_T bytes = (SIZE_T)width * height * 4;
    BYTE *pixels = (BYTE*)HeapAlloc(GetProcessHeap(), 0, bytes);
    if (!pixels) return TRUE;
    BITMAPINFO info = {0};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC dc = GetDC(NULL);
    BOOL copied = dc && GetDIBits(dc, bitmap, 0, (UINT)height, pixels, &info, DIB_RGB_COLORS) != 0;
    if (dc) ReleaseDC(NULL, dc);
    if (!copied) { HeapFree(GetProcessHeap(), 0, pixels); return TRUE; }
    int samples = 0, visible = 0;
    for (int y = 0; y < height; y += 4) {
        for (int x = 0; x < width; x += 4) {
            const BYTE *pixel = pixels + ((SIZE_T)y * width + x) * 4;
            if (max(pixel[0], max(pixel[1], pixel[2])) >= 12) ++visible;
            ++samples;
        }
    }
    HeapFree(GetProcessHeap(), 0, pixels);
    return visible >= max(4, samples / 2000);
}

static HBITMAP capture_with_duplication(HWND hwnd, const int region[4], const RECT *window_rect) {
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    IDXGIFactory1 *factory = NULL;
    IDXGIAdapter1 *adapter = NULL;
    IDXGIOutput *output = NULL;
    IDXGIOutput1 *output1 = NULL;
    IDXGIOutputDuplication *duplication = NULL;
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    ID3D11Texture2D *desktop = NULL;
    ID3D11Texture2D *staging = NULL;
    IDXGIResource *resource = NULL;
    BOOL frame_acquired = FALSE;
    HBITMAP bitmap = NULL;
    BOOL com_owned = SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED));
    HRESULT hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void**)&factory);
    if (FAILED(hr)) goto cleanup;

    for (UINT adapter_index = 0; !adapter && factory->lpVtbl->EnumAdapters1(factory, adapter_index, &adapter) == S_OK; ++adapter_index) {
        for (UINT output_index = 0; adapter->lpVtbl->EnumOutputs(adapter, output_index, &output) == S_OK; ++output_index) {
            DXGI_OUTPUT_DESC output_desc;
            if (SUCCEEDED(output->lpVtbl->GetDesc(output, &output_desc)) && output_desc.Monitor == monitor) break;
            output->lpVtbl->Release(output);
            output = NULL;
        }
        if (!output) {
            adapter->lpVtbl->Release(adapter);
            adapter = NULL;
        }
    }
    if (!adapter || !output) goto cleanup;

    D3D_FEATURE_LEVEL feature_level;
    hr = D3D11CreateDevice((IDXGIAdapter*)adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL,
                           D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION,
                           &device, &feature_level, &context);
    if (FAILED(hr)) goto cleanup;
    hr = output->lpVtbl->QueryInterface(output, &IID_IDXGIOutput1, (void**)&output1);
    if (FAILED(hr)) goto cleanup;
    hr = output1->lpVtbl->DuplicateOutput(output1, (IUnknown*)device, &duplication);
    if (FAILED(hr)) goto cleanup;

    DXGI_OUTDUPL_FRAME_INFO frame_info;
    ZeroMemory(&frame_info, sizeof(frame_info));
    hr = duplication->lpVtbl->AcquireNextFrame(duplication, 1000, &frame_info, &resource);
    if (FAILED(hr)) goto cleanup;
    frame_acquired = TRUE;
    hr = resource->lpVtbl->QueryInterface(resource, &IID_ID3D11Texture2D, (void**)&desktop);
    if (FAILED(hr)) goto cleanup;

    D3D11_TEXTURE2D_DESC source_desc;
    desktop->lpVtbl->GetDesc(desktop, &source_desc);
    UINT capture_width = (UINT)max(1, region[2] - region[0]);
    UINT capture_height = (UINT)max(1, region[3] - region[1]);
    D3D11_TEXTURE2D_DESC staging_desc = source_desc;
    staging_desc.Width = capture_width;
    staging_desc.Height = capture_height;
    staging_desc.MipLevels = 1;
    staging_desc.ArraySize = 1;
    staging_desc.SampleDesc.Count = 1;
    staging_desc.SampleDesc.Quality = 0;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.MiscFlags = 0;
    hr = device->lpVtbl->CreateTexture2D(device, &staging_desc, NULL, &staging);
    if (FAILED(hr)) goto cleanup;

    DXGI_OUTPUT_DESC output_desc;
    if (FAILED(output->lpVtbl->GetDesc(output, &output_desc))) goto cleanup;
    LONG source_x = window_rect->left + max(0, region[0]) - output_desc.DesktopCoordinates.left;
    LONG source_y = window_rect->top + max(0, region[1]) - output_desc.DesktopCoordinates.top;
    if (source_x < 0 || source_y < 0 || source_x + (LONG)capture_width > (LONG)source_desc.Width ||
        source_y + (LONG)capture_height > (LONG)source_desc.Height) goto cleanup;
    D3D11_BOX source_box = {
        (UINT)source_x, (UINT)source_y, 0,
        (UINT)(source_x + capture_width), (UINT)(source_y + capture_height), 1
    };
    context->lpVtbl->CopySubresourceRegion(context, (ID3D11Resource*)staging, 0, 0, 0, 0,
                                           (ID3D11Resource*)desktop, 0, &source_box);

    D3D11_MAPPED_SUBRESOURCE mapped;
    ZeroMemory(&mapped, sizeof(mapped));
    hr = context->lpVtbl->Map(context, (ID3D11Resource*)staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) goto cleanup;
    BITMAPINFO bitmap_info = {0};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = (LONG)capture_width;
    bitmap_info.bmiHeader.biHeight = -(LONG)capture_height;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;
    void *pixels = NULL;
    HDC screen = GetDC(NULL);
    bitmap = CreateDIBSection(screen, &bitmap_info, DIB_RGB_COLORS, &pixels, NULL, 0);
    ReleaseDC(NULL, screen);
    if (bitmap && pixels) {
        for (UINT row = 0; row < capture_height; ++row)
            memcpy((BYTE*)pixels + (SIZE_T)row * capture_width * 4,
                   (BYTE*)mapped.pData + (SIZE_T)row * mapped.RowPitch,
                   (SIZE_T)capture_width * 4);
    } else if (bitmap) {
        DeleteObject(bitmap);
        bitmap = NULL;
    }
    context->lpVtbl->Unmap(context, (ID3D11Resource*)staging, 0);

cleanup:
    if (frame_acquired) duplication->lpVtbl->ReleaseFrame(duplication);
    if (resource) resource->lpVtbl->Release(resource);
    if (staging) staging->lpVtbl->Release(staging);
    if (desktop) desktop->lpVtbl->Release(desktop);
    if (duplication) duplication->lpVtbl->Release(duplication);
    if (output1) output1->lpVtbl->Release(output1);
    if (context) context->lpVtbl->Release(context);
    if (device) device->lpVtbl->Release(device);
    if (output) output->lpVtbl->Release(output);
    if (adapter) adapter->lpVtbl->Release(adapter);
    if (factory) factory->lpVtbl->Release(factory);
    if (com_owned) CoUninitialize();
    return bitmap;
}

HBITMAP capture_game_window(const wchar_t *title, const int region[4], int mode, RECT *captured_rect) {
    HWND hwnd = find_game_window(title);
    if (!hwnd) hwnd = find_game_window(L"原神");
    if (!hwnd || !window_bounds(hwnd, captured_rect)) return NULL;
    BOOL foreground = GetForegroundWindow() == hwnd;
    int x = captured_rect->left + max(0, region[0]);
    int y = captured_rect->top + max(0, region[1]);
    int width = max(1, region[2] - region[0]);
    int height = max(1, region[3] - region[1]);
    if (mode == 2 && foreground) {
        HBITMAP duplicated = capture_with_duplication(hwnd, region, captured_rect);
        if (duplicated && bitmap_has_visible_content(duplicated)) return duplicated;
        if (duplicated) DeleteObject(duplicated);
    }
    HDC screen = GetDC(NULL);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    HGDIOBJ old = SelectObject(memory, bitmap);
    BOOL ok = FALSE;
    if (mode == 1 || !foreground) {
        int full_width = max(region[2], captured_rect->right - captured_rect->left);
        int full_height = max(region[3], captured_rect->bottom - captured_rect->top);
        HDC full_dc = CreateCompatibleDC(screen);
        HBITMAP full_bitmap = CreateCompatibleBitmap(screen, full_width, full_height);
        HGDIOBJ full_old = full_bitmap ? SelectObject(full_dc, full_bitmap) : NULL;
        ok = full_bitmap && PrintWindow(hwnd, full_dc, PW_RENDERFULLCONTENT);
        if (ok) ok = BitBlt(memory, 0, 0, width, height, full_dc, max(0, region[0]), max(0, region[1]), SRCCOPY);
        if (full_old) SelectObject(full_dc, full_old);
        if (full_bitmap) DeleteObject(full_bitmap);
        DeleteDC(full_dc);
        if (ok) {
            SelectObject(memory, old);
            BOOL visible = bitmap_has_visible_content(bitmap);
            old = SelectObject(memory, bitmap);
            if (!visible) ok = FALSE;
        }
        if (!ok && GetForegroundWindow() == hwnd)
            ok = BitBlt(memory, 0, 0, width, height, screen, x, y, SRCCOPY | CAPTUREBLT);
    } else {
        ok = BitBlt(memory, 0, 0, width, height, screen, x, y, SRCCOPY | CAPTUREBLT);
    }
    SelectObject(memory, old); DeleteDC(memory); ReleaseDC(NULL, screen);
    if (!ok) { DeleteObject(bitmap); return NULL; }
    return bitmap;
}

static BOOL focus_capture_window(HWND target) {
    if (!target || !IsWindow(target)) return FALSE;
    HWND foreground = GetForegroundWindow();
    if (foreground == target) return TRUE;
    DWORD current_thread = GetCurrentThreadId();
    DWORD target_thread = GetWindowThreadProcessId(target, NULL);
    BOOL attached = target_thread && target_thread != current_thread &&
                    AttachThreadInput(current_thread, target_thread, TRUE);
    ShowWindow(target, SW_RESTORE);
    BringWindowToTop(target);
    SetActiveWindow(target);
    SetForegroundWindow(target);
    SetFocus(target);
    BOOL focused = GetForegroundWindow() == target;
    if (attached) AttachThreadInput(current_thread, target_thread, FALSE);
    return focused;
}

BOOL activate_game_window(const wchar_t *title) {
    HWND target = find_game_window(title);
    if (!target) target = find_game_window(L"原神");
    if (!target) return FALSE;
    return focus_capture_window(target);
}

HBITMAP capture_game_window_for_test(const wchar_t *title, const int region[4], int mode,
                                     HWND restore_window, RECT *captured_rect) {
    HWND target = find_game_window(title);
    if (!target) target = find_game_window(L"原神");
    if (!target) return NULL;
    HWND previous = GetForegroundWindow();
    BOOL switched = previous != target;
    BOOL focused = !switched || focus_capture_window(target);
    if (switched) Sleep(150);
    int selected_mode = mode;
    if (!focused && selected_mode == 0) selected_mode = 1;
    HBITMAP bitmap = capture_game_window(title, region, selected_mode, captured_rect);
    if (!bitmap && mode != 1) bitmap = capture_game_window(title, region, 1, captured_rect);
    HWND restore = restore_window && IsWindow(restore_window) ? restore_window : previous;
    if (switched && restore && restore != target) {
        focus_capture_window(restore);
    }
    return bitmap;
}

static DWORD WINAPI capture_thread(LPVOID parameter) {
    App *app = (App*)parameter;
    RECT rect;
    InterlockedExchange(&app->busy, 1); ui_set_busy(1);
    app_log_format(L"INFO", L"开始图像捕获测试...");
    int mode = wcscmp(app->config.screenshot_mode, L"printwindow") == 0 ? 1 :
               wcscmp(app->config.screenshot_mode, L"duplication") == 0 ? 2 : 0;
    HBITMAP bitmap = capture_game_window_for_test(app->config.window_title, app->config.region, mode,
                                                  app->hwnd, &rect);
    if (bitmap) {
        EnterCriticalSection(&g_app.log_lock);
        if (app->preview_bitmap) DeleteObject(app->preview_bitmap);
        app->preview_bitmap = bitmap;
        app->region_selecting = FALSE;
        app->region_dragging = FALSE;
        _snwprintf_s(app->preview_info, 512, _TRUNCATE, L"捕获成功：%ld×%ld，模式 %s", rect.right - rect.left, rect.bottom - rect.top, app->config.screenshot_mode);
        LeaveCriticalSection(&g_app.log_lock);
        app_log_format(L"INFO", L"%s", app->preview_info);
        PostMessageW(app->hwnd, WM_APP_REGION, 0, 0);
    } else {
        app_log_format(L"ERROR", L"捕获失败；请确认游戏窗口存在且未使用独占全屏");
    }
    InterlockedExchange(&app->busy, 0); ui_set_busy(0);
    return 0;
}

BOOL capture_test_async(App *app) {
    if (InterlockedCompareExchange(&app->running, 1, 1)) return FALSE;
    background_reap(app);
    if (app->task_thread) return FALSE;
    if (InterlockedCompareExchange(&app->busy, 1, 0) != 0) return FALSE;
    HANDLE thread = CreateThread(NULL, 0, capture_thread, app, 0, NULL);
    if (!thread) { InterlockedExchange(&app->busy, 0); return FALSE; }
    app->task_thread = thread;
    ui_set_busy(1);
    return TRUE;
}

static DWORD WINAPI region_capture_thread(LPVOID parameter) {
    App *app = (App*)parameter;
    RECT window_rect;
    InterlockedExchange(&app->busy, 1);
    ui_set_busy(1);
    app_log_format(L"INFO", L"正在捕获原神完整窗口，请在预览图中拖拽选择聊天区域...");
    int mode = wcscmp(app->config.screenshot_mode, L"printwindow") == 0 ? 1 :
               wcscmp(app->config.screenshot_mode, L"duplication") == 0 ? 2 : 0;
    int full_region[4] = { 0, 0, 0, 0 };
    HWND target = find_game_window(app->config.window_title);
    if (!target) target = find_game_window(L"原神");
    if (target && GetWindowRect(target, &window_rect)) {
        full_region[2] = max(1, window_rect.right - window_rect.left);
        full_region[3] = max(1, window_rect.bottom - window_rect.top);
    }
    HBITMAP bitmap = full_region[2] > 0
        ? capture_game_window_for_test(app->config.window_title, full_region, mode, app->hwnd, &window_rect)
        : NULL;
    if (bitmap) {
        BITMAP info;
        ZeroMemory(&info, sizeof(info));
        GetObjectW(bitmap, sizeof(info), &info);
        EnterCriticalSection(&app->log_lock);
        if (app->preview_bitmap) DeleteObject(app->preview_bitmap);
        app->preview_bitmap = bitmap;
        app->preview_source_width = info.bmWidth;
        app->preview_source_height = abs(info.bmHeight);
        app->region_selecting = TRUE;
        app->region_dragging = FALSE;
        app->region_drag_start.x = 0;
        app->region_drag_start.y = 0;
        app->region_drag_current.x = 0;
        app->region_drag_current.y = 0;
        _snwprintf_s(app->preview_info, 512, _TRUNCATE,
                     L"完整窗口：%d×%d；请在预览图内拖拽框选聊天区域",
                     info.bmWidth, abs(info.bmHeight));
        LeaveCriticalSection(&app->log_lock);
        app_log_format(L"INFO", L"完整窗口捕获成功：%d×%d", info.bmWidth, abs(info.bmHeight));
        PostMessageW(app->hwnd, WM_APP_REGION, 0, 0);
    } else {
        app_log_format(L"ERROR", L"完整窗口捕获失败；请确认原神窗口可见且未使用独占全屏");
    }
    InterlockedExchange(&app->busy, 0);
    ui_set_busy(0);
    return 0;
}

BOOL capture_select_region_async(App *app) {
    if (!app || InterlockedCompareExchange(&app->running, 1, 1)) return FALSE;
    background_reap(app);
    if (app->task_thread) return FALSE;
    if (InterlockedCompareExchange(&app->busy, 1, 0) != 0) return FALSE;
    HANDLE thread = CreateThread(NULL, 0, region_capture_thread, app, 0, NULL);
    if (!thread) {
        InterlockedExchange(&app->busy, 0);
        return FALSE;
    }
    app->task_thread = thread;
    ui_set_busy(1);
    return TRUE;
}
