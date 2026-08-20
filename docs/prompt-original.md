# Prompt: Windows AirPlay Receiver (fork UxPlay)

> Bu dosyayı başka bir IDE'ye (Cursor / Claude Code / Codex) yapıştır ya da
> `@airplay-receiver-prompt.md` olarak referans ver. Teknik prompt İngilizce —
> kod üreten asistanlar böyle daha iyi sonuç verir.

---

You are an expert C/C++ systems engineer. Fork and enhance **UxPlay** — the
open-source AirPlay receiver — into a polished personal-use Windows application
that mirrors an iPhone's screen to the PC. Do NOT reinvent the AirPlay protocol
from scratch; build on the existing, actively-maintained UxPlay codebase.

# GOAL
An iPhone 13 (iOS) mirrors its screen to a Windows 11 PC over Wi-Fi using the
iPhone's BUILT-IN AirPlay "Screen Mirroring" (Control Center → Screen Mirroring).
No iOS code, no Apple account, no App Store. The Windows app must:
  - Advertise itself as an AirPlay receiver on the LAN (mDNS).
  - Accept the AirPlay mirroring session from the iPhone.
  - Decode + render H.264 video and play audio in a window.

# CONTEXT / CURRENT STATE (verified against UxPlay v1.74, June 2026)
- UxPlay is actively maintained: https://github.com/FDH2/UxPlay
- It already runs on Windows, built with MinGW-64 under MSYS2.
- v1.74 added an INTERNAL mDNS responder — no dependency on Apple Bonjour/avahi.
  (Legacy external DNS-SD still available via `-DUSE_DNS_SD=1`; internal via
  `-DUSE_MDNS=1`.)
- Rendering uses GStreamer (configurable video/audio sinks + pipeline).
- License: GPLv3 (fine for personal use; a distributed fork must stay GPL).
- Plain screen mirroring needs no DRM/FairPlay handshake. DRM video playback is
  OUT OF SCOPE — explicitly ignore it.

# TECH STACK
- Language: C/C++ (UxPlay is C++), CMake build.
- Toolchain: MSYS2 (UCRT64 or MINGW64) + MinGW-64 + CMake + pkg-config.
- Media: GStreamer (Windows build via MSYS2 `pacman` or official installer).
  Enumerate required plugins (h264parse, aacparse, rtp, autovideosink/gl,
  autoaudiosink, etc.).
- Networking: UxPlay's internal mDNS responder (default). Socket/HTTP/RTSP
  already handled by UxPlay.

# PHASES

## Phase 0 — PROVE IT WORKS FIRST (before any custom code)
- Install MSYS2 + GStreamer; clone UxPlay; build it on Windows; run it.
- Mirror the iPhone to it. Confirm video + audio + discovery all work.
- Record the exact build commands, dependencies, and any gotchas into a doc.
- If discovery fails, try `-DUSE_DNS_SD=1` (Bonjour) vs `-DUSE_MDNS=1`
  (internal) and note which works on this machine.

## Phase 1 — Understand + fork
- Read the UxPlay source; write DESIGN.md documenting: the AirPlay handshake
  flow (pair-setup/pair-verify, GET /info, POST /stream, POST /audio), the mDNS
  TXT records, ports, and the "features" bitmask — each field cited to source
  file:line.
- Fork the repo; create a personal branch; verify the fork still builds.

## Phase 2 — Turn it into a proper Windows app
- Wrap UxPlay with a simple GUI (Qt, SDL2, or Win32 — choose and justify):
  status ("waiting"/"connected"), device name, resolution, FPS, bitrate.
- Config: device name, port, video sink, always-on-top, fullscreen toggle,
  screenshot.
- Clean start/stop and reconnect handling; single-instance; system tray icon.

## Phase 3 — Polish + packaging
- Correct aspect-ratio scaling on window resize.
- Optional: autostart on login, remember last settings, log file.
- Package as a self-contained Windows build/installer (document the MSYS2
  runtime DLLs needed).

# PITFALLS
- Windows mDNS was historically the flakiest part; v1.74's internal responder
  fixes most of it, but if the PC doesn't appear in the iPhone's Screen
  Mirroring list, toggle `-DUSE_MDNS` / `-DUSE_DNS_SD`.
- The "features" bitmask (in the TXT record and /info response) must match what
  iOS expects, or the device won't appear as a screen-mirroring target.
- GStreamer on Windows: missing plugins or wrong runtime PATH are the usual
  failures — enumerate plugins and document PATH setup.
- Latency is inherent to AirPlay over Wi-Fi; do not promise zero-lag.

# DELIVERABLE FOR THIS PASS
1. Phase 0 build notes (exact commands) proving iPhone → Windows mirroring works.
2. DESIGN.md (architecture + protocol flow with citations).
3. A building fork with a minimal GUI (Phase 2), or if time-constrained, a clear
   ROADMAP.md for Phases 2–3.
4. Be honest: mark what is verified-working vs. guessed. Do not fabricate
   protocol details — cite UxPlay source lines.

Personal-use tool. GPLv3. No Apple Developer account required.
