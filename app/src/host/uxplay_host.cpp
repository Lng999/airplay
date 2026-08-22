// uxplay_host.cpp — child-process host for uxplay.exe (docs/DESIGN.md §6.1, docs/PHASE2-SPEC.md).
//
// Threading contract (also stated in app/include/airplay/uxplay_host.h):
//   * HostEventCallback is invoked ON THE READER THREAD. The UI must marshal (PostMessage) — it
//     must never touch HWNDs from inside the callback, and it must never call stop() from inside
//     the callback (stop() joins the reader thread => self-join deadlock).
//   * start()/stop() are serialised by a lifecycle mutex; state()/pid()/isRunning() are lock-free.
#include "airplay/uxplay_host.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "child_env.h"
#include "line_parser.h"

namespace airplay {
namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n < 0 ? 0 : n), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], n);
    return out;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n < 0 ? 0 : n), '\0');
    if (n > 0) {
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &out[0], n,
                            nullptr, nullptr);
    }
    return out;
}

// MSVC/CommandLineToArgvW quoting rules (the "Parsing C++ Command-Line Arguments" algorithm):
// a run of backslashes is doubled only when it precedes a quote or ends the argument.
std::wstring quoteArg(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;

    std::wstring out;
    out.push_back(L'"');
    for (size_t i = 0; ; ++i) {
        size_t backslashes = 0;
        while (i < arg.size() && arg[i] == L'\\') { ++i; ++backslashes; }
        if (i == arg.size()) {
            out.append(backslashes * 2, L'\\');
            break;
        }
        if (arg[i] == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
        } else {
            out.append(backslashes, L'\\');
        }
        out.push_back(arg[i]);
    }
    out.push_back(L'"');
    return out;
}

std::string lastErrorText(const char* what, DWORD code) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s failed (GetLastError=%lu)", what,
                  static_cast<unsigned long>(code));
    return std::string(buf);
}

} // namespace

// -------------------------------------------------------------------------------------------------

struct UxplayHost::Impl {
    std::mutex              lifecycle;      // serialises start()/stop()
    std::mutex              cbMutex;
    HostEventCallback       cb;

    std::atomic<int>        state{static_cast<int>(HostState::Stopped)};
    std::atomic<uint32_t>   childPid{0};
    std::atomic<bool>       stopRequested{false};

    HANDLE                  hProcess{nullptr};
    // The child is put in a job object with KILL_ON_JOB_CLOSE, so it cannot outlive us even
    // if the GUI is killed outright. An orphaned uxplay.exe is not harmless: SO_REUSEADDR
    // lets it keep listening on the same port, and the phone may connect to *it* instead of
    // to our child - the GUI then sees no events at all and acts on the wrong process.
    HANDLE                  hJob{nullptr};
    HANDLE                  hProcThread{nullptr};
    HANDLE                  hReadPipe{nullptr};
    std::thread             reader;

    // PIN block bookkeeping (see line_parser.h): only one art row carries decodable digits, so we
    // track the run and synthesise a digit-less Pin event if none of the rows decoded.
    bool pinRunActive{false};
    bool pinRunDecoded{false};
    // Last fatal-looking line seen on the pipe. Written and read on the reader thread only.
    // A child that rejects its arguments exits with code 0, so the code alone explains nothing.
    std::string lastErrorLine;
    // -FPSdata reports arrive as XML, one element per line, so the key and its value land in
    // two separate calls to handleLine().
    std::string pendingPlistKey;

    void emit(const HostEvent& ev) {
        HostEventCallback local;
        {
            std::lock_guard<std::mutex> lk(cbMutex);
            local = cb;
        }
        if (local) local(ev);
    }

    void emitLog(const std::string& text) {
        HostEvent ev;
        ev.kind = HostEventKind::LogLine;
        ev.message = text;
        emit(ev);
    }

    void emitError(const std::string& text) {
        HostEvent ev;
        ev.kind = HostEventKind::Error;
        ev.message = text;
        emit(ev);
    }

