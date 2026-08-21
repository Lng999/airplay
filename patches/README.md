# Patches against `third_party/UxPlay`

Local fixes carried on top of the pinned UxPlay submodule (1.74, `a3c19cbc`).
The submodule itself is **never** committed to — these patches are re-applied to a
clean checkout on every build.

| Patch | Fixes |
|---|---|
| `0001-mdnsd-windows-iface.patch` | `lib/mdnsd` advertises `127.0.0.1` and joins the multicast group on the loopback interface, making the AirPlay service invisible to every other host (iPhone included). |
| `0002-uxplay-handle-ctrl-break.patch` | `CtrlHandler` ignores `CTRL_BREAK_EVENT`, so a GUI host cannot stop UxPlay cleanly. |

---

## `0001-mdnsd-windows-iface.patch` — the two defects

Full analysis with evidence: [`docs/research/mdnsd-windows-rca.md`](../docs/research/mdnsd-windows-rca.md).

### Defect 1 — wrong source of the local IPv4 address (the real bug)

`mdns_get_default_ipv4()` (`third_party/UxPlay/lib/mdnsd/mdnsd.c:348-373`) determines the
host's own address with the classic "connect a UDP socket and ask `getsockname()`" trick,
using the mDNS multicast group `224.0.0.251` as the destination
(`mdnsd.c:364-369`).

That trick relies on the kernel doing a route lookup at `connect()` time. Linux does.
**Windows does not do it for a multicast destination** — it returns the loopback address.
Measured on this machine (`gcc` + Winsock, no UxPlay involved):

```
connect(224.0.0.251:5353) -> local=127.0.0.1      <-- what mdnsd asks for
connect(8.8.8.8:53)       -> local=192.168.1.107  <-- unicast works fine
connect(192.168.1.1:53)   -> local=192.168.1.107
```

`mdnsd->ipv4_addr` therefore becomes `127.0.0.1` (`mdnsd.c:992`), and that one value is
consumed in three places, each of which independently breaks discovery:

1. **`mdnsd.c:246`** — published as the host `A` record. Remote clients are told the
   AirPlay server is at `127.0.0.1`, i.e. at *themselves*.
2. **`mdnsd.c:454-458`** — installed as `IP_MULTICAST_IF`. Announcements are transmitted
   on the loopback adapter and never reach the wire.
3. **`mdnsd.c:471-486`** — used as the `IP_ADD_MEMBERSHIP` interface. The process joins
   `224.0.0.251` on loopback only, so queries arriving on the Ethernet adapter are never
   delivered — even though the socket *is* correctly bound to `INADDR_ANY:5353`.

Point 3 is why the symptom looks like a firewall problem: the process is visibly listening
on `0.0.0.0:5353` in `netstat -ano`, yet it answers nothing from the LAN.

The fix asks the IP Helper API (`GetAdaptersAddresses`) for the *up, non-loopback,
IPv4-enabled* adapter that owns a default gateway, breaking ties on the lowest
`Ipv4Metric`. `169.254.0.0/16` (APIPA) and `127.0.0.0/8` addresses are skipped.

### Defect 2 — a loopback address was never rejected

Even when the probe misfires, nothing downstream sanity-checks the result. The patch adds
a hard guard: a `127.x.x.x` result is discarded (both platforms — a loopback A record is
never correct for a service advertisement), and on Windows a final
`getaddrinfo(gethostname(), AF_INET)` fallback is tried before giving up. `mdns_add_a()`
already skips a zero address (`mdnsd.c:201-203`), so the worst case degrades to
"no A record" instead of "actively wrong A record".

### What the patch deliberately does *not* change

