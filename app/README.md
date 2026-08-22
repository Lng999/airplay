# app/ — airplay-gui (Win32 GUI, Milestone 2)

The Windows front-end for the UxPlay-based AirPlay receiver. The GUI does **not** contain
the AirPlay stack: it starts `uxplay.exe` as a child process, parses its stdout and shows
the state. Milestone 2 adds the picture: the receiver's video window is adopted into a
window of ours (`SetParent`), so it is framed, letterboxed, resizable and can go fullscreen.
See `docs/PHASE2-SPEC.md`, `docs/PHASE2-M2-SPEC.md` and `docs/DESIGN.md` §6.1.

```
app/
  include/airplay/uxplay_host.h   contract between the UI and the host (orchestrator owns it)
  src/host/                       child-process host: CreateProcess, stdout reader, line parser
  src/ui/                         this GUI (window, tray, config, log, single instance)
  src/ui/video_embed.*            find the receiver's window and adopt it (or hand it back)
  src/ui/video_window.*           the window the picture lives in
  src/ui/autostart.*              the HKCU Run value behind "start with Windows"
  res/                            app.rc + app.manifest (Common Controls v6, PerMonitorV2, asInvoker)
  tests/                          host unit tests + a live test that spawns the real receiver
```

## Build

Everything is built in an **MSYS2 UCRT64** shell (same toolchain as UxPlay itself):

```bash
bash scripts/build-app.sh          # -> build-app/airplay-gui.exe
bash scripts/build-app.sh clean    # wipe build-app first
```

or by hand:

```bash
cmake -S app -B build-app -G Ninja
cmake --build build-app
```

The executable links `-static -municode -mwindows`, so it needs **no MSYS2 DLLs** — only
system DLLs (kernel32, user32, gdi32, comctl32, shell32, ole32, iphlpapi). The child
`uxplay.exe` still needs `C:\msys64\ucrt64\bin` on its `PATH`; the host puts it there.

### UI-only builds (no host)

```bash
cmake -S app -B build-app -G Ninja -DAIRPLAY_HOST_STUB=ON
```

replaces the real host with `src/ui/host_stub.cpp`: a fake that never spawns a process and
walks Starting → Waiting → Stopped with invented log lines. Useful for working on the
window without a built receiver. It is a development aid, never a specification.

### Tests

`app/CMakeLists.txt` adds `app/tests` automatically when the real host is used:

```bash
ctest --test-dir build-app                 # airplay_host_tests (unit)
./build-app/tests/airplay_host_live.exe    # spawns the real uxplay.exe, needs a LAN
```

## Run

```
build-app/airplay-gui.exe
build-app/airplay-gui.exe -autostart     # same as [app] autostart_receiver=1
```

`uxplay.exe` is looked up in this order: `[app] uxplay_path` from the config, then
`uxplay.exe` next to `airplay-gui.exe`, then `<exe dir>\..\build\uxplay.exe` (the
development layout `build-app/` + `build/`). If none exists the status line shows an error
and **Start** refuses to run.

A **portable or installed copy skips the config entirely** for that lookup and for
`msys_root`: when `<exe dir>\ucrt64\bin` exists the folder carries its own receiver and its
own runtime, and `%APPDATA%\config.ini` is shared with every other copy on the machine — so
honouring a path from there would let one copy run another copy's binaries. Detected paths
are not written back either; only a path a human typed into the file survives a save.

The window is Turkish and collapsed by default (`docs/PHASE2-UX-SPEC.md`): a coloured status
dot, the state, one line saying what to do next, the receiver name and a single Start/Stop
button. Two section headers open the rest:

- **Gelişmiş** — port, **Akıcılık** (the frame-rate ceiling), video/audio sink, **Görüntü
  çözücü** (decoder), Bağlantı zaman aşımı, Fullscreen, H.265, Ayrıntılı günlük (debug),
  Always on top, Start receiver on launch, **Görüntüyü uygulamada göster** (the picture
  window, see below), **Windows açılışında başlat** and *Komutu kopyala* (the exact argv,
  for reproducing a problem in a plain terminal).
- **Ayrıntılar** — the last 500 log lines.

Opening or closing a section re-fits the window height and is remembered in `config.ini`.
Receiver settings are greyed out while the child runs — UxPlay only reads its argv at
startup, so a change needs a restart.

The status dot: grey idle, amber starting/stopping, blue advertising, green mirroring,
red error.

### One receiver at a time

Two `uxplay.exe` on one machine do **not** conflict loudly: `SO_REUSEADDR` lets both bind the
same port and both keep listening, so the phone connects to whichever answers first. When
that is not our child, the GUI receives no events at all and would act on the wrong process —
which is exactly how the sleep handling appeared "broken" while it was in fact never reached.

