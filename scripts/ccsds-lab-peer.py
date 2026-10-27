#!/usr/bin/env python3
"""Minimal ci_lab / to_lab stand-in for proving the MOST↔CCSDS translator locally.

Without a full NASA cFS build (CI / cloud agents), this peer:
  - listens for CCSDS TC on --tc-port (default 1234, ci_lab-like)
  - after receiving TO_LAB_ENABLE (or immediately with --auto-tm), publishes
    synthetic CCSDS TM to --tm-host:--tm-port (default 127.0.0.1:1235)

Not flight software — only enough bytes to exercise the translator + MOS.
"""

from __future__ import annotations

import argparse
import os
import socket
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from ccsds_lab_codec import (  # noqa: E402
    SAMPLE_APP_CMD_MID,
    SAMPLE_APP_HK_TLM_MID,
    TO_LAB_CMD_MID,
    TO_LAB_HK_TLM_MID,
    TO_LAB_OUTPUT_ENABLE_CC,
    looks_like_ccsds,
    parse_primary_header,
)


def pack_tm(stream_id: int, seq: int, payload: bytes) -> bytes:
    seq_field = 0xC000 | (seq & 0x3FFF)
    total = 6 + len(payload)
    length = total - 7
    return struct.pack(">HHH", stream_id & 0xFFFF, seq_field, length) + payload


def main() -> int:
    p = argparse.ArgumentParser(description="Fake ci_lab/to_lab peer for MOS translator tests")
    p.add_argument("--tc-bind", default="0.0.0.0")
    p.add_argument("--tc-port", type=int, default=1234)
    p.add_argument("--tm-host", default="127.0.0.1")
    p.add_argument("--tm-port", type=int, default=1235)
    p.add_argument("--interval", type=float, default=1.0)
    p.add_argument("--auto-tm", action="store_true",
                   help="publish TM even before TO_LAB_ENABLE")
    p.add_argument("--once-tm", action="store_true")
    args = p.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((args.tc_bind, args.tc_port))
    sock.settimeout(0.2)

    enabled = args.auto_tm
    print(
        f"[peer] ci_lab-like TC {args.tc_bind}:{args.tc_port} → TM "
        f"{args.tm_host}:{args.tm_port} (enabled={enabled})",
        flush=True,
    )

    seq = 0
    next_tm = time.monotonic()
    try:
        while True:
            now = time.monotonic()
            if enabled and now >= next_tm:
                seq += 1
                # Alternate HK-like packets
                mid = TO_LAB_HK_TLM_MID if seq % 2 else SAMPLE_APP_HK_TLM_MID
                # Minimal secondary + counters (not full HK layout)
                payload = struct.pack(">IHHBB", int(now), 0, 0, seq & 0xFF, (seq // 2) & 0xFF)
                pkt = pack_tm(mid, seq, payload)
                sock.sendto(pkt, (args.tm_host, args.tm_port))
                print(f"[peer] TM mid=0x{mid:04X} seq={seq} ({len(pkt)}B)", flush=True)
                next_tm = now + args.interval
                if args.once_tm:
                    return 0

            try:
                data, addr = sock.recvfrom(4096)
            except socket.timeout:
                continue

            if not looks_like_ccsds(data):
                print(f"[peer] non-CCSDS from {addr}: {data[:32]!r}", flush=True)
                continue
            hdr = parse_primary_header(data)
            assert hdr is not None
            stream_id, _v, _t, seq_field, _len = hdr
            cmd_code = data[6] & 0x7F if len(data) >= 7 else -1
            print(
                f"[peer] TC from {addr} mid=0x{stream_id:04X} cc={cmd_code} "
                f"({len(data)}B)",
                flush=True,
            )
            if stream_id == TO_LAB_CMD_MID and cmd_code == TO_LAB_OUTPUT_ENABLE_CC:
                enabled = True
                dest = data[8:].split(b"\0", 1)[0].decode("ascii", errors="replace")
                print(f"[peer] TO enabled → {args.tm_host}:{args.tm_port} (dest_ip={dest!r})",
                      flush=True)
            elif stream_id == SAMPLE_APP_CMD_MID and cmd_code == 0:
                print("[peer] SAMPLE_APP NOOP accepted", flush=True)
    except KeyboardInterrupt:
        print("\n[peer] stopped", flush=True)
        return 0
    finally:
        sock.close()


if __name__ == "__main__":
    sys.exit(main())
