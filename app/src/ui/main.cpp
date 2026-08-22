// main.cpp - entry point for airplay-gui (Win32, -municode => wWinMain).
//
// Order matters: single-instance check first (so a second launch just raises the first
// window), then common controls, then the config, then the window.

#include "config_store.h"   // must come first: <winsock2.h> before <windows.h>

#include <commctrl.h>
#include <objbase.h>   // WIN32_LEAN_AND_MEAN leaves COM out of windows.h
#include <shellapi.h>

#include <string>

#include "main_window.h"
#include "single_instance.h"

namespace {

// One flag, with or without the second dash.
//   -autostart  behave as if [app] autostart_receiver were set (the smoke test uses it)
//   -minimized  come up in the notification area; what the logon entry passes
//               (src/ui/autostart.cpp)
bool hasFlag(const wchar_t* name) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;
    const std::wstring shortForm = std::wstring(L"-") + name;
    const std::wstring longForm  = std::wstring(L"--") + name;
    bool found = false;
    for (int i = 1; i < argc && !found; ++i) {
        found = lstrcmpiW(argv[i], shortForm.c_str()) == 0 ||
                lstrcmpiW(argv[i], longForm.c_str()) == 0;
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

    // Shell APIs the UI reaches for (SHGetKnownFolderPath, the tray icon) are COM;
    // apartment-threaded is what a UI thread wants.
    const HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

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
    window.show(cfg.startMinimized || hasFlag(L"minimized"));

    if (cfg.autostartReceiver || hasFlag(L"autostart")) window.startReceiver();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window.hwnd(), &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (SUCCEEDED(comInit)) CoUninitialize();
    return static_cast<int>(msg.wParam);
}
