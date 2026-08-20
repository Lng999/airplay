# Root-cause analysis: UxPlay's bundled `lib/mdnsd` is invisible to iOS on Windows

**Subject:** UxPlay 1.74, submodule `third_party/UxPlay` @ `a3c19cbc`, built with MSYS2 UCRT64.
**Symptom:** `build/uxplay.exe` runs, the local zeroconf browser (`scripts/mdns-browse.py`)
sees both `_airplay._tcp` and `_raop._tcp`, but an iPhone on the same LAN never lists the
receiver.
**Verdict:** one defect in `lib/mdnsd/mdnsd.c`, reproduced in isolation, fixed and verified.
All measurements below were taken live on this machine (`DESKTOP-UA4DNG8`, 2026-08-20/21).

---

## 0. Machine baseline

| Fact | Value | Source |
|---|---|---|
| Only Up adapter | `Ethernet`, Realtek PCIe GbE, ifIndex 7 | `Get-NetAdapter` |
| Its MAC | `82-7D-A5-FC-4C-BD` | `Get-NetAdapter` |
| Its IPv4 | `192.168.1.107/24` | `Get-NetIPAddress` |
| Default route | ifIndex 7, next hop `192.168.1.1`, metric 0/25 | `Get-NetRoute 0.0.0.0/0` |
| `gethostname()` | `DESKTOP-UA4DNG8` | `python -c ...` |
| `$env:COMPUTERNAME` | `DESKTOP-57478B1` | PowerShell |
| `gethostbyname(gethostname())` | `192.168.1.107` | `python -c ...` |
| `hosts` file | **no** entry for either computer name | `C:\Windows\System32\drivers\etc\hosts` |

Two of the reported oddities are resolved immediately and are **not** the bug:

* **The MAC is correct.** `82:7d:a5:fc:4c:bd` is exactly the Realtek Ethernet adapter's
  address. It only *looks* suspicious because bit 1 of the first octet is set (`0x82`),
  marking it locally administered — i.e. the adapter's MAC has been spoofed/randomised at
  the driver level, outside UxPlay. `find_mac()` (`third_party/UxPlay/uxplay.cpp:804-839`)
  simply takes the first `GetAdaptersAddresses` entry with a 6-octet MAC, `IfType` 6
  (Ethernet) or 71 (Wi-Fi), and `OperStatus == 1` (`uxplay.cpp:824-828`), then `break`s.
  With one adapter present that is unambiguously right. It is worth noting for later that
  this loop checks neither for a usable IP nor for the default route, so on a machine with
  a Hyper-V/VPN/virtual adapter enumerated first it *could* pick the wrong one — but that
  is not what happened here.

* **The hostname mismatch is cosmetic.** The two names come from two different Windows
  namespaces, and this machine genuinely disagrees with itself:

  ```
  HKLM\SYSTEM\CurrentControlSet\Services\Tcpip\Parameters  Hostname     = DESKTOP-UA4DNG8
  HKLM\SYSTEM\CurrentControlSet\Control\ComputerName\...   ComputerName = DESKTOP-57478B1
  ```

  The **service instance** name gets the NetBIOS name via `GetComputerNameA()` in
  `append_hostname()` (`uxplay.cpp:1132-1154`, called at `uxplay.cpp:3169`) → `…@DESKTOP-57478B1`.
  The **host record** name gets the DNS hostname via `gethostname()` in
  `dnssd_make_host_name()` (`lib/mdnsd/dnssd_mdnsd.c:62-72`, called at `dnssd_mdnsd.c:92`,
  handed to `mdnsd_init()` at `dnssd_mdnsd.c:219`) → `DESKTOP-UA4DNG8.local`.
  Because the `SRV` target and the `A` record owner both come from the same
  `mdnsd->host_name` (`mdnsd.c:246`, `mdnsd.c:573` via `mdns_add_service_records`), the
  advertisement is internally consistent and resolvable. Ugly, harmless. (The underlying
  registry inconsistency is a machine-level artefact of a rename that never fully
  propagated; renaming the PC and rebooting would align them.)

---

## A. Where the `A` record's IP comes from, and why it is `127.0.0.1`

Not from `gethostname()`/`getaddrinfo()` — on this machine that path would have produced
the *correct* address (`192.168.1.107`, table above). `lib/mdnsd` never asks the resolver.

`mdnsd_start()` sets the address once, at startup:

```c
/* third_party/UxPlay/lib/mdnsd/mdnsd.c:992 */
mdnsd->ipv4_addr = mdns_get_default_ipv4();
```

and `mdns_get_default_ipv4()` (`mdnsd.c:348-373`) uses the "connected UDP socket"
route-discovery trick, with the **mDNS multicast group** as the destination:

