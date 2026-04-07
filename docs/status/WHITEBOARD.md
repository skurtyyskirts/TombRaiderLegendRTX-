# TRL RTX Remix — Results Whiteboard

**Last updated:** 2026-03-27 (session 4)
**Builds completed:** 001-044 (003-015, 034, 043 not preserved)
**Goal:** Get Tomb Raider Legend rendering correctly with RTX Remix — stable hashes, no culling, anchored lights

---

## Status at a Glance

| Goal | Status | Notes |
|------|--------|-------|
| FFP proxy DLL builds & chains | DONE | MSVC x86, chains to Remix d3d9 |
| Transform pipeline (View/Proj/World) | DONE | View/Proj from game memory, World via WVP decomposition |
| Asset hash stability (static camera) | DONE | `indices,texcoords,geometrydescriptor` rule, session-reproducible |
| Asset hash stability (with movement) | DONE | Lara model rock-solid; world geometry stable once culling fixed |
| Automated test pipeline | DONE | Two-phase (hash debug + clean render), randomized movement |
| Input delivery to DirectInput game | DONE | Scancode flag fix (build 018) |
| Backface culling disabled | DONE | D3DCULL_NONE + cull globals stamped |
| Frustum distance culling disabled | DONE | Threshold -1e30 + 7 NOP jumps + RET at 0x407150 |
| Sector/portal visibility disabled | DONE | NOPs at 0x46C194 + 0x46C19D, 65x draw increase |
| Light frustum rejection disabled | DONE | NOP at 0x60CE20 |
| Light visibility pre-check disabled | DONE | `Light_VisibilityTest` at 0x60B050 → `mov al,1; ret 4` (build 031) |
| Sector light count gate disabled | DONE | NOP at 0xEC6337 (build 035) |
| GREEN light stable at all positions | **FAILING** | Build 038 confirmed: both stage lights vanish at distance; "green at distance" in 039/041 is positional, not stable |
| RED light stable at all positions | **FAILING** | Both lights absent at distance; "red" at distance was fallback light (build 038 diagnostic) |
| Remix light anchors hold on movement | **FAILING** | Root cause reframed (build 038): anchor geometry not submitted at distance — geometry culling, not light culling |

---

## The Two Remaining Problems

### Problem 1: Anchor Geometry Not Submitted at Distance

**Root cause reframed (build 038):** The "red light at distance" in builds 019-037 was the fallback light (`rtx.fallbackLightRadiance = 3, 0.3, 0.3`). With neutral fallback (build 038), BOTH stage lights vanish when Lara walks away from the stage. This means the engine's light culling functions (RenderLights_FrustumCull, Light_VisibilityTest, etc.) are irrelevant — Remix lights are anchored to geometry hashes, and when the anchor geometry isn't submitted as a draw call, the lights vanish.

**All identified culling paths in SceneTraversal_CullAndSubmit (0x407150) exhausted (build 040):** 11 of 12 conditional exits NOPed (the 12th is a safety check for LOD count == 0). Draw counts rose from ~93K (with RET) to ~180-190K (without RET + 11 NOPs). Still fails.

**Multiple render paths confirmed (build 044):** Upstream caller analysis revealed 3 separate render paths: (1) RenderVisibleSectors → RenderSector, (2) SceneTraversal wrapper → 0x407150, (3) moveable object loop at 0x40E2C0. Patches applied to all three, yet anchor geometry disappears.

**Working hypothesis (build 044):** The anchor meshes may be **terrain geometry** going through TerrainDrawable (0x40ACF0) / TERRAIN_DrawUnits — a separate rendering path with its own culling, not covered by any current patches.

**Camera-sector proximity filter NOPed (build 044):** NOP at 0x46B85A (RenderSector JNE) — no effect on pattern.

**Additional NOPs tried (builds 037, 040, 041):** RenderLights gate (0x60E3B1), sector light count clear (0x603AE6), far clip stamp (0x10FC910 → 1e30f), 4 additional cull flags in 0x407150 (0x4071CE, 0x407976, 0x407B06, 0x407ABC) — none resolved the issue.

### Problem 2: Hash Instability (Believed Resolved)

Earlier builds (017, 022) showed hash color shifts on movement. Build 021+ confirmed this was proxy bugs, not engine behavior. TRL skinning is GPU-side (VS constants), VBs are static, so hashes are inherently stable. No hash instability has been observed since the sector visibility patches were added.

