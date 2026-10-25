#!/usr/bin/env bash
# Closed-loop demo: Python UDP harness (cFS-side peer) + mos-desktop --bridge=udp
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${MOS_DESKTOP:-$ROOT/build/src/app/mos-desktop}"
HARNESS="$ROOT/scripts/cfs-udp-harness.py"

TC_PORT="${MOS_CFS_TC_PORT:-1234}"
TM_PORT="${MOS_CFS_TM_PORT:-1235}"

if [[ ! -x "$BIN" ]]; then
  echo "mos-desktop not found at $BIN — build first:" >&2
  echo "  cmake -S . -B build -G Ninja && cmake --build build" >&2
  exit 1
fi

cleanup() {
  if [[ -n "${HARNESS_PID:-}" ]] && kill -0 "$HARNESS_PID" 2>/dev/null; then
    kill "$HARNESS_PID" 2>/dev/null || true
    wait "$HARNESS_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

python3 "$HARNESS" --tc-port "$TC_PORT" --tm-port "$TM_PORT" &
HARNESS_PID=$!
sleep 0.3

export MOS_CFS_TC_PORT="$TC_PORT"
export MOS_CFS_TM_PORT="$TM_PORT"
export MOS_BRIDGE=udp

echo "Starting MOS UDP mode (TC→${TC_PORT}, TM←${TM_PORT})"
exec "$BIN" --bridge=udp "$@"
