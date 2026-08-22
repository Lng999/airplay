#include "video_window.h"

#include <algorithm>

#include "../res/resource.h"
#include "strings.h"

namespace ui {
namespace {

const wchar_t* const kClass = L"AirplayVideoWindow";

// Our entry in the window's own system menu. A custom WM_SYSCOMMAND id has to be below
// 0xF000 and a multiple of 16 - Windows masks the low four bits off before it arrives.
constexpr int kSysFullscreen = 0x1000;

constexpr int kMinClient = 160;

} // namespace

VideoWindow::VideoWindow(HINSTANCE hinst, AppConfig& cfg) : hinst_(hinst), cfg_(cfg) {}

VideoWindow::~VideoWindow() {
    // Never take the guest down with us: it belongs to the receiver, and Windows destroys
    // child windows along with their parent.
    release();
    if (hwnd_) {
        HWND h = hwnd_;
        hwnd_ = nullptr;
        DestroyWindow(h);
    }
}

// ---------------------------------------------------------------------------

bool VideoWindow::ensureWindow() {
    if (hwnd_) return true;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &VideoWindow::wndProcThunk;
    wc.hInstance     = hinst_;
    wc.hIcon   = static_cast<HICON>(LoadImageW(hinst_, MAKEINTRESOURCEW(IDI_APPICON),
                                               IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
    wc.hIconSm = static_cast<HICON>(LoadImageW(hinst_, MAKEINTRESOURCEW(IDI_APPICON),
                                               IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                               GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    // Black, so the letterbox bars are not a grey frame around the picture.
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    hwnd_ = CreateWindowExW(0, kClass, str::kVideoTitle,
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            CW_USEDEFAULT, CW_USEDEFAULT, 640, 360,
                            nullptr, nullptr, hinst_, this);
    if (!hwnd_) return false;

    if (HMENU sys = GetSystemMenu(hwnd_, FALSE)) {
        AppendMenuW(sys, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(sys, MF_STRING, kSysFullscreen, str::kVideoFullscreenMenu);
    }
    return true;
}

bool VideoWindow::adopt(HWND guest) {
    if (!ensureWindow()) return false;
    if (adopted_.valid()) release();

    adopted_ = adoptWindow(guest, hwnd_);
    if (!adopted_.valid()) return false;

    sizeToSource();
    setAlwaysOnTop(cfg_.alwaysOnTop);
    ShowWindow(hwnd_, SW_SHOW);
    layoutGuest();
    if (cfg_.videoFullscreen || cfg_.fullscreen) setFullscreen(true);
    return true;
}

void VideoWindow::release() {
    if (!adopted_.valid()) return;
    AdoptedWindow a = adopted_;
    adopted_ = AdoptedWindow{};
    releaseWindow(a);
    if (hwnd_) {
        saveRect();
        ShowWindow(hwnd_, SW_HIDE);
    }
}

// ---------------------------------------------------------------------------

SIZE VideoWindow::frameExtra() const {
    RECT r{0, 0, 100, 100};
    const DWORD style = hwnd_ ? static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE))
                              : static_cast<DWORD>(WS_OVERLAPPEDWINDOW);
    const DWORD ex = hwnd_ ? static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE)) : 0u;
    AdjustWindowRectEx(&r, style, FALSE, ex);
    return SIZE{(r.right - r.left) - 100, (r.bottom - r.top) - 100};
}

void VideoWindow::sizeToSource() {
    if (!hwnd_ || fullscreen_) return;
    const int sw = adopted_.source.cx, sh = adopted_.source.cy;
    if (sw <= 0 || sh <= 0) return;

    // A remembered position wins - the user put the window where they wanted it. Only its
    // height is corrected, so a new source aspect does not letterbox forever.
    RECT work{0, 0, 1280, 800};
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &mi)) work = mi.rcWork;
    const int workW = work.right - work.left, workH = work.bottom - work.top;

    const SIZE extra = frameExtra();
    int cw = sw, ch = sh;
    if (cfg_.vw > 200 && cfg_.vh > 200) {
        cw = cfg_.vw - extra.cx;
        ch = cfg_.vh - extra.cy;
    }
    // Never larger than 85% of the work area, and always at the source aspect.
    const int maxW = static_cast<int>(workW * 0.85), maxH = static_cast<int>(workH * 0.85);
    double scale = 1.0;
    if (cw > maxW) scale = static_cast<double>(maxW) / cw;
    if (ch > maxH) scale = (std::min)(scale, static_cast<double>(maxH) / ch);
    cw = static_cast<int>(cw * scale);
    ch = static_cast<int>(cw * static_cast<double>(sh) / sw + 0.5);
    if (cw < kMinClient) cw = kMinClient;
    if (ch < kMinClient / 2) ch = kMinClient / 2;