---

## Culling Layers — Complete Map

Every culling mechanism discovered and its patch status:

| Layer | Address(es) | What It Does | Patched? | Build Added |
|-------|-------------|--------------|----------|-------------|
| 1. Frustum distance threshold | 0xEFDD64 | Float constant (16.0f) — objects closer than threshold culled | Yes — stamped to -1e30f per BeginScene | 016 |
| 2. Per-object frustum function | 0x407150 | `SceneTraversal_CullAndSubmit` — RET kills entire function | Yes — `ret` at entry | 016 |
| 3. Scene traversal cull jumps (7x) | 0x4072BD, 0x4072D2, 0x407AF1, 0x407B30, 0x407B49, 0x407B62, 0x407B7B | Distance + screen-boundary conditional skips | Yes — all 7 NOPed (6 bytes each) | 016 |
| 4. D3D backface culling | SetRenderState(D3DRS_CULLMODE) | Hardware backface cull | Yes — forced D3DCULL_NONE | 016 |
| 5. Cull mode globals | 0xF2A0D4, 0xF2A0D8, 0xF2A0DC | Engine cull state variables | Yes — stamped to D3DCULL_NONE per scene | 029 |
| 6. Sector/portal visibility | 0x46C194, 0x46C19D | JE + JNE gates for sector rendering | Yes — both NOPed | 028 |
| 7. Light frustum 6-plane test | 0x60CE20 | Rejects lights failing plane test | Yes — NOPed | 024 |
| 8. Light broad-visibility test | 0x60CDE2 | Early light rejection | Yes — NOPed | 024 |
| 9. Pending-render flags | 0x603832, 0x60E30D | Caller chain flags | Yes — NOPed (no effect) | 025 |
| 10. Light visibility state NOPs | 5 addresses in LightVolume_UpdateVisibility | Visibility state check | Attempted — NOT confirmed in log | 026 |
| 11. Light_VisibilityTest | 0x0060B050 | Pre-frustum distance/sphere/cone gate per light | Yes — `mov al,1; ret 4` | 031 |
| 12. Sector light count gate | 0xEC6337 (inside FUN_00EC62A0) | JNZ gate: skips light pass if sector light count == 0 | Yes — NOPed | 033 |
| 13. Sector light list population | FUN_006033d0 / FUN_00602aa0 | Upstream: builds per-sector light arrays (proximity filter) | IRRELEVANT — not the root cause (build 038 reframe) | — |
| 14. LOD alpha fade | 0x446580 | 10 callers, may fade geometry invisible at distance | **UNEXPLORED** | — |
| 15. Scene graph sector early-outs | Unknown | Sector-based submission skipping | **UNEXPLORED** (may be covered by layer 6) | — |
| 16. Light Draw virtual method | vtable[0x18] per light | Internal culling inside light's Draw method | IRRELEVANT — Remix lights anchor to geometry, not engine light functions | — |
| 17. RenderLights gate | 0x60E3B1 | JE skipping RenderLights_FrustumCull when sector light count = 0 | Yes — NOPed | 037 |
| 18. Sector light count clear | 0x603AE6 | MOV zeroing [eax+0x1B0] per frame | Yes — NOPed | 037 |
| 19. Additional SceneTraversal exits (4x) | 0x4071CE, 0x407976, 0x407B06, 0x407ABC | Object disable flag (A+B), far clip, draw distance fade-out inside 0x407150 | Yes — all 4 NOPed | 040 |
| 20. Far clip distance global | 0x10FC910 | g_farClipDistance stamped to 1e30f per BeginScene | Yes — stamped | 041 |
| 21. Camera-sector proximity filter | 0x46B85A | JNE in RenderSector skipping objects without flag 0x200000 when not in camera sector | Yes — NOPed | 044 |
| 22. Terrain rendering path | TerrainDrawable (0x40ACF0) / TERRAIN_DrawUnits | Separate draw path for terrain geometry, own culling | **UNEXPLORED — prime suspect** | — |

---

## Build History — What Was Tested and What We Learned

### Phase 1: Baseline & Hash Stability (Builds 001-002)

| Build | Result | What We Tested | What We Learned |
|-------|--------|----------------|-----------------|
| 001 | PASS | Passthrough proxy + transform override, static camera | Hashes stable across frames and sessions |
| 002 | PASS | Two-phase test (hash debug + clean render) | RTX path tracing works, hash stability confirmed |

