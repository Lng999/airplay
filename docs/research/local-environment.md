# Local Environment Inventory

Machine survey for building UxPlay on this Windows box. **Nothing was installed or modified**; all data below comes from live commands run on 2026-08-20.

Legend: **PRESENT** = verified installed · **MISSING** = verified absent · **UNKNOWN** = could not determine (reason given).

---

## 0. Summary table

| Item | Status | Note |
|---|---|---|
| MSYS2 | **MISSING** | no `C:\msys64`, nothing on any drive |
| Standalone GStreamer | **MISSING** | no env vars, no `C:\gstreamer` |
| Visual Studio 2026 Community + C++ | **PRESENT** | full MSVC x64 toolset + clang-cl |
| CMake 4.3.1 / Ninja 1.13.2 | **PRESENT** | bundled with VS, **not on PATH** |
| Git 2.53.0 | **PRESENT** | on PATH |
| Python 3.14.3 | **PRESENT** | on PATH (meson could be pip-installed) |
| pkg-config / pkgconf | **MISSING** | needed by UxPlay's CMakeLists |
| gcc / clang / cl on PATH | **MISSING** | present inside VS, needs a dev shell |
| Apple Bonjour / iTunes | **MISSING** | no service, no `dns-sd.exe` |
| WSL | **MISSING** | no distro installed |
| Network | Ethernet, **Private** profile | 192.168.1.107/24 |
| Firewall | enabled on all 3 profiles | generic mDNS 5353 rules already allow |
| GPU | NVIDIA RTX 4060 | `d3d11videosink` is the natural choice |
| Free disk on C: | **53.7 GB** | tight-ish but workable |

---

## 1. MSYS2 — MISSING

```
Test-Path C:\msys64                       -> False
Get-ChildItem C:\ -Directory -Filter msys*   -> (empty)
```

Swept every fixed drive root plus `C:\Program Files`, `C:\Program Files (x86)`,
`C:\Users\pc`, `C:\Users\pc\AppData\Local` for `msys|mingw|cygwin` — **no hits**.
Only fixed filesystem drive is `C:`.

Consequence: no `pacman`, so no `ucrt64` / `mingw64` / `clang64` environments exist.
There is nothing to query with `pacman -Q`.

`bash.exe` **does** resolve, but it is `C:\Windows\system32\bash.exe` (the WSL launcher),
and WSL has no distribution installed:

```
wsl.exe -l -v -> "Linux için Windows Alt Sistemi yüklü dağıtımı yok."
```

The Bash tool used for this survey is Git-for-Windows' bundled MinGW shell — it has no
compiler, no pacman, and is not a substitute for MSYS2.

## 2. Standalone GStreamer (official MSVC/MinGW installer) — MISSING

```
Get-ChildItem Env: | ? Name -like *GSTREAMER*   -> (empty)
Get-ChildItem Env: | ? Name -like *GST*         -> (empty)
```

- `GSTREAMER_1_0_ROOT_MSVC_X86_64` — not set
- `GSTREAMER_1_0_ROOT_MINGW_X86_64` — not set
- `C:\gstreamer` — does not exist
- No `PATH` entry matching `gstreamer|msys|mingw`
- `gst-inspect-1.0` — not on PATH
- `winget list` (194 installed packages) — no GStreamer entry

So **no GStreamer at all**: neither the standalone runtime/devel installer nor an
MSYS2-provided one. This is the single biggest gap — UxPlay's renderers are pure GStreamer.

## 3. Tools on PATH

| Tool | Status | Path / version |
|---|---|---|
| `git` | PRESENT | `C:\Program Files\Git\cmd\git.exe` — `git version 2.53.0.windows.3` |
| `winget` | PRESENT | `...\WindowsApps\winget.exe` — `v1.29.280`; sources `msstore`, `winget`, `winget-font` all configured |
| `python` | PRESENT | `C:\Users\pc\AppData\Local\Programs\Python\Python314\python.exe` — `Python 3.14.3` |
| `py` | PRESENT | `C:\Users\pc\AppData\Local\Programs\Python\Launcher\py.exe` |
| `python3` | PRESENT (shim) | `...\WindowsApps\python3.exe` — Store alias stub |
| `cmake` | MISSING on PATH | but bundled in VS, see §4 |
| `ninja` | MISSING on PATH | but bundled in VS, see §4 |
| `gcc` / `clang` / `cl` | MISSING on PATH | `cl` and `clang-cl` exist inside VS; require a Developer shell / `vcvarsall.bat` |
| `make` | MISSING | — |
| `pkg-config` / `pkgconf` | **MISSING** | UxPlay's `CMakeLists.txt:31` does `find_package(PkgConfig REQUIRED)` — this will fail until MSYS2 (or a standalone pkgconf) provides it |

