#include "tray.h"

#include <shellapi.h>

#include <cstring>

#include "../res/resource.h"
#include "strings.h"

namespace ui {
namespace {

void fillNid(NOTIFYICONDATAW& nid, HWND owner, UINT callbackMsg, const std::wstring& tip) {
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = owner;
    nid.uID              = Tray::kIconId;
    nid.uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = callbackMsg;
    // Notification-area size, not the window size: SM_CXSMICON picks the 16px (or the DPI
    // equivalent) image out of the .ico. LR_SHARED, so there is nothing to destroy.
    nid.hIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
                                              MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                              GetSystemMetrics(SM_CXSMICON),
                                              GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    if (!nid.hIcon) nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    lstrcpynW(nid.szTip, tip.c_str(), static_cast<int>(ARRAYSIZE(nid.szTip)));
}

} // namespace

Tray::~Tray() { remove(); }

bool Tray::add(HWND owner, UINT callbackMsg) {
    owner_       = owner;
    callbackMsg_ = callbackMsg;

    NOTIFYICONDATAW nid;
    fillNid(nid, owner_, callbackMsg_, tip_);
    added_ = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    return added_;
}

void Tray::readd() {
    if (!owner_) return;
    added_ = false;
    add(owner_, callbackMsg_);
}

void Tray::remove() {
    if (!added_ || !owner_) return;
    NOTIFYICONDATAW nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd   = owner_;
    nid.uID    = kIconId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    added_ = false;
}

void Tray::setTip(const std::wstring& tip) {
    tip_ = tip.size() < 127 ? tip : tip.substr(0, 126);
    if (!added_ || !owner_) return;
    NOTIFYICONDATAW nid;
    fillNid(nid, owner_, callbackMsg_, tip_);
    nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void Tray::showBalloon(const std::wstring& title, const std::wstring& text) const {
    if (!added_ || !owner_) return;
    NOTIFYICONDATAW nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd   = owner_;
    nid.uID    = kIconId;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;
    lstrcpynW(nid.szInfoTitle, title.c_str(), static_cast<int>(ARRAYSIZE(nid.szInfoTitle)));
    lstrcpynW(nid.szInfo, text.c_str(), static_cast<int>(ARRAYSIZE(nid.szInfo)));
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

int Tray::trackMenu(HWND owner, bool running, bool pictureHidden) const {
    HMENU menu = CreatePopupMenu();
    if (!menu) return kTrayNone;

    AppendMenuW(menu, MF_STRING, kTrayShow, str::kTrayShow);
    if (pictureHidden) AppendMenuW(menu, MF_STRING, kTrayPicture, str::kTrayPicture);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (running ? MF_GRAYED : 0), kTrayStart, str::kTrayStart);
    AppendMenuW(menu, MF_STRING | (running ? 0 : MF_GRAYED), kTrayStop, str::kTrayStop);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayUpdate, str::kTrayUpdate);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayExit, str::kTrayExit);
    SetMenuDefaultItem(menu, kTrayShow, FALSE);

    POINT pt{};
    GetCursorPos(&pt);
    // Required so the menu closes when the user clicks elsewhere (KB135788).
    SetForegroundWindow(owner);
    int cmd = static_cast<int>(TrackPopupMenu(menu,
                                              TPM_RETURNCMD | TPM_NONOTIFY |
                                                  TPM_RIGHTBUTTON | TPM_LEFTALIGN,
                                              pt.x, pt.y, 0, owner, nullptr));
    PostMessageW(owner, WM_NULL, 0, 0);
    DestroyMenu(menu);
    return cmd;
}

} // namespace ui
