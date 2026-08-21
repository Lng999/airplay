#include "config_store.h"   // must come first: winsock2 before windows.h, and the helpers

#include "updater.h"
#include "version.h"

#include <winhttp.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace ui {
namespace {

const wchar_t* kUserAgent = L"airplay-gui/" AIRPLAY_VERSION_WSTR;

void setErr(std::string* err, const std::string& msg) {
    if (err) *err = msg;
}

void setWinErr(std::string* err, const char* what) {
    if (!err) return;
    char b[160];
    std::snprintf(b, sizeof(b), "%s failed (%lu)", what, GetLastError());
    *err = b;
}

// --- the small JSON scanner -----------------------------------------------------------------
// GitHub returns compact JSON with no whitespace around ':', but be tolerant anyway.

std::string jsonUnescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) { out.push_back(s[i]); continue; }
        switch (s[++i]) {
            case 'n':  out.push_back('\n'); break;
            case 'r':  break;                       // CRLF -> LF
            case 't':  out.push_back('\t'); break;
            case '"':  out.push_back('"');  break;
            case '\\': out.push_back('\\'); break;
            case '/':  out.push_back('/');  break;
            case 'u': {
                // Only the BMP, and only as UTF-8. Release notes are the one field that can
                // carry these; surrogate pairs in them are rare enough to pass through as-is.
                if (i + 4 >= s.size()) break;
                const unsigned cp = std::strtoul(s.substr(i + 1, 4).c_str(), nullptr, 16);
                i += 4;
                if (cp < 0x80) {
                    out.push_back(static_cast<char>(cp));
                } else if (cp < 0x800) {
                    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                } else {
                    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
                break;
            }
            default: out.push_back(s[i]); break;
        }
    }
    return out;
}

// Value of the first `"key": "..."` at or after `from`. `end` receives the offset just past
// the closing quote, so callers can keep scanning. Returns false when the key is not there.
bool findString(const std::string& doc, size_t from, const char* key, std::string& value,
                size_t* end = nullptr) {
    const std::string needle = std::string("\"") + key + "\"";
    size_t k = doc.find(needle, from);
    if (k == std::string::npos) return false;
    size_t c = doc.find(':', k + needle.size());
    if (c == std::string::npos) return false;
    size_t q = doc.find('"', c + 1);
    if (q == std::string::npos) return false;
    size_t p = q + 1;
    std::string raw;
    for (; p < doc.size(); ++p) {
        if (doc[p] == '\\' && p + 1 < doc.size()) { raw.push_back(doc[p]); raw.push_back(doc[p + 1]); ++p; continue; }
        if (doc[p] == '"') break;
        raw.push_back(doc[p]);
    }
    if (p >= doc.size()) return false;
    value = jsonUnescape(raw);
    if (end) *end = p + 1;
    return true;
}

// --- WinHTTP ----------------------------------------------------------------------------------

struct Session {
    HINTERNET h = nullptr;
    ~Session() { if (h) WinHttpCloseHandle(h); }
};
struct Handle {
    HINTERNET h = nullptr;
    ~Handle() { if (h) WinHttpCloseHandle(h); }
};

// Opens `url` and leaves the response ready to be read. Follows redirects (WinHTTP does that
// by default), which is what makes browser_download_url work: it 302s to a CDN host.
bool openUrl(const std::wstring& url, Session& session, Handle& conn, Handle& req,
             std::string* err) {
    URL_COMPONENTS uc{};
    uc.dwStructSize     = sizeof(uc);
    wchar_t host[256]{}, path[2048]{};
    uc.lpszHostName     = host;  uc.dwHostNameLength     = 255;
    uc.lpszUrlPath      = path;  uc.dwUrlPathLength      = 2047;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) { setWinErr(err, "WinHttpCrackUrl"); return false; }

    session.h = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h) { setWinErr(err, "WinHttpOpen"); return false; }

    // Two minutes end to end: enough for a 75 MB installer on a slow line, short enough that
    // a dead network does not leave the thread hanging for the life of the process.
    WinHttpSetTimeouts(session.h, 15000, 15000, 30000, 120000);

    conn.h = WinHttpConnect(session.h, host, uc.nPort, 0);
    if (!conn.h) { setWinErr(err, "WinHttpConnect"); return false; }

    const DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    req.h = WinHttpOpenRequest(conn.h, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                               WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req.h) { setWinErr(err, "WinHttpOpenRequest"); return false; }

    const wchar_t* headers = L"Accept: application/vnd.github+json\r\n"
                             L"X-GitHub-Api-Version: 2022-11-28\r\n";
    if (!WinHttpSendRequest(req.h, headers, static_cast<DWORD>(-1),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        setWinErr(err, "WinHttpSendRequest");
        return false;
    }
    if (!WinHttpReceiveResponse(req.h, nullptr)) { setWinErr(err, "WinHttpReceiveResponse"); return false; }

    DWORD status = 0, len = sizeof(status);
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        char b[96];
        std::snprintf(b, sizeof(b), "HTTP %lu", status);
        setErr(err, b);
        return false;
    }
    return true;
}