## 4. Visual Studio — PRESENT (2026 Community, C++ workload confirmed)

```
displayName         : Visual Studio Community 2026
installationPath    : C:\Program Files\Microsoft Visual Studio\18\Community
installationVersion : 18.8.12021.73
```

C++ desktop workload confirmed — `vswhere -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64`
returns the same install.

Contents that matter:

- MSVC toolsets: `14.51.36231` (current), `14.16.27023` (VS2017-era compat toolset)
- LLVM/clang-cl: `VC\Tools\Llvm\x64` and `VC\Tools\Llvm\ARM64`
- Windows SDK: `10.0.26100.0` (`C:\Program Files (x86)\Windows Kits\10\Include`)
- Bundled CMake — `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` → `cmake version 4.3.1-msvc1`
- Bundled Ninja — `...\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe` → `1.13.2`

Note: CMake **4.x** rejects `cmake_minimum_required(VERSION 3.13)` compatibility below 3.5
and warns/errors on old-style minimums. UxPlay's `third_party/UxPlay/CMakeLists.txt:1` declares
`cmake_minimum_required( VERSION 3.13 )`, which CMake 4 still accepts, but any dependency
declaring `< 3.5` would break. MSYS2's own CMake (typically 3.3x/4.x) is the safer path anyway.

## 5. Apple Bonjour / iTunes — MISSING

```
Get-Service "Bonjour Service"    -> (no such service)
Test-Path "C:\Program Files\Bonjour"        -> False
Test-Path "C:\Program Files (x86)\Bonjour"  -> False
Get-Command dns-sd                          -> (not found)
Uninstall registry scan for iTunes|Bonjour|Apple -> (no matches)
```

No Apple software of any kind. UxPlay on Windows needs an mDNS/DNS-SD responder for
`_airplay._tcp` / `_raop._tcp` advertisement — either Apple's Bonjour SDK for Windows
(`dnssd.lib` + `Bonjour Service`) or a bundled alternative.

**Port-conflict warning (verified live):** UDP 5353 is already bound by two processes:

```
:::5353        <- svchost   (pid 2988)   # Windows' own DNS Client mDNS responder
0.0.0.0:5353   <- Spotify   (pid 18696)  # Spotify Connect discovery
```

Both bind with SO_REUSEADDR-style sharing, so a third responder can usually coexist, but
Windows' built-in mDNS and Bonjour's `mDNSResponder` are known to interfere with each other's
record publication. Expect to test this and possibly disable one of them.

## 6. Network & firewall

**Adapter — single wired connection, no Wi-Fi adapter up:**

```
Name     InterfaceDescription               LinkSpeed ifIndex
Ethernet Realtek PCIe GbE Family Controller 1 Gbps    7
```

**Connection profile — Private (good; mDNS rules are scoped to LocalSubnet):**

```
Name             : Ağ 6
InterfaceAlias   : Ethernet
NetworkCategory  : Private
IPv4Connectivity : Internet
```

**IPv4:** `192.168.1.107/24` on `Ethernet`.

> Relevant for AirPlay: the iPhone will be on Wi-Fi while this PC is on Ethernet. They must be
> on the **same L2 subnet / same router** for mDNS multicast to reach the PC — verify the AP and
> the LAN port are bridged and that the router does not have client/AP isolation enabled.
> This is a **MANUEL DOĞRULAMA GEREKLİ** item.

**Firewall profiles — all enabled:**

```
Name    Enabled
Domain  True
Private True
Public  True
```

**Existing mDNS rules — generic Windows rules are present and enabled:**

```
Rule Name: mDNS (UDP-Gelen)   In  UDP/5353  Profiles: Private  RemoteIP: LocalSubnet  Allow  Enabled
Rule Name: mDNS (UDP-Gelen)   In  UDP/5353  Profiles: Domain   RemoteIP: Any          Allow  Enabled
Rule Name: mDNS (UDP-Gelen)   In  UDP/5353  Profiles: Public   RemoteIP: LocalSubnet  Allow  Enabled
Rule Name: mDNS (UDP-Giden)   Out UDP/5353  Profiles: Private/Domain/Public           Allow  Enabled
```

