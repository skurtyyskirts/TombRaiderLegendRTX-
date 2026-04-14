# TRL RTX Remix — Test Status Report

**Last reviewed:** 2026-04-14
**Builds reviewed:** 001, 002, 016–033, 035–042, 044–047, 064–077 (003–015, 034, 043, 048–063 not preserved)
**Overall status:** FAILING — stale anchor hashes in mod.usda. Replacement asset pipeline confirmed working (build 075). Cold launch stable (build 077). All 31 culling layers patched. Fresh Remix capture needed to get current building mesh hash IDs.

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

1. **Stage lights absent — stale anchor hashes.** The 8 building mesh hashes in `mod.usda` were captured under a previous Remix configuration. Build 075 confirmed that the replacement asset pipeline works (purple test light visible), but no currently-rendered mesh matches the stored hash IDs. "White dots" from prior builds were denoiser/NRC artifacts, not lights (proven by testing with light radius 2→3000: no change in dot behavior).

2. **LOD alpha fade unpatched.** `0x446580` with 10 callers may fade geometry at distance. Low priority — geometry IS being submitted.

### Hurdles (Resolved)

1. **`user.conf` override (build 075 — FIXED).** `user.conf` in the game directory had `rtx.enableReplacementAssets=False`, silently disabling all mod content in builds 016–074. Remix loads config as `dxvk.conf → rtx.conf → user.conf`; the last value wins. Fixed by setting `enableReplacementAssets=True` in `user.conf`.

2. **FLOAT3 draws fixed (build 071b).** Nulling VS before FLOAT3 draws puts character geometry through FFP correctly. Lara visible in all recent builds.

3. **All 31 patches active simultaneously, confirmed crash-free (build 074).** Deferred patch initialization eliminates the menu crash.

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

### Phase 5: Proxy Improvements + Deep Culling

*Builds 048–063 not preserved.*

