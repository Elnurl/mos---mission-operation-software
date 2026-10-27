#!/usr/bin/env python3
"""MOST ↔ CCSDS UDP translator for MOS `--bridge=udp` ↔ live NASA cFS.

Topology (defaults — all remappable):

  MOS UdpCfsBridge                 this translator                    cFS
  -----------------                ----------------                   ----
  TC → mos_tc_port (1334)  ──MOST──►  pack CCSDS  ──► ci_lab :1234
  TM ← mos_tm_port (1335)  ◄─MOST──  unpack/synth ◄── to_lab → :cfs_tm_listen

Why not bind MOS directly on 1234/1235?
  ci_lab already owns :1234; to_lab sends TM to its configured TLM port
  (classic tutorials **1235**, current upstream to_lab default **2234**).
  The translator owns the cFS TM listen socket and re-publishes MOST to MOS.

On start (unless --no-auto-enable):
  1. Send TO_LAB OUTPUT_ENABLE (CC=6) to ci_lab with dest_IP = --enable-dest-ip
  2. Optionally ADD_PKT for --add-mid values (best-effort; prefer to_lab_sub.tbl)

Stub TC (cmdId|mnemonic|argsJson) → CCSDS; replies MOSACK|cmdId|0|1|detail to MOS TM port.
CCSDS TM → MOST frame (see ccsds_lab_codec.pack_most_from_ccsds) → MOS TM port.
"""

from __future__ import annotations

import argparse
import os
import socket
import sys
import time
from typing import List

# Allow `python3 scripts/most-ccsds-translator.py` without PYTHONPATH.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from ccsds_lab_codec import (  # noqa: E402
    looks_like_ccsds,
    mnemonic_to_ccsds,
    pack_add_pkt,
    pack_most_from_ccsds,
    pack_to_lab_enable,
    parse_primary_header,
    seq_count,
)


def _parse_mids(text: str) -> List[int]:
    out: List[int] = []
    for part in text.replace(",", " ").split():
        part = part.strip()
        if not part:
            continue
        out.append(int(part, 0))
    return out


