// test_line_parser.cpp — unit tests for airplay::parseUxplayLine / parseUxplayLineDetailed and
// UxplayHost::buildArgs. Hand-rolled assertions, no external test framework.
//
// One test per row of docs/DESIGN.md §6.1 "Lines we can parse for state", plus the generic
// WARNING/ERROR levels and the unknown-line fallback. Every sample line below is the literal
// output of the format string at the cited third_party/UxPlay/uxplay.cpp line.
#include <cstdio>
#include <string>
#include <vector>

#include "airplay/uxplay_host.h"
#include "line_parser.h"

using namespace airplay;

// -------------------------------------------------------------------------------------------------
namespace {

int g_tests = 0;
int g_checks = 0;
int g_failed = 0;
const char* g_current = "";

void beginTest(const char* name) { g_current = name; ++g_tests; }

void reportFailure(const char* file, int line, const std::string& what) {
    ++g_failed;
    std::printf("FAIL  [%s] %s:%d  %s\n", g_current, file, line, what.c_str());
}

void checkTrue(bool cond, const char* expr, const char* file, int line) {
    ++g_checks;
    if (!cond) reportFailure(file, line, std::string("expected true: ") + expr);
}

void checkEqStr(const std::string& got, const std::string& want, const char* expr,
                const char* file, int line) {
    ++g_checks;
    if (got != want) {
        reportFailure(file, line,
                      std::string(expr) + "\n        got  = [" + got + "]\n        want = [" + want + "]");
    }
}

void checkEqInt(long long got, long long want, const char* expr, const char* file, int line) {
    ++g_checks;
    if (got != want) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s  got = %lld  want = %lld", expr, got, want);
        reportFailure(file, line, buf);
    }
}

#define CHECK(cond)          checkTrue((cond), #cond, __FILE__, __LINE__)
#define CHECK_STR(got, want) checkEqStr((got), (want), #got, __FILE__, __LINE__)
#define CHECK_INT(got, want) checkEqInt((got), (want), #got, __FILE__, __LINE__)
#define CHECK_TAG(p, want)   checkEqInt(static_cast<long long>((p).tag), \
                                        static_cast<long long>(want), "tag", __FILE__, __LINE__)

const HostEvent* find(const std::vector<HostEvent>& evs, HostEventKind kind) {
    for (const HostEvent& e : evs) {
        if (e.kind == kind) return &e;
    }
    return nullptr;
}

// Every parse must yield exactly one LogLine carrying the raw line, as the first event.
void checkLogLine(const ParsedLine& p, const std::string& raw, const char* file, int line) {
    ++g_checks;
    if (p.events.empty() || p.events[0].kind != HostEventKind::LogLine ||
        p.events[0].message != raw) {
        reportFailure(file, line, "events[0] must be LogLine with the raw line, got " +
                                      (p.events.empty() ? std::string("<none>")
                                                        : p.events[0].message));
    }
}
#define CHECK_LOGLINE(p, raw) checkLogLine((p), (raw), __FILE__, __LINE__)

std::wstring joinArgs(const std::vector<std::wstring>& a) {
    std::wstring s;
    for (size_t i = 0; i < a.size(); ++i) {
        if (i) s.push_back(L' ');
        s += a[i];
    }
    return s;
}

std::string narrowAscii(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s.push_back(c < 128 ? static_cast<char>(c) : '?');
    return s;
}

// -------------------------------------------------------------------------------------------------
// DESIGN §6.1 row 1 — startup banner (uxplay.cpp:2997)
void testBanner() {
    beginTest("banner");
    const std::string raw = "UxPlay 1.74: An Open-Source AirPlay mirroring and audio-streaming server.";
    ParsedLine p = parseUxplayLineDetailed(raw);
    CHECK_LOGLINE(p, raw);
    CHECK_TAG(p, LineTag::Banner);
    CHECK_INT(static_cast<long long>(p.events.size()), 1);
}

