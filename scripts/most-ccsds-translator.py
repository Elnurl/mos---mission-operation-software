#!/usr/bin/env python3
"""MOST ↔ CCSDS UDP translator for MOS `--bridge=udp` ↔ live NASA cFS.

Topology (defaults):

  MOS UdpCfsBridge                 this translator                    cFS
  -----------------                ----------------                   ----
  TC → mos_tc_port (1334)  ──MOST──►  pack CCSDS  ──► ci_lab :1234
  TM ← mos_tm_port (1335)  ◄─MOST──  unpack/synth ◄── to_lab → :1235 and/or :2234

Why dual TM listen?
  Classic tutorials use **1235**; current upstream to_lab default is **2234**
  (`TO_LAB_MISSION_TLM_PORT`). Listening on both avoids a silent empty table.

Subscriptions:
  ENABLE alone does not create TM if to_lab's subscription table is empty
  (stock `apps/to_lab/fsw/tables/to_lab_sub.c` often has every MID commented).
  Fix with `scripts/patch-cfs-to-lab-subs.sh`, then restart `core-cpu1`.
  Optional `--add-pkt` tries runtime ADD_PKT (can print Subscribe Err: Bad Arg
  if MsgId layout mismatches — use `--add-pkt-layout`).
"""

from __future__ import annotations

import argparse
import os
import select
import socket
import sys
import time
from typing import List

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from ccsds_lab_codec import (  # noqa: E402
    DEFAULT_ADD_MIDS,
    looks_like_ccsds,
    mnemonic_to_ccsds,
    pack_add_pkt,
    pack_most_from_ccsds,
    pack_to_lab_enable,
    pack_to_lab_send_data_types,
    parse_primary_header,
    seq_count,
)


def _parse_ints(text: str) -> List[int]:
    out: List[int] = []
    for part in text.replace(",", " ").split():
        part = part.strip()
        if not part:
            continue
        out.append(int(part, 0))
    return out


def _bind_udp(bind_addr: str, port: int) -> socket.socket:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((bind_addr, port))
    sock.setblocking(False)
    return sock


