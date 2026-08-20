#!/usr/bin/env python3
"""Browse the LAN for AirPlay mDNS services (_airplay._tcp / _raop._tcp).

Part of the "iPhone'suz duman testi" (smoke test without an iPhone) described in
docs/SPEC.md section 2 item 5: we cannot test real mirroring without a device, but we
*can* verify that our receiver advertises itself correctly over mDNS, and we can see
what other AirPlay devices exist on the LAN.

TXT record keys are documented in docs/research/uxplay-source-map.md section 5.3/5.4
(source: third_party/UxPlay/lib/mdnsd/dnssd_mdnsd.c and lib/dns_sd/dns_sd.c).

Usage:
    python scripts/mdns-browse.py                       # browse 5 s, human output
    python scripts/mdns-browse.py --seconds 10
    python scripts/mdns-browse.py --json
    python scripts/mdns-browse.py --expect AirPlay-PC   # exit 0 if seen, 1 if not

Exit codes:
    0  success (with --expect: a matching _airplay._tcp service was seen)
    1  --expect given but no matching _airplay._tcp service was seen
    2  the `zeroconf` package is not installed
    3  runtime error (socket/mDNS failure)
   130 interrupted (Ctrl-C)
"""

from __future__ import annotations

import argparse
import json
import sys
import threading
import time

# --- dependency check -------------------------------------------------------------

try:
    from zeroconf import ServiceBrowser, ServiceListener, Zeroconf
except ImportError:  # pragma: no cover - environment dependent
    sys.stderr.write(
        "ERROR: the 'zeroconf' package is not installed.\n"
        "\n"
        "Install it with:\n"
        "    python -m pip install zeroconf\n"
        "\n"
        "(A virtualenv is fine too:\n"
        "    python -m venv .venv\n"
        "    .venv\\Scripts\\python -m pip install zeroconf\n"
        "    .venv\\Scripts\\python scripts\\mdns-browse.py )\n"
    )
    raise SystemExit(2)


SERVICE_TYPES = ["_airplay._tcp.local.", "_raop._tcp.local."]

# TXT keys worth calling out; see docs/research/uxplay-source-map.md section 5.3/5.4.
HIGHLIGHT_KEYS = ["features", "deviceid", "model", "srcvers", "flags", "pk", "ft", "am", "vs"]

# Short human-readable notes for the highlighted keys.
KEY_NOTES = {
    "features": "AirPlay features bitmask (low32,high32)",
    "ft": "RAOP features bitmask (low32,high32)",
    "deviceid": "MAC address of the advertising interface",
    "model": "advertised hardware model (UxPlay: AppleTV3,2)",
    "am": "RAOP model",
    "srcvers": "AirPlay source version (UxPlay: 220.68)",
    "vs": "RAOP server version",
    "flags": "status flags (UxPlay hardcodes 0x4)",
    "pk": "Ed25519 public key (per-install)",
}


def _decode(value: object) -> str:
    """Decode a TXT record value (bytes / None / str) into something printable."""
    if value is None:
        return ""
    if isinstance(value, bytes):
        try:
            return value.decode("utf-8")
        except UnicodeDecodeError:
            return value.hex()
    return str(value)


