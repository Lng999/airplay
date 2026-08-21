// stale_receivers.h - clear away receivers we do not own before starting our own.
//
// Two uxplay.exe on one machine do not conflict loudly: SO_REUSEADDR lets both bind the same
// port, so both keep listening and the phone picks whichever answers first. When that is not
// our child, the GUI sees no events at all and acts on the wrong process. The job object in
// uxplay_host.cpp stops *us* from leaving orphans behind; this handles the ones already
// there - from an older build, a crash before that fix, or a manual AirPlay.bat run.
#pragma once

#include <string>

#include "config_store.h"   // Win32 headers in the right order

namespace ui {

// Terminates every running process whose image is exePath, except the calling process.
// Returns how many were terminated. Best-effort: processes we may not open are skipped.
int killStaleReceivers(const std::wstring& exePath);

} // namespace ui
