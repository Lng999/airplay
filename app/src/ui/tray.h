// tray.h - notification-area icon and its context menu (PHASE2-SPEC "Tray").
#pragma once

#include <string>

#include "config_store.h"   // Win32 headers in the right order

namespace ui {

// Command ids returned by Tray::trackMenu(); the main window maps them onto its own actions.
enum TrayCommand : int {
    kTrayNone  = 0,
    kTrayShow  = 40001,
    kTrayStart = 40002,
    kTrayStop  = 40003,
    kTrayExit  = 40004
};

class Tray {
public:
    static constexpr UINT kIconId = 1;

    Tray() = default;
    ~Tray();
    Tray(const Tray&) = delete;
    Tray& operator=(const Tray&) = delete;

    // callbackMsg is the private message the shell posts to owner for mouse events.
    bool add(HWND owner, UINT callbackMsg);
    void remove();

    // Explorer restarted -> the icon must be re-added (RegisterWindowMessage("TaskbarCreated")).
    void readd();

    void setTip(const std::wstring& tip);

    // Blocking popup menu at the cursor; returns a TrayCommand (kTrayNone if dismissed).
    int trackMenu(HWND owner, bool running) const;

private:
    HWND         owner_       = nullptr;
    UINT         callbackMsg_ = 0;
    bool         added_       = false;
    std::wstring tip_         = L"airplay";
};

} // namespace ui