// row 2 — ports (uxplay.cpp:3205)
void testPorts() {
    beginTest("ports");
    const std::string raw = "using network ports UDP 7300 7301 7302 TCP 7300 7301 7302";
    ParsedLine p = parseUxplayLineDetailed(raw);
    CHECK_LOGLINE(p, raw);
    CHECK_TAG(p, LineTag::Ports);
    const HostEvent* e = find(p.events, HostEventKind::Ports);
    CHECK(e != nullptr);
    if (e) {
        CHECK_INT(e->udpPorts[0], 7300);
        CHECK_INT(e->udpPorts[1], 7301);
        CHECK_INT(e->udpPorts[2], 7302);
        CHECK_INT(e->tcpPorts[0], 7300);
        CHECK_INT(e->tcpPorts[1], 7301);
        CHECK_INT(e->tcpPorts[2], 7302);
    }
    // the legacy set printed for bare "-p" (uxplay.cpp:1336-1337)
    ParsedLine legacy = parseUxplayLineDetailed("using network ports UDP 7011 6001 6000 TCP 7100 7000 7001");
    const HostEvent* le = find(legacy.events, HostEventKind::Ports);
    CHECK(le != nullptr);
    if (le) {
        CHECK_INT(le->udpPorts[0], 7011);
        CHECK_INT(le->tcpPorts[0], 7100);
        CHECK_INT(le->tcpPorts[2], 7001);
    }
}

// row 3 — MAC / deviceID (uxplay.cpp:3211, :3213, :3218)
void testMac() {
    beginTest("mac");
    ParsedLine sys = parseUxplayLineDetailed("using system MAC address 3c:22:fb:11:22:33");
    CHECK_TAG(sys, LineTag::Mac);
    CHECK_STR(sys.detail, "3c:22:fb:11:22:33");
    ParsedLine usr = parseUxplayLineDetailed("using user-set MAC address 01:02:03:04:05:06");
    CHECK_TAG(usr, LineTag::Mac);
    CHECK_STR(usr.detail, "01:02:03:04:05:06");
    ParsedLine rnd = parseUxplayLineDetailed("using randomly-generated MAC address aa:bb:cc:dd:ee:ff");
    CHECK_TAG(rnd, LineTag::Mac);
    CHECK_STR(rnd.detail, "aa:bb:cc:dd:ee:ff");
}

// row 4 — key persistence (uxplay.cpp:3165)
void testKeyStorage() {
    beginTest("key storage");
    const std::string raw = "public key storage (for persistence) is in C:\\Users\\pc\\AppData\\Roaming\\airplay/.uxplay.pem";
    ParsedLine p = parseUxplayLineDetailed(raw);
    CHECK_LOGLINE(p, raw);
    CHECK_TAG(p, LineTag::KeyStorage);
    CHECK_STR(p.detail, "C:\\Users\\pc\\AppData\\Roaming\\airplay/.uxplay.pem");
}

// row 5 — client connecting (uxplay.cpp:2282)
void testClientInfo() {
    beginTest("client info");
    const std::string raw = "connection request from iPhone (iPhone14,5) with deviceID = 3C:22:FB:AA:BB:CC";
    ParsedLine p = parseUxplayLineDetailed(raw);
    CHECK_LOGLINE(p, raw);
    CHECK_TAG(p, LineTag::ClientInfo);
    const HostEvent* e = find(p.events, HostEventKind::ClientInfo);
    CHECK(e != nullptr);
    if (e) {
        CHECK_STR(e->clientName, "iPhone");
        CHECK_STR(e->clientModel, "iPhone14,5");
        CHECK_STR(e->clientDeviceId, "3C:22:FB:AA:BB:CC");
    }
}

// row 5b — the name is user-chosen: spaces and parentheses must not break the parse.
void testClientInfoAwkwardName() {
    beginTest("client info, awkward name");
    const std::string raw =
        "connection request from Mustafa's iPhone (work) (iPhone14,5) with deviceID = 3C:22:FB:00:11:22";
    ParsedLine p = parseUxplayLineDetailed(raw);
    CHECK_TAG(p, LineTag::ClientInfo);
    const HostEvent* e = find(p.events, HostEventKind::ClientInfo);
    CHECK(e != nullptr);
    if (e) {
        CHECK_STR(e->clientName, "Mustafa's iPhone (work)");
        CHECK_STR(e->clientModel, "iPhone14,5");
        CHECK_STR(e->clientDeviceId, "3C:22:FB:00:11:22");
    }
}