| Build | Result | Key Change | Key Finding |
|-------|--------|------------|-------------|
| 045 | FAIL | D3DPOOL_MANAGED VBs + per-frame VB flush | SHORT4 hash debug stable; blank amber render — flush too aggressive; D3DPOOL_MANAGED is correct |
| 046 | FAIL | Content fingerprint cache + null VS for ALL draws | Nulling VS for FLOAT3 breaks rendering (view-space positions render at extreme scale); fingerprint cache is correct |
| 047 | FAIL | Remove `positions` from asset hash | Catastrophic hash collision — all geometry same hash; `positions` must stay in hash rule |
| 048-063 | — | Not preserved | Terrain cull gate, null-check trampoline, ProcessPendingRemovals fix, MeshSubmit_VisibilityGate, stream unload gate, mesh eviction NOPs |
| 064 | FAIL | Hash stability test (camera pan) | Phase 1 invalid (15s load wait too short for Remix+Peru); Phase 2: ~244 draws, no lights |
| 065 | FAIL | Hash stability test (improved) | Hash stable; ~650-657 draws; anchor meshes absent; light patches disabled (crash risk) |
| 066 | FAIL | Disable draw cache (`DRAW_CACHE_ENABLED 0`) | No effect — draw cache only replays 3 draws |
| 067 | FAIL | Remove VP inverse epsilon threshold | No effect — VP changes are large enough to always trigger recalculation |
| 068 | FAIL | Re-enable all 3 light patches (LightVisTest + sector gate + RenderLights gate) | No crash — ProcessPendingRemovals fix resolved 0xEE88AD; all 20+ patches confirmed; lights still absent |
| 069 | FAIL | Hash stability test (dipcnt failed) | ~670-694 draws/frame; 75% of initial draws culled; patch integrity confirmed |
| 070 | FAIL | Hash stability, anti-culling disabled for baseline | Draw counts collapsed 93% (2833→185); proxy uses internal NOP jumps not RET at 0x407150 |
| 071 | FAIL | Added 3 anchor hashes to mod.usda (5→8 total) | Lara not visible; ~2845 draws; no lights |
| 071b | FAIL | FLOAT3 draw path fix — null VS before FLOAT3 draws | **Lara now visible** — FLOAT3 geometry through FFP for first time; lights still absent |
| 072 | FAIL | RenderQueue_FrustumCull bypass (0x40C430 → 0x40C390) | +29% draws (2845→3657); Lara in hash debug; no crash; lights still absent |
| 073 | FAIL | `rtx.useVertexCapture = True` | ~3651 draws; white dots visible in screenshots — possible overexposed stage lights |
| 074 | FAIL | Deferred patches + permanent page unlock | 3749 draws/scene; all 31 patches active; menu crash fixed; no lights |
| 075 | FAIL | Fix `user.conf` enableReplacementAssets; purple reference light test | **Breakthrough**: purple test light visible + stable (pipeline confirmed). Stage light hashes stale. White dots were denoiser artifacts. |
| 076 | FAIL | Restore null-crash guard (`0x40D2AF`) + PUREDEVICE stripping + FourCC rejection | Crash protections restored; 3,733 draws/scene; replacement (purple) light visible in clean render; stage lights absent |
| 077 | FIXED | DrawCache use-after-free fix — AddRef vb/ib/decl/tex0 in Record; Clear on Release/Reset/transition | **Cold launch crash fixed.** 2,468 draws/scene; 90+ seconds stable from cold menu start; stage lights still absent (stale hashes) |

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
- [x] D3DPOOL_MANAGED for expanded SHORT4→FLOAT3 VBs (build 045)
- [x] Content fingerprint cache for VB invalidation (build 046)
- [x] `positions` confirmed required in asset hash rule (build 047)
- [x] Terrain cull gate NOPed (builds 045-063)
- [x] Null-check trampoline at 0xEDF9E3 (builds 045-063)
- [x] ProcessPendingRemovals stale field fix — resolved crash at 0xEE88AD (builds 045-063)
- [x] MeshSubmit_VisibilityGate → return 0 (builds 045-063)
- [x] Stream unload gate NOPed (builds 045-063)
- [x] Mesh eviction NOPed: SectorEviction (×2) + ObjectTracker_Evict (builds 045-063)
- [x] Post-sector loop enabled + post-sector bitmask/distance culls NOPed (builds 045-063)
- [x] All 3 light pipeline gates re-enabled and confirmed crash-free (build 068)
- [x] FLOAT3 draw path fixed — null VS before FLOAT3 draws (build 071b); Lara now visible in all renders
- [x] RenderQueue_FrustumCull (0x40C430) bypassed via JMP → 0x40C390 — Layer 31 (build 072)
- [x] Extended anchor mesh hashes in mod.usda from 5 to 8 (build 071)
- [x] Deferred `TRL_ApplyMemoryPatches()` to first valid `BeginScene` — fixes menu crash (build 074)
- [x] Permanent `VirtualProtect` page unlock — eliminates ~28 kernel calls/frame (build 074)
- [x] Fixed `user.conf` `enableReplacementAssets=False` override — replacement asset pipeline now enabled (build 075)
- [x] Confirmed replacement asset pipeline works end-to-end — purple test light visible, stable, camera-shift confirmed (build 075)
- [x] Confirmed white dots in builds 072-073 were denoiser/NRC artifacts, not anchor lights (build 075 radius test)
- [x] Restored null-crash guard at `0x40D2AF` and PUREDEVICE stripping in W9_CreateDevice (build 076)
- [x] Fixed DrawCache use-after-free: AddRef vb/ib/decl/tex0 in DrawCache_Record; DrawCache_Clear on WD_Release/WD_Reset/transition flush (build 077)
- [x] Confirmed cold launch stable — game survives menu→level transition without TR7.arg (build 077)

---

## What Still Needs to Be Done

### Immediate — Correct the Anchor Hashes

Now that the game launches stably from cold start (build 077), a fresh Remix capture can be taken at any time.

- [ ] **Fresh Remix capture**: launch game (no TR7.arg needed), load Peru, enable hash debug view (debug view 277), position Lara near the stage, and use Remix Toolkit to capture. Extract current mesh hash IDs for the building columns/pillars.
- [ ] **Update mod.usda**: replace the 8 stale hash entries with hash IDs from the fresh capture
- [ ] **Re-test**: both red and green lights should appear once hashes match

### If Hashes Are Updated but Lights Still Absent

- [ ] Verify hash debug view shows the building geometry in frame (confirm Lara is close enough to stage)
- [ ] **dx9tracer capture**: use `--classify-draws` and `--shader-map` to identify draw calls for the building; compare hash IDs against Toolkit capture

### Fallback

- [ ] **Anchor to Lara's mesh**: Lara's body mesh hash is now captured in recent builds; anchoring both lights to Lara guarantees visibility at all positions

### Lower Priority

- [ ] Investigate LOD alpha fade at `0x446580` (10 callers) — may fade geometry at distance

---

## Review Schedule

Updated at the end of each development phase or every 5 builds, whichever comes first.
