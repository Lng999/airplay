// controls.h - the two custom-drawn controls of the main window.
//
// Both behave like the BUTTON they replace, so MainWindow's existing code keeps working:
//   kToggleClass - a switch standing in for BS_AUTOCHECKBOX: BM_GETCHECK / BM_SETCHECK, and a
//                  click toggles it and sends WM_COMMAND(BN_CLICKED) to the parent.
//   kPushClass   - a rounded push button: accent-filled by default, a bordered card with
//                  BS_FLAT. A click sends WM_COMMAND(BN_CLICKED).
// Both take WM_SETFONT, follow WS_DISABLED, are tab stops with a focus ring, and react to
// Space (and BM_CLICK). Their background comes from the parent's WM_CTLCOLORBTN brush, so
// they sit on whatever surface the parent says they are on. Colours: activePalette().
// Shapes are drawn with Direct2D (anti-aliased), text with GDI ClearType.
#pragma once

#include <windows.h>

namespace ui {

extern const wchar_t* const kToggleClass;
extern const wchar_t* const kPushClass;

// kPushClass style for a quiet, text-only button ("Ayarlar…", "Geri"): accent-coloured text,
// a soft fill only under the pointer. BS_NOTIFY means nothing else to this class.
inline constexpr DWORD kQuietButton = BS_NOTIFY;

// Registers both classes; call once before creating them. False if registration failed.
bool registerControls(HINSTANCE hinst);

// Anti-aliased shapes onto a GDI DC, for the window's own painting (cards, the status dot).
// A border is drawn only when borderWidth > 0. Silently does nothing if Direct2D is missing.
void paintRoundRect(HDC dc, const RECT& r, float radius, COLORREF fill, COLORREF border,
                    float borderWidth);
void paintCircle(HDC dc, float cx, float cy, float radius, COLORREF fill);

} // namespace ui
