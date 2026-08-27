// test_release_flash.cpp - is anything half-finished ever on screen?
//
//   airplay_release_flash.exe [path\to\gst-launch-1.0.exe]
//
// Two moments, both of them a blink the user reported and neither visible to a test that only
// looks at the end state. A spin-sampling thread (no Sleep, QPC timestamps) watches both
// windows across each one.
//
// Taking the picture (adopt):
//   * restyling the guest in place repainted it on the desktop without its chrome, so the
//     receiver's own window was seen changing shape before it moved into ours;
//   * showing our window and laying the picture out afterwards put a default-sized window
//     holding an unplaced picture on screen for a frame, and then a jump to fullscreen.
//
// Letting it go (release):
//   * clearing the region, hiding, reparenting and restyling the guest are four repaints of
//     our client area, and what they repaint is a window whose picture has already gone -
//     black, or the device frame around a hole. Hiding ourselves last left that up for
//     ~2.8 ms here, a frame or two on a busier machine;
//   * writing the saved style word back put WS_VISIBLE on a window that was top-level again,
//     so it came back for ~2 ms until SWP_HIDEWINDOW cleared it.
//
// NOT registered with ctest: it needs GStreamer on disk and a GPU that can present.
#include "config_store.h"   // Win32 headers in the right order

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "video_embed.h"
#include "video_window.h"