bool httpGet(const std::wstring& url, std::string& body, std::string* err) {
    Session s; Handle c, r;
    if (!openUrl(url, s, c, r, err)) return false;

    body.clear();
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(r.h, &avail)) { setWinErr(err, "WinHttpQueryDataAvailable"); return false; }
        if (avail == 0) break;
        std::vector<char> buf(avail);
        DWORD read = 0;
        if (!WinHttpReadData(r.h, buf.data(), avail, &read)) { setWinErr(err, "WinHttpReadData"); return false; }
        if (read == 0) break;
        body.append(buf.data(), read);
        if (body.size() > 4u * 1024 * 1024) break;   // the API response is ~30 KB
    }
    return true;
}

} // namespace

// ------------------------------------------------------------------------------------------

int compareVersions(const std::wstring& a, const std::wstring& b) {
    size_t i = 0, j = 0;
    for (int part = 0; part < 4; ++part) {
        long x = 0, y = 0;
        while (i < a.size() && a[i] >= L'0' && a[i] <= L'9') x = x * 10 + (a[i++] - L'0');
        while (j < b.size() && b[j] >= L'0' && b[j] <= L'9') y = y * 10 + (b[j++] - L'0');
        if (x != y) return x < y ? -1 : 1;
        if (i < a.size() && a[i] == L'.') ++i; else i = a.size();
        if (j < b.size() && b[j] == L'.') ++j; else j = b.size();
        if (i >= a.size() && j >= b.size()) break;
    }
    return 0;
}

std::wstring currentVersion() { return AIRPLAY_VERSION_WSTR; }

bool checkForUpdate(UpdateInfo& out, std::string* err) {
    setErr(err, "");

    const std::wstring url = L"https://api.github.com/repos/" AIRPLAY_GH_OWNER_W L"/"
                             AIRPLAY_GH_REPO_W L"/releases/latest";
    std::string body;
    if (!httpGet(url, body, err)) return false;

    std::string tag;
    if (!findString(body, 0, "tag_name", tag)) { setErr(err, "tag_name not in the response"); return false; }

    std::wstring version = widen(tag);
    if (!version.empty() && (version[0] == L'v' || version[0] == L'V')) version.erase(0, 1);
    if (compareVersions(currentVersion(), version) >= 0) return false;   // up to date

    out = UpdateInfo{};
    out.version = version;

    std::string s;
    if (findString(body, 0, "html_url", s)) out.pageUrl = widen(s);
    if (findString(body, 0, "body", s)) {
        // Four lines is enough to say what changed inside a message box.
        std::string trimmed;
        int lines = 0;
        for (char ch : s) {
            if (ch == '\n' && ++lines >= 4) break;
            trimmed.push_back(ch);
        }
        out.notes = widen(trimmed);
    }

    // The installer is the one .exe attached to the release. Scan every asset's
    // browser_download_url rather than trusting a name, so renaming the file is harmless.
    size_t pos = body.find("\"assets\"");
    if (pos == std::string::npos) pos = 0;
    std::string dl;
    size_t next = pos;
    while (findString(body, next, "browser_download_url", dl, &next)) {
        if (dl.size() > 4 && dl.compare(dl.size() - 4, 4, ".exe") == 0) {
            out.downloadUrl = widen(dl);
            break;
        }
    }
    return true;
}

bool downloadFile(const std::wstring& url, const std::wstring& destPath, std::string* err) {
    setErr(err, "");
    Session s; Handle c, r;
    if (!openUrl(url, s, c, r, err)) return false;

    const std::wstring partPath = destPath + L".part";
    HANDLE f = CreateFileW(partPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { setWinErr(err, "CreateFile"); return false; }

    bool ok = true;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(r.h, &avail)) { setWinErr(err, "WinHttpQueryDataAvailable"); ok = false; break; }
        if (avail == 0) break;
        std::vector<char> buf(avail);
        DWORD read = 0;
        if (!WinHttpReadData(r.h, buf.data(), avail, &read)) { setWinErr(err, "WinHttpReadData"); ok = false; break; }
        if (read == 0) break;
        DWORD written = 0;
        if (!WriteFile(f, buf.data(), read, &written, nullptr) || written != read) {
            setWinErr(err, "WriteFile"); ok = false; break;
        }
    }
    CloseHandle(f);

    if (!ok) { DeleteFileW(partPath.c_str()); return false; }

    DeleteFileW(destPath.c_str());
    if (!MoveFileW(partPath.c_str(), destPath.c_str())) {
        setWinErr(err, "MoveFile");
        DeleteFileW(partPath.c_str());
        return false;
    }
    return true;
}

std::wstring installerDownloadPath(const std::wstring& version) {
    wchar_t tmp[MAX_PATH]{};
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = (n > 0 && n < MAX_PATH) ? std::wstring(tmp, n) : std::wstring(L".\\");
    dir = joinPath(dir, L"airplay-update");
    ensureDir(dir);
    return joinPath(dir, L"AirPlay-Setup-" + version + L".exe");
}

} // namespace ui
