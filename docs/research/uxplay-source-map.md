# UxPlay Source Map (research notes)

Target: <https://github.com/FDH2/UxPlay> (GPLv3), cloned for real into
`C:\Users\pc\Desktop\airplay\third_party\UxPlay`.

All `file:line` citations below refer to files inside that clone at the commit recorded in §1.
Anything I could not verify from the source is explicitly marked **UNVERIFIED**.

> Convention: paths are relative to `third_party/UxPlay/`.

---

## 1. Version / commit pin

| Item | Value | Source |
|---|---|---|
| Clone command | `git clone --depth=1 https://github.com/FDH2/UxPlay third_party/UxPlay` | — |
| HEAD commit | `a3c19cbc7fcc870d74a0960bc97817a2569b4808` | `git rev-parse HEAD` |
| HEAD date | `Sun Aug 9 11:55:45 2026 -0400` | `git log -1` |
| HEAD subject | `Merge pull request #544 from JerryNee/agent/p2p-awdl-discovery` | `git log -1` |
| Latest remote tag | `v1.73.6` = `21eef8df25d91e12635c36d8176ad192725baca2` | `git ls-remote --tags origin` |
| Version in source | **1.74** | `uxplay.cpp:75` — `#define VERSION "1.74"` |
| Version in RPM spec | `1.74` | `uxplay.spec:3` |
| Version in CMakeLists | *none* — `CMakeLists.txt:3` is just `project( uxplay )`; no `VERSION` argument | `CMakeLists.txt:1-3` |

So: **HEAD is unreleased 1.74 development, ~5 months past the newest tag `v1.73.6`.**
`git describe --tags` cannot work on a `--depth=1` clone (no tag objects fetched); tag list was
obtained via `git ls-remote --tags origin`.

README self-describes 1.74 as **Experimental** (`README.md:5`).

### Consequence for us
Pinning to `master` HEAD means picking up the *experimental* internal-mDNS work.
Pinning to `v1.73.6` means no internal mDNS → Bonjour SDK becomes mandatory on Windows.
**Recommendation: pin to this exact commit hash**, not to a branch.

---

## 2. Windows build (MSYS2)

### 2.1 Environment
- README section: `README.md:985` — `## Building UxPlay on Microsoft Windows, using MSYS2 with the MinGW-64 compiler.` (runs to `README.md:1177`).
- Tested on Windows 10/11 x86_64 and Windows 11 ARM (`README.md:987`).
- **UCRT64** is the recommended MSYS2 subsystem on x86_64 (`README.md:1019-1025`);
  MINGW64 is the legacy MSVCRT fallback; **CLANGARM64** for ARM (`README.md:1027-1029`).
- MSYS2 install path default `C:\msys64` (README says `C:\mysys64` at `README.md:1011` — that is a typo in the README; the rest of the doc uses `C:\msys64`, e.g. `README.md:1067`).

### 2.2 Exact pacman package list

Build-time (from `README.md:1031-1046`):

```
pacman -Syu                                    # from the plain MSYS2 shell, first run
pacman -S mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-gcc
pacman -S git
pacman -S mingw-w64-ucrt-x86_64-libplist mingw-w64-ucrt-x86_64-gstreamer mingw-w64-ucrt-x86_64-gst-plugins-base
```

Runtime GStreamer plugins (`README.md:1080-1089`) — a **separate** step:

```
pacman -S mingw-w64-ucrt-x86_64-gst-libav
pacman -S mingw-w64-ucrt-x86_64-gst-plugins-good
pacman -S mingw-w64-ucrt-x86_64-gst-plugins-bad
```

Notes:
- OpenSSL is assumed already installed with MSYS2 (`README.md:1042`) — no explicit package.
- `ninja` is never explicitly installed; it arrives as a dependency of the cmake package (`README.md:1035-1037`).
- `pacman -S man` optional, for the manpage (`README.md:1077-1078`).
- MINGW64 variant: drop `-ucrt` from every prefix. ARM: use `mingw-w64-clang-aarch64-*`.

**Cross-check with CI** (`.github/workflows/build.yml:118-127`, the authoritative machine-verified list):

```
${pkg_prefix}-cmake ${pkg_prefix}-gcc ${pkg_prefix}-openssl ${pkg_prefix}-libplist
${pkg_prefix}-gstreamer ${pkg_prefix}-gst-plugins-base ${pkg_prefix}-gst-plugins-good
${pkg_prefix}-pkg-config base-devel
```
with `pkg_prefix ∈ {mingw-w64-ucrt-x86_64 (UCRT64), mingw-w64-x86_64 (MINGW64), mingw-w64-clang-aarch64 (CLANGARM64)}`
(`.github/workflows/build.yml:100-105, 152-155`).

CI adds `openssl`, `pkg-config`, `base-devel` that the README omits. **Use the union of both lists.**

### 2.3 Exact build commands

README path (`README.md:1053-1061`):
```
mkdir build
cd build
cmake ..
ninja
```
Install (`README.md:1067-1075`):
```
cmake --install . --prefix $HOME/../../ucrt64      # → C:/msys64/ucrt64
ninja uninstall                                    # from the build dir
```

CI path (`.github/workflows/build.yml:129-137`), with `PKG_CONFIG_PATH=/ucrt64/lib/pkgconfig`:
```
cmake -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNO_X11_DEPS=ON
cmake --build build --parallel
```

### 2.4 Deployment / DLLs
There is **no bundling story** in the README. It only says: run `C:\msys64\ucrt64\bin\uxplay.exe`
by full path, or add `C:\msys64\ucrt64\bin` to PATH so Windows resolves the MinGW DLLs
(`README.md:1156-1161`). For a redistributable we must compute the DLL closure ourselves
(`ntldd -R` in MSYS2) **plus** ship the GStreamer plugin `.dll`s and set `GST_PLUGIN_PATH` /
`GST_PLUGIN_SYSTEM_PATH`. **UNVERIFIED** — no upstream guidance exists; this is our problem to solve.

### 2.5 Firewall / network
- UDP **5353** must be open for mDNS (`README.md:586`).
- Inbound rule example (`README.md:1100-1110`):
  `New-NetFirewallRule -DisplayName "UxPlay" -Direction Inbound -Program "C:\path\to\uxplay.exe" -Action Allow -Profile Private -Protocol Any`
- Network profile must be **Private**, not Public (`README.md:1112-1120`).

### 2.6 Windows sinks
- Video: `d3d12videosink`, `d3d11videosink`, `d3dvideosink`, `glimagesink`, `gtksink`, `autovideosink`
  (`README.md:1138-1146`). README **recommends `-vs d3d12videosink`** so the Alt-Enter fullscreen
  toggle is added (`README.md:1148-1153`).
- Audio: `directsoundsink` (legacy) or `wasapisink` (`README.md:1123-1136`); device by GUID via
  `-as 'wasapisink device="<guid>"'`, GUIDs from `gst-device-monitor-1.0 Audio`.

### 2.7 1.74 changelog / internal mDNS
- `README.md:2190-2191`: `1.74  2026-06-21  Optional minimal internal mDNSResponder to replace Bonjour/Avahi. Reworked language selection for HLS video.`
- `README.md:5-17` (the NEWS block) states the default switch: **Linux/\*BSD/Windows default to the internal
  mDNS in 1.74; macOS keeps Bonjour.** `-DUSE_DNS_SD=1` forces the old Apple/Avahi path; `-DUSE_MDNS=1`
  forces internal (needed only on macOS).
- `README.md:989-990`: "Uxplay >=1.74 now supplies its own self-contained mdns replacement for Bonjour,
  making step 1 [Bonjour SDK] unnecessary unless you choose to use Bonjour."
