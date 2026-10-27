#!/usr/bin/env bash
# Live-cFS path demo: MOST↔CCSDS translator + mos-desktop --bridge=udp
#
# Expects either:
#   A) real core-cpu1 already running (ci_lab :1234, to_lab sending TM), or
#   B) MOS_USE_LAB_PEER=1 to start scripts/ccsds-lab-peer.py as a stand-in
#
# MOS does NOT bind 1234/1235 here — translator remaps:
#   MOS TC→1334, MOS TM←1335; cFS/peer uses 1234 and TM on 1235+2234.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${MOS_DESKTOP:-$ROOT/build/src/app/mos-desktop}"
XLAT="$ROOT/scripts/most-ccsds-translator.py"
PEER="$ROOT/scripts/ccsds-lab-peer.py"

CFS_TC_PORT="${MOS_CFS_CI_PORT:-1234}"
CFS_TM_PORTS="${MOS_CFS_TM_LISTEN:-1235,2234}"
# Peer stand-in publishes to the first listed TM port.
CFS_TM_PEER_PORT="${CFS_TM_PORTS%%,*}"
MOS_TC_PORT="${MOS_XLAT_TC_PORT:-1334}"
MOS_TM_PORT="${MOS_XLAT_TM_PORT:-1335}"

if [[ ! -x "$BIN" ]]; then
  echo "mos-desktop not found at $BIN — build first:" >&2
  echo "  cmake -S . -B build -G Ninja && cmake --build build" >&2
  exit 1
fi

PIDS=()
cleanup() {
  for pid in "${PIDS[@]:-}"; do
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
    fi
  done
}
trap cleanup EXIT

if [[ "${MOS_USE_LAB_PEER:-0}" == "1" ]]; then
  echo "Starting CCSDS lab peer (ci_lab/to_lab stand-in) on ${CFS_TC_PORT}/${CFS_TM_PEER_PORT}"
  python3 "$PEER" --tc-port "$CFS_TC_PORT" --tm-port "$CFS_TM_PEER_PORT" &
  PIDS+=($!)
  sleep 0.3
fi

echo "Starting MOST↔CCSDS translator (MOS ${MOS_TC_PORT}/${MOS_TM_PORT} ↔ cFS ${CFS_TC_PORT}/{${CFS_TM_PORTS}})"
python3 "$XLAT" \
  --mos-tc-port "$MOS_TC_PORT" \
  --mos-tm-port "$MOS_TM_PORT" \
  --cfs-tc-port "$CFS_TC_PORT" \
  --cfs-tm-ports "$CFS_TM_PORTS" \
  "$@" &
PIDS+=($!)
sleep 0.4

export MOS_BRIDGE=udp
export MOS_CFS_TC_HOST=127.0.0.1
export MOS_CFS_TC_PORT="$MOS_TC_PORT"
export MOS_CFS_TM_PORT="$MOS_TM_PORT"

echo "Starting MOS UDP mode via translator"
exec "$BIN" --bridge=udp
