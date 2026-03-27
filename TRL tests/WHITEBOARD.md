# TRL RTX Remix — Results Whiteboard

**Last updated:** 2026-03-27
**Builds completed:** 001-033 (20 builds, 003-015 not preserved)
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
| Sector light count gate disabled | DONE | NOP at 0xEC6337 (build 033) |
| Lights stable across all positions | **FAILING** | Lights vanish when Lara moves to a sector without stage lights in its list |
| Remix light anchors hold on movement | **FAILING** | Consequence of above |

---

## The Two Remaining Problems

### Problem 1: Lights Disappear on Movement

**`Light_VisibilityTest` patched (build 031) — lights still disappear at distance.** The patch correctly bypasses per-light AABB checks but is insufficient on its own.

**Root cause (confirmed build 031):** `RenderScene_Main` (0x603810) iterates sectors and only calls `RenderScene_LightPass` if `sector+0x84 + sector+0x94 != 0`. The per-sector light array at `[sector+0x1B0]` is only populated for sectors near the camera. When Lara moves to a sector that does not include the stage lights in its list, `[sector+0x1B0]` (light count) is 0 and `RenderLights_FrustumCull` is skipped entirely.

**Config flag tried (build 032):** Stamped engine flag at `0x01075BE0` ("Disable extra static light culling and fading") — no effect. Flag has no code xrefs and is not connected to the light collection system.

**Sector light count gate NOPed (build 033):** Added NOP at `0xEC6337` to bypass the light count gate. Untested in valid screenshots (macro failure in build 033).

**Remaining suspects:** `FUN_006033d0` and `FUN_00602aa0` (called before `RenderScene_Main` in `RenderScene_TopLevel`) — these likely populate per-sector light lists and apply proximity filtering. Finding and patching the proximity filter there is the next step.

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
| 12. Sector light count gate | 0xEC6337 | JNZ gate: skips light pass if sector light count == 0 | Yes — NOPed | 033 |
| 13. Sector light list population | FUN_006033d0 / FUN_00602aa0 | Upstream: builds per-sector light arrays (proximity filter) | **NO — root cause of remaining failure** | — |
| 14. LOD alpha fade | 0x446580 | 10 callers, may fade geometry invisible at distance | **UNEXPLORED** | — |
| 15. Scene graph sector early-outs | Unknown | Sector-based submission skipping | **UNEXPLORED** (may be covered by layer 6) | — |
| 16. Light Draw virtual method | vtable[0x18] per light | Internal culling inside light's Draw method | **UNEXPLORED** (hypothesis from build 025) | — |

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

**Conclusion:** Per-light culling gates all patched. Remaining blocker is upstream sector light list population — FUN_006033d0 / FUN_00602aa0 populate per-sector lists with only nearby lights.

---

## What Has NOT Been Tried

| Idea | Why It Matters | Difficulty |
|------|----------------|------------|
| Fix pause menu in test macro | Build 033 macro captured pause screen instead of gameplay — add ESCAPE keypress after level load | Easy — test infrastructure fix |
| Decompile `FUN_006033d0` and `FUN_00602aa0` | These are called before `RenderScene_Main` and likely populate per-sector light lists with proximity filter | Medium — static analysis needed |
| Patch the proximity filter in light list builder | Remove the "only include nearby lights" condition so all lights enter every sector's list | Hard — need to find and understand the filter |
| Force `sector+0x84` non-zero for all sectors | `RenderScene_Main` gates on this field; if it's 0 a sector skips the light pass entirely | Medium — find the setter function |
| Patch Light Draw virtual method internal culling | Build 025 hypothesis: light's own Draw method may clip | Medium — need vtable analysis |
| Patch LOD alpha fade at 0x446580 | 10 callers, may fade geometry at distance | Medium — verify if affects anything post-sector-patch |
| Investigate 0x41F96A object visibility check | Uses same threshold, different code path | Low priority — sector patch may cover this |
| Investigate particle/effect distance culling at 0x446B5A, 0x446BE0 | Particles/effects may disappear at distance | Low priority — not related to lights |

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
FUN_006033d0 / FUN_00602aa0 ← UNPATCHED, BUILDS PER-SECTOR LIGHT LISTS (proximity filter here)
  └→ Sector light list ([sector+0x1B0] count, [sector+0x1B8] array)
       └→ Sector light count gate (0xEC6337) ← NOPed (build 033)
            └→ Light_VisibilityTest (0x60B050) ← patched → always TRUE (build 031)
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

**Two steps needed:**

1. **Fix test macro** — build 033 captured the pause menu instead of gameplay. Add an ESCAPE keypress after level load to dismiss it. Re-run the existing proxy code (0xEC6337 NOP untested) with valid screenshots.

2. **Patch sector light list builder** — `FUN_006033d0` / `FUN_00602aa0` in `RenderScene_TopLevel` (0x60A0F0) populate per-sector light lists with a proximity filter. Decompile both to find and remove the filter so all level lights enter every sector's list.

---

## Decision Tree for Next Failure

```
Fix macro pause menu → re-run build 033 proxy code
├── Lights stable at all positions → DONE (miracle build)
└── Lights still disappear at distance
    ├── Check proxy log: was 0xEC6337 NOP applied?
    │   └── No → fix address
    ├── Check: does [this+0x1B0] drop to 0 when Lara moves?
    │   └── Yes → Sector light list not populated upstream
    │       ├── Decompile FUN_006033d0 + FUN_00602aa0
    │       └── Patch proximity filter in light collection function
    └── Check: do lights appear briefly then vanish?
        └── Yes → Light Draw method internal culling (vtable[0x18])
```