- `README.md:1002-1003`: a third discovery route exists — a **Bluetooth LE beacon** (Windows supported,
  `CMakeLists.txt:106-110` installs `uxplay_beacon_module_winrt.py`).
- The README never names `lib/mdnsd` — that path was found by reading the tree.

**This is the single most important fact for our project: with 1.74 we can ship without the Bonjour SDK / Apple Bonjour service.**

---

## 3. Build system: every option flag

### 3.1 Root `CMakeLists.txt`
No `option()` calls at all — everything is a bare `if (VAR)` on cache/`-D` variables.

| Flag | Line | Effect |
|---|---|---|
| `ZOOMFIX` | `CMakeLists.txt:11-13` | **Dead.** Prints "no longer used"; ZOOMFIX is auto-applied if X11 present. |
| `USE_X11` | `CMakeLists.txt:15` | Forces the X11 search branch on non-Unix. |
| `NO_X11_DEPS` | `CMakeLists.txt:16-27` | Skip `find_package(X11)` entirely. CI uses `-DNO_X11_DEPS=ON` on Windows and macOS (`build.yml:84,134,182`). |
| `USE_DNS_SD` | `CMakeLists.txt:51` | Use Apple `dns_sd.h` backend → `add_subdirectory(lib/dns_sd)`. |
| `USE_MDNS` | `CMakeLists.txt:51` | Only meaningful on Apple: `APPLE AND NOT USE_MDNS` selects Bonjour, so `-DUSE_MDNS=1` forces internal on macOS. |
| `GST_MACOS` | `CMakeLists.txt:76-79` | `add_definitions(-DGST_MACOS)`; wraps `main()` in `gst_macos_main` (`uxplay.cpp:2871-2885`). Auto-set in `renderers/CMakeLists.txt:62-74`. |
| `NO_MARCH_NATIVE` | `lib/CMakeLists.txt:6-14` | Off ⇒ `-O3 -march=native` on x86; on ⇒ `-O2`. **We should set `-DNO_MARCH_NATIVE=ON` for a redistributable binary.** |

Auto-defined compile definitions (not user flags):
- `-DDBUS` (`CMakeLists.txt:34`) — Linux only.
- `-DFULL_RANGE_RGB_FIX` (`CMakeLists.txt:46`) — Unix-not-Apple only; drives `DEFAULT_SRGB_FIX` at `uxplay.cpp:88-92`.
- `-DUXPLAY_HAVE_APPLE_P2P` (`CMakeLists.txt:54`), `-DSUPPRESS_AVAHI_COMPAT_WARNING` (`:58`).
- `-DNOHOLD` always (`lib/CMakeLists.txt:29`).
- `-DPLIST_210` / `-DPLIST_230` by libplist version (`lib/CMakeLists.txt:110,114`).
- `-DX_DISPLAY_FIX`, `-DZOOM_WINDOW_NAME_FIX` (`renderers/CMakeLists.txt:24,31`).
- `-DNEED_G_STRING_REPLACE` when GLib < 2.68 (`renderers/CMakeLists.txt:39`).

### 3.2 Which mDNS backend gets compiled
`CMakeLists.txt:51-69`:
```
if (USE_DNS_SD OR ( APPLE AND NOT USE_MDNS ))   →  add_subdirectory( lib/dns_sd )
else()                                          →  add_subdirectory( lib/mdnsd )
```
Both produce a static lib literally named `dnssd` (`lib/dns_sd/CMakeLists.txt:4`, `lib/mdnsd/CMakeLists.txt:4`),
so `lib/dnssd.h` is a stable façade and only the `dnssd_private_*` implementation swaps.

### 3.3 Link graph
```
uxplay(exe) ── renderers ── airplay ─┬─ playfair
                                     ├─ dnssd   (lib/dns_sd  OR  lib/mdnsd)
                                     ├─ llhttp
                                     ├─ pthread
                                     ├─ wsock32, iphlpapi, ws2_32   (WIN32 only)
                                     ├─ libplist-2.0
                                     └─ OpenSSL::Crypto
```
`CMakeLists.txt:81-85`, `lib/CMakeLists.txt:59-105,137-145`, `renderers/CMakeLists.txt:48-54,89`.

All four libs are **STATIC**. Only the `uxplay` executable is installed (`CMakeLists.txt:87`);
no headers, no pkg-config file, no shared library is exported.

### 3.4 External dependencies
`libplist-2.0` (pkg-config, `lib/CMakeLists.txt:95`), OpenSSL ≥ 1.1.1 (`lib/CMakeLists.txt:138`),
GLib ≥ 2.0, GStreamer ≥ 1.4 with `gstreamer-sdp-1.0`, `gstreamer-video-1.0`, `gstreamer-app-1.0`
(`renderers/CMakeLists.txt:36,42-46`). On Windows, `dns_sd` backend additionally wants
`C:\Program Files\Bonjour SDK\Lib\x64\dnssd.lib` (`lib/dns_sd/CMakeLists.txt:18-28`) — avoidable with the internal mdnsd.

---

## 4. Source tree map

### `lib/` — the `airplay` static library (protocol core, plain C)

| File | Purpose | Key entry points |
|---|---|---|
| `raop.c` / `raop.h` | Top-level server object; owns httpd + pairing + dnssd handle; dispatches RTSP/HTTP requests to handlers. | `raop_init` (`raop.c:586`), `raop_init2` (`:653`), `raop_start_httpd` (`:834`), `raop_stop_httpd` (`:841`), `raop_set_dnssd` (`:816`), `raop_set_plist` (`:736`), `raop_set_tcp_ports`/`raop_set_udp_ports` (`:790`,`:782`), `raop_destroy` (`:701`) |
| `raop_handlers.h` | **All RTSP/1.0 handlers**, defined as `static` functions in a header (included by `raop.c`). | see §6 |
| `http_handlers.h` | **All HTTP/1.1 handlers** (AirPlay video / HLS / reverse channel). | see §6 |
| `httpd.c` / `httpd.h` | Threaded TCP listener + connection table; protocol-agnostic. | `httpd_init` (`httpd.h:50`), `httpd_start` (`:54`), `httpd_stop` (`:55`), callbacks `conn_init`/`conn_request`/`conn_destroy` (`httpd.h:32-38`) |
| `http_request.c/.h`, `http_response.c/.h` | Request parsing (wraps llhttp) and response building. | `http_request_get_method/_url/_header/_data` |
| `pairing.c` / `pairing.h` | Ed25519/X25519 pairing, SRP pin pairing, persistent key file. | `pairing_init_generate` (`pairing.h:39`), `pairing_session_init` (`:42`), `pairing_session_handshake` (`:45`), `srp_new_user` (`:57`), `pairing_digest_verify` (`:66`) |
| `srp.c` / `srp.h` | SRP-6a for the `-pin` / `-pw` flows. | |
| `crypto.c` / `crypto.h` | AES/SHA/ED25519/X25519 wrappers over OpenSSL. | |
| `fairplay_playfair.c` / `fairplay.h` | FairPlay SAP handshake used by `/fp-setup`. | `fairplay_init` (`fairplay.h:22`), `fairplay_setup` (`:23`), `fairplay_handshake` (`:24`), `fairplay_decrypt` (`:25`) |
| `raop_rtp.c/.h` | Audio RTP receiver (ALAC/AAC/PCM), control + timing sockets. | `raop_rtp_init` (`raop_rtp.h:32`), `raop_rtp_start_audio` (`:35`), `raop_rtp_flush` (`:43`), `raop_rtp_stop` (`:44`) |
| `raop_rtp_mirror.c/.h` | Mirror-mode TCP video stream (h264/h265), AES-CTR per-frame decryption. | `raop_rtp_mirror_init` (`raop_rtp_mirror.h:28`), `raop_rtp_mirror_init_aes` (`:30`), `raop_rtp_mirror_start` (`:31`) |
| `raop_ntp.c/.h` | NTP-ish clock sync with the client. | `raop_ntp_init`, `raop_ntp_start` (`raop_ntp.h:47`), `ntp_global_init` (`raop.h:123`, required on Windows for QPC init) |
| `raop_buffer.c/.h` | Audio jitter/reorder buffer. | |
| `mirror_buffer.c/.h` | Mirror-video key/IV derivation + buffering. | |
| `airplay_video.c/.h` | AirPlay-video / HLS session state, playlists, FCUP requests. | `airplay_video_init` (`raop.h:120`) |
| `fcup_request.h` | FCUP (reverse-channel) request template. | |
| `dnssd.c` / `dnssd.h` / `dnssdint.h` | Backend-independent DNS-SD façade + all TXT constants. | see §5 |
| `netutils.c/.h` | Socket helpers, interface enumeration, `netutils_init`, `netutils_set_peer_to_peer`. | |
| `utils.c/.h` | hex/base64, `utils_hwaddr_raop`, `utils_hwaddr_airplay`. | |
| `byteutils.c/.h` | LE/BE integer read/write. | |
| `logger.c/.h` | Level-based logger with a pluggable callback. | `logger_init`, `logger_log` |
| `compat.h`, `sockets.h`, `threads.h` | Win32/POSIX shims (`compat.h:18-42` pulls winsock2/ws2tcpip/mswsock on WIN32). | |
| `global.h` | `GLOBAL_MODEL "AppleTV3,2"` (`global.h:21`), `GLOBAL_VERSION "220.68"` (`:22`), `MAX_HWADDR_LEN 6` (`:30`). | |
| `stream.h` | `audio_decode_struct` / `video_decode_struct` passed to the render callbacks. | |

