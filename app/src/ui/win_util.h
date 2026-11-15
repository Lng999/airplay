// win_util.h - small, pure Win32 helpers shared by the UI windows.
//
// Header-only on purpose: video_window.cpp is also compiled into the live test binaries
// (app/tests/CMakeLists.txt), and an inline helper needs no extra source in any of them.
// Like every UI header it expects config_store.h to have been included first (_WIN32_WINNT).
#pragma once

#include <windows.h>

namespace ui {

// True when the rectangle overlaps at least one monitor that is attached right now. A window
// position saved on a monitor that has since been unplugged fails this, and the caller should
// fall back to its default placement instead of opening the window off screen.
inline bool rectOnSomeMonitor(const RECT& r) {
    if (r.right <= r.left || r.bottom <= r.top) return false;
    return MonitorFromRect(&r, MONITOR_DEFAULTTONULL) != nullptr;
}

// The window's DPI. GetDpiForWindow is looked up at run time (Windows 10 1607+); before
// that, the system DPI.
inline UINT dpiForWindow(HWND hwnd) {
    using Fn = UINT(WINAPI*)(HWND);
    if (HMODULE u32 = GetModuleHandleW(L"user32.dll")) {
        auto fn = reinterpret_cast<Fn>(
            reinterpret_cast<void*>(GetProcAddress(u32, "GetDpiForWindow")));
        if (fn) {
            UINT d = fn(hwnd);
            if (d >= 72) return d;
        }
    }
    HDC dc = GetDC(nullptr);
    UINT d = dc ? static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX)) : 96u;
    if (dc) ReleaseDC(nullptr, dc);
    return d ? d : 96u;
}

} // namespace ui