def main() -> int:
    default_tm_ports = os.environ.get("MOS_CFS_TM_LISTEN", "1235,2234")
    p = argparse.ArgumentParser(description="MOS MOST ↔ cFS CCSDS UDP translator")
    p.add_argument("--mos-tc-bind", default="0.0.0.0")
    p.add_argument("--mos-tc-port", type=int, default=int(os.environ.get("MOS_XLAT_TC_PORT", "1334")))
    p.add_argument("--mos-tm-host", default=os.environ.get("MOS_XLAT_TM_HOST", "127.0.0.1"))
    p.add_argument("--mos-tm-port", type=int, default=int(os.environ.get("MOS_XLAT_TM_PORT", "1335")))
    p.add_argument("--cfs-tc-host", default=os.environ.get("MOS_CFS_TC_HOST", "127.0.0.1"))
    p.add_argument("--cfs-tc-port", type=int, default=int(os.environ.get("MOS_CFS_CI_PORT", "1234")))
    p.add_argument("--cfs-tm-bind", default="0.0.0.0")
    p.add_argument(
        "--cfs-tm-ports",
        default=default_tm_ports,
        help="comma list of UDP ports where to_lab may send TM (default 1235,2234)",
    )
    p.add_argument("--enable-dest-ip", default=os.environ.get("MOS_TO_DEST_IP", "127.0.0.1"))
    p.add_argument("--dest-ip-width", type=int, choices=(16, 18), default=16)
    p.add_argument("--no-auto-enable", action="store_true")
    p.add_argument(
        "--add-pkt",
        action="store_true",
        help="after ENABLE, send TO_LAB ADD_PKT for --add-mid (may Bad Arg; prefer patch-cfs-to-lab-subs.sh)",
    )
    p.add_argument(
        "--add-pkt-layout",
        default=os.environ.get("MOS_TO_ADD_PKT_LAYOUT", "msgid32_le"),
        choices=("msgid32_le", "msgid16_le", "msgid16_be"),
        help="native ADD_PKT Stream packing (default msgid32_le for modern cFE)",
    )
    p.add_argument(
        "--add-mid",
        default=os.environ.get(
            "MOS_TO_ADD_MIDS",
            ",".join(f"0x{m:04X}" for m in DEFAULT_ADD_MIDS),
        ),
    )
    p.add_argument(
        "--request-data-types",
        action="store_true",
        default=True,
        help="after enable, send TO_LAB SEND_DATA_TYPES once (default on)",
    )
    p.add_argument("--no-request-data-types", action="store_true")
    p.add_argument(
        "--data-types-interval",
        type=float,
        default=float(os.environ.get("MOS_TO_DATA_TYPES_INTERVAL", "5")),
        help="re-send SEND_DATA_TYPES every N seconds while no TM seen (0=disable)",
    )
    args = p.parse_args()
    if args.no_request_data_types:
        args.request_data_types = False

    tm_ports = _parse_ints(args.cfs_tm_ports)
    if not tm_ports:
        print("[xlat] error: --cfs-tm-ports empty", file=sys.stderr)
        return 2

    mos_sock = _bind_udp(args.mos_tc_bind, args.mos_tc_port)
    tm_socks = []
    for port in tm_ports:
        try:
            tm_socks.append((_bind_udp(args.cfs_tm_bind, port), port))
        except OSError as exc:
            print(f"[xlat] WARN: cannot bind TM port {port}: {exc}", flush=True)
    if not tm_socks:
        print("[xlat] error: no TM listen sockets", file=sys.stderr)
        return 2

    cfs_tc_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    print(
        f"[xlat] MOS TC {args.mos_tc_bind}:{args.mos_tc_port} → cFS ci_lab "
        f"{args.cfs_tc_host}:{args.cfs_tc_port}",
        flush=True,
    )
    listened = ", ".join(str(p) for _, p in tm_socks)
    print(
        f"[xlat] cFS to_lab → {{{listened}}} → MOST {args.mos_tm_host}:{args.mos_tm_port}",
        flush=True,
    )
    print(
        "[xlat] If TM stays silent: (1) run scripts/patch-cfs-to-lab-subs.sh && restart "
        "core-cpu1  (2) confirm to_lab port is in the listen set above  "
        "(3) avoid ADD_PKT Bad Arg — omit --add-pkt until subs table is fixed",
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
        send_cfs(
            pack_to_lab_enable(
                dest_ip=args.enable_dest_ip,
                seq=cmd_seq,
                dest_ip_width=args.dest_ip_width,
            ),
            f"TO_LAB_ENABLE dest={args.enable_dest_ip}",
        )
        if args.add_pkt:
            for mid in _parse_ints(args.add_mid):
                time.sleep(0.05)
                send_cfs(
                    pack_add_pkt(mid, seq=cmd_seq, layout=args.add_pkt_layout),
                    f"ADD_PKT mid=0x{mid:04X} layout={args.add_pkt_layout}",
                )
        if args.request_data_types:
            time.sleep(0.05)
            send_cfs(pack_to_lab_send_data_types(seq=cmd_seq), "TO_LAB_SEND_DATA_TYPES")

    tm_count = 0
    last_tm_mono = 0.0
    last_warn_mono = time.monotonic()
    last_data_types = time.monotonic()
    try:
        while True:
            readers = [mos_sock] + [s for s, _ in tm_socks]
            ready, _, _ = select.select(readers, [], [], 0.2)
            now = time.monotonic()

            for sock in ready:
                if sock is mos_sock:
                    try:
                        data, addr = sock.recvfrom(4096)
                    except OSError:
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
                        ack = (
                            f"MOSACK|{cmd_id}|0|unknown mnemonic {mnemonic!r} "
                            f"(try NOOP, TO_ENABLE, TO_SEND_DATA_TYPES, TO_NOOP)"
                        )
                        mos_sock.sendto(ack.encode("utf-8"), (args.mos_tm_host, args.mos_tm_port))
                        continue
                    send_cfs(ccsds, mnemonic)
                    ack = f"MOSACK|{cmd_id}|1|forwarded {mnemonic} as CCSDS ({len(ccsds)}B)"
                    mos_sock.sendto(ack.encode("utf-8"), (args.mos_tm_host, args.mos_tm_port))
                    print(f"[xlat] ACK ok → MOS for {cmd_id}", flush=True)
                    continue

                # TM socket
                try:
                    data, addr = sock.recvfrom(4096)
                except OSError:
                    continue
                if not data:
                    continue
                if not looks_like_ccsds(data):
                    print(f"[xlat] ignore non-CCSDS from {addr} ({len(data)}B)", flush=True)
                    continue
                hdr = parse_primary_header(data)
                most = pack_most_from_ccsds(data)
                # Prefer sending from the MOS TC socket so source is consistent,
                # but any datagram to mos_tm_port is fine.
                mos_sock.sendto(most, (args.mos_tm_host, args.mos_tm_port))
                tm_count += 1
                last_tm_mono = now
                sid = hdr[0] if hdr else 0
                sc = seq_count(hdr[3]) if hdr else 0
                if tm_count <= 8 or tm_count % 50 == 0:
                    print(
                        f"[xlat] TM#{tm_count} from {addr} mid=0x{sid:04X} "
                        f"seq={sc} → MOST {len(most)}B → :{args.mos_tm_port}",
                        flush=True,
                    )

            # Periodic SEND_DATA_TYPES while waiting for first TM
            if (
                args.request_data_types
                and args.data_types_interval > 0
                and tm_count == 0
                and now - last_data_types >= args.data_types_interval
            ):
                send_cfs(pack_to_lab_send_data_types(seq=cmd_seq), "TO_LAB_SEND_DATA_TYPES (retry)")
                last_data_types = now

            # Idle warning
            if tm_count == 0 and now - last_warn_mono >= 10.0:
                print(
                    "[xlat] still no CCSDS TM — check: core-cpu1 running; "
                    f"to_lab sending to one of {{{listened}}} with dest_IP="
                    f"{args.enable_dest_ip}; subscription table not empty "
                    "(scripts/patch-cfs-to-lab-subs.sh). TC path can work while TM is dead.",
                    flush=True,
                )
                last_warn_mono = now
            elif tm_count > 0 and last_tm_mono and now - last_tm_mono >= 15.0 and now - last_warn_mono >= 15.0:
                print(
                    f"[xlat] no TM for {now - last_tm_mono:.0f}s (had {tm_count} earlier)",
                    flush=True,
                )
                last_warn_mono = now
    except KeyboardInterrupt:
        print("\n[xlat] stopped", flush=True)
        return 0
    finally:
        mos_sock.close()
        for s, _ in tm_socks:
            s.close()
        cfs_tc_sock.close()


if __name__ == "__main__":
    sys.exit(main())
