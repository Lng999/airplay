// video_window.h - the window the mirrored picture lives in (docs/PHASE2-M2-SPEC.md).
//
// A top-level window of ours that adopts the receiver's video window (see video_embed.h).
// It owns nothing about AirPlay: it is a black frame that keeps the guest centred at the
// source aspect ratio, plus fullscreen and always-on-top, which the guest cannot do for us.
#pragma once

#include <functional>
#include <string>

#include "config_store.h"
#include "device_frames.h"
#include "video_embed.h"

namespace ui {

class VideoWindow {
public:
    VideoWindow(HINSTANCE hinst, AppConfig& cfg);
    ~VideoWindow();
    VideoWindow(const VideoWindow&) = delete;
    VideoWindow& operator=(const VideoWindow&) = delete;

    // Adopt the receiver's window; creates our own window on first call and shows it.
    bool adopt(HWND guest);

    // Hand the guest back to the desktop and hide. Safe when nothing is adopted, and safe
    // when the guest has already been destroyed by the receiver.
    void release();

    // The user closed the picture. Unlike release(), the guest stays adopted - a child of
    // our now-hidden window - so the receiver's own window does not pop back onto the
    // desktop, which is what made a close look like a reopen. The session carries on.
    void hide();
    // Bring a hidden picture back (tray menu, or the embed setting turned on again).
    void showPicture();
    bool isHidden() const { return hidden_; }

    bool hasGuest()   const { return adopted_.valid(); }
    bool guestAlive() const { return adopted_.valid() && IsWindow(adopted_.hwnd); }
    SIZE sourceSize() const { return adopted_.source; }
    HWND hwnd()       const { return hwnd_; }

    // Who is mirroring, from HostEvent::clientModel ("iPhone14,5"). Decides whether a
    // device frame is drawn at all and what shape its cut-out has.
    void setDevice(const std::wstring& model);
    void setFrameEnabled(bool on);
    bool frameActive() const;
    // Only meaningful when the stream carries bars: they look the same whichever way the
    // phone is held, so the user gets to say.
    void toggleRotation();

    void setAlwaysOnTop(bool on);
    void setTitle(const std::wstring& text);
    void setFullscreen(bool on);
    bool isFullscreen() const { return fullscreen_; }

    // The user closed the video window: the guest is already back on the desktop and we are
    // hidden. The main window uses this to stop re-adopting for the rest of the session.
    void setOnClosed(std::function<void()> cb) { onClosed_ = std::move(cb); }

    // Copy the current position into the config (the caller decides when to persist it).
    void saveRect();

private:
    static LRESULT CALLBACK wndProcThunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    bool ensureWindow();
    void layoutGuest();
    void layoutPlain();          // letterbox, no frame - milestone 2 behaviour
    void layoutFramed();         // device frame: crop, place, clip
    void applyGuestRegion();     // rounded screen corners and the notch/island cut-out
    void clearGuestRegion();
    void paintFrame(HDC dc, const RECT& client);
    double effectiveAspect() const;   // what the window should keep while resizing
    void sizeToSource();
    void constrainSizing(WPARAM edge, RECT* r) const;
    SIZE frameExtra() const;   // window size minus client size, for the current style

    HINSTANCE  hinst_ = nullptr;
    AppConfig& cfg_;

    HWND          hwnd_ = nullptr;
    AdoptedWindow adopted_{};
    // Closed by the user while the guest is still ours; see hide().
    bool          hidden_ = false;

    // --- device frame ---
    std::wstring   deviceModel_;
    DeviceProfile  device_{};
    bool           wantsFrame_ = false;    // the model is a phone/tablet we can draw
    bool           landscapeHint_ = false; // user's answer when the stream is ambiguous
    FrameGeometry  geom_{};                // last computed layout, for WM_PAINT
    bool          fullscreen_ = false;
    WINDOWPLACEMENT prevPlacement_{};   // saved across a fullscreen switch
    LONG_PTR        prevStyle_ = 0;
    std::wstring    title_;
    std::function<void()> onClosed_;
};

} // namespace ui
