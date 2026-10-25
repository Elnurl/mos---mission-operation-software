# MOS — Mission Operations Software

Phase 2 **Slice A**: Linux desktop ops client proving a single-spacecraft
TM → decode → UI → TC → ack path. Default path uses a **stub/sim** bridge;
optional **UDP** bridge aligns with NASA cFS lab ports (ci_lab / to_lab style)
and closes the loop via a local harness when full cFS is not running.

**Stack:** Qt 6 + C++ · SQLite · CMake  
**Out of scope (this phase):** procedures/checklists, multi-sat UI, Postgres, RF, SDA.

## Layout

```text
mos/
  CMakeLists.txt
  src/
    app/         # mos-desktop entry + Qt UI
    core/        # asset, session, TM/TC pipelines
    adapters/    # protocol pack (cFS-oriented MOST)
    bridge/      # ICfsBridge + StubCfsBridge + UdpCfsBridge + SimTmGenerator
    persist/     # SQLite schema + access
  tests/
  scripts/       # UDP harness, demo script, WSL cFS notes
```

## Dependencies (Linux)

Supported build hosts: native Linux, and **Windows 11 + WSL Ubuntu** (22.04+).

- CMake ≥ 3.21
- C++17 compiler (g++ / clang++) with libstdc++
- Qt 6: Core, Gui, Widgets, Sql, Network (SQLite driver)
- OpenGL headers/libs (required by Qt6 Gui / CMake `WrapOpenGL`)
- SQLite 3 (via Qt Sql + system lib)
- Python 3 (only for `scripts/cfs-udp-harness.py`)

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

## Run — UDP / cFS-port mode

Defaults match NASA cFS lab apps: **TC → :1234**, **TM ← :1235**.

```bash
# terminal A
python3 scripts/cfs-udp-harness.py

# terminal B
./build/src/app/mos-desktop --bridge=udp
```

Or:

```bash
chmod +x scripts/run-udp-demo.sh
./scripts/run-udp-demo.sh
```

Env knobs: `MOS_BRIDGE`, `MOS_CFS_TC_HOST`, `MOS_CFS_TC_PORT`, `MOS_CFS_TM_PORT`,
`MOS_CFS_TM_BIND`. Full WSL NASA cFS build steps and honesty about CCSDS gaps:
**[`scripts/cfs-sim-notes.md`](scripts/cfs-sim-notes.md)**.

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
| Live TM table (MOST frames) | Yes (stub or UDP) |
| TC compose + history/ack | Yes (stub or UDP+harness) |
| `ICfsBridge` + `StubCfsBridge` | Yes |
| `UdpCfsBridge` (ci/to-style ports) | Yes — proven vs harness |
| NASA cFS CCSDS ci_lab/to_lab bytes | Not yet — see scripts notes |

## What’s next

1. MOST↔CCSDS translator or real CCSDS pack for live `core-cpu1`.
2. TM history views / plots; keep single-sat UI until Phase 4 fleet work.
3. Verification demo for SYS-030…034 / SYS-050 against chosen cFS ground path.