```c
/* mdnsd.c:361-369 */
remote.sin_family = AF_INET;
remote.sin_port   = htons(MDNS_PORT);
inet_pton(AF_INET, MDNS_ADDR4, &remote.sin_addr);      /* 224.0.0.251 */

if (connect(fd, (struct sockaddr *) &remote, sizeof(remote)) == 0 &&
    getsockname(fd, (struct sockaddr *) &local, &local_len) == 0) {
    addr = local.sin_addr.s_addr;
}
```

The trick assumes `connect()` performs a routing-table lookup and binds the socket to the
outgoing interface's address. On Linux it does. **On Windows it does not do so for a
multicast destination.**

### Reproduced in isolation

A 40-line Winsock program (no UxPlay code) compiled with the same UCRT64 gcc:

```
connect(224.0.0.251:5353) -> cr=0(err=0) gr=0(err=0) local=127.0.0.1
connect(8.8.8.8:53)       -> cr=0(err=0) gr=0(err=0) local=192.168.1.107
connect(192.168.1.1:53)   -> cr=0(err=0) gr=0(err=0) local=192.168.1.107
gethostname=DESKTOP-UA4DNG8
  gai 192.168.1.107
```

`connect()` *succeeds* (no error to check), and `getsockname()` *succeeds* — the function
has no way to tell it was handed garbage. Both routing entries exist for `224.0.0.0/4`
(Ethernet metric 256/ifMetric 25, Loopback 256/75), and Windows' source-address selection
for an unbound multicast destination picks loopback anyway.

### One bad value, three failures

`mdnsd->ipv4_addr == 127.0.0.1` is consumed in three places, and each one alone is enough
to make the receiver undiscoverable:

| # | Site | Consequence |
|---|---|---|
| 1 | `mdnsd.c:246` `mdns_add_a(packet, mdnsd->host_name, mdnsd->ipv4_addr, ttl)` | The published `A` record for `DESKTOP-UA4DNG8.local.` is `127.0.0.1`. Any remote client that resolves it is pointed at itself. |
| 2 | `mdnsd.c:454-458` `setsockopt(..., IP_MULTICAST_IF, &iface, ...)` with `iface = 127.0.0.1` | All outgoing announcements and responses are transmitted **on the loopback adapter**. Nothing reaches the wire. |
| 3 | `mdnsd.c:471-486` `mreq.imr_interface.s_addr = iface_addr` then `IP_ADD_MEMBERSHIP` | The process joins `224.0.0.251` **on loopback only**. Multicast queries arriving on Ethernet are never delivered to the socket. |

Failure 3 is the one that makes the bug look like a firewall problem, and it is why the
initial "not bound to 5353" hypothesis was a red herring — see section B.

Note also the silent fallback at `mdnsd.c:477-483`: if the loopback `IP_ADD_MEMBERSHIP`
had failed, the code would retry with `INADDR_ANY` and the receive path would have started
working by accident. It doesn't fail — joining on loopback is perfectly legal — so the
fallback never fires.

---

## B. The 5353 socket: correct, and not the problem

`mdns_open_socket4()` (`mdnsd.c:433-489`) does the right thing:

* `SO_REUSEADDR` is set at **`mdnsd.c:447`, before** the `bind()` at `mdnsd.c:465`
  (`SO_REUSEPORT` at `mdnsd.c:448-450` is compiled out on Windows — the macro does not exist).
* The bind target is `INADDR_ANY:5353` (`mdnsd.c:460-463`), which is what a shared mDNS
  responder must use.
* The bind result **is** checked: failure returns `-WSAGetLastError()` (`mdnsd.c:465-469`),
  `mdnsd_start()` propagates it (`mdnsd.c:1007-1010`), `dnssd_register_raop()` /
  `dnssd_register_airplay()` return it (`dnssd_mdnsd.c:256-261`, `281-286`), and
  `dnssd_error_text()` prints the numeric socket error (`dnssd_mdnsd.c:351-357`). There is
  no silent-failure path here.
* There is no `#ifdef _WIN32` divergence in this function at all.

Windows lets multiple processes share UDP 5353 provided **every** binder set
`SO_REUSEADDR`; the built-in DNS-SD client in `svchost` does, which is why Brave, Spotify
and UxPlay coexist on the port. No `SO_EXCLUSIVEADDRUSE` manipulation is needed (and
`SO_EXCLUSIVEADDRUSE` is off by default anyway).

**Live confirmation** (patched build, PID 9040; the stock build behaves identically here):

