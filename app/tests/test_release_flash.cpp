// test_release_flash.cpp - is anything on screen between the picture leaving and the window
// going away?
//
//   airplay_release_flash.exe [path\to\gst-launch-1.0.exe]
//
// Letting go of the guest is four operations that each repaint our client area: clearing its
// window region, hiding it, reparenting it, restyling it. Every one of them redraws a window
// whose picture is already gone - a black rectangle, or the device frame around a hole. Doing
// them before hiding ourselves left that on screen for ~3 ms here, a frame or two on a busier
// machine, and that is what users saw as a blink after pressing Durdur.
//
// The same measurement caught a second one: writing the saved style word back to the guest put
// WS_VISIBLE on a window that was top-level again, so it came back for ~2 ms until
// SWP_HIDEWINDOW took it off.
//
// A spin-sampling thread (no Sleep, QPC timestamps) watches both windows across release() and
// fails if either was on screen when it should not have been.
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

// A flash shorter than this is below what a 60 Hz display could show anyway, and leaves room
// for the sampler catching a parent and its child a few microseconds apart.
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
    bool   oursVis;
};

std::atomic<bool>   g_spin{false};
std::vector<Sample> g_log;

Sample take(HWND guest, HWND ours) {
    return Sample{ms_now(), IsWindow(guest) != 0 && IsWindowVisible(guest) != 0,
                  IsWindowVisible(ours) != 0};
}

void spinner(HWND guest, HWND ours) {
    Sample prev = take(guest, ours);
    g_log.push_back(prev);
    while (g_spin.load(std::memory_order_acquire)) {
        Sample s = take(guest, ours);
        if (s.guestVis != prev.guestVis || s.oursVis != prev.oursVis) {
            g_log.push_back(s);
            prev = s;
        }
    }
    g_log.push_back(take(guest, ours));
}

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("%-52s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

// Total time the predicate held across the recorded transitions.
double totalWhile(bool (*pred)(const Sample&), double until) {
    double total = 0.0, start = -1.0;
    for (const Sample& s : g_log) {
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
bool looseGuest(const Sample& s) { return s.guestVis && !s.oursVis; }

} // namespace

int wmain(int argc, wchar_t** argv) {
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_t0);

    const std::wstring gst = argc > 1 ? argv[1] : L"C:/msys64/ucrt64/bin/gst-launch-1.0.exe";
    // 1080p and the device frame on purpose: the most expensive repaint the app ever does.
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
    std::printf("child pid %lu\n", pi.dwProcessId);

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
    cfg.embedVideo  = true;
    cfg.deviceFrame = true;
    ui::VideoWindow video(GetModuleHandleW(nullptr), cfg);
    if (!video.adopt(guest)) {
        std::printf("adopt failed\n");
        TerminateProcess(pi.hProcess, 0);
        return 2;
    }
    video.setDevice(L"iPhone14,5");
    pump(2000);
    HWND ours = video.hwnd();

    g_spin.store(true, std::memory_order_release);
    std::thread sp(spinner, guest, ours);
    Sleep(30);                       // let the sampler settle on a steady state first

    const double tRelease = ms_now();
    video.release();                 // what Stop, Exit and shutdown do
    const double tDone = ms_now();

    Sleep(60);
    g_spin.store(false, std::memory_order_release);
    sp.join();
    const double tEnd = ms_now();

    std::printf("release() took %.3f ms\n\n--- visibility transitions ---\n", tDone - tRelease);
    for (const Sample& s : g_log)
        std::printf("  %9.3f ms   guestVis=%d  oursVis=%d\n", s.t, s.guestVis, s.oursVis);
    std::printf("\n");

    const double empty = totalWhile(emptyFrame, tEnd);
    const double loose = totalWhile(looseGuest, tEnd);
    std::printf("our window on screen with no picture in it : %.3f ms\n", empty);
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