// row 6 — restrict-list refusal (uxplay.cpp:2286); the format string embeds '\n' => two lines.
void testClientRejected() {
    beginTest("client rejected");
    ParsedLine a = parseUxplayLineDetailed(
        "client connections have been restricted to those with listed deviceID,");
    CHECK_TAG(a, LineTag::ClientRejected);
    CHECK(find(a.events, HostEventKind::Warning) != nullptr);
    ParsedLine b = parseUxplayLineDetailed(
        "use \"-allow 3C:22:FB:AA:BB:CC\" to allow this client to connect.");
    CHECK_TAG(b, LineTag::ClientRejected);
    CHECK(find(b.events, HostEventKind::Warning) != nullptr);
}

// row 7 — blocked client (uxplay.cpp:2294)
void testClientBlocked() {
    beginTest("client blocked");
    const std::string raw = "*** attempt to connect by blocked client (clientID 3C:22:FB:AA:BB:CC): DENIED";
    ParsedLine p = parseUxplayLineDetailed(raw);
    CHECK_LOGLINE(p, raw);
    CHECK_TAG(p, LineTag::ClientBlocked);
    const HostEvent* e = find(p.events, HostEventKind::Warning);
    CHECK(e != nullptr);
    if (e) CHECK_STR(e->message, raw);
}

// row 8 — negotiated audio format (uxplay.cpp:2449): ct=8 mirroring, ct=2 audio-only
void testAudioFormat() {
    beginTest("audio format");
    ParsedLine mirror = parseUxplayLineDetailed(
        "ct=8 spf=352 usingScreen=1 isMedia=0  audioFormat=0x400000000000000");
    CHECK_TAG(mirror, LineTag::AudioFormat);
    CHECK_STR(mirror.detail, "mirroring");
    CHECK_INT(static_cast<long long>(mirror.events.size()), 2);
    CHECK_STR(mirror.events[1].message, "mirroring");

    ParsedLine audio = parseUxplayLineDetailed(
        "ct=2 spf=352 usingScreen=0 isMedia=0  audioFormat=0x100000000000000");
    CHECK_TAG(audio, LineTag::AudioFormat);
    CHECK_STR(audio.detail, "audio-only");
    CHECK_STR(audio.events[1].message, "audio-only");
}

// row 9 — pairing PIN block (uxplay.cpp:2196-2205 -> create_pin_display :395-465).
// The block is ASCII art only; the digits never appear as text. Row index 4 of the 8-row glyph is
// the only row whose ten patterns are pairwise distinct, so exactly that row yields a Pin event.
void testPinArt() {
    beginTest("pin art");
    // PIN 1234, margin 10, gap 3 — rendered from the digits[] table in uxplay.cpp:399-411.
    const std::string row0 = "             d888       .8888d.      .d8888b.        d8888    ";
    const std::string row4 = "              888       .od888P\"         \"Y8b    d88   888    ";

    CHECK(isPinArtLine(row0));
    CHECK(isPinArtLine(row4));
    CHECK(!isPinArtLine("using network ports UDP 7300 7301 7302 TCP 7300 7301 7302"));
    CHECK_STR(decodePinArtRow(row4), "1234");
    CHECK_STR(decodePinArtRow(row0), "");   // ambiguous row: no digits

    ParsedLine p4 = parseUxplayLineDetailed(row4);
    CHECK_LOGLINE(p4, row4);
    CHECK_TAG(p4, LineTag::PinArt);
    const HostEvent* e = find(p4.events, HostEventKind::Pin);
    CHECK(e != nullptr);
    if (e) CHECK_STR(e->message, "1234");

    ParsedLine p0 = parseUxplayLineDetailed(row0);
    CHECK_TAG(p0, LineTag::PinArt);
    CHECK(find(p0.events, HostEventKind::Pin) == nullptr);   // host synthesises the empty Pin
}

// row 10 — new client registered (uxplay.cpp:2570)
void testRegisteredClient() {
    beginTest("registered new client");
    const std::string raw = "registered new client: iPhone DeviceID = 3C:22:FB:AA:BB:CC PK = ";
    ParsedLine p = parseUxplayLineDetailed(raw);
    CHECK_LOGLINE(p, raw);
    CHECK_TAG(p, LineTag::RegisteredClient);
    CHECK_STR(p.detail, "iPhone");
}

