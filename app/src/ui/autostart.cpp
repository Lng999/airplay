#include "autostart.h"

#include <shlobj.h>

namespace ui {
namespace {

const wchar_t* const kRunKey   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* const kRunValue = L"airplay";

std::wstring exePath() {
    wchar_t buf[MAX_PATH]{};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return (n == 0 || n >= MAX_PATH) ? std::wstring() : std::wstring(buf, n);
}

// Leftovers only. installer/airplay.iss no longer creates a Startup shortcut: its
// "Windows açılışında başlat" task writes the same Run value as below (kRunKey/kRunValue,
// "<exe>" -minimized). The v0.3.0 installer put "AirPlay Alicisi.lnk" in the Startup folder,
// and unreleased builds of the script put "airplay.lnk". migrateLegacyStartupShortcut() turns
// either into the Run value; until it has run they still count as ours, and toggling the box
// removes them too.
const wchar_t* const kStartupShortcuts[] = {L"airplay.lnk", L"AirPlay Alicisi.lnk"};

// The per-user Startup folder with a trailing backslash, or empty if it cannot be resolved.
std::wstring startupDir() {
    PWSTR raw = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Startup, 0, nullptr, &raw)) && raw) {
        out = raw;
        if (!out.empty() && out.back() != L'\\') out.push_back(L'\\');
    }
    if (raw) CoTaskMemFree(raw);
    return out;
}

// override, normalised to a trailing backslash, or the real Startup folder when it is empty.
std::wstring resolveStartupDir(const std::wstring& override) {
    if (override.empty()) return startupDir();
    std::wstring out = override;
    if (out.back() != L'\\' && out.back() != L'/') out.push_back(L'\\');
    return out;
}

bool anyShortcutExists(const std::wstring& dir) {
    if (dir.empty()) return false;
    for (const wchar_t* name : kStartupShortcuts)
        if (fileExists(dir + name)) return true;
    return false;
}

void deleteShortcuts(const std::wstring& dir) {
    if (dir.empty()) return;
    for (const wchar_t* name : kStartupShortcuts) {
        const std::wstring lnk = dir + name;
        if (fileExists(lnk)) DeleteFileW(lnk.c_str());
    }
}

bool runValueExists(const wchar_t* runKey) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, runKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    const LONG rc = RegQueryValueExW(key, kRunValue, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

// on: runKey\airplay = "<our exe>" -minimized. off: that value removed (already gone is fine).
bool writeRunValue(const wchar_t* runKey, bool on) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, runKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS)
        return false;

    LONG rc;
    if (on) {
        const std::wstring exe = exePath();
        if (exe.empty()) { RegCloseKey(key); return false; }
        // -minimized: coming up at logon should land in the notification area, not open a
        // window over whatever the user is doing.
        const std::wstring cmd = L"\"" + exe + L"\" -minimized";
        rc = RegSetValueExW(key, kRunValue, 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(cmd.c_str()),
                            static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        rc = RegDeleteValueW(key, kRunValue);
        if (rc == ERROR_FILE_NOT_FOUND) rc = ERROR_SUCCESS;   // already gone is success
    }
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

} // namespace

bool isLaunchAtLogon() {
    return runValueExists(kRunKey) || anyShortcutExists(startupDir());
}

bool setLaunchAtLogon(bool on) {
    // The shortcuts go either way: when switching on they would be a duplicate launch, and
    // when switching off they are the half the user cannot see.
    deleteShortcuts(startupDir());
    return writeRunValue(kRunKey, on);
}

void migrateLegacyStartupShortcut(const std::wstring& startupDirOverride,
                                  const std::wstring& runKeyOverride) {
    const std::wstring dir = resolveStartupDir(startupDirOverride);
    if (!anyShortcutExists(dir)) return;   // nothing to migrate: the common case, every launch

    const wchar_t* runKey = runKeyOverride.empty() ? kRunKey : runKeyOverride.c_str();
    // The shortcut is the only record that the user wanted this; never drop it before the
    // Run value is in place. A failed write leaves everything as it was for the next launch.
    if (!runValueExists(runKey) && !writeRunValue(runKey, true)) return;
    deleteShortcuts(dir);
}

} // namespace ui
