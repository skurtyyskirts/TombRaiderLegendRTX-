# TRL RTX Remix — Test Status Report

**Last reviewed:** 2026-03-28
**Builds reviewed:** 001, 002, 016–033, 035–042, 044 (003–015, 034, 043 not preserved)
**Overall status:** FAILING — anchor geometry disappears at distance; all identified culling paths exhausted; `TerrainDrawable (0x40ACF0)` is prime suspect.

---

## Current Findings

### What Works

1. **Asset hash stability (static + moving camera):** Hash rule `indices,texcoords,geometrydescriptor` produces stable, session-reproducible hashes. Lara's character model is rock-solid across all positions and sessions. World geometry hashes are stable since sector visibility patches were added (build 028+).

2. **Proxy transform pipeline:** View/Proj matrices read from game memory (`0x010FC780`, `0x01002530`), World computed via WVP decomposition. 100% of draws processed (`passthrough=0`, `xformBlocked=0`, `vpValid=1`) in all recent builds.

3. **Geometry culling exhausted (22 layers mapped, 19+ patched):**
   - Frustum threshold stamped to -1e30 per BeginScene (`0xEFDD64`)
   - Per-object frustum function RETed at `0x407150`
   - 11 scene traversal cull branches NOPed inside `0x407150`
   - Backface culling forced to `D3DCULL_NONE`
   - Cull mode globals stamped (`0xF2A0D4/D8/DC`)
   - Sector/portal visibility gates NOPed (`0x46C194`, `0x46C19D`) — 65× draw count increase
   - Camera-sector proximity filter NOPed (`0x46B85A` in `RenderSector`)
   - Far clip distance stamped to 1e30f per BeginScene (`0x10FC910`)

4. **Per-light culling gates exhausted:**
   - Light frustum 6-plane test NOPed (`0x60CE20`)
   - Light broad-visibility test NOPed (`0x60CDE2`)
   - `Light_VisibilityTest` patched → always TRUE (`0x60B050 → mov al,1; ret 4`, build 031)
   - Sector light count gate NOPed (`0xEC6337`) inside `FUN_00EC62A0`, confirmed build 035
   - RenderLights gate NOPed (`0x60E3B1`, build 037)
   - Sector light count clear NOPed (`0x603AE6`, build 037)

5. **Automated test pipeline:** Two-phase (hash debug + clean render), randomized movement, scancode input delivery confirmed working since build 018.

### What Fails

1. **Both stage lights disappear when Lara walks away from the stage.** Root cause reframed in build 038: the "red light at distance" in earlier builds was the RTX fallback light (`rtx.fallbackLightRadiance`). With a neutral fallback, both lights vanish — the problem is anchor geometry not being submitted as draw calls, not light culling functions.

2. **TerrainDrawable path unexplored.** All three geometry render paths in `RenderFrame (0x450B00)` have been patched: `RenderVisibleSectors → RenderSector`, `SceneTraversal wrapper → 0x407150`, and the moveable object loop at `0x40E2C0`. Anchor geometry still disappears. The separate `TerrainDrawable (0x40ACF0) / TERRAIN_DrawUnits` path has its own culling and has not been touched.

3. **LOD alpha fade unpatched.** `0x446580` with 10 callers may fade geometry invisible at distance. Low priority but unexplored.

### Hurdles

1. **Anchor geometry, not light culling.** Build 038 conclusively showed both lights vanish — Remix lights anchor to geometry draw calls. When the anchor mesh draw call isn't submitted, the Remix light disappears. Patching engine light functions is irrelevant.

2. **Three render paths all patched, still fails.** Upstream caller analysis (build 044) identified all three geometry render paths and applied patches to each. The failing culling must be in a fourth path (`TerrainDrawable`) or a mechanism affecting the sector object lists before render dispatch.

3. **dx9tracer frame diff not yet done.** A near-vs-far frame capture would definitively show which draw calls (and which geometry hashes) disappear at distance — confirming whether the anchor mesh is terrain or instance geometry.

---

## Build-by-Build Summary

### Phase 1: Baseline & Hash Stability