// row 11 — connection lost. Two spellings: uxplay.cpp:2264 "*** ERROR lost ..." (via conn_reset)
// and uxplay.cpp:538 "***ERROR lost ..." (via feedback_callback, no space). Both are LOGI, so
// neither carries log()'s "*** ERROR: " prefix.
void testLostConnection() {
    beginTest("lost connection");
    const std::string a = "*** ERROR lost connection with client (network problem?)";
    ParsedLine pa = parseUxplayLineDetailed(a);
    CHECK_LOGLINE(pa, a);
    CHECK_TAG(pa, LineTag::LostConnection);
    const HostEvent* ea = find(pa.events, HostEventKind::Error);
    CHECK(ea != nullptr);
    if (ea) CHECK_STR(ea->message, a);

    const std::string b = "***ERROR lost connection with client (network problem?)";
    ParsedLine pb = parseUxplayLineDetailed(b);
    CHECK_TAG(pb, LineTag::LostConnection);
    CHECK(find(pb.events, HostEventKind::Error) != nullptr);
}

// row 12 — missed-feedback detail (uxplay.cpp:539); printed indented by three spaces.
void testFeedbackTimeout() {
    beginTest("feedback timeout");
    const std::string raw = "   Interval since last client feedback request exceeds limit of 15 seconds";
    ParsedLine p = parseUxplayLineDetailed(raw);
    CHECK_LOGLINE(p, raw);   // LogLine keeps the original indentation
    CHECK_TAG(p, LineTag::FeedbackTimeout);
    CHECK(find(p.events, HostEventKind::Warning) != nullptr);
}

// row 13 — shutdown (uxplay.cpp:3309)
void testStopping() {
    beginTest("stopping");
    ParsedLine p = parseUxplayLineDetailed("Stopping RAOP Server...");
    CHECK_TAG(p, LineTag::Stopping);
    CHECK_INT(static_cast<long long>(p.events.size()), 1);
}

// row 14 — sinks disabled (uxplay.cpp:3080 / :3184)
void testDisabledSinks() {
    beginTest("video/audio disabled");
    CHECK_TAG(parseUxplayLineDetailed("video_disabled"), LineTag::VideoDisabled);
    CHECK_TAG(parseUxplayLineDetailed("audio_disabled"), LineTag::AudioDisabled);
}

// row 15 — fullscreen hint (uxplay.cpp:3097 / :3106)
void testFullscreenHint() {
    beginTest("fullscreen hint");
    ParsedLine p = parseUxplayLineDetailed(
        "Use Alt-Enter key combination to toggle into/out of full-screen mode");
    CHECK_TAG(p, LineTag::FullscreenHint);
}

// row 16 — mirrored resolution, DEBUG level only (lib/raop_rtp_mirror.c:608-609); needs -d.
void testResolution() {
    beginTest("resolution (debug)");
    const std::string raw =
        "raop_rtp_mirror width_source = 1170.000000 height_source = 2532.000000 width = 1170.000000 height = 2532.000000";
    ParsedLine p = parseUxplayLineDetailed(raw);
    CHECK_LOGLINE(p, raw);
    CHECK_TAG(p, LineTag::Resolution);
    const HostEvent* e = find(p.events, HostEventKind::Resolution);
    CHECK(e != nullptr);
    if (e) {
        CHECK_INT(e->srcWidth, 1170);
        CHECK_INT(e->srcHeight, 2532);
        CHECK_INT(e->width, 1170);
        CHECK_INT(e->height, 2532);
    }
}

// generic levels — log() prefixes only these two (uxplay.cpp:239 / :242)
void testWarning() {
    beginTest("warning");
    ParsedLine p = parseUxplayLineDetailed("*** WARNING: no audio sink found");
    CHECK_TAG(p, LineTag::Warning);
    const HostEvent* e = find(p.events, HostEventKind::Warning);
    CHECK(e != nullptr);
    if (e) CHECK_STR(e->message, "no audio sink found");
}

void testError() {
    beginTest("error");
    ParsedLine p = parseUxplayLineDetailed("*** ERROR: stopping");
    CHECK_TAG(p, LineTag::Error);
    const HostEvent* e = find(p.events, HostEventKind::Error);
    CHECK(e != nullptr);
    if (e) CHECK_STR(e->message, "stopping");
}

