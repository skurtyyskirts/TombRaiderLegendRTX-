# Builds 001–015 — Baseline & Hash Stability

> The first epoch: prove the proxy chain works, prove geometry hashes stay stable across normal play. **Builds 003–015 are not preserved** — what survives is 001 and 002 plus the lessons folded into the build-016 baseline.

## Context

Before build 001 there was no proxy. The project had been characterizing TRL's render architecture (cdcEngine is fully shader-driven, no FFP, transforms live in VS constants) and building the FFP proxy template. Build 001 was the first end-to-end "does this chain load and does Remix see anything?" test.

The two PASS builds in this range established the project's two baseline invariants:

- **Invariant A (build 001):** The proxy can chain to Remix, recover W/V/P from VS constants, feed them to `SetTransform`, and produce geometry that Remix can see and hash.
- **Invariant B (build 002):** Asset hashes are stable frame-to-frame and across normal camera motion. Generation hashes flicker (expected — they include vertex layout and vertex shader, which change).

These invariants are reasserted by every build that follows. Loss of either is a regression and triggers immediate investigation.

## Build 001 — Baseline passthrough

**Status:** PASS

**Configuration:**
- Shader passthrough + transform override
- `rtx.geometryAssetHashRuleString = indices,texcoords,geometrydescriptor` (no `positions` yet)
- `ENABLE_SKINNING = 0`
- Frustum threshold patched to `1e30f`
- `RET` at `0x407150` (the version of the patch that was later shown to skip submission body)

**Result:** Asset hashes stable across A/D strafe and across sessions in the Bolivia cave. 1,440 draws per 120-frame batch.

**Discoveries:**
- View matrix is reliably readable at `0x010FC780`
- Projection matrix is reliably readable at `0x01002530`
- `vpValid=1` always (the recovered ViewProj is always valid)
- A simple `RET` at `0x407150` is enough to disable culling for this very limited scene

**Why this was later revised:** The RET at `0x407150` skips the function's actual submission logic, capping draws at ~1,440. This was acceptable for hash-stability testing but masked all upstream culling. Build 039 removed the RET and got draws to ~93K-180K.

## Build 002 — Stable hash

**Status:** PASS

**Configuration:** Two-phase test introduced — debug view 277 (hash-debug colored geometry) + clean RTX render. No proxy code changes.

**Result:** Asset hashes stable frame-to-frame **and** across camera movement. Generation hash still flickers because it includes positions and many TRL meshes share index/texcoord layouts with slightly different positions.

**Why this test framework matters:** The two-phase pattern (debug-view colors for hash stability + clean render for visual confirmation of lights) became the project's standard test workflow through build 045. The later builds (064+) replaced it with the hash-stability-test pattern (camera pan only, no WASD), but the same two-phase structure.

## Builds 003–015 — Not preserved

Thirteen consecutive builds, none archived. Inferred from CHANGELOG.md and CLAUDE.md context:

- Investigation of vertex declaration formats (FVF vs declarations)
- First identification of SHORT4 normalized vertex positions
- First attempts at the SHORT4 → FLOAT3 expansion path
- Discovery of the bone-matrix register range (c48+)
- Iteration on the `proxy.ini` configuration surface
- Several proxy crashes that prevented archiving

The lessons from this period are folded into the build-016 codebase. The non-preservation rule of the project (archive every build) was established **after** this stretch, in response to it.

## What persisted forward from this epoch

- The two-phase test workflow
- The VS constant register layout (c0–c3 World, c8–c11 View, c12–c15 Proj, c48+ bones)
- The view/proj matrix global addresses (`0x010FC780`, `0x01002530`)
- The `vpValid=1` invariant
- The `g_pEngineRoot → +0x214 → TRLRenderer* → +0x0C → IDirect3DDevice9*` chain (formally mapped build 021 but discovered here)
- The convention that asset hash uses `indices,texcoords,geometrydescriptor` — later corrected by build 047's dead end to require `positions` as well

## Open issues at end of epoch

- Geometry visible in Bolivia cave only; other levels had not been tested
- Movement input delivery not yet correct (the macro pipeline was working in the Bolivia cave because the cave has minimal interactive content; failed in Croft Manor)
- No anchor mesh hashes identified; lighting tests not yet meaningful

These are picked up in [[Build-016-to-044-Anti-Culling]].

## See also

- [[Build-History-Index]] — full one-line-per-build table
- [[Hash-Stability]] — why generation hashes flicker
- [[VS-Constant-Register-Layout]] — the c0–c96 map established here
- [[Transform-Matrices]] — WVP recovery math
