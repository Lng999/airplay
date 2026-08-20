// host_stub.cpp - lane U development stub for the real host in app/src/host/.
//
// It is compiled ONLY when app/src/host/CMakeLists.txt is missing or -DAIRPLAY_HOST_STUB=ON.
// It spawns no process: it fakes the state machine (Starting -> Waiting after 300 ms, a Ports
// event, a few log lines) so the UI can be exercised on its own. Nothing here is a
// specification - app/include/airplay/uxplay_host.h is.

#include "airplay/uxplay_host.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace airplay {

struct UxplayHost::Impl {
    std::mutex              mtx;
    std::condition_variable cv;
    HostEventCallback       cb;
    std::atomic<bool>       running{false};
    std::atomic<bool>       quit{false};
    std::atomic<uint32_t>   pid{0};
    HostState               state{HostState::Stopped};
    std::thread             worker;

    HostEventCallback snapshot() {
        std::lock_guard<std::mutex> lk(mtx);
        return cb;
    }

    // The callback must never be invoked with the mutex held: the UI thread calls stop()
    // (which takes the mutex) from inside its message pump.
    void emit(const HostEvent& ev) {
        HostEventCallback c = snapshot();
        if (c) c(ev);
    }

    void emitLog(const std::string& line) {
        HostEvent e;
        e.kind    = HostEventKind::LogLine;
        e.message = line;
        emit(e);
    }

    void setState(HostState s) {
        {
            std::lock_guard<std::mutex> lk(mtx);
            state = s;
        }
        HostEvent e;
        e.kind  = HostEventKind::StateChanged;
        e.state = s;
        emit(e);
    }

    bool sleepOrQuit(int ms) {
        std::unique_lock<std::mutex> lk(mtx);
        return !cv.wait_for(lk, std::chrono::milliseconds(ms), [this] { return quit.load(); });
    }
};

UxplayHost::UxplayHost() : impl_(new Impl) {}

UxplayHost::~UxplayHost() { stop(); }

void UxplayHost::setCallback(HostEventCallback cb) {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    impl_->cb = std::move(cb);
}

bool UxplayHost::start(const HostConfig& cfg, std::string* err) {
    if (impl_->running.load()) {
        if (err) *err = "already running";
        return false;
    }
    impl_->quit.store(false);
    impl_->running.store(true);
    impl_->pid.store(4242);
    {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        impl_->state = HostState::Starting;
    }

    HostConfig copy = cfg;
    impl_->worker = std::thread([this, copy] {
        impl_->setState(HostState::Starting);
        if (!impl_->sleepOrQuit(300)) {
            impl_->setState(HostState::Stopped);
            return;
        }

        impl_->emitLog("UxPlay 1.74: An Open-Source AirPlay mirroring and audio-streaming "
                       "server. [STUB HOST - no child process]");
        impl_->emitLog("using network ports UDP 7100 7101 7102 TCP 7100 7101 7102");
        {
            HostEvent e;
            e.kind = HostEventKind::Ports;
            e.udpPorts[0] = e.tcpPorts[0] = copy.port ? copy.port : 7100;
            e.udpPorts[1] = e.tcpPorts[1] = (copy.port ? copy.port : 7100) + 1;
            e.udpPorts[2] = e.tcpPorts[2] = (copy.port ? copy.port : 7100) + 2;
            impl_->emit(e);
        }
        impl_->emitLog("using system MAC address 00:11:22:33:44:55");
        impl_->emitLog("public key storage (for persistence) is in <stub>/.uxplay.pem");
        impl_->emitLog("Initialized server socket(s)");
        impl_->setState(HostState::Waiting);

        int tick = 0;
        while (impl_->sleepOrQuit(2000)) {
            ++tick;
            impl_->emitLog("stub host alive, tick " + std::to_string(tick) + " (name=" +
                           copy.name + ", vs=" + copy.videoSink + ")");
        }
    });
    return true;
}

void UxplayHost::stop(unsigned graceMs) {
    (void)graceMs;
    if (!impl_->running.exchange(false)) return;

    {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        impl_->quit.store(true);
        impl_->state = HostState::Stopping;
    }
    impl_->cv.notify_all();
    if (impl_->worker.joinable()) impl_->worker.join();
    impl_->pid.store(0);
    impl_->setState(HostState::Stopped);
}

bool UxplayHost::isRunning() const { return impl_->running.load(); }

HostState UxplayHost::state() const {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    return impl_->state;
}

uint32_t UxplayHost::pid() const { return impl_->pid.load(); }

std::vector<std::wstring> UxplayHost::buildArgs(const HostConfig& cfg) {
    auto w = [](const std::string& s) {
        return std::wstring(s.begin(), s.end());  // stub only: ASCII sink/name values
    };
    std::vector<std::wstring> a;
    a.push_back(L"-n");
    a.push_back(w(cfg.name));
    a.push_back(L"-nh");
    a.push_back(L"-p");
    if (cfg.port > 0) a.push_back(std::to_wstring(cfg.port));
    a.push_back(L"-vs");
    a.push_back(w(cfg.videoSink));
    a.push_back(L"-as");
    a.push_back(w(cfg.audioSink));
    if (cfg.fullscreen) a.push_back(L"-fs");
    if (cfg.h265) a.push_back(L"-h265");
    if (cfg.noHold) a.push_back(L"-nohold");
    if (cfg.resetSeconds != 15) {
        a.push_back(L"-reset");
        a.push_back(std::to_wstring(cfg.resetSeconds));
    }
    if (cfg.debug) a.push_back(L"-d");
    for (const std::string& e : cfg.extraArgs) a.push_back(w(e));
    return a;
}

std::vector<HostEvent> parseUxplayLine(const std::string& line) {
    HostEvent e;
    e.kind    = HostEventKind::LogLine;
    e.message = line;
    return {e};
}

} // namespace airplay