    const int w = cw + extra.cx, h = ch + extra.cy;
    int x = cfg_.vx, y = cfg_.vy;
    if (cfg_.vw <= 200 || cfg_.vh <= 200 || x == CW_USEDEFAULT || y == CW_USEDEFAULT) {
        x = work.left + (workW - w) / 2;
        y = work.top + (workH - h) / 2;
    }
    SetWindowPos(hwnd_, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

void VideoWindow::layoutGuest() {
    if (!hwnd_ || !guestAlive()) return;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int cw = rc.right, ch = rc.bottom;
    if (cw <= 0 || ch <= 0) return;

    const int sw = adopted_.source.cx > 0 ? adopted_.source.cx : cw;
    const int sh = adopted_.source.cy > 0 ? adopted_.source.cy : ch;

    // Letterbox: the guest keeps the source aspect, the black class brush fills the rest.
    const double scale = (std::min)(static_cast<double>(cw) / sw, static_cast<double>(ch) / sh);
    int w = static_cast<int>(sw * scale + 0.5), h = static_cast<int>(sh * scale + 0.5);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    SetWindowPos(adopted_.hwnd, nullptr, (cw - w) / 2, (ch - h) / 2, w, h,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void VideoWindow::constrainSizing(WPARAM edge, RECT* r) const {
    if (!r || adopted_.source.cx <= 0 || adopted_.source.cy <= 0) return;
    const SIZE extra = frameExtra();
    int cw = (r->right - r->left) - extra.cx;
    int ch = (r->bottom - r->top) - extra.cy;
    if (cw < kMinClient) cw = kMinClient;
    if (ch < kMinClient / 2) ch = kMinClient / 2;

    const double ar = static_cast<double>(adopted_.source.cx) / adopted_.source.cy;
    if (edge == WMSZ_TOP || edge == WMSZ_BOTTOM) {
        cw = static_cast<int>(ch * ar + 0.5);
    } else {
        ch = static_cast<int>(cw / ar + 0.5);
    }

    const int w = cw + extra.cx, h = ch + extra.cy;
    if (edge == WMSZ_TOP || edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT)
        r->top = r->bottom - h;
    else
        r->bottom = r->top + h;
    if (edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT || edge == WMSZ_BOTTOMLEFT)
        r->left = r->right - w;
    else
        r->right = r->left + w;
}

// ---------------------------------------------------------------------------

void VideoWindow::setAlwaysOnTop(bool on) {
    if (!hwnd_) return;
    SetWindowPos(hwnd_, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void VideoWindow::setTitle(const std::wstring& text) {
    title_ = text;
    if (hwnd_) SetWindowTextW(hwnd_, title_.empty() ? str::kVideoTitle : title_.c_str());
}

void VideoWindow::setFullscreen(bool on) {
    if (!hwnd_ || on == fullscreen_) return;

    if (on) {
        prevPlacement_.length = sizeof(prevPlacement_);
        GetWindowPlacement(hwnd_, &prevPlacement_);
        prevStyle_ = GetWindowLongPtrW(hwnd_, GWL_STYLE);

        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi)) return;

        SetWindowLongPtrW(hwnd_, GWL_STYLE,
                          (prevStyle_ & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW)) |
                              static_cast<LONG_PTR>(WS_POPUP));
        SetWindowPos(hwnd_, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    } else {
        SetWindowLongPtrW(hwnd_, GWL_STYLE, prevStyle_);
        SetWindowPlacement(hwnd_, &prevPlacement_);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        setAlwaysOnTop(cfg_.alwaysOnTop);
    }
    fullscreen_ = on;
    cfg_.videoFullscreen = on;
    layoutGuest();
}

void VideoWindow::saveRect() {
    if (!hwnd_ || fullscreen_ || !IsWindowVisible(hwnd_) || IsIconic(hwnd_)) return;
    RECT r{};
    if (!GetWindowRect(hwnd_, &r)) return;
    cfg_.vx = r.left;
    cfg_.vy = r.top;
    cfg_.vw = r.right - r.left;
    cfg_.vh = r.bottom - r.top;
}

// ---------------------------------------------------------------------------

LRESULT CALLBACK VideoWindow::wndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    VideoWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<VideoWindow*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<VideoWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->wndProc(hwnd, msg, wp, lp);
}

LRESULT VideoWindow::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED) layoutGuest();
            return 0;

        case WM_SIZING:
            constrainSizing(wp, reinterpret_cast<RECT*>(lp));
            return TRUE;

        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            const SIZE extra = frameExtra();
            mmi->ptMinTrackSize.x = kMinClient + extra.cx;
            mmi->ptMinTrackSize.y = kMinClient / 2 + extra.cy;
            return 0;
        }

        // The guest is a child window and does not take focus by itself, so these reach us
        // even while the picture covers the whole client area.
        case WM_KEYDOWN:
            if (wp == VK_F11) { setFullscreen(!fullscreen_); return 0; }
            if (wp == VK_ESCAPE && fullscreen_) { setFullscreen(false); return 0; }
            break;

        case WM_SYSKEYDOWN:
            if (wp == VK_RETURN) { setFullscreen(!fullscreen_); return 0; }
            break;

        case WM_LBUTTONDBLCLK:
            setFullscreen(!fullscreen_);
            return 0;

        case WM_SYSCOMMAND:
            if ((wp & 0xFFF0) == kSysFullscreen) { setFullscreen(!fullscreen_); return 0; }
            break;

        case WM_CLOSE:
            // Closing the picture must not close the session. The guest goes back to being
            // its own window on the desktop; the receiver keeps mirroring into it.
            if (fullscreen_) setFullscreen(false);
            release();
            if (onClosed_) onClosed_();
            return 0;

        case WM_DESTROY:
            hwnd_ = nullptr;
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace ui
