// test_controls.cpp - the custom toggle and push button keep the BUTTON contract
// (src/ui/controls.*) that MainWindow relies on: BM_GETCHECK / BM_SETCHECK, and a click -
// mouse, Space or BM_CLICK - sending WM_COMMAND(BN_CLICKED) to the parent, never while disabled.
//
// The windows are created hidden; nothing is shown and no input is synthesised system-wide.
#include "config_store.h"   // Win32 headers in the right order

#include <cstdio>

#include "controls.h"
#include "theme.h"

namespace {

int g_checks = 0, g_failed = 0;
void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failed;
        std::printf("FAIL  %s\n", what);
    }
}

int g_clicks = 0, g_lastId = 0;
HWND g_lastFrom = nullptr;

LRESULT CALLBACK parentProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_COMMAND && HIWORD(wp) == BN_CLICKED) {
        ++g_clicks;
        g_lastId = LOWORD(wp);
        g_lastFrom = reinterpret_cast<HWND>(lp);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

} // namespace

int main() {
    HINSTANCE hinst = GetModuleHandleW(nullptr);
    check(ui::registerControls(hinst), "registerControls");
    check(ui::registerControls(hinst), "registerControls is idempotent");

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = parentProc;
    wc.hInstance = hinst;
    wc.lpszClassName = L"AirplayControlsTestParent";
    RegisterClassExW(&wc);
    HWND parent = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 300, 200,
                                  nullptr, nullptr, hinst, nullptr);
    check(parent != nullptr, "parent window");

    HWND toggle = CreateWindowExW(0, ui::kToggleClass, L"Telefon çerçevesi çiz",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP, 10, 10, 260, 30, parent,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(42)), hinst, nullptr);
    HWND push = CreateWindowExW(0, ui::kPushClass, L"Başlat", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                10, 50, 260, 40, parent,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(43)), hinst, nullptr);
    check(toggle && push, "controls created");

    // --- toggle: the checkbox half of the contract ---
    check(SendMessageW(toggle, BM_GETCHECK, 0, 0) == BST_UNCHECKED, "toggle starts off");
    SendMessageW(toggle, BM_SETCHECK, BST_CHECKED, 0);
    check(SendMessageW(toggle, BM_GETCHECK, 0, 0) == BST_CHECKED, "BM_SETCHECK turns it on");
    check(g_clicks == 0, "BM_SETCHECK does not notify (like a real checkbox)");

    SendMessageW(toggle, BM_CLICK, 0, 0);
    check(SendMessageW(toggle, BM_GETCHECK, 0, 0) == BST_UNCHECKED, "a click toggles it off");
    check(g_clicks == 1 && g_lastId == 42 && g_lastFrom == toggle,
          "a click sends BN_CLICKED with the control's id and handle");

    // Space: down then up, as the keyboard delivers it.
    SendMessageW(toggle, WM_KEYDOWN, VK_SPACE, 0);
    check(g_clicks == 1, "Space down alone does nothing yet");
    SendMessageW(toggle, WM_KEYUP, VK_SPACE, 0);
    check(g_clicks == 2 && SendMessageW(toggle, BM_GETCHECK, 0, 0) == BST_CHECKED,
          "Space up clicks");

    // Mouse: press and release inside.
    SendMessageW(toggle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5));
    SendMessageW(toggle, WM_LBUTTONUP, 0, MAKELPARAM(5, 5));
    check(g_clicks == 3 && SendMessageW(toggle, BM_GETCHECK, 0, 0) == BST_UNCHECKED,
          "press and release inside clicks");
    // Released outside: no click.
    SendMessageW(toggle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5));
    SendMessageW(toggle, WM_LBUTTONUP, 0, MAKELPARAM(5000, 5));
    check(g_clicks == 3, "released outside does not click");

    EnableWindow(toggle, FALSE);
    SendMessageW(toggle, BM_CLICK, 0, 0);
    check(g_clicks == 3 && SendMessageW(toggle, BM_GETCHECK, 0, 0) == BST_UNCHECKED,
          "a disabled toggle ignores clicks");
    EnableWindow(toggle, TRUE);

    // --- push button: clicks, never checks ---
    SendMessageW(push, BM_CLICK, 0, 0);
    check(g_clicks == 4 && g_lastId == 43, "the push button sends BN_CLICKED");
    check(SendMessageW(push, BM_GETCHECK, 0, 0) == BST_UNCHECKED, "a push button never checks");
    check((SendMessageW(push, WM_GETDLGCODE, 0, 0) & DLGC_BUTTON) != 0,
          "dialog navigation treats it as a button");

    // --- text and font round-trip, painting in both palettes does not fault ---
    SetWindowTextW(push, L"Durdur");
    wchar_t text[32] = L"";
    GetWindowTextW(push, text, 32);
    check(lstrcmpW(text, L"Durdur") == 0, "WM_SETTEXT / WM_GETTEXT");
    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    SendMessageW(push, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    check(reinterpret_cast<HFONT>(SendMessageW(push, WM_GETFONT, 0, 0)) == font, "WM_SETFONT / WM_GETFONT");
    for (bool dark : {false, true}) {
        ui::activePalette() = ui::paletteFor(dark);
        UpdateWindow(toggle);
        RedrawWindow(parent, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    }
    check(ui::paletteFor(true).dark && !ui::paletteFor(false).dark, "both palettes exist");

    DestroyWindow(parent);
    std::printf("%s: %d checks, %d failed\n", g_failed ? "FAILED" : "OK", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