### `lib/dns_sd/` — Apple Bonjour / Avahi backend
`dns_sd.c` only. On Windows it **dynamically loads `dnssd.dll` via `LoadLibraryA` + `GetProcAddress`**
(`lib/dns_sd/dns_sd.c:159-181`), so the Bonjour *service* must be installed at runtime.

### `lib/mdnsd/` — internal minimal mDNSResponder (new in 1.74)
- `mdnsd.c` (1107 lines) + `mdnsd.h` (52 lines) — self-contained responder, own thread.
  `MDNS_ADDR4 "224.0.0.251"` (`lib/mdnsd/mdnsd.c:28`), `MDNS_PORT 5353` (`:30`),
  socket setup with `SO_REUSEADDR`/`SO_REUSEPORT`/`IP_MULTICAST_TTL`/`IP_MULTICAST_LOOP`
  (`:447-457`), responder thread `mdns_thread` (`:870`) started at `:1013`.
- `dnssd_mdnsd.c` — implements the same `dnssd.h` façade on top of `mdnsd.h`.
- Public API: `mdnsd_init/destroy/start/stop/set_services/announce/goodbye`, `mdnsd_txt_add`
  (`lib/mdnsd/mdnsd.h:39-50`). `MDNSD_TTL_SERVICE 4500` (`:22`).
- Authored by `kgbook, 2026` (`lib/mdnsd/mdnsd.h:5`, `lib/mdnsd/dnssd_mdnsd.c:16`).

### `lib/llhttp/` — vendored llhttp HTTP parser (MIT), `api.c`, `http.c`, `llhttp.c`, `llhttp.h`.

### `lib/playfair/` — vendored FairPlay implementation (`playfair.c`, `omg_hax.c`, `hand_garble.c`,
`modified_md5.c`, `sap_hash.c`). Single public function `playfair_decrypt` (`lib/playfair/playfair.h:4`).
**Licence caveat**: README calls its legal status "unclear" (`README.md:2486-2489`) — relevant to us if we redistribute.

### `renderers/` — the `renderers` static library (GStreamer)
> Note: there is **no** `renderers/video_renderer_gstreamer.c` in 1.74. It is `renderers/video_renderer.c`.

| File | Purpose | Entry points |
|---|---|---|
| `video_renderer.c` (1191 lines) | Builds and drives up to 3 GStreamer video pipelines (h264 / h265 / jpeg-coverart) or one playbin for HLS. | `video_renderer_init` (`video_renderer.c:252`), `video_renderer_start/stop/destroy`, `video_renderer_render_buffer`, `video_renderer_listen` (`video_renderer.h:50-77`) |
| `audio_renderer.c` | Two audio pipelines (ALAC / AAC branches). | `gstreamer_init` (`audio_renderer.h:35`), `audio_renderer_init` (`:36`), `audio_renderer_start` (`:37`), `audio_renderer_render_buffer` (`:39`), `audio_renderer_set_volume` (`:40`) |
| `mux_renderer.c` | `-mp4` recording to file. | `mux_renderer_init` (`mux_renderer.h:32`) … |
| `x_display_fix.h` | X11 window-name/zoom workarounds (Linux only). | |

**Critical for embedding:** the renderers expose **no handle type**. `video_renderer_start()`,
`audio_renderer_set_volume()` etc. take no context pointer — all state lives in file-scope statics
(e.g. `static bool auto_videosink` at `video_renderer.c:42`, `renderer_type[]` array). They are
**process-wide singletons**; one AirPlay receiver per process.

---

## 5. mDNS registration, TXT records, features bitmask

### 5.1 Where the constants live — `lib/dnssdint.h`

```c
lib/dnssdint.h:27  #define RAOP_TXTVERS "1"
lib/dnssdint.h:28  #define RAOP_CH  "2"            /* audio channels */
lib/dnssdint.h:29  #define RAOP_CN  "0,1,2,3"      /* PCM, ALAC, AAC, AAC ELD */
lib/dnssdint.h:30  #define RAOP_ET  "0,3,5"        /* None, FairPlay, FairPlay SAPv2.5 */
lib/dnssdint.h:31  #define RAOP_VV  "2"
lib/dnssdint.h:32  #define FEATURES_1 "0x5A7FFEE6" /* first 32 bits, bit 27 ("legacy pairing") ON */
lib/dnssdint.h:33  //#define FEATURES_1 "0x527FFEE6" /* ... bit 27 OFF */
lib/dnssdint.h:34  #define FEATURES_2 "0x0"        /* second 32 bits */
lib/dnssdint.h:35  #define RAOP_RHD "5.6.0.0"
lib/dnssdint.h:36  #define RAOP_SF  "0x4"
lib/dnssdint.h:37  #define RAOP_SV  "false"
lib/dnssdint.h:38  #define RAOP_DA  "true"
lib/dnssdint.h:39  #define RAOP_SR  "44100"
lib/dnssdint.h:40  #define RAOP_SS  "16"
lib/dnssdint.h:41  #define RAOP_VS  GLOBAL_VERSION   /* "220.68" */
lib/dnssdint.h:42  #define RAOP_TP  "UDP"
lib/dnssdint.h:43  #define RAOP_MD  "0,1,2"        /* text, artwork, progress */
lib/dnssdint.h:44  #define RAOP_VN  "65537"
lib/dnssdint.h:46  #define AIRPLAY_SRCVERS GLOBAL_VERSION
lib/dnssdint.h:47  #define AIRPLAY_FLAGS "0x84"    /* NOTE: not actually used, see 5.4 */
lib/dnssdint.h:48  #define AIRPLAY_VV "2"
lib/dnssdint.h:49  #define AIRPLAY_PI "2e388006-13ba-4041-9a67-25dd4a43d536"
```
`lib/dnssdint.h:25` also carries a commented-out fixed `PK` (`b07727d6…54e7`) that restores the old
"everyone advertises the same public key" behaviour; consumed at `lib/raop.c:666-674`.

