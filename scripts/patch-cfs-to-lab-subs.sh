#!/usr/bin/env bash
# Patch NASA cFS to_lab subscription table so ENABLE produces real UDP TM.
#
# Stock apps/to_lab/fsw/tables/to_lab_sub.c often ships with every MID commented
# out → after TO_LAB_ENABLE, to_lab has nothing to forward (MOS live table empty
# even though NOOP/TC still works via ci_lab).
#
# Usage (WSL — does NOT touch ~/.nos3/42):
#   CFS_ROOT=~/cFS ./scripts/patch-cfs-to-lab-subs.sh
#   cd ~/cFS && make && make install
#   # Ctrl+C old core-cpu1, then:
#   cd ~/cFS/build/exe/cpu1 && ./core-cpu1
set -euo pipefail

CFS_ROOT="${CFS_ROOT:-$HOME/cFS}"
if [[ ! -d "$CFS_ROOT" ]]; then
  echo "CFS_ROOT not found: $CFS_ROOT" >&2
  exit 1
fi

# Prefer empty app table (usual culprit), then mission sample table.
CANDIDATES=(
  "$CFS_ROOT/apps/to_lab/fsw/tables/to_lab_sub.c"
  "$CFS_ROOT/sample_defs/tables/to_lab_sub.c"
)

TARGET=""
for f in "${CANDIDATES[@]}"; do
  if [[ -f "$f" ]]; then
    TARGET="$f"
    # If app table exists but already has uncommented WRAP_VALUE entries, still OK to refresh.
    break
  fi
done

if [[ -z "$TARGET" ]]; then
  echo "Could not find to_lab_sub.c under $CFS_ROOT" >&2
  printf '  missing: %s\n' "${CANDIDATES[@]}" >&2
  exit 1
fi

STAMP="$(date +%Y%m%d%H%M%S)"
BACKUP="${TARGET}.bak-mos-${STAMP}"
cp -a "$TARGET" "$BACKUP"
echo "Backup: $BACKUP"
echo "Patching: $TARGET"

# Preserve CFE_TBL_FILEDEF symbol name from the original file.
SYM="Subscriptions"
if grep -qE 'TO_LAB_Subs_t[[:space:]]+TO_LAB_Subs' "$BACKUP"; then
  SYM="TO_LAB_Subs"
elif grep -qE 'TO_LAB_Subs_t[[:space:]]+Subscriptions' "$BACKUP"; then
  SYM="Subscriptions"
elif grep -q 'CFE_TBL_FILEDEF(TO_LAB_Subs' "$BACKUP"; then
  SYM="TO_LAB_Subs"
fi

# Optional includes — only if headers exist in the tree.
OPTIONAL_INCLUDES=()
for inc in ci_lab_msgids.h sample_app_msgids.h; do
  if find "$CFS_ROOT" -name "$inc" 2>/dev/null | grep -q .; then
    OPTIONAL_INCLUDES+=("#include \"$inc\"")
  fi
done

{
  cat << EOF
/************************************************************************
 * MOS patch: minimal to_lab subscriptions for ground demos.
 * Backup: $(basename "$BACKUP")
 ************************************************************************/

#include "cfe_tbl_filedef.h"
#include "cfe_sb_api_typedefs.h"
#include "to_lab_tbl.h"
#include "cfe_msgids.h"
#include "to_lab_msgids.h"
EOF
  for line in "${OPTIONAL_INCLUDES[@]:-}"; do
    echo "$line"
  done
  cat << EOF

TO_LAB_Subs_t ${SYM} = {
    .Subs = {
        {CFE_SB_MSGID_WRAP_VALUE(TO_LAB_HK_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(TO_LAB_DATA_TYPES_MID), {0, 0}, 4},
EOF
  if find "$CFS_ROOT" -name 'ci_lab_msgids.h' 2>/dev/null | grep -q .; then
    echo '        {CFE_SB_MSGID_WRAP_VALUE(CI_LAB_HK_TLM_MID), {0, 0}, 4},'
  fi
  if find "$CFS_ROOT" -name 'sample_app_msgids.h' 2>/dev/null | grep -q .; then
    echo '        {CFE_SB_MSGID_WRAP_VALUE(SAMPLE_APP_HK_TLM_MID), {0, 0}, 4},'
  fi
  cat << EOF
        {CFE_SB_MSGID_WRAP_VALUE(CFE_ES_HK_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_EVS_HK_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_EVS_LONG_EVENT_MSG_MID), {0, 0}, 32},
        {CFE_SB_MSGID_RESERVED, {0, 0}, 0},
    }};

CFE_TBL_FILEDEF(${SYM}, TO_LAB.Subscriptions, TO Lab Sub Tbl, to_lab_sub.tbl)
EOF
} > "$TARGET"

echo "Wrote symbol ${SYM} with core + available lab/sample HK MIDs."
echo
echo "Next:"
echo "  cd \"$CFS_ROOT\""
echo "  sudo sysctl -w fs.mqueue.msg_max=256 fs.mqueue.msgsize_max=65536"
echo "  make && make install"
echo "  # restart core-cpu1, then translator (listens 1235+2234), then MOS"
echo
echo "Discover TM port your binary uses:"
echo "  rg -n 'DEFAULT_TO_LAB_MISSION_TLM_PORT|TLM_PORT' \"$CFS_ROOT/apps/to_lab\" \"$CFS_ROOT/build\" 2>/dev/null | head"
echo "  python3 \"$(cd "$(dirname "$0")" && pwd)/probe-cfs-tm.py\""