class _Collector(ServiceListener):
    """Collects (type, name) pairs; resolution happens later on the main thread."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self.seen: list[tuple[str, str]] = []

    def _record(self, type_: str, name: str) -> None:
        with self._lock:
            if (type_, name) not in self.seen:
                self.seen.append((type_, name))

    # zeroconf ServiceListener API
    def add_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        self._record(type_, name)

    def update_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        self._record(type_, name)

    def remove_service(self, zc: Zeroconf, type_: str, name: str) -> None:  # noqa: D102
        # Keep it in the list; a goodbye packet during the browse window is still
        # useful information ("it was there, then it left").
        self._record(type_, name)

    def snapshot(self) -> list[tuple[str, str]]:
        with self._lock:
            return list(self.seen)


def decode_features(raw: str) -> str:
    """Turn '0x5A7FFEE6,0x0' into a combined 64-bit value description."""
    parts = [p.strip() for p in raw.split(",") if p.strip()]
    try:
        low = int(parts[0], 16)
        high = int(parts[1], 16) if len(parts) > 1 else 0
    except (ValueError, IndexError):
        return ""
    combined = (high << 32) | low
    return f"= 0x{combined:X} (64-bit)"


def resolve(zc: Zeroconf, type_: str, name: str, timeout_ms: int = 3000) -> dict | None:
    info = zc.get_service_info(type_, name, timeout=timeout_ms)
    if info is None:
        return {
            "service_type": type_,
            "name": name,
            "resolved": False,
            "host": None,
            "addresses": [],
            "port": None,
            "txt": {},
        }

    try:
        addresses = list(info.parsed_addresses())
    except Exception:  # older zeroconf versions
        addresses = []

    txt: dict[str, str] = {}
    for key, value in (info.properties or {}).items():
        txt[_decode(key)] = _decode(value)

    return {
        "service_type": type_,
        "name": name,
        "resolved": True,
        "host": info.server,
        "addresses": addresses,
        "port": info.port,
        "txt": txt,
    }


def print_service(svc: dict) -> None:
    short_type = svc["service_type"].replace(".local.", "")
    print("")
    print(f"  [{short_type}] {svc['name']}")
    if not svc["resolved"]:
        print("      (could not resolve SRV/TXT within timeout)")
        return
    addrs = ", ".join(svc["addresses"]) or "(none)"
    print(f"      host    : {svc['host']}")
    print(f"      address : {addrs}")
    print(f"      port    : {svc['port']}")

    txt = svc["txt"]
    if not txt:
        print("      txt     : (empty)")
        return

    highlights = [k for k in HIGHLIGHT_KEYS if k in txt]
    if highlights:
        print("      key TXT fields:")
        for key in highlights:
            note = KEY_NOTES.get(key, "")
            value = txt[key]
            extra = ""
            if key in ("features", "ft"):
                extra = decode_features(value)
            if key == "pk" and len(value) > 24:
                value = value[:16] + "..." + value[-8:]
            line = f"        * {key:<9}= {value}"
            if extra:
                line += f"  {extra}"
            if note:
                line += f"   [{note}]"
            print(line)

    rest = sorted(k for k in txt if k not in highlights)
    if rest:
        print("      other TXT fields:")
        for key in rest:
            print(f"          {key:<9}= {txt[key]}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Browse the LAN for _airplay._tcp / _raop._tcp mDNS services.",
    )
    parser.add_argument(
        "--seconds",
        type=float,
        default=5.0,
        help="how long to browse before reporting (default: 5)",
    )
    parser.add_argument(
        "--expect",
        metavar="SUBSTRING",
        default=None,
        help=(
            "exit 0 only if an _airplay._tcp service whose name contains SUBSTRING "
            "(case-insensitive) was seen; otherwise exit 1"
        ),
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="print machine-readable JSON instead of the human report",
    )
    args = parser.parse_args(argv)

    if args.seconds <= 0:
        parser.error("--seconds must be > 0")

    zc = None
    browsers = []
    collector = _Collector()
    interrupted = False

    try:
        zc = Zeroconf()
        for type_ in SERVICE_TYPES:
            browsers.append(ServiceBrowser(zc, type_, collector))

        if not args.json:
            print(f"Browsing {', '.join(SERVICE_TYPES)} for {args.seconds:g}s ...")

        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            time.sleep(min(0.25, max(0.0, deadline - time.monotonic())))

        services = []
        for type_, name in collector.snapshot():
            svc = resolve(zc, type_, name)
            if svc is not None:
                services.append(svc)
    except KeyboardInterrupt:
        interrupted = True
        services = []
    except OSError as exc:
        sys.stderr.write(f"ERROR: mDNS browse failed: {exc}\n")
        sys.stderr.write(
            "Hint: UDP 5353 may be blocked by the firewall, or no interface is up.\n"
        )
        return 3
    finally:
        for browser in browsers:
            try:
                browser.cancel()
            except Exception:
                pass
        if zc is not None:
            try:
                zc.close()
            except Exception:
                pass

    if interrupted:
        sys.stderr.write("\nInterrupted.\n")
        return 130

    services.sort(key=lambda s: (s["service_type"], s["name"].lower()))

    airplay = [s for s in services if s["service_type"] == "_airplay._tcp.local."]
    raop = [s for s in services if s["service_type"] == "_raop._tcp.local."]

    matched = None
    if args.expect:
        needle = args.expect.lower()
        for svc in airplay:
            if needle in svc["name"].lower():
                matched = svc
                break

    if args.json:
        payload = {
            "seconds": args.seconds,
            "counts": {"_airplay._tcp": len(airplay), "_raop._tcp": len(raop)},
            "services": services,
        }
        if args.expect:
            payload["expect"] = args.expect
            payload["expect_matched"] = matched is not None
            payload["expect_match_name"] = matched["name"] if matched else None
        print(json.dumps(payload, indent=2, sort_keys=False))
    else:
        if not services:
            print("")
            print("  no _airplay._tcp services")
            print("  no _raop._tcp services")
            print("")
            print("  (nothing advertising on this LAN segment, or UDP 5353 is blocked)")
        else:
            if not airplay:
                print("")
                print("  no _airplay._tcp services")
            if not raop:
                print("")
                print("  no _raop._tcp services")
            for svc in services:
                print_service(svc)

        print("")
        print(
            f"Summary: {len(airplay)} x _airplay._tcp, {len(raop)} x _raop._tcp "
            f"in {args.seconds:g}s"
        )
        if args.expect:
            if matched:
                print(f"EXPECT OK  : _airplay._tcp service matching '{args.expect}' -> {matched['name']}")
            else:
                print(f"EXPECT FAIL: no _airplay._tcp service matching '{args.expect}'")

    if args.expect and matched is None:
        return 1
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:  # pragma: no cover
        sys.stderr.write("\nInterrupted.\n")
        raise SystemExit(130)