namespace {

// Shorter than this is below what a 60 Hz display could show, and leaves room for the sampler
// catching a parent and its child a few microseconds apart.
constexpr double kToleranceMs = 0.5;

LARGE_INTEGER g_freq{};
LARGE_INTEGER g_t0{};

double ms_now() {
    LARGE_INTEGER n{};
    QueryPerformanceCounter(&n);
    return (n.QuadPart - g_t0.QuadPart) * 1000.0 / g_freq.QuadPart;
}

void pump(unsigned ms) {
    const double until = ms_now() + ms;
    for (;;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (ms_now() >= until) return;
        Sleep(1);
    }
}

struct Sample {
    double t;
    bool   guestVis;
    bool   guestTopLevel;
    bool   guestChild;     // WS_CHILD, which only means anything once it has a parent
    bool   oursVis;
    RECT   guestRect;
    RECT   oursRect;
};

bool sameRect(const RECT& a, const RECT& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

Sample take(HWND guest, HWND ours) {
    Sample s{};
    s.t = ms_now();
    if (IsWindow(guest)) {
        s.guestVis      = IsWindowVisible(guest) != 0;
        s.guestTopLevel = GetParent(guest) == nullptr;
        s.guestChild    = (GetWindowLongPtrW(guest, GWL_STYLE) & WS_CHILD) != 0;
        GetWindowRect(guest, &s.guestRect);
    }
    if (ours && IsWindow(ours)) {
        s.oursVis = IsWindowVisible(ours) != 0;
        GetWindowRect(ours, &s.oursRect);
    }
    return s;
}

bool differs(const Sample& a, const Sample& b) {
    return a.guestVis != b.guestVis || a.guestTopLevel != b.guestTopLevel ||
           a.guestChild != b.guestChild || a.oursVis != b.oursVis ||
           !sameRect(a.guestRect, b.guestRect) || !sameRect(a.oursRect, b.oursRect);
}

std::atomic<bool>   g_spin{false};
std::vector<Sample> g_log;
HWND                g_guest = nullptr;
HWND*               g_ours  = nullptr;   // our window does not exist yet when adopt starts

void spinner() {
    Sample prev = take(g_guest, *g_ours);
    g_log.push_back(prev);
    while (g_spin.load(std::memory_order_acquire)) {
        Sample s = take(g_guest, *g_ours);
        if (differs(prev, s)) {
            g_log.push_back(s);
            prev = s;
        }
    }
    g_log.push_back(take(g_guest, *g_ours));
}

void startWatch(HWND guest, HWND* ours) {
    g_log.clear();
    g_guest = guest;
    g_ours  = ours;
    g_spin.store(true, std::memory_order_release);
}

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("%-54s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

void dump() {
    for (const Sample& s : g_log)
        std::printf("  %9.3f ms  guest[vis=%d top=%d child=%d %ldx%ld]  ours[vis=%d %ldx%ld]\n",
                    s.t, s.guestVis, s.guestTopLevel, s.guestChild,
                    s.guestRect.right - s.guestRect.left,
                    s.guestRect.bottom - s.guestRect.top, s.oursVis,
                    s.oursRect.right - s.oursRect.left, s.oursRect.bottom - s.oursRect.top);
}

// Total time a predicate held between `from` and `until`.
double totalWhile(bool (*pred)(const Sample&), double from, double until) {
    double total = 0.0, start = -1.0;
    for (const Sample& s : g_log) {
        if (s.t < from) continue;
        const bool on = pred(s);
        if (on && start < 0) start = s.t;
        if (!on && start >= 0) {
            total += s.t - start;
            start = -1.0;
        }
    }
    if (start >= 0) total += until - start;
    return total;
}

bool emptyFrame(const Sample& s) { return s.oursVis && !s.guestVis; }
bool looseGuest(const Sample& s) { return s.guestVis && !s.oursVis && s.guestTopLevel; }
// Chrome already stripped, still standing on the desktop, still on screen: the receiver's
// own window visibly losing its title bar and frame a moment before it moves into ours.
bool restyledInPlace(const Sample& s) { return s.guestVis && s.guestTopLevel && s.guestChild; }

// Once our window is on screen, nothing about the layout may still be moving.
bool settledOnShow(double* firstShow) {
    bool  seen = false;
    RECT  ours{}, guest{};
    for (const Sample& s : g_log) {
        if (!s.oursVis) continue;
        if (!seen) {
            seen  = true;
            ours  = s.oursRect;
            guest = s.guestRect;
            if (firstShow) *firstShow = s.t;
            continue;
        }
        if (!sameRect(ours, s.oursRect) || !sameRect(guest, s.guestRect)) return false;
    }
    return seen;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_t0);

    const std::wstring gst = argc > 1 ? argv[1] : L"C:/msys64/ucrt64/bin/gst-launch-1.0.exe";
    // 1080p and a device frame on purpose: the most expensive repaint the app ever does.
    std::wstring cmd = L"\"" + gst +
                       L"\" videotestsrc pattern=smpte ! video/x-raw,width=1920,height=1080 ! "
                       L"d3d11videosink";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mut(cmd.begin(), cmd.end());
    mut.push_back(L'\0');
    if (!CreateProcessW(nullptr, mut.data(), nullptr, nullptr, FALSE,
                        CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        std::printf("CreateProcess failed: %lu\n", GetLastError());
        return 2;
    }
    std::printf("child pid %lu\n\n", pi.dwProcessId);

    HWND guest = nullptr;
    for (int i = 0; i < 150 && !guest; ++i) {
        pump(100);
        guest = ui::findReceiverVideoWindow(pi.dwProcessId);
    }
    if (!guest) {
        std::printf("no sink window - is GStreamer where this test looked?\n");
        TerminateProcess(pi.hProcess, 0);
        return 2;
    }

    ui::AppConfig cfg;
    cfg.embedVideo      = true;
    cfg.deviceFrame     = true;
    cfg.videoFullscreen = true;   // the remembered state that used to arrive as a second jump
    ui::VideoWindow video(GetModuleHandleW(nullptr), cfg);
    video.setDevice(L"iPhone14,5");

    // ---- 1. taking the picture --------------------------------------------------------
    HWND ours = nullptr;
    startWatch(guest, &ours);
    std::thread spAdopt(spinner);
    Sleep(30);

    const double tAdopt = ms_now();
    const bool ok = video.adopt(guest);
    ours = video.hwnd();
    Sleep(60);
    g_spin.store(false, std::memory_order_release);
    spAdopt.join();
    const double tAdoptEnd = ms_now();

    std::printf("--- adopt (called at %.3f) ---\n", tAdopt);
    dump();
    // Before the call the sink's window is legitimately on the desktop - that is how it is
    // found at all, and only the poll interval decides how long. What must never happen is
    // it standing there already wearing child styles.
    const double stripped = totalWhile(restyledInPlace, tAdopt, tAdoptEnd);
    const double bare     = totalWhile(looseGuest, tAdopt, tAdoptEnd);
    double firstShow = 0.0;
    const bool settled = settledOnShow(&firstShow);
    std::printf("\nguest on the desktop wearing child styles : %.3f ms\n", stripped);
    std::printf("guest on the desktop at all, after adopt : %.3f ms\n\n", bare);

    check(ok, "adopt succeeded");
    check(stripped <= kToleranceMs, "the guest is never restyled in place on the desktop");
    check(bare <= kToleranceMs, "adopt takes it off the desktop before anything else");
    check(settled, "our window is only shown once the layout is final");

    video.setFullscreen(false);
    pump(400);

    // ---- 2. letting it go -------------------------------------------------------------
    startWatch(guest, &ours);
    std::thread spRel(spinner);
    Sleep(30);

    const double tRelease = ms_now();
    video.release();                 // what Stop, Exit and shutdown do
    const double tDone = ms_now();

    Sleep(60);
    g_spin.store(false, std::memory_order_release);
    spRel.join();
    const double tEnd = ms_now();

    std::printf("\n--- release (%.3f ms) ---\n", tDone - tRelease);
    dump();
    const double empty = totalWhile(emptyFrame, tRelease, tEnd);
    const double loose = totalWhile(looseGuest, tRelease, tEnd);
    std::printf("\nour window on screen with no picture in it : %.3f ms\n", empty);
    std::printf("guest on screen after we went away         : %.3f ms\n\n", loose);

    check(empty <= kToleranceMs, "no empty frame between the picture and the close");
    check(loose <= kToleranceMs, "the guest never blinks back as its own window");
    check(!IsWindowVisible(ours), "our window ends up hidden");
    check(IsWindow(guest) && !IsWindowVisible(guest), "the guest ends up alive and hidden");
    check(GetParent(guest) == nullptr, "the guest is a top-level window again");

    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "OK", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
