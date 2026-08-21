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
#pragma once

#include <string>

namespace ui {

struct UpdateInfo {
    std::wstring version;       // "0.4.0" - the release tag with any leading 'v' removed
    std::wstring downloadUrl;   // browser_download_url of the .exe asset
    std::wstring pageUrl;       // the release page, shown when there is no asset to fetch
    std::wstring notes;         // release body, trimmed to a few lines for the prompt
};

// Compares dotted numeric versions. Missing components count as 0, so "0.3" == "0.3.0".
// Returns <0 if a is older than b, 0 if equal, >0 if newer. Non-numeric junk stops the scan.
int compareVersions(const std::wstring& a, const std::wstring& b);

// The version this binary was built as (AIRPLAY_VERSION_STR).
std::wstring currentVersion();

// GET https://api.github.com/repos/<owner>/<repo>/releases/latest, blocking.
//   true  -> `out` is filled in and describes a release NEWER than currentVersion()
//   false -> either we are up to date (*err empty) or the check failed (*err set, UTF-8)
// Drafts and prereleases are skipped by the API's own /latest endpoint.
bool checkForUpdate(UpdateInfo& out, std::string* err);

// Downloads `url` to `destPath` (overwriting), blocking. `err` is UTF-8 on failure.
// The file is written to a .part first and renamed on success, so a half-finished download
// can never be handed to ShellExecute as if it were an installer.
bool downloadFile(const std::wstring& url, const std::wstring& destPath, std::string* err);

// %TEMP%\airplay-update\AirPlay-Setup-<version>.exe, directories created.
std::wstring installerDownloadPath(const std::wstring& version);

} // namespace ui
