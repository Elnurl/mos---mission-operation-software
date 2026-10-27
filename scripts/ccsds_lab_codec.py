#!/usr/bin/env python3
"""Minimal CCSDS / cFE lab helpers for MOS ↔ ci_lab / to_lab.

Used by most-ccsds-translator.py and ccsds-lab-peer.py.
Not a full CCSDS stack — enough to ENABLE TO, send sample NOOPs, and
surface APID/sequence from downlink packets.
"""

from __future__ import annotations

import struct
from typing import Iterable, Optional, Tuple

# Classic sample-mission stream IDs (cpu1). Override via translator CLI/env.
TO_LAB_CMD_MID = 0x1880
SAMPLE_APP_CMD_MID = 0x1882
CI_LAB_CMD_MID = 0x1884

TO_LAB_HK_TLM_MID = 0x0880
SAMPLE_APP_HK_TLM_MID = 0x0883

TO_LAB_NOOP_CC = 0
TO_LAB_OUTPUT_ENABLE_CC = 6
TO_LAB_ADD_PKT_CC = 2
SAMPLE_APP_NOOP_CC = 0

# CCSDS primary header length field = (total_packet_octets - 7)
PRI_HDR_LEN = 6
CMD_SEC_HDR_LEN = 2


def cfs_cmd_checksum(packet: bytes) -> int:
    """cFE-style command checksum: XOR all bytes (checksum field 0) then XOR 0xFF."""
    data = bytearray(packet)
    if len(data) < 8:
        raise ValueError("command packet too short for checksum")
    data[7] = 0
    x = 0
    for b in data:
        x ^= b
    return x ^ 0xFF


def pack_cmd(stream_id: int, cmd_code: int, payload: bytes = b"", seq: int = 0) -> bytes:
    """Pack a CCSDS telecommand (version 0, type=CMD, sec hdr flag=1)."""
    # Stream ID already includes version/type/sec/apid bits for classic cFS (e.g. 0x1880).
    seq_field = 0xC000 | (seq & 0x3FFF)
    total = PRI_HDR_LEN + CMD_SEC_HDR_LEN + len(payload)
    length = total - 7
    if length < 0 or length > 0xFFFF:
        raise ValueError("invalid command length")
    pkt = bytearray()
    pkt += struct.pack(">HHH", stream_id & 0xFFFF, seq_field, length)
    pkt += bytes([(cmd_code & 0x7F), 0])  # checksum filled below
    pkt += payload
    pkt[7] = cfs_cmd_checksum(bytes(pkt))
    return bytes(pkt)


def pack_to_lab_enable(dest_ip: str = "127.0.0.1", seq: int = 0,
                       dest_ip_width: int = 16) -> bytes:
    """TO_LAB OUTPUT_ENABLE (CC=6). Payload is null-padded dest_IP char array."""
    if dest_ip_width not in (16, 18):
        raise ValueError("dest_ip_width must be 16 (stock to_lab) or 18 (legacy/OpenC3)")
    raw = dest_ip.encode("ascii", errors="replace")
    if len(raw) >= dest_ip_width:
        raw = raw[: dest_ip_width - 1]
    payload = raw.ljust(dest_ip_width, b"\0")
    return pack_cmd(TO_LAB_CMD_MID, TO_LAB_OUTPUT_ENABLE_CC, payload, seq=seq)


def pack_to_lab_noop(seq: int = 0) -> bytes:
    return pack_cmd(TO_LAB_CMD_MID, TO_LAB_NOOP_CC, b"", seq=seq)


def pack_sample_noop(seq: int = 0) -> bytes:
    return pack_cmd(SAMPLE_APP_CMD_MID, SAMPLE_APP_NOOP_CC, b"", seq=seq)


def pack_add_pkt(stream_id: int, buf_limit: int = 4, seq: int = 0) -> bytes:
    """Best-effort ADD_PKT for classic uint16 MsgId + QoS(0,0) + BufLimit.

    Newer cFE MsgId layouts may differ — prefer editing to_lab_sub.tbl on the
    flight side when ADD_PKT is ignored.
    """
    # Stream (uint16 BE), Flags Priority/Reliability, BufLimit
    payload = struct.pack(">HBBB", stream_id & 0xFFFF, 0, 0, buf_limit & 0xFF)
    return pack_cmd(TO_LAB_CMD_MID, TO_LAB_ADD_PKT_CC, payload, seq=seq)