| Build | Result | Key Change | Key Finding |
|-------|--------|------------|-------------|
| 001 | PASS | Baseline passthrough + transform override | Hashes stable (static camera), cross-session reproducible |
| 002 | PASS | Two-phase test confirmation | RTX path tracing works, hash stability confirmed |

### Phase 2: Anti-Culling Attempts

| Build | Result | Key Change | Key Finding |
|-------|--------|------------|-------------|
| 016 | PASS* | 3-layer anti-culling + frustum threshold | Draw count 91.8K; *movement was broken (false positive — no scancode flag) |
| 017 | FAIL | NOPs in proxy + BeginScene re-stamp | Lights disappear after D-strafe, hash colors shift |
| 018 | FAIL | Scancode fix — movement actually works | Green light disappears on D-strafe; real movement confirmed |
| 019 | PASS* | Same code as 018, different RNG seed | False positive — wrong screenshots evaluated |
| 020 | FAIL | Fixed screenshot selection | Build 019 was false positive; red light missing in 2/3 shots |

### Phase 3: Light Culling Investigation

| Build | Result | Key Change | Key Finding |
|-------|--------|------------|-------------|
| 021 | PASS* | VS 2026 Insiders build fix | False positive — Lara didn't move |
| 022 | FAIL | Confirmed exe is unmodified (runtime-only patches) | D held too long, Lara left stage area |
| 023 | FAIL | Light frustum NOPs in wrong source file | Bug: repo-root proxy/ not compiled — always edit patches/ proxy |
| 024 | FAIL | Light frustum NOPs in correct source | Shot 1 both lights visible; shots 2-3 fail — zone hypothesis formed |
| 025 | FAIL | Pending-render flag NOPs (0x603832, 0x60E30D) | No effect — bottleneck is not in caller chain flags |
| 026 | FAIL | LightVolume_UpdateVisibility state NOPs (5 addrs) | Patches NOT in proxy log — silent VirtualProtect failure |
| 027 | FAIL | Same patches + randomized movement | Draw counts 93K–189K confirm sector patch works; issue is light range |
| 028 | FAIL | Sector visibility NOPs + removed native light patches | Geometry fully submitting (65× increase); clean render dark |
| 029 | FAIL | Cull globals stamped + light frustum NOP + threshold -1e30 | All geometry culling defeated; light disappearance remains |
| 030 | FAIL | Baseline retest + Ghidra analysis | Root cause: `Light_VisibilityTest` at 0x60B050 unpatched |
| 031 | FAIL | `Light_VisibilityTest` → always TRUE (0x60B050) | Lights at baseline; disappear at distance — root moved to sector light list |
| 032 | FAIL | Config flag 0x01075BE0 = 1 ("Disable extra light culling") | No effect — flag has no code xrefs, not connected to light collection |
| 033 | FAIL | NOP at 0xEC6337 (sector light count gate) | Macro failure — pause menu blocked all screenshots; inconclusive |
| 035 | FAIL | Sector light gate NOP confirmed + Light_VisibilityTest + directional red fallback | Green stable at all positions; red anchor meshes in sectors with `[sector_data+0x664]=0` |

### Phase 4: Geometry Submission Investigation

| Build | Result | Key Change | Key Finding |
|-------|--------|------------|-------------|
| 036 | FAIL | Re-test with fixed automation (no proxy changes) | Green culled at sector boundary; sector light count gate at 0x60E345 next target |
| 037 | FAIL | RenderLights gate NOP (0x60E3B1) + sector light count clear NOP (0x603AE6) | Green still missing at distance; "red" at distance may be fallback |
| 038 | FAIL | Changed fallback to neutral white (1,1,1) to diagnose | **Root cause reframe: both lights gone = geometry not submitted, not light culling** |
| 039 | FAIL | Removed RET at 0x407150 (function runs with 7 NOPs) | Draw counts 93K→180K; green at shot 3 (extreme distance); shot 2 loses both |
| 040 | FAIL | 11 cull NOPs inside SceneTraversal_CullAndSubmit | ~190K draws; all 11 paths NOPed — culling NOT in this function |
| 041 | FAIL | Far clip distance stamp (0x10FC910 → 1e30f) per BeginScene | Same pattern as 039; far clip not the issue |
| 042 | FAIL | Re-parented lights to mesh_7DFF31ACB21B3988 (largest mesh) | Worse — all shots show fallback only; large mesh not always drawn; reverted |
| 043 | CRASH | Aggressive 7-NOP set | Crashed — not preserved |
| 044 | FAIL | Camera-sector proximity filter NOP (0x46B85A in RenderSector) | Same pattern; all 3 render paths patched; terrain path (0x40ACF0) prime suspect |

