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
| Ground → `ci_lab` TC | **1234** | **1234** (`CI_LAB_MISSION_BASE_UDP_PORT`) | cFS owns this |
| `to_lab` → ground TM | **1235** (many tutorials) | **2234** (`TO_LAB_MISSION_TLM_PORT`) | listen where *your build* sends |
| MOS ↔ harness (MOST) | TC **1234** / TM **1235** | same | harness owns 1234 |
| MOS ↔ translator (MOST) | TC **1334** / TM **1335** | same | avoids clashing with ci_lab |

If TM never appears against live `core-cpu1`, check the to_lab port in your tree
(`grep TLM_PORT ~/cFS/...`) and set `MOS_CFS_TM_LISTEN` / `--cfs-tm-port` accordingly
(try **1235** then **2234**).

---

## A. Stub (default — always works)

```bash
./build/src/app/mos-desktop
```

## B. UDP harness (MOST closed loop — no cFS)

Uses the same ports as stock ci_lab / to_lab *tutorials* (1234/1235) with **MOST**
payloads (not CCSDS):

```bash
# terminal A
python3 scripts/cfs-udp-harness.py

# terminal B
./build/src/app/mos-desktop --bridge=udp
```

Or: `./scripts/run-udp-demo.sh`

**Wire payload:**

- **TM:** binary MOST (`0x4D4F5354` + version + seq + floats + mode)
- **TC:** UTF-8 `commandId|mnemonic|argsJson`
- **ACK:** UTF-8 `MOSACK|commandId|1|detail`

## C. Live NASA cFS (recommended: translator)

`ci_lab` / `to_lab` speak **CCSDS**, not MOST. MOS-8 adds
`scripts/most-ccsds-translator.py` so MOS can keep its stub/MOST UI path while
the translator owns the lab bytes.

### Owner machine — two terminals (+ translator)

**Every new WSL session** (message queues):

```bash
sudo sysctl -w fs.mqueue.msg_max=256
sudo sysctl -w fs.mqueue.msgsize_max=65536
```

**Terminal 1 — cFS (leave running):**

```bash
cd ~/cFS/build/exe/cpu1
./core-cpu1
```

Expect SBN / TO_LAB lines. Do **not** reinstall cFS or touch `~/.nos3/42`.

**Terminal 2 — translator:**

```bash
cd ~/mos---mission-operation-software
python3 scripts/most-ccsds-translator.py \
  --cfs-tm-port 1235 \
  --enable-dest-ip 127.0.0.1
# If no TM: retry with --cfs-tm-port 2234
```

On start the translator sends **TO_LAB OUTPUT_ENABLE** (CC=6) to ci_lab `:1234`
with `dest_IP=127.0.0.1`, then best-effort **ADD_PKT** for `0x0880` / `0x0883`.

**Terminal 3 — MOS:**

```bash
cd ~/mos---mission-operation-software
export QT_QPA_PLATFORM=xcb   # if needed under WSLg/XLaunch
export MOS_BRIDGE=udp
export MOS_CFS_TC_PORT=1334
export MOS_CFS_TM_PORT=1335
./build/src/app/mos-desktop --bridge=udp
```

Or combined (peer stand-in when cFS is not up):

```bash
MOS_USE_LAB_PEER=1 ./scripts/run-live-cfs-path.sh
# with real core-cpu1 already running:
./scripts/run-live-cfs-path.sh
```

**UI commands that map through the translator:**

| Mnemonic | CCSDS |
| --- | --- |
| `NOOP` | SAMPLE_APP NOOP (`0x1882` CC=0) |
| `TO_ENABLE` / `ENABLE_TLM` | TO_LAB OUTPUT_ENABLE (`0x1880` CC=6) |
| `TO_NOOP` | TO_LAB NOOP |

### TO_LAB subscriptions (important)

Stock `to_lab` subscription tables often ship with **all MIDs commented out**.
ENABLE alone does not create traffic if nothing is subscribed.

Options:

1. Uncomment entries in `to_lab` sub table / rebuild install (owner-side), or
2. Rely on translator `--add-mid` (best-effort; MsgId layout varies by cFE age), or
3. Use `MOS_CFS_WIRE=ccsds` and send `TO_ENABLE` from MOS after fixing the table.

### Prove without cFS (CI / cloud)

```bash
python3 scripts/ccsds_lab_codec.py          # codec self-check
python3 scripts/ccsds-lab-peer.py --auto-tm &
python3 scripts/most-ccsds-translator.py --no-auto-enable &
# then MOS with MOS_CFS_TC_PORT=1334 MOS_CFS_TM_PORT=1335 --bridge=udp
MOS_USE_LAB_PEER=1 ./scripts/run-live-cfs-path.sh
```

## D. Alternate: direct CCSDS wire (no translator)

MOS binds **1235**, sends to **1234**, packs lab CCSDS itself:

```bash
# core-cpu1 already running; to_lab will send to MOS:1235 after ENABLE
export MOS_BRIDGE=udp
export MOS_CFS_WIRE=ccsds
export MOS_CFS_TC_PORT=1234
export MOS_CFS_TM_PORT=1235
export MOS_TO_DEST_IP=127.0.0.1
# MOS_CFS_AUTO_ENABLE=0 to skip automatic TO_LAB_ENABLE
./build/src/app/mos-desktop --bridge=udp
```

`CfsAdapter` decodes raw CCSDS into `CCSDS_MID` / `SEQ` / … (and fills familiar
columns diagnostically). ACKs are **synthetic** (UDP send success), not FSW
verification. Prefer the translator when you want MOSACK + MOST UI continuity.

---

## WSL Ubuntu: build NASA cFS (reference only)

Owner already has `~/cFS` built — do **not** reinstall for daily use. Full
install notes live in the Project store `docs/cfs-wsl-install.md`. Summary:

```bash
cd ~/cFS
make SIMULATION=native MISSIONCONFIG=sample prep   # only if needed
make && make install
cd build/exe/cpu1 && ./core-cpu1
```

`MISSIONCONFIG` is the prefix before `_defs` → **`sample`**, not `sample_defs`.

---

## Honest gaps (proven vs not)

| Item | Status |
| --- | --- |
| Stub desktop demo | **Proven** (default) |
| `UdpCfsBridge` + MOST harness + MOSACK | **Proven** |
| MOST↔CCSDS translator codec + lab peer loop | **Proven** in-repo (no full cFS required) |
| Auto `TO_LAB_ENABLE` + mnemonic→CCSDS map | **Proven** (codec/unit tests + peer) |
| Live `core-cpu1` on owner WSL | **Owner verifies** — agent has no `~/cFS` |
| Full HK field decode (BUS_V etc. from flight) | **Gap** — diagnostic APID/LEN mapping only |
| `ADD_PKT` on all cFE MsgId layouts | **Gap** — prefer editing `to_lab` sub table |
| Modern default TM port **2234** vs **1235** | **Config** — set `--cfs-tm-port` to match build |
| Full cFS in CI | **Out of scope** |
