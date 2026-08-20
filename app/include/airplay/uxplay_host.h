// uxplay_host.h — contract between the UI (app/src/ui) and the child-process host (app/src/host).
// Milestone 1: the GUI drives a child uxplay.exe (see docs/DESIGN.md §6.1) and parses its stdout.
// Owner of this header: the orchestrator. Lanes implement against it; do not change signatures
// without updating docs/PHASE2-SPEC.md.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace airplay {

enum class HostState {
    Stopped,     // no child process
    Starting,    // CreateProcess done, banner not yet seen
    Waiting,     // server up, advertising, no client ("using network ports ..." seen)
    Connected,   // "connection request from ..." seen, no "lost connection"/"Stopping" since
    Stopping,    // stop requested, waiting for child exit
    Error        // child exited unexpectedly or failed to start; see HostEvent::message
};

enum class HostEventKind {
    StateChanged,    // state field valid
    LogLine,         // one raw stdout line (UTF-8), message field valid
    ClientInfo,      // client name/model/deviceid parsed (uxplay.cpp:2282)
    Ports,           // ports parsed (uxplay.cpp:3205): udp[3], tcp[3]
    Resolution,      // only with debug=true: width/height parsed (lib/raop_rtp_mirror.c:608-609)
    Pin,             // pairing PIN block seen (uxplay.cpp:2196-2205); message = digits if parsed
    Warning,         // "*** WARNING: ..." line
    Error            // "*** ERROR: ..." line (does not necessarily change state)
};

struct HostEvent {
    HostEventKind kind{};
    HostState     state{HostState::Stopped};   // valid for StateChanged
    std::string   message;                     // raw line / error text / pin digits
    // ClientInfo
    std::string   clientName, clientModel, clientDeviceId;
    // Ports
    int udpPorts[3]{0,0,0}, tcpPorts[3]{0,0,0};
    // Resolution (src = iPhone, plain = after scaling)
    int srcWidth{0}, srcHeight{0}, width{0}, height{0};
};

struct HostConfig {
    std::wstring uxplayExe;          // absolute path to uxplay.exe
    std::wstring msysRoot;           // e.g. L"C:\\msys64" -> <root>\ucrt64\bin prepended to PATH,
                                     // GST_PLUGIN_SYSTEM_PATH=<root>\ucrt64\lib\gstreamer-1.0
    std::wstring homeDir;            // HOME + XDG_CONFIG_HOMEDIR (uxplay.cpp:768-779 trap); created if missing
    std::wstring gstRegistry;        // GST_REGISTRY file path (own cache, avoids clashes with other GStreamer installs)
    std::string  name{"AirPlay-PC"}; // -n <name> -nh
    int          port{7100};         // -p <n> (opens n..n+2 TCP+UDP, uxplay.cpp:1347-1352); 0 = bare -p (legacy set)
    std::string  videoSink{"d3d11videosink"};   // -vs
    std::string  audioSink{"autoaudiosink"};    // -as
    bool fullscreen{false};          // -fs
    bool h265{false};                // -h265
    bool debug{false};               // -d  (required for Resolution events; re-enables stdout buffering!)
    bool noHold{true};               // -nohold (new client may take over)
    int  resetSeconds{15};           // -reset <n>; 0 disables
    std::vector<std::string> extraArgs; // passed through verbatim, last
};

// Callback is invoked on the host's reader thread. The UI must marshal to its own thread
// (e.g. PostMessage) — never touch HWNDs directly from the callback.
using HostEventCallback = std::function<void(const HostEvent&)>;

class UxplayHost {
public:
    UxplayHost();
    ~UxplayHost();                   // calls stop() if running
    UxplayHost(const UxplayHost&) = delete;
    UxplayHost& operator=(const UxplayHost&) = delete;

    void setCallback(HostEventCallback cb);

    // Builds argv from cfg, sets env, CreateProcessW with redirected stdout/stderr (merged),
    // CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW, starts reader thread. Returns false + error
    // text (via Error event and 'err') if the exe is missing or CreateProcess fails.
    bool start(const HostConfig& cfg, std::string* err = nullptr);

    // Graceful stop: CTRL_BREAK_EVENT to the child's process group (AttachConsole trick), wait up to
    // graceMs, then TerminateProcess. Returns when the child has exited. Safe to call when stopped.
    void stop(unsigned graceMs = 3000);

    bool      isRunning() const;
    HostState state() const;
    uint32_t  pid() const;           // 0 when stopped

    // Builds the exact argv that start() would use (for the UI's "command line" display and tests).
    static std::vector<std::wstring> buildArgs(const HostConfig& cfg);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Pure, testable line classifier used by UxplayHost (app/src/host/line_parser.*).
// Given one stdout line (trailing CR/LF stripped), returns 0..n events (StateChanged events are
// NOT produced here — state transitions are decided by UxplayHost from the event kinds).
std::vector<HostEvent> parseUxplayLine(const std::string& line);

} // namespace airplay
