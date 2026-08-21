#include "config_store.h"

#include <shlobj.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace ui {
namespace {

std::wstring knownFolder(REFKNOWNFOLDERID id, const wchar_t* envFallback) {
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &raw)) && raw) {
        std::wstring out(raw);
        CoTaskMemFree(raw);
        return out;
    }
    if (raw) CoTaskMemFree(raw);
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(envFallback, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return std::wstring(buf, n);
    return std::wstring();
}

std::wstring readStr(const std::wstring& file, const wchar_t* sec, const wchar_t* key,
                     const std::wstring& def) {
    std::vector<wchar_t> buf(1024);
    DWORD n = GetPrivateProfileStringW(sec, key, def.c_str(), buf.data(),
                                       static_cast<DWORD>(buf.size()), file.c_str());
    return std::wstring(buf.data(), n);
}

int readInt(const std::wstring& file, const wchar_t* sec, const wchar_t* key, int def) {
    wchar_t d[32];
    _snwprintf(d, 32, L"%d", def);
    std::wstring s = readStr(file, sec, key, d);
    if (s.empty()) return def;
    return static_cast<int>(wcstol(s.c_str(), nullptr, 10));
}

bool readBool(const std::wstring& file, const wchar_t* sec, const wchar_t* key, bool def) {
    return readInt(file, sec, key, def ? 1 : 0) != 0;
}

void writeStr(const std::wstring& file, const wchar_t* sec, const wchar_t* key,
              const std::wstring& val) {
    WritePrivateProfileStringW(sec, key, val.c_str(), file.c_str());
}

void writeInt(const std::wstring& file, const wchar_t* sec, const wchar_t* key, int val) {
    wchar_t b[32];
    _snwprintf(b, 32, L"%d", val);
    writeStr(file, sec, key, b);
}

} // namespace

// ---------------------------------------------------------------------------

std::string narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                        &out[0], n, nullptr, nullptr);
    return out;
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], n);
    return out;
}

std::wstring joinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    std::wstring out = a;
    if (out.back() != L'\\' && out.back() != L'/') out.push_back(L'\\');
    size_t i = 0;
    while (i < b.size() && (b[i] == L'\\' || b[i] == L'/')) ++i;
    out.append(b, i, std::wstring::npos);
    return out;
}

