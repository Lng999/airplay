// line_parser.h — pure, Win32-free classifier for uxplay.exe stdout lines.
//
// The public entry point is `airplay::parseUxplayLine` (declared in the contract header
// app/include/airplay/uxplay_host.h). It returns only the event kinds the contract knows about
// (HostEventKind). Many rows of docs/DESIGN.md §6.1 have no dedicated HostEventKind — they are
// still recognised, and the recognition result is exposed here as `LineTag` so that UxplayHost can
// drive its state machine without re-matching strings. Tests assert on both.
//
// Every uxplay format string quoted below was verified against third_party/UxPlay/uxplay.cpp at the
// cited line; all logging goes through log() (uxplay.cpp:231-251), which prefixes ONLY the levels
// ERR ("*** ERROR: ", :239) and WARNING ("*** WARNING: ", :242) and appends one '\n'.
#pragma once

#include <string>
#include <vector>

#include "airplay/uxplay_host.h"

namespace airplay {

// One tag per row of docs/DESIGN.md §6.1 "Lines we can parse for state", plus the generic levels.
enum class LineTag {
    Unknown = 0,
    Banner,           // uxplay.cpp:2997  "UxPlay 1.74: An Open-Source AirPlay ..."
    Ports,            // uxplay.cpp:3205  "using network ports UDP %d %d %d TCP %d %d %d"
    Mac,              // uxplay.cpp:3211/:3213/:3218 "using ... MAC address %s"
    KeyStorage,       // uxplay.cpp:3165  "public key storage (for persistence) is in %s"
    ClientInfo,       // uxplay.cpp:2282  "connection request from %s (%s) with deviceID = %s"
    ClientRejected,   // uxplay.cpp:2286  restrict-list refusal (2 printed lines)
    ClientBlocked,    // uxplay.cpp:2294  "*** attempt to connect by blocked client (clientID %s): DENIED"
    AudioFormat,      // uxplay.cpp:2449  "ct=%d spf=%d usingScreen=%d isMedia=%d  audioFormat=0x%lx"
    PinArt,           // uxplay.cpp:2196-2205 -> create_pin_display() :395-465, ASCII-art row
    RegisteredClient, // uxplay.cpp:2570  "registered new client: %s DeviceID = %s PK = "
    LostConnection,   // uxplay.cpp:2264 ("*** ERROR ...") and :538 ("***ERROR ...", no space)
    FeedbackTimeout,  // uxplay.cpp:539   "   Interval since last client feedback request exceeds ..."
    Stopping,         // uxplay.cpp:3309  "Stopping RAOP Server..."
    VideoDisabled,    // uxplay.cpp:3080  "video_disabled"
    AudioDisabled,    // uxplay.cpp:3184  "audio_disabled"
    FullscreenHint,   // uxplay.cpp:3097/:3106 "Use Alt-Enter key combination to ..."
    Resolution,       // lib/raop_rtp_mirror.c:608-609 (DEBUG level, needs -d)
    Warning,          // "*** WARNING: ..."
    Error             // "*** ERROR: ..."
};

struct ParsedLine {
    LineTag                tag{LineTag::Unknown};
    std::vector<HostEvent> events;   // what parseUxplayLine() returns; events[0] is always LogLine
    // Extra text that has no home in HostEvent, filled in for a few tags:
    //   Mac              -> the MAC address
    //   KeyStorage       -> the .pem path
    //   AudioFormat      -> "mirroring" (ct=8) or "audio-only" (ct=2), "" otherwise
    //   RegisteredClient -> the client name
    //   PinArt           -> the decoded PIN digits, "" when this row carries none
    std::string            detail;
};

// Full classification. `line` must already have had its trailing CR/LF removed (parseUxplayLine
// strips them defensively anyway).
ParsedLine parseUxplayLineDetailed(const std::string& line);

// --- helpers, exposed for unit tests -------------------------------------------------------------

// True when the line is one row of the ASCII-art PIN block. The geometry (left margin 10, glyph
// width 10, gap 3) is fixed by the call site display_pin() (uxplay.cpp:2197-2198) and by
// create_pin_display() (uxplay.cpp:395-465).
bool isPinArtLine(const std::string& line);

// Attempts to read the PIN digits out of one art row. Only row index 4 of the 8-row glyph is
// unambiguous (every digit has a distinct 5th row), so exactly one row of a PIN block yields
// digits; all other rows return "".
std::string decodePinArtRow(const std::string& line);

} // namespace airplay
