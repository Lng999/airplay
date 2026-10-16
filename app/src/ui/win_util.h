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

} // namespace ui