bool fileExists(const std::wstring& path) {
    if (path.empty()) return false;
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool ensureDir(const std::wstring& dir) {
    if (dir.empty()) return false;
    DWORD a = GetFileAttributesW(dir.c_str());
    if (a != INVALID_FILE_ATTRIBUTES) return (a & FILE_ATTRIBUTE_DIRECTORY) != 0;

    // create parents first
    size_t cut = dir.find_last_of(L"\\/");
    if (cut != std::wstring::npos && cut > 2) {
        std::wstring parent = dir.substr(0, cut);
        if (parent.size() > 2 && !ensureDir(parent)) return false;
    }
    if (CreateDirectoryW(dir.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

std::wstring exeDir() {
    std::vector<wchar_t> buf(MAX_PATH);
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return std::wstring();
        if (n < buf.size() - 1) {
            std::wstring p(buf.data(), n);
            size_t cut = p.find_last_of(L"\\/");
            return cut == std::wstring::npos ? std::wstring() : p.substr(0, cut);
        }
        buf.resize(buf.size() * 2);
    }
}

std::wstring roamingAppDir() {
    std::wstring base = knownFolder(FOLDERID_RoamingAppData, L"APPDATA");
    return base.empty() ? std::wstring() : joinPath(base, L"airplay");
}

std::wstring localAppDir() {
    std::wstring base = knownFolder(FOLDERID_LocalAppData, L"LOCALAPPDATA");
    return base.empty() ? std::wstring() : joinPath(base, L"airplay");
}

std::wstring defaultUxplayPath() {
    const std::wstring dir = exeDir();
    if (dir.empty()) return std::wstring();

    std::wstring sideBySide = joinPath(dir, L"uxplay.exe");
    if (fileExists(sideBySide)) return sideBySide;

    // Development layout: <repo>\build-app\airplay-gui.exe -> <repo>\build\uxplay.exe
    std::wstring inRepo = joinPath(joinPath(dir, L".."), L"build\\uxplay.exe");
    wchar_t full[MAX_PATH * 2];
    DWORD n = GetFullPathNameW(inRepo.c_str(), MAX_PATH * 2, full, nullptr);
    if (n > 0 && n < MAX_PATH * 2) inRepo.assign(full, n);
    if (fileExists(inRepo)) return inRepo;

    return std::wstring();
}

// ---------------------------------------------------------------------------

ConfigStore::ConfigStore() {
    std::wstring dir = roamingAppDir();
    if (!dir.empty()) {
        ensureDir(dir);
        path_ = joinPath(dir, L"config.ini");
    }
}

void ConfigStore::load(AppConfig& cfg) const {
    if (path_.empty()) return;
    const std::wstring& f = path_;

    cfg.name         = readStr (f, L"receiver", L"name",       cfg.name);
    cfg.port         = readInt (f, L"receiver", L"port",       cfg.port);
    cfg.videoSink    = readStr (f, L"receiver", L"video_sink", cfg.videoSink);
    cfg.audioSink    = readStr (f, L"receiver", L"audio_sink", cfg.audioSink);
    cfg.fullscreen   = readBool(f, L"receiver", L"fullscreen", cfg.fullscreen);
    cfg.h265         = readBool(f, L"receiver", L"h265",       cfg.h265);
    cfg.debug        = readBool(f, L"receiver", L"debug",      cfg.debug);
    cfg.noHold       = readBool(f, L"receiver", L"nohold",     cfg.noHold);
    cfg.resetSeconds = readInt (f, L"receiver", L"reset",      cfg.resetSeconds);

    cfg.maxFps            = readInt(f,  L"receiver", L"max_fps",       cfg.maxFps);
    cfg.videoDecoder      = readStr(f,  L"receiver", L"video_decoder", cfg.videoDecoder);
    cfg.fpsData           = readBool(f, L"receiver", L"fps_data",      cfg.fpsData);

    cfg.alwaysOnTop       = readBool(f, L"app", L"always_on_top",      cfg.alwaysOnTop);
    cfg.startMinimized    = readBool(f, L"app", L"start_minimized",    cfg.startMinimized);
    cfg.autostartReceiver = readBool(f, L"app", L"autostart_receiver", cfg.autostartReceiver);
    cfg.showAdvanced      = readBool(f, L"app", L"show_advanced",      cfg.showAdvanced);
    cfg.showDetails       = readBool(f, L"app", L"show_details",       cfg.showDetails);
    cfg.trayHintShown     = readBool(f, L"app", L"tray_hint_shown",    cfg.trayHintShown);
    cfg.hideWhenStalled   = readBool(f, L"app", L"hide_when_stalled",  cfg.hideWhenStalled);
    cfg.msysRoot          = readStr (f, L"app", L"msys_root",          cfg.msysRoot);
    cfg.uxplayPath        = readStr (f, L"app", L"uxplay_path",        L"");

    if (cfg.uxplayPath.empty() || !fileExists(cfg.uxplayPath))
        cfg.uxplayPath = defaultUxplayPath();

    cfg.x = readInt(f, L"window", L"x", cfg.x);
    cfg.y = readInt(f, L"window", L"y", cfg.y);
    cfg.w = readInt(f, L"window", L"w", cfg.w);
    cfg.h = readInt(f, L"window", L"h", cfg.h);

    if (cfg.port < 0 || cfg.port > 65533) cfg.port = 7100;
    if (cfg.resetSeconds < 0) cfg.resetSeconds = 0;
    if (cfg.name.empty()) cfg.name = L"AirPlay-PC";
}

void ConfigStore::save(const AppConfig& cfg) const {
    if (path_.empty()) return;
    const std::wstring& f = path_;

    writeStr(f, L"receiver", L"name",       cfg.name);
    writeInt(f, L"receiver", L"port",       cfg.port);
    writeStr(f, L"receiver", L"video_sink", cfg.videoSink);
    writeStr(f, L"receiver", L"audio_sink", cfg.audioSink);
    writeInt(f, L"receiver", L"fullscreen", cfg.fullscreen ? 1 : 0);
    writeInt(f, L"receiver", L"h265",       cfg.h265 ? 1 : 0);
    writeInt(f, L"receiver", L"debug",      cfg.debug ? 1 : 0);
    writeInt(f, L"receiver", L"nohold",     cfg.noHold ? 1 : 0);
    writeInt(f, L"receiver", L"reset",      cfg.resetSeconds);

    writeInt(f, L"receiver", L"max_fps",       cfg.maxFps);
    writeStr(f, L"receiver", L"video_decoder", cfg.videoDecoder);
    writeInt(f, L"receiver", L"fps_data",      cfg.fpsData ? 1 : 0);

    writeInt(f, L"app", L"always_on_top",      cfg.alwaysOnTop ? 1 : 0);
    writeInt(f, L"app", L"start_minimized",    cfg.startMinimized ? 1 : 0);
    writeInt(f, L"app", L"autostart_receiver", cfg.autostartReceiver ? 1 : 0);
    writeInt(f, L"app", L"show_advanced",      cfg.showAdvanced ? 1 : 0);
    writeInt(f, L"app", L"show_details",       cfg.showDetails ? 1 : 0);
    writeInt(f, L"app", L"tray_hint_shown",    cfg.trayHintShown ? 1 : 0);
    writeInt(f, L"app", L"hide_when_stalled",  cfg.hideWhenStalled ? 1 : 0);
    writeStr(f, L"app", L"msys_root",          cfg.msysRoot);
    writeStr(f, L"app", L"uxplay_path",        cfg.uxplayPath);

    writeInt(f, L"window", L"x", cfg.x);
    writeInt(f, L"window", L"y", cfg.y);
    writeInt(f, L"window", L"w", cfg.w);
    writeInt(f, L"window", L"h", cfg.h);

    WritePrivateProfileStringW(nullptr, nullptr, nullptr, f.c_str());  // flush the cache
}

airplay::HostConfig ConfigStore::toHostConfig(const AppConfig& cfg) {
    airplay::HostConfig hc;
    hc.uxplayExe    = cfg.uxplayPath;
    hc.msysRoot     = cfg.msysRoot;
    hc.homeDir      = roamingAppDir();
    hc.gstRegistry  = joinPath(localAppDir(), L"gst-registry.bin");
    hc.name         = narrow(cfg.name);
    hc.port         = cfg.port;
    hc.videoSink    = narrow(cfg.videoSink);
    hc.audioSink    = narrow(cfg.audioSink);
    hc.fullscreen   = cfg.fullscreen;
    hc.h265         = cfg.h265;
    hc.debug        = cfg.debug;
    hc.noHold       = cfg.noHold;
    hc.resetSeconds = cfg.resetSeconds;
    hc.maxFps       = cfg.maxFps;
    hc.videoDecoder = narrow(cfg.videoDecoder);
    hc.fpsData      = cfg.fpsData;
    return hc;
}

} // namespace ui
