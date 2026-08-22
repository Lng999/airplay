# BUILD-NOTES — Phase 0, executed 2026-08-20

Everything below was **actually run** on this machine on 2026-08-20. Nothing here is
predicted: every command, version, size and TXT record is copied out of the real console
logs of that run.

## Machine

| | |
|---|---|
| OS | Windows 10 Pro 22H2, build 19045 (UBR 6466) |
| CPU / GPU | AMD Ryzen 5 5600 (6C/12T) / NVIDIA GeForce RTX 4060 |
| Network | Ethernet, **Private** profile, `192.168.1.107` |
| UxPlay | submodule `third_party/UxPlay` @ `a3c19cbc` (2026-08-09), `VERSION "1.74"` |
| Toolchain | MSYS2 **UCRT64** — gcc 16.2.0, cmake 4.4.2, ninja 1.13.2, pkgconf 3.0.5 |
| Media | GStreamer 1.28.6 (base/good/bad/libav), OpenSSL 3.6.3, libplist 2.7.0 |

Before this run the box had **no** MSYS2, **no** GStreamer and **no** pkg-config
(`docs/research/local-environment.md`). VS 2026 was present but unused — UxPlay needs MinGW.

---

## 1. Commands that were actually run, in order

### a. MSYS2 silent install (`scripts/setup-msys2.ps1`)

```powershell
pwsh -NoProfile -File scripts/setup-msys2.ps1
```

which downloads `msys2-x86_64-latest.exe` and runs the Qt Installer Framework verb:

```
msys2-x86_64-latest.exe in --confirm-command --accept-messages --root C:/msys64
```

`--root` takes **forward** slashes. Result: `Installation finished! Components installed
successfully.`, exit 0, 268.23 MB installed. The installer itself then ran
`C:/msys64\usr\bin\bash.exe --login -c exit`, which triggered MSYS2's first-run setup
(pacman master key generation, keyring signing, trust DB).

### b. Core update — twice

```
pacman -Syuu --noconfirm      # pass 1 (may kill the shell -> exit code ignored)
pacman -Syuu --noconfirm      # pass 2
```

Pass 1 upgraded `ca-certificates`; pass 2 reported `Starting core system upgrade... yapılacak
bir şey yok` (nothing to do) — i.e. the runtime was already current, so the usual
"close the terminal and run again" restart was not needed.

### c. Packages (13)

```
pacman -S --needed --noconfirm \
  mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-openssl mingw-w64-ucrt-x86_64-libplist \
  mingw-w64-ucrt-x86_64-gstreamer mingw-w64-ucrt-x86_64-gst-plugins-base \
  mingw-w64-ucrt-x86_64-gst-plugins-good mingw-w64-ucrt-x86_64-gst-plugins-bad \
  mingw-w64-ucrt-x86_64-gst-libav mingw-w64-ucrt-x86_64-ntldd git
```

Post-install hooks printed one non-fatal `hata: komut düzgün çalıştırılamadı` while updating
the fontconfig cache; every other hook (GDK-Pixbuf, GIO, GSettings, info dir) succeeded and
the step exited 0. Verification output of that same step:

```
gcc.exe (Rev3, Built by MSYS2 project) 16.2.0
cmake version 4.4.2
1.13.2                        # ninja
3.0.5                         # pkgconf
gst-inspect-1.0 version 1.28.6
212                           # gst-inspect-1.0 plugin count
```

### d. Build

```
MSYSTEM=UCRT64 C:\msys64\usr\bin\bash.exe -l <wrapper.sh>
```

where `wrapper.sh` is exactly:

```bash
#!/bin/bash
cd /c/Users/pc/Desktop/airplay || exit 1
exec bash scripts/build.sh "$@"
```

`scripts/build.sh` configured with (from `build.log`):

```
cmake -S third_party/UxPlay -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DNO_MARCH_NATIVE=ON
cmake --build build -j
```

No `-DUSE_DNS_SD` -> the bundled `lib/mdnsd` responder, which CMake confirmed:

```
-- ******** using internal minimal mDNSResponder implementation (lib/mdnsd) for Service Discovery
```

### e. Smoke test

```powershell
pwsh -NoProfile -File scripts/smoke-test.ps1
```

### Gotcha: `bash.exe -lc` does not start in the repo

Invoking `C:\msys64\usr\bin\bash.exe -lc "ntldd -R build/uxplay.exe"` from Git Bash lands in
the **MSYS2 home** (`/home/pc`), not in the calling directory — `CHERE_INVOKING=1` did **not**
help, because the login shell's profile still moves to `$HOME`. Symptom:
`build/uxplay.exe: not found` even though the file exists. Always put an **absolute `cd`**
inside the command string (or use a wrapper script that does):

