# scripts/

Phase 0 tooling: get MSYS2 in place, build the pinned UxPlay submodule, open the
firewall, launch the receiver. Run them in this order.

| # | Command | What it does |
|---|---|---|
| 1 | `pwsh -File scripts/setup-msys2.ps1` | Silently installs MSYS2 to `C:\msys64`, runs `pacman -Syuu` twice, installs the UCRT64 toolchain + GStreamer, prints tool versions. |
| 2 | `C:\msys64\usr\bin\bash.exe -lc "cd /c/Users/pc/Desktop/airplay && ./scripts/build.sh"` | Configures with Ninja/Release/`-DNO_MARCH_NATIVE=ON` and builds `build\uxplay.exe`. |
| 3 | `pwsh -File scripts/firewall-rules.ps1` *(elevated)* | Creates the inbound rules the iPhone needs. |
| 4 | `pwsh -File scripts/run-uxplay.ps1` | Launches `uxplay.exe` with PATH, `HOME` and GStreamer env prepared. |
| 5 | `pwsh -File scripts/smoke-test.ps1` | Device-free checks (GStreamer plugins, mDNS announcement). |

## Details

- **setup-msys2.ps1** — `-Root <path>` (default `C:\msys64`), `-SkipInstall`, `-DryRun`.
  Idempotent; re-running only tops packages up. No elevation needed for the default root.
  Start with `-DryRun` to see every command first.
- **build.sh** — must run inside the MSYS2 **UCRT64** environment (`MSYSTEM=UCRT64`).
  Assumes the submodule is already checked out (`git submodule update --init`) and fails
  with a clear message otherwise. `./scripts/build.sh clean` wipes `build/`.
  `USE_DNS_SD=1 ./scripts/build.sh` switches from the bundled mDNS responder to Apple
  Bonjour (fallback for upstream issue #546; needs the Bonjour SDK).
- **run-uxplay.ps1** — `-Name`, `-Port`, `-VideoSink`, `-AudioSink`, `-Debug`,
  `-Fullscreen`, `-ExePath`, `-Root`, `-Help`. Prepends `C:\msys64\ucrt64\bin` to PATH,
  points `HOME`/`XDG_CONFIG_HOMEDIR` at `%APPDATA%\airplay` so pairing keys persist, and
  gives GStreamer a private registry under `%LOCALAPPDATA%\airplay`. Warns if Apple's
  Bonjour Service is running or no adapter is on the Private network profile.
  `-Port N` maps to `uxplay -p N`, which opens **TCP+UDP N, N+1, N+2**; `-Port 0` emits a
  bare `-p` for the legacy set (TCP 7100/7000/7001, UDP 7011/6001/6000).
- **firewall-rules.ps1** — elevated. `-ExePath`, `-Port`, `-Remove`. Every rule it creates
  is prefixed `airplay:`; it deletes that whole set before recreating it, so re-running is
  safe. Keep `-Port` in sync with `run-uxplay.ps1`.

## Manual verification

None of these scripts can prove mirroring works — that needs the iPhone. After step 4,
walk through **`docs/MANUAL-VERIFY.md`**: discovery in Control Center, video, audio,
rotation, stop/reconnect, latency. Tick the boxes there; nothing counts as working until
they are ticked.
