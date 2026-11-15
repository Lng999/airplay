#include "video_window.h"

#include <algorithm>
#include <cmath>

#include "../res/resource.h"
#include "strings.h"
#include "win_util.h"

namespace ui {
namespace {

const wchar_t* const kClass = L"AirplayVideoWindow";

// Our entry in the window's own system menu. A custom WM_SYSCOMMAND id has to be below
// 0xF000 and a multiple of 16 - Windows masks the low four bits off before it arrives.
constexpr int kSysFullscreen = 0x1000;

constexpr int kMinClient = 160;

// The rotate entry, next to fullscreen in the window's own system menu.
constexpr int kSysRotate = 0x1010;

// The phone: a slim near-black bezel with a lighter hairline, as macOS iPhone Mirroring draws
// it. The backdrop only shows in fullscreen; windowed, the window is shaped to the body.
constexpr COLORREF kBackdrop = RGB(0x0a, 0x0a, 0x0c);
constexpr COLORREF kBody     = RGB(0x05, 0x05, 0x06);
constexpr COLORREF kBodyEdge = RGB(0x48, 0x48, 0x50);

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
        AppendMenuW(sys, MF_STRING, kSysRotate, str::kVideoRotateMenu);
    }
    return true;
}

bool VideoWindow::adopt(HWND guest) {
    if (!ensureWindow()) return false;
    if (adopted_.valid()) release();

    adopted_ = adoptWindow(guest, hwnd_);
    if (!adopted_.valid()) return false;

    // Sized, placed, framed and - if that is the remembered state - already fullscreen,
    // all of it while we are still off screen. Showing first and arranging afterwards put a
    // default-sized window holding an unplaced picture on screen for a frame, and then a
    // jump to fullscreen on top of it: the blink on the first connection. The window goes
    // up once, finished.
    hidden_ = false;
    updateChrome();
    sizeToSource();
    setAlwaysOnTop(cfg_.alwaysOnTop);
    if (cfg_.videoFullscreen || cfg_.fullscreen) setFullscreen(true);
    layoutGuest();
    ShowWindow(hwnd_, SW_SHOW);
    return true;
}

void VideoWindow::release(bool toDesktop) {
    hidden_ = false;
    if (!adopted_.valid()) return;

    // Us first, guest second. Clearing the region, hiding the guest and reparenting it are
    // four repaints of our client area, and what they repaint is a window with the picture
    // already gone: a black rectangle, or the device frame drawn around a hole. Hiding
    // ourselves last left that on screen for a couple of milliseconds - a frame or two on a
    // busy machine - which is the blink users saw after pressing Durdur. Nothing can flash
    // inside a window that is already off screen.
    if (hwnd_) {
        saveRect();
        ShowWindow(hwnd_, SW_HIDE);
    }

    clearGuestRegion();
    AdoptedWindow a = adopted_;
    adopted_ = AdoptedWindow{};
    geom_ = FrameGeometry{};
    releaseWindow(a, toDesktop);
}

void VideoWindow::hide() {
    if (!hwnd_) return;
    // saveRect() is a no-op while fullscreen, which is what we want: the remembered rect is
    // the windowed one, and leaving fullscreen below does not change it.
    saveRect();
    // One call, and the guest goes dark with us - it is still our child, and Windows hides a
    // parent and its children together. Leaving fullscreen comes after, never before:
    // SetWindowPlacement would put the window back on screen on its way out of fullscreen.
    ShowWindow(hwnd_, SW_HIDE);
    if (fullscreen_) setFullscreen(false);
    hidden_ = true;
}

void VideoWindow::showPicture() {
    if (!hwnd_ || !hidden_) return;
    hidden_ = false;
    ShowWindow(hwnd_, SW_SHOW);
    setAlwaysOnTop(cfg_.alwaysOnTop);
    layoutGuest();
    InvalidateRect(hwnd_, nullptr, TRUE);
    SetForegroundWindow(hwnd_);
}

