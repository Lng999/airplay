#include "config_store.h"   // must come first: winsock2 before windows.h, and the helpers

#include "updater.h"
#include "version.h"

#include <winhttp.h>
#include <bcrypt.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
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

bool isJsonWs(char ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; }

size_t skipWs(const std::string& d, size_t i) {
    while (i < d.size() && isJsonWs(d[i])) ++i;
    return i;
}

// `i` is on an opening quote; returns the offset just past the closing one, or npos.
size_t skipString(const std::string& d, size_t i) {
    for (++i; i < d.size(); ++i) {
        if (d[i] == '\\') { ++i; continue; }
        if (d[i] == '"') return i + 1;
    }
    return std::string::npos;
}

// `i` is on the first character of any JSON value; returns the offset just past it, or npos.
size_t skipValue(const std::string& d, size_t i) {
    if (i >= d.size()) return std::string::npos;
    if (d[i] == '"') return skipString(d, i);
    if (d[i] != '{' && d[i] != '[') {
        while (i < d.size() && d[i] != ',' && d[i] != '}' && d[i] != ']') ++i;
        return i;
    }
    int depth = 0;
    while (i < d.size()) {
        const char ch = d[i];
        if (ch == '"') {
            i = skipString(d, i);
            if (i == std::string::npos) return i;
            continue;
        }
        if (ch == '{' || ch == '[') ++depth;
        else if ((ch == '}' || ch == ']') && --depth == 0) return i + 1;
        ++i;
    }
    return std::string::npos;
}

// `i` is on an opening quote; the unescaped string (UTF-8). False if it never closes.
bool readString(const std::string& d, size_t i, std::string& value) {
    const size_t e = skipString(d, i);
    if (e == std::string::npos) return false;
    value = jsonUnescape(d.substr(i + 1, e - i - 2));
    return true;
}

// Offset of the value of member `key` of the object whose '{' is at `open`, or npos. Only
// that object's own members count: keys of nested objects (an asset's "uploader") are skipped.
size_t memberValue(const std::string& d, size_t open, const char* key) {
    if (open >= d.size() || d[open] != '{') return std::string::npos;
    size_t i = skipWs(d, open + 1);
    while (i < d.size() && d[i] == '"') {
        const size_t keyEnd = skipString(d, i);
        if (keyEnd == std::string::npos) return std::string::npos;
        const size_t colon = skipWs(d, keyEnd);
        if (colon >= d.size() || d[colon] != ':') return std::string::npos;
        const size_t value = skipWs(d, colon + 1);
        if (d.compare(i + 1, keyEnd - i - 2, key) == 0 && std::strlen(key) == keyEnd - i - 2)
            return value;
        i = skipWs(d, skipValue(d, value));
        if (i >= d.size() || d[i] != ',') return std::string::npos;
        i = skipWs(d, i + 1);
    }
    return std::string::npos;
}

} // namespace

