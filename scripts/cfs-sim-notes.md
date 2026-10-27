# cFS / UDP bridge bring-up (Slice A → MOS-8)

MOS keeps **`StubCfsBridge`** as the default offline demo and **`UdpCfsBridge`**
for a real socket path aligned with NASA cFS lab ground ports (**ci_lab** TC /
**to_lab** TM).

| Mode | How | Role |
| --- | --- | --- |
| `stub` (default) | `./build/src/app/mos-desktop` | In-process `SimTmGenerator` + fake ACKs |
| `udp` + harness | `--bridge=udp` + `cfs-udp-harness.py` | MOST over 1234/1235 (no cFS) |
| `udp` + translator | `--bridge=udp` + `most-ccsds-translator.py` | **Honest live-cFS path** (MOST↔CCSDS) |
| `udp` + `MOS_CFS_WIRE=ccsds` | direct CCSDS on 1234/1235 | Alternate: MOS packs lab CCSDS itself |

## Ports

| Direction | Classic lab | Modern upstream `to_lab` default | MOS / translator |
| --- | --- | --- | --- |
| Ground → `ci_lab` TC | **1234** | **1234** | cFS owns this |
| `to_lab` → ground TM | **1235** (many tutorials) | **2234** (`TO_LAB_MISSION_TLM_PORT`) | translator listens **both** by default |
| MOS ↔ harness (MOST) | TC **1234** / TM **1235** | same | harness owns 1234 |
| MOS ↔ translator (MOST) | TC **1334** / TM **1335** | same | avoids clashing with ci_lab |

---

## Why TC can work while the live TM table is empty

| Symptom | Cause | Fix |
| --- | --- | --- |
| NOOP → Acked, TM table blank | `to_lab` subscription table empty (stock comments out all MIDs) | `scripts/patch-cfs-to-lab-subs.sh` → `make && make install` → restart `core-cpu1` |
| `Subscribe Err: Bad Arg` on ADD_PKT | Wrong MsgId wire layout for your cFE | **Do not** use `--add-pkt` until sure; prefer the patch script |
| Translator silent, wrong port | Build sends to **2234**, listener was only **1235** (or vice versa) | Translator now listens **1235,2234**; or `python3 scripts/probe-cfs-tm.py` |
| ENABLE never sent / wrong dest IP | to_lab still `Awaiting enable` | Translator auto-sends `TO_LAB_ENABLE` to `127.0.0.1` |

ENABLE alone does **not** create traffic if nothing is subscribed.

---

## A. Stub (default — always works)

```bash
./build/src/app/mos-desktop
```

## B. UDP harness (MOST closed loop — no cFS)

```bash
python3 scripts/cfs-udp-harness.py          # terminal A
./build/src/app/mos-desktop --bridge=udp    # terminal B
```

Or: `./scripts/run-udp-demo.sh`

## C. Live NASA cFS — steps that produce **visible TM**

### C0. One-time subscription patch (owner WSL)

```bash
cd ~/mos---mission-operation-software
CFS_ROOT=~/cFS ./scripts/patch-cfs-to-lab-subs.sh
cd ~/cFS
sudo sysctl -w fs.mqueue.msg_max=256 fs.mqueue.msgsize_max=65536
make && make install
# Ctrl+C any running core-cpu1, then start fresh below
```

Do **not** touch `~/.nos3/42`.

### C1. Guaranteed non-empty live table (`--demo-tm`)

Use this when NOOP Acks but the TM table is blank — proves MOS ports + decode.

**Terminal 1 — cFS:** `cd ~/cFS/build/exe/cpu1 && ./core-cpu1`

**Terminal 2 — translator:**

```bash
cd ~/mos---mission-operation-software
python3 scripts/most-ccsds-translator.py --demo-tm
```

**Terminal 3 — MOS (these env vars are required):**

```bash
export QT_QPA_PLATFORM=xcb
export MOS_CFS_TC_PORT=1334
export MOS_CFS_TM_PORT=1335
./build/src/app/mos-desktop --bridge=udp
```

Expect translator lines `MOST#… demo-tm → 127.0.0.1:1335` and a filling live table within ~1s.

### C2. Real cFS TM (after C0)

Same terminals without `--demo-tm`. Success:

```text
[xlat] MOST#1 from-ccsds mid=0x…. → 127.0.0.1:1335
```

If you only see `still no CCSDS TM` → C0 not applied or `core-cpu1` not restarted after `make install`.  
Mnemonics: `NOOP`, `TO_ENABLE`, `TO_SEND_DATA_TYPES`, `TO_NOOP`.

**Common footgun:** MOS started without `MOS_CFS_TM_PORT=1335` still binds **1235**; translator publishes MOST to **1335** → empty table while ACKs can look fine if ports were partially correct. Always export both 1334/1335.
### C3. Diagnostics

```bash
# With only core-cpu1 running (stop translator so ports are free):
python3 scripts/probe-cfs-tm.py
# Then from another shell, ENABLE:
python3 -c "import socket,sys; sys.path.insert(0,'scripts'); from ccsds_lab_codec import pack_to_lab_enable; \
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); s.sendto(pack_to_lab_enable('127.0.0.1'),('127.0.0.1',1234))"
```

Optional runtime ADD_PKT (often causes Bad Arg — last resort):

```bash
python3 scripts/most-ccsds-translator.py --add-pkt --add-pkt-layout msgid32_le
```

### C4. Combined helper

```bash
MOS_USE_LAB_PEER=1 MOS_XLAT_DEMO_TM=1 ./scripts/run-live-cfs-path.sh
# real core-cpu1 already up:
MOS_XLAT_DEMO_TM=1 ./scripts/run-live-cfs-path.sh
```

## D. Alternate: direct CCSDS wire (no translator)

```bash
export MOS_BRIDGE=udp MOS_CFS_WIRE=ccsds
export MOS_CFS_TC_PORT=1234 MOS_CFS_TM_PORT=1235   # or 2234 for TM bind
export MOS_TO_DEST_IP=127.0.0.1
./build/src/app/mos-desktop --bridge=udp
```

Still needs a non-empty to_lab subscription table. ACKs are synthetic.

---

## Honest gaps (proven vs not)

| Item | Status |
| --- | --- |
| Stub + MOST harness | **Proven** |
| Translator MOST↔CCSDS + lab peer | **Proven** in-repo |
| Dual TM listen 1235+2234 + SEND_DATA_TYPES | **In this increment** |
| Subscription patch script for owner `~/cFS` | **Delivered** — owner runs rebuild |
| Live `core-cpu1` TM on owner WSL | **Owner verifies** after C0+C1 |
| Full flight HK engineering units | **Gap** — diagnostic APID/LEN→MOST columns |
| ADD_PKT on all cFE MsgId layouts | **Fragile** — prefer table patch |
| Full cFS in CI | **Out of scope** |