Both ends are closed now: the child runs inside a job object with
`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` so it cannot outlive the GUI however the GUI dies, and
**Start** first terminates any process whose image is exactly our `uxplay.exe`
(`stale_receivers.cpp`), logging how many it removed. A manually launched `AirPlay.bat` is
therefore closed by pressing Start — deliberately, since the two would otherwise fight.

### The picture window

`[app] embed_video=1` (the default) makes the mirrored picture appear in a window of ours
rather than in `uxplay.exe`'s. The mechanism, and why the obvious alternative does not work,
is in `docs/PHASE2-M2-SPEC.md`; in short:

- `d3d11videosink` creates a top-level window in the **child** process once frames arrive.
  `GstVideoOverlay` cannot be used to point it at one of our HWNDs, because the sink
  subclasses whatever window it is handed and `SetWindowLongPtr(GWLP_WNDPROC)` is the one
  call Win32 refuses across a process boundary.
- Reparenting is allowed. A 300 ms timer looks for a top-level window owned by the child
  (by pid — `GetClassNameW` returns a single character for that window), makes it `WS_CHILD`
  and `SetParent`s it into `AirplayVideoWindow`. The sink keeps owning, drawing into and
  resizing its own window; we only decide where it sits.
- The client size the window had **before** adoption is the source resolution, so the status
  line shows `1920×1080` without `-d`.
- Ours letterboxes the guest at that aspect on a black background, and dragging its edge
  keeps the aspect (`WM_SIZING`). **F11**, **Alt+Enter**, a double click, or the entry added
  to the window's own system menu toggle fullscreen; **Esc** leaves it. *Her zaman üstte*
  now applies to this window too.
- Closing the picture window hands the receiver's window back to the desktop and does not
  end the session; it is not adopted again until the next Start.
- Release always happens **before** the child is stopped. By then the guest is our child
  window, and Windows destroys child windows along with their parent — including one that
  belongs to another process, which the sink does not expect.
- `-fs` is not passed while embedding: fullscreen is this window's job.

`app/tests/test_embed_live.cpp` exercises the whole path against a real `d3d11videosink`
(a `videotestsrc` pipeline standing in for the receiver) — find, adopt, letterbox, aspect,
release, and that the other process survives all of it.

### Sleeping phone

Locking the iPhone does **not** end an AirPlay session. It keeps requesting feedback and
keeps sending audio; what stops is the video, so the last frame would hang on screen with
disembodied sound behind it.

**The GUI does nothing about it.** The status stays `Bağlandı`, the dot stays green, the
video window stays where it is, the audio session is never touched. Waking the phone simply
resumes the picture. The receiver only ever changes what it shows because the user pressed
Start or Durdur.

An earlier version hid the video window and muted the child while the phone slept, driven by
two signals: `patches/0004` (`mirror idle` / `mirror active`, within ~400 ms of the frames
stopping) and the client's own once-a-second `-FPSdata` report. It was removed in favour of
plain mirroring — the window disappearing and coming back on its own was more surprising
than the frozen frame it replaced.

Both signals are still parsed. `MirrorActivity` now has no consumer; `-FPSdata` remains for
the frame-rate readout on the status line — its XML, 30 lines a second, is dropped in the
host before it can reach the log, and only the last non-zero rate survives (it reads 0 while
the screen is off, and that reading is ignored rather than shown).

The feedback-timeout line (`uxplay.cpp:549`) is recognised but not treated as an error — it
means the network is late, not that anything failed.

This is also why **Bağlantı zaman aşımı defaults to off**: with UxPlay's own 15 s limit the
session was declared lost while the phone was just asleep.

- **Close button hides to the notification area.** A balloon says so the first time
  (`[app] tray_hint_shown`). The tray menu (right click) has Göster / Başlat / Durdur /
  Çıkış; only **Çıkış** really quits, and it stops the child first.
- **Second launch** does not start a second GUI: it raises the first window
  (named mutex + `FindWindowW("AirplayGuiMainWindow")`).
- **Copy cmdline** puts the exact argv the host would use on the clipboard — handy for
  reproducing a problem in a plain terminal.

### Updates

`scripts/publish-release.ps1` puts one `.exe` on a GitHub release tagged `v<version>`. The app
asks `/releases/latest` for `tag_name`, compares it with the version compiled into it
(`src/ui/version.h.in`, sourced from `project(... VERSION)`) and, when the release is newer,
offers it: tray balloon, then a prompt carrying the first lines of the release notes.