    HostState get() const { return static_cast<HostState>(state.load(std::memory_order_acquire)); }

    void setState(HostState s) {
        const HostState prev = get();
        if (prev == s) return;
        state.store(static_cast<int>(s), std::memory_order_release);
        HostEvent ev;
        ev.kind = HostEventKind::StateChanged;
        ev.state = s;
        emit(ev);
    }

    void closeHandlesLocked() {
        if (hReadPipe)   { CloseHandle(hReadPipe);   hReadPipe = nullptr; }
        if (hProcThread) { CloseHandle(hProcThread); hProcThread = nullptr; }
        if (hProcess)    { CloseHandle(hProcess);    hProcess = nullptr; }
        if (hJob)        { CloseHandle(hJob);        hJob = nullptr; }
        childPid.store(0, std::memory_order_release);
    }

    void closePinRun() {
        if (pinRunActive && !pinRunDecoded) {
            HostEvent ev;                     // digits could not be recovered from the ASCII art
            ev.kind = HostEventKind::Pin;
            emit(ev);
        }
        pinRunActive = false;
        pinRunDecoded = false;
    }

    void handleLine(const std::string& line) {
        const ParsedLine parsed = parseUxplayLineDetailed(line);

        if (parsed.tag == LineTag::PinArt) {
            if (!pinRunActive) { pinRunActive = true; pinRunDecoded = false; }
            if (!parsed.detail.empty()) pinRunDecoded = true;
        } else {
            closePinRun();
        }

        // --- -FPSdata report: pair "<key>submitSurfaceFPS</key>" with the integer after it ---
        if (parsed.tag == LineTag::PlistKey) {
            pendingPlistKey = parsed.detail;
        } else if (parsed.tag == LineTag::PlistInteger) {
            if (pendingPlistKey == "submitSurfaceFPS") {
                HostEvent fps{};
                fps.kind = HostEventKind::MirrorFps;
                fps.srcWidth = std::atoi(parsed.detail.c_str());
                fps.message = parsed.detail;
                emit(fps);
            }
            pendingPlistKey.clear();
        } else if (parsed.tag != LineTag::PlistNoise) {
            pendingPlistKey.clear();
        }

        // The reports are ~30 lines of XML every second. Useful as a signal, unreadable as a
        // log, so the envelope never reaches the sink - only the parsed frame rate does. The
        // same goes for the receiver's own once-a-second stats line (patches/0005): the
        // numbers are emitted as a MirrorStats event just below, the text is noise.
        if (parsed.tag == LineTag::PlistKey || parsed.tag == LineTag::PlistInteger ||
            parsed.tag == LineTag::PlistNoise) {
            return;
        }
        if (parsed.tag == LineTag::MirrorStats) {
            for (const HostEvent& ev : parsed.events)
                if (ev.kind == HostEventKind::MirrorStats) emit(ev);
            return;
        }

        if (parsed.tag == LineTag::Error) {
            for (const HostEvent& ev : parsed.events)
                if (ev.kind == HostEventKind::Error) lastErrorLine = ev.message;
        }

        for (const HostEvent& ev : parsed.events) emit(ev);

        // --- state machine ---------------------------------------------------------------------
        // Ordering note: the banner (uxplay.cpp:2997) is printed ~200 lines of code BEFORE
        // "using network ports" (uxplay.cpp:3205), and the ports line is only printed when udp[0]
        // is set, i.e. when -p was passed. buildArgs() always passes -p, so Ports is the reliable
        // "server is up" marker and the banner only confirms the child started.
        const HostState cur = get();
        switch (parsed.tag) {
        case LineTag::Ports:
            if (cur == HostState::Starting) setState(HostState::Waiting);
            break;
        case LineTag::ClientInfo:
            if (cur == HostState::Starting || cur == HostState::Waiting) {
                setState(HostState::Connected);
            }
            break;
        case LineTag::LostConnection:
            if (cur == HostState::Connected) setState(HostState::Waiting);
            break;
        case LineTag::Stopping:
            if (cur == HostState::Connected) setState(HostState::Waiting);
            break;
        default:
            break;
        }
    }

