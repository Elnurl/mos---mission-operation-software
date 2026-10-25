#!/usr/bin/env python3
"""Minimal cFS-side UDP peer for MOS Slice A closed-loop demos.

Mimics the NASA cFS lab ground port layout:
  - listens for TC on --tc-port (default 1234, ci_lab-like)
  - publishes TM + MOSACK to --tm-host:--tm-port (default 127.0.0.1:1235, to_lab-like)

Payload is MOS MOST framing (not full CCSDS) so CfsAdapter works unchanged.
See scripts/cfs-sim-notes.md for real cFS WSL bring-up and honesty about gaps.
"""

from __future__ import annotations

import argparse
import math
import socket
import struct
import sys
import time


MOST_MAGIC = 0x4D4F5354
MOST_VERSION = 1


def pack_most(sequence: int) -> bytes:
    t = float(sequence)
    bus_v = 28.0 + 0.5 * math.sin(t / 5.0)
    batt = 75.0 + 5.0 * math.sin(t / 20.0)
    temp = 22.0 + 3.0 * math.sin(t / 12.0)
    mode = (sequence // 30) % 3
    return struct.pack(
        ">IHIfffI",
        MOST_MAGIC,
        MOST_VERSION,
        sequence & 0xFFFFFFFF,
        float(bus_v),
        float(batt),
        float(temp),
        mode,
    )


def main() -> int:
    p = argparse.ArgumentParser(description="MOS cFS UDP harness (MOST over ci/to-style ports)")
    p.add_argument("--tc-bind", default="0.0.0.0", help="bind address for TC listen")
    p.add_argument("--tc-port", type=int, default=1234, help="TC listen port (ci_lab default)")
    p.add_argument("--tm-host", default="127.0.0.1", help="TM destination host (MOS bind)")
    p.add_argument("--tm-port", type=int, default=1235, help="TM destination port (to_lab default)")
    p.add_argument("--interval", type=float, default=1.0, help="TM publish interval seconds")
    p.add_argument("--once-tm", action="store_true", help="send one TM then exit (tests)")
    args = p.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((args.tc_bind, args.tc_port))
    sock.settimeout(0.2)

    print(
        f"[harness] TC listen {args.tc_bind}:{args.tc_port} → TM {args.tm_host}:{args.tm_port}",
        flush=True,
    )
    print("[harness] waiting for MOS UDP bridge (MOST TM / stub TC / MOSACK)", flush=True)

    seq = 0
    next_tm = time.monotonic()

    try:
        while True:
            now = time.monotonic()
            if now >= next_tm:
                seq += 1
                payload = pack_most(seq)
                sock.sendto(payload, (args.tm_host, args.tm_port))
                print(f"[harness] TM seq={seq} ({len(payload)} bytes)", flush=True)
                next_tm = now + args.interval
                if args.once_tm:
                    return 0

            try:
                data, addr = sock.recvfrom(4096)
            except socket.timeout:
                continue

            text = data.decode("utf-8", errors="replace").strip()
            print(f"[harness] TC from {addr}: {text!r}", flush=True)
            # Stub encoding: cmdId|mnemonic|argsJson
            parts = text.split("|", 2)
            cmd_id = parts[0] if parts else ""
            mnemonic = parts[1] if len(parts) > 1 else "?"
            if not cmd_id:
                continue
            ack = f"MOSACK|{cmd_id}|1|harness accepted {mnemonic}".encode("utf-8")
            sock.sendto(ack, (args.tm_host, args.tm_port))
            print(f"[harness] ACK → {args.tm_host}:{args.tm_port} for {cmd_id}", flush=True)
    except KeyboardInterrupt:
        print("\n[harness] stopped", flush=True)
        return 0
    finally:
        sock.close()


if __name__ == "__main__":
    sys.exit(main())