// uxplay.cpp:1207 - argv encoding rejection, written with fprintf(stderr) so there is no
// "*** " prefix. It is followed by exit(0), which on its own looks like a clean shutdown.
void testBareErrorLine() {
    beginTest("bare Error: line");
    const std::string raw =
        "Error: detected a non-ascii or non-UTF-8 string \"orpc?cpcu?\""
        "while parsing input arguments";
    ParsedLine p = parseUxplayLineDetailed(raw);
    CHECK_TAG(p, LineTag::Error);
    const HostEvent* e = find(p.events, HostEventKind::Error);
    CHECK(e != nullptr);
    if (e) CHECK_STR(e->message, raw);

    // uxplay.cpp:1241 - the -n specific rejection
    ParsedLine q = parseUxplayLineDetailed("invalid (non-UTF-8/ascii) server name in \"-n xx\"");
    CHECK_TAG(q, LineTag::Error);
    CHECK(find(q.events, HostEventKind::Error) != nullptr);
}

// uxplay.cpp:549 - the client stopped asking for feedback. An iPhone with its screen off
// prints this once a second; it is NOT a failure and must not surface as one.
void testFeedbackLate() {
    beginTest("feedback late is not an error");
    ParsedLine p = parseUxplayLineDetailed(
        "*** ERROR:   3 seconds since last client feedback request "
        "(expected every two seconds); client may be offline");
    CHECK_TAG(p, LineTag::FeedbackLate);
    // Recognised, but must not reach the user as an error - and it is not a stall either:
    // a sleeping iPhone keeps requesting feedback.
    CHECK(find(p.events, HostEventKind::Error) == nullptr);
    CHECK(find(p.events, HostEventKind::MirrorFps) == nullptr);
    CHECK_INT(static_cast<long long>(p.events.size()), 1);   // the LogLine only
}

// -FPSdata reports (lib/raop_rtp_mirror.c:807-811). submitSurfaceFPS == 0 is the only
// reliable "the phone is not producing frames" signal - the client keeps sending feedback
// while its screen is off, so the feedback timer never notices.
void testPlistReport() {
    beginTest("FPSdata plist lines");
    ParsedLine k = parseUxplayLineDetailed("	<key>submitSurfaceFPS</key>");
    CHECK_TAG(k, LineTag::PlistKey);
    CHECK_STR(k.detail, "submitSurfaceFPS");

    ParsedLine v = parseUxplayLineDetailed("	<integer>0</integer>");
    CHECK_TAG(v, LineTag::PlistInteger);
    CHECK_STR(v.detail, "0");

    ParsedLine v2 = parseUxplayLineDetailed("	<integer>44</integer>");
    CHECK_STR(v2.detail, "44");

    for (const char* noise : {"<?xml version=\"1.0\" encoding=\"UTF-8\"?>", "<plist version=\"1.0\">",
                              "<dict>", "</dict>", "</plist>", "	<real>1.5e-05</real>"})
        CHECK_TAG(parseUxplayLineDetailed(noise), LineTag::PlistNoise);
}

// unknown line -> LogLine only
void testUnknown() {
    beginTest("unknown line");
    const std::string raw = "*** NOTE CHANGE: -reset n now means reset n seconds (not 3n seconds) after client goes offline";
    std::vector<HostEvent> evs = parseUxplayLine(raw);
    CHECK_INT(static_cast<long long>(evs.size()), 1);
    CHECK(evs[0].kind == HostEventKind::LogLine);
    CHECK_STR(evs[0].message, raw);
    CHECK_TAG(parseUxplayLineDetailed(raw), LineTag::Unknown);

    std::vector<HostEvent> empty = parseUxplayLine("");
    CHECK_INT(static_cast<long long>(empty.size()), 1);
    CHECK(empty[0].kind == HostEventKind::LogLine);
}

// trailing CR/LF must never reach the classifier or the LogLine text
void testEolStripping() {
    beginTest("CRLF stripping");
    std::vector<HostEvent> evs = parseUxplayLine("Stopping RAOP Server...\r\n");
    CHECK_STR(evs[0].message, "Stopping RAOP Server...");
    CHECK_TAG(parseUxplayLineDetailed("Stopping RAOP Server...\r"), LineTag::Stopping);
}

// -------------------------------------------------------------------------------------------------
// buildArgs — docs/DESIGN.md §6.1 argv table
HostConfig baseConfig() {
    HostConfig c;
    c.uxplayExe = L"C:\\airplay\\build\\uxplay.exe";
    c.name = "AirPlay-PC";
    return c;
}

