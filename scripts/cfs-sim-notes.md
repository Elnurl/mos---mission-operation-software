# cFS sim bring-up notes (Slice A)

Slice A currently uses **`StubCfsBridge` + `SimTmGenerator`** so the desktop TM/TC loop
works without a live cFS instance. The bridge interface (`ICfsBridge`) is the swap point.

## Next: real wire-up

1. Choose the cFS ground interface (common options: CI/TO UDP, custom socket, file drop).
2. Implement `RealCfsBridge : public ICfsBridge` in `src/bridge/`.
3. Map inbound TM bytes → existing `CfsAdapter` (or a real CCSDS/cFS pack) via `TmPipeline`.
4. Map outbound encoded TC from `CfsAdapter::encodeTelecommand` → CI/command ingress.
5. Translate cFS command status / HK ack into `commandAck(...)`.
6. Keep `StubCfsBridge` behind a runtime flag (`MOS_BRIDGE=stub|cfs`) for offline demos.

## Suggested local tools

```bash
# Example only — adjust to your cFS checkout / NASA cFS sample apps
# export CFS_DIR=/path/to/cFS
# cd "$CFS_DIR" && make
# ./core-cpu1   # or your mission target
```

Document the exact ports and APID map here once the spike lands.
