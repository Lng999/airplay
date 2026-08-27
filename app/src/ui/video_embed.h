// video_embed.h - taking the receiver's video window into a window of ours.
//
// The picture belongs to uxplay.exe: d3d11videosink creates a top-level window in the child
// process as soon as frames arrive. GstVideoOverlay cannot be used from here - the sink
// subclasses whatever HWND it is handed, and SetWindowLongPtr(GWLP_WNDPROC) is the one thing
// Win32 refuses to do to another process's window. Reparenting is allowed, and that is what
// this module does. See docs/PHASE2-M2-SPEC.md for the measurements behind that claim.
#pragma once

#include "config_store.h"   // Win32 headers in the right order

namespace ui {

// What we changed about the guest window, so it can be handed back exactly as it was.
struct AdoptedWindow {
    HWND     hwnd     = nullptr;
    LONG_PTR style    = 0;
    LONG_PTR exStyle  = 0;
    RECT     rect{};            // screen rect before adoption
    SIZE     source{0, 0};      // client size before adoption = the video's own size

    bool valid() const { return hwnd != nullptr; }
};

// The child's video window, or nullptr while it has none. Matched by process id, not by
// class name: GetClassNameW returns a single character for the sink's window (measured), and
// a receiver started with CREATE_NO_WINDOW has no console window to confuse us with.
HWND findReceiverVideoWindow(DWORD pid);

// Make `guest` a child of `host`. Returns the saved state; an invalid() result means the
// window went away or refused to move (nothing is left half-changed in that case).
AdoptedWindow adoptWindow(HWND guest, HWND host);

// Hand the window back: original styles, original screen rect, no parent. Safe to call with
// a guest that has already been destroyed.
//
// `visible` says whether it is shown where it lands. Only the embed setting being turned off
// wants the receiver's own window back on the desktop. Stopping, exiting and swapping guests
// all end with that window destroyed moments later, and one that flashes up in between reads
// as a bug - it is the same window the user just watched, appearing and vanishing on its own.
// It still has to leave our window either way: Windows destroys children with their parent,
// and the sink does not expect that.
void releaseWindow(const AdoptedWindow& a, bool visible);

} // namespace ui