    void readerLoop() {
        std::string pending;
        char buf[4096];
        for (;;) {
            DWORD got = 0;
            if (!ReadFile(hReadPipe, buf, sizeof(buf), &got, nullptr) || got == 0) break;
            pending.append(buf, got);
            size_t nl;
            while ((nl = pending.find('\n')) != std::string::npos) {
                std::string line = pending.substr(0, nl);
                while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
                pending.erase(0, nl + 1);
                handleLine(line);
            }
        }
        if (!pending.empty()) {
            while (!pending.empty() && (pending.back() == '\r' || pending.back() == '\n')) {
                pending.pop_back();
            }
            if (!pending.empty()) handleLine(pending);
        }
        closePinRun();

        // EOF on the pipe: every writer closed it, so the child is gone or going.
        WaitForSingleObject(hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(hProcess, &code);

        if (stopRequested.load(std::memory_order_acquire)) {
            setState(HostState::Stopped);
        } else {
            char msg[512];
            if (lastErrorLine.empty()) {
                std::snprintf(msg, sizeof(msg), "uxplay.exe exited unexpectedly (exit code %lu)",
                              static_cast<unsigned long>(code));
            } else {
                std::snprintf(msg, sizeof(msg), "uxplay.exe exited (code %lu): %s",
                              static_cast<unsigned long>(code), lastErrorLine.c_str());
            }
            emitError(msg);
            setState(HostState::Error);
        }
    }
};

// -------------------------------------------------------------------------------------------------

UxplayHost::UxplayHost() : impl_(new Impl()) {}

UxplayHost::~UxplayHost() {
    stop();
}

void UxplayHost::setCallback(HostEventCallback cb) {
    std::lock_guard<std::mutex> lk(impl_->cbMutex);
    impl_->cb = std::move(cb);
}

bool UxplayHost::isRunning() const {
    const HostState s = impl_->get();
    return s == HostState::Starting || s == HostState::Waiting ||
           s == HostState::Connected || s == HostState::Stopping;
}

HostState UxplayHost::state() const { return impl_->get(); }

uint32_t UxplayHost::pid() const { return impl_->childPid.load(std::memory_order_acquire); }

std::vector<std::wstring> UxplayHost::buildArgs(const HostConfig& cfg) {
    std::vector<std::wstring> a;
    a.push_back(cfg.uxplayExe);                                  // argv[0]
    a.push_back(L"-n");   a.push_back(widen(cfg.name));          // uxplay.cpp:1233
    a.push_back(L"-nh");                                         // uxplay.cpp:1249
    a.push_back(L"-p");                                          // uxplay.cpp:1334-1338
    if (cfg.port > 0) {
        a.push_back(std::to_wstring(cfg.port));                  // TCP+UDP n,n+1,n+2 (:1347-1352)
    }                                                            // bare -p => legacy set (:1336-37)
    a.push_back(L"-vs");  a.push_back(widen(cfg.videoSink));     // uxplay.cpp:3092-3099
    a.push_back(L"-as");  a.push_back(widen(cfg.audioSink));     // uxplay.cpp:1410-1413
    if (cfg.fullscreen) a.push_back(L"-fs");                     // uxplay.cpp:1448
    if (cfg.h265)       a.push_back(L"-h265");                   // uxplay.cpp:1751
    if (cfg.debug)      a.push_back(L"-d");                      // uxplay.cpp:1369
    if (cfg.noHold)     a.push_back(L"-nohold");                 // uxplay.cpp:1597
    if (cfg.maxFps > 0) {                                        // uxplay.cpp:1313-1320
        a.push_back(L"-fps");
        a.push_back(std::to_wstring(cfg.maxFps));
    }
    if (!cfg.videoDecoder.empty()) {                             // uxplay.cpp:1393-1396
        a.push_back(L"-vd");
        a.push_back(widen(cfg.videoDecoder));
    }
    // Always on (uxplay.cpp:1451). The reports are how we know whether the phone is still
    // producing frames; the XML never reaches the log, only the parsed rate does.
    a.push_back(L"-FPSdata");
    a.push_back(L"-reset"); a.push_back(std::to_wstring(cfg.resetSeconds));  // uxplay.cpp:1452
    for (const std::string& extra : cfg.extraArgs) a.push_back(widen(extra));
    return a;
}

bool UxplayHost::start(const HostConfig& cfg, std::string* err) {
    std::lock_guard<std::mutex> lk(impl_->lifecycle);

    if (isRunning()) {
        if (err) *err = "already running";
        return false;
    }
    // A previous run may have finished on its own; reap its thread/handles first.
    if (impl_->reader.joinable()) impl_->reader.join();
    impl_->closeHandlesLocked();
    impl_->stopRequested.store(false, std::memory_order_release);
    impl_->pinRunActive = false;
    impl_->pinRunDecoded = false;
    impl_->lastErrorLine.clear();
    impl_->pendingPlistKey.clear();

    const auto fail = [&](const std::string& text) {
        if (err) *err = text;
        impl_->emitError(text);
        impl_->setState(HostState::Error);
        return false;
    };

    if (cfg.uxplayExe.empty() ||
        GetFileAttributesW(cfg.uxplayExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return fail("uxplay.exe not found: " + narrow(cfg.uxplayExe));
    }

    // --- environment ---------------------------------------------------------------------------
    ChildEnvConfig envCfg;
    envCfg.msysRoot = cfg.msysRoot;
    envCfg.homeDir = cfg.homeDir;
    envCfg.gstRegistry = cfg.gstRegistry;
    std::string envWarn;
    std::vector<wchar_t> envBlock = buildChildEnvironment(envCfg, &envWarn);
    if (!envWarn.empty()) {
        HostEvent ev;
        ev.kind = HostEventKind::Warning;
        ev.message = envWarn;
        impl_->emit(ev);
    }

    // --- stdout+stderr pipe (one anonymous pipe, both streams merged) ---------------------------
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &sa, 64 * 1024)) {
        return fail(lastErrorText("CreatePipe", GetLastError()));
    }
    // Only the write end may be inherited; otherwise the reader never sees EOF.
    if (!SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0)) {
        const DWORD e = GetLastError();
        CloseHandle(readEnd); CloseHandle(writeEnd);
        return fail(lastErrorText("SetHandleInformation", e));
    }

    // uxplay never reads stdin (verified: no stdin/getchar/fgets in uxplay.cpp); hand it NUL so a
    // stray read can never block on our own console.
    HANDLE nulIn = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                               OPEN_EXISTING, 0, nullptr);

    // --- command line --------------------------------------------------------------------------
    const std::vector<std::wstring> args = buildArgs(cfg);
    std::wstring cmdline;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) cmdline.push_back(L' ');
        cmdline += quoteArg(args[i]);
    }
    std::vector<wchar_t> cmdlineBuf(cmdline.begin(), cmdline.end());
    cmdlineBuf.push_back(L'\0');   // CreateProcessW may modify this buffer

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nulIn != INVALID_HANDLE_VALUE ? nulIn : nullptr;
    si.hStdOutput = writeEnd;
    si.hStdError = writeEnd;

    PROCESS_INFORMATION pi{};
    const DWORD flags = CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT;
    const BOOL ok = CreateProcessW(cfg.uxplayExe.c_str(), cmdlineBuf.data(), nullptr, nullptr,
                                   TRUE, flags, envBlock.data(), nullptr, &si, &pi);
    const DWORD createErr = GetLastError();

    // The parent must drop its copy of the write end (and of NUL) immediately: as long as it is
    // open, ReadFile on the read end never returns EOF after the child dies.
    CloseHandle(writeEnd);
    if (nulIn != INVALID_HANDLE_VALUE) CloseHandle(nulIn);

    if (!ok) {
        CloseHandle(readEnd);
        return fail(lastErrorText("CreateProcessW", createErr));
    }

    // Assign before the first resume-equivalent work happens; a failure here is not fatal,
    // it only means we lose the orphan protection.
    impl_->hJob = CreateJobObjectW(nullptr, nullptr);
    if (impl_->hJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(impl_->hJob, JobObjectExtendedLimitInformation, &limits,
                                sizeof(limits));
        AssignProcessToJobObject(impl_->hJob, pi.hProcess);
    }

    impl_->hProcess = pi.hProcess;
    impl_->hProcThread = pi.hThread;
    impl_->hReadPipe = readEnd;
    impl_->childPid.store(pi.dwProcessId, std::memory_order_release);
    impl_->setState(HostState::Starting);
    impl_->emitLog("start: " + narrow(cmdline));

    impl_->reader = std::thread([this] { impl_->readerLoop(); });
    return true;
}

