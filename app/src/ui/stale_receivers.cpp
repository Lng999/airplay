#include "stale_receivers.h"

#include <tlhelp32.h>

#include <cwctype>

namespace ui {
namespace {

bool samePath(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        wchar_t ca = static_cast<wchar_t>(std::towlower(a[i]));
        wchar_t cb = static_cast<wchar_t>(std::towlower(b[i]));
        if (ca == L'/') ca = L'\\';
        if (cb == L'/') cb = L'\\';
        if (ca != cb) return false;
    }
    return true;
}

// Full image path of a pid, or empty when it cannot be read (access denied, already gone).
std::wstring imagePath(HANDLE process) {
    wchar_t buf[MAX_PATH * 2];
    DWORD len = static_cast<DWORD>(ARRAYSIZE(buf));
    if (!QueryFullProcessImageNameW(process, 0, buf, &len)) return std::wstring();
    return std::wstring(buf, len);
}

} // namespace

int killStaleReceivers(const std::wstring& exePath) {
    if (exePath.empty()) return 0;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    // Match on the file name first (cheap) and only then open the process to compare the
    // full path - so an unrelated uxplay.exe from somewhere else is left alone.
    const size_t slash = exePath.find_last_of(L"\\/");
    const std::wstring leaf = (slash == std::wstring::npos) ? exePath : exePath.substr(slash + 1);

    const DWORD self = GetCurrentProcessId();
    int killed = 0;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snap, &entry)) {
        do {
            if (entry.th32ProcessID == self) continue;
            if (!samePath(entry.szExeFile, leaf)) continue;

            HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE,
                                      FALSE, entry.th32ProcessID);
            if (!proc) continue;
            if (samePath(imagePath(proc), exePath) && TerminateProcess(proc, 0)) ++killed;
            CloseHandle(proc);
        } while (Process32NextW(snap, &entry));
    }
    CloseHandle(snap);
    return killed;
}

} // namespace ui