void testBuildArgsDefault() {
    beginTest("buildArgs default");
    std::string s = narrowAscii(joinArgs(UxplayHost::buildArgs(baseConfig())));
    CHECK_STR(s,
              "C:\\airplay\\build\\uxplay.exe -n AirPlay-PC -nh -p 7100 "
              "-vs d3d11videosink -as autoaudiosink -nohold -fps 60 -FPSdata -reset 15");
}

void testBuildArgsBarePort() {
    beginTest("buildArgs port 0 => bare -p");
    HostConfig c = baseConfig();
    c.port = 0;
    std::vector<std::wstring> a = UxplayHost::buildArgs(c);
    std::string s = narrowAscii(joinArgs(a));
    CHECK(s.find(" -p -vs ") != std::string::npos);
    CHECK(s.find(" -p 0") == std::string::npos);
}

void testBuildArgsFlags() {
    beginTest("buildArgs flags");
    HostConfig c = baseConfig();
    c.fullscreen = true;
    c.h265 = true;
    c.debug = true;
    c.noHold = false;
    c.resetSeconds = 0;
    c.videoSink = "d3d12videosink";
    c.audioSink = "wasapi2sink";
    std::string s = narrowAscii(joinArgs(UxplayHost::buildArgs(c)));
    CHECK_STR(s,
              "C:\\airplay\\build\\uxplay.exe -n AirPlay-PC -nh -p 7100 "
              "-vs d3d12videosink -as wasapi2sink -fs -h265 -d -fps 60 -FPSdata -reset 0");
}

// -fps / -vd / -FPSdata: the smoothness levers. maxFps 0 must omit -fps entirely so the
// library default (30, lib/raop.c:623) applies.
void testBuildArgsSmoothness() {
    beginTest("buildArgs fps/decoder");
    HostConfig c = baseConfig();
    c.maxFps = 0;
    std::string s = narrowAscii(joinArgs(UxplayHost::buildArgs(c)));
    CHECK(s.find("-fps") == std::string::npos);

    c.maxFps = 30;
    c.videoDecoder = "d3d11h264dec";
    s = narrowAscii(joinArgs(UxplayHost::buildArgs(c)));
    CHECK(s.find(" -fps 30 ") != std::string::npos);
    CHECK(s.find(" -vd d3d11h264dec ") != std::string::npos);
    // -FPSdata is unconditional: the stall detection depends on those reports.
    CHECK(s.find(" -FPSdata") != std::string::npos);
}

void testBuildArgsExtrasLast() {
    beginTest("buildArgs extraArgs last");
    HostConfig c = baseConfig();
    c.extraArgs = {"-m", "aa:bb:cc:dd:ee:ff"};
    std::vector<std::wstring> a = UxplayHost::buildArgs(c);
    // 16 fixed elements (exe -n name -nh -p 7100 -vs .. -as .. -nohold -fps 60 -FPSdata
    // -reset 15) + 2 extras
    CHECK_INT(static_cast<long long>(a.size()), 18);
    CHECK(a[a.size() - 2] == L"-m");
    CHECK(a[a.size() - 1] == L"aa:bb:cc:dd:ee:ff");
}

void testBuildArgsNameWithSpaces() {
    beginTest("buildArgs name kept as one argv element");
    HostConfig c = baseConfig();
    c.name = "Salon PC";
    std::vector<std::wstring> a = UxplayHost::buildArgs(c);
    CHECK(a[1] == L"-n");
    CHECK(a[2] == L"Salon PC");     // quoting happens in start(), not in buildArgs
}

} // namespace

int main() {
    testBanner();
    testPorts();
    testMac();
    testKeyStorage();
    testClientInfo();
    testClientInfoAwkwardName();
    testClientRejected();
    testClientBlocked();
    testAudioFormat();
    testPinArt();
    testRegisteredClient();
    testLostConnection();
    testFeedbackTimeout();
    testStopping();
    testDisabledSinks();
    testFullscreenHint();
    testResolution();
    testWarning();
    testError();
    testBareErrorLine();
    testFeedbackLate();
    testPlistReport();
    testUnknown();
    testEolStripping();
    testBuildArgsDefault();
    testBuildArgsBarePort();
    testBuildArgsFlags();
    testBuildArgsSmoothness();
    testBuildArgsExtrasLast();
    testBuildArgsNameWithSpaces();

    std::printf("%s: %d tests, %d checks, %d failed\n",
                g_failed ? "FAILED" : "OK", g_tests, g_checks, g_failed);
    return g_failed ? 1 : 0;
}
