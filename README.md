# MOS — Mission Operations Software

Phase 2 **Slice A** scaffold: Linux desktop ops client proving a single-spacecraft
TM → decode → UI → TC → ack path against a **stub/sim cFS bridge**, with clear
boundaries for a real cFS wire-up.

**Stack:** Qt 6 + C++ · SQLite · CMake  
**Out of scope (this phase):** procedures/checklists, multi-sat UI, Postgres, RF, SDA.

## Layout

```text
mos/
  CMakeLists.txt
  src/
    app/         # mos-desktop entry + Qt UI
    core/        # asset, session, TM/TC pipelines
    adapters/    # protocol pack (cFS-oriented stub)
    bridge/      # ICfsBridge + StubCfsBridge + SimTmGenerator
    persist/     # SQLite schema + access
  tests/
  scripts/       # cFS sim bring-up notes
```

## Dependencies (Linux)

Supported build hosts: native Linux, and **Windows 11 + WSL Ubuntu** (22.04+).

- CMake ≥ 3.21
- C++17 compiler (g++ / clang++) with libstdc++
- Qt 6: Core, Gui, Widgets, Sql (SQLite driver)
- OpenGL headers/libs (required by Qt6 Gui / CMake `WrapOpenGL`)
- SQLite 3 (via Qt Sql + system lib)

### Debian / Ubuntu (incl. WSL)

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build g++ \
  qt6-base-dev qt6-base-dev-tools libqt6sql6-sqlite libsqlite3-dev \
  libgl1-mesa-dev libopengl-dev
```

If CMake fails with `Could NOT find OpenGL (missing: OPENGL_INCLUDE_DIR)` or
`WrapOpenGL` / Qt6 Gui not found, install the Mesa/OpenGL packages above and
reconfigure (`rm -rf build && cmake -S . -B build …`).

### Windows 11 + WSL display

Building inside WSL Ubuntu is supported. Running the GUI needs a display server:

- Prefer **WSLg** (Windows 11 default for recent WSL) — `./build/src/app/mos-desktop` should open a window with no extra setup.
- If WSLg is unavailable, use **VcXsrv / XLaunch** (or another X server), set `export DISPLAY=:0` (or your X server’s address), then run `mos-desktop`.

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

## Run

```bash
./build/src/app/mos-desktop
```

On first launch MOS creates `~/.local/share/MOS/mos.sqlite` (XDG app data),
bootstraps asset **SC-1**, starts the stub bridge, and streams simulated TM into
the live table. Compose a telecommand (default `NOOP`) and Send — status moves
Queued → Sent → Acked via the stub.

Headless smoke (no display):

```bash
QT_QPA_PLATFORM=offscreen timeout 3 ./build/src/app/mos-desktop || true
```

## What works now

| Piece | Status |
| --- | --- |
| Asset SC-1 in SQLite config | Yes |
| Live TM table (sim MOST frames) | Yes |
| TC compose + history/ack | Yes (stub) |
| `ICfsBridge` separation | Yes |
| Real cFS sockets / CI-TO | **Not yet** — see `scripts/cfs-sim-notes.md` |

## What’s next

1. Implement a real `ICfsBridge` against your cFS ground interface.
2. Replace stub encode/decode with a versioned mission/cFS pack as needed.
3. TM history views / plots; keep single-sat UI until Phase 4 fleet work.
4. Verification demo for SYS-030…034 / SYS-050 once the live bridge is up.
