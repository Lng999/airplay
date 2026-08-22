// line_parser.cpp — see line_parser.h. Pure C++17, no Win32, no I/O.
#include "line_parser.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace airplay {
namespace {

bool startsWith(const std::string& s, const char* prefix) {
    const size_t n = std::strlen(prefix);
    return s.size() >= n && s.compare(0, n, prefix) == 0;
}

bool endsWith(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
    return s.substr(b, e - b);
}

std::string stripEol(const std::string& s) {
    size_t e = s.size();
    while (e > 0 && (s[e - 1] == '\n' || s[e - 1] == '\r')) --e;
    return s.substr(0, e);
}

HostEvent makeEvent(HostEventKind kind, const std::string& message) {
    HostEvent ev;
    ev.kind = kind;
    ev.message = message;
    return ev;
}

int toInt(float f) { return static_cast<int>(std::lround(f)); }

// --- PIN ASCII art ------------------------------------------------------------------------------
// display_pin() calls create_pin_display(pin, margin = 10, gap = 3) (uxplay.cpp:2197-2199).
// create_pin_display() (uxplay.cpp:395-465) writes, for each of the h = 8 glyph rows,
// `margin` spaces followed by, per digit, w = 10 glyph characters and `gap` spaces.
constexpr size_t kPinMargin = 10;
constexpr size_t kPinGlyphW = 10;
constexpr size_t kPinGap    = 3;

// pixels[] = { ' ', '8', 'd', 'b', 'P', 'Y', 'o', '"', '.' }  (uxplay.cpp:414)
bool isPinPixel(char c) {
    switch (c) {
    case ' ': case '8': case 'd': case 'b':
    case 'P': case 'Y': case 'o': case '"': case '.':
        return true;
    default:
        return false;
    }
}

// digits[l][4] rendered through pixels[] (uxplay.cpp:399-411). Row index 4 is the only one of the
// eight whose ten patterns are pairwise distinct, so it alone identifies a digit on its own.
const char* const kPinRow4[10] = {
    "888    888",   // 0  "1110000111"
    "    888   ",   // 1  "0000111000"
    " .od888P\" ",  // 2  "0862111470"
    "     \"Y8b ",  // 3  "0000075130"
    "d88   888 ",   // 4  "2110001110"
    "     \"Y88b",  // 5  "0000075113"
    "888P \"Y88b",  // 6  "1114075113"
    "   d88P   ",   // 7  "0002114000"
    ".d8P\"\"Y8b.", // 8  "8214775138"
    " \"Y888P888",  // 9  "0751114111"
};

} // namespace

bool isPinArtLine(const std::string& line) {
    if (line.size() < kPinMargin + kPinGlyphW + kPinGap) return false;
    if ((line.size() - kPinMargin) % (kPinGlyphW + kPinGap) != 0) return false;
    for (size_t i = 0; i < kPinMargin; ++i) {
        if (line[i] != ' ') return false;               // the left margin must be blank
    }
    bool anyInk = false;
    for (size_t i = kPinMargin; i < line.size(); ++i) {
        if (!isPinPixel(line[i])) return false;
        if (line[i] != ' ') anyInk = true;
    }
    return anyInk;
}

std::string decodePinArtRow(const std::string& line) {
    if (!isPinArtLine(line)) return std::string();
    const size_t cells = (line.size() - kPinMargin) / (kPinGlyphW + kPinGap);
    std::string digits;
    for (size_t c = 0; c < cells; ++c) {
        const std::string glyph = line.substr(kPinMargin + c * (kPinGlyphW + kPinGap), kPinGlyphW);
        // the gap after each glyph must really be blank
        for (size_t g = 0; g < kPinGap; ++g) {
            if (line[kPinMargin + c * (kPinGlyphW + kPinGap) + kPinGlyphW + g] != ' ') {
                return std::string();
            }
        }
        bool matched = false;
        for (int d = 0; d < 10; ++d) {
            if (glyph == kPinRow4[d]) {
                digits.push_back(static_cast<char>('0' + d));
                matched = true;
                break;
            }
        }
        if (!matched) return std::string();             // not the identifying row (or a new font)
    }
    return digits;
}