*False positive — movement input not reaching game or Lara didn't move.

---

## What's Been Done

- [x] D3D9 proxy DLL with shader passthrough + transform override
- [x] Asset hash rule: `indices,texcoords,geometrydescriptor` (excluding positions)
- [x] View/Proj matrix reading from game memory
- [x] World matrix decomposition from WVP
- [x] Frustum threshold stamped to -1e30 per BeginScene (`0xEFDD64`)
- [x] Per-object frustum function RETed (`0x407150`)
- [x] 11 scene traversal cull branches NOPed inside `0x407150`
- [x] Backface culling forced to `D3DCULL_NONE`
- [x] Cull mode globals stamped (`0xF2A0D4/D8/DC`)
- [x] Sector/portal visibility gates NOPed (`0x46C194`, `0x46C19D`) — 65× draw increase
- [x] Camera-sector proximity filter NOPed (`0x46B85A` in RenderSector, build 044)
- [x] Far clip distance stamped to 1e30f per BeginScene (`0x10FC910`, build 041)
- [x] Light frustum 6-plane test NOPed (`0x60CE20`)
- [x] Light broad-visibility test NOPed (`0x60CDE2`)
- [x] `Light_VisibilityTest` patched → always TRUE (`0x60B050`, build 031)
- [x] Sector light count gate NOPed (`0xEC6337`, build 035)
- [x] RenderLights gate NOPed (`0x60E3B1`, build 037)
- [x] Sector light count clear NOPed (`0x603AE6`, build 037)
- [x] Automated two-phase test pipeline (hash debug + clean render, randomized movement)
- [x] Scancode input fix for DirectInput games
- [x] Stage light anchoring via mod.usda mesh hashes
- [x] Root cause reframe — confirmed geometry submission, not light culling (build 038)
- [x] All three render paths identified and patched (RenderVisibleSectors, SceneTraversal wrapper, moveable object loop)
- [x] Alternative red anchor meshes investigated (7DFF, 6AF0, 5601, ECD5) — all in zero-light sectors (build 035)
- [x] VirtualProtect failure detection: always verify patches in proxy log

---

## What Still Needs to Be Done

### Blocking — Terrain Rendering Path

- [ ] **Decompile `TerrainDrawable (0x40ACF0)`** and `TERRAIN_DrawUnits` — find the culling condition and NOP it
- [ ] **dx9tracer frame diff**: capture one frame near stage, one far — diff draw call lists to identify which hashes disappear
- [ ] **Confirm anchor mesh type**: is it terrain geometry (TerrainDrawable path) or an instance object?

### Alternatives if Terrain Path Doesn't Resolve It

- [ ] **Option A — Runtime sector data patch:** Find `*(renderCtx+0x220)` base at runtime and write 1 to `[sector_data + N×0x684 + 0x664]` for all N sectors. Forces sectors to claim native static lights, enabling `FUN_00EC62A0` to populate light counts.
- [ ] **Option B — Draw call replay in proxy:** Record anchor mesh `DrawIndexedPrimitive` calls during the first frame. Replay them every subsequent frame so anchor hashes are always present for Remix regardless of distance.
- [ ] **Option C — Anchor to Lara's mesh:** Identify Lara's body mesh hash from Remix captures. Anchor both stage lights to her — she's always rendered, always in the same sector as the camera.

### Lower Priority

- [ ] Investigate LOD alpha fade at `0x446580` (10 callers) — may fade geometry at distance
- [ ] Decompile `FUN_006033d0` and `FUN_00602aa0` — understand upstream light list builder and proximity filter
- [ ] Investigate `sector+0x84` setter — `RenderScene_Main` gates the light pass on this field
- [ ] Investigate `LightVolume::Draw` (vtable[0x18]) for internal culling

---

## Review Schedule

Updated at the end of each development phase or every 5 builds, whichever comes first.
