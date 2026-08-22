// autostart.h - "start with Windows" (docs/PHASE2-M2-SPEC.md §4).
//
// Two mechanisms can already put us there: this Run value, and the optional Startup shortcut
// installer/airplay.iss offers. The checkbox has to speak for both, or turning it off would
// leave the installer's shortcut behind and the app would still come up at logon.
#pragma once

#include <string>

#include "config_store.h"   // Win32 headers in the right order

namespace ui {

// True when either the HKCU Run value or the installer's Startup shortcut exists.
bool isLaunchAtLogon();

// on  -> HKCU\...\Run\airplay = "<our exe>" -minimized
// off -> that value and the Startup shortcut are both removed.
// Returns false if the registry write failed (a missing shortcut is not a failure).
bool setLaunchAtLogon(bool on);

} // namespace ui
