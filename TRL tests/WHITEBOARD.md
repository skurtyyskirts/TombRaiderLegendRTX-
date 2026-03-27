# TRL RTX Remix — Results Whiteboard

**Last updated:** 2026-03-27
**Builds completed:** 001-030 (16 builds, 003-015 not preserved)
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
| Light visibility pre-check disabled | **NOT DONE** | `Light_VisibilityTest` at 0x60B050 unpatched |
| Lights stable across all positions | **FAILING** | Lights vanish when Lara walks away from stage |
| Remix light anchors hold on movement | **FAILING** | Consequence of above |

---

## The Two Remaining Problems

### Problem 1: Lights Disappear on Movement

**Root cause identified (build 030):** `Light_VisibilityTest` at `0x0060B050` is an unpatched culling gate. It runs per-light BEFORE the frustum test. For light types 0 and 1, it performs distance/sphere/cone checks and rejects lights that are "too far." The proxy already NOPs the frustum rejection (stage 2), but stage 1 kills them first.

**Fix ready but untested:** Patch `0x0060B050` with `mov al, 1; ret 4` (5 bytes) to force all lights visible.

**Risk:** If lights still disappear after this patch, the issue moves upstream to sector-level light list population — the loop in `RenderLights_FrustumCull` iterates over a list at `[param+0x1B0]` count / `[param+0x1B8]` array. If the sector system doesn't populate this list with all level lights, they never enter the loop.

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
| **11. Light_VisibilityTest** | **0x0060B050** | **Pre-frustum distance/sphere/cone gate per light** | **NO — identified but unpatched** | — |
| 12. Sector light list population | [param+0x1B0] / [param+0x1B8] | Upstream light array fed to RenderLights_FrustumCull | **UNEXPLORED** | — |
| 13. LOD alpha fade | 0x446580 | 10 callers, may fade geometry invisible at distance | **UNEXPLORED** | — |
| 14. Scene graph sector early-outs | Unknown | Sector-based submission skipping | **UNEXPLORED** (may be covered by layer 6) | — |
| 15. Light Draw virtual method | vtable[0x18] per light | Internal culling inside light's Draw method | **UNEXPLORED** (hypothesis from build 025) | — |

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

**Conclusion:** Geometry culling fully solved. Light culling has a remaining gate at `Light_VisibilityTest`.

---

## What Has NOT Been Tried

| Idea | Why It Matters | Difficulty |
|------|----------------|------------|
| Patch `Light_VisibilityTest` (0x60B050) → `mov al,1; ret 4` | Identified root cause of light disappearance | Easy — 5 byte patch, fix is ready |
| Force sector light list to include all lights | If Light_VisibilityTest patch doesn't help, lights may not be in the iteration list at all | Hard — need to understand sector system |
| Patch Light Draw virtual method internal culling | Build 025 hypothesis: light's own Draw method may clip | Medium — need vtable analysis |
| Patch LOD alpha fade at 0x446580 | 10 callers, may fade geometry at distance | Medium — need to verify if it affects anything post-sector-patch |
| Trace `"Disable extra static light culling and fading"` string at 0xEFF384 | Engine has a debug toggle for light culling — might be activatable | Easy — find the config flag and set it |
| Investigate 0x41F96A object visibility check | Uses same threshold, different code path | Low priority — sector patch may cover this |
| Investigate particle/effect distance culling at 0x446B5A, 0x446BE0 | Particles/effects may disappear at distance | Low priority — not related to lights |
| Force all sectors to populate all lights in their light lists | Nuclear option if per-light patches don't work | Hard — deep sector system RE needed |
| Shorter movement distances in test macro | Test if lights work at close range after 0x60B050 patch | Easy — just change RNG bounds |

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

### Light Pipeline
```
Sector light list ([param+0x1B0] count, [param+0x1B8] array)
  └→ Light_VisibilityTest (0x60B050) ← UNPATCHED, BLOCKS LIGHTS
       └→ Frustum 6-plane test (0x60CE20) ← patched (NOP)
            └→ Light Draw (vtable[0x18]) ← unexplored
```

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

**Patch `Light_VisibilityTest` at 0x0060B050** — this is the single identified blocker. The fix is 5 bytes (`B0 01 C2 04 00` = `mov al, 1; ret 4`). If this works, lights should remain visible at all positions. If it doesn't, investigate the sector light list population upstream.

---

## Decision Tree for Next Failure

```
Patch Light_VisibilityTest (0x60B050)
├── Lights now stable at all positions → DONE (miracle build)
└── Lights still disappear
    ├── Check proxy log: was patch applied?
    │   └── No → fix VirtualProtect / address
    ├── Check: do lights appear briefly then vanish?
    │   └── Yes → Light Draw method internal culling (vtable[0x18])
    └── Check: do lights never appear at far positions?
        └── Yes → Sector light list not populated
            ├── Trace "Disable extra static light culling and fading" config string
            └── RE the sector light list builder to force all lights
```
