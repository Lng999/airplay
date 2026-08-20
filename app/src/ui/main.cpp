// main.cpp - entry point for airplay-gui (Win32, -municode => wWinMain).
//
// Order matters: single-instance check first (so a second launch just raises the first
// window), then common controls, then the config, then the window.

#include "config_store.h"   // must come first: <winsock2.h> before <windows.h>

#include <commctrl.h>
#include <shellapi.h>

#include "main_window.h"
#include "single_instance.h"

namespace {

// -autostart / --autostart: behave as if [app] autostart_receiver were set. Used by the
// smoke test so the receiver can be brought up without clicking Start.
bool wantsAutostart() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;
    bool found = false;
    for (int i = 1; i < argc; ++i) {
        if (lstrcmpiW(argv[i], L"-autostart") == 0 || lstrcmpiW(argv[i], L"--autostart") == 0) {
            found = true;
            break;
        }
    }
    LocalFree(argv);
    return found;
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    ui::SingleInstance instance;
    if (!instance.acquire()) {
        ui::activateExistingInstance();
        return 0;
    }

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);

    ui::ConfigStore store;
    ui::AppConfig cfg;
    store.load(cfg);

    ui::MainWindow window(hInstance, store, cfg);
    if (!window.create()) {
        MessageBoxW(nullptr, L"Failed to create the main window.", L"airplay",
                    MB_ICONERROR | MB_OK);
        return 1;
    }
    window.show(cfg.startMinimized);

    if (cfg.autostartReceiver || wantsAutostart()) window.startReceiver();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window.hwnd(), &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return static_cast<int>(msg.wParam);
}
