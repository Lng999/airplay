# GStreamer + MSYS2 on Windows 10 for a UxPlay-based AirPlay receiver

Research notes for the `airplay` project. Target: build UxPlay (GStreamer backend) under MSYS2
on Windows 10 x86_64, then embed/wrap it in a native Win32 GUI.

**Status legend**
- ✅ **Verified** — confirmed against a primary source (URL given) or against the pinned UxPlay
  source tree in `third_party/UxPlay` (`file:line` given).
- ⚠️ **Unverified / inferred** — reasoning or secondary source only; must be tested on real hardware.
- 🧪 **MANUAL VERIFICATION REQUIRED** — needs a real iPhone + real GPU to confirm.

**Pinned upstream:** `third_party/UxPlay` @ `a3c19cbc7fcc870d74a0960bc97817a2569b4808`
(2026-08-09), which is **UxPlay 1.74 (Experimental)** — `third_party/UxPlay/README.md:1`.

---

## 1. MSYS2 installation without manual clicking, and which environment to use

### 1.1 Silent install of the official installer ✅

MSYS2's own installer documentation gives these CLI examples verbatim
(<https://www.msys2.org/docs/installer/>):

> **CLI Usage Examples**
> The GUI installer utilizes the Qt Installer Framework which also offers CLI options for automation.
>
> Installing the GUI installer via the CLI to `C:\msys64`:
> ```
> .\msys2-x86_64-latest.exe in --confirm-command --accept-messages --root C:/msys64
> ```
>
> Uninstalling an existing installation in `C:\msys64` via the CLI:
> ```
> C:\msys64\uninstall.exe pr --confirm-command
> ```
>
> Installing the self extracting archive to `C:\msys64`:
> ```
> .\msys2-base-x86_64-latest.sfx.exe -y -oC:\
> ```

Notes:
- `in` is the Qt Installer Framework `install` verb. The `--root` value uses **forward slashes**
  in the documented example (`C:/msys64`).
- The installer is signed; SHA256 and GPG signature are obtainable by appending `.sha256` / `.sig`
  to the download URL. Signing key fingerprint `0EBF 782C 5D53 F7E5 FB02 A667 46BD 761F 7A49 B0EC`
  (<https://www.msys2.org/docs/installer/>).
- Latest installer releases: <https://github.com/msys2/msys2-installer/releases>.

### 1.2 winget ✅ (with caveats)

`MSYS2.MSYS2` exists in the community winget repository. Manifest directory:
<https://github.com/microsoft/winget-pkgs/tree/master/manifests/m/MSYS2/MSYS2>
(version directories present up to `20260611` as of this research).

From `manifests/m/MSYS2/MSYS2/20250830/MSYS2.MSYS2.installer.yaml`:

```yaml
PackageIdentifier: MSYS2.MSYS2
InstallerType: exe
InstallerSwitches:
  Silent: install --confirm-command --root "C:\msys64"
  SilentWithProgress: install --confirm-command --root "C:\msys64"
  InstallLocation: --root "<INSTALLPATH>"
UpgradeBehavior: deny
RequireExplicitUpgrade: true
Installers:
- Architecture: x64
  Scope: user       # default install is per-user
- Architecture: x64
  Scope: machine
  InstallerSwitches:
    Custom: AllUsers=true
  ElevationRequirement: elevationRequired
```

So `winget install --id MSYS2.MSYS2 --silent` works and lands in `C:\msys64` by default.

⚠️ Caveats: `UpgradeBehavior: deny` + `RequireExplicitUpgrade: true` mean winget will **not**
auto-upgrade MSYS2 — updates still go through `pacman -Syu`. The MSYS2 site itself does **not**
document winget (nothing about it on <https://www.msys2.org/docs/installer/>), so the
installer-with-flags route is the more "official" one.

**Recommendation for `scripts/`:** use the official installer with the documented flags
(reproducible, pinnable version, verifiable checksum); offer `winget` as a convenience fallback.

After install, MSYS2 must be updated at least once:

```
C:\msys64\usr\bin\bash.exe -lc "pacman -Syuu --noconfirm"
```

⚠️ May need to be run twice — the first `-Syu` can update the MSYS2 runtime itself and force the
shell to close. See <https://www.msys2.org/docs/updating/>.

### 1.3 UCRT64 vs MINGW64 — use **UCRT64** ✅

MSYS2 docs (<https://www.msys2.org/docs/environments/>) state plainly:

> *"If you are unsure, go with **UCRT64**."*

| Env | Prefix path | Package prefix | C library |
|---|---|---|---|
| UCRT64 | `/ucrt64` → `C:\msys64\ucrt64` | `mingw-w64-ucrt-x86_64-` | UCRT |
| MINGW64 | `/mingw64` → `C:\msys64\mingw64` | `mingw-w64-x86_64-` | MSVCRT |
| CLANGARM64 | `/clangarm64` | `mingw-w64-clang-aarch64-` | UCRT |

MSYS2 additionally announced **"Deprecating the MINGW64 environment"** on **2026-03-15**
(<https://www.msys2.org/news/>): no new packages are added to MINGW64 and existing leaf packages may
be removed if issues arise; users should migrate to UCRT64 or CLANG64.

UxPlay's own README agrees (`third_party/UxPlay/README.md:1019-1027`):

> *"NEW: On Intel x84-64 systems, MSYS2 now recommends using the newer UCRT64 terminal environment
> (which uses the newer Microsoft UCRT "Universal C RunTime Library", included as part of the
> Windows OS since Windows 10) rather than the MINGW64 terminal environment (which uses the older
> Microsoft MSVCRT C library, which has "legacy" status, but is available on all Windows systems)."*

**Decision: UCRT64.** UCRT ships in-box on Windows 10, our only target. MINGW64 would only matter for
Windows 7/8, which we do not support. (ARM64 → `CLANGARM64` + `mingw-w64-clang-aarch64-*`,
`README.md:1029-1032`.)

---

## 2. Exact pacman package names (UCRT64) ✅

All names below were checked on packages.msys2.org. Versions are as observed during this research
(2026-08) and will drift.

| Purpose | Package | Version seen | Source |
|---|---|---|---|
| Full toolchain (group, 13 pkgs) | `mingw-w64-ucrt-x86_64-toolchain` | group | <https://packages.msys2.org/group/mingw-w64-ucrt-x86_64-toolchain> |
| C/C++ compiler only | `mingw-w64-ucrt-x86_64-gcc` | 16.2.0-3 | <https://packages.msys2.org/package/mingw-w64-ucrt-x86_64-gcc?repo=ucrt64> |
| CMake | `mingw-w64-ucrt-x86_64-cmake` | 4.4.2-2 | <https://packages.msys2.org/package/mingw-w64-ucrt-x86_64-cmake?repo=ucrt64> |
| Ninja | `mingw-w64-ucrt-x86_64-ninja` | 1.13.2-1 | <https://packages.msys2.org/package/mingw-w64-ucrt-x86_64-ninja?repo=ucrt64> |
| make (member of toolchain group) | `mingw-w64-ucrt-x86_64-make` | — | group page above |
| pkg-config implementation | `mingw-w64-ucrt-x86_64-pkgconf` | 1~3.0.5-1 | <https://packages.msys2.org/base/mingw-w64-pkgconf> |
| OpenSSL | `mingw-w64-ucrt-x86_64-openssl` | 3.6.3-1 | <https://packages.msys2.org/package/mingw-w64-ucrt-x86_64-openssl?repo=ucrt64> |
| libplist | `mingw-w64-ucrt-x86_64-libplist` | 2.7.0-4 | <https://packages.msys2.org/base/mingw-w64-libplist> |
| GStreamer core | `mingw-w64-ucrt-x86_64-gstreamer` | 1.28.6-1 | <https://packages.msys2.org/base/mingw-w64-gstreamer> |
| gst-plugins-base | `mingw-w64-ucrt-x86_64-gst-plugins-base` | 1.28.6-1 | <https://packages.msys2.org/base/mingw-w64-gst-plugins-base> |
| gst-plugins-good | `mingw-w64-ucrt-x86_64-gst-plugins-good` | 1.28.6-1 | <https://packages.msys2.org/base/mingw-w64-gst-plugins-good> |
| gst-plugins-bad | `mingw-w64-ucrt-x86_64-gst-plugins-bad` | 1.28.6-2 | <https://packages.msys2.org/base/mingw-w64-gst-plugins-bad> |
| gst-plugins-bad libs (auto dep) | `mingw-w64-ucrt-x86_64-gst-plugins-bad-libs` | 1.28.6-2 | same page |
| gst-plugins-ugly *(optional)* | `mingw-w64-ucrt-x86_64-gst-plugins-ugly` | 1.28.6-1 | <https://packages.msys2.org/base/mingw-w64-gst-plugins-ugly> |
| gst-libav (FFmpeg decoders) | `mingw-w64-ucrt-x86_64-gst-libav` | 1.28.6-2 | <https://packages.msys2.org/base/mingw-w64-gst-libav> |
| gtksink / gtkglsink *(optional)* | `mingw-w64-ucrt-x86_64-gst-plugin-gtk` | 1.28.6-1 | <https://packages.msys2.org/base/mingw-w64-gst-plugins-good> |
| GTK3 *(only if gtksink used)* | `mingw-w64-ucrt-x86_64-gtk3` | 3.24.52-1 | <https://packages.msys2.org/package/mingw-w64-ucrt-x86_64-gtk3?repo=ucrt64> |
| DLL dependency walker | `mingw-w64-ucrt-x86_64-ntldd` | r19.7fb9365-4 | <https://packages.msys2.org/package/mingw-w64-ucrt-x86_64-ntldd?repo=ucrt64> |
| git (MSYS2-native, no prefix) | `git` | — | `README.md:1044` (`pacman -S git`) |

Notes:
- `mingw-w64-ucrt-x86_64-toolchain` is a **group**, not a package. `pacman -S <group>` prompts for
  member selection; `--noconfirm` selects all 13 members (including `gdb` and `gdb-multiarch`,
  hundreds of MB). For a lean scripted install prefer `gcc` + `pkgconf` + `make` explicitly.
- `pkgconf` is already a member of the toolchain group.
- UxPlay's README says *"openssl is already installed with MSYS2"* (`README.md:1043`) — that refers
  to the MSYS2-env OpenSSL. The **UCRT64** OpenSSL is pulled in as a dependency anyway, but list it
  explicitly to be safe.
- `gst-plugins-ugly` is **not** required by UxPlay for mirroring — the README lists only `libav`,
  `plugins-good`, `plugins-bad` (`README.md:1080-1085`). Include it only if you later want e.g.
  `x264enc`.
- `gst-plugin-gtk` (which provides `gtksink`/`gtkglsink`) is split out of `gst-plugins-good` in
  MSYS2 and is a **separate package**. Only needed if we ever choose `gtksink` over `d3d11videosink`.

### Full install one-liner (UCRT64 shell)

```bash
pacman -S --needed --noconfirm \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-openssl \
  mingw-w64-ucrt-x86_64-libplist \
  mingw-w64-ucrt-x86_64-gstreamer \
  mingw-w64-ucrt-x86_64-gst-plugins-base \
  mingw-w64-ucrt-x86_64-gst-plugins-good \
  mingw-w64-ucrt-x86_64-gst-plugins-bad \
  mingw-w64-ucrt-x86_64-gst-libav \
  mingw-w64-ucrt-x86_64-ntldd \
  git
```

Non-interactive from `cmd.exe` / PowerShell:

```
C:\msys64\ucrt64.exe -c "pacman -S --needed --noconfirm ..."
```

⚠️ `MSYSTEM` must be `UCRT64` **before** the login shell starts so `/ucrt64/bin` is on PATH. The
`ucrt64.exe` launcher does this; the alternative is `set MSYSTEM=UCRT64` then
`C:\msys64\usr\bin\bash.exe -lc "..."`. 🧪 Smoke-test this when writing `scripts/`.

### Build

```bash
cd third_party/UxPlay && mkdir -p build && cd build
cmake -G Ninja ..
ninja
```

Per `README.md:1055-1070`. Plain `cmake ..` already defaults to Ninja in MSYS2/UCRT64 (the issue #454
transcript shows `-- Building for: Ninja`).

Install into the prefix (there is no `make install` on Windows, `README.md:1066-1074`):

```bash
cmake --install . --prefix /ucrt64
```

---

## 3. GStreamer elements UxPlay actually uses, and which package owns each

### 3.1 The pipelines UxPlay builds ✅

**Video (mirroring)** — `renderers/video_renderer.c:360-385`:

```
appsrc name=video_source ! queue ! <parser> ! <decoder> ! <flip?> ! <converter> ! videoscale ! <videosink> name=<...> [options] sync=true|false
```

Defaults (`uxplay.cpp:107,112,122-124`):
- `videosink = "autovideosink"`
- `audiosink = "autoaudiosink"`
- `video_parser = "h264parse"` (rewritten to `h265parse` for the HEVC/4K renderer,
  `video_renderer.c:395-405`)
- `video_decoder = "decodebin"`
- `video_converter = "videoconvert"`

`-avdec` forces `video_decoder = "avdec_h264"` (`uxplay.cpp:1429-1433`).

Real observed pipeline, from UxPlay issue #466 (<https://github.com/FDH2/UxPlay/issues/466>):

```
appsrc name=video_source ! queue ! h264parse ! decodebin ! videoconvert ! video/x-raw,colorimetry=sRGB,format=RGB ! videoconvert ! videoscale ! <sink> sync=true
```

There is also a JPEG cover-art renderer `appsrc ! jpegdec ! … imagefreeze allow-replace=TRUE !
textoverlay name=metadata_overlay ! <sink>` (`video_renderer.c:361-372`) and an HLS renderer using
`playbin3` (`video_renderer.c:317-330`).

**Audio** — `renderers/audio_renderer.c:147-170`:

```
appsrc name=audio_source ! queue ! [avdec_aac | avdec_alac | (raw PCM)] ! audioconvert ! audioresample quality=10 ! volume name=volume ! level ! <audiosink> sync=true|false
```

UxPlay probes for `avdec_aac` / `avdec_alac` at startup (`audio_renderer.c:141-142`) and logs
*"GStreamer libav plugin feature avdec_aac is missing, cannot decode AAC audio"* if absent
(`audio_renderer.c:264`). **`gst-libav` is therefore mandatory for audio, not optional.**

**Mux / record path** (`renderers/mux_renderer.c:120-131`) additionally uses `h265parse` and
`aacparse`.

**RTP forwarding paths** use `rtph264pay` (`video_renderer.c:372`) and `rtpL16pay`
(`audio_renderer.c:191-194`) — only with `-vrtp` / `-artp`.

### 3.2 Element → plugin → MSYS2 package map ✅

| Element | GStreamer plugin | Package (per docs) | MSYS2 package |
|---|---|---|---|
| `appsrc` | `app` | Base | `…-gst-plugins-base` |
| `queue`, `fakesink`, `tee`, `capsfilter` | `coreelements` | Core | `…-gstreamer` |
| `videoconvert`, `videoscale` | `videoconvertscale` | Base | `…-gst-plugins-base` |
| `audioconvert`, `audioresample`, `volume` | `audioconvert` / `audioresample` / `volume` | Base | `…-gst-plugins-base` |
| `textoverlay` | `pango` | Base | `…-gst-plugins-base` |
| `playbin`, `playbin3`, `decodebin` | `playback` | Base | `…-gst-plugins-base` |
| `h264parse`, `h265parse` | `videoparsersbad` | **Bad** — <https://gstreamer.freedesktop.org/documentation/videoparsersbad/h264parse.html> | `…-gst-plugins-bad` |
| `aacparse` | `audioparsers` | **Good** — <https://gstreamer.freedesktop.org/documentation/audioparsers/aacparse.html> | `…-gst-plugins-good` |
| `avdec_h264`, `avdec_aac`, `avdec_alac` | `libav` | **FFMPEG** — <https://gstreamer.freedesktop.org/documentation/libav/avdec_h264.html>, <https://gstreamer.freedesktop.org/documentation/libav/avdec_aac.html> | `…-gst-libav` |
| `autovideosink`, `autoaudiosink` | `autodetect` | **Good** — <https://gstreamer.freedesktop.org/documentation/autodetect/autovideosink.html> | `…-gst-plugins-good` |
| `level` | `level` | Good | `…-gst-plugins-good` |
| `jpegdec`, `imagefreeze` | `jpeg` / `imagefreeze` | Good | `…-gst-plugins-good` |
| `rtph264pay`, `rtpL16pay` | `rtp` | Good | `…-gst-plugins-good` |
| `d3d11videosink`, `d3d11h264dec`, `d3d11convert` | `d3d11` | **Bad** — <https://gstreamer.freedesktop.org/documentation/d3d11/d3d11videosink.html> | `…-gst-plugins-bad` |
| `d3d12videosink`, `d3d12h264dec` | `d3d12` | **Bad** — <https://gstreamer.freedesktop.org/documentation/d3d12/d3d12videosink.html> | `…-gst-plugins-bad` |
| `d3dvideosink` (Direct3D 9, legacy) | `d3d` | **Bad** — <https://gstreamer.freedesktop.org/documentation/d3d/index.html> | `…-gst-plugins-bad` |
| `wasapisink` | `wasapi` | **Bad** — <https://gstreamer.freedesktop.org/documentation/wasapi/wasapisink.html> | `…-gst-plugins-bad` |
| `wasapi2sink` | `wasapi2` | **Bad** — <https://gstreamer.freedesktop.org/documentation/wasapi2/wasapi2sink.html> | `…-gst-plugins-bad` |
| `directsoundsink` | `directsound` | Good, **deprecated** — see note | `…-gst-plugins-good` ⚠️ |
| `glimagesink` | `opengl` | Base | `…-gst-plugins-base` |
| `gtksink`, `gtkglsink` | `gtk` | Good (split out in MSYS2) | `…-gst-plugin-gtk` (+ `…-gtk3`) |

⚠️ **Note on `directsoundsink`:** the current GStreamer plugin index lists only `directsoundsrc`
(<https://gstreamer.freedesktop.org/documentation/plugins_doc.html>), and the platform tutorial marks
`directsoundsink` as **deprecated**
(<https://gstreamer.freedesktop.org/documentation/tutorials/basic/platform-specific-elements.html>).
The element still exists in gst-plugins-good master
(<https://github.com/GStreamer/gst-plugins-good/blob/master/sys/directsound/gstdirectsoundsink.c>)
but its rendered doc page 404s. **Do not rely on it.** Verify at runtime with
`gst-inspect-1.0 directsoundsink`.

### 3.3 Recommended sinks on Windows ✅

From the official platform tutorial
(<https://gstreamer.freedesktop.org/documentation/tutorials/basic/platform-specific-elements.html>):

- **Video:** *"This video sink is based on Direct3D11 and is the recommended element on Windows."*
  (`d3d11videosink`). `glimagesink` is recommended *"on most platforms except for Windows (On
  Windows, `d3d11videosink` is recommended)"*. `d3dvideosink` (D3D9) *"is not recommended for
  applications targetting Windows 8 or more recent."* `dshowvideosink` is deprecated. The page
  predates `d3d12videosink` and does not mention it.
- **Audio:** *"Those elements are the default audio sink elements on Windows, based on WASAPI …
  `wasapi2sink` is default for Windows 8 or more recent. Otherwise `wasapisink` will be default
  audio sink element."*

UxPlay's README instead recommends `d3d12videosink`, purely for the Alt-Enter fullscreen toggle
(`README.md:1146-1155`):

> *"It is recommended that Windows users add a line `-vs d3d12videosink` in their UxPlay startup
> file, to get this toggle option (autovideosink will usually select d3d12videosink, but will not
> provide the toggle option)."*

and warns (`README.md:1141-1145`):

> *"There have been reports of segfaults of the newer d3d12 videodecoder on certain older Nvidia
> cards when the image resolution changes, e.g., when the iOS client is rotated between portrait and
> landcape modes: this was a GStreamer issue that is apparently now fixed (a workaround is to use
> d3d11)."*

UxPlay auto-appends fullscreen options for both sinks (`uxplay.cpp:3092-3108`):

```
d3d11videosink → " fullscreen-toggle-mode=GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_ALT_ENTER "
d3d12videosink → " fullscreen-on-alt-enter=TRUE "
```

Audio options (`README.md:1124-1140`): `-as wasapisink` or `-as directsoundsink`; device selection via
`uxplay -as 'wasapisink device="<guid>"'` where the GUID comes from `gst-device-monitor-1.0 Audio`.

**Recommendation for this project: `d3d11videosink` + `wasapi2sink`.**
Rationale:
1. `d3d11videosink` is what the GStreamer project itself calls "recommended on Windows".
2. It implements `GstVideoOverlay` **including `render-rectangle`**, which `d3d12videosink` may
   ignore (see §5.3) — and we need overlay embedding for the Win32 GUI.
3. It avoids the D3D12 resolution-change segfault class (UxPlay issue #414).
4. We do not need the Alt-Enter toggle, because our GUI will own the window.
5. `wasapi2sink` is the documented Windows-8+ default; `wasapisink` is the fallback.
   🧪 Confirm both exist in MSYS2 1.28.6 with `gst-inspect-1.0`.

For **decoding**, keep the `decodebin` default: `d3d11h264dec` has rank **primary+1**
(<https://gstreamer.freedesktop.org/documentation/d3d11/d3d11h264dec.html>), so `decodebin` prefers
the DXVA hardware decoder over `avdec_h264` automatically. Keep `gst-libav` installed regardless —
`avdec_aac`/`avdec_alac` are mandatory, and `-avdec` is the software fallback.

### 3.4 Verifying elements at runtime

In a UCRT64 shell (or with `C:\msys64\ucrt64\bin` on PATH):

```bash
gst-inspect-1.0 --version                 # confirm 1.28.x
gst-inspect-1.0 | tail -3                 # total plugin/feature count; low ⇒ plugin path broken
for e in appsrc queue h264parse h265parse decodebin avdec_h264 avdec_aac avdec_alac \
         aacparse videoconvert videoscale audioconvert audioresample volume level \
         autovideosink autoaudiosink d3d11videosink d3d12videosink d3d11h264dec \
         wasapi2sink wasapisink directsoundsink glimagesink jpegdec imagefreeze textoverlay; do
  gst-inspect-1.0 "$e" >/dev/null 2>&1 && echo "OK   $e" || echo "MISS $e"
done
gst-device-monitor-1.0 Audio              # audio device GUIDs for `wasapisink device=`
```

`gst-inspect-1.0` exits non-zero and prints `No such element or plugin '<name>'` when an element is
missing. `gst-inspect-1.0 <element>` also prints the owning **Plugin** and **Filename** — the fastest
way to learn which DLL must be shipped (§4.3).

Where the tools live ✅:
- `gst-inspect-1.0.exe`, `gst-launch-1.0.exe`, `gst-stats-1.0.exe`, `gst-typefind-1.0.exe`, and
  `libexec/gstreamer-1.0/gst-plugin-scanner.exe` → package `…-gstreamer`
  (<https://packages.msys2.org/package/mingw-w64-ucrt-x86_64-gstreamer?repo=ucrt64>)
- `gst-device-monitor-1.0.exe`, `gst-discoverer-1.0.exe`, `gst-play-1.0.exe` →
  package `…-gst-plugins-base`
  (<https://packages.msys2.org/package/mingw-w64-ucrt-x86_64-gst-plugins-base?repo=ucrt64>)

---

## 4. Runtime PATH / DLL deployment

### 4.1 The minimum: put the environment's `bin` on PATH ✅

UxPlay README (`README.md:1157-1162`):

> *"The executable uxplay.exe can also be run without the MSYS2 environment, in the Windows Terminal,
> using the full path, e.g. `C:\msys64\ucrt64\bin\uxplay`. (This makes Windows search the
> executable's directory for needed MSYS2 DLL libraries). Use of the full path can be avoided by
> permanently adding `C:\msys64\ucrt64\bin` … to your user path"*

The failure mode is documented in UxPlay PR #543 (<https://github.com/FDH2/UxPlay/pull/543>):

> *"`The code execution cannot proceed because libglib-2.0-0.dll was not found` when running
> `uxplay.exe` outside the MSYS2 shell — caused by the environment's `bin` directory (where all
> runtime DLLs live) not being on `PATH`."*

**The directory is `C:\msys64\ucrt64\bin`.** Everything (glib, gstreamer, openssl, libplist,
libwinpthread-1, libstdc++-6, libgcc_s_seh-1 …) sits flat in there.

### 4.2 Plugin paths and the registry cache ✅

Prefix layout:

```
C:\msys64\ucrt64\bin\                                  *.dll, gst-*.exe, uxplay.exe
C:\msys64\ucrt64\lib\gstreamer-1.0\                    libgst*.dll  (the plugins)
C:\msys64\ucrt64\libexec\gstreamer-1.0\gst-plugin-scanner.exe
```

Environment variables (<https://gstreamer.freedesktop.org/documentation/gstreamer/running.html>,
<https://gstreamer.freedesktop.org/documentation/gstreamer/gstregistry.html>):

- `GST_PLUGIN_PATH` — *"can be set to a colon-separated list of paths (or a **semicolon-separated
  list on Windows**)"*. Searched **before** system paths; the override hook.
- `GST_PLUGIN_SYSTEM_PATH` — system plugin dirs. *"Setting this variable to an empty string will
  cause GStreamer not to scan any system paths at all for plug-ins."* — exactly what a hermetic
  redistributable bundle wants.
- `GST_PLUGIN_SCANNER` — path to `gst-plugin-scanner.exe`. ⚠️ Not documented on `running.html`, but
  it is the standard variable every relocatable GStreamer bundle sets; set it explicitly if the
  scanner is not at `<prefix>/libexec/gstreamer-1.0/`.
- `GST_REGISTRY` — *"Set this environment variable to make GStreamer use a different file for the
  plugin cache / registry than the default one."*
- `GST_REGISTRY_UPDATE=no` — *"prevent GStreamer from updating the plugin registry."*
- `GST_REGISTRY_FORK=no` — *"prevent GStreamer from forking on startup in order to update the plugin
  registry."*
- `GST_DEBUG` — *"a list of debug options, which cause GStreamer to print out different types of
  debugging information to stderr"*, levels 0–9. Use `GST_DEBUG=*:4` when diagnosing.

Plugin search order (gstregistry docs): `--gst-plugin-path` → `GST_PLUGIN_PATH` →
`GST_PLUGIN_SYSTEM_PATH` → defaults `$XDG_DATA_HOME/gstreamer-1.0/plugins/` and
`$prefix/lib/gstreamer-1.0/`.

**Registry cache gotcha ⚠️** — the cache file is
`$XDG_CACHE_HOME/gstreamer-1.0/registry-<arch>.bin` (or whatever `$GST_REGISTRY` names). On Windows,
GLib's `g_get_user_cache_dir()` returns *"the directory that serves as a common repository for
temporary Internet files"* when `XDG_CACHE_HOME` is unset
(<https://docs.gtk.org/glib/func.get_user_cache_dir.html>) — i.e. a per-user location under
`%LOCALAPPDATA%`, **shared with every other GStreamer installation on the machine.**

Consequences to plan for:
1. A stale registry left by a *different* GStreamer (official MSVC installer, OBS, Shotcut, an older
   MSYS2 prefix) makes elements "disappear" or crash on load. Fix: delete
   `%LOCALAPPDATA%\gstreamer-1.0\registry.*.bin`, or point `GST_REGISTRY` at our own file.
2. For our shipped app, **always** set our own `GST_REGISTRY` so we never fight another install.
3. Set `GST_REGISTRY_UPDATE=no` only after the registry is known-good (e.g. generated at install
   time); otherwise newly added plugins stay invisible.
4. Do **not** set `GST_PLUGIN_PATH` machine-wide — it leaks into every other GStreamer app. Set it in
   the child process environment only.

### 4.3 Building a self-contained folder

1. Compute the transitive DLL closure with `ntldd`
   (<https://packages.msys2.org/package/mingw-w64-ucrt-x86_64-ntldd?repo=ucrt64>):
   ```bash
   ntldd -R build/uxplay.exe | grep -i ucrt64 | awk '{print $3}' | sort -u
   ```
   `ldd` also works inside MSYS2, but `ntldd` reads PE imports properly. Repeat for **every** plugin
   DLL you copy — plugins pull their own deps (`libgstlibav.dll` → libav*, etc.).
2. Copy the needed `lib/gstreamer-1.0/libgst*.dll`. Starting set for our pipelines:
   `coreelements`, `app`, `playback`, `typefindfunctions`, `videoconvertscale`, `audioconvert`,
   `audioresample`, `volume`, `videoparsersbad`, `audioparsers`, `libav`, `autodetect`, `level`,
   `d3d11`, `d3d12`, `wasapi2`, `jpeg`, `imagefreeze`, `pango`, `rtp`.
   ⚠️ Exact DLL basenames must be read from `gst-inspect-1.0 <element> | grep Filename` — several
   elements share a DLL (`videoconvert` and `videoscale` both live in `libgstvideoconvertscale.dll`)
   and some names differ from the plugin name.
3. Copy `libexec/gstreamer-1.0/gst-plugin-scanner.exe` and set `GST_PLUGIN_SCANNER`.
4. At launch, set for the child process only:
   ```
   PATH                   = <bundle>\bin;%PATH%
   GST_PLUGIN_SYSTEM_PATH = (empty string)
   GST_PLUGIN_PATH        = <bundle>\lib\gstreamer-1.0
   GST_PLUGIN_SCANNER     = <bundle>\libexec\gstreamer-1.0\gst-plugin-scanner.exe
   GST_REGISTRY           = %LOCALAPPDATA%\<ourapp>\gst-registry.bin
   ```
5. ⚠️ The official *"Deploying your application on Windows"* page
   (<https://gstreamer.freedesktop.org/documentation/deploying/windows.html>) covers only the MSVC
   MSI runtime (`msiexec /passive INSTALLDIR=…`) and WiX merge modules; it does **not** document the
   MSYS2 hand-copy route. It does state the general problem: *"GStreamer is modular in nature.
   Plug-ins are loaded depending on the media that is being played, so, if you do not know in advance
   what files you are going to play, you do not know which DLLs you need to deploy."* For us the
   media is always H.264/HEVC + AAC/ALAC, so the set is closed and enumerable.
6. ⚠️ **Licensing:** UxPlay is GPLv3 and `gst-libav` bundles FFmpeg. Redistribution obligations
   apply. Out of scope for this document but must not be ignored when shipping.

🧪 The entire bundling procedure is untested — **MANUAL VERIFICATION REQUIRED**.

---

## 5. Embedding GStreamer video in a Win32 HWND

### 5.1 UxPlay does **not** do this today ✅

Grepping the pinned tree for `VideoOverlay`, `set_window_handle`, `render_rectangle` across
`renderers/*.c`, `renderers/*.h` and `uxplay.cpp` returns **nothing**. UxPlay always lets the
videosink create its own top-level window. Embedding into our GUI therefore requires one of:

- **(a)** a patch in `patches/` that fetches the sink from the pipeline and calls
  `gst_video_overlay_set_window_handle()` with an HWND we pass in (cleanest);
- **(b)** running `uxplay.exe` as a child process and re-parenting its window with `SetParent()`
  (hacky, and see issue #427 about D3D window capture);
- **(c)** linking `librairplay` directly and writing our own renderer (most work, most control).

The pipelines are built with `gst_parse_launch()` and the sink is given an explicit element name
(`<videosink>_<codec>`, `video_renderer.c:374-380`), so approach (a) is easy:
`gst_bin_get_by_name(GST_BIN(pipeline), "d3d11videosink_h264")`.

### 5.2 The GstVideoOverlay contract ✅

<https://gstreamer.freedesktop.org/documentation/video/gstvideooverlay.html>

```c
void     gst_video_overlay_set_window_handle    (GstVideoOverlay *overlay, guintptr handle);
gboolean gst_video_overlay_set_render_rectangle (GstVideoOverlay *overlay,
                                                 gint x, gint y, gint width, gint height);
void     gst_video_overlay_expose               (GstVideoOverlay *overlay);
void     gst_video_overlay_handle_events        (GstVideoOverlay *overlay, gboolean handle_events);
```

Key points, quoted from the docs:
- *"a GstMessage is posted on the bus to inform the application that it should set the Window
  identifier immediately"* — the `prepare-window-handle` message. It must be handled in a **sync**
  bus handler (`gst_bus_set_sync_handler`) because *"the video sink will need an answer right then."*
- Threading: *"It is generally not advisable to call any GUI toolkit functions or window system
  functions from the streaming thread"* — so the sync handler should do the bare minimum: call
  `set_window_handle` with an HWND already created on the UI thread, nothing else.
- Passing handle `0` tells the sink to create its own internal window.
- `set_render_rectangle`: *"When unset or unsupported, video fills 100% of the overlay window."*
  Pass `-1` for width/height to unset. *"one should call `gst_video_overlay_expose` to force a
  redraw"* afterwards.
- `gst_video_overlay_expose` redraws the last frame even while paused — call it on `WM_PAINT` /
  after `WM_SIZE`.
- `gst_video_overlay_handle_events(overlay, FALSE)` stops the sink from consuming window-system
  events (they are otherwise forwarded upstream as navigation events).

On Windows the `guintptr handle` is an **HWND** cast to `guintptr`.

### 5.3 Sink support matrix ✅

| Sink | GstVideoOverlay | `force-aspect-ratio` | `render-rectangle` | Notes |
|---|---|---|---|---|
| `d3d11videosink` | ✅ yes | ✅ yes, default `true` | ✅ yes — documented as a `GstValueArray` property, plus the overlay API | also implements `GstNavigation` |
| `d3d12videosink` | ✅ yes | ✅ yes, default `true` | ⚠️ **conditional** — the docs say that with `direct-swapchain` enabled *"`GstVideoOverlay::set_render_rectangle` will be ignored"*; no standalone `render-rectangle` property documented | also `GstNavigation`, `GstColorBalance`; has `fullscreen`, `fullscreen-on-alt-enter` |
| `d3dvideosink` (D3D9) | ⚠️ historically yes; the standalone doc page 404s, only the plugin index page exists | ⚠️ | ⚠️ | *"not recommended for applications targetting Windows 8 or more recent"* |
| `glimagesink` | ✅ yes | ✅ | ✅ | not recommended on Windows |

Sources: <https://gstreamer.freedesktop.org/documentation/d3d11/d3d11videosink.html>,
<https://gstreamer.freedesktop.org/documentation/d3d12/d3d12videosink.html>,
<https://gstreamer.freedesktop.org/documentation/d3d/index.html>,
<https://gstreamer.freedesktop.org/documentation/tutorials/basic/platform-specific-elements.html>

**Aspect ratio:** `d3d11videosink`'s `force-aspect-ratio` is documented as *"When enabled, scaling
will respect original aspect ratio"*, default **`true`**. Letterboxing inside our HWND is therefore
free — we do **not** need to compute a render rectangle just to preserve aspect ratio. Leave it at
the default and let the sink fill the client rect.

**This is the decisive argument for `d3d11videosink` over `d3d12videosink` in our GUI:**
`render-rectangle` support is unconditional on d3d11 and conditional on d3d12.

### 5.4 What `d3d11videosink` actually does with your HWND ✅ (source-read)

From GStreamer master,
`subprojects/gst-plugins-bad/sys/d3d11/gstd3d11window_win32.cpp`
(<https://github.com/GStreamer/gstreamer/blob/main/subprojects/gst-plugins-bad/sys/d3d11/gstd3d11window_win32.cpp>):

- line ~517: stores your HWND as `external_hwnd`.
- lines ~520–532: **subclasses your window** — the original WNDPROC is saved via
  `GetWindowLongPtrA(hwnd, GWLP_WNDPROC)` and replaced with
  `SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR) sub_class_proc)`. On teardown (line ~577) the
  original WNDPROC is restored with another `SetWindowLongPtrA`.
- lines ~1008–1013: creates an **internal child window**, sets its style to `WS_CHILD | WS_MAXIMIZE`,
  calls `SetParent(internal_hwnd, external_hwnd)`, then sizes it from
  `GetClientRect(external_hwnd, &rect)`.
- lines ~886–891: on resize it re-reads the external window's client rect.

Implications for our Win32 GUI ⚠️:
1. **Do not subclass the same HWND yourself.** Give the sink a dedicated child "video panel" HWND,
   never the main frame, so our WNDPROC chain and the sink's do not fight.
2. The sink tracks the external window's client rect itself, so a simple `WM_SIZE` handler that does
   nothing is often enough; call `gst_video_overlay_expose()` on `WM_PAINT`.
3. Mouse/keyboard events over the video area reach the sink's child window first. Call
   `gst_video_overlay_handle_events(overlay, FALSE)` if the GUI must own input.
4. **Set the pipeline to `GST_STATE_NULL` before destroying the HWND**, so the sink restores the
   original WNDPROC and detaches the child window. Destroying the HWND first is a crash / dangling
   WNDPROC risk.

🧪 All of §5.4 is source-read, not executed — **MANUAL VERIFICATION REQUIRED**.

---

## 6. Known UxPlay-on-Windows issues (GitHub)

All links under <https://github.com/FDH2/UxPlay>.

1. **#523 — "Replace DNS-SD dependency with built-in mDNS responder, and Add multi-platform CI build
   verification"** (closed, 2026-05-16) <https://github.com/FDH2/UxPlay/issues/523> — introduced the
   self-contained mDNSResponder in `lib/mdnsd`, removing the Apple Bonjour SDK requirement.

2. **#529 — "Comments on new self-contained minimal mDNSResponder (replaces Avahi/Bonjour)"**
   (open, 2026-06-21) <https://github.com/FDH2/UxPlay/issues/529> — the tracking/feedback issue.
   Body: *"Currently in the GitHub UxPlay master branch, (the future UxPlay-1.74 release) the new code
   is used by default, but the Apple dns-sd.h code (in lib/dns_sd) is also available by compiling with
   `cmake -DUSE_DNS_SD=1`."*

   ⚠️ **Correction to the task brief's flag name.** The opt-out flag is **`-DUSE_DNS_SD=1`**
   (use Bonjour instead). `-DUSE_MDNS=1` exists but is meaningful **only on Apple** — see
   `third_party/UxPlay/CMakeLists.txt:51`:
   ```cmake
   if (USE_DNS_SD OR ( APPLE AND NOT USE_MDNS ))
   ```
   On Windows the internal responder is the **default in 1.74**; no flag is needed.
   Version introduced: **1.74, 2026-06-21** — changelog line `README.md:2190`:
   *"1.74 2026-06-21 Optional minimal internal mDNSResponder to replace …"*.
   Also `README.md:989-990`: *"NEW: Uxplay >=1.74 now supplies its own self-contained mdns replacement
   for Bonjour, making step 1 unnecessary unless you choose to use Bonjour. To use Bonjour, compile
   with `cmake -DUSE_DNS_SD=1`."*

3. **#546 — "Verify new mdns implementation on Windows"** (closed, 2026-08-02)
   <https://github.com/FDH2/UxPlay/issues/546> — a user reports the new internal mDNS
   *"wasn't able to get it to work"* on native Windows, while *"I tried it in WSL and it seems to work
   in there. I am relatively confident that my firewall is setup correctly."* No root cause given.
   🧪 **This is the single biggest risk for our project** — the 1.74 internal responder is not
   confidently proven on native Windows 10. Fallback: build with `-DUSE_DNS_SD=1` + Bonjour SDK.
   UxPlay's own error string points at the same area (`lib/mdnsd/dnssd_mdnsd.c:353`):
   *"setup or start of self-contained mDNSResponder (lib/mdnsd) failed; check UDP port 5353 and
   multicast access"*. Port constant: `lib/mdnsd/mdnsd.c:30` `#define MDNS_PORT 5353`.

4. **#297 — "(mDNS port 5353 UDP issues on Windows????) UxPlay stops responding after a while"**
   (closed, 2024-05-10) <https://github.com/FDH2/UxPlay/issues/297> —
   *"the UxPlay device will disappear from available devices after some time. Only way to reconnect is
   to relaunch the app."* Classic symptom of the **Apple "Bonjour Service" (`mDNSResponder.exe`)
   competing for UDP 5353** with UxPlay's own responder. README (`README.md:1813`) documents stopping
   it: Win+R → `services.msc` → **Bonjour Service**.
   ⚠️ Bonjour ships with iTunes / iCloud / several Adobe products — very likely already present on a
   target PC.

5. **#543 — "docs(windows): document ARM64/CLANGARM64 build, add DLL and firewall troubleshooting"**
   (merged PR, 2026-07-29) <https://github.com/FDH2/UxPlay/pull/543> — the origin of the current
   firewall/DLL troubleshooting text. Two named failure modes: missing `libglib-2.0-0.dll` (PATH), and
   *"UxPlay starts cleanly but no AirPlay device ever appears on iOS — usually Windows Firewall
   silently blocking inbound mDNS/AirPlay traffic, since no rule exists by default and the automatic
   permission prompt doesn't always appear."*

6. **#414 — "Solved: Windows D3D12 gstreamer decoder segfault issue on certain older(?) GPU's (Nvidia):
   solved by forcing D3D11 uxplay options"** (closed, 2025-05-02)
   <https://github.com/FDH2/UxPlay/issues/414> — segfault immediately after
   *"Begin streaming to GStreamer video pipeline"* with an iPad; resolved by forcing D3D11.
   Reinforces the `d3d11videosink` choice.

7. **#506 — "*** ERROR: httpd error in select: 10038 An operation was attempted on something that is
   not a socket."** (closed, 2026-03-18) <https://github.com/FDH2/UxPlay/issues/506> — Windows-only
   Winsock bug triggered after a client disconnects; the app then cannot discover or accept new
   connections until restarted. Fixed in **1.73.6** per the changelog (`README.md:2193`:
   *"1.73.6 2026-03-22 Fix 'not a socket' message uxplay bug."*). Our pin (1.74) includes the fix.
   Relevant to the GUI design: exactly the class of bug that silently kills a long-running background
   receiver — plan a health check / auto-restart supervisor.

8. **#520 — "windows arm64 dnssd.dll loading issues (?)"** (closed "not planned", 2026-05-05)
   <https://github.com/FDH2/UxPlay/issues/520> — filed by the maintainer of the third-party
   prebuilt-Windows project <https://github.com/leapbtw/uxplay-windows>, which has working GitHub
   Actions build scripts for Windows that we can crib from.

9. **#427 — "OBS Window Capture results in black screen"** (closed, 2025-07-02)
   <https://github.com/FDH2/UxPlay/issues/427> — *"I can capture the screen and it works fine, but if
   I try capturing the window it results in a black screen."* ⚠️ Direct3D swapchain windows are not
   capturable by ordinary window capture. Relevant if our GUI ever needs to screenshot or record the
   video area — we would have to tap the pipeline (`tee` + `appsink`), not the HWND.

10. **#540 — "iOS 26/27 mirror window stays open (frozen last frame) after disconnect — typeless
    TEARDOWN doesn't reset the …"** (open, 2026-07-23) <https://github.com/FDH2/UxPlay/issues/540>
    and **#535 — "Mirroring negotiates and 'initializes' but client never opens data connection
    (iOS 27, combinedGetInfoWithControlSetup)"** (open, 2026-07-15)
    <https://github.com/FDH2/UxPlay/issues/535> — current iOS 26/27 protocol regressions, still open.
    #535 reports the control connection stays alive and `/feedback` heartbeats flow, but the client
    never dials the data port. 🧪 Must be tested against the exact iPhone/iOS version we target.

11. **#454 — "'not declared' ERROR during Windows 11 'ninja' command in MSYS2"** (closed, 2025-09-09)
    <https://github.com/FDH2/UxPlay/issues/454> —
    `'GetCurrentProcessID' was not declared in this scope; did you mean 'GetCurrentProcessId'`,
    a real typo fixed upstream. Historical, but a reminder that MSYS2 GCC bumps periodically break
    UxPlay's Win32 code (that report was GCC 15.2.0; MSYS2 now ships 16.2.0).

---

## 7. Windows firewall

### 7.1 Which ports ✅

From `third_party/UxPlay/README.md:1388-1397`:

> *"**-p** allows you to select the network ports used by UxPlay (these need to be opened if the
> server is behind a firewall). By itself, `-p` sets "legacy" ports **TCP 7100, 7000, 7001, UDP 6000,
> 6001, 7011**. `-p n` (e.g. `-p 35000`) sets TCP and UDP ports n, n+1, n+2. `-p n1,n2,n3`
> (comma-separated values) sets each port separately; `-p n1,n2` sets ports n1,n2,n2+1. `-p tcp n` or
> `-p udp n` sets just the TCP or UDP ports. Ports must be in the range [1024-65535].*
>
> *If the `-p` option is not used, the ports are chosen dynamically (randomly), which will not work if
> a firewall is running."*

Restated at `README.md:1949`: *"open UDP 7011,6001,6000 TCP 7100,7000,7001 and use `uxplay -p`"*.

Plus **UDP 5353** for mDNS (`lib/mdnsd/mdnsd.c:30`), multicast group `224.0.0.251` / `ff02::fb`.

**⇒ Always launch UxPlay with an explicit `-p` in a firewalled deployment** (either bare `-p` for the
legacy set, or a private range like `-p 35000`).

### 7.2 Program-scoped rule (simplest — what UxPlay recommends) ✅

`README.md:1104-1110`, elevated PowerShell:

```powershell
New-NetFirewallRule -DisplayName "UxPlay" -Direction Inbound `
    -Program "C:\path\to\uxplay.exe" -Action Allow `
    -Profile Private -Protocol Any
```

A program rule covers every port the process listens on, which also makes the random-port default
workable. Parameter semantics per
<https://learn.microsoft.com/en-us/powershell/module/netsecurity/new-netfirewallrule>
(cf. its EXAMPLE 3:
`New-NetFirewallRule -DisplayName "Allow Messenger" -Direction Inbound -Program "C:\Program Files (x86)\Messenger\msmsgs.exe" -RemoteAddress LocalSubnet -Action Allow`).

⚠️ `-Program` must be the **final** exe path. If we ship `uxplay.exe` inside our own bundle, the rule
must point at the bundled copy and must be recreated if the install path changes.

### 7.3 Port-scoped rules (explicit, pairs with `-p`)

Elevated PowerShell. Parameters (`-Protocol`, `-LocalPort`, `-Direction`, `-Action`, `-Profile`) per
the Microsoft Learn reference above; cf. its EXAMPLE 7,
`-Protocol TCP -LocalPort 12345,5000-5020`:

```powershell
# mDNS / service discovery
New-NetFirewallRule -DisplayName "UxPlay mDNS (UDP 5353)" `
    -Direction Inbound -Action Allow -Protocol UDP -LocalPort 5353 -Profile Private

# UxPlay legacy port set (matches `uxplay -p`)
New-NetFirewallRule -DisplayName "UxPlay TCP (legacy)" `
    -Direction Inbound -Action Allow -Protocol TCP -LocalPort 7000,7001,7100 -Profile Private

New-NetFirewallRule -DisplayName "UxPlay UDP (legacy)" `
    -Direction Inbound -Action Allow -Protocol UDP -LocalPort 6000,6001,7011 -Profile Private
```

Equivalent `netsh advfirewall` forms (elevated `cmd.exe`):

```bat
netsh advfirewall firewall add rule name="UxPlay mDNS (UDP 5353)" dir=in action=allow protocol=UDP localport=5353 profile=private
netsh advfirewall firewall add rule name="UxPlay TCP (legacy)"    dir=in action=allow protocol=TCP localport=7000,7001,7100 profile=private
netsh advfirewall firewall add rule name="UxPlay UDP (legacy)"    dir=in action=allow protocol=UDP localport=6000,6001,7011 profile=private
netsh advfirewall firewall add rule name="UxPlay (program)"       dir=in action=allow program="C:\path\to\uxplay.exe" enable=yes profile=private
```

Removal:

```powershell
Remove-NetFirewallRule -DisplayName "UxPlay*"
```

```bat
netsh advfirewall firewall delete rule name="UxPlay (program)"
```

⚠️ The `netsh` lines are the conventional equivalents; I did not find a Microsoft page spelling out
these exact commands, so treat their syntax as **unverified in detail**. The `New-NetFirewallRule`
forms are grounded in the Learn reference and in UxPlay's README.

### 7.4 Non-firewall discovery prerequisites ✅

`README.md:1112-1122`:

> *"your PC's network connection is on the **Private** network profile, not Public
> (`Get-NetConnectionProfile` in PowerShell shows this — Windows restricts discovery traffic more
> aggressively on Public networks); the iPhone/iPad and the PC are on the *same* Wi-Fi network (not
> one of them on cellular/VPN, and not a router/AP with "client/AP isolation" enabled on a guest
> network, which blocks devices from discovering each other even on the same SSID); and that no
> third-party antivirus/firewall is separately blocking the app."*

Checklist for our installer / first-run wizard:
- [ ] `Get-NetConnectionProfile` → `NetworkCategory` is `Private`
- [ ] Firewall rule(s) created (program rule at minimum)
- [ ] Apple **Bonjour Service** not holding UDP 5353
      (`Get-Service Bonjour`; `netstat -ano -p UDP | findstr :5353`)
- [ ] No VPN active; no AP/client isolation on the Wi-Fi
- [ ] UxPlay launched with an explicit `-p`

---

## 8. Open questions / next actions

| # | Question | How |
|---|---|---|
| 1 | Does UxPlay 1.74's internal mDNS responder actually work on native Win10? (issue #546) | 🧪 manual test |
| 2 | If not — does `-DUSE_DNS_SD=1` + Bonjour SDK v3.0 work, and may we redistribute it? | 🧪 manual test |
| 3 | Coexistence with an already-installed Apple Bonjour Service on UDP 5353 | 🧪 manual test |
| 4 | `d3d11videosink` + `gst_video_overlay_set_window_handle` into a child HWND: resize, aspect, teardown | 🧪 prototype |
| 5 | Exact minimal plugin-DLL set for a self-contained bundle | script: `gst-inspect-1.0 … Filename` + `ntldd -R` |
| 6 | Do `directsoundsink` / `wasapi2sink` / `wasapisink` exist in MSYS2 GStreamer 1.28.6? | `gst-inspect-1.0` after install |
| 7 | iOS 26/27 regressions #535 / #540 against our actual iPhone | 🧪 manual test |
| 8 | Does the MSYS2 `ucrt64.exe -c "..."` launcher work non-interactively for scripted pacman? | 🧪 smoke test |

---

## Appendix: source URLs used

- MSYS2 installer CLI flags — <https://www.msys2.org/docs/installer/>
- MSYS2 environments ("go with UCRT64") — <https://www.msys2.org/docs/environments/>
- MSYS2 news, MINGW64 deprecation 2026-03-15 — <https://www.msys2.org/news/>
- MSYS2 updating — <https://www.msys2.org/docs/updating/>
- MSYS2 installer releases — <https://github.com/msys2/msys2-installer/releases>
- winget manifest — <https://github.com/microsoft/winget-pkgs/tree/master/manifests/m/MSYS2/MSYS2>
- MSYS2 package index — <https://packages.msys2.org/package/>
- GStreamer env vars / running — <https://gstreamer.freedesktop.org/documentation/gstreamer/running.html>
- GStreamer registry — <https://gstreamer.freedesktop.org/documentation/gstreamer/gstregistry.html>
- GstVideoOverlay — <https://gstreamer.freedesktop.org/documentation/video/gstvideooverlay.html>
- Platform-specific elements (Windows sinks) — <https://gstreamer.freedesktop.org/documentation/tutorials/basic/platform-specific-elements.html>
- d3d11videosink — <https://gstreamer.freedesktop.org/documentation/d3d11/d3d11videosink.html>
- d3d11h264dec — <https://gstreamer.freedesktop.org/documentation/d3d11/d3d11h264dec.html>
- d3d12videosink — <https://gstreamer.freedesktop.org/documentation/d3d12/d3d12videosink.html>
- d3dvideosink (d3d plugin index) — <https://gstreamer.freedesktop.org/documentation/d3d/index.html>
- wasapisink — <https://gstreamer.freedesktop.org/documentation/wasapi/wasapisink.html>
- wasapi2sink — <https://gstreamer.freedesktop.org/documentation/wasapi2/wasapi2sink.html>
- h264parse — <https://gstreamer.freedesktop.org/documentation/videoparsersbad/h264parse.html>
- aacparse — <https://gstreamer.freedesktop.org/documentation/audioparsers/aacparse.html>
- avdec_h264 — <https://gstreamer.freedesktop.org/documentation/libav/avdec_h264.html>
- avdec_aac — <https://gstreamer.freedesktop.org/documentation/libav/avdec_aac.html>
- autovideosink — <https://gstreamer.freedesktop.org/documentation/autodetect/autovideosink.html>
- Plugin index — <https://gstreamer.freedesktop.org/documentation/plugins_doc.html>
- Deploying on Windows — <https://gstreamer.freedesktop.org/documentation/deploying/windows.html>
- d3d11 win32 window source — <https://github.com/GStreamer/gstreamer/blob/main/subprojects/gst-plugins-bad/sys/d3d11/gstd3d11window_win32.cpp>
- directsoundsink source — <https://github.com/GStreamer/gst-plugins-good/blob/master/sys/directsound/gstdirectsoundsink.c>
- `g_get_user_cache_dir` — <https://docs.gtk.org/glib/func.get_user_cache_dir.html>
- `New-NetFirewallRule` — <https://learn.microsoft.com/en-us/powershell/module/netsecurity/new-netfirewallrule>
- UxPlay repo / issues — <https://github.com/FDH2/UxPlay>
- Third-party prebuilt Windows UxPlay — <https://github.com/leapbtw/uxplay-windows>