`GLOBAL_MODEL "AppleTV3,2"` and `GLOBAL_VERSION "220.68"` are at `lib/global.h:21-22`.

### 5.2 Where the features value is parsed and mutated
- `dnssd_init` parses the two hex strings into `dnssd->features1` / `features2`
  (`lib/dnssd.c:64-79`). Struct fields at `lib/dnssd.h:45-46`.
- `dnssd_set_airplay_features(dnssd, bit, val)` (`lib/dnssd.c:155-172`) flips individual bits;
  bits ≥ 32 go into `features2`.
- `dnssd_get_airplay_features` recombines to `uint64_t` (`lib/dnssd.c:145-149`).
- **`uxplay.cpp` overrides the whole 32-bit map bit by bit at startup**, `uxplay.cpp:2001-2039`,
  with an explanatory comment at `uxplay.cpp:1997-1999` ("overwrites features set in dnssdint.h;
  default: FEATURES_1 = 0x5A7FFEE6, FEATURES_2 = 0"). Bits 32-63 are listed but commented out
  (`uxplay.cpp:2042-2080`).
- Runtime-conditional bits (the ones our GUI would need to control):
  - `uxplay.cpp:2083` bit 0 = `hls_support`
  - `uxplay.cpp:2084` bit 4 = `hls_support` (HLS)
  - `uxplay.cpp:2089` bit 42 = `h265_support` (ScreenMultiCodec / 4K)
  - `uxplay.cpp:2092` bit 27 = `setup_legacy_pairing` (drives the 5-second pairing delay; see `README.md:2157-2170`)
- Logged at connect: `uxplay.cpp:1956-1957` — `"register_dnssd: advertised AirPlay service with \"Features\" code = 0x%llX"`.

**Effective advertised features are therefore NOT `0x5A7FFEE6`** — that is only the seed. The value is
recomputed from `uxplay.cpp:2001-2092`. Any reimplementation must copy that block, not `dnssdint.h`.

### 5.3 `_raop._tcp` TXT record
Built twice, once per backend, with identical key order:

| Key | Value | mdnsd backend | dns_sd backend |
|---|---|---|---|
| `ch` | `"2"` | `lib/mdnsd/dnssd_mdnsd.c:123` | `lib/dns_sd/dns_sd.c:249` |
| `cn` | `"0,1,2,3"` | `:124` | `:250` |
| `da` | `"true"` | `:125` | `:251` |
| `et` | `"0,3,5"` | `:126` | `:252` |
| `vv` | `"2"` | `:127` | `:253` |
| `ft` | `"0x%X,0x%X"` of features1,features2 | `:120,128` | `:246,254` |
| `am` | `"AppleTV3,2"` | `:129` | `:255` |
| `md` | `"0,1,2"` | `:130` | `:256` |
| `rhd` | `"5.6.0.0"` | `:131` | `:257` |
| `pw` | `false` / `true` | `:132` | `:261,267,271` |
| `sf` | `0x4` / `0x8c` (pin) / `0x84` (password) | `:104-118,138` | `:262,268,272` |
| `sr` | `"44100"` | `:133` | `:275` |
| `ss` | `"16"` | `:134` | `:276` |
| `sv` | `"false"` | `:135` | `:277` |
| `tp` | `"UDP"` | `:136` | `:278` |
| `txtvers` | `"1"` | `:137` | `:279` |
| `vs` | `"220.68"` | `:139` | `:280` |
| `vn` | `"65537"` | `:140` | `:281` |
| `pk` | per-install Ed25519 public key (hex) | `:141` | `:282` |

Service instance name is `"<HWADDR>@<name>"`, i.e. MAC-hex `@` friendly name
(`lib/mdnsd/dnssd_mdnsd.c:82-85`; `lib/dns_sd/dns_sd.c:285-297`), service type `_raop._tcp`
(`lib/dns_sd/dns_sd.c:301`, `lib/mdnsd/dnssd_mdnsd.c:184`).

### 5.4 `_airplay._tcp` TXT record

| Key | Value | mdnsd backend | dns_sd backend |
|---|---|---|---|
| `deviceid` | MAC as `AA:BB:…` | `lib/mdnsd/dnssd_mdnsd.c:160` | `lib/dns_sd/dns_sd.c:334` |
| `features` | `"0x%X,0x%X"` | `:161` (built at `:157`) | `:335` (built at `:324`) |
| `pw` | `true` if pin/password else `false` | `:162` | `:338,343,347` |
| `flags` | **hardcoded `"0x4"`** in both backends — `AIRPLAY_FLAGS "0x84"` in `dnssdint.h:47` is dead | `:163` | `:339,344,348` |
| `model` | `"AppleTV3,2"` | `:164` | `:351` |
| `pk` | Ed25519 public key hex | `:165` | `:352` |
| `pi` | `"2e388006-13ba-4041-9a67-25dd4a43d536"` | `:166` | `:353` |
| `srcvers` | `"220.68"` | `:167` | `:354` |
| `vv` | `"2"` | `:168` | `:355` |

Service name is the plain friendly name (`lib/dns_sd/dns_sd.c:359`;
`lib/mdnsd/dnssd_mdnsd.c:87-89` builds `"<name>._airplay._tcp.local"`).

### 5.5 Registration call flow
```
uxplay.cpp:1990   dnssd_init(name, len, hw_addr, hw_addr_len, pin_pw, &err)   → lib/dnssd.c:33
uxplay.cpp:1995   dnssd_set_peer_to_peer(...)                                  → lib/dnssd.c:174
uxplay.cpp:2001-2092  dnssd_set_airplay_features(...) × 32                     → lib/dnssd.c:155
uxplay.cpp:2771   raop_set_dnssd(raop, dnssd)                                  → lib/raop.c:816
uxplay.cpp:1934   dnssd_register_raop(dnssd, raop_port)                        → mdnsd:239 / dns_sd:233
uxplay.cpp:1945   dnssd_register_airplay(dnssd, airplay_port)                  → mdnsd:265 / dns_sd:312
uxplay.cpp:1963-4 dnssd_unregister_raop / _airplay                             → mdnsd:312/332
uxplay.cpp:1972   dnssd_destroy                                                → lib/dnssd.c:114
```
Both services are advertised on **the same TCP port** — `airplay_port = raop_port` (`uxplay.cpp:2768`).

Internal-backend specifics: `dnssd_register_*` builds the TXT, sets `registered`, calls
`mdnsd_set_services` then `mdnsd_start` + `mdnsd_announce(…, MDNSD_TTL_SERVICE)`
(`lib/mdnsd/dnssd_mdnsd.c:248-261`, `273-286`). Unregister sends a goodbye packet and stops the
responder when no service remains (`:311-349`). Host name is `gethostname()` sanitized to
`<host>.local` (`:62-72`).

Error text for the internal backend explicitly mentions UDP 5353 / multicast
(`lib/mdnsd/dnssd_mdnsd.c:351-358`).

---

## 6. HTTP / RTSP request handling

### 6.1 Dispatcher
`conn_request()` in `lib/raop.c:192` is the single entry. It reads method/url/protocol
(`lib/raop.c:217-221`) and branches on protocol:

**RTSP/1.0** (`lib/raop.c:406-444`):

| Method + URL | Handler | Definition |
|---|---|---|
| `POST /feedback` | `raop_handler_feedback` | `lib/raop.c:409` → `lib/raop_handlers.h:1195` |
| `POST /pair-pin-start` | `raop_handler_pairpinstart` | `:411` → `raop_handlers.h:252` |
| `POST /pair-setup-pin` | `raop_handler_pairsetup_pin` | `:413` → `raop_handlers.h:277` |
| `POST /pair-setup` | `raop_handler_pairsetup` | `:415` → `raop_handlers.h:436` |
| `POST /pair-verify` | `raop_handler_pairverify` | `:417` → `raop_handlers.h:464` |
| `POST /fp-setup` | `raop_handler_fpsetup` | `:419` → `raop_handlers.h:545` |
| `POST /audioMode` | `raop_handler_audiomode` | `:421` → `raop_handlers.h:1174` |
| `GET /info` | `raop_handler_info` | `:425` → `raop_handlers.h:37` |
| `OPTIONS` | `raop_handler_options` | `:428` → `raop_handlers.h:585` |
| `SETUP` | `raop_handler_setup` | `:431` → `raop_handlers.h:593` |
| `GET_PARAMETER` | `raop_handler_get_parameter` | `:433` → `raop_handlers.h:1062` |
| `SET_PARAMETER` | `raop_handler_set_parameter` | `:435` → `raop_handlers.h:1118` |
| `RECORD` | `raop_handler_record` | `:437` → `raop_handlers.h:1206` |
| `FLUSH` | `raop_handler_flush` | `:439` → `raop_handlers.h:1220` |
| `TEARDOWN` | `raop_handler_teardown` | `:441` → `raop_handlers.h:1243` |
| anything else | `501 Not Implemented` | `:443` |

`OPTIONS` advertises `Public: SETUP, RECORD, FLUSH, TEARDOWN, OPTIONS, GET_PARAMETER, SET_PARAMETER`
(`lib/raop_handlers.h:589`).

**HTTP/1.1** (`lib/raop.c:445-475`):

| Method + URL | Handler | Definition |
|---|---|---|
| `POST /reverse` | `http_handler_reverse` | `:448` → `lib/http_handlers.h:359` |
| `POST /play` | `http_handler_play` | `:450` → `http_handlers.h:658` |
| `POST /getProperty?…` | `http_handler_get_property` | `:452` → `http_handlers.h:212` |
| `POST /scrub?…` | `http_handler_scrub` | `:454` → `http_handlers.h:106` |
| `POST /rate?…` | `http_handler_rate` | `:456` → `http_handlers.h:128` |
| `POST /stop` | `http_handler_stop` | `:458` → `http_handlers.h:149` |
| `POST /action` | `http_handler_action` | `:460` → `http_handlers.h:395` |
| `POST /fp-setup2` | `http_handler_fpsetup2` | `:462` → `http_handlers.h:222` |
| `GET /server-info` | `http_handler_server_info` | `:467` → `http_handlers.h:43` |
| `GET /playback-info` | `http_handler_playback_info` | `:469` → `http_handlers.h:307` |
| `PUT /setProperty?…` | `http_handler_set_property` | `:473` → `http_handlers.h:161` |
| any HLS request | `http_handler_hls` | `:477` → `http_handlers.h:885` |

Every non-HLS response gets `Server: AirTunes/220.68` and the echoed `CSeq` (`lib/raop.c:489-492`).

### 6.2 `GET /info` specifics (`lib/raop_handlers.h:37-…`)
Three request shapes are handled (`:44-47`): with CSeq+plist, with CSeq only, and without CSeq
(the Bluetooth-LE discovery form). The `qualifier` array in the binary plist selects
`txtAirPlay` (`:79-80`) or `txtRAOP` (`:81-82`), and the response embeds the *same bytes* as the
mDNS TXT record via `dnssd_get_airplay_txt` / `dnssd_get_raop_txt` (`:97`, `:103`).
Full-info responses add `deviceID`, `macAddress` (`:119-123`) and `pk` (`:127-130`).

This is why the BLE-beacon discovery path works without mDNS at all: `/info` re-serves the TXT blob.

### 6.3 Streaming setup
`SETUP` (`lib/raop_handlers.h:593`) is where streams are created: "The first SETUP call that initializes
keys and timing" (`:627`), branches per stream `type` and errors with
`"SETUP tries to setup stream of unknown type %llu"` (`:1046`). Mirroring init failure is logged at `:962`,
audio at `:1029`.

### 6.4 Transport layer
`httpd.c` (737 lines) owns the listening sockets and connection table; the connection type enum is
`CONNECTION_TYPE_{UNKNOWN,RAOP,AIRPLAY,PTTH,HLS}` (`lib/httpd.h:24-30`). `raop.c` installs its three
callbacks in `raop_init2` (`lib/raop.c:680-688`).

---

## 7. `uxplay.cpp` — how everything is wired

### 7.1 Shape
- Single translation unit, 132 KB, ~3400 lines. `main()` at `uxplay.cpp:2884`
  (or a `gst_macos_main` trampoline at `:2877` when `GST_MACOS` is defined, `:2871-2885`).
- ~200 file-scope `static` globals hold the entire configuration (`uxplay.cpp:94-200+`).
- Defaults: `DEFAULT_NAME "UxPlay"` (`:79`), `DEFAULT_PLAYBIN_VERSION 3` (`:85`),
  `MISSED_FEEDBACK_LIMIT 15` (`:83`), `MIN_PASSWORD_LENGTH 4` (`:84`),
  `LOWEST_ALLOWED_PORT 1024` (`:81`), `BT709_FIX` (`:86`), `SRGB_FIX` (`:87`).

### 7.2 Callback table (`raop_callbacks_t`)
Struct declared at `lib/raop.h:71-113`; only `audio_process` and `video_process` are mandatory
(validated at `lib/raop.c:595-598`). Filled at `uxplay.cpp:2693-2729`:

```
conn_init, conn_destroy, conn_reset, conn_feedback                   uxplay.cpp:2695-2698
audio_process, video_process                                          :2699-2700
audio_flush, video_flush, video_pause, video_resume                   :2701-2704
audio_set_client_volume, audio_set_volume, audio_get_format           :2705-2707
video_report_size                                                     :2708
audio_set_metadata, audio_set_coverart,
audio_stop_coverart_rendering, audio_set_progress                     :2709-2712
report_client_request, display_pin, register_client,
check_register, passwd, export_dacp                                   :2713-2718
video_reset, video_set_codec, mirror_video_running                    :2719-2722
on_video_play/scrub/rate/stop/playlist_remove/acquire_playback_info   :2724-2729
```

**These 30 callbacks are the natural GUI integration surface.** `report_client_request` (allow/deny a
client), `display_pin` (show the pairing PIN), `register_client` / `check_register` (persisted pairings),
`passwd`, `video_report_size` and `mirror_video_running` are exactly what a GUI needs to drive its UI.

Where each callback is *implemented* in `uxplay.cpp` (this is the list a GUI would re-implement):

| Callback | Impl | Callback | Impl |
|---|---|---|---|
| `conn_init` | `:2232` | `audio_set_metadata` | `:2513` |
| `conn_destroy` | `:2238` | `audio_set_coverart` | `:2489` |
| `conn_feedback` | `:2256` | `audio_stop_coverart_rendering` | `:2500` |
| `conn_reset` | `:2261` | `audio_set_progress` | `:2506` |
| `report_client_request` | `:2281` | `register_client` | `:2565` |
| `audio_process` | `:2302` | `check_register` | `:2581` |
| `video_process` | `:2337` | `passwd` | `:2208` |
| `mirror_video_running` | `:2364` | `display_pin` | `:2196` |
| `video_pause` / `video_resume` | `:2372` / `:2378` | `export_dacp` | `:2220` |
| `audio_flush` / `video_flush` | `:2385` / `:2391` | `video_reset` | `:2125` |
| `audio_set_client_volume` | `:2397` | `video_set_codec` | `:2185` |
| `audio_set_volume` | `:2401` | `on_video_play` | `:2598` |
| `audio_get_format` | `:2447` | `on_video_scrub` / `_rate` / `_stop` | `:2609` / `:2614` / `:2637` |
| `video_report_size` | `:2483` | `on_video_playlist_remove` / `_acquire_playback_info` | `:2627` / `:2642` |

`mirror_video_running` is assigned only inside a `#ifdef DBUS` guard (`uxplay.cpp:2721-2723`), i.e. **never on Windows**.
`log_callback` (`uxplay.cpp:2673`) is not a struct field — it is installed via `raop_set_log_callback`
(`:2736`) and `logger_set_callback(render_logger, …)` (`:3177`).
There is **no** `conn_teardown`, `export_dnssd` or `registration_error` callback; client-key persistence
goes through `register_client` / `check_register`.

`conn_reset` (`:2261`) is what triggers the restart cycle: it sets `reset_httpd`, `relaunch_video`
and `reset_loop` (`:2274-2278`).

### 7.3 Start / stop sequence (`main()`)
```
uxplay.cpp:2891   ntp_global_init()                      [#ifdef _WIN32 — QPC init]
uxplay.cpp:2895   SetConsoleCtrlHandler(CtrlHandler)     [#ifdef _WIN32]
uxplay.cpp:3172   gstreamer_init()
uxplay.cpp:3243   start_dnssd(server_hw_addr, server_name)          → :1978
uxplay.cpp:3246   start_raop_server(display, tcp, udp, debug_log)   → :2692
uxplay.cpp:3252   raop_set_lang(...)                     [HLS only]
uxplay.cpp:3269   register_dnssd()                                  → :1929
uxplay.cpp:3277   main_loop()                                       → :671
uxplay.cpp:3310   stop_raop_server()  /  :3311 stop_dnssd()  /  :3313 cleanup() → :3316
```
Inside `start_raop_server` (`:2692-2777`): `raop_init` (`:2731`), `raop_set_log_callback` (`:2736`),
`raop_init2(raop, nohold, mac_address, keyfile)` (`:2739`), `raop_set_plist` × N (`:2748-2757`),
`raop_set_tcp_ports` / `raop_set_udp_ports` (`:2760-2761`), `raop_start_httpd` (`:2764`),
`raop_set_dnssd` (`:2771`).

### 7.4 Main loop
`main_loop()` (`uxplay.cpp:671-759`) is a **GLib main loop**, not a custom loop:
- `g_main_loop_new(NULL, FALSE)` (`:674`), `g_main_loop_run(loop)` (`:734`).
- Bus watches: `video_renderer_listen(loop, i)` per video renderer (`:703`),
  `audio_renderer_listen(loop, i)` per audio renderer (`:716`).
- Timeouts: 1 s `feedback_callback` (`:721`), 100 ms `reset_callback` (`:722`),
  1 s `progress_callback` (`:712`), 100 ms `video_eos_watch_callback` / `x11_window_callback` for HLS (`:698-699`).
- Windows: stashes the loop in the global `gmainloop` (`:724-725`) so the console CtrlHandler can
  `g_idle_add(handle_signal)` → `g_main_loop_quit` (`:581-600`). POSIX uses `g_unix_signal_add` (`:730-732`).
- **Restart mechanism:** the loop quits, `main()` inspects `relaunch_video` (`:3278`); if set it stops the
  audio renderer, destroys + re-inits + restarts the video renderer with the *same* option strings
  (`:3286-3297`), optionally bounces the httpd (`:3279-3281`, `:3299-3303`), then `goto reconnect` (`:3307`)
  back to `main_loop()`. This is the "new client connected / resolution changed" path.

### 7.5 Options → GStreamer pipeline strings
Option parsing is a long `if/else if` chain over `std::string arg` starting at `uxplay.cpp:1214`.

| Option | Line | Variable it sets | Default |
|---|---|---|---|
| `-vp <parser>` | `:1388-1391` | `video_parser` | `"h264parse"` (`:122`) |
| `-vd <decoder>` | `:1392-1395` | `video_decoder` | `"decodebin"` (`:123`) |
| `-vc <converter>` | `:1396-1399` | `video_converter` | `"videoconvert"` (`:124`) |
| `-vs <sink [opts]>` | `:1400-1409` | `videosink` + `videosink_options` — **splits on the first space**; everything after it becomes sink properties | `"autovideosink"` (`:107`), options `""` (`:108`) |
| `-as <sink>` | `:1410-1413` | `audiosink` | `"autoaudiosink"` (`:112`) |
| `-avdec` | `:1427-1433` | forces `h264parse` / `avdec_h264` / `videoconvert` | |
| `-v4l2` | `:1434-1438` | `v4l2h264dec` / `v4l2convert` | |
| `-bt709` | `:1586` | appends `" ! capssetter caps=\"video/x-h264, colorimetry=bt709\""` to `video_parser` (`:3110-3113`) | |
| `-srgb` | `:1588` | prepends `SRGB_FIX` to `video_converter` (`:3115-3119`) | |
| `-fs` | `:1448-1449` | `fullscreen = true` | |
| `-n <name>` | `:1233` | `server_name` | `"UxPlay"` |
| `-nh` | `:1249` | `do_append_hostname = false` | |
| `-vsync`/`-async` | `:1284`,`:1251` | `video_sync` / `audio_sync` | `video_sync=true` (`:101`), `audio_sync=false` (`:100`) |
| `-h265` | `:1751` | `h265_support` → features bit 42 + second pipeline | |
| `-hls` | `:1729-1730` | `hls_support` → features bits 0 and 4 | |
| `-p`/`-m`/`-a`/`-d`/`-s`/`-fps`/`-o`/`-f`/`-r` | `:1334`,`:1353`,`:1367`,`:1369`,`:1304`,`:1312`,`:1320`,`:1322`,`:1328` | ports / MAC / audio-off / debug / resolution / fps / overscan / flip / rotate | |
| `-pin`/`-pw`/`-reg`/`-key`/`-restrict`/`-allow`/`-block` | `:1612`,`:1651`,`:1625`,`:1636`,`:1224`,`:1216`,`:1220` | access control | |
| `-vrtp`/`-artp` | `:1460`,`:1468` | `rtp_pipeline` / `artp_pipeline` — bypass local rendering, forward RTP to e.g. OBS | |
| `-mp4`/`-vdmp`/`-admp` | `:1500`,`:1476`,`:1511` | file recording / raw dumps | |
| `-nofreeze`, `-nohold`, `-nc`, `-reset`, `-FPSdata`, `-taper`, `-db`, `-dacp`, `-p2p`, `-lang`, `-slang`, `-al`, `-scrsv`, `-rc` | `:1753`,`:1597`,`:1418`,`:1452`,`:1450`,`:1677`,`:1679`,`:1664`,`:1623`,`:1739`,`:1745`,`:1599`,`:1271`,`:1214` | misc | |
| `-t`, `-rpi*` | `:1414`,`:1439` | **removed**, print an error and `exit(1)` | |

Windows-specific sink post-processing, `uxplay.cpp:3084-3108`:
- `-fs` + `waylandsink`/`vaapisink` → ` fullscreen=true`; `kmssink` → ` force-modesetting=TRUE`.
- `d3d11videosink` with empty options → ` fullscreen-toggle-mode=GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_PROPERTY fullscreen=TRUE`
  when `-fs`, else `…_ALT_ENTER` (`:3092-3099`).
- `d3d12videosink` → ` fullscreen=TRUE` or ` fullscreen-on-alt-enter=TRUE` (`:3101-3108`).
- `-vs 0` disables video entirely: sink becomes `fakesink`, fps forced to 1 (`:3075-3082`).
- `-as 0` disables audio (`:3049-3052`).

Window title: `video_renderer_init` calls `g_set_application_name(server_name)` **once**
(`renderers/video_renderer.c:276-281`), which is what puts the name in the title bar. There is no
per-sink `title` property set. Config-file support: `$UXPLAYRC`, `~/.uxplayrc`, `~/.config/uxplayrc`
(`uxplay.cpp:781-802`), parsed by `read_config_file` (`:2787`).

### 7.6 Video pipeline construction — `renderers/video_renderer.c`
`video_renderer_init` signature at `renderers/video_renderer.c:252-254` / `video_renderer.h:50-53`.

Non-HLS pipeline is assembled with `GString` (`renderers/video_renderer.c:360-395`):

```
appsrc name=video_source ! queue ! <parser> ! <decoder> ! [videoflip …] <converter> ! videoscale ! <videosink> name=<videosink>_<codec><videosink_options> sync=<true|false>
```
- `appsrc name=video_source ! ` — `:360`
- jpeg/coverart variant substitutes `jpegdec` and inserts
  ` imagefreeze allow-replace=TRUE ! textoverlay name=metadata_overlay ! ` — `:361-362`, `:380-382`
- `-vrtp` variant replaces the decoder with `rtph264pay <rtp_pipeline>` and skips everything after — `:369-372`
- videoflip fragment from `append_videoflip` — `:376`, table at `:110-152`
- `sync=true` / `sync=false` — `:389-395`
- `h264`↔`h265` string rewriting so one template serves both codecs — `:397-409`
- Built string is logged at DEBUG (`:411`) then handed to `gst_parse_launch` (`:412`);
  `appsrc` is retrieved by name and configured `is-live=TRUE, format=GST_FORMAT_TIME` (`:427-429`).

HLS path uses `playbin` / `playbin3` instead (`:317-320`), with a manually built video-sink element via
`make_video_sink()` (`:183-224`, called at `:331`) when the sink is not an autovideosink.
`auto_videosink` is decided by substring match on `"autovideosink"` / `"fpsdisplaysink"` (`:260`).

**Default sink on Windows is `autovideosink`** — same as every platform (`uxplay.cpp:107`); there is
*no* `#ifdef _WIN32` default-sink override. README recommends the user set `-vs d3d12videosink`
manually (`README.md:1148-1153`). **For our app we should default to `d3d12videosink` (fallback `d3d11videosink`) ourselves.**

Audio pipeline (`renderers/audio_renderer.c:147-195`):
```
appsrc name=audio_source ! queue ! [avdec_aac | avdec_alac] ! audioconvert ! audioresample quality=10 ! volume name=volume ! [level ! ] <audiosink> sync=<…>
```

### 7.7 Windows-specific code in `uxplay.cpp`
Every `_WIN32` block (`grep -n "_WIN32" uxplay.cpp`):

| Lines | What it does |
|---|---|
| `:39-44` | Windows includes: `glib.h`, `<unordered_map>`, `winsock2.h`, `iphlpapi.h`, `pthread.h` ("for pthreads in MSYS2 UCRT"); the `#else` branch pulls `glib-unix.h`, `ifaddrs.h`, `pwd.h`, etc. |
| `:328-336` | `file_has_write_access` uses `_access(f,0)` / `_access(f,2)` instead of `access(F_OK/W_OK)`. |
| `:581-600` | `handle_signal` + `CtrlHandler(DWORD)` handling `CTRL_C_EVENT` / `CTRL_CLOSE_EVENT` / `CTRL_SHUTDOWN_EVENT`; posts `g_idle_add` into the GLib loop or calls `cleanup(); exit(0)`. |
| `:724-725`, `:736-737` | Sets/clears the global `gmainloop` around `g_main_loop_run` (POSIX instead installs `g_unix_signal_add` watches). |
| `:773-777` | `#ifndef _WIN32` — the `getpwuid(getuid())->pw_dir` home-dir fallback is skipped on Windows; only `$HOME`/`$HOMEPATH`-style env lookup remains (`:770-772`). |
| `:809-…` | `find_mac()` uses `GetAdaptersAddresses(AF_UNSPEC,…)`, filtering `PhysicalAddressLength==6`, `IfType ∈ {6 Ethernet, 71 Wireless}`, `OperStatus==1` (`:815-828`). |
| `:1134-1139` | `append_hostname()` uses `GetComputerNameA` instead of `uname()`. |
| `:2889-2892` | `ntp_global_init()` — initialises the QPC frequency for receive timestamping. **Windows-only requirement.** |
| `:2894-2898` | `SetConsoleCtrlHandler(CtrlHandler, TRUE)`; failure is fatal (`exit(1)`). |
| `:2990-2995` | `SetConsoleOutputCP(CP_UTF8)` and `setbuf(stdout, NULL)` when not in debug mode. |
| `:3257-3264` | BLE beacon: `GetCurrentProcessId()` instead of `getpid()`. |

Notably **absent** on every Windows path: no `WSAStartup()` in `uxplay.cpp` (socket init lives in
`lib/netutils.c` via `netutils_init()`, called from `raop_init` at `lib/raop.c:590`), no
`GST_PLUGIN_PATH` bootstrapping, no `GetModuleFileName`. **We will have to add plugin-path
bootstrapping ourselves for a relocatable build.**

Library-side Windows code: `lib/compat.h:18-29` (winsock2/ws2tcpip/mstcpip/mswsock/windows.h,
`snprintf`→`_snprintf`), `lib/CMakeLists.txt:21-24` (`-D_WIN32` CFLAGS) and `:69-77`
(links `wsock32`, `iphlpapi`, `ws2_32`), `lib/dns_sd/dns_sd.c:159-181` (`LoadLibraryA("dnssd.dll")`),
`lib/mdnsd/mdnsd.c:377` (a `#ifdef WIN32` block in the interface-enumeration path).

### 7.8 Config file and persistent state (important on Windows)

Startup config file — `find_uxplay_config_file()` (`uxplay.cpp:781-802`), search order:
1. `$UXPLAYRC` (`:787-791`)
2. `<homedir>/.uxplayrc` (`:794-796`)
3. `<homedir>/.config/uxplayrc` (`:797-799`)

`get_homedir()` (`uxplay.cpp:768-779`) tries `$XDG_CONFIG_HOMEDIR`, then `$HOME`, then (POSIX only,
compiled out on Windows at `:773-777`) `getpwuid(getuid())->pw_dir`.
**⇒ On Windows, if neither `XDG_CONFIG_HOMEDIR` nor `HOME` is set, `get_homedir()` returns `NULL`
and all persistent state silently stops working** (`uxplay.cpp:3159-3161` logs
"could not determine $HOME: public key will not be saved"). MSYS2 shells set `HOME`; a GUI launching
`uxplay.exe` from Explorer does **not**. Our wrapper must set `HOME` (or `XDG_CONFIG_HOMEDIR`)
explicitly in the child environment.

`-rc <file>` is pre-scanned directly over `argv` in `main()` (`uxplay.cpp:2919-2943`) before normal
parsing; `read_config_file()` (`:2787-2869`) strips `#` comments, handles quotes/escapes, prefixes each
line's first token with `-`, and recursively calls `parse_arguments`. CLI args are parsed afterwards
(`:2945`) and therefore win.

Persistent state files (all `<homedir>/…`, no `%APPDATA%` handling anywhere):

| File | Default path | Set at | Written/read at |
|---|---|---|---|
| Ed25519 key (pairing persistence) | `<homedir>/.uxplay.pem` — only when `-key` is given bare **and** `pin_pw == 1` | `uxplay.cpp:3153-3162` | passed to `raop_init2` (`:2739`); actual file I/O in `lib/pairing.c` |
| Registered client keys | `<homedir>/.uxplay.register` | `uxplay.cpp:3121-3129` | read `:3132-3151` (44-char base64 per line); written by `register_client` `:2565`; queried by `check_register` `:2581` |
| DACP remote info | `<homedir>/.uxplay.dacp` | `uxplay.cpp:1674-1676` | written `:2220-2230`, deleted on last disconnect `:2248-2250` |
| coverart / metadata / BLE PID | user-specified (`-ca`, `-md`, `-ble`) | `:1535`, `:1548`, `:1561` | removed in `cleanup()` `:3334-3340` |

Additional options the table in §7.5 did not list: `-ca <file>` (`:1535`), `-md <file>` (`:1548`),
`-ble <file>` (`:1561`), `-vol <v>` (`:1703`).
`-p` defaults: TCP `7100/7000/7001`, UDP `7011/6001/6000` (`uxplay.cpp:1335-1338`).
`-pin [nnnn]` sets `setup_legacy_pairing = true; pin_pw = 1;` and stores `pin = n + 10000` (`:1612-1621`).
`-pw` bare ⇒ `pin_pw = 3` (fresh random password each connection); with an argument ⇒ `pin_pw = 2`.

---

## 8. Is there a library mode? Embedding assessment

### 8.1 What exists
**There is a real C API — it just isn't packaged as one.**

- `lib/raop.h:125-144` declares 20 `RAOP_API` functions (`raop_init`, `raop_init2`, `raop_set_*`,
  `raop_start_httpd`, `raop_stop_httpd`, `raop_is_running`, `raop_destroy`, …). `RAOP_API` is defined
  as empty at `lib/raop.h:26`, and the header is `extern "C"`-guarded (`:30-32`).
- `lib/dnssd.h:58-75` declares the whole DNS-SD façade, also `extern "C"` (`:23-25`), also with a
  `DNSSD_API` marker (`:19-21`).
- `renderers/video_renderer.h` and `renderers/audio_renderer.h` are `extern "C"` too (`:30-32` / `:26-28`).
- Everything is built as **static** libraries: `airplay`, `renderers`, `dnssd`, `llhttp`, `playfair`.

So `uxplay.cpp` is **not** where the protocol lives — it is a ~3400-line *application* that configures
the library, supplies 30 callbacks, and owns a GLib main loop.

### 8.2 What is missing / in the way
1. **No install/export of the libraries.** `CMakeLists.txt:87` installs only the executable. No headers,
   no `.pc`, no CMake package config. We must consume UxPlay via `add_subdirectory()` (or a vendored
   copy) rather than `find_package`.
2. **The renderers are process-global singletons.** `video_renderer_start()` / `audio_renderer_flush()`
   etc. take no handle (`renderers/video_renderer.h:54-77`); state lives in file statics
   (`renderers/video_renderer.c:37-42` and the `renderer_type[]` array). ⇒ **one receiver per process.**
   `raop_t` itself *is* a handle, so the protocol core is re-entrant-ish; the renderers are not.
3. **All policy lives in `uxplay.cpp` statics**, not in the library: the features bitmap
   (`uxplay.cpp:2001-2092`), the client allow/block lists (`:158-160`), the pipeline strings (`:107-124`),
   the restart logic (`:3278-3307`). Reusing `lib/` means re-implementing that ~600-line policy layer.
4. **GLib main loop required.** The renderers publish bus watches onto a `GMainLoop`
   (`video_renderer_listen`, `audio_renderer_listen`). Any GUI toolkit we pick must either run a GLib
   loop on a dedicated thread or integrate its own loop with GLib's context.
5. `exit()` is called directly all over the application layer — option parsing (`uxplay.cpp:1389`,
   `:1415`, `:1447`, `:1758`), init failures (`:2897`, `:3174`) and even `cleanup()` itself
   (`uxplay.cpp:3354`). None of that is recoverable in-process, so approach B below must not reuse
   `uxplay.cpp`'s error paths verbatim.
7. Restart is a `goto reconnect` inside `main()` (`uxplay.cpp:3274`, `:3307`), not a re-entrant API.
6. Toolchain: the libs are built by MinGW-GCC with `pthread`. Linking them into an **MSVC** GUI is not
   supported out of the box (mixing MinGW static libs + MSVC is fragile; GStreamer itself must match).
   **UNVERIFIED** whether an MSVC build of `lib/` compiles at all — CI only builds MinGW.

### 8.3 Recommendation

| Approach | Effort | Verdict |
|---|---|---|
| **A. Wrap `uxplay.exe` as a child process** — GUI spawns it with generated args / a generated `uxplayrc`, parses stdout for state, kills/restarts on config change. | Low. Zero upstream changes; `patches/` stays empty. | **Best first milestone.** stdout is line-based and unbuffered on Windows (`uxplay.cpp:2990-2994`), and `SetConsoleOutputCP(CP_UTF8)` makes parsing predictable. Limitations: no in-process window embedding (the sink owns its own HWND), coarse control, PIN/allow-deny prompts only observable as text. |
| **B. Link `lib/` + `renderers/` into our own process**, replacing `uxplay.cpp` with our own `uxplay_core.cpp` that keeps the same callback table but exposes a small C API to the GUI. | Medium. ~600-800 lines to port the policy layer; must keep it in sync with upstream. | **The right end state.** Gives real callbacks (`display_pin`, `report_client_request`, `video_report_size`, `mirror_video_running`) instead of stdout scraping, and lets us set the video sink's window handle via `GstVideoOverlay` so the stream renders inside our own window. Requires our GUI to be MinGW-buildable (or a MinGW-built DLL boundary with a plain C ABI). |
| C. Fork and modify `uxplay.cpp` in place | Low-medium | Rejected — makes rebasing on upstream painful and violates the "don't edit third_party" rule. |

**Concrete path:** ship milestone 1 with approach A, and in parallel build approach B as a
`uxplay_core` static/shared lib compiled by the same MSYS2/UCRT64 toolchain, exposing a narrow
`extern "C"` surface (start/stop, config struct, event callback) that our GUI (also UCRT64, or MSVC
talking to a MinGW-built DLL through a plain C ABI) consumes. Keep every upstream change as a file
in `patches/`.

---

## 9. Open questions / to verify

- **UNVERIFIED:** whether the internal `lib/mdnsd` responder actually works on Windows 10 alongside a
  running Apple Bonjour service (both want UDP 5353; `SO_REUSEADDR`/`SO_REUSEPORT` are set at
  `lib/mdnsd/mdnsd.c:447-450`, but `SO_REUSEPORT` does not exist on Windows). **Needs a real test.**
- **UNVERIFIED:** the DLL closure needed for a standalone (non-MSYS2) distribution, and whether
  GStreamer plugins load from a relocated directory via `GST_PLUGIN_SYSTEM_PATH`.
- **UNVERIFIED:** whether `d3d12videosink` or `d3d11videosink` is the better default on our target
  hardware (README notes d3d12 segfaults on some older Nvidia cards on rotation, `README.md:1141-1146`).
- **UNVERIFIED / MANUAL VERIFICATION REQUIRED:** actual iPhone mirroring, pairing PIN flow, and
  h265 (`-h265`) behaviour — cannot be tested by an agent.
- **UNVERIFIED:** whether `lib/` compiles under MSVC (CI is MinGW-only).
- **Confirmed gotcha (not UNVERIFIED, but easy to miss):** launched without `HOME`/`XDG_CONFIG_HOMEDIR`,
  `uxplay.exe` loses all persistent state (pairing key, registered clients, rc file). Our launcher must
  set it. See §7.8.
- Licence: UxPlay is GPLv3 and vendors `lib/playfair`, whose legal status upstream itself calls
  "unclear" (`README.md:2486-2489`). Our app linking it is a **derived work under GPLv3**.