**Conclusion:** Hash rule `indices,texcoords,geometrydescriptor` works. Transform pipeline correct.

### Phase 2: Anti-Culling Attempts (Builds 016-020)

| Build | Result | What We Tested | What We Learned |
|-------|--------|----------------|-----------------|
| 016 | PASS* | 3-layer culling patches (threshold + RET + 7 NOPs + CULL_NONE) | Draw count stabilized at 91.8K — BUT movement was broken (false positive) |
| 017 | FAIL | Same patches, BeginScene re-stamp | Lights disappear on D-strafe, hash colors shift — first real movement test |
| 018 | FAIL | Scancode fix — movement actually works now | Green light vanishes after ~8s D-strafe. Confirmed builds 001-016 had broken input |
| 019 | PASS* | Same code as 018, different RNG movement | Both lights visible — but later found to be false positive (wrong screenshots evaluated) |
| 020 | FAIL | Fixed screenshot selection bug | Build 019's PASS was false positive. Red light missing in 2/3 shots |

**Conclusion:** Basic culling patches work for geometry but NOT for lights. Movement reveals light culling.

### Phase 3: Light Culling Investigation (Builds 021-030)

| Build | Result | What We Tested | What We Learned |
|-------|--------|----------------|-----------------|
| 021 | PASS* | VS 2026 Insiders build fix | False positive — Lara didn't actually move |
| 022 | FAIL | Confirmed on-disk exe is unmodified (runtime patches only) | D hold too long, Lara left stage area |
| 023 | FAIL | Light frustum NOPs (but in WRONG source file!) | Bug: patches in repo-root proxy/ not compiled |
| 024 | FAIL | Light frustum NOPs in CORRECT source | Shot 1 BOTH lights visible. Shots 2-3 fail. Improvement! Zone hypothesis formed |
| 025 | FAIL | Pending-render flag NOPs at 0x603832 + 0x60E30D | NO EFFECT. Bottleneck is NOT in caller chain |
| 026 | FAIL | LightVolume_UpdateVisibility state NOPs (5 addresses) | Patches NOT confirmed in proxy log — silent failure |
| 027 | FAIL | Same patches, randomized movement | Draw counts 93K-189K confirm sector patch works. Issue is light range |
| 028 | FAIL | Sector visibility NOPs + removed native light patches | Geometry fully submitting (65x increase). Clean render dark = Remix light range issue |
| 029 | FAIL | Cull globals stamped + light frustum NOP + threshold -1e30 | All geometry culling confirmed defeated. Light disappearance remains |
| 030 | FAIL | Baseline retest + Ghidra analysis | **ROOT CAUSE: `Light_VisibilityTest` at 0x60B050 unpatched** |
| 031 | FAIL | `Light_VisibilityTest` patch (0x60B050 → `mov al,1; ret 4`) | Lights at baseline; still disappear at distance — root moved to sector light list population |
| 032 | FAIL | Config flag stamp (0x01075BE0 = 1) for "Disable extra static light culling" | No effect — flag has no code xrefs, not connected to light collection |
| 033 | FAIL | Same proxy + new NOP at 0xEC6337 (sector light count gate) | Macro failed — pause menu blocked all screenshots; proxy healthy; result inconclusive |
| 035 | FAIL | Sector light count gate NOP confirmed + Light_VisibilityTest patch + directional red fallback | Green stable with gate NOP; red anchor meshes all in sectors with `[sector_data+0x664]=0` — gate only helps sectors with non-zero static data |

**Conclusion (revised build 038):** Per-light culling gates were the wrong target. The "red light at distance" was the fallback light. Both stage lights vanish because anchor geometry isn't submitted — geometry culling is the real problem.

### Phase 4: Geometry Submission Investigation (Builds 036-044)