```
C:\msys64\usr\bin\bash.exe -lc "cd /c/Users/pc/Desktop/airplay && ./scripts/build.sh"
```

`scripts/README.md` already spells the command this way — keep the `cd` there; it is not
decoration.

---

## 2. Build result

```
==> Result
executable : /c/Users/pc/Desktop/airplay/build/uxplay.exe
size       : 932431 bytes
DLL deps   : 491 total (transitive), 15 from ucrt64  [ntldd -R]
```

- 40/40 ninja steps, no failures. Configure took 2.5 s.
- Targets built: `libplayfair.a`, `libdnssd.a`, `libllhttp.a`, `libairplay.a`,
  `librenderers.a`, then `uxplay.exe`.
- Effective CFLAGS reported by CMake:
  `-DSTANDALONE -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -DTARGET_POSIX -D_WIN32 -fPIC -DPIC -D_REENTRANT -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -Wall -O2`
  and `-- Not using -march=native` — `-DNO_MARCH_NATIVE=ON` took effect, so the binary is not
  pinned to this exact CPU.
- Dependencies resolved by CMake: libplist 2.7.0, OpenSSL 3.6.3, glib 2.88.3,
  gstreamer / -sdp / -video / -app 1.28.6, pkg-config 3.0.5.

## 3. Smoke test result — summary table (verbatim)

```
STATUS CHECK                                    DETAIL
------ -----                                    ------
PASS   MSYS2 UCRT64 present                     C:\msys64\ucrt64\bin\gst-inspect-1.0.exe (gst-inspect-1.0 version 1.28.6)
PASS   GStreamer elements (17)                  all present; registry C:\Users\pc\AppData\Local\airplay\gst-registry.bin
PASS   uxplay.exe exists                        C:\Users\pc\Desktop\airplay\build\uxplay.exe
PASS   uxplay.exe -v                            UxPlay version 1.74; for help, use option "-h"
PASS   Network profile is Private               Ethernet=Private
PASS   IPv4 address                             Ethernet=192.168.1.107
INFO   UDP 5353 listeners (informational)       svchost (pid 2988, ::); svchost (pid 2988, 0.0.0.0)
PASS   Apple Bonjour Service                    not installed (internal mDNS responder has 5353 to itself)
PASS   Firewall allows inbound UDP 5353         66 enabled inbound allow rule(s), e.g. Brave (mDNS-In), Google Chrome (mDNS-In), mDNS (UDP-Gelen), Microsoft Edge (mDNS-Gelen), AllJoyn Yönlendirici (UDP-In), Among Us, AnyDesk
WARN   Project firewall rules (airplay:*)       none found - run the firewall setup script (New-NetFirewallRule -DisplayName "airplay: ..." needs admin)
PASS   python + zeroconf                        C:\Users\pc\Desktop\airplay\.venv\Scripts\python.exe  -> zeroconf 0.150.0
PASS   scripts\mdns-browse.py present           C:\Users\pc\Desktop\airplay\scripts\mdns-browse.py
PASS   receiver advertises _airplay._tcp on LAN mDNS service matching "AirPlay-PC" seen within 6s


11 PASS  0 FAIL  1 WARN  0 SKIP

NOTE: real iPhone mirroring (video + audio) cannot be tested here -> MANUEL DOGRULAMA GEREKLI.
```

(The raw log renders the Turkish rule names mojibake'd — `AllJoyn Y?nlendirici` — because the
console code page is not UTF-8. Restored above; content is otherwise verbatim.)

All 17 required GStreamer elements resolved: `appsrc queue h264parse decodebin avdec_h264
videoconvert videoscale d3d11videosink autovideosink avdec_aac avdec_alac audioconvert
audioresample volume level wasapi2sink autoaudiosink`.

## 4. Advertised mDNS TXT records (verbatim from the live receiver)

Launched as `uxplay.exe -n AirPlay-PC -p 7100 -vs d3d11videosink -as wasapi2sink`, then
browsed for 6 s with `scripts/mdns-browse.py`:

