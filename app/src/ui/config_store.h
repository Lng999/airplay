// config_store.h - %APPDATA%\airplay\config.ini <-> AppConfig <-> airplay::HostConfig.
// Also the single place where the UI's Win32 headers are pulled in, so that <winsock2.h>
// always precedes <windows.h> (iphlpapi needs winsock2; windows.h would otherwise drag in
// the winsock 1.1 header and the two clash).
#pragma once

// The UI needs Windows 10 declarations (WM_DPICHANGED, GetDpiForWindow). airplay_host
// exports _WIN32_WINNT=0x0601 PUBLIC-ly, so raise it here - before any Windows header is
// pulled in. Every UI translation unit includes this header first for exactly that reason.
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#undef WINVER
#define WINVER 0x0A00

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include <string>

#include "airplay/uxplay_host.h"

namespace ui {

// ---- small helpers shared by the UI translation units ----------------------
std::string  narrow(const std::wstring& w);   // UTF-16 -> UTF-8
std::wstring widen(const std::string& s);     // UTF-8  -> UTF-16
std::wstring joinPath(const std::wstring& a, const std::wstring& b);
bool         ensureDir(const std::wstring& dir);   // recursive mkdir -p
bool         fileExists(const std::wstring& path);

std::wstring exeDir();          // directory holding airplay-gui.exe
std::wstring roamingAppDir();   // %APPDATA%\airplay      (HOME for the child, config.ini)
std::wstring localAppDir();     // %LOCALAPPDATA%\airplay (gst registry, logs)

// ---- configuration ---------------------------------------------------------
struct AppConfig {
    // [receiver]
    std::wstring name         = L"AirPlay-PC";
    int          port         = 7100;          // 0 => bare -p (legacy port set)
    std::wstring videoSink    = L"d3d11videosink";
    std::wstring audioSink    = L"autoaudiosink";
    bool         fullscreen   = false;
    bool         h265         = false;
    bool         debug        = false;         // required for Resolution events (DESIGN 6.1)
    bool         noHold       = true;
    int          resetSeconds = 0;             // -reset n; 0 = no timeout at all.
                                               // A locked iPhone stops sending feedback,
                                               // and any limit would end the session
                                               // while the phone is just asleep.
    int          maxFps       = 60;            // -fps; 0 = UxPlay default (30, raop.c:623)
    std::wstring videoDecoder;                 // -vd; empty = decodebin (picks by rank)

    // [app]
    bool         alwaysOnTop       = false;
    bool         startMinimized    = false;
    bool         autostartReceiver = false;
    bool         showAdvanced      = false;   // collapsible sections, see PHASE2-UX-SPEC
    bool         showDetails       = false;
    bool         trayHintShown     = false;   // the "still running in the tray" balloon
    bool         hideWhenStalled   = true;    // hide the frozen picture while the phone sleeps
                                              // (no UI: an escape hatch, not a preference)
    std::wstring msysRoot;                     // empty => no runtime tree found
    std::wstring uxplayPath;                   // empty => not found, UI shows an error

    // [window]
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = 0, h = 0;
};

// Default uxplay.exe location: next to the GUI exe, else <exe dir>\..\build\uxplay.exe,
// else empty.
std::wstring defaultUxplayPath();

// Default runtime root: the portable tree at <exe dir>\ucrt64\bin if there is one, else
// C:\msys64 if MSYS2 is installed, else empty. The GUI never hardcodes C:\msys64 any more -
// a copied-to-another-machine install has no MSYS2 and carries its own ucrt64\ tree instead.
std::wstring defaultMsysRoot();

class ConfigStore {
public:
    ConfigStore();

    const std::wstring& path() const { return path_; }

    void load(AppConfig& cfg) const;
    void save(const AppConfig& cfg) const;

    // homeDir = %APPDATA%\airplay, gstRegistry = %LOCALAPPDATA%\airplay\gst-registry.bin
    static airplay::HostConfig toHostConfig(const AppConfig& cfg);

private:
    std::wstring path_;
};

} // namespace ui
