// autostart.h - "start with Windows" (docs/PHASE2-M2-SPEC.md §4).
//
// One mechanism: HKCU\...\Run\airplay. installer/airplay.iss's startup task writes the same
// value. A Startup-folder shortcut left by the v0.3.0 installer (or an unreleased build) is
// turned into that value by migrateLegacyStartupShortcut(); until then the checkbox speaks
// for it too - otherwise turning it off would leave the shortcut behind and we would still
// start at logon.
#pragma once

#include <string>

#include "config_store.h"   // Win32 headers in the right order

namespace ui {

// True when the HKCU Run value or a leftover Startup shortcut exists.
bool isLaunchAtLogon();

// on  -> HKCU\...\Run\airplay = "<our exe>" -minimized
// off -> that value and any leftover Startup shortcut are removed.
// Returns false if the registry write failed (a missing shortcut is not a failure).
bool setLaunchAtLogon(bool on);

// One-time migration of the v0.3.0 leftover, called once at startup (main.cpp). If
// "AirPlay Alicisi.lnk" (or the unreleased "airplay.lnk") is in the Startup folder: write the
// Run value unless it already exists, then delete the shortcut(s). Idempotent and silent; if
// the Run value cannot be written nothing is touched, so the shortcut keeps working and the
// next launch tries again. The installer deliberately leaves the v0.3.0 shortcut alone.
//
// The overrides are for tests only: a scratch directory instead of the real Startup folder,
// and an HKCU subkey instead of ...\CurrentVersion\Run. Empty = the real ones.
void migrateLegacyStartupShortcut(const std::wstring& startupDirOverride = L"",
                                  const std::wstring& runKeyOverride = L"");

} // namespace ui
