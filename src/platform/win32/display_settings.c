#ifdef _WIN32
#include "platform/win32/native.h"
#include "platform/win32/display_settings.h"
#include "app/app_state.h"

static const wchar_t gpu_key[] = L"Software\\Microsoft\\DirectX\\UserGpuPreferences";
static const wchar_t gpu_field[] = L"GpuPreference=";

static bool read_gpu_profile(wchar_t *path, DWORD path_cap, wchar_t *profile, DWORD bytes) {
    DWORD n = GetModuleFileNameW(NULL, path, path_cap);
    if (!n || n >= path_cap) return false;
    LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, gpu_key, path, RRF_RT_REG_SZ,
                                  NULL, profile, &bytes);
    if (status == ERROR_FILE_NOT_FOUND) {
        profile[0] = 0;
        return true;
    }
    return status == ERROR_SUCCESS;
}

RenderDevice win32_read_render_device(void) {
    wchar_t path[2048], profile[2048];
    if (!read_gpu_profile(path, 2048, profile, sizeof(profile))) return GPU_DEFAULT;
    wchar_t *ctx = NULL;
    for (wchar_t *p = wcstok_s(profile, L";", &ctx); p; p = wcstok_s(NULL, L";", &ctx)) {
        if (!_wcsicmp(p, L"GpuPreference=2")) return GPU_DISCRETE;
        if (!_wcsicmp(p, L"GpuPreference=1")) return GPU_INTEGRATED;
    }
    return GPU_DEFAULT;
}

bool win32_set_render_device(RenderDevice device) {
    wchar_t path[2048], profile[2048], result[2080] = L"";
    if (!read_gpu_profile(path, 2048, profile, sizeof(profile))) return false;
    wchar_t *ctx = NULL;
    for (wchar_t *p = wcstok_s(profile, L";", &ctx); p; p = wcstok_s(NULL, L";", &ctx)) {
        if (_wcsnicmp(p, gpu_field, wcslen(gpu_field)) == 0) continue;
        wcscat_s(result, 2080, p);
        wcscat_s(result, 2080, L";");
    }
    if (device == GPU_DISCRETE) wcscat_s(result, 2080, L"GpuPreference=2;");
    if (device == GPU_INTEGRATED) wcscat_s(result, 2080, L"GpuPreference=1;");
    if (!result[0]) {
        LSTATUS status = RegDeleteKeyValueW(HKEY_CURRENT_USER, gpu_key, path);
        return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
    }
    DWORD bytes = (DWORD)((wcslen(result) + 1) * sizeof(wchar_t));
    return RegSetKeyValueW(HKEY_CURRENT_USER, gpu_key, path, REG_SZ, result, bytes) == ERROR_SUCCESS;
}

static WindowMode current_mode;
static WINDOWPLACEMENT windowed = { sizeof(WINDOWPLACEMENT) };
static MONITORINFOEXW monitor;

static bool fullscreen_display(bool enable) {
    DEVMODEW dm = {0};
    dm.dmSize = sizeof(dm);
    if (enable && !EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &dm)) return false;
    return ChangeDisplaySettingsExW(monitor.szDevice, enable ? &dm : NULL, NULL,
                                    enable ? CDS_FULLSCREEN : 0, NULL) == DISP_CHANGE_SUCCESSFUL;
}

static bool place_window(HWND hwnd, WindowMode mode) {
    LONG_PTR style = mode == WINDOW_DECORATED ? WS_OVERLAPPEDWINDOW : WS_POPUP;
    SetLastError(ERROR_SUCCESS);
    if (!SetWindowLongPtrW(hwnd, GWL_STYLE, style | WS_VISIBLE) && GetLastError()) return false;
    UINT flags = SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_NOACTIVATE;
    if (mode == WINDOW_DECORATED) {
        return SetWindowPlacement(hwnd, &windowed) &&
               SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, flags | SWP_NOMOVE | SWP_NOSIZE);
    }
    RECT r = monitor.rcMonitor;
    return SetWindowPos(hwnd, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, flags);
}

bool win32_set_window_mode(WindowMode mode) {
    if (mode == current_mode) return true;
    HWND hwnd = g_app.hwnd;
    if (current_mode == WINDOW_DECORATED) {
        monitor.cbSize = sizeof(monitor);
        if (!GetWindowPlacement(hwnd, &windowed) ||
            !GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST),
                             (MONITORINFO *)&monitor)) return false;
    }
    if (mode == WINDOW_FULLSCREEN && !fullscreen_display(true)) return false;
    if (current_mode == WINDOW_FULLSCREEN && !fullscreen_display(false)) return false;
    if (current_mode == WINDOW_DECORATED && IsZoomed(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    if (!place_window(hwnd, mode)) {
        if (mode == WINDOW_FULLSCREEN) fullscreen_display(false);
        if (current_mode == WINDOW_FULLSCREEN) fullscreen_display(true);
        place_window(hwnd, current_mode);
        return false;
    }
    current_mode = mode;
    return true;
}

void win32_release_window_mode(void) {
    if (current_mode == WINDOW_FULLSCREEN) fullscreen_display(false);
    current_mode = WINDOW_DECORATED;
}
#endif