Plus per-browser rules (`Brave (mDNS-In)`, `Google Chrome (mDNS-In)`, `Microsoft Edge (mDNS-Gelen)`)
— those are app-scoped and irrelevant to us.

**No rules exist mentioning `uxplay` or `AirPlay`.** UxPlay's RAOP/AirPlay TCP ports
(7000, 7100, and the dynamically-chosen data ports) have **no allow rule** — inbound
connections will prompt or be silently dropped. A rule will need to be created for
`uxplay.exe` once built.

> Caveat: `Get-NetFirewallRule` failed with *"Erişim engellendi"* (access denied) in the
> non-elevated shell. The rule data above came from `netsh advfirewall firewall show rule name=all`,
> which does work unprivileged. **Creating** rules will require an elevated shell.

## 7. System / OS / hardware

```
Caption        : Microsoft Windows 10 Pro
Version        : 10.0.19045
DisplayVersion : 22H2
CurrentBuild   : 19045   UBR: 6466
OSArchitecture : 64 bit
```

> Note: `README.md` says "Windows 11 için" but this machine is **Windows 10 Pro 22H2** (build 19045.6466).
> Worth reconciling in the project docs.

```
CPU: AMD Ryzen 5 5600 6-Core   (6 cores / 12 threads)
GPU: NVIDIA GeForce RTX 4060   DriverVersion 32.0.16.1047
     VideoMode 1920x1080 x 32-bit
```

**Video sink implications:**
- `d3d11videosink` — available on Win10 1903+; build 19045 qualifies. Best default here.
- NVIDIA RTX 4060 supports NVDEC H.264/HEVC → `nvh264dec` / `nvcodec` plugin is a viable
  hardware-decode path (GStreamer `nvcodec` plugin, ships in gst-plugins-bad).
- `glimagesink` works but adds a copy; `d3d11videosink` is the recommended Windows sink.
- Only one display, 1080p — no multi-monitor placement concerns.

## 8. Disk space

```
Drive  UsedGB  FreeGB
C:     422.5   53.7
```

Only one fixed drive. **53.7 GB free.** A full MSYS2 + GStreamer + toolchain install is
roughly 5–10 GB, so there is room, but it is not abundant — worth watching.

---

## 9. Repository state (context)

```
C:\Users\pc\Desktop\airplay
├── CLAUDE.md, README.md, .gitignore
├── docs/            (prompt-original.md, research/)
├── patches/         (empty)
├── scripts/         (empty)
└── third_party/UxPlay/   (git submodule)
```

UxPlay pinned at `a3c19cbc7fcc870d74a0960bc97817a2569b4808` (2026-08-09,
"Merge pull request #544 from JerryNee/agent/p2p-awdl-discovery"). No tag reachable
(`git describe --tags` → "No names found"), so this is a raw master pin, not a release.

`third_party/UxPlay/CMakeLists.txt` requires `PkgConfig` (line 31) and optionally probes
`dbus-1>=1.4.12` (line 32) and `X11` (line 17) on non-Windows paths.

---

## 10. Gap list — what must be set up

Verified-missing prerequisites, in dependency order:

1. **MSYS2** — nothing exists; a fresh install is required (UCRT64 env recommended).
2. **Toolchain inside MSYS2** — `gcc`, `cmake`, `ninja`, `pkgconf`, `make`.
3. **GStreamer** — core + `plugins-base` + `plugins-good` + `plugins-bad` (for `d3d11`/`nvcodec`)
   + `plugins-libav`.
4. **OpenSSL ≥ 3.0** and **libplist** — UxPlay's crypto/plist dependencies.
5. **mDNS responder** — Apple Bonjour SDK for Windows, or an alternative; plus resolving the
   existing UDP/5353 contention with `svchost` and Spotify.
6. **Firewall rule** for the built `uxplay.exe` (TCP 7000/7100 + UDP data ports) — requires elevation.

Already satisfied and needing no action: Git, Python, Visual Studio + Windows SDK (useful as a
fallback MSVC path), the Private network profile, and the generic mDNS 5353 allow rules.
