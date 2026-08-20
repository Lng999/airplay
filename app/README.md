# app/ — airplay-gui (Win32 GUI, Milestone 1)

The Windows front-end for the UxPlay-based AirPlay receiver. In Milestone 1 the GUI does
**not** contain the AirPlay stack: it starts `uxplay.exe` as a child process, parses its
stdout and shows the state. See `docs/PHASE2-SPEC.md` and `docs/DESIGN.md` §6.1.

```
app/
  include/airplay/uxplay_host.h   contract between the UI and the host (orchestrator owns it)
  src/host/                       child-process host: CreateProcess, stdout reader, line parser
  src/ui/                         this GUI (window, tray, config, log, single instance)
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

The window: status line, receiver name/port, video and audio sink, the Fullscreen / H.265 /
Debug log switches, Always on top, Start receiver on launch, the Start / Stop / Copy cmdline
buttons and the last 500 log lines. Receiver settings are greyed out while the child runs —
UxPlay only reads its argv at startup, so a change needs a restart.

- **Close button hides to the notification area.** The tray menu (right click) has
  Show / Start / Stop / Exit; only **Exit** really quits, and it stops the child first.
- **Second launch** does not start a second GUI: it raises the first window
  (named mutex + `FindWindowW("AirplayGuiMainWindow")`).
- **Copy cmdline** puts the exact argv the host would use on the clipboard — handy for
  reproducing a problem in a plain terminal.

## Where things live

| What | Path |
|---|---|
| Settings | `%APPDATA%\airplay\config.ini` |
| Receiver identity (`.uxplay.pem`) | `%APPDATA%\airplay` (passed to the child as `HOME`) |
| GUI log | `%LOCALAPPDATA%\airplay\logs\gui.log` (rotated to `gui.log.1` at 5 MB) |
| GStreamer registry cache | `%LOCALAPPDATA%\airplay\gst-registry.bin` |

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
nohold=1
reset=15            ; -reset N, 0 disables the missed-feedback timeout

[app]
always_on_top=0
start_minimized=0
autostart_receiver=0
msys_root=C:\msys64
uxplay_path=

[window]
x=…  y=…  w=…  h=…
```

## Milestone 1 limitations

- **The video window belongs to `uxplay.exe`**, not to us. We do not reparent or embed it;
  that is Milestone 2 (`GstVideoOverlay` + a real `uxplay_core`). Consequently *Always on
  top* applies to the **GUI** window only, and *Fullscreen* is handled by the sink
  (Alt+Enter in the video window).
- **No FPS and no bitrate.** Those numbers exist only inside the renderer, which we do not
  link against in M1.
- **Resolution is shown only when "Debug log" is on.** UxPlay logs the mirrored size at
  DEBUG level inside the library (`lib/raop_rtp_mirror.c:608-609`); without `-d` the line is
  never printed. Turning it on also re-enables stdout buffering, so log lines arrive in
  bursts.
- **Any configuration change needs a Stop/Start.**
- Client allow/deny is decided inside the child; the GUI cannot prompt interactively.
- Real mirroring can only be confirmed with an actual iPhone — see `docs/MANUAL-VERIFY.md`.
