// theme.h - the light/dark palette the main window and its custom controls paint with.
//
// Follows Windows' own "app mode" (Settings > Personalisation > Colours), read from the
// AppsUseLightTheme value; the main window re-reads it on WM_SETTINGCHANGE "ImmersiveColorSet".
// Header-only, like win_util.h. Expects config_store.h first (_WIN32_WINNT).
#pragma once

#include <windows.h>
#include <dwmapi.h>

namespace ui {

struct Palette {
    bool     dark;
    COLORREF window;      // page background
    COLORREF card;        // raised surface: the status card, the toggle group, input fields
    COLORREF text;
    COLORREF textMuted;   // supporting lines, disabled labels
    COLORREF border;      // hairlines around cards and secondary buttons
    COLORREF accent;      // the primary button, a switched-on toggle, links
    COLORREF accentHot;   // the same under the pointer
    COLORREF onAccent;    // text on accent
    COLORREF toggleOff;   // track of a switched-off toggle
    COLORREF knob;
};

inline COLORREF blend(COLORREF a, COLORREF b, double t) {
    auto ch = [t](int x, int y) { return static_cast<BYTE>(x + (y - x) * t + 0.5); };
    return RGB(ch(GetRValue(a), GetRValue(b)), ch(GetGValue(a), GetGValue(b)),
               ch(GetBValue(a), GetBValue(b)));
}

// Neutral greys for both modes; the one colour is the accent, which is the user's own
// Windows accent (systemAccent) rather than a colour this app picks.
inline Palette paletteFor(bool dark, COLORREF accent = RGB(0x00, 0x78, 0xd7)) {
    // On a dark surface the accent is lifted a little so it does not sink into the grey.
    const COLORREF a = dark ? blend(accent, RGB(0xff, 0xff, 0xff), 0.15) : accent;
    const COLORREF hot = dark ? blend(a, RGB(0xff, 0xff, 0xff), 0.15) : blend(a, RGB(0, 0, 0), 0.12);
    if (dark)
        return Palette{true,
                       RGB(0x1e, 0x1e, 0x20), RGB(0x2a, 0x2a, 0x2d),
                       RGB(0xee, 0xee, 0xf0), RGB(0x9a, 0x9a, 0xa0), RGB(0x38, 0x38, 0x3c),
                       a, hot, RGB(0xff, 0xff, 0xff),
                       RGB(0x4a, 0x4a, 0x50), RGB(0xf4, 0xf4, 0xf4)};
    return Palette{false,
                   RGB(0xf0, 0xf0, 0xf2), RGB(0xff, 0xff, 0xff),
                   RGB(0x1d, 0x1d, 0x1f), RGB(0x6e, 0x6e, 0x73), RGB(0xe3, 0xe3, 0xe6),
                   a, hot, RGB(0xff, 0xff, 0xff),
                   RGB(0xd6, 0xd6, 0xdb), RGB(0xff, 0xff, 0xff)};
}

// The accent colour the user picked in Windows (DWM's AccentColor, stored as 0xAABBGGRR).
// Falls back to Windows' default blue when it cannot be read.
inline COLORREF systemAccent() {
    DWORD abgr = 0, size = sizeof(abgr);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor",
                     RRF_RT_REG_DWORD, nullptr, &abgr, &size) != ERROR_SUCCESS)
        return RGB(0x00, 0x78, 0xd7);
    return RGB(abgr & 0xff, (abgr >> 8) & 0xff, (abgr >> 16) & 0xff);
}

// True when Windows is set to dark mode for apps. Any failure to read it means light, which
// is also what Windows assumes when the value is missing.
inline bool systemPrefersDark() {
    DWORD value = 1, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS)
        return false;
    return value == 0;
}

// The palette everything paints with right now; MainWindow sets it.
inline Palette& activePalette() {
    static Palette p = paletteFor(false);
    return p;
}

// A dark caption to go with a dark window. Attribute 20 since Windows 10 20H1, 19 before it;
// on anything older both fail and the caption simply stays light.
inline void useDarkTitleBar(HWND hwnd, bool dark) {
    const BOOL v = dark ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &v, sizeof(v))))
        DwmSetWindowAttribute(hwnd, 19, &v, sizeof(v));
}

} // namespace ui