| Build | Result | What We Tested | What We Learned |
|-------|--------|----------------|-----------------|
| 036 | FAIL | Re-test with fixed automation (no proxy changes) | Green culled at sector boundary; confirmed sector light count gate at 0x60E345 as next target |
| 037 | FAIL | RenderLights gate NOP (0x60E3B1) + sector light count clear NOP (0x603AE6) | Green still missing at distance; hypothesized "red" at distance may be fallback light |
| 038 | FAIL | Changed fallback light to neutral white (1,1,1) to diagnose | **ROOT CAUSE REFRAME: both lights gone at distance; "red" was fallback — problem is geometry, not lights** |
| 039 | FAIL | Removed RET at 0x407150 (function now executes with 7 NOPs active) | Draw counts 93K→180K; green appears at shot 3 (extreme distance); shot 2 still loses both |
| 040 | FAIL | 11 cull NOPs inside SceneTraversal_CullAndSubmit (all safe exits) | Draw counts ~190K; all 11 paths NOPed — culling NOT in this function |
| 041 | FAIL | Far clip distance stamp (0x10FC910 → 1e30f) per BeginScene | Same pattern as 039; far clip not the issue |
| 042 | FAIL | Re-parented lights to mesh_7DFF31ACB21B3988 (largest captured mesh) | Worse — all shots show fallback only; large mesh not always drawn; reverted immediately |
| 044 | FAIL | Camera-sector proximity filter NOP (0x46B85A in RenderSector) | Same pattern; 3 render paths all patched; terrain path (0x40ACF0) identified as prime suspect |

*Note: Build 043 (aggressive 7-NOP set) crashed and was not preserved.*

**Conclusion:** All identified culling paths in geometry submission (11 NOPs in 0x407150, sector visibility, proximity filter, far clip) exhausted. Anchor geometry still disappears. Terrain rendering path (TerrainDrawable / TERRAIN_DrawUnits) is the unexplored prime suspect.

---

## What Has NOT Been Tried

| Idea | Why It Matters | Difficulty |
|------|----------------|------------|
| Investigate TerrainDrawable (0x40ACF0) / TERRAIN_DrawUnits | Prime suspect: separate terrain render path with own culling; anchor meshes may be terrain | Medium — static analysis needed |
| dx9tracer frame capture at near vs far position | Definitively identifies which draw calls disappear; would show if anchor hashes are absent vs just invisible | Medium — setup dx9tracer |
| Find Lara's character mesh hash | Her body is always drawn; anchoring lights to her hash would guarantee visibility at all positions | Easy — hash debug screenshots |
| Find and NOP terrain culling path | TerrainDrawable likely has distance/sector culling separate from SceneTraversal | Hard — need to find and understand the path |
| Patch LOD alpha fade at 0x446580 | 10 callers, may fade geometry at distance | Medium — verify if affects anything post-sector-patch |
| Investigate 0x41F96A object visibility check | Uses same threshold, different code path | Low priority |

---

## Key Addresses Reference

### Engine Globals
| Address | Name | Notes |
|---------|------|-------|
| 0x01392E18 | `g_pEngineRoot` | Root engine object |
| 0x010FC780 | View matrix source | Read by proxy |
| 0x01002530 | Proj matrix source | Read by proxy |
| 0xEFDD64 | Frustum threshold (16.0f) | Stamped to -1e30 per scene |
| 0xF2A0D4/D8/DC | Cull mode globals | Stamped to D3DCULL_NONE |
| 0xEFD404/0xEFD40C | Screen boundary min/max | Used by boundary cull checks |

### Sector Data Layout
| Field | Notes |
|-------|-------|
| `*(renderCtx+0x220)` | Sector data base pointer |
| `sector_data + N*0x684 + 0x664` | Native static light count for sector N (0 = no lights, gate skips) |
| `sector+0x1B0` | Per-sector light list count (populated by FUN_00EC62A0) |
| `sector+0x1B8` | Per-sector light list array pointer |
| `sector+0x84`, `sector+0x94` | Fields gating light pass in `RenderScene_Main` (must be non-zero) |

### Renderer Chain
```
g_pEngineRoot (+0x214) → TRLRenderer* (+0x0C) → IDirect3DDevice9*
```

### VS Constant Register Map (Key Registers)
| Register | Purpose |
|----------|---------|
| c0-c3 | World matrix (transposed) |
| c0-c7 | WorldViewProjection (two 4x4) |
| c8-c15 | ViewProjection (two 4x4) |
| c16+ | Bone/skin matrices |
| c39 | Utility {2.0, 0.5, 0.0, 1.0} |

