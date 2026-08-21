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

The window is Turkish and collapsed by default (`docs/PHASE2-UX-SPEC.md`): a coloured status
dot, the state, one line saying what to do next, the receiver name and a single Start/Stop
button. Two section headers open the rest:

- **Gelişmiş** — port, **Akıcılık** (the frame-rate ceiling), video/audio sink, **Görüntü
  çözücü** (decoder), Fullscreen, H.265, Ayrıntılı günlük (debug), the client FPS-report
  switch, Always on top, Start receiver on launch and *Komutu kopyala* (the exact argv, for
  reproducing a problem in a plain terminal).
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

### Sleeping phone

Locking the iPhone does **not** end an AirPlay session. It keeps requesting feedback and
keeps sending audio; what stops is the video, so the last frame would hang on screen with
disembodied sound behind it.

The signal is the client's own `-FPSdata` report: `submitSurfaceFPS` falls to 0 while the
screen is off and rises again on wake (measured in a live session: 59, 24, 0, 0, 0, 52, 59,
60). `-FPSdata` is therefore passed unconditionally, and its XML — 30 lines a second — is
dropped in the host before it can reach the log; only the parsed rate survives, and it is
shown on the status line.

Two zero reports in a row (one dropped report must not blink the window away) mean asleep:
status `Duraklatıldı`, amber dot, the child's **video window hidden** and its **audio session
muted** (`audio_mute.cpp`, matched on process id — the GStreamer pipeline is never touched).
The first non-zero report undoes both, with `SW_SHOWNA` so the returning picture does not
steal focus. Nothing is required from the user in either direction. A 10 s watchdog unhides
the window if the reports stop altogether. `[app] hide_when_stalled=0` turns it all off.

The feedback-timeout line (`uxplay.cpp:549`) is recognised but no longer treated as an
error — it means the network is late, not that anything failed.

This is also why **Bağlantı zaman aşımı defaults to off**: with UxPlay's own 15 s limit the
session was declared lost while the phone was just asleep.

- **Close button hides to the notification area.** A balloon says so the first time
  (`[app] tray_hint_shown`). The tray menu (right click) has Göster / Başlat / Durdur /
  Çıkış; only **Çıkış** really quits, and it stops the child first.
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
hide_when_stalled=1 ; hide the frozen video window while the phone's screen is off
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
- **Resolution is shown only when "Ayrıntılı günlük" (debug) is on.** UxPlay logs the mirrored size at
  DEBUG level inside the library (`lib/raop_rtp_mirror.c:608-609`); without `-d` the line is
  never printed. Turning it on also re-enables stdout buffering, so log lines arrive in
  bursts.
- **Any configuration change needs a Stop/Start.**
- Client allow/deny is decided inside the child; the GUI cannot prompt interactively.
- Real mirroring can only be confirmed with an actual iPhone — see `docs/MANUAL-VERIFY.md`.
