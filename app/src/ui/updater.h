// updater.h - ask GitHub whether a newer release exists, and fetch its installer.
//
// Everything here BLOCKS on the network, so it is called from a worker thread; the thread
// posts the result back to the window (main_window.cpp, WM_UPDATE_*). Nothing in this file
// touches a HWND.
//
// The transport is WinHTTP - part of Windows, so the shipped folder gains no DLL for this.
// The JSON that comes back is read with a deliberately small scanner (findString below)
// rather than a parser: we want three fields out of a document whose shape is fixed by the
// GitHub API, and a dependency for that would cost more than it is worth.
//
// Trust: the installer is run with /SILENT, so every URL that came out of the API response is
// held to an https + GitHub-host allowlist (isAllowedUpdateUrl) before it is fetched or opened,
// and so is the host a download finally lands on after redirects. The installer must also
// match the SHA-256 published next to it as "<installer>.sha256" (scripts/publish-release.ps1);
// a release without one is skipped, and a mismatch is deleted unrun.
#pragma once

#include <string>
#include <vector>

namespace ui {

// Why a newer release was found but will not be offered. Not an error: the check worked, the
// release just does not meet the bar, and the UI says so in the log rather than in a dialog.
enum class UpdateSkip {
    None,
    UntrustedUrl,   // download or page URL outside the allowlist
    NoChecksum,     // an installer with no "<installer>.sha256" asset beside it (old release)
};

struct UpdateInfo {
    std::wstring version;       // "0.4.0" - the release tag with any leading 'v' removed
    std::wstring downloadUrl;   // browser_download_url of the .exe asset
    std::wstring pageUrl;       // the release page, shown when there is no asset to fetch
    std::wstring notes;         // release body, trimmed to a few lines for the prompt
    UpdateSkip   skip = UpdateSkip::None;
};

// --- pure helpers (no network; unit-tested in app/tests/test_updater.cpp) -------------------

// Compares dotted numeric versions. Missing components count as 0, so "0.3" == "0.3.0".
// Returns <0 if a is older than b, 0 if equal, >0 if newer. Non-numeric junk stops the scan.
int compareVersions(const std::wstring& a, const std::wstring& b);

// True only for https:// URLs whose host is exactly (case-insensitively) one of github.com,
// objects.githubusercontent.com or release-assets.githubusercontent.com. Anything unusual in
// the authority - userinfo ('@'), an explicit port, percent-escapes, a trailing dot - is
// refused rather than interpreted.
bool isAllowedUpdateUrl(const std::wstring& url);

// Value of the first `"key": "..."` at or after `from`, JSON escapes undone (UTF-8). `end`
// receives the offset just past the closing quote, so callers can keep scanning. Returns false
// when the key is not there or its value is not a string.
bool findString(const std::string& doc, size_t from, const char* key, std::string& value,
                size_t* end = nullptr);

// One entry of the release's top-level "assets" array: its "name" and "browser_download_url".
// Keys of nested objects (an asset's "uploader") are not mistaken for the asset's own.
struct ReleaseAsset {
    std::string name;
    std::string url;
};
std::vector<ReleaseAsset> parseReleaseAssets(const std::string& releaseJson);

// The digest out of a .sha256 file: "<64 hex>  <name>", "<64 hex> *<name>" or the bare digest,
// leading whitespace and a UTF-8 BOM tolerated. `hex` comes back lower-case.
bool parseSha256File(const std::string& content, std::string& hex);

// Same non-empty hex string, ignoring case. False if either side has a non-hex character.
bool hexEquals(const std::string& a, const std::string& b);

// Lower-case hex SHA-256 of a buffer via CNG (BCrypt), or "" if CNG failed.
std::string sha256Hex(const void* data, size_t len);

// Where an installer's checksum asset is served: GitHub puts every asset of a release under the
// same .../releases/download/<tag>/ prefix, so "<installer>.sha256" sits at the URL + ".sha256".
std::wstring checksumUrlFor(const std::wstring& installerUrl);

// --- network --------------------------------------------------------------------------------

// The version this binary was built as (AIRPLAY_VERSION_STR).
std::wstring currentVersion();

// GET https://api.github.com/repos/<owner>/<repo>/releases/latest, blocking.
//   true  -> `out` is filled in and describes a release NEWER than currentVersion(); if
//            out.skip != None it must not be downloaded or opened
//   false -> either we are up to date (*err empty) or the check failed (*err set, UTF-8)
// Drafts and prereleases are skipped by the API's own /latest endpoint.
bool checkForUpdate(UpdateInfo& out, std::string* err);

// Fetches checksumUrlFor(url), then downloads `url` to `destPath` (overwriting), blocking,
// hashing it on the way. The file is written to a .part first and only renamed once its
// SHA-256 matches, so neither a half-finished nor a tampered download can be handed to
// ShellExecute. Refuses a URL - or a redirect target - outside isAllowedUpdateUrl.
// `err` is UTF-8 on failure; *integrityFailed is set when the failure is a digest mismatch or
// an unreadable .sha256 (as opposed to a network or disk error).
bool downloadInstaller(const std::wstring& url, const std::wstring& destPath, std::string* err,
                       bool* integrityFailed);

// %TEMP%\airplay-update\AirPlay-Setup-<version>.exe, directories created.
std::wstring installerDownloadPath(const std::wstring& version);

} // namespace ui