```
netstat -ano | findstr 9040
  TCP    0.0.0.0:7401           0.0.0.0:0    LISTENING   9040
  TCP    [::]:7401              [::]:0       LISTENING   9040
  UDP    0.0.0.0:5353           *:*                      9040
```

The process **is** bound to `0.0.0.0:5353`. It simply never receives LAN traffic there,
because of failure 3 above.

### Why only TCP 7101 listens — expected

The debug log prints `using network ports UDP 7400 7401 7402 TCP 7400 7401 7402` (for
`-p 7400`; the user's `-p 7100` run yields 7100/7101/7102). `raop_set_tcp_ports()` maps
`tcp[0] → mirror_data_lport` and `tcp[1] → raop->port` (`lib/raop.c:790-793`), and only
`tcp[1]` gets a listening socket, opened once by `raop_start_httpd()` →
`httpd_start()` (`uxplay.cpp:2760-2765`, `lib/raop.c:834-837`, `lib/httpd.c:654-694`,
which binds an IPv4 *and* an IPv6 socket — hence the `0.0.0.0:7101` + `[::]:7101` pair).
`tcp[0]` (7100) is the mirror data port, opened on demand by `raop_rtp_mirror` only after a
client connects; `tcp[2]` is unused — `uxplay.cpp:2767-2768` explicitly reuses the httpd
port for AirPlay instead (`/* use raop_port for airplay_port (instead of tcp[2]) */`).
**One listening TCP port is correct.**

### IPv6 mDNS is disabled on Windows

`mdns_get_default_ipv6()` is a stub under `#ifdef WIN32` returning 0
(`mdnsd.c:375-381`), so `mdns_open_socket6()` bails at `mdnsd.c:500-502` and
`mdnsd->sock_fd6` stays `-1`. No `AAAA` record is ever published (`mdnsd.c:252-258`).
Consistent with `netstat`: uxplay does not appear on `[::]:5353`. iOS discovers over IPv4
mDNS, so this is a gap rather than a blocker — but it is a real difference from Bonjour.

### Announcement TTL / cache-flush behaviour

Host records go out with `MDNS_TTL_HOST` = 120 s (`mdnsd.c:33`), service records with
`MDNSD_TTL_SERVICE` = 4500 s (`mdnsd.h:22`, used at `dnssd_mdnsd.c:258`/`283`). `A`, `AAAA`,
`SRV` and `TXT` all set the cache-flush bit `DNS_CACHE_FLUSH` (`mdnsd.c:44`, applied at
`mdnsd.c:172`, `188`, `204`, `227`); `PTR` correctly does not (`mdnsd.c:160`). Goodbye
packets are sent via `mdnsd_goodbye()` on unregister (`dnssd_mdnsd.c:321`, `341`). All of
this is standards-correct — the records were simply carrying the wrong address and going
out the wrong interface.

---

## C. Upstream status (issue #546 and after)

[`FDH2/UxPlay#546`](https://github.com/FDH2/UxPlay/issues/546) — *"Verify new mdns
implementation on Windows"*, opened by **thiccaxe**, **CLOSED 2026-08-02**, one comment:

> "Has anyone tried out the new mdns implementation on Windows? I gave it a shot and wasn't
> able to get it to work. I tried it in WSL and it seems to work in there. I am relatively
> confident that my firewall is setup correctly."

and the closing comment, from the same author:

> "My firewall was in fact not setup correctly. works."

So the exact symptom **was** reported upstream, and was closed as a firewall
misconfiguration. That diagnosis does not hold on this machine: the process is bound and
listening on 5353, and the loopback `A` record is observable directly (section D). It is
plausible that thiccaxe's firewall really was the (or a) problem for him and the interface
bug went unnoticed behind it — the two produce the same user-visible symptom.

**No upstream fix exists.** `lib/mdnsd/mdnsd.c` has exactly one commit in its history:

```
2026-06-21  69783a04  minimal mDNSResponder implementation mdnsd from @kgbook
```

Nothing has touched the file since it was introduced. A search of all open and closed
issues for "windows mdns" turns up only older, unrelated tickets (#297 on 5353 issues from
2024, #309/#278 on WSL, #334 "iPhone doesn't see airplay" from 2024) — all predating the
`lib/mdnsd` rewrite and referring to the Bonjour/`dns_sd` code path.

---

## D. The fix, and its verification

Patch: **`patches/0001-mdnsd-windows-iface.patch`** (see `patches/README.md` for how to
apply it and how `build.sh` should apply it idempotently). It is **not** applied to
`third_party/` — the submodule is untouched.

It changes `lib/mdnsd/mdnsd.c` only:

1. Adds `mdns_get_ipv4_from_adapters()` under `#ifdef WIN32`: walks
   `GetAdaptersAddresses(AF_INET, …)` and picks the adapter that is `IfOperStatusUp`, is
   not `IF_TYPE_SOFTWARE_LOOPBACK`, has `IP_ADAPTER_IPV4_ENABLED`, and has a
   `FirstGatewayAddress` (i.e. owns a default route), tie-broken by the lowest
   `Ipv4Metric`; skips `0.0.0.0`, `127.0.0.0/8` and `169.254.0.0/16`.
2. Calls it first from `mdns_get_default_ipv4()`, keeping the existing connect-probe as the
   POSIX path and as a Windows fallback.
3. Rejects a `127.x` probe result outright, and adds a last-resort
   `getaddrinfo(gethostname(), AF_INET)` on Windows.
4. Adds `#include <iphlpapi.h>` **after** `#include "../compat.h"` — critical: `iphlpapi.h`
   needs `winsock2.h`/`windows.h` first, and placing it before `compat.h` makes the whole
   of `iptypes.h` compile out (`IP_ADAPTER_ADDRESSES` undeclared). No CMake change is
   needed: `iphlpapi` is already linked into the `airplay` target at
   `third_party/UxPlay/lib/CMakeLists.txt:76`, after `dnssd` at line 73, so the static
   library's undefined symbol resolves.

### Verification — built and run for real

Scratch copy at `%TEMP%\…\scratchpad\uxplay-patched`, configured with the exact flags
`scripts/build.sh` uses (`-G Ninja -DCMAKE_BUILD_TYPE=Release -DNO_MARCH_NATIVE=ON`), built
into `scratchpad\build-patched`. Build clean.

**A/B, same machine, same command line, minutes apart** (`-n <name> -p 7400 -d -vs fakesink -as fakesink`):

| Check | Stock `build/uxplay.exe` | Patched build |
|---|---|---|
| `scripts/mdns-browse.py` resolved address | **`127.0.0.1`** | **`192.168.1.107`** |
| `netstat -ano` — UDP `0.0.0.0:5353` for its PID | yes (25276) | yes (9040) |
| TCP listeners | `0.0.0.0:7401`, `[::]:7401` | `0.0.0.0:7401`, `[::]:7401` |
| Reply to an mDNS `PTR` query sent from a socket **bound to 192.168.1.107** (loopback excluded) | **none** | **replies, `A = 192.168.1.107`** |

The last row is the decisive one. A raw `_airplay._tcp.local` `PTR` query was multicast
from a socket explicitly bound to `192.168.1.107` with `IP_MULTICAST_IF` and
`IP_ADD_MEMBERSHIP` on that same address — i.e. exactly the path an iPhone's query takes,
with loopback ruled out:

```
stock   : responders seen from LAN-bound socket: NONE
patched : responders seen from LAN-bound socket: {'192.168.1.107': 1,
                                                  '192.168.1.107 A=192.168.1.107': 1}
```

This reproduces the user-visible bug ("the iPhone can't see it") without an iPhone, and
shows the patch fixes it.

---

## Remaining risk / known gaps

* **Not tested against a real iPhone.** Everything above proves the responder now answers
  LAN-side queries with a routable address. Anything past discovery — pairing, the
  `fp-setup` handshake, mirroring — is untested by this work.
* **Windows Firewall still matters.** The RCA machine let the LAN-bound probe through
  because probe and responder share the host. A separate device also needs inbound UDP 5353
  and inbound TCP on the AirPlay port allowed for `uxplay.exe`
  (`scripts/firewall-rules.ps1` exists for this).
* **Multi-adapter machines are untested.** The new selector prefers "has a default gateway,
  lowest `Ipv4Metric`", which is right for the common case, but a box with an active VPN or
  Hyper-V switch could still pick a different adapter than the user intends — and
  `find_mac()` (`uxplay.cpp:824-828`) picks its adapter by a *different, weaker* rule
  (first Up Ethernet/Wi-Fi), so the two could disagree and advertise a `deviceid` from one
  NIC with the address of another. Worth an `--iface`-style override if it ever comes up.
* **The address is sampled once**, at `mdnsd_start()` (`mdnsd.c:992`). DHCP renewal to a new
  subnet, or cable/Wi-Fi hand-off, leaves a stale `A` record until restart. Pre-existing
  behaviour, not introduced by the patch.
* **No IPv6 on Windows** (section B). Bonjour would publish `AAAA`; this responder does not.
* **`0002-uxplay-handle-ctrl-break.patch` and `0001` have not been tested together**, though
  they touch different files and cannot conflict textually.
* No uxplay.exe process was left running by this investigation; both test instances
  (PIDs 9040 and 25276, both started here) were terminated.