void UxplayHost::stop(unsigned graceMs) {
    std::lock_guard<std::mutex> lk(impl_->lifecycle);

    if (!impl_->hProcess) {
        if (impl_->reader.joinable()) impl_->reader.join();
        if (impl_->get() != HostState::Error) impl_->setState(HostState::Stopped);
        return;
    }

    const HANDLE hProcess = impl_->hProcess;
    const DWORD childPid = impl_->childPid.load(std::memory_order_acquire);

    if (WaitForSingleObject(hProcess, 0) == WAIT_OBJECT_0) {
        // The child died on its own before stop() was called: let the reader keep whatever verdict
        // it reached (Error + exit code) instead of masking it with a Stopping/Stopped pair.
        if (impl_->reader.joinable()) impl_->reader.join();
        impl_->closeHandlesLocked();
        if (impl_->get() != HostState::Error) impl_->setState(HostState::Stopped);
        return;
    }

    impl_->stopRequested.store(true, std::memory_order_release);
    impl_->setState(HostState::Stopping);

    bool graceful = false;
    {
        // Graceful path: console control event to the child's own process group.
        // NOTE: CTRL_C_EVENT is disabled for a group created with CREATE_NEW_PROCESS_GROUP, so
        // CTRL_BREAK_EVENT is the only usable signal. uxplay's CtrlHandler (uxplay.cpp:587-599)
        // handles CTRL_C/CTRL_CLOSE/CTRL_SHUTDOWN and returns FALSE for CTRL_BREAK, i.e. the OS
        // default action ends the process — it exits, but without running cleanup().
        const bool hadConsole = (GetConsoleWindow() != nullptr);
        FreeConsole();
        if (AttachConsole(childPid)) {
            SetConsoleCtrlHandler(nullptr, TRUE);              // don't kill ourselves
            GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, childPid);
            FreeConsole();
            SetConsoleCtrlHandler(nullptr, FALSE);
        }
        if (hadConsole && AttachConsole(ATTACH_PARENT_PROCESS)) {
            // Best effort: a console app that inherited its console loses stdout across the
            // FreeConsole above. Harmless when stdout is a pipe or when there is no console.
            FILE* f = nullptr;
            freopen_s(&f, "CONOUT$", "w", stdout);
            freopen_s(&f, "CONOUT$", "w", stderr);
        }

        graceful = (WaitForSingleObject(hProcess, graceMs) == WAIT_OBJECT_0);
        if (!graceful) TerminateProcess(hProcess, 1);
    }
    impl_->emitLog(graceful ? "stop: graceful" : "stop: terminated");

    WaitForSingleObject(hProcess, INFINITE);
    if (impl_->reader.joinable()) impl_->reader.join();     // sets Stopped (stopRequested == true)
    impl_->closeHandlesLocked();
    impl_->setState(HostState::Stopped);
}

} // namespace airplay
