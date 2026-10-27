#!/usr/bin/env python3
"""Minimal CCSDS / cFE lab helpers for MOS ↔ ci_lab / to_lab.

Used by most-ccsds-translator.py and ccsds-lab-peer.py.
Not a full CCSDS stack — enough to ENABLE TO, ADD_PKT, send sample NOOPs,
and surface APID/sequence from downlink packets.
"""

from __future__ import annotations

import struct
from typing import List, Optional, Tuple

# Classic sample-mission stream IDs (cpu1). Override via translator CLI/env.
TO_LAB_CMD_MID = 0x1880
SAMPLE_APP_CMD_MID = 0x1882
CI_LAB_CMD_MID = 0x1884

TO_LAB_HK_TLM_MID = 0x0880
TO_LAB_DATA_TYPES_MID = 0x0881
SAMPLE_APP_HK_TLM_MID = 0x0883
CI_LAB_HK_TLM_MID = 0x0884
CFE_ES_HK_TLM_MID = 0x0800
CFE_EVS_HK_TLM_MID = 0x0801
CFE_EVS_LONG_EVENT_MSG_MID = 0x0808

TO_LAB_NOOP_CC = 0
TO_LAB_ADD_PKT_CC = 2
TO_LAB_SEND_DATA_TYPES_CC = 3
TO_LAB_OUTPUT_ENABLE_CC = 6
SAMPLE_APP_NOOP_CC = 0

# Default MIDs worth subscribing for a visible live table on stock sample.
DEFAULT_ADD_MIDS: Tuple[int, ...] = (
    TO_LAB_HK_TLM_MID,
    TO_LAB_DATA_TYPES_MID,
    SAMPLE_APP_HK_TLM_MID,
    CI_LAB_HK_TLM_MID,
    CFE_ES_HK_TLM_MID,
    CFE_EVS_HK_TLM_MID,
    CFE_EVS_LONG_EVENT_MSG_MID,
)

# CCSDS primary header length field = (total_packet_octets - 7)
PRI_HDR_LEN = 6
CMD_SEC_HDR_LEN = 2

# ADD_PKT payload layouts (command *payload* is native endian on ci_lab ingest).
ADD_PKT_LAYOUTS = ("msgid32_le", "msgid16_le", "msgid16_be")


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


def pack_to_lab_send_data_types(seq: int = 0) -> bytes:
    """TO_LAB SEND_DATA_TYPES (CC=3) — forces one diagnostic TM packet on SB."""
    return pack_cmd(TO_LAB_CMD_MID, TO_LAB_SEND_DATA_TYPES_CC, b"", seq=seq)


def pack_sample_noop(seq: int = 0) -> bytes:
    return pack_cmd(SAMPLE_APP_CMD_MID, SAMPLE_APP_NOOP_CC, b"", seq=seq)


def pack_add_pkt(stream_id: int, buf_limit: int = 4, seq: int = 0,
                 layout: str = "msgid32_le") -> bytes:
    """TO_LAB ADD_PKT (CC=2).

    Payload is a native C struct on the FSW side (not CCSDS-endian):

      msgid32_le — modern cFE: CFE_SB_MsgId_t {uint32 Value} LE + QoS(2) + BufLimit u8
      msgid16_le — older uint16 MsgId LE + QoS(2) + BufLimit u8
      msgid16_be — classic ground-tool style uint16 BE + QoS(2) + BufLimit u8

    Wrong layout → cFS ``Subscribe Err: Bad Arg``. Prefer patching to_lab_sub
    when ADD_PKT keeps failing; see scripts/patch-cfs-to-lab-subs.sh.
    """
    if layout not in ADD_PKT_LAYOUTS:
        raise ValueError(f"unknown ADD_PKT layout {layout!r}; use {ADD_PKT_LAYOUTS}")
    bl = max(1, min(int(buf_limit), 255))
    if layout == "msgid32_le":
        # Match packed/near-packed native struct used by stock to_lab msgdefs.
        payload = struct.pack("<I", stream_id & 0xFFFFFFFF) + bytes([0, 0, bl])
    elif layout == "msgid16_le":
        payload = struct.pack("<H", stream_id & 0xFFFF) + bytes([0, 0, bl])
    else:  # msgid16_be
        payload = struct.pack(">H", stream_id & 0xFFFF) + bytes([0, 0, bl])
    return pack_cmd(TO_LAB_CMD_MID, TO_LAB_ADD_PKT_CC, payload, seq=seq)


def pack_add_pkt_variants(stream_id: int, buf_limit: int = 4,
                          seq_start: int = 0) -> List[Tuple[str, bytes]]:
    """Build ADD_PKT for every known layout (owner can try until Bad Arg stops)."""
    out: List[Tuple[str, bytes]] = []
    seq = seq_start
    for layout in ADD_PKT_LAYOUTS:
        out.append((layout, pack_add_pkt(stream_id, buf_limit=buf_limit, seq=seq, layout=layout)))
        seq = (seq + 1) & 0x3FFF
    return out


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
    if m in ("TO_SEND_DATA_TYPES", "SEND_DATA_TYPES", "TO_DATA_TYPES"):
        return pack_to_lab_send_data_types(seq=seq)
    if m in ("CI_NOOP", "CI_LAB_NOOP"):
        return pack_cmd(CI_LAB_CMD_MID, 0, b"", seq=seq)
    _ = args_json
    return None


def self_check() -> None:
    """Quick assertions used by scripts/tests."""
    en18 = pack_to_lab_enable("127.0.0.1", dest_ip_width=18)
    assert len(en18) == 26
    assert struct.unpack(">H", en18[4:6])[0] == 26 - 7
    assert cfs_cmd_checksum(en18) == en18[7]
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
    add = pack_add_pkt(0x0883, layout="msgid32_le")
    assert len(add) == 8 + 7
    assert pack_to_lab_send_data_types()[6] & 0x7F == TO_LAB_SEND_DATA_TYPES_CC
    tm = struct.pack(">HHH", 0x0883, 0xC001, 0x0001) + b"\x00\x00"
    most = pack_most_from_ccsds(tm)
    assert most[:4] == b"MOST"


if __name__ == "__main__":
    self_check()
    print("ccsds_lab_codec self_check OK")