ParsedLine parseUxplayLineDetailed(const std::string& rawIn) {
    const std::string raw = stripEol(rawIn);

    ParsedLine out;
    out.events.push_back(makeEvent(HostEventKind::LogLine, raw));   // ALWAYS, contract requirement

    const std::string t = trim(raw);

    // --- "lost connection": two spellings exist. uxplay.cpp:2264 prints "*** ERROR lost ...",
    //     uxplay.cpp:538 prints "***ERROR lost ..." (no space). Neither uses the "*** ERROR: "
    //     prefix of log(), because both are emitted with LOGI. Check before the generic prefixes.
    if (t.find("ERROR lost connection with client") != std::string::npos &&
        startsWith(t, "***")) {
        out.tag = LineTag::LostConnection;
        out.events.push_back(makeEvent(HostEventKind::Error, raw));
        return out;
    }

    // --- mirror activity (patches/0004). The fast path: reported within ~400 ms of the
    //     frames stopping, against ~2 s for the client's own reports.
    if (startsWith(t, "mirror idle:")) {
        out.tag = LineTag::MirrorIdle;
        HostEvent ev = makeEvent(HostEventKind::MirrorActivity, t);
        ev.srcWidth = 0;
        out.events.push_back(ev);
        return out;
    }
    if (startsWith(t, "mirror active:")) {
        out.tag = LineTag::MirrorActive;
        HostEvent ev = makeEvent(HostEventKind::MirrorActivity, t);
        ev.srcWidth = 1;
        out.events.push_back(ev);
        return out;
    }

    // --- stream statistics (patches/0005), once a second while frames arrive. Measured in
    //     the receiver, so it needs no -FPSdata and describes what really came in. Like the
    //     -FPSdata envelope it never reaches the log: one line a second is noise there.
    if (startsWith(t, "mirror stats:")) {
        out.tag = LineTag::MirrorStats;
        unsigned kbps = 0, fps = 0;
        if (std::sscanf(t.c_str(), "mirror stats: %u kbps %u fps", &kbps, &fps) == 2) {
            HostEvent ev{};
            ev.kind = HostEventKind::MirrorStats;
            ev.message = t;
            ev.kbps = static_cast<int>(kbps);
            ev.fps  = static_cast<int>(fps);
            out.events.push_back(ev);
        }
        return out;
    }

    // --- -FPSdata report (lib/raop_rtp_mirror.c:807-811 dumps the client's plist as XML).
    //     The client keeps requesting feedback while its screen is off, so the only honest
    //     "no picture is arriving" signal is submitSurfaceFPS dropping to 0 in these reports.
    //     Split into key/value lines here; UxplayHost pairs them up.
    if (startsWith(t, "<key>") && endsWith(t, "</key>")) {
        out.tag = LineTag::PlistKey;
        out.detail = t.substr(5, t.size() - 11);
        return out;
    }
    if (startsWith(t, "<integer>") && endsWith(t, "</integer>")) {
        out.tag = LineTag::PlistInteger;
        out.detail = t.substr(9, t.size() - 19);
        return out;
    }
    if (startsWith(t, "<?xml") || startsWith(t, "<!DOCTYPE plist") || startsWith(t, "<plist") ||
        startsWith(t, "</plist") || t == "<dict>" || t == "</dict>" ||
        (startsWith(t, "<real>") && endsWith(t, "</real>"))) {
        out.tag = LineTag::PlistNoise;
        return out;
    }

    // --- client stopped asking for feedback (uxplay.cpp:549). Emitted through LOGE, so it
    //     wears the "*** ERROR: " prefix, but it is not a failure: an iPhone whose screen is
    //     off produces it once a second and recovers on its own. Must be checked before the
    //     generic error prefix or it would be reported as an error to the user.
    if (t.find("seconds since last client feedback request") != std::string::npos) {
        // Tagged but no event: it means the network is late, not that mirroring paused - an
        // iPhone with its screen off keeps asking for feedback. It must simply not be
        // reported to the user as an error.
        out.tag = LineTag::FeedbackLate;
        return out;
    }

    if (startsWith(t, "*** ERROR: ")) {                             // uxplay.cpp:239
        out.tag = LineTag::Error;
        out.events.push_back(makeEvent(HostEventKind::Error, t.substr(11)));
        return out;
    }
    // --- bare "Error: ..." / "invalid ... server name": argument parsing writes straight to
    //     stderr instead of going through log(), so these carry no "*** " prefix
    //     (uxplay.cpp:1207 non-UTF-8 argv, :1241 bad -n name). They are fatal - the process
    //     exit()s immediately after - and are the only explanation the user ever gets, so
    //     they must not be swallowed as an unknown line.
    if (startsWith(t, "Error: ") || startsWith(t, "invalid (non-UTF-8/ascii) server name")) {
        out.tag = LineTag::Error;
        out.events.push_back(makeEvent(HostEventKind::Error, t));
        return out;
    }

    if (startsWith(t, "*** WARNING: ")) {                           // uxplay.cpp:242
        out.tag = LineTag::Warning;
        out.events.push_back(makeEvent(HostEventKind::Warning, t.substr(13)));
        return out;
    }

    // --- blocked client, uxplay.cpp:2294 ---------------------------------------------------------
    if (t.find("attempt to connect by blocked client") != std::string::npos) {
        out.tag = LineTag::ClientBlocked;
        out.events.push_back(makeEvent(HostEventKind::Warning, t));
        return out;
    }

    // --- startup banner, uxplay.cpp:2997 ---------------------------------------------------------
    if (startsWith(t, "UxPlay ") && t.find("An Open-Source AirPlay") != std::string::npos) {
        out.tag = LineTag::Banner;
        return out;
    }

    // --- ports, uxplay.cpp:3205 ------------------------------------------------------------------
    if (startsWith(t, "using network ports ")) {
        int u0 = 0, u1 = 0, u2 = 0, c0 = 0, c1 = 0, c2 = 0;
        if (std::sscanf(t.c_str(), "using network ports UDP %d %d %d TCP %d %d %d",
                        &u0, &u1, &u2, &c0, &c1, &c2) == 6) {
            HostEvent ev = makeEvent(HostEventKind::Ports, t);
            ev.udpPorts[0] = u0; ev.udpPorts[1] = u1; ev.udpPorts[2] = u2;
            ev.tcpPorts[0] = c0; ev.tcpPorts[1] = c1; ev.tcpPorts[2] = c2;
            out.tag = LineTag::Ports;
            out.events.push_back(ev);
            return out;
        }
    }

    // --- MAC address, uxplay.cpp:3211 / :3213 / :3218 --------------------------------------------
    if (startsWith(t, "using system MAC address ") ||
        startsWith(t, "using user-set MAC address ") ||
        startsWith(t, "using randomly-generated MAC address ")) {
        out.tag = LineTag::Mac;
        out.detail = trim(t.substr(t.rfind("MAC address ") + 12));
        return out;
    }

    // --- public key storage, uxplay.cpp:3165 -----------------------------------------------------
    if (startsWith(t, "public key storage (for persistence) is in ")) {
        out.tag = LineTag::KeyStorage;
        out.detail = trim(t.substr(std::strlen("public key storage (for persistence) is in ")));
        return out;
    }

    // --- connection request, uxplay.cpp:2282 -----------------------------------------------------
    //     "connection request from %s (%s) with deviceID = %s"; the client name is user-chosen and
    //     may contain spaces and parentheses, so parse from the right.
    static const char kConnPrefix[] = "connection request from ";
    static const char kDevIdSep[]   = " with deviceID = ";
    if (startsWith(t, kConnPrefix)) {
        const size_t sep = t.rfind(kDevIdSep);
        if (sep != std::string::npos && sep > std::strlen(kConnPrefix)) {
            const std::string deviceId = trim(t.substr(sep + std::strlen(kDevIdSep)));
            std::string left = t.substr(std::strlen(kConnPrefix), sep - std::strlen(kConnPrefix));
            std::string name = trim(left), model;
            const size_t open = left.rfind('(');
            const size_t close = left.rfind(')');
            if (open != std::string::npos && close != std::string::npos && close > open) {
                model = left.substr(open + 1, close - open - 1);
                name  = trim(left.substr(0, open));
            }
            HostEvent ev = makeEvent(HostEventKind::ClientInfo, t);
            ev.clientName = name;
            ev.clientModel = model;
            ev.clientDeviceId = deviceId;
            out.tag = LineTag::ClientInfo;
            out.detail = name;
            out.events.push_back(ev);
            return out;
        }
    }

    // --- restrict-list refusal, uxplay.cpp:2286. The format string embeds a '\n', so uxplay prints
    //     TWO lines; both are tagged. -----------------------------------------------------------
    if (startsWith(t, "client connections have been restricted to those with listed deviceID") ||
        startsWith(t, "use \"-allow ")) {
        out.tag = LineTag::ClientRejected;
        out.events.push_back(makeEvent(HostEventKind::Warning, t));
        return out;
    }

    // --- negotiated audio format, uxplay.cpp:2449 ------------------------------------------------
    //     "ct=%d spf=%d usingScreen=%d isMedia=%d  audioFormat=0x%lx"; ct=8 => mirroring,
    //     ct=2 => audio-only (DESIGN §6.1). The mode is reported as a second LogLine because the
    //     contract has no dedicated HostEventKind for it (see "contract notes" in the final report).
    if (startsWith(t, "ct=")) {
        int ct = 0;
        if (std::sscanf(t.c_str(), "ct=%d", &ct) == 1) {
            out.tag = LineTag::AudioFormat;
            if (ct == 8)      out.detail = "mirroring";
            else if (ct == 2) out.detail = "audio-only";
            if (!out.detail.empty()) {
                out.events.push_back(makeEvent(HostEventKind::LogLine, out.detail));
            }
            return out;
        }
    }

    // --- registered new client, uxplay.cpp:2570 --------------------------------------------------
    if (startsWith(t, "registered new client: ")) {
        out.tag = LineTag::RegisteredClient;
        std::string rest = t.substr(std::strlen("registered new client: "));
        const size_t dev = rest.find(" DeviceID = ");
        out.detail = trim(dev == std::string::npos ? rest : rest.substr(0, dev));
        return out;
    }

    // --- missed-feedback detail line, uxplay.cpp:539 (printed indented, after the lost-connection
    //     line) ------------------------------------------------------------------------------------
    if (startsWith(t, "Interval since last client feedback request exceeds limit of ")) {
        out.tag = LineTag::FeedbackTimeout;
        out.events.push_back(makeEvent(HostEventKind::Warning, t));
        return out;
    }

    // --- shutdown, uxplay.cpp:3309 ---------------------------------------------------------------
    if (startsWith(t, "Stopping RAOP Server...")) {
        out.tag = LineTag::Stopping;
        return out;
    }

    // --- sinks disabled, uxplay.cpp:3080 / :3184 -------------------------------------------------
    if (t == "video_disabled") { out.tag = LineTag::VideoDisabled; return out; }
    if (t == "audio_disabled") { out.tag = LineTag::AudioDisabled; return out; }

    // --- fullscreen hint, uxplay.cpp:3097 / :3106 ------------------------------------------------
    if (startsWith(t, "Use Alt-Enter key combination to toggle")) {
        out.tag = LineTag::FullscreenHint;
        return out;
    }

    // --- mirrored resolution, lib/raop_rtp_mirror.c:608-609 (LOGGER_DEBUG => needs -d) -----------
    {
        const size_t at = t.find("raop_rtp_mirror width_source = ");
        if (at != std::string::npos) {
            float ws = 0, hs = 0, w = 0, h = 0;
            if (std::sscanf(t.c_str() + at,
                            "raop_rtp_mirror width_source = %f height_source = %f width = %f height = %f",
                            &ws, &hs, &w, &h) == 4) {
                HostEvent ev = makeEvent(HostEventKind::Resolution, t);
                ev.srcWidth = toInt(ws); ev.srcHeight = toInt(hs);
                ev.width = toInt(w);     ev.height = toInt(h);
                out.tag = LineTag::Resolution;
                out.events.push_back(ev);
                return out;
            }
        }
    }

    // --- pairing PIN block, uxplay.cpp:2196-2205 -------------------------------------------------
    //     The block is pure ASCII art: create_pin_display() never writes the digits as text, so a
    //     Pin event can only be produced for the one art row that is decodable (row index 4).
    //     UxplayHost emits a digit-less Pin at the end of an art run if no row decoded.
    //     NOTE: `raw`, not `t` — the geometry check depends on the untouched left margin.
    if (isPinArtLine(raw)) {
        out.tag = LineTag::PinArt;
        out.detail = decodePinArtRow(raw);
        if (!out.detail.empty()) {
            out.events.push_back(makeEvent(HostEventKind::Pin, out.detail));
        }
        return out;
    }

    out.tag = LineTag::Unknown;
    return out;
}

std::vector<HostEvent> parseUxplayLine(const std::string& line) {
    return parseUxplayLineDetailed(line).events;
}

} // namespace airplay
