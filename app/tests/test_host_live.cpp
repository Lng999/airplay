// test_host_live.cpp — end-to-end smoke test for UxplayHost against the REAL receiver.
//
//   airplay_host_live.exe [path\to\uxplay.exe]
//
// Default path: <dir of this exe>\..\..\build\uxplay.exe (i.e. build-host/tests -> build/).
// MSYS-style arguments ("/c/Users/...") are accepted and converted.
//
// It really starts uxplay.exe, so for a few seconds this machine advertises an AirPlay receiver
// named "AirPlay-LiveTest" on TCP+UDP 7300-7302 (7300 is used instead of the product default 7100
// so a running GUI does not clash). Exit code 0 iff HostState::Waiting was reached and the child
// exited afterwards.
//
// NOT registered with ctest: it needs a built receiver and opens real sockets.
#include <windows.h>
#include <shellapi.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cwctype>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

#include "airplay/uxplay_host.h"

using namespace airplay;

namespace {

const wchar_t* stateName(HostState s) {
    switch (s) {
    case HostState::Stopped:   return L"Stopped";
    case HostState::Starting:  return L"Starting";
    case HostState::Waiting:   return L"Waiting";
    case HostState::Connected: return L"Connected";
    case HostState::Stopping:  return L"Stopping";
    case HostState::Error:     return L"Error";
    }
    return L"?";
}

const char* kindName(HostEventKind k) {
    switch (k) {
    case HostEventKind::StateChanged: return "StateChanged";
    case HostEventKind::LogLine:      return "LogLine";
    case HostEventKind::ClientInfo:   return "ClientInfo";
    case HostEventKind::Ports:        return "Ports";
    case HostEventKind::Resolution:   return "Resolution";
    case HostEventKind::MirrorFps:    return "MirrorFps";
    case HostEventKind::MirrorActivity: return "MirrorActivity";
    case HostEventKind::Pin:          return "Pin";
    case HostEventKind::Warning:      return "Warning";
    case HostEventKind::Error:        return "Error";
    }
    return "?";
}

std::wstring exeDir() {
    wchar_t buf[MAX_PATH * 2] = {0};
    GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    std::wstring p(buf);
    const size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring(L".") : p.substr(0, slash);
}

// "/c/Users/pc/x" -> "C:\Users\pc\x"; also normalises forward slashes.
std::wstring toWindowsPath(std::wstring p) {
    if (p.size() >= 3 && p[0] == L'/' && iswalpha(p[1]) && p[2] == L'/') {
        p = std::wstring(1, static_cast<wchar_t>(towupper(p[1]))) + L":" + p.substr(2);
    }
    for (wchar_t& c : p) {
        if (c == L'/') c = L'\\';
    }
    return p;
}

std::wstring absolutePath(const std::wstring& p) {
    wchar_t buf[MAX_PATH * 2] = {0};
    const DWORD n = GetFullPathNameW(p.c_str(), static_cast<DWORD>(std::size(buf)), buf, nullptr);
    return n ? std::wstring(buf, n) : p;
}

std::wstring envVar(const wchar_t* name) {
    wchar_t buf[MAX_PATH * 2] = {0};
    const DWORD n = GetEnvironmentVariableW(name, buf, static_cast<DWORD>(std::size(buf)));
    return n ? std::wstring(buf, n) : std::wstring();
}

struct Recorder {
    std::mutex m;
    std::condition_variable cv;
    std::vector<HostEvent> events;
    bool sawWaiting = false;
    HostState last = HostState::Stopped;

