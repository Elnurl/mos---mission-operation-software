# cFS / UDP bridge bring-up (Slice A)

MOS keeps **`StubCfsBridge`** as the default offline demo and adds **`UdpCfsBridge`**
for a real socket path aligned with NASA cFS lab ground ports (**ci_lab** TC /
**to_lab** TM).

| Mode | How | Role |
| --- | --- | --- |
| `stub` (default) | `./build/src/app/mos-desktop` | In-process `SimTmGenerator` + fake ACKs |
| `udp` | `--bridge=udp` or `MOS_BRIDGE=udp` | UDP to harness or live cFS-side peer |

## Proven closed loop (no full cFS required)

Uses the same ports as stock ci_lab / to_lab defaults:

| Direction | Default | Env |
| --- | --- | --- |
| MOS → peer TC | `127.0.0.1:1234` | `MOS_CFS_TC_HOST`, `MOS_CFS_TC_PORT` |
| Peer → MOS TM/ACK | MOS binds `:1235` | `MOS_CFS_TM_PORT`, `MOS_CFS_TM_BIND` |

**Wire payload (Slice A):**

- **TM:** binary MOST frame (`0x4D4F5354` + version + seq + floats + mode) — same as `SimTmGenerator` / `CfsAdapter`
- **TC:** UTF-8 `commandId|mnemonic|argsJson` — same as stub encode
- **ACK:** UTF-8 `MOSACK|commandId|1|detail` (or `0` for fail)

### One-shot demo (WSL Ubuntu)

```bash
# terminal A — cFS-side peer
python3 scripts/cfs-udp-harness.py

# terminal B — MOS
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/src/app/mos-desktop --bridge=udp
```

Or combined:

```bash
chmod +x scripts/run-udp-demo.sh
./scripts/run-udp-demo.sh
```

You should see live TM in the UI and Send NOOP → Queued → Sent → Acked (`harness accepted …`).

### Headless smoke

```bash
python3 scripts/cfs-udp-harness.py &
HPID=$!
QT_QPA_PLATFORM=offscreen MOS_BRIDGE=udp timeout 3 ./build/src/app/mos-desktop --bridge=udp || true
kill $HPID
```

---

## WSL Ubuntu: build & run NASA cFS (owner machine)

Target: **Windows 11 + WSL Ubuntu 22.04+**. Run these **inside WSL**, not PowerShell.

### 1. Packages

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake git
```

### 2. Clone NASA cFS bundle

```bash
cd ~
git clone --recurse-submodules https://github.com/nasa/cFS.git
cd cFS
cp cfe/cmake/Makefile.sample Makefile
cp -r cfe/cmake/sample_defs sample_defs
```

### 3. Message-queue limits (common WSL failure)

If `core-cpu1` dies with `OS_QueueCreate` / `Invalid argument`:

```bash
sudo sysctl -w fs.mqueue.msg_max=256
sudo sysctl -w fs.mqueue.msgsize_max=65536
# optional persist in /etc/sysctl.conf then: sudo sysctl -p
```

### 4. Build (native sim)

```bash
make distclean          # optional
make SIMULATION=native prep
make
make install
```

### 5. Run cpu1

```bash
cd build/exe/cpu1
./core-cpu1
```

Stock sample apps include **ci_lab** (commands in) and **to_lab** (telemetry out):

| App | Typical UDP role | Default port |
| --- | --- | --- |
| `ci_lab` | listens for ground commands | **1234** |
| `to_lab` | sends TM to ground after Enable Tlm | **1235** |

Enable TO from the cFS ground tools / cmdUt (send `TO_LAB_ENABLE` with destination IP/port of the MOS host). Ensure `to_lab` subscription table includes the MIDs you care about (empty/commented tables = no TM).

### 6. Docker alternative (optional)

Build/run cFS in Docker on WSL with ports published, e.g. `-p 1234:1234/udp -p 1235:1235/udp`,
and the same `make SIMULATION=native prep && make && make install` flow inside the image.
See community write-ups (e.g. Jude Labs cFS-on-WSL/Docker) if you prefer containers.

---

## Honest gaps (what is / is not proven)

| Item | Status |
| --- | --- |
| Stub desktop demo unchanged | **Proven** (default `stub`) |
| `UdpCfsBridge` bind/send/recv + MOSACK | **Proven** against `cfs-udp-harness.py` |
| End-to-end UI TM + TC ack over UDP | **Proven** with harness (local/script) |
| NASA `core-cpu1` + raw ci_lab/to_lab CCSDS ↔ MOS MOST | **Not yet** — different packet formats |
| Full cFS in CI | **Out of scope** this increment (heavy) |

**Next bridge work:** either (a) a thin UDP translator MOST↔CCSDS CI/TO, or (b) a real CCSDS pack in `CfsAdapter` speaking ci_lab/to_lab bytes directly. Ports and `ICfsBridge` stay stable either way.

Point MOS at a translator the same way as the harness:

```bash
export MOS_BRIDGE=udp
export MOS_CFS_TC_HOST=127.0.0.1
export MOS_CFS_TC_PORT=1234
export MOS_CFS_TM_PORT=1235
./build/src/app/mos-desktop --bridge=udp
```