Saying yes downloads the installer to `%TEMP%\airplay-update` (onto a `.part`, renamed only
when complete), stops the child — the installer replaces `uxplay.exe` too — and runs setup
`/SILENT`. `installer/airplay.iss` starts the app back up afterwards, which is why its `[Run]`
entry deliberately has no `skipifsilent`.

The check runs on a worker thread and is fired from a one-shot 4 s timer rather than
`WM_CREATE`: its answer is a message box, and a message box must not arrive before the window
it belongs to. `HTTP 404` in the log means the release is not public — the transport is
WinHTTP with no credentials, by design.

## Where things live

| What | Path |
|---|---|
| Settings | `%APPDATA%\airplay\config.ini` |
| Receiver identity (`.uxplay.pem`) | `%APPDATA%\airplay` (passed to the child as `HOME`) |
| GUI log | `%LOCALAPPDATA%\airplay\logs\gui.log` (rotated to `gui.log.1` at 5 MB) |
| GStreamer registry cache | `%LOCALAPPDATA%\airplay\gst-registry.bin` |
| Application icon | `app/res/app.ico`, drawn by `tools/make-icon.py` |

`config.ini` sections and keys:

```ini
[receiver]
name=AirPlay-PC     ; -n, plus -nh so the @hostname suffix is suppressed
port=7100           ; -p N opens TCP+UDP N..N+2; 0 emits a bare -p (legacy port set)
video_sink=d3d11videosink
audio_sink=autoaudiosink
fullscreen=0
h265=0
debug=0             ; -d; verbose, and the only way to get resolution numbers
reset=0             ; -reset n; how long the client may stay silent. 0 = never give up.
                    ; A locked iPhone is silent, so any limit ends the session while the
                    ; phone is merely asleep.
max_fps=60          ; -fps n; the maxFPS plist item the client obeys. UxPlay's own default
                    ; is 30 (lib/raop.c:623) and is the main reason mirroring can look choppy.
                    ; 0 = omit -fps and take that default.
video_decoder=      ; -vd; empty = "decodebin", which picks by GStreamer rank. On this machine
                    ; that is d3d12h264dec (258) while the sink is d3d11videosink, so frames
                    ; cross the D3D12/D3D11 boundary; d3d11h264dec keeps one API.
nohold=1

[app]
always_on_top=0
start_minimized=0
autostart_receiver=0
show_advanced=0     ; collapsible sections, written the moment they are toggled
show_details=0
tray_hint_shown=0   ; the one-shot "still running in the tray" balloon
auto_update=1       ; check GitHub for a newer release ~4 s after the window is up.
                    ; 0 only disables the startup check; the tray menu item still works.
embed_video=1       ; show the picture in our own window (see "The picture window").
                    ; 0 = milestone-1 behaviour: uxplay.exe keeps its own top-level window.
msys_root=          ; empty = detect. Written back only when a human put a path here:
uxplay_path=        ; a portable install answers both from its own folder (see below).

[window]
x=…  y=…  w=…  h=…

[video]
x=…  y=…  w=…  h=…  ; where the picture window was last
fullscreen=0        ; and whether it was fullscreen
```

"Windows açılışında başlat" is **not** in `config.ini`. Two mechanisms can start us at
logon — `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\airplay` and the optional
Startup shortcut `installer/airplay.iss` offers — and a checkbox backed by a third place
would drift out of step with both. It reads either, writes the Run value
(`"<exe>" -minimized`) and, when switched off, removes the shortcut as well
(`src/ui/autostart.cpp`).

## What is still out of reach

- **We do not own the pixels.** The picture window is adopted, not rendered by us: there is
  no `uxplay_core` linked into this process, so anything that needs to touch frames — a
  screenshot, a recording, an overlay — is not possible from here.
- **Resolution and frame rate depend on how you are running.** Both come for free while
  embedding (adoption reports the size, `patches/0005` reports rate and bitrate). With
  `embed_video=0` the resolution again needs "Ayrıntılı günlük" (`-d`): UxPlay logs the
  mirrored size at DEBUG level inside the library (`lib/raop_rtp_mirror.c:608-609`) and
  without `-d` the line is never printed. Turning it on also re-enables stdout buffering,
  so log lines arrive in bursts.
- **Any receiver setting needs a Stop/Start.** `embed_video` is the exception: it acts at
  once, because it is ours and not part of the child's argv.
- Client allow/deny is decided inside the child; the GUI cannot prompt interactively.
- Real mirroring can only be confirmed with an actual iPhone — see `docs/MANUAL-VERIFY.md`.
