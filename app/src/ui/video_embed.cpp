#include "video_embed.h"

#include <cstring>

namespace ui {
namespace {

struct SearchState {
    DWORD pid  = 0;
    HWND  found = nullptr;
};

BOOL CALLBACK enumTopLevel(HWND h, LPARAM lp) {
    auto* st = reinterpret_cast<SearchState*>(lp);

    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != st->pid) return TRUE;
    if (!IsWindowVisible(h)) return TRUE;
    if (GetParent(h) != nullptr) return TRUE;

    // The receiver runs with CREATE_NO_WINDOW so it should have no console, but a console
    // handed to it by something else would be the wrong window to grab.
    char cls[64]{};
    GetClassNameA(h, cls, sizeof(cls));
    if (std::strcmp(cls, "ConsoleWindowClass") == 0) return TRUE;

    RECT r{};
    if (!GetWindowRect(h, &r)) return TRUE;
    if (r.right - r.left < 16 || r.bottom - r.top < 16) return TRUE;

    st->found = h;
    return FALSE;
}

} // namespace

HWND findReceiverVideoWindow(DWORD pid) {
    if (!pid) return nullptr;
    SearchState st{pid, nullptr};
    EnumWindows(enumTopLevel, reinterpret_cast<LPARAM>(&st));
    return st.found;
}

AdoptedWindow adoptWindow(HWND guest, HWND host) {
    AdoptedWindow a;
    if (!guest || !host || !IsWindow(guest)) return a;

    RECT wr{}, cr{};
    if (!GetWindowRect(guest, &wr) || !GetClientRect(guest, &cr)) return a;
    // The sink sizes its window to the video, so the client area is the source resolution.
    if (cr.right <= 0 || cr.bottom <= 0) return a;

    const LONG_PTR style   = GetWindowLongPtrW(guest, GWL_STYLE);
    const LONG_PTR exStyle = GetWindowLongPtrW(guest, GWL_EXSTYLE);

    const LONG_PTR childStyle =
        (style & ~static_cast<LONG_PTR>(WS_POPUP | WS_OVERLAPPED | WS_CAPTION | WS_THICKFRAME |
                                        WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU)) |
        static_cast<LONG_PTR>(WS_CHILD | WS_CLIPSIBLINGS);
    const LONG_PTR childEx =
        exStyle & ~static_cast<LONG_PTR>(WS_EX_APPWINDOW | WS_EX_WINDOWEDGE | WS_EX_TOPMOST |
                                         WS_EX_TOOLWINDOW | WS_EX_DLGMODALFRAME);

    SetWindowLongPtrW(guest, GWL_STYLE, childStyle);
    SetWindowLongPtrW(guest, GWL_EXSTYLE, childEx);

    if (!SetParent(guest, host) && GetParent(guest) != host) {
        SetWindowLongPtrW(guest, GWL_STYLE, style);
        SetWindowLongPtrW(guest, GWL_EXSTYLE, exStyle);
        return a;
    }

    a.hwnd    = guest;
    a.style   = style;
    a.exStyle = exStyle;
    a.rect    = wr;
    a.source  = SIZE{cr.right, cr.bottom};

    // A guest handed back with visible=false comes out of that without WS_VISIBLE, and the
    // style word above carries the gap into this adoption. Visibility is ShowWindow's to
    // set, never SetWindowLongPtr's; on an already-visible guest this is a no-op.
    ShowWindow(guest, SW_SHOW);
    return a;
}

void releaseWindow(const AdoptedWindow& a, bool visible) {
    if (!a.valid() || !IsWindow(a.hwnd)) return;

    // Dark first, then reparent: a window that is already hidden cannot flash on the way out.
    if (!visible) ShowWindow(a.hwnd, SW_HIDE);

    SetParent(a.hwnd, nullptr);
    // This puts the saved WS_VISIBLE bit back whether we want it or not; the SetWindowPos
    // below is what actually decides, and SWP_HIDEWINDOW clears the bit again for good.
    SetWindowLongPtrW(a.hwnd, GWL_STYLE, a.style);
    SetWindowLongPtrW(a.hwnd, GWL_EXSTYLE, a.exStyle);
    SetWindowPos(a.hwnd, HWND_TOP, a.rect.left, a.rect.top, a.rect.right - a.rect.left,
                 a.rect.bottom - a.rect.top,
                 SWP_FRAMECHANGED | SWP_NOACTIVATE |
                     (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
}

} // namespace ui
