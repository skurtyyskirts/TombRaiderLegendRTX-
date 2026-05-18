# 36-Layer Culling Map

> The canonical inventory of every culling layer in cdcEngine that gates geometry from reaching the renderer. **32 layers confirmed patched. 2 layers irrelevant to Remix. 2 layers unexplored.**

This is the single most-cited artifact of the project. Each row represents a discrete code path that can reject a draw call before it reaches `IDirect3DDevice9::DrawIndexedPrimitive`. Three years of work went into discovering, decompiling, and patching this list.

## Patch status legend

- ✅ Patched (NOP, RET, byte-rewrite, or runtime stamp)
- ⚪ Irrelevant to Remix (kept for completeness)
- 🔍 Unexplored — never decompiled or live-traced

## The table

| # | Layer | Address(es) | Patched? | First Build |
|---|-------|------------|----------|-------|
| 1 | Frustum distance threshold | `0xEFDD64` | ✅ `-1e30f` per BeginScene | 016 |
| 2 | Per-object frustum function | `0x407150` | ✅ 11 NOP jumps inside function (NOT RET at entry) | 016 |
| 3 | Scene traversal cull jumps (7×) | `0x4072BD`, `0x4072D2`, `0x407AF1`, `0x407B30`, `0x407B49`, `0x407B62`, `0x407B7B` | ✅ all NOPed | 016 |
| 4 | D3D backface culling | `SetRenderState` | ✅ `D3DCULL_NONE` | 016 |
| 5 | Cull mode globals | `0xF2A0D4 / D8 / DC` | ✅ stamped per scene | 029 |
| 6 | Sector/portal visibility | `0x46C194`, `0x46C19D` | ✅ both NOPed | 028 |
| 7 | Light frustum 6-plane test | `0x60CE20` | ✅ NOPed | 024 |
| 8 | Light broad-visibility test | `0x60CDE2` | ✅ NOPed | 024 |
| 9 | Pending-render flags | `0x603832`, `0x60E30D` | ✅ NOPed (no effect — see Dead Ends #6) | 025 |
| 10 | Light visibility state NOPs | 5 addrs in `LightVolume_UpdateVisibility` | ✅ Attempted — not confirmed in log | 026 |
| 11 | `Light_VisibilityTest` | `0x60B050` | ✅ `mov al,1; ret 4` | 031 |
| 12 | Sector light count gate | `0xEC6337` | ✅ NOPed | 033 |
| 13 | Sector light list population | `FUN_006033d0` / `FUN_00602aa0` | ⚪ Reframed irrelevant (build 038) | — |
| 14 | LOD alpha fade | `0x446580` | 🔍 **UNEXPLORED** | — |
| 15 | Scene graph sector early-outs | Unknown | 🔍 **UNEXPLORED** | — |
| 16 | Light Draw virtual method | `vtable[0x18]` | ⚪ Irrelevant — Remix anchors to geometry | — |
| 17 | RenderLights gate | `0x60E3B1` | ✅ NOPed | 037 |
| 18 | Sector light count clear | `0x603AE6` | ✅ NOPed | 037 |
| 19 | Additional SceneTraversal exits (4×) | `0x4071CE`, `0x407976`, `0x407B06`, `0x407ABC` | ✅ all NOPed | 040 |
| 20 | Far clip distance global | `0x10FC910` | ✅ `1e30f` per BeginScene | 041 |
| 21 | Camera-sector proximity filter | `0x46B85A` | ✅ NOPed | 044 |
| 22 | Terrain rendering path | `TerrainDrawable` (`0x40ACF0`) / `TERRAIN_DrawUnits` | ✅ terrain flag gate NOPed (`0x40AE3E`) | 045–063 |
| 23 | Null-check guard | `0xEDF9E3` | ✅ trampoline patched | 045–063 |
| 24 | `ProcessPendingRemovals` stale field | `FUN_00ProcessPendingRemovals` | ✅ patched (resolved crash at `0xEE88AD`) | 045–063 |
| 25 | MeshSubmit visibility gate | `MeshSubmit_VisibilityGate` (`0x454AB0`) | ✅ `xor eax,eax; ret` | 045–063 |
| 26 | Sector already-rendered skip | `0x46B7F2` | ✅ NOPed | 045–063 |
| 27 | Post-sector bitmask/distance culls | `0x40E30F`, `0x40E3B0` | ✅ NOPed | 045–063 |
| 28 | Stream unload gate | `0x415C51` | ✅ NOPed | 045–063 |
| 29 | Mesh eviction | `SectorEviction` (×2) + `ObjectTracker_Evict` | ✅ all 3 NOPed | 045–063 |
| 30 | Post-sector loop | `0xF12016` (enable flag), `0x10024E8` (gate) | ✅ enabled | 045–063 |
| 31 | Render queue frustum culler | `0x40C430` (`RenderQueue_FrustumCull`) | ✅ JMP to `0x40C390` (uncull path); +29% draws | 072 |
| 32 | Frustum screen-size rejection | `0x46C242`, `0x46C25B` | ✅ NOPed | 045–063 |
| 33 | `SectorPortalVisibility` resets | 4 write sites | ✅ all 4 NOPed | 045–063 |
| 34 | `Sector_SubmitObject` gates | `0x40C666`, `0x40C68B` | ✅ NOPed | 045–063 |
| 35 | Level writers | `0x46CCB4`, `0x4E6DFA` | ✅ NOPed | 045–063 |
| 36 | Null crash guard (scene traversal) | `0x40D2AC` | ✅ trampoline patched | 076 |

## How a draw call dies

The full cdcEngine submission path runs a draw through (approximately) this sequence of gates:

```
RenderFrame                              (0x450B00)
  └─ RenderVisibleSectors                (0x46C180)        ← Layer 6, 21, 26, 32, 33
       └─ RenderSector                   (0x46B7D0)        ← Layer 21
            └─ SceneTraversal wrapper    (0x443C20)
                 └─ SceneTraversal_CullAndSubmit (0x407150) ← Layer 2, 3, 19, 36
                      └─ Sector_SubmitObject                ← Layer 34
                           └─ RenderQueue_FrustumCull (0x40C430) ← Layer 31
                                └─ MeshSubmit_VisibilityGate (0x454AB0) ← Layer 25
                                     └─ DrawIndexedPrimitive

Light pass (separate path):
  RenderLights_FrustumCull (0x60C7D0)    ← Layer 7, 8, 17
       └─ Light_VisibilityTest (0x60B050) ← Layer 11
       └─ LightVolume_UpdateVisibility   ← Layer 10
```

`SceneTraversal_CullAndSubmit` is **not** patched at entry with a RET — that was attempted in early builds and discarded as it skipped legitimate submission logic. Instead the proxy NOPs all 11 internal conditional exits while keeping the function's submission body running. See build 039 in [[Build-016-to-044-Anti-Culling]].

## The two unexplored layers

### Layer 14: LOD alpha fade (`0x446580`)
Has 10 callers. May fade geometry invisible at distance via alpha rather than skipping submission. Worth a static-analyzer pass + live trace.

### Layer 15: Scene graph sector early-outs
The exact code addresses are unknown. Identified only because `RenderScene_TopLevel` (`0x60A0F0`) calls `FUN_006033d0` / `FUN_00602aa0` before `RenderScene_Main`, and these are suspected to populate sector light lists. May also gate sector visits.

Neither layer is on the critical path right now — the asset-pipeline blocker is in [[Build-074-077-Asset-Pipeline]], not in culling.

## Three side-effect lessons

When patching this many layers in a complex shipping renderer, side effects are constant. Three lessons recur:

1. **`Light_VisibilityTest` has side effects.** Bypassing `0x60B050` for type-0 lights calls `FUN_0060ad20` (sphere), for type-1 calls `FUN_005f9a60` (cone). The function isn't just a yes/no test — it updates internal light state. Forcing TRUE without preserving the state-update broke native fill lighting in build 029.
2. **`SceneTraversal_CullAndSubmit` does the actual submission.** A RET at `0x407150` skips the submission body and produces ~1,440 draws/scene instead of ~93,000. NOPing the 11 internal exits keeps the submission while disabling the rejection (build 039).
3. **`RenderQueue_FrustumCull` recursion.** `0x40C430` is a recursive BVH traversal. Bypassing it via JMP to `0x40C390` (the "no-cull" path) lifts draw count from 2,845 to 3,657 (+29%) and is the single biggest layer win (build 072).

The `culling-patch-reviewer` agent (`.claude/agents/culling-patch-reviewer.md`) audits any new culling patch proposal against these three lessons before approving.

## Patch verification

After build 074, all patches are deferred to the first `BeginScene` where `viewProjValid=1`. This avoids a menu crash that landed in builds where patches fired before the renderer was initialized. The verification log line that proves a patch is active is the `[PATCH OK]` entry written from the proxy's `TRL_ApplyMemoryPatches()`. Patches that silently fail to take effect (e.g. due to `VirtualProtect` failure on a missing page) **will not appear** in the log — that's the de-facto patch-applied verification. This was the diagnostic mechanism that caught the silent failure of the build-026 `LightVolume_UpdateVisibility` NOPs.

## See also

- [[Engine-Memory-Map]] — globals, sector layout, renderer chain
- [[Rosetta-Stone]] — master cross-reference (every address has a one-line `why`)
- [[Build-016-to-044-Anti-Culling]] — the build-by-build narrative of how this table was built
- [[Dead-Ends]] — culling-related approaches that were tried and failed
- [[FFP-Proxy-Pipeline]] — section 7 on patch application order
