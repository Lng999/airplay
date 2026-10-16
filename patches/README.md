# Patches against `third_party/UxPlay`

Local fixes carried on top of the pinned UxPlay submodule (1.74, `4e95a9e` = upstream master
2026-10-16; previously `a3c19cbc`, moved 2026-10-17, see `docs/UPSTREAM-SYNC-SPEC.md`).
The submodule itself is **never** committed to — these patches are re-applied to a
clean checkout on every build.

| Patch | Fixes |
|---|---|
| ~~`0001-mdnsd-windows-iface.patch`~~ | **Retired 2026-10-17** — fixed upstream by `8d62eba` (see below). Number left unused. |
| `0002-uxplay-handle-ctrl-break.patch` | `CtrlHandler` ignores `CTRL_BREAK_EVENT`, so a GUI host cannot stop UxPlay cleanly. |
| `0003-uxplay-utf8-argv-manifest.patch` | A non-ASCII `-n <name>` is rejected on Windows: argv arrives in the ANSI code page. |
| `0004-uxplay-report-mirror-idle.patch` | Hook: says within 400 ms that video frames stopped or resumed. |
| `0005-uxplay-report-video-stats.patch` | Hook: reports the video bitrate and frame rate once a second. |

`0002`–`0005` were regenerated against `4e95a9e` on 2026-10-17 (`git diff` per patch on top of
the previous one). Only hunk positions and context moved; the added/removed lines are
identical to the `a3c19cbc` versions (0002 +1/−0, 0003 +22/−1, 0004 +36/−0, 0005 +47/−0).
`0005` still applies only on top of `0004`.

---

## `0001-mdnsd-windows-iface.patch` — retired (2026-10-17, upstream `8d62eba`)

The patch file is deleted; the number `0001` stays unused so `0002`–`0005` keep their names.
Its full text and the original two-defect analysis are in git history (the last commit that
still has `patches/0001-mdnsd-windows-iface.patch`) and in
[`docs/research/mdnsd-windows-rca.md`](../docs/research/mdnsd-windows-rca.md).

**Why it is no longer needed.** The defect was that `mdns_get_default_ipv4()` found the
host address with `connect(224.0.0.251)` + `getsockname()`, which on Windows can return
`127.0.0.1`; that value became the A record, `IP_MULTICAST_IF` and the `IP_ADD_MEMBERSHIP`
interface. Upstream `8d62eba` (2026-09-02) fixes the same root cause: on `_WIN32`,
`mdns_get_default_ipv4()` (`third_party/UxPlay/lib/mdnsd/mdnsd.c:348-414`) no longer calls
`connect()` at all and walks `GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, …)`
instead (`mdnsd.c:360-367`); the old probe now only runs in the POSIX `#else` branch
(`mdnsd.c:392-414`). The result still lands in `mdnsd->ipv4_addr` (`mdnsd.c:1038`) and from
there in the A record (`mdnsd.c:246`), `IP_MULTICAST_IF` (`mdnsd.c:503`) and
`IP_ADD_MEMBERSHIP` (`mdnsd.c:518-526`).

**How upstream differs from our 0001** [read from source, not measured]:

| Topic | Upstream `8d62eba` | Retired 0001 |
|---|---|---|
| Adapter choice | **first** adapter that is up, not loopback, has a gateway (`mdnsd.c:374-380`) | adapter with a gateway and the lowest `Ipv4Metric` |
| Virtual adapters | skipped if the description contains `Hyper-V`, `Virtual`, `WSL` or `vEthernet` (`mdnsd.c:377-380`) | not filtered |
| APIPA `169.254/16` | not filtered (`FirstUnicastAddress` as is, `mdnsd.c:382-385`) | skipped |
| Nothing found | `addr = 0`: no A record (`mdns_add_a()` skips 0, `mdnsd.c:201-203`), multicast on `INADDR_ANY` (`mdnsd.c:522`) | `connect` probe, `127/8` rejected, then `gethostname()` + `getaddrinfo` |
| `127.x` guard on POSIX | none | yes |

**Remaining risk** [inference]: on a machine whose only gateway-owning NIC has a description
containing `Virtual`/`vEthernet` — a Hyper-V **External** switch, some VPN adapters —
upstream picks nothing and publishes no A record; 0001 would have picked that adapter.
Whether iOS still resolves the service then is not measured. It is listed in
`docs/MANUAL-VERIFY.md` ("Upstream 4e95a9e"). If it bites, the fix is a small new patch
("`addr == 0` → fallback"), not a revival of 0001.

