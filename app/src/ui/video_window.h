// video_window.h - the window the mirrored picture lives in (docs/PHASE2-M2-SPEC.md).
//
// A top-level window of ours that adopts the receiver's video window (see video_embed.h).
// It owns nothing about AirPlay: it is a black frame that keeps the guest centred at the
// source aspect ratio, plus fullscreen and always-on-top, which the guest cannot do for us.
#pragma once

#include <functional>
#include <string>

#include "config_store.h"
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

    bool hasGuest()   const { return adopted_.valid(); }
    bool guestAlive() const { return adopted_.valid() && IsWindow(adopted_.hwnd); }
    SIZE sourceSize() const { return adopted_.source; }
    HWND hwnd()       const { return hwnd_; }

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
    void sizeToSource();
    void constrainSizing(WPARAM edge, RECT* r) const;
    SIZE frameExtra() const;   // window size minus client size, for the current style

    HINSTANCE  hinst_ = nullptr;
    AppConfig& cfg_;

    HWND          hwnd_ = nullptr;
    AdoptedWindow adopted_{};
    bool          fullscreen_ = false;
    WINDOWPLACEMENT prevPlacement_{};   // saved across a fullscreen switch
    LONG_PTR        prevStyle_ = 0;
    std::wstring    title_;
    std::function<void()> onClosed_;
};

} // namespace ui
