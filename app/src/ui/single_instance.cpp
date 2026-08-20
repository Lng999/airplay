#include "single_instance.h"

namespace ui {

SingleInstance::~SingleInstance() {
    if (mutex_) {
        CloseHandle(mutex_);
        mutex_ = nullptr;
    }
}

bool SingleInstance::acquire(const wchar_t* name) {
    // Keep the handle even when the mutex already existed: releasing it early would let a
    // third instance believe it is the first while the second is still tearing down.
    mutex_ = CreateMutexW(nullptr, TRUE, name);
    if (!mutex_) return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex_);
        mutex_ = nullptr;
        return false;
    }
    return true;
}

bool activateExistingInstance(const wchar_t* windowClass) {
    HWND hwnd = FindWindowW(windowClass, nullptr);
    if (!hwnd) return false;

    // The first instance may be hidden in the tray.
    ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(hwnd);
    return true;
}

} // namespace ui