```
  [_airplay._tcp] AirPlay-PC@DESKTOP-57478B1._airplay._tcp.local.
      host    : DESKTOP-UA4DNG8.local.
      address : 127.0.0.1
      port    : 7101
      key TXT fields:
        * features = 0x527FFEE6,0x0  = 0x527FFEE6 (64-bit)   [AirPlay features bitmask (low32,high32)]
        * deviceid = 82:7d:a5:fc:4c:bd   [MAC address of the advertising interface]
        * model    = AppleTV3,2   [advertised hardware model (UxPlay: AppleTV3,2)]
        * srcvers  = 220.68   [AirPlay source version (UxPlay: 220.68)]
        * flags    = 0x4   [status flags (UxPlay hardcodes 0x4)]
        * pk       = 1a7edd49579373ae...bb0d417e   [Ed25519 public key (per-install)]
      other TXT fields:
          pi       = 2e388006-13ba-4041-9a67-25dd4a43d536
          pw       = false
          vv       = 2

  [_raop._tcp] 827DA5FC4CBD@AirPlay-PC@DESKTOP-57478B1._raop._tcp.local.
      host    : DESKTOP-UA4DNG8.local.
      address : 127.0.0.1
      port    : 7101
      key TXT fields:
        * pk       = 1a7edd49579373ae...bb0d417e   [Ed25519 public key (per-install)]
        * ft       = 0x527FFEE6,0x0  = 0x527FFEE6 (64-bit)   [RAOP features bitmask (low32,high32)]
        * am       = AppleTV3,2   [RAOP model]
        * vs       = 220.68   [RAOP server version]
      other TXT fields:
          ch       = 2
          cn       = 0,1,2,3
          da       = true
          et       = 0,3,5
          md       = 0,1,2
          pw       = false
          rhd      = 5.6.0.0
          sf       = 0x4
          sr       = 44100
          ss       = 16
          sv       = false
          tp       = UDP
          txtvers  = 1
          vn       = 65537
          vv       = 2

Summary: 1 x _airplay._tcp, 1 x _raop._tcp in 6s
```

### features — prediction vs reality