    void onEvent(const HostEvent& ev) {
        std::lock_guard<std::mutex> lk(m);
        events.push_back(ev);
        if (ev.kind == HostEventKind::StateChanged) {
            last = ev.state;
            if (ev.state == HostState::Waiting) sawWaiting = true;
            cv.notify_all();
        }
    }
};

void printEvent(const HostEvent& ev) {
    switch (ev.kind) {
    case HostEventKind::StateChanged:
        std::wprintf(L"  %-12hs %ls\n", kindName(ev.kind), stateName(ev.state));
        break;
    case HostEventKind::Ports:
        std::wprintf(L"  %-12hs UDP %d %d %d  TCP %d %d %d\n", kindName(ev.kind),
                     ev.udpPorts[0], ev.udpPorts[1], ev.udpPorts[2],
                     ev.tcpPorts[0], ev.tcpPorts[1], ev.tcpPorts[2]);
        break;
    case HostEventKind::ClientInfo:
        std::wprintf(L"  %-12hs name=%hs model=%hs id=%hs\n", kindName(ev.kind),
                     ev.clientName.c_str(), ev.clientModel.c_str(), ev.clientDeviceId.c_str());
        break;
    case HostEventKind::Resolution:
        std::wprintf(L"  %-12hs src %dx%d -> %dx%d\n", kindName(ev.kind),
                     ev.srcWidth, ev.srcHeight, ev.width, ev.height);
        break;
    default:
        std::wprintf(L"  %-12hs %hs\n", kindName(ev.kind), ev.message.c_str());
        break;
    }
}

} // namespace

int main() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    std::wstring exe;
    if (argc > 1) {
        exe = absolutePath(toWindowsPath(argv[1]));
    } else {
        exe = absolutePath(exeDir() + L"\\..\\..\\build\\uxplay.exe");
    }
    if (argv) LocalFree(argv);

    HostConfig cfg;
    cfg.uxplayExe   = exe;
    cfg.msysRoot    = L"C:\\msys64";
    cfg.homeDir     = envVar(L"APPDATA") + L"\\airplay";
    cfg.gstRegistry = envVar(L"LOCALAPPDATA") + L"\\airplay\\gst-registry.bin";
    cfg.name        = "AirPlay-LiveTest";
    cfg.port        = 7300;              // not 7100: avoid clashing with a running GUI
    cfg.videoSink   = "d3d11videosink";
    cfg.audioSink   = "autoaudiosink";
    cfg.noHold      = true;
    cfg.resetSeconds = 15;

    std::wprintf(L"exe          : %ls\n", cfg.uxplayExe.c_str());
    std::wprintf(L"HOME         : %ls\n", cfg.homeDir.c_str());
    std::wprintf(L"GST_REGISTRY : %ls\n", cfg.gstRegistry.c_str());
    std::wprintf(L"\n");

    Recorder rec;
    UxplayHost host;
    host.setCallback([&rec](const HostEvent& ev) { rec.onEvent(ev); });

    std::string err;
    if (!host.start(cfg, &err)) {
        std::wprintf(L"start() failed: %hs\n", err.c_str());
        return 1;
    }
    std::wprintf(L"started, pid = %lu\n", static_cast<unsigned long>(host.pid()));

    bool reachedWaiting = false;
    {
        std::unique_lock<std::mutex> lk(rec.m);
        reachedWaiting = rec.cv.wait_for(lk, std::chrono::seconds(8),
                                         [&rec] { return rec.sawWaiting || rec.last == HostState::Error; }) &&
                         rec.sawWaiting;
    }
    std::wprintf(L"reached Waiting within 8 s : %hs\n", reachedWaiting ? "yes" : "NO");

    host.stop(3000);

    const HostState finalState = host.state();
    const bool stillRunning = host.isRunning();

    std::vector<HostEvent> events;
    {
        std::lock_guard<std::mutex> lk(rec.m);
        events = rec.events;
    }

    std::wprintf(L"\n--- events (%zu) ---\n", events.size());
    for (const HostEvent& ev : events) printEvent(ev);

    bool graceful = false, sawStopVerdict = false;
    for (const HostEvent& ev : events) {
        if (ev.kind == HostEventKind::LogLine && ev.message.rfind("stop: ", 0) == 0) {
            sawStopVerdict = true;
            graceful = (ev.message == "stop: graceful");
        }
    }

    std::wprintf(L"\n--- summary ---\n");
    std::wprintf(L"final state   : %ls\n", stateName(finalState));
    std::wprintf(L"still running : %hs\n", stillRunning ? "yes" : "no");
    std::wprintf(L"stop path     : %hs\n",
                 !sawStopVerdict ? "no verdict (child had already exited)"
                                 : (graceful ? "graceful (CTRL_BREAK, no TerminateProcess)"
                                             : "terminated (TerminateProcess after grace period)"));

    const bool ok = reachedWaiting && !stillRunning && finalState == HostState::Stopped;
    std::wprintf(L"RESULT        : %hs\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