def main() -> int:
    p = argparse.ArgumentParser(description="MOS MOST ↔ cFS CCSDS UDP translator")
    p.add_argument("--mos-tc-bind", default="0.0.0.0", help="bind for MOST TC from MOS")
    p.add_argument("--mos-tc-port", type=int, default=int(os.environ.get("MOS_XLAT_TC_PORT", "1334")))
    p.add_argument("--mos-tm-host", default=os.environ.get("MOS_XLAT_TM_HOST", "127.0.0.1"))
    p.add_argument("--mos-tm-port", type=int, default=int(os.environ.get("MOS_XLAT_TM_PORT", "1335")),
                   help="MOS UdpCfsBridge TM listen port")
    p.add_argument("--cfs-tc-host", default=os.environ.get("MOS_CFS_TC_HOST", "127.0.0.1"))
    p.add_argument("--cfs-tc-port", type=int, default=int(os.environ.get("MOS_CFS_TC_PORT", "1234")),
                   help="ci_lab listen port")
    p.add_argument("--cfs-tm-bind", default="0.0.0.0")
    p.add_argument(
        "--cfs-tm-port",
        type=int,
        default=int(os.environ.get("MOS_CFS_TM_LISTEN", os.environ.get("MOS_CFS_TM_PORT", "1235"))),
        help="UDP port where to_lab sends TM (1235 classic / 2234 modern default)",
    )
    p.add_argument("--enable-dest-ip", default=os.environ.get("MOS_TO_DEST_IP", "127.0.0.1"),
                   help="IP embedded in TO_LAB_ENABLE (must reach --cfs-tm-bind)")
    p.add_argument("--dest-ip-width", type=int, choices=(16, 18), default=16,
                   help="TO_LAB EnableOutput dest_IP field width (16=stock)")
    p.add_argument("--no-auto-enable", action="store_true", help="do not send TO_LAB_ENABLE on start")
    p.add_argument("--add-mid", default=os.environ.get("MOS_TO_ADD_MIDS", "0x0880,0x0883"),
                   help="comma/space MIDs for best-effort TO_LAB ADD_PKT after enable")
    p.add_argument("--no-add-pkt", action="store_true")
    args = p.parse_args()

    mos_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    mos_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    mos_sock.bind((args.mos_tc_bind, args.mos_tc_port))
    mos_sock.settimeout(0.2)

    cfs_tm_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    cfs_tm_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    cfs_tm_sock.bind((args.cfs_tm_bind, args.cfs_tm_port))
    cfs_tm_sock.settimeout(0.2)

    # Shared sock for sending TC toward ci_lab (ephemeral source).
    cfs_tc_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    print(
        f"[xlat] MOS TC {args.mos_tc_bind}:{args.mos_tc_port} → cFS ci_lab "
        f"{args.cfs_tc_host}:{args.cfs_tc_port}",
        flush=True,
    )
    print(
        f"[xlat] cFS to_lab → {args.cfs_tm_bind}:{args.cfs_tm_port} → MOST "
        f"{args.mos_tm_host}:{args.mos_tm_port}",
        flush=True,
    )

    cmd_seq = 0

    def send_cfs(pkt: bytes, label: str) -> None:
        nonlocal cmd_seq
        cfs_tc_sock.sendto(pkt, (args.cfs_tc_host, args.cfs_tc_port))
        print(f"[xlat] → cFS {label} ({len(pkt)} bytes) seq={cmd_seq}", flush=True)
        cmd_seq = (cmd_seq + 1) & 0x3FFF

    if not args.no_auto_enable:
        time.sleep(0.15)
        en = pack_to_lab_enable(
            dest_ip=args.enable_dest_ip,
            seq=cmd_seq,
            dest_ip_width=args.dest_ip_width,
        )
        send_cfs(en, f"TO_LAB_ENABLE dest={args.enable_dest_ip}")
        if not args.no_add_pkt:
            for mid in _parse_mids(args.add_mid):
                time.sleep(0.05)
                send_cfs(pack_add_pkt(mid, seq=cmd_seq), f"ADD_PKT mid=0x{mid:04X}")

    tm_count = 0
    try:
        while True:
            # CCSDS TM from to_lab → MOST to MOS
            try:
                data, addr = cfs_tm_sock.recvfrom(4096)
            except socket.timeout:
                data = b""
            if data:
                if looks_like_ccsds(data):
                    hdr = parse_primary_header(data)
                    most = pack_most_from_ccsds(data)
                    cfs_tm_sock.sendto(most, (args.mos_tm_host, args.mos_tm_port))
                    tm_count += 1
                    sid = hdr[0] if hdr else 0
                    sc = seq_count(hdr[3]) if hdr else 0
                    if tm_count <= 5 or tm_count % 50 == 0:
                        print(
                            f"[xlat] TM#{tm_count} from {addr} mid=0x{sid:04X} "
                            f"seq={sc} → MOST {len(most)}B",
                            flush=True,
                        )
                else:
                    print(f"[xlat] ignore non-CCSDS from {addr} ({len(data)}B)", flush=True)

            # MOST / stub TC from MOS → CCSDS to ci_lab + MOSACK
            try:
                data, addr = mos_sock.recvfrom(4096)
            except socket.timeout:
                continue

            text = data.decode("utf-8", errors="replace").strip()
            parts = text.split("|", 2)
            cmd_id = parts[0] if parts else ""
            mnemonic = parts[1] if len(parts) > 1 else ""
            args_json = parts[2] if len(parts) > 2 else "{}"
            print(f"[xlat] TC from MOS {addr}: {mnemonic!r} id={cmd_id}", flush=True)
            if not cmd_id:
                continue

            ccsds = mnemonic_to_ccsds(
                mnemonic,
                args_json,
                seq=cmd_seq,
                dest_ip=args.enable_dest_ip,
            )
            if ccsds is None:
                ack = f"MOSACK|{cmd_id}|0|unknown mnemonic {mnemonic!r} (try NOOP, TO_ENABLE, TO_NOOP)"
                mos_sock.sendto(ack.encode("utf-8"), (args.mos_tm_host, args.mos_tm_port))
                print(f"[xlat] ACK fail → MOS for {cmd_id}", flush=True)
                continue

            # If payload already looks like raw CCSDS was sent, forward as-is
            send_cfs(ccsds, mnemonic)
            ack = f"MOSACK|{cmd_id}|1|forwarded {mnemonic} as CCSDS ({len(ccsds)}B)"
            mos_sock.sendto(ack.encode("utf-8"), (args.mos_tm_host, args.mos_tm_port))
            print(f"[xlat] ACK ok → MOS for {cmd_id}", flush=True)
    except KeyboardInterrupt:
        print("\n[xlat] stopped", flush=True)
        return 0
    finally:
        mos_sock.close()
        cfs_tm_sock.close()
        cfs_tc_sock.close()


if __name__ == "__main__":
    sys.exit(main())