* **The 5353 bind is already correct.** `SO_REUSEADDR` is set at `mdnsd.c:447`, *before*
  the `bind()` at `mdnsd.c:465`, and the bind targets `INADDR_ANY:5353` (`mdnsd.c:460-463`).
  Windows permits several processes to share UDP 5353 as long as every one of them set
  `SO_REUSEADDR`, which `svchost` (Windows' own mDNS/DNS-SD client) does. Confirmed on this
  machine: uxplay.exe appears in `netstat -ano` alongside `svchost`, Brave and Spotify.
  No `SO_EXCLUSIVEADDRUSE` change is needed and none is made.
* **Bind failures are already reported.** `mdns_open_socket4()` returns `-WSAGetLastError()`
  (`mdnsd.c:465-469`), `mdnsd_start()` propagates it (`mdnsd.c:1007-1010`), and
  `dnssd_error_text()` prints it (`lib/mdnsd/dnssd_mdnsd.c:351-357`). The `mdnsd` library
  has no `logger_t` handle, so routing this through the library logger would mean changing
  `mdnsd_init()`'s signature — out of scope for a minimal fix.
* **IPv6 stays disabled on Windows.** `mdns_get_default_ipv6()` is a `#ifdef WIN32` stub
  returning 0 (`mdnsd.c:377-380`), so `mdns_open_socket6()` is never opened and no `AAAA`
  record is published. iOS discovers over IPv4 mDNS fine, so this is a known gap, not a
  blocker. Fixing it means a second `GetAdaptersAddresses` walk for link-local IPv6 plus
  the scope id — a separate patch if it ever proves necessary.

---

## `0003-uxplay-utf8-argv-manifest.patch` — non-ASCII `-n <name>` was unusable on Windows

`parse_arguments()` rejects any argv element that is not valid UTF-8
(`uxplay.cpp:1204-1210`, `exit(0)`), and `-n` rejects a non-UTF-8 server name again at
`:1239-1243`. That check is correct — the problem is what reaches it.

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

## Applying

From the repo root, against a clean submodule checkout:

```bash
git -C third_party/UxPlay apply ../../patches/0001-mdnsd-windows-iface.patch
```

Paths inside the patch are relative to the submodule root (`lib/mdnsd/mdnsd.c`), hence the
`../../` on the patch file itself.

### Line endings

`.gitattributes` sets `* text=auto eol=lf`, but the *submodule* has no `.gitattributes` and
the global `core.autocrlf` is `true`, so `third_party/UxPlay/lib/mdnsd/mdnsd.c` sits in the
working tree with **CRLF** endings while the patch is stored with LF. `git apply` handles
this correctly — verified against a synthetic LF-blob/CRLF-worktree repo. Plain
`patch -p1` may not; prefer `git apply`.

## How `build.sh` should apply these idempotently

`scripts/build.sh` currently builds the submodule as-is. To make it patch-aware without
ever leaving the submodule dirty in a surprising state, it should, right after the
"submodule is checked out" sanity check and *before* `cmake` configure:

1. **Reset first, then apply.** Run `git -C "${SRC_DIR}" checkout -- .` so the tree is
   pristine, then apply every `patches/*.patch` in sorted order. This makes the step
   naturally idempotent: re-running `build.sh` re-creates the same tree regardless of what
   the previous run left behind. Guard the reset behind a check that the submodule has no
   *unexpected* modifications you care about — a `git -C "${SRC_DIR}" stash list`-style
   escape hatch is overkill here; the submodule is pinned and disposable.

2. **Or, if a reset is too blunt, test before applying.** For each patch:
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
`third_party/UxPlay/CMakeLists.txt:51`); it needs the Bonjour SDK for Windows installed.
This should not be necessary — patch `0001` was verified to fix discovery on this machine —
but it remains the escape hatch if another process ever does hold UDP 5353 exclusively
(i.e. bound it *without* `SO_REUSEADDR`), which would make `mdns_open_socket4()`'s `bind()`
fail with `WSAEADDRINUSE` (10048). Diagnose that case by correlating the PIDs from
`netstat -ano | findstr 5353` with `tasklist /FI "PID eq <pid>"` — `netstat -abno` would
name the binaries directly but requires an elevated prompt.
