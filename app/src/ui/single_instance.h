// single_instance.h - one GUI per user session (PHASE2-SPEC "Single instance").
#pragma once

#include "config_store.h"   // pulls in <winsock2.h> before <windows.h>

namespace ui {

// The GUID is fixed by PHASE2-SPEC; "Local\" scopes it to the logon session so that two
// different users on the same machine can each run their own receiver.
inline constexpr const wchar_t* kMutexName =
    L"Local\\airplay-gui-7c1d9d0e-1f4d-4f1f-9d7e-airplay";

// Window class name; also the handle a second instance uses to find the first.
inline constexpr const wchar_t* kWindowClass = L"AirplayGuiMainWindow";

class SingleInstance {
public:
    SingleInstance() = default;
    ~SingleInstance();
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    // true  -> we are the first instance (mutex owned)
    // false -> another instance already holds it
    bool acquire(const wchar_t* name = kMutexName);

private:
    HANDLE mutex_ = nullptr;
};

// Bring an already running instance to the front. Returns true if one was found.
bool activateExistingInstance(const wchar_t* windowClass = kWindowClass);

} // namespace ui
