// test_embed_live.cpp - does adopting another process's video window really work here?
//
//   airplay_embed_live.exe [path\to\gst-launch-1.0.exe]
//
// Default: C:/msys64/ucrt64/bin/gst-launch-1.0.exe. A videotestsrc pipeline into
// d3d11videosink stands in for the receiver: the interesting part is not AirPlay but the
// cross-process window surgery in src/ui/video_embed.cpp, and that is identical either way.
// The claim under test (docs/PHASE2-M2-SPEC.md): the sink's window can be found by process
// id, restyled, reparented into a window of ours, resized, and handed back intact.
//
// NOT registered with ctest: it needs GStreamer on disk and a GPU that can present.
// Exit code 0 iff every step passed.
#include "config_store.h"   // Win32 headers in the right order

#include <cstdio>
#include <string>

#include "video_embed.h"
#include "video_window.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("%-52s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

// Run the message loop for roughly ms milliseconds. Everything here is driven by window
// messages - WM_SIZE reaches the guest only if we pump.
void pump(unsigned ms) {
    const DWORD until = GetTickCount() + ms;
    for (;;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (static_cast<int>(GetTickCount() - until) >= 0) return;
        Sleep(10);
    }
}

std::wstring clientSizeText(HWND h) {
    RECT r{};
    GetClientRect(h, &r);
    wchar_t b[64];
    _snwprintf(b, 64, L"%ldx%ld", r.right, r.bottom);
    return b;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    std::wstring gst = argc > 1 ? argv[1] : L"C:/msys64/ucrt64/bin/gst-launch-1.0.exe";

    std::wstring cmd = L"\"" + gst +
                       L"\" videotestsrc pattern=smpte is-live=true ! "
                       L"video/x-raw,width=1280,height=720 ! d3d11videosink";
    std::wstring mutable_cmd = cmd;

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi)) {
        std::printf("could not start %ls (error %lu)\n", gst.c_str(), GetLastError());
        return 2;
    }
    std::printf("child pid %lu\n", pi.dwProcessId);

    // 1. find it
    HWND guest = nullptr;
    for (int i = 0; i < 60 && !guest; ++i) {
        pump(250);
        guest = ui::findReceiverVideoWindow(pi.dwProcessId);
    }
    check(guest != nullptr, "findReceiverVideoWindow finds the sink window");
    if (!guest) {
        TerminateProcess(pi.hProcess, 0);
        return 1;
    }
    std::printf("guest %p, client %ls\n", static_cast<void*>(guest),
                clientSizeText(guest).c_str());

    // 2. adopt it into a real picture window
    ui::AppConfig cfg;
    ui::VideoWindow video(GetModuleHandleW(nullptr), cfg);
    const bool adopted = video.adopt(guest);
    check(adopted, "VideoWindow::adopt takes the window");
    check(video.hwnd() != nullptr, "the picture window exists");
    check(GetParent(guest) == video.hwnd(), "the guest is now our child");
    check((GetWindowLongPtrW(guest, GWL_STYLE) & WS_CHILD) != 0, "the guest is a WS_CHILD");

    const SIZE src = video.sourceSize();
    std::printf("source %ldx%ld\n", src.cx, src.cy);
    check(src.cx == 1280 && src.cy == 720, "the source size is the video size");

    pump(1000);

    // 3. resize ours; the guest must follow, letterboxed at the source aspect
    SetWindowPos(video.hwnd(), nullptr, 0, 0, 800, 700, SWP_NOMOVE | SWP_NOZORDER);
    pump(500);

    RECT client{}, g{};
    GetClientRect(video.hwnd(), &client);
    GetWindowRect(guest, &g);
    POINT tl{g.left, g.top};
    ScreenToClient(video.hwnd(), &tl);
    const int gw = g.right - g.left, gh = g.bottom - g.top;
    std::printf("client %ldx%ld, guest %dx%d at %ld,%ld\n", client.right, client.bottom, gw, gh,
                tl.x, tl.y);
    check(gw <= client.right && gh <= client.bottom, "the guest fits inside our client area");
    const double want = static_cast<double>(src.cx) / src.cy;
    const double got  = gh > 0 ? static_cast<double>(gw) / gh : 0.0;
    check(got > want - 0.02 && got < want + 0.02, "the guest keeps the source aspect ratio");
    check(tl.x >= 0 && tl.y >= 0, "the guest is letterboxed, not clipped");

    DWORD ec = 0;
    GetExitCodeProcess(pi.hProcess, &ec);
    check(ec == STILL_ACTIVE, "the receiver process survived the surgery");

    // 4. hand it back
    video.release();
    pump(300);
    check(IsWindow(guest) != 0, "the guest still exists after release");
    check(GetParent(guest) == nullptr, "the guest is a top-level window again");
    check((GetWindowLongPtrW(guest, GWL_STYLE) & WS_CHILD) == 0, "WS_CHILD is gone");

    GetExitCodeProcess(pi.hProcess, &ec);
    check(ec == STILL_ACTIVE, "the receiver process is still alive at the end");

    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "OK", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
