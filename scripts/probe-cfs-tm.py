#!/usr/bin/env python3
"""Listen for to_lab UDP TM on classic/modern ports and print what arrives.

Owner diagnostic (WSL), with core-cpu1 already running:

  python3 scripts/probe-cfs-tm.py
  # In another terminal, send ENABLE via translator or:
  python3 -c "from ccsds_lab_codec import pack_to_lab_enable; import socket; \
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); \
s.sendto(pack_to_lab_enable('127.0.0.1'), ('127.0.0.1',1234))"

If nothing prints for 15s: subscription table empty and/or wrong dest port.
"""

from __future__ import annotations

import argparse
import os
import select
import socket
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from ccsds_lab_codec import looks_like_ccsds, parse_primary_header, seq_count  # noqa: E402


def main() -> int:
    p = argparse.ArgumentParser(description="Probe to_lab UDP telemetry ports")
    p.add_argument("--bind", default="0.0.0.0")
    p.add_argument("--ports", default="1235,2234")
    p.add_argument("--seconds", type=float, default=20.0)
    args = p.parse_args()

    socks = []
    for part in args.ports.replace(",", " ").split():
        port = int(part, 0)
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            s.bind((args.bind, port))
        except OSError as exc:
            print(f"[probe] cannot bind {port}: {exc}", flush=True)
            continue
        s.setblocking(False)
        socks.append((s, port))
        print(f"[probe] listening on {args.bind}:{port}", flush=True)

    if not socks:
        return 2

    print(f"[probe] waiting {args.seconds:.0f}s for CCSDS TM…", flush=True)
    end = time.monotonic() + args.seconds
    counts = {port: 0 for _, port in socks}
    while time.monotonic() < end:
        ready, _, _ = select.select([s for s, _ in socks], [], [], 0.5)
        for s in ready:
            data, addr = s.recvfrom(4096)
            port = s.getsockname()[1]
            counts[port] += 1
            if looks_like_ccsds(data):
                hdr = parse_primary_header(data)
                sid = hdr[0] if hdr else 0
                sc = seq_count(hdr[3]) if hdr else 0
                print(
                    f"[probe] :{port} from {addr} mid=0x{sid:04X} seq={sc} len={len(data)}",
                    flush=True,
                )
            else:
                print(f"[probe] :{port} non-CCSDS {len(data)}B from {addr}", flush=True)

    print("[probe] summary:", ", ".join(f"{p}={n}" for p, n in counts.items()), flush=True)
    if sum(counts.values()) == 0:
        print(
            "[probe] NO TM — patch subs (scripts/patch-cfs-to-lab-subs.sh), "
            "restart core-cpu1, re-send TO_LAB_ENABLE, retry.",
            flush=True,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