Measured 2026-10-17 on this machine (Realtek Ethernet + two VMware host-only adapters,
`VMware Virtual Ethernet Adapter …`): `scripts/smoke-test.ps1` saw the receiver advertised
at `192.168.1.106`, the Ethernet address.

What 0001 deliberately did not change still holds at `4e95a9e`: `SO_REUSEADDR` before
`bind()` (`mdnsd.c:493`, `:511`, bind to `INADDR_ANY:5353`), bind errors returned as
`-SOCKET_GET_ERROR()` (`mdnsd.c:511-515`), and IPv6 off on Windows
(`mdns_get_default_ipv6()` returns 0 under `WIN32`, `mdnsd.c:421-426`).

---

## `0003-uxplay-utf8-argv-manifest.patch` — non-ASCII `-n <name>` was unusable on Windows

`parse_arguments()` rejects any argv element that is not valid UTF-8
(`uxplay.cpp:1490-1494`, `exit(0)`), and `-n` rejects a non-UTF-8 server name again at
`:1522-1526`. That check is correct — the problem is what reaches it.

`uxplay.exe` is linked without `-municode`, so the CRT builds `argv[]` from
`GetCommandLineA()`, i.e. the UTF-16 command line converted to the process **ANSI code
page**. A launcher that starts the receiver with `CreateProcessW` (our `app/src/host`, but
equally `cmd`, PowerShell or Explorer) therefore delivers `-n Salon Odası` as CP1254 bytes;
`is_utf8()` sees `0xFD` with no continuation byte and aborts with

```
Error: detected a non-ascii or non-UTF-8 string "orpc?cpcu?" while parsing input arguments
```

with **exit code 0**, which looks to the caller like a clean shutdown rather than a rejected
argument.

The patch embeds a manifest declaring `activeCodePage = UTF-8` (Windows 10 1903+,
`CREATEPROCESS_MANIFEST_RESOURCE_ID` 1 / `RT_MANIFEST` 24). The process ANSI code page then
*is* UTF-8, so the CRT's own down-conversion produces exactly the bytes `is_utf8()` wants.
No C++ source changes, no `wmain`, and non-Windows builds are untouched.

Verified 2026-08-21: `-n orpcıcpcuı` used to exit immediately; with the patch the receiver
starts and logs `WARNING: a non-ascii (UTF-8) server-name "orpcıcpcuı" was specified`, i.e.
UxPlay decoded the name correctly.

An external `uxplay.exe.manifest` next to the binary was tried first and does **not** work —
`activeCodePage` is only honoured from an embedded manifest.

## `0004-uxplay-report-mirror-idle.patch` — telling the front-end that frames stopped

Not a defect fix: a hook we need and upstream has no reason to want.

A client whose screen is off keeps the RTSP session alive and keeps requesting feedback
(`feedback_callback()`, `uxplay.cpp:575-598`, therefore stays quiet); what it stops is sending video. The only
evidence upstream offers is the client's own `-FPSdata` report, where `submitSurfaceFPS`
drops to 0 — but those arrive once a second, and waiting for two of them to be sure meant
the front-end reacted 2-3 seconds late.

`video_process()` (`uxplay.cpp:2729`) runs once per frame. The patch stamps
`last_video_frame_time` there and adds a 200 ms GLib timer that logs one line each way:

```
mirror idle: no video frames from client     (400 ms without a frame)
mirror active: video frames resumed
```

`app/src/host/line_parser.cpp` turns those into `MirrorActivity` events, about half a second
after the frames stop. The GUI no longer acts on them: an earlier version hid the frozen
picture and muted the receiver, and that was removed in favour of leaving a sleeping phone's
session alone (`app/README.md`, "Sleeping phone"). The events have no consumer
(`app/src/ui/main_window.cpp`, `case K::MirrorActivity`); the two lines still reach the log.
Whether to keep carrying this patch is an open decision.

## `0005-uxplay-report-video-stats.patch` — how much video is actually arriving

Also a hook, not a fix. Nothing upstream reports the video bitrate, and the only frame-rate
figure is the client's own `-FPSdata` report — which the client only sends when asked, and
which describes what the *phone* thinks it produced.