### Render Paths (upstream caller analysis, build 044)
```
RenderFrame (0x450B00)
  ├── RenderVisibleSectors (0x46C180) → RenderSector (0x46B7D0) ← proximity filter NOPed 0x46B85A
  ├── SceneTraversal wrapper (0x443C20) → SceneTraversal_CullAndSubmit (0x407150) ← all 11 NOPs + no RET
  └── Moveable object loop (0x40E2C0)
TerrainDrawable (0x40ACF0) / TERRAIN_DrawUnits ← UNEXPLORED, PRIME SUSPECT
```

### Light Pipeline (IRRELEVANT since build 038 — lights are Remix geometry-anchored)
```
FUN_006033d0 / FUN_00602aa0 ← IRRELEVANT (per build 038 root cause reframe)
  └→ FUN_00EC62A0 (0xEC62A0) — reads [sector_data+0x664], populates [sector+0x1B0] count
       └→ Sector light count gate (0xEC6337) ← NOPed (build 033)
            └→ RenderLights gate (0x60E3B1) ← NOPed (build 037)
                 └→ Light_VisibilityTest (0x60B050) ← patched → always TRUE (build 031)
                      └→ Frustum 6-plane test (0x60CE20) ← patched (NOP)
```

### Additional Globals (builds 037-041)
| Address | Name | Notes |
|---------|------|-------|
| 0x60E3B1 | RenderLights gate | JE skipping light pass if sector light count 0 — NOPed (build 037) |
| 0x603AE6 | Sector light count clear | MOV zeroing [eax+0x1B0] per frame — NOPed (build 037) |
| 0x10FC910 | `g_farClipDistance` | Stamped to 1e30f per BeginScene (build 041) |

---

## Known False Positives & Testing Pitfalls

| Issue | Builds Affected | Resolution |
|-------|----------------|------------|
| Movement input not reaching game (no KEYEVENTF_SCANCODE) | 001-016 | Fixed in build 018 |
| Wrong screenshots evaluated (camera-pan vs post-movement) | 019-020 | Fixed screenshot selection |
| Patches in wrong source file (repo-root vs patches/ proxy) | 023 | Always edit `patches/TombRaiderLegend/proxy/d3d9_device.c` |
| Lara walking past stage area (movement too long) | 022, 027, 029 | Randomized movement with shorter bounds |
| VirtualProtect silent failures | 026 | Always check proxy log for patch confirmation |
| `user.conf` overriding Remix settings | Pre-016 | Set `rtx.enableReplacementAssets` correctly |

---

## Immediate Next Step

**Investigate the terrain rendering path.** All identified geometry culling paths in SceneTraversal (0x407150), RenderSector (0x46B7D0), and the moveable object loop (0x40E2C0) have been patched. Anchor geometry still disappears. The unexplored path is TerrainDrawable (0x40ACF0) / TERRAIN_DrawUnits.

Two parallel tracks:
1. **Static analysis**: Decompile TerrainDrawable (0x40ACF0) and TERRAIN_DrawUnits — find the culling condition and NOP it. Also decompile the sector iteration loop at 0x46C180 to understand per-sector object lists.
2. **dx9tracer frame capture**: Capture one frame near stage and one frame far from stage. Diff the draw call lists to identify exactly which hashes disappear — this definitively shows whether the anchor meshes are terrain or instance geometry.

**What was tried and ruled out (since build 035):**
- Both stage lights fully absent at distance (build 038) — confirmed geometry, not light culling
- All 11 safe exits in SceneTraversal_CullAndSubmit NOPed (build 040) — not the issue
- Far clip stamp to 1e30f (build 041) — no effect
- Camera-sector proximity filter NOPed (build 044) — no effect
- Re-parenting lights to large mesh (build 042) — made things worse; mesh not always drawn
- RenderLights gate + sector light count clear NOPed (build 037) — irrelevant (light functions not the issue)

---

## Decision Tree for Next Failure

```
Both lights gone at distance (confirmed pattern builds 038-044)
├── dx9tracer diff: which hashes disappear?
│   ├── Anchor hashes absent → geometry not submitted
│   │   ├── Hashes match terrain signature → investigate TerrainDrawable (0x40ACF0)
│   │   └── Hashes match instance signature → look at sector object list population
│   └── Anchor hashes present → Remix side issue (anchor config or light range)
├── Decompile TerrainDrawable (0x40ACF0) → find distance/sector culling → NOP it
└── Fallback: find Lara's always-drawn mesh hash → anchor lights to her body
```
