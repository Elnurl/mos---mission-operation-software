# MOS — Mission Operations Software

Phase 2 **Slice A**: Linux desktop ops client proving a single-spacecraft
TM → decode → UI → TC → ack path. Default path uses a **stub/sim** bridge;
optional **UDP** bridge aligns with NASA cFS lab ports (ci_lab / to_lab) via a
MOST harness or a **MOST↔CCSDS translator** toward live `core-cpu1`.

**Stack:** Qt 6 + C++ · SQLite · CMake  
**Out of scope (this phase):** procedures/checklists, multi-sat UI, Postgres, RF, SDA.

## Layout

```text
mos/
  CMakeLists.txt
  src/
    app/         # mos-desktop entry + Qt UI
    core/        # asset, session, TM/TC pipelines
    adapters/    # protocol pack (MOST + raw CCSDS lab TM)
    bridge/      # ICfsBridge + Stub/Udp + CcsdsLab helpers
    persist/     # SQLite schema + access
  tests/
  scripts/       # UDP harness, MOST↔CCSDS translator, WSL cFS notes
```

## Dependencies (Linux)

Supported build hosts: native Linux, and **Windows 11 + WSL Ubuntu** (22.04+).

- CMake ≥ 3.21
- C++17 compiler (g++ / clang++) with libstdc++
- Qt 6: Core, Gui, Widgets, Sql, Network (SQLite driver)
- OpenGL headers/libs (required by Qt6 Gui / CMake `WrapOpenGL`)
- SQLite 3 (via Qt Sql + system lib)
- Python 3 (harness / translator / lab peer)

### Debian / Ubuntu (incl. WSL)

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build g++ \
  qt6-base-dev qt6-base-dev-tools libqt6sql6-sqlite libsqlite3-dev \
  libgl1-mesa-dev libopengl-dev python3
```

If CMake fails with `Could NOT find OpenGL (missing: OPENGL_INCLUDE_DIR)` or
`WrapOpenGL` / Qt6 Gui not found, install the Mesa/OpenGL packages above and
reconfigure (`rm -rf build && cmake -S . -B build …`).

### Windows 11 + WSL display

Building inside WSL Ubuntu is supported. Running the GUI needs a display server:

- Prefer **WSLg** (Windows 11 default for recent WSL) — `./build/src/app/mos-desktop` should open a window with no extra setup.
- If WSLg is unavailable, use **VcXsrv / XLaunch** (or another X server), set `export DISPLAY=:0` (or your X server’s address), then run `mos-desktop`.
- If you see `qt.qpa.plugin: Could not find the Qt platform plugin "wayland"`, it is usually harmless under X11/WSLg. To silence it, force xcb:

```bash
export QT_QPA_PLATFORM=xcb
./build/src/app/mos-desktop
```

Headless configure/build and `QT_QPA_PLATFORM=offscreen` smoke tests do not need a display.

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Tests:

```bash
ctest --test-dir build --output-on-failure
# or: ./build/tests/mos_tests
```

## Run — stub mode (default)

```bash
./build/src/app/mos-desktop
# equivalent: MOS_BRIDGE=stub ./build/src/app/mos-desktop --bridge=stub
```

On first launch MOS creates `~/.local/share/MOS/mos.sqlite` (XDG app data),
bootstraps asset **SC-1**, starts the stub bridge, and streams simulated TM into
the live table. Compose a telecommand (default `NOOP`) and Send — status moves
Queued → Sent → Acked via the stub.

## Run — UDP harness (MOST, no cFS)

Defaults match classic lab ports: **TC → :1234**, **TM ← :1235** (MOST payloads).

```bash
# terminal A
python3 scripts/cfs-udp-harness.py

# terminal B
./build/src/app/mos-desktop --bridge=udp
```

Or: `./scripts/run-udp-demo.sh`

## Run — live NASA cFS (translator)

cFS `ci_lab`/`to_lab` use **CCSDS**. Keep MOS on MOST and run the translator
(remapped ports so MOS does not fight ci_lab for :1234).

**WSL every session:**

```bash
sudo sysctl -w fs.mqueue.msg_max=256
sudo sysctl -w fs.mqueue.msgsize_max=65536
```

**Terminal 1 — cFS** (already built at `~/cFS`; do not touch `~/.nos3/42`):

```bash
cd ~/cFS/build/exe/cpu1
./core-cpu1
```

**Terminal 2 — translator** (sends `TO_LAB_ENABLE` on start):

```bash
python3 scripts/most-ccsds-translator.py --cfs-tm-port 1235
# no TM? try --cfs-tm-port 2234 (modern to_lab default)
```

**Terminal 3 — MOS:**

```bash
export QT_QPA_PLATFORM=xcb
export MOS_CFS_TC_PORT=1334
export MOS_CFS_TM_PORT=1335
./build/src/app/mos-desktop --bridge=udp
```

Helper: `./scripts/run-live-cfs-path.sh` (add `MOS_USE_LAB_PEER=1` to use the
in-repo CCSDS peer instead of live `core-cpu1`).

Full port table, TO_LAB subscription notes, and proven-vs-gap:
**[`scripts/cfs-sim-notes.md`](scripts/cfs-sim-notes.md)**.

Env knobs: `MOS_BRIDGE`, `MOS_CFS_TC_HOST`, `MOS_CFS_TC_PORT`, `MOS_CFS_TM_PORT`,
`MOS_CFS_TM_BIND`, `MOS_CFS_WIRE=ccsds` (direct CCSDS alternate),
`MOS_CFS_AUTO_ENABLE`, `MOS_TO_DEST_IP`.

**Local DB path:** `~/.local/share/MOS/mos.sqlite`  
If you upgraded from an earlier Slice A build and see repeated
`TM error: NOT NULL constraint failed: tm_samples.unit`, quit the app and remove
the DB once so schema/data are recreated cleanly:

```bash
rm -f ~/.local/share/MOS/mos.sqlite
```

Headless smoke (no display):

```bash
QT_QPA_PLATFORM=offscreen timeout 3 ./build/src/app/mos-desktop || true
```

## What works now

| Piece | Status |
| --- | --- |
| Asset SC-1 in SQLite config | Yes |
| Live TM table (MOST frames) | Yes (stub / UDP harness / translator) |
| TC compose + history/ack | Yes (stub / UDP+harness / translator MOSACK) |
| `ICfsBridge` + `StubCfsBridge` | Yes |
| `UdpCfsBridge` (ci/to-style ports) | Yes — proven vs harness |
| MOST↔CCSDS translator + lab peer | Yes — in-repo proof path |
| Raw CCSDS TM decode in `CfsAdapter` | Yes (diagnostic fields) |
| Live `core-cpu1` on owner WSL | Owner run — see notes for gaps |

## What’s next

1. Richer HK decode once flight MIDs/subscriptions are fixed on the cFS side.
2. TM history views / plots; keep single-sat UI until Phase 4 fleet work.
3. Verification demo for SYS-030…034 / SYS-050 against chosen cFS ground path.
4. Later: NOS3/42 richer mission sim alongside cFS (do not replace MOS↔cFS).