void VideoWindow::setDevice(const std::wstring& model) {
    if (model == deviceModel_) return;
    deviceModel_ = model;
    device_ = lookupDevice(model);
    wantsFrame_ = deviceWantsFrame(model);
    landscapeHint_ = false;
    if (hwnd_) {
        updateChrome();
        layoutGuest();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

void VideoWindow::setFrameEnabled(bool on) {
    if (cfg_.deviceFrame == on) return;
    cfg_.deviceFrame = on;
    if (!hwnd_) return;
    if (!on) clearGuestRegion();
    updateChrome();
    layoutGuest();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

// With a frame, the window is the phone and nothing else - no caption, no border, shaped to
// the body (layoutFramed) - like macOS iPhone Mirroring. Without one, an ordinary window.
// Fullscreen owns the style while it lasts; setFullscreen(false) calls back in here.
void VideoWindow::updateChrome() {
    if (!hwnd_ || fullscreen_) return;
    const LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_STYLE);
    const LONG_PTR want =
        (style & static_cast<LONG_PTR>(WS_VISIBLE | WS_MINIMIZE)) |
        static_cast<LONG_PTR>(WS_CLIPCHILDREN) |
        static_cast<LONG_PTR>(frameActive() ? (WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX)
                                            : WS_OVERLAPPEDWINDOW);
    if (style == want) return;
    SetWindowLongPtrW(hwnd_, GWL_STYLE, want);
    shapeWindow(false);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    sizeToSource();   // the window's aspect changes with the frame
}

// Clip our own window to the phone body, so the corners around it are the desktop. In
// window coordinates, which for the borderless style are the client coordinates of geom_.
void VideoWindow::shapeWindow(bool on) {
    if (on && geom_.valid) {
        const RECT& b = geom_.body;
        HRGN rgn = CreateRoundRectRgn(b.left, b.top, b.right + 1, b.bottom + 1,
                                      geom_.bodyRadius * 2, geom_.bodyRadius * 2);
        if (!rgn) return;
        // The window takes ownership of the region.
        if (SetWindowRgn(hwnd_, rgn, TRUE)) shaped_ = true;
        else DeleteObject(rgn);
    } else if (shaped_) {
        SetWindowRgn(hwnd_, nullptr, TRUE);
        shaped_ = false;
    }
}

bool VideoWindow::frameActive() const {
    return cfg_.deviceFrame && wantsFrame_ && adopted_.source.cx > 0;
}

void VideoWindow::toggleRotation() {
    landscapeHint_ = !landscapeHint_;
    if (!hwnd_) return;
    layoutGuest();
    InvalidateRect(hwnd_, nullptr, TRUE);
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
    // With a frame on, the window follows the phone's shape, not the stream's - a portrait
    // screen inside a 16:9 stream would otherwise open as a wide window with a tiny phone
    // in the middle.
    const double framed = effectiveAspect();
    if (framed > 0.0) {
        const double area = static_cast<double>(cw) * ch;
        ch = static_cast<int>(std::sqrt(area / framed));
        cw = static_cast<int>(ch * framed);
    }
    // Never larger than 85% of the work area, and always at the source aspect.
    const int maxW = static_cast<int>(workW * 0.85), maxH = static_cast<int>(workH * 0.85);
    double scale = 1.0;
    if (cw > maxW) scale = static_cast<double>(maxW) / cw;
    if (ch > maxH) scale = (std::min)(scale, static_cast<double>(maxH) / ch);
    cw = static_cast<int>(cw * scale);
    const double aspect = effectiveAspect() > 0.0 ? effectiveAspect()
                                                  : static_cast<double>(sw) / sh;
    ch = static_cast<int>(cw / aspect + 0.5);
    if (cw < kMinClient) cw = kMinClient;
    if (ch < kMinClient / 2) ch = kMinClient / 2;

    const int w = cw + extra.cx, h = ch + extra.cy;
    int x = cfg_.vx, y = cfg_.vy;
    // A remembered spot on a monitor that has since been unplugged counts as no spot at all.
    if (cfg_.vw <= 200 || cfg_.vh <= 200 || x == CW_USEDEFAULT || y == CW_USEDEFAULT ||
        !rectOnSomeMonitor(RECT{x, y, x + w, y + h})) {
        x = work.left + (workW - w) / 2;
        y = work.top + (workH - h) / 2;
    }
    SetWindowPos(hwnd_, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

void VideoWindow::layoutGuest() {
    if (!hwnd_ || !guestAlive()) return;
    if (frameActive())
        layoutFramed();
    else
        layoutPlain();
}

void VideoWindow::layoutPlain() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int cw = rc.right, ch = rc.bottom;
    if (cw <= 0 || ch <= 0) return;

    geom_ = FrameGeometry{};
    clearGuestRegion();
    shapeWindow(false);

    const int sw = adopted_.source.cx > 0 ? adopted_.source.cx : cw;
    const int sh = adopted_.source.cy > 0 ? adopted_.source.cy : ch;

    // Letterbox: the guest keeps the source aspect, the black backdrop fills the rest.
    const double scale = (std::min)(static_cast<double>(cw) / sw, static_cast<double>(ch) / sh);
    int w = static_cast<int>(sw * scale + 0.5), h = static_cast<int>(sh * scale + 0.5);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    SetWindowPos(adopted_.hwnd, nullptr, (cw - w) / 2, (ch - h) / 2, w, h,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void VideoWindow::layoutFramed() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const SIZE client{rc.right, rc.bottom};
    geom_ = layoutDeviceFrame(client, adopted_.source, device_, landscapeHint_);
    if (!geom_.valid) {
        layoutPlain();
        return;
    }
    SetWindowPos(adopted_.hwnd, nullptr, geom_.guest.left, geom_.guest.top,
                 geom_.guest.right - geom_.guest.left, geom_.guest.bottom - geom_.guest.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    applyGuestRegion();
    shapeWindow(!fullscreen_);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

// The region clips the guest to the screen rectangle (dropping any pillarbox the phone baked
// in) and rounds the display corners. No notch or Dynamic Island is cut out: the stream
// already carries whatever iOS draws there - an island that grows for a Live Activity
// included - and a fixed hole on top of it showed two islands. Cross-process SetWindowRgn is
// allowed - measured, see docs/PHASE2-M2-SPEC.md - but it is in window coordinates, so every
// resize re-applies it.
void VideoWindow::applyGuestRegion() {
    if (!guestAlive() || !geom_.valid) return;

    const RECT& clip = geom_.guestClip;
    HRGN rgn = CreateRoundRectRgn(clip.left, clip.top, clip.right + 1, clip.bottom + 1,
                                  geom_.screenRadius * 2, geom_.screenRadius * 2);
    if (!rgn) return;
    // The window takes ownership of the region; do not delete it here.
    if (!SetWindowRgn(adopted_.hwnd, rgn, TRUE)) DeleteObject(rgn);
}

void VideoWindow::clearGuestRegion() {
    if (guestAlive()) SetWindowRgn(adopted_.hwnd, nullptr, TRUE);
}

double VideoWindow::effectiveAspect() const {
    if (!frameActive()) return 0.0;
    // geom_ knows the orientation actually in use; before the first layout, guess from the
    // stream the same way layoutDeviceFrame will.
    const bool landscape = geom_.valid ? geom_.landscape : landscapeHint_;
    return frameAspect(device_, landscape);
}

void VideoWindow::paintFrame(HDC dc, const RECT& client) {
    HBRUSH back = CreateSolidBrush(kBackdrop);
    FillRect(dc, &client, back);
    DeleteObject(back);

    if (!geom_.valid) return;

    HBRUSH body = CreateSolidBrush(kBody);
    HPEN   edge = CreatePen(PS_SOLID, 1, kBodyEdge);
    HGDIOBJ oldB = SelectObject(dc, body);
    HGDIOBJ oldP = SelectObject(dc, edge);
    RoundRect(dc, geom_.body.left, geom_.body.top, geom_.body.right, geom_.body.bottom,
              geom_.bodyRadius * 2, geom_.bodyRadius * 2);
    SelectObject(dc, oldB);
    SelectObject(dc, oldP);
    DeleteObject(body);
    DeleteObject(edge);
}

void VideoWindow::constrainSizing(WPARAM edge, RECT* r) const {
    if (!r || adopted_.source.cx <= 0 || adopted_.source.cy <= 0) return;
    const SIZE extra = frameExtra();
    int cw = (r->right - r->left) - extra.cx;
    int ch = (r->bottom - r->top) - extra.cy;
    if (cw < kMinClient) cw = kMinClient;
    if (ch < kMinClient / 2) ch = kMinClient / 2;

    const double framed = effectiveAspect();
    const double ar = framed > 0.0 ? framed
                                   : static_cast<double>(adopted_.source.cx) / adopted_.source.cy;
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
        // Going fullscreen before the window has ever been shown records SW_HIDE, and
        // leaving fullscreen later would then restore a window nobody can see. Windowed
        // means visible; only hide() decides otherwise, and it checks for itself.
        if (prevPlacement_.showCmd == SW_HIDE) prevPlacement_.showCmd = SW_SHOWNORMAL;
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
        // SetWindowPlacement applies the saved showCmd, so a window that is off screen when
        // it leaves fullscreen would come back on. Keep whatever visibility we came in with.
        WINDOWPLACEMENT wp = prevPlacement_;
        if (!IsWindowVisible(hwnd_)) wp.showCmd = SW_HIDE;
        SetWindowPlacement(hwnd_, &wp);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        setAlwaysOnTop(cfg_.alwaysOnTop);
    }
    fullscreen_ = on;
    cfg_.videoFullscreen = on;
    if (!on) updateChrome();   // the frame may have been switched while fullscreen
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
            if (wp == 'R') { toggleRotation(); return 0; }
            break;

        case WM_SYSKEYDOWN:
            if (wp == VK_RETURN) { setFullscreen(!fullscreen_); return 0; }
            break;

        case WM_LBUTTONDBLCLK:
            setFullscreen(!fullscreen_);
            return 0;

        // Borderless framed window: a band along the edge resizes, everything else is a
        // caption - drag to move, right-click for the system menu (DefWindowProc), double-click
        // for fullscreen. The guest is disabled (video_embed.cpp), so this sees the picture too.
        case WM_NCHITTEST:
            if (frameActive() && !fullscreen_) {
                POINT pt{static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
                ScreenToClient(hwnd, &pt);
                RECT rc{};
                GetClientRect(hwnd, &rc);
                const int band = GetSystemMetrics(SM_CXSIZEFRAME) +
                                 GetSystemMetrics(SM_CXPADDEDBORDER);
                const bool l = pt.x < band, r = pt.x >= rc.right - band;
                const bool t = pt.y < band, b = pt.y >= rc.bottom - band;
                if (t) return l ? HTTOPLEFT : r ? HTTOPRIGHT : HTTOP;
                if (b) return l ? HTBOTTOMLEFT : r ? HTBOTTOMRIGHT : HTBOTTOM;
                if (l) return HTLEFT;
                if (r) return HTRIGHT;
                return HTCAPTION;
            }
            break;

        case WM_NCLBUTTONDBLCLK:
            if (wp == HTCAPTION) { setFullscreen(!fullscreen_); return 0; }
            break;

        case WM_SYSCOMMAND:
            if ((wp & 0xFFF0) == kSysFullscreen) { setFullscreen(!fullscreen_); return 0; }
            if ((wp & 0xFFF0) == kSysRotate) { toggleRotation(); return 0; }
            break;

        // The guest covers the screen rect, so all this paints is the backdrop and the body
        // around and behind it - including whatever shows through the cut-out.
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            if (frameActive() && geom_.valid) {
                paintFrame(dc, rc);
            } else {
                HBRUSH b = CreateSolidBrush(RGB(0, 0, 0));
                FillRect(dc, &rc, b);
                DeleteObject(b);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_CLOSE:
            // Closing the picture must not close the session - and must not hand the guest
            // back to the desktop either: releaseWindow() restores it as a top-level window,
            // which the user sees as the window they just closed opening itself again.
            // Keep it adopted and hide the pair. Mirroring, audio included, carries on.
            hide();
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
