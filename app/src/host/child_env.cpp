// child_env.cpp — see child_env.h.
#include "child_env.h"

#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <utility>

namespace airplay {
namespace {

std::wstring trimTrailingSlashes(std::wstring s) {
    while (!s.empty() && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
    return s;
}

std::wstring parentDir(const std::wstring& path) {
    const size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return std::wstring();
    if (pos == 0) return path.substr(0, 1);
    return path.substr(0, pos);
}

// An environment entry looks like "NAME=VALUE". Windows also stores per-drive current directories
// as "=C:=C:\some\dir"; for those the name is "=C:". Keep them, CMD relies on them.
std::wstring entryName(const std::wstring& entry) {
    const size_t start = entry.empty() || entry[0] != L'=' ? 0 : 1;
    const size_t eq = entry.find(L'=', start);
    return eq == std::wstring::npos ? entry : entry.substr(0, eq);
}

void setVar(std::vector<std::pair<std::wstring, std::wstring>>& vars,
            const std::wstring& name, const std::wstring& value) {
    for (auto& kv : vars) {
        if (_wcsicmp(kv.first.c_str(), name.c_str()) == 0) {
            kv.second = value;
            return;
        }
    }
    vars.emplace_back(name, value);
}

std::wstring getVar(const std::vector<std::pair<std::wstring, std::wstring>>& vars,
                    const std::wstring& name) {
    for (const auto& kv : vars) {
        if (_wcsicmp(kv.first.c_str(), name.c_str()) == 0) return kv.second;
    }
    return std::wstring();
}

void note(std::string* err, const std::string& text) {
    if (!err) return;
    if (!err->empty()) err->append("; ");
    err->append(text);
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n < 0 ? 0 : n), '\0');
    if (n > 0) {
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                            &out[0], n, nullptr, nullptr);
    }
    return out;
}

} // namespace

bool ensureDirectoryW(const std::wstring& dir) {
    if (dir.empty()) return false;
    const DWORD attr = GetFileAttributesW(dir.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;

    const std::wstring parent = parentDir(dir);
    if (!parent.empty() && parent != dir && parent.find(L'\\') != std::wstring::npos) {
        ensureDirectoryW(parent);
    }
    if (CreateDirectoryW(dir.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

std::vector<wchar_t> buildChildEnvironment(const ChildEnvConfig& cfg, std::string* err) {
    std::vector<std::pair<std::wstring, std::wstring>> vars;

    // --- inherit the parent environment ----------------------------------------------------------
    if (LPWCH block = GetEnvironmentStringsW()) {
        for (LPWCH p = block; *p; ) {
            const std::wstring entry(p);
            p += entry.size() + 1;
            const std::wstring name = entryName(entry);
            const std::wstring value = entry.size() > name.size() ? entry.substr(name.size() + 1)
                                                                  : std::wstring();
            vars.emplace_back(name, value);
        }
        FreeEnvironmentStringsW(block);
    }

    const std::wstring root = trimTrailingSlashes(cfg.msysRoot);

    // --- PATH ------------------------------------------------------------------------------------
    if (!root.empty()) {
        const std::wstring ucrtBin = root + L"\\ucrt64\\bin";
        std::wstring path = getVar(vars, L"PATH");
        setVar(vars, L"PATH", path.empty() ? ucrtBin : ucrtBin + L";" + path);
        setVar(vars, L"GST_PLUGIN_SYSTEM_PATH", root + L"\\ucrt64\\lib\\gstreamer-1.0");
    }

    // --- HOME / XDG_CONFIG_HOMEDIR (docs/DESIGN.md §5.3) -----------------------------------------
    if (!cfg.homeDir.empty()) {
        const std::wstring home = trimTrailingSlashes(cfg.homeDir);
        if (!ensureDirectoryW(home)) {
            note(err, "could not create HOME directory " + narrow(home));
        }
        setVar(vars, L"HOME", home);
        setVar(vars, L"XDG_CONFIG_HOMEDIR", home);
    }

    // --- GST_REGISTRY ----------------------------------------------------------------------------
    if (!cfg.gstRegistry.empty()) {
        const std::wstring parent = parentDir(cfg.gstRegistry);
        if (!parent.empty() && !ensureDirectoryW(parent)) {
            note(err, "could not create GST_REGISTRY directory " + narrow(parent));
        }
        setVar(vars, L"GST_REGISTRY", cfg.gstRegistry);
    }

    // --- sort case-insensitively -----------------------------------------------------------------
    std::stable_sort(vars.begin(), vars.end(),
                     [](const std::pair<std::wstring, std::wstring>& a,
                        const std::pair<std::wstring, std::wstring>& b) {
                         return _wcsicmp(a.first.c_str(), b.first.c_str()) < 0;
                     });

    // --- flatten -----------------------------------------------------------------------------------
    std::vector<wchar_t> out;
    for (const auto& kv : vars) {
        if (kv.first.empty()) continue;
        out.insert(out.end(), kv.first.begin(), kv.first.end());
        out.push_back(L'=');
        out.insert(out.end(), kv.second.begin(), kv.second.end());
        out.push_back(L'\0');
    }
    out.push_back(L'\0');   // block terminator
    return out;
}

} // namespace airplay
