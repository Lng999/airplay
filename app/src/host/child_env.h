// child_env.h — builds the environment block uxplay.exe needs as a child process.
//
// This is the C++ equivalent of the env section of scripts/run-uxplay.ps1:
//   PATH                 <msysRoot>\ucrt64\bin prepended (all runtime DLLs live flat there,
//                        UxPlay README.md:1157-1162)
//   HOME                 } get_homedir() (uxplay.cpp:768-779) has no getpwuid() fallback on
//   XDG_CONFIG_HOMEDIR   } Windows (:773-777) -> without these the Ed25519 key is not persisted
//                          (uxplay.cpp:3153-3162). See docs/DESIGN.md §5.3.
//   GST_REGISTRY         private registry file, so a stale shared cache from another GStreamer
//                        install cannot hide elements
//   GST_PLUGIN_SYSTEM_PATH  <msysRoot>\ucrt64\lib\gstreamer-1.0
#pragma once

#include <string>
#include <vector>

namespace airplay {

struct ChildEnvConfig {
    std::wstring msysRoot;      // e.g. L"C:\\msys64" (trailing backslashes are trimmed)
    std::wstring homeDir;       // created if missing
    std::wstring gstRegistry;   // its parent directory is created if missing
};

// Returns a CREATE_UNICODE_ENVIRONMENT block: "K=V\0K=V\0...\0". Entries are sorted
// case-insensitively (_wcsicmp), which is what CreateProcessW documents for the sorted-block
// convention; Windows itself tolerates unsorted blocks, we sort anyway so the block is
// reproducible and diffable in tests.
// Directory creation failures are reported through *err (UTF-8) but are NOT fatal: the block is
// still returned so the caller can decide.
std::vector<wchar_t> buildChildEnvironment(const ChildEnvConfig& cfg, std::string* err = nullptr);

// Creates `dir` and every missing parent. True if the directory exists afterwards.
bool ensureDirectoryW(const std::wstring& dir);

} // namespace airplay
