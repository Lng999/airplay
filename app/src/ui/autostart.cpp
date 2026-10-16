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

// installer/airplay.iss puts "{userstartup}\airplay.lnk" there when the user ticks the
// "Windows açılışında başlat" task. Installers up to v0.4.0 named it after the app instead,
// and such a shortcut survives until the next install, so both names count as ours.
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

bool runValueExists() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    const LONG rc = RegQueryValueExW(key, kRunValue, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

} // namespace

bool isLaunchAtLogon() {
    if (runValueExists()) return true;
    const std::wstring dir = startupDir();
    if (dir.empty()) return false;
    for (const wchar_t* name : kStartupShortcuts)
        if (fileExists(dir + name)) return true;
    return false;
}

bool setLaunchAtLogon(bool on) {
    // The shortcuts go either way: when switching on they would be a duplicate launch, and
    // when switching off they are the half the user cannot see.
    const std::wstring dir = startupDir();
    if (!dir.empty()) {
        for (const wchar_t* name : kStartupShortcuts) {
            const std::wstring lnk = dir + name;
            if (fileExists(lnk)) DeleteFileW(lnk.c_str());
        }
    }

    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
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

} // namespace ui
