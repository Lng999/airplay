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
// "Windows açılışında başlat" task.
std::wstring startupShortcut() {
    PWSTR raw = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Startup, 0, nullptr, &raw)) && raw) {
        out = raw;
        if (!out.empty() && out.back() != L'\\') out.push_back(L'\\');
        out += L"airplay.lnk";
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
    const std::wstring lnk = startupShortcut();
    return !lnk.empty() && fileExists(lnk);
}

bool setLaunchAtLogon(bool on) {
    // The shortcut goes either way: when switching on it would be a duplicate launch, and
    // when switching off it is the half the user cannot see.
    const std::wstring lnk = startupShortcut();
    if (!lnk.empty() && fileExists(lnk)) DeleteFileW(lnk.c_str());

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