def looks_like_ccsds(datagram: bytes) -> bool:
    if len(datagram) < PRI_HDR_LEN:
        return False
    version = (datagram[0] >> 5) & 0x7
    return version == 0


def parse_primary_header(datagram: bytes) -> Optional[Tuple[int, int, int, int, int]]:
    """Return (stream_id, version, pkt_type, seq_flags_count, length) or None."""
    if not looks_like_ccsds(datagram):
        return None
    stream_id, seq_field, length = struct.unpack(">HHH", datagram[:6])
    version = (stream_id >> 13) & 0x7
    pkt_type = (stream_id >> 12) & 0x1  # 0=TM, 1=TC
    return stream_id, version, pkt_type, seq_field, length


def seq_count(seq_field: int) -> int:
    return seq_field & 0x3FFF


def pack_most_from_ccsds(datagram: bytes, sequence_override: Optional[int] = None) -> bytes:
    """Synthesize a MOS MOST TM frame from a CCSDS datagram for CfsAdapter.

    Maps:
      SEQ      ← CCSDS sequence count (or override)
      BUS_V    ← APID (as float, diagnostic)
      BATT_SOC ← packet length field
      TEMP_BRD ← payload size after primary header
      MODE     ← 0 IDLE / 1 NOMINAL / 2 SAFE from low bits of stream id
    """
    hdr = parse_primary_header(datagram)
    if hdr is None:
        raise ValueError("not CCSDS")
    stream_id, _ver, _typ, seq_field, length = hdr
    apid = stream_id & 0x07FF
    seq = sequence_override if sequence_override is not None else seq_count(seq_field)
    payload_octets = max(0, len(datagram) - PRI_HDR_LEN)
    mode = apid % 3
    return struct.pack(
        ">IHIfffI",
        0x4D4F5354,  # MOST
        1,
        seq & 0xFFFFFFFF,
        float(apid),
        float(length),
        float(payload_octets),
        mode,
    )


def mnemonic_to_ccsds(mnemonic: str, args_json: str = "{}", seq: int = 0,
                      dest_ip: str = "127.0.0.1") -> Optional[bytes]:
    """Map MOS stub mnemonics to lab CCSDS commands. Unknown → None."""
    m = mnemonic.strip().upper()
    if m in ("NOOP", "SAMPLE_NOOP", "SAMPLE_APP_NOOP"):
        return pack_sample_noop(seq=seq)
    if m in ("TO_NOOP", "TO_LAB_NOOP"):
        return pack_to_lab_noop(seq=seq)
    if m in ("TO_ENABLE", "TO_LAB_ENABLE", "ENABLE_TLM", "ENABLE_TELEMETRY"):
        return pack_to_lab_enable(dest_ip=dest_ip, seq=seq)
    if m in ("CI_NOOP", "CI_LAB_NOOP"):
        return pack_cmd(CI_LAB_CMD_MID, 0, b"", seq=seq)
    # Allow raw hex payload: RAW_CCSDS with args {"hex":"..."} — kept simple: ignore args
    _ = args_json
    return None


def self_check() -> None:
    """Quick assertions used by scripts/tests."""
    en18 = pack_to_lab_enable("127.0.0.1", dest_ip_width=18)
    assert len(en18) == 26
    assert struct.unpack(">H", en18[4:6])[0] == 26 - 7
    assert cfs_cmd_checksum(en18) == en18[7]
    # OpenC3 tutorial bytes used length=0x0012 with 18-byte IP (checksum 0x98).
    oc = bytearray(struct.pack(">HHH", 0x1880, 0xC000, 0x0012) + bytes([6, 0]) +
                   b"127.0.0.1".ljust(18, b"\0"))
    oc[7] = cfs_cmd_checksum(bytes(oc))
    assert oc[7] == 0x98
    en16 = pack_to_lab_enable("127.0.0.1", dest_ip_width=16)
    assert len(en16) == 24
    assert cfs_cmd_checksum(en16) == en16[7]
    noop = pack_sample_noop()
    assert len(noop) == 8
    assert looks_like_ccsds(noop)
    # Fake TM: version=0 type=TM sec=1 apid=0x083 → 0x0883
    tm = struct.pack(">HHH", 0x0883, 0xC001, 0x0001) + b"\x00\x00"
    most = pack_most_from_ccsds(tm)
    assert most[:4] == b"MOST"


if __name__ == "__main__":
    self_check()
    print("ccsds_lab_codec self_check OK")