bool findString(const std::string& doc, size_t from, const char* key, std::string& value,
                size_t* end) {
    const std::string needle = std::string("\"") + key + "\"";
    size_t k = doc.find(needle, from);
    if (k == std::string::npos) return false;
    // Only whitespace may sit between the key and ':', and between ':' and the opening quote;
    // anything else (null, a number, an object) means the value is not a string.
    const size_t c = skipWs(doc, k + needle.size());
    if (c >= doc.size() || doc[c] != ':') return false;
    const size_t q = skipWs(doc, c + 1);
    if (q >= doc.size() || doc[q] != '"') return false;
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

// --- URL allowlist ------------------------------------------------------------------------------

bool isAllowedUpdateUrl(const std::wstring& url) {
    static const wchar_t kScheme[] = L"https://";
    const size_t schemeLen = sizeof(kScheme) / sizeof(kScheme[0]) - 1;
    if (url.size() <= schemeLen) return false;
    for (size_t i = 0; i < schemeLen; ++i) {
        if (towlower(url[i]) != kScheme[i]) return false;
    }

    // The authority runs to the first '/', '?' or '#'. Every character in it must be a plain
    // hostname character: that alone rules out userinfo ("github.com@evil"), ports, IPv6
    // literals, percent-escapes and backslash tricks without having to reason about each.
    std::wstring host;
    size_t i = schemeLen;
    for (; i < url.size(); ++i) {
        const wchar_t ch = url[i];
        if (ch == L'/' || ch == L'?' || ch == L'#') break;
        const wchar_t lc = static_cast<wchar_t>(towlower(ch));
        const bool ok = (lc >= L'a' && lc <= L'z') || (lc >= L'0' && lc <= L'9') || lc == L'.' ||
                        lc == L'-';
        if (!ok) return false;
        host.push_back(lc);
    }

    static const wchar_t* const kHosts[] = {
        L"github.com",
        L"objects.githubusercontent.com",
        L"release-assets.githubusercontent.com",
    };
    for (const wchar_t* h : kHosts) {
        if (host == h) return true;
    }
    return false;
}

// --- release assets and checksums ------------------------------------------------------------

std::vector<ReleaseAsset> parseReleaseAssets(const std::string& doc) {
    std::vector<ReleaseAsset> out;
    size_t i = memberValue(doc, skipWs(doc, 0), "assets");
    if (i >= doc.size() || doc[i] != '[') return out;
    i = skipWs(doc, i + 1);
    while (i < doc.size() && doc[i] != ']') {
        if (doc[i] == '{') {
            ReleaseAsset a;
            const size_t n = memberValue(doc, i, "name");
            const size_t u = memberValue(doc, i, "browser_download_url");
            if (n < doc.size() && u < doc.size() && doc[n] == '"' && doc[u] == '"' &&
                readString(doc, n, a.name) && readString(doc, u, a.url)) {
                out.push_back(a);
            }
        }
        i = skipWs(doc, skipValue(doc, i));
        if (i >= doc.size() || doc[i] != ',') break;
        i = skipWs(doc, i + 1);
    }
    return out;
}

namespace {

int hexDigit(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

} // namespace

bool parseSha256File(const std::string& content, std::string& hex) {
    size_t i = 0;
    if (content.compare(0, 3, "\xEF\xBB\xBF") == 0) i = 3;   // a BOM from an editor
    while (i < content.size() && isJsonWs(content[i])) ++i;
    if (content.size() - i < 64) return false;
    std::string h;
    for (size_t k = 0; k < 64; ++k) {
        const int v = hexDigit(content[i + k]);
        if (v < 0) return false;
        h.push_back("0123456789abcdef"[v]);
    }
    // The digest must end there: "<hex>  <name>", "<hex> *<name>" or the bare digest.
    if (i + 64 < content.size() && !isJsonWs(content[i + 64])) return false;
    hex = h;
    return true;
}

bool hexEquals(const std::string& a, const std::string& b) {
    if (a.empty() || a.size() != b.size()) return false;
    for (size_t k = 0; k < a.size(); ++k) {
        const int x = hexDigit(a[k]), y = hexDigit(b[k]);
        if (x < 0 || x != y) return false;
    }
    return true;
}

namespace {

// Incremental SHA-256 over CNG, so the installer is hashed as it streams to disk.
class Sha256 {
public:
    Sha256() {
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg_, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
            alg_ = nullptr;
            return;
        }
        if (!BCRYPT_SUCCESS(BCryptCreateHash(alg_, &hash_, nullptr, 0, nullptr, 0, 0))) hash_ = nullptr;
    }
    ~Sha256() {
        if (hash_) BCryptDestroyHash(hash_);
        if (alg_) BCryptCloseAlgorithmProvider(alg_, 0);
    }
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;

    bool update(const void* data, size_t len) {
        if (!hash_) return false;
        auto* p = static_cast<PUCHAR>(const_cast<void*>(data));
        while (len > 0) {
            const ULONG chunk = len > 0x40000000u ? 0x40000000u : static_cast<ULONG>(len);
            if (!BCRYPT_SUCCESS(BCryptHashData(hash_, p, chunk, 0))) return false;
            p += chunk;
            len -= chunk;
        }
        return true;
    }
    // Lower-case hex digest, or "" if CNG failed anywhere along the way. Call once.
    std::string finishHex() {
        if (!hash_) return {};
        UCHAR digest[32];
        if (!BCRYPT_SUCCESS(BCryptFinishHash(hash_, digest, sizeof(digest), 0))) return {};
        std::string hex;
        for (UCHAR b : digest) {
            hex.push_back("0123456789abcdef"[b >> 4]);
            hex.push_back("0123456789abcdef"[b & 0x0F]);
        }
        return hex;
    }

private:
    BCRYPT_ALG_HANDLE  alg_  = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
};

} // namespace

std::string sha256Hex(const void* data, size_t len) {
    Sha256 h;
    if (!h.update(data, len)) return {};
    return h.finishHex();
}

std::wstring checksumUrlFor(const std::wstring& installerUrl) { return installerUrl + L".sha256"; }

namespace {

// --- WinHTTP ----------------------------------------------------------------------------------

struct Session {
    HINTERNET h = nullptr;
    ~Session() { if (h) WinHttpCloseHandle(h); }
};
struct Handle {
    HINTERNET h = nullptr;
    ~Handle() { if (h) WinHttpCloseHandle(h); }
};

// The URL a request ended up at, after WinHTTP followed any redirects. Empty on failure.
std::wstring finalUrl(HINTERNET req) {
    DWORD bytes = 0;
    WinHttpQueryOption(req, WINHTTP_OPTION_URL, nullptr, &bytes);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0) return {};
    std::wstring buf(bytes / sizeof(wchar_t) + 1, L'\0');
    if (!WinHttpQueryOption(req, WINHTTP_OPTION_URL, &buf[0], &bytes)) return {};
    buf.resize(bytes / sizeof(wchar_t));
    while (!buf.empty() && buf.back() == L'\0') buf.pop_back();
    return buf;
}

// Opens `url` and leaves the response ready to be read. Follows redirects (WinHTTP does that
// by default), which is what makes browser_download_url work: it 302s to a CDN host.
// `allowlisted`: the URL came out of an API response, so both it and wherever the redirects
// end must pass isAllowedUpdateUrl. (The API URL itself is a compile-time constant.)
bool openUrl(const std::wstring& url, bool allowlisted, Session& session, Handle& conn,
             Handle& req, std::string* err) {
    if (allowlisted && !isAllowedUpdateUrl(url)) {
        setErr(err, "untrusted download URL");
        return false;
    }

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

    // Checked before a single body byte is read: a redirect off GitHub is refused outright.
    if (allowlisted && !isAllowedUpdateUrl(finalUrl(req.h))) {
        setErr(err, "redirected to an untrusted host");
        return false;
    }

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

bool httpGet(const std::wstring& url, bool allowlisted, std::string& body, std::string* err) {
    Session s; Handle c, r;
    if (!openUrl(url, allowlisted, s, c, r, err)) return false;

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
    if (!httpGet(url, false, body, err)) return false;

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

    // The installer is the one .exe attached to the release, judged by its download URL so
    // renaming the file is harmless. Its checksum is the asset named "<installer>.sha256"
    // (scripts/publish-release.ps1), which GitHub serves at the installer's URL + ".sha256".
    const std::vector<ReleaseAsset> assets = parseReleaseAssets(body);
    const ReleaseAsset* exe = nullptr;
    for (const ReleaseAsset& a : assets) {
        if (a.url.size() > 4 && a.url.compare(a.url.size() - 4, 4, ".exe") == 0) { exe = &a; break; }
    }
    bool hasChecksum = false;
    if (exe) {
        out.downloadUrl = widen(exe->url);
        for (const ReleaseAsset& a : assets) {
            if (a.name == exe->name + ".sha256" && widen(a.url) == checksumUrlFor(out.downloadUrl)) {
                hasChecksum = true;
                break;
            }
        }
    }

    // Both URLs are about to be fetched or handed to ShellExecute; neither leaves GitHub.
    if ((!out.downloadUrl.empty() && !isAllowedUpdateUrl(out.downloadUrl)) ||
        (!out.pageUrl.empty() && !isAllowedUpdateUrl(out.pageUrl))) {
        out.skip = UpdateSkip::UntrustedUrl;
    } else if (exe && !hasChecksum) {
        // A release from before checksums were published: there is nothing to verify the
        // installer against, and it would run with /SILENT, so it is not offered at all.
        out.skip = UpdateSkip::NoChecksum;
    }
    return true;
}

bool downloadInstaller(const std::wstring& url, const std::wstring& destPath, std::string* err,
                       bool* integrityFailed) {
    setErr(err, "");
    if (integrityFailed) *integrityFailed = false;

    // The expected digest first: no point fetching 75 MB that cannot be checked.
    std::string sumFile, expected;
    if (!httpGet(checksumUrlFor(url), true, sumFile, err)) return false;
    if (!parseSha256File(sumFile, expected)) {
        setErr(err, "the .sha256 asset is not a SHA-256 digest");
        if (integrityFailed) *integrityFailed = true;
        return false;
    }

    Session s; Handle c, r;
    if (!openUrl(url, true, s, c, r, err)) return false;

    const std::wstring partPath = destPath + L".part";
    HANDLE f = CreateFileW(partPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { setWinErr(err, "CreateFile"); return false; }

    Sha256 hash;
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
        if (!hash.update(buf.data(), read)) { setErr(err, "BCryptHashData failed"); ok = false; break; }
    }
    CloseHandle(f);

    if (!ok) { DeleteFileW(partPath.c_str()); return false; }

    // Compared while the file is still a .part: a mismatch never gets an installer's name.
    const std::string actual = hash.finishHex();
    if (actual.empty()) {
        setErr(err, "BCryptFinishHash failed");
        DeleteFileW(partPath.c_str());
        return false;
    }
    if (!hexEquals(actual, expected)) {
        setErr(err, "SHA-256 mismatch: expected " + expected + ", got " + actual);
        if (integrityFailed) *integrityFailed = true;
        DeleteFileW(partPath.c_str());
        return false;
    }

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