`video_process()` sees every frame, so the patch counts bytes and frames there and a
one-second GLib timer divides:

```
mirror stats: 12480 kbps 59 fps
```

Silent while no frames arrive (screen off), which `0004` already reports separately.
`app/src/host/line_parser.cpp` turns the line into a `MirrorStats` event and the GUI puts
`· 12.5 Mbps · 59 fps` on the status line.

## Applying

From the repo root, against a clean submodule checkout:

```bash
git -C third_party/UxPlay apply ../../patches/0002-uxplay-handle-ctrl-break.patch
```

Paths inside the patch are relative to the submodule root (`uxplay.cpp`), hence the
`../../` on the patch file itself.

### Line endings

`.gitattributes` sets `* text=auto eol=lf`, but the *submodule* has no `.gitattributes` and
the global `core.autocrlf` is `true`, so `third_party/UxPlay/uxplay.cpp` sits in the
working tree with **CRLF** endings while the patch is stored with LF. `git apply` handles
this correctly — verified against a synthetic LF-blob/CRLF-worktree repo. Plain
`patch -p1` may not; prefer `git apply`.

## How `build.sh` applies these (implemented)

`scripts/build.sh` resets the submodule and applies every patch before each build
(`apply_patches()`). The design notes below were written before that existed; to stay
patch-aware without ever leaving the submodule dirty in a surprising state, it does this
right after the "submodule is checked out" sanity check and *before* `cmake` configure:

1. **Reset first, then apply.** Run `git -C "${SRC_DIR}" checkout -- .` so the tree is
   pristine, then apply every `patches/*.patch` in sorted order. This makes the step
   naturally idempotent: re-running `build.sh` re-creates the same tree regardless of what
   the previous run left behind. Guard the reset behind a check that the submodule has no
   *unexpected* modifications you care about — a `git -C "${SRC_DIR}" stash list`-style
   escape hatch is overkill here; the submodule is pinned and disposable.

   This is what `scripts/build.sh` does. Option 2 below was the first implementation and
   had to be abandoned: `0005` rewrites the context `0004` is anchored on, so on a correctly
   patched tree neither the reverse check nor the forward check succeeds and the build died
   claiming the patch did not apply. Note that the reset has to remove untracked files too
   (`git clean -fd`) — `0003` creates `uxplay.rc` and `uxplay.manifest`, and re-applying it
   over them fails.

2. ~~**Or, if a reset is too blunt, test before applying.**~~ (abandoned, see above) For each patch:
   `git -C "${SRC_DIR}" apply --reverse --check "${p}"` succeeds *only* if the patch is
   already applied — skip it. Otherwise `git -C "${SRC_DIR}" apply --check "${p}"` must
   succeed before the real `git apply "${p}"` runs; if neither check passes, the submodule
   is in an unknown state and the build should `die` with the patch name rather than build
   something half-patched. (Verified: a second `git apply` of the same patch fails with
   `patch does not apply`, and `git apply --reverse --check` succeeds — so this test is
   reliable.)

3. **Force a reconfigure when the patch set changes.** The patches touch sources, not
   `CMakeLists.txt`, so Ninja's dependency scan picks the change up on its own; no
   `build/` wipe is needed. Only a patch that adds or removes a source file would require
   `build.sh clean`.

4. **Report it.** Echo `==> Applying N patch(es)` in the same `step()` style as the rest of
   the script so a patched build is never mistaken for a stock one.

## Fallback if `lib/mdnsd` cannot be made to work

Build against Apple Bonjour instead:

```bash
USE_DNS_SD=1 ./scripts/build.sh
```

`scripts/build.sh` already supports this (`-DUSE_DNS_SD=1`, selected at
`third_party/UxPlay/CMakeLists.txt:57`); it needs the Bonjour SDK for Windows installed.
This should not be necessary — discovery works with the bundled responder (upstream `8d62eba`,
formerly our `0001`; smoke test 2026-10-17) —
but it remains the escape hatch if another process ever does hold UDP 5353 exclusively
(i.e. bound it *without* `SO_REUSEADDR`), which would make `mdns_open_socket4()`'s `bind()`
fail with `WSAEADDRINUSE` (10048). Diagnose that case by correlating the PIDs from
`netstat -ano | findstr 5353` with `tasklist /FI "PID eq <pid>"` — `netstat -abno` would
name the binaries directly but requires an elevated prompt.
