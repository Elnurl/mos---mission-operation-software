#!/usr/bin/env python3
"""MOST ↔ CCSDS UDP translator for MOS `--bridge=udp` ↔ live NASA cFS.

Topology (defaults):

  MOS UdpCfsBridge                 this translator                    cFS
  -----------------                ----------------                   ----
  TC → mos_tc_port (1334)  ──MOST──►  pack CCSDS  ──► ci_lab :1234
  TM ← mos_tm_port (1335)  ◄─MOST──  unpack/synth ◄── to_lab → :1235 and/or :2234

CRITICAL — MOS must bind the translator ports, not the harness defaults:

  export MOS_CFS_TC_PORT=1334
  export MOS_CFS_TM_PORT=1335
  ./build/src/app/mos-desktop --bridge=udp

If MOS omits those env vars it listens on :1235; translator publishes MOST to
:1335 → live table stays empty even while NOOP Acks (ACKs happen to share the
TM socket path only when MOS is on 1335).

Debugging empty to_lab:
  --demo-tm   emit periodic MOST on :1335 so the UI shows params while you fix
              cFS subscriptions (patch-cfs-to-lab-subs.sh + restart core-cpu1).
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
    pack_demo_most,
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
    p.add_argument("--add-pkt", action="store_true")
    p.add_argument(
        "--add-pkt-layout",
        default=os.environ.get("MOS_TO_ADD_PKT_LAYOUT", "msgid32_le"),
        choices=("msgid32_le", "msgid16_le", "msgid16_be"),
    )
    p.add_argument(
        "--add-mid",
        default=os.environ.get(
            "MOS_TO_ADD_MIDS",
            ",".join(f"0x{m:04X}" for m in DEFAULT_ADD_MIDS),
        ),
    )
    p.add_argument("--request-data-types", action="store_true", default=True)
    p.add_argument("--no-request-data-types", action="store_true")
    p.add_argument(
        "--data-types-interval",
        type=float,
        default=float(os.environ.get("MOS_TO_DATA_TYPES_INTERVAL", "5")),
    )
    p.add_argument(
        "--demo-tm",
        action="store_true",
        default=os.environ.get("MOS_XLAT_DEMO_TM", "").lower() in ("1", "true", "yes"),
        help="publish periodic demo MOST to MOS TM port (fills live table while debugging cFS)",
    )
    p.add_argument(
        "--demo-tm-interval",
        type=float,
        default=float(os.environ.get("MOS_XLAT_DEMO_TM_INTERVAL", "1.0")),
        help="seconds between --demo-tm frames (default 1.0)",
    )
    args = p.parse_args()
    if args.no_request_data_types:
        args.request_data_types = False

    tm_ports = _parse_ints(args.cfs_tm_ports)
    if not tm_ports:
        print("[xlat] error: --cfs-tm-ports empty", file=sys.stderr)
        return 2

    mos_sock = _bind_udp(args.mos_tc_bind, args.mos_tc_port)
    # Dedicated unbound socket for MOS TX (MOST + MOSACK) — clearer + avoids oddities.
    mos_tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

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

    print("=" * 72, flush=True)
    print("[xlat] MOS MUST use translator ports (otherwise live TM table stays empty):", flush=True)
    print(
        f"       export MOS_CFS_TC_PORT={args.mos_tc_port} MOS_CFS_TM_PORT={args.mos_tm_port}",
        flush=True,
    )
    print("       ./build/src/app/mos-desktop --bridge=udp", flush=True)
    print("=" * 72, flush=True)
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
    if args.demo_tm:
        print(
            f"[xlat] DEMO TM ON — publishing MOST every {args.demo_tm_interval}s to "
            f"{args.mos_tm_host}:{args.mos_tm_port} (UI should fill even if to_lab is quiet)",
            flush=True,
        )
    print(
        "[xlat] Checklist if live table empty: "
        "(1) MOS env ports above  (2) patch-cfs-to-lab-subs.sh + make install + restart core-cpu1  "
        "(3) --demo-tm while debugging  (4) paste this terminal's [xlat] lines",
        flush=True,
    )

    cmd_seq = 0
    demo_seq = 0
    tm_count = 0
    most_sent = 0

    def send_cfs(pkt: bytes, label: str) -> None:
        nonlocal cmd_seq
        cfs_tc_sock.sendto(pkt, (args.cfs_tc_host, args.cfs_tc_port))
        print(f"[xlat] → cFS {label} ({len(pkt)} bytes) seq={cmd_seq}", flush=True)
        cmd_seq = (cmd_seq + 1) & 0x3FFF

    def send_most_to_mos(most: bytes, reason: str) -> None:
        nonlocal most_sent
        n = mos_tx.sendto(most, (args.mos_tm_host, args.mos_tm_port))
        most_sent += 1
        if most_sent <= 8 or most_sent % 50 == 0:
            print(
                f"[xlat] MOST#{most_sent} {reason} ({n}/{len(most)}B) → "
                f"{args.mos_tm_host}:{args.mos_tm_port}",
                flush=True,
            )

    def send_ack(cmd_id: str, ok: bool, detail: str) -> None:
        flag = "1" if ok else "0"
        ack = f"MOSACK|{cmd_id}|{flag}|{detail}".encode("utf-8")
        mos_tx.sendto(ack, (args.mos_tm_host, args.mos_tm_port))

    def emit_send_data_types_diag() -> None:
        """Local diagnostic MOST whenever we fire SEND_DATA_TYPES (even if cFS is quiet)."""
        nonlocal demo_seq
        demo_seq += 1
        # Distinctive values so UI shows activity tied to the command.
        most = pack_demo_most(demo_seq)
        send_most_to_mos(most, "diag-after-SEND_DATA_TYPES")

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
            emit_send_data_types_diag()

    last_tm_mono = 0.0
    last_warn_mono = time.monotonic()
    last_data_types = time.monotonic()
    last_demo = time.monotonic()
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
                        send_ack(
                            cmd_id,
                            False,
                            f"unknown mnemonic {mnemonic!r} "
                            f"(try NOOP, TO_ENABLE, TO_SEND_DATA_TYPES, TO_NOOP)",
                        )
                        continue
                    send_cfs(ccsds, mnemonic)
                    send_ack(cmd_id, True, f"forwarded {mnemonic} as CCSDS ({len(ccsds)}B)")
                    print(f"[xlat] ACK ok → MOS for {cmd_id}", flush=True)
                    if mnemonic.strip().upper() in (
                        "TO_SEND_DATA_TYPES",
                        "SEND_DATA_TYPES",
                        "TO_DATA_TYPES",
                    ):
                        emit_send_data_types_diag()
                    continue

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
                sid = hdr[0] if hdr else 0
                sc = seq_count(hdr[3]) if hdr else 0
                send_most_to_mos(most, f"from-ccsds mid=0x{sid:04X} seq={sc} via {addr}")
                tm_count += 1
                last_tm_mono = now

            if (
                args.request_data_types
                and args.data_types_interval > 0
                and tm_count == 0
                and now - last_data_types >= args.data_types_interval
            ):
                send_cfs(pack_to_lab_send_data_types(seq=cmd_seq), "TO_LAB_SEND_DATA_TYPES (retry)")
                emit_send_data_types_diag()
                last_data_types = now

            if args.demo_tm and args.demo_tm_interval > 0 and now - last_demo >= args.demo_tm_interval:
                demo_seq += 1
                send_most_to_mos(pack_demo_most(demo_seq), "demo-tm")
                last_demo = now

            if tm_count == 0 and now - last_warn_mono >= 10.0:
                print(
                    "[xlat] still no CCSDS TM from to_lab — "
                    f"MOST published to MOS so far: {most_sent}. "
                    "If MOS live table is empty, MOS is probably not on "
                    f"MOS_CFS_TM_PORT={args.mos_tm_port}. "
                    "If MOST# lines appear but you want real cFS bytes: "
                    "patch-cfs-to-lab-subs.sh && make install && restart core-cpu1. "
                    "Use --demo-tm for steady UI fill while debugging.",
                    flush=True,
                )
                last_warn_mono = now
            elif tm_count > 0 and last_tm_mono and now - last_tm_mono >= 15.0 and now - last_warn_mono >= 15.0:
                print(
                    f"[xlat] no CCSDS TM for {now - last_tm_mono:.0f}s "
                    f"(had {tm_count}; MOST sent total {most_sent})",
                    flush=True,
                )
                last_warn_mono = now
    except KeyboardInterrupt:
        print(f"\n[xlat] stopped (ccsds_tm={tm_count} most_sent={most_sent})", flush=True)
        return 0
    finally:
        mos_sock.close()
        mos_tx.close()
        for s, _ in tm_socks:
            s.close()
        cfs_tc_sock.close()


if __name__ == "__main__":
    sys.exit(main())