**`features = 0x527FFEE6,0x0` — MATCHES** `docs/DESIGN.md` exactly (`DESIGN.md:386-388` and
`:931-932`: "default advertised `features` = `0x527FFEE6,0x0` … not the active
`0x5A7FFEE6`"). The static seed macro `FEATURES_1 = "0x5A7FFEE6"` (`lib/dnssdint.h:32`) is
indeed overwritten bit by bit at `uxplay.cpp:2001-2092`, and bit 27 ("supports legacy
pairing") ends up **OFF** without `-pin`. `_raop`'s `ft` carries the same value, as DESIGN.md
predicted. `flags` / `sf` = `0x4` and `model` / `am` = `AppleTV3,2` also match.

## 5. Installed package versions (`pacman -Q`, read back after the build)

| Package | Version |
|---|---|
| mingw-w64-ucrt-x86_64-gcc | 16.2.0-3 |
| mingw-w64-ucrt-x86_64-gcc-libs | 16.2.0-3 |
| mingw-w64-ucrt-x86_64-gcc-libgfortran | 16.2.0-3 |
| mingw-w64-ucrt-x86_64-cmake | 4.4.2-2 |
| mingw-w64-ucrt-x86_64-ninja | 1.13.2-1 |
| mingw-w64-ucrt-x86_64-pkgconf | 1~3.0.5-1 |
| mingw-w64-ucrt-x86_64-openssl | 3.6.3-1 |
| mingw-w64-ucrt-x86_64-libplist | 2.7.0-4 |
| mingw-w64-ucrt-x86_64-gstreamer | 1.28.6-1 |
| mingw-w64-ucrt-x86_64-gst-plugins-base | 1.28.6-1 |
| mingw-w64-ucrt-x86_64-gst-plugins-good | 1.28.6-1 |
| mingw-w64-ucrt-x86_64-gst-plugins-bad | 1.28.6-2 |
| mingw-w64-ucrt-x86_64-gst-plugins-bad-libs | 1.28.6-2 |
| mingw-w64-ucrt-x86_64-gst-libav | 1.28.6-2 |
| mingw-w64-ucrt-x86_64-ntldd | r19.7fb9365-4 |

## 6. The 15 ucrt64 DLLs `uxplay.exe` needs

`ntldd -R build/uxplay.exe | grep -i ucrt64` (run from the repo root inside UCRT64):

```
libcrypto-3-x64.dll     libgstapp-1.0-0.dll
libffi-8.dll            libgstbase-1.0-0.dll
libgcc_s_seh-1.dll      libgstreamer-1.0-0.dll
libglib-2.0-0.dll       libiconv-2.dll
libgmodule-2.0-0.dll    libintl-8.dll
libgobject-2.0-0.dll    libpcre2-8-0.dll
libplist-2.0.dll        libstdc++-6.dll
libwinpthread-1.dll
```

These 15 are the link-time closure only. The GStreamer **plugins** (`d3d11videosink`,
`wasapi2sink`, `avdec_*`, …) are dlopen'd at runtime from `C:\msys64\ucrt64\lib\gstreamer-1.0`
and do **not** appear here — a self-contained bundle (Phase 3) must copy them separately.

## 7. Gotchas observed

1. **Three harmless `cp: cannot stat` lines.** MSYS2's first-run setup, and every login shell
   afterwards, prints:
   ```
   /usr/bin/cp: cannot stat 'C:\Windows\system32\drivers\etc\protocol': No such file or directory
   /usr/bin/cp: cannot stat 'C:\Windows\system32\drivers\etc\services': No such file or directory
   /usr/bin/cp: cannot stat 'C:\Windows\system32\drivers\etc\networks': No such file or directory
   ```
   Those three files simply do not exist on this Win10 install (only `hosts` does). Cosmetic —
   they appear on stderr at the top of `build.log` and on every `bash -lc` call. Filter with
   `2>&1 | grep -v 'cannot stat'` when parsing output.
2. **UDP 5353 is shared.** `svchost` (pid 2988) holds `::` and `0.0.0.0` on 5353, and
   Spotify / Brave join the same multicast group when they are running. The bundled
   `lib/mdnsd` responder still advertised successfully — multicast sockets are shared, not
   exclusive. Apple's Bonjour Service is **not** installed here, which removes the known
   issue #297 conflict.
3. **`airplay:*` firewall rules did not exist at smoke-test time** (the one WARN). 66
   unrelated inbound UDP allow rules already covered 5353, which is why discovery worked
   anyway. `scripts/firewall-rules.ps1` (elevated) was applied afterwards; re-run the smoke
   test to turn that WARN into a PASS.
4. **The GStreamer registry is project-private:** `%LOCALAPPDATA%\airplay\gst-registry.bin`,
   set via `GST_REGISTRY` by both `run-uxplay.ps1` and `smoke-test.ps1`, so an OBS or other
   GStreamer install cannot poison it. Pairing state goes to `%APPDATA%\airplay` through
   `HOME` / `XDG_CONFIG_HOMEDIR`.
5. **`gst-inspect-1.0` counts 212 plugins** with this package set — a quick sanity number if
   the registry ever has to be rebuilt.
6. **`scripts/build.sh` resets the submodule before it patches** (added 2026-08-22 with
   `patches/0005`). It runs `git checkout -- .` *and* `git clean -fd` inside
   `third_party/UxPlay`, so anything hand-edited there — including untracked files — is
   discarded on every build. Edit `patches/*.patch`, never the submodule. The previous
   scheme detected an already-applied patch with `git apply --reverse --check`, and that
   broke the moment two patches touched the same neighbourhood: `0005` rewrites the context
   `0004` is anchored on, so on a correctly patched tree neither check succeeded and the
   build died claiming the patch did not apply.

## 8. Verified vs NOT verified

### Verified on 2026-08-20 (real output exists)

- UxPlay `a3c19cbc` builds clean on MSYS2 UCRT64 -> `build\uxplay.exe`, 932 431 bytes,
  40/40 ninja steps.
- `uxplay.exe -v` runs: `UxPlay version 1.74`.
- All 17 GStreamer elements the design needs are present, including `d3d11videosink`,
  `wasapi2sink`, `avdec_aac`, `avdec_alac`, `h264parse`.
- The receiver **starts and advertises** `_airplay._tcp` **and** `_raop._tcp` using the
  internal `lib/mdnsd` responder — no Bonjour SDK needed. Upstream issue #546 did **not**
  bite here.
- The advertised TXT records, including `features = 0x527FFEE6,0x0`, match DESIGN.md's
  reverse-engineered prediction.
- Network preconditions: Private profile, `192.168.1.107`, inbound UDP 5353 allowed.

### NOT verified — do not claim these work

- **iPhone discovery, mirroring, audio.** No device was involved. Nothing about video decode,
  the FairPlay handshake, latency or audio sync is proven. -> walk `docs/MANUAL-VERIFY.md`.
- **Discovery from a *different* LAN host.** The browse was **self-discovery**:
  `mdns-browse.py` ran on the same machine and resolved the service to `127.0.0.1` /
  `DESKTOP-UA4DNG8.local.`. That proves the responder answers queries; it does **not** prove
  the announcement leaves the Ethernet NIC and survives the firewall as seen by another host.
- **`-DUSE_DNS_SD=1` fallback build.** Never attempted — the Bonjour SDK is not installed. If
  the internal responder fails against a real iPhone, that path is still untested code.
- **A self-contained (copy-the-DLLs) bundle.** Only the 15-DLL link closure is known; the
  runtime plugin set has not been enumerated or relocated.
