# Builds 045–073 — The Hash Pipeline Campaign

> The technically densest stretch of the project. Vertex-buffer management, content-fingerprint caching, the FLOAT3 fix, the BVH frustum-culler bypass, and finally the first confirmed stable world hashes. **Builds 048–063 are not preserved** — a 16-build stretch of unsuccessful proxy iteration whose lessons survive in the build-064+ baseline.

This epoch is where the project transitioned from "patch every culling jump" to "understand why Remix's hashing flickers". The work here produced the stable-hashes result that is captured in `TRL tests/contenders/build-073-stable-hashes/` and the accompanying `TECHNICAL_ANALYSIS.md`.

## Builds 045–047 — Vertex buffer experiments

### Build 045 — D3DPOOL_MANAGED + per-frame VB flush

`S4_GetCachedExpVB` switched to `D3DPOOL_MANAGED` (1). Per-frame `S4_FlushVBCache()` called every EndScene.

**Result:**
- Hash debug: stable (cyan/green/yellow/magenta consistent across positions)
- Clean render: uniform brown/amber (flush too aggressive — 512 VB creates per frame)
- `SetWorldMatrix` called 14,100× in 15s (~940/sec)

`D3DPOOL_MANAGED` is the correct pool choice, but flushing the entire cache every frame is too aggressive. ([[Dead-Ends]] #8)

### Build 046 — Content fingerprint cache + null VS for all

Introduces a content fingerprint cache (XOR of first 32 VB bytes) replacing per-frame flush. Also nulls VS for **ALL** DIP draws (not just SHORT4). Removes `vertexshader` from generation hash rule.

**Result:** Rendering broken. Lara's face fills the screen at extreme close-up.

**Why:** FLOAT3 positions are **pre-transformed to view space by the game CPU**. The VS only applies projection. Nulling the VS for FLOAT3 removes the only remaining transform, leaving view-space positions interpreted as world-space — which puts the camera inside Lara's head. ([[Dead-Ends]] #9)

This is one of the project's most important discoveries: TRL has **two different vertex pipelines**, and they need different proxy handling.

- **SHORT4 static geometry:** positions are normalized integers in an AABB, need expansion to FLOAT3 in world coordinates. Proxy nulls VS, expands VB, draws via FFP.
- **FLOAT3 character meshes:** positions are FLOAT3 already, but pre-transformed to view space. Must NOT null VS for these draws (or projection won't apply). Build 071b fixes this with a different path.

`SetWorldMatrix` called 43,604× this build (3× build 045) — the unnecessary VS nulls were causing extra state changes.

### Build 047 — Removing positions from asset hash

Removed `positions` from `geometryAssetHashRuleString` (kept generation rule). Reverted VS-null for FLOAT3.

**Result: catastrophic hash collision.** The entire scene rendered as one hot pink color in hash debug. `indices,texcoords,geometrydescriptor` is not unique; many TRL meshes share index layouts and stride. ([[Dead-Ends]] #10)

**Positions are required in the asset hash. Permanent rule.**

## Builds 048–063 — The lost stretch

Sixteen builds in a row, none archived. The proxy and patch list went through major iteration:
- Terrain rendering investigation (`TerrainDrawable` at `0x40ACF0` is a constructor, not the dispatch — real dispatch at `0x40AE20`)
- Terrain flag gate NOPed at `0x40AE3E`
- Null-check guard trampoline at `0xEDF9E3`
- `ProcessPendingRemovals` stale-field patch (resolved crash at `0xEE88AD`)
- `MeshSubmit_VisibilityGate` (`0x454AB0`) patched to `xor eax,eax; ret`
- Sector already-rendered skip at `0x46B7F2`
- Post-sector bitmask/distance culls at `0x40E30F`, `0x40E3B0`
- Stream unload gate at `0x415C51`
- Mesh eviction (3 sites: `SectorEviction` ×2 + `ObjectTracker_Evict`)
- Post-sector loop enable at `0xF12016`, `0x10024E8`
- Frustum screen-size rejection at `0x46C242`, `0x46C25B`
- 4 `SectorPortalVisibility` reset writes
- `Sector_SubmitObject` gates at `0x40C666`, `0x40C68B`
- Level writers at `0x46CCB4`, `0x4E6DFA`

These are all layers 22–35 in the [[36-Layer-Culling-Map]]. All confirmed in place by build 064's static-analyzer verification.

## Builds 064–070 — The hash-stability test protocol

### Build 064 — Workflow change

First build under the new hash-stability test workflow (camera-only pan, no WASD). Macro: 300px left + 600px right (nets 300px right of center). Three screenshots per phase (center, left, right).

Phase 1 was invalid — only loading screen captured. The 15s wait after window detection was insufficient for Remix + Peru level initialization. Draw counts 244–245 stable in the loading screen. Frustum threshold and cull globals patched correctly.

### Builds 065–070 — Stability under longer test windows

- Build 065: Longer load wait. Hash debug stable. ~650–657 draws per diagnostic sample. **Light anchor hashes documented:** `mesh_5601C7C67406C663`, `mesh_ECD53B85CBA3D2A5` (red), `mesh_AB241947CA588F11`, `mesh_EFD9D357F2D3A56F` (green), `mesh_D4A147BEEBC48792` (red).
- Build 066: `DRAW_CACHE_ENABLED 0` — disabled the 4096-entry draw replay cache. No change. ([[Dead-Ends]] #11)
- Build 067: Removed 1e-4 epsilon from `mat4_changed()` — VP inverse now always recalculates. No change. ([[Dead-Ends]] #12)
- Build 068: Re-enabled `0x60B050 + 0xEC6337 + 0x60E3B1` patches. **No crash** — the crash at `0xEE88AD` was fixed in builds 048–063 by the `ProcessPendingRemovals` + null-check trampoline. 20+ patches confirmed active. Problem confirmed **upstream of light pipeline**.
- Build 069: No code change. **Draw counts collapse during session: 2647 → 1390 → 911 → 673** (75% culled despite patches). `SetWorldMatrix` 43,188 hits in 15s.
- Build 070: No code change. Draw counts collapsed 93% over session: 2833 → 670 → 639 → 185 (s4: 2459 → 64, f3: 374 → 121). **Confirmed proxy at `0x407150` uses NOP-jump strategy (not RET).** CLAUDE.md previously claimed "RET at entry" — this was corrected here.

The draw-count collapse over session duration is the new mystery. The patches stay in place but the proxy emits fewer and fewer draws as the session continues.

## Builds 071, 071b — The FLOAT3 fix and Lara visible

### Build 071 — Expanded anchor mesh list

`mod.usda` expanded from 5 to 8 anchor mesh hashes:
- Original 5: `mesh_2509CEDB7BB2FAFE`, `mesh_47AC93EAC3777CA5`, `mesh_DD7F8EE7F4F3969E`, `mesh_CE011E8D334D2E48`, `mesh_2AF374CD4EA62668`
- Added: `mesh_5601C7C67406C663`, `mesh_ECD53B85CBA3D2A5`, `mesh_AB241947CA588F11`

Draws stable at ~2845. `SetWorldMatrix` 51,484 calls in 15s from caller `0x004150DF`. Lara still not visible.

### Build 071b — Lara visible for the first time

**FLOAT3 draw path fixed.** New pattern in both `WD_DrawIndexedPrimitive` and `WD_DrawPrimitive` for the FLOAT3 branch:

```c
// Save VS
GetVertexShader(&saved_vs);
// Null VS — Remix sees FFP draw
SetVertexShader(NULL);
// Apply FFP state (transforms already pushed by SetTransform)
ApplyFFPState();
// Draw
real_DrawIndexedPrimitive(...);
// Restore VS so game can continue
SetVertexShader(saved_vs);
```

This mirrors the SHORT4 expansion pattern but skips the VB expansion (FLOAT3 doesn't need it).

**Result: Lara is visible for the first time in the project's history.** ~684 draws/frame (429 SHORT4 + 255 FLOAT3). The breakthrough here is realizing that with `useVertexCapture=False` Remix skips all VS-bound draws — and TRL's character meshes were all VS-bound. The null-VS-then-FFP pattern makes them visible.

Stage lights still missing.

## Build 072 — RenderQueue_FrustumCull bypass

**Layer 31** patched: `RenderQueue_FrustumCull` (`0x40C430`) → 5-byte JMP to `RenderQueue_NoCull` (`0x40C390`). The recursive BVH frustum culler is bypassed.

**Draws +29% (2845 → 3657: s4=3413 + f3=244).** Lara visible in hash debug for the first time. No lights.

This is the single biggest culling-layer win. The BVH was rejecting ~30% of all draws before any of the other patches had a chance to see them. ([[Dead-Ends]] #13 — the bypass works but doesn't fix the lighting blocker)

`SetWorldMatrix` 36,905 calls (slightly down from build 071).

## Build 073 — Vertex capture enabled

`rtx.useVertexCapture = True` (was False since build 045). Draws ~3651.

`SetWorldMatrix` drops to 21,181 (down significantly — GPU vertex capture reduces CPU per-draw work).

Small white dots appear in the scene. Initially flagged as a regression but later confirmed (build 075) as **denoiser/NRC artifacts**, not light placement — they don't scale with light radius.

### The contenders/build-073-stable-hashes split

The standard `build-073-vertexcapture-true-FAIL-lights-missing/` folder records this build as FAIL because stage lights weren't visible. But for **world hash stability**, this is the first PASS in project history.

A second folder, `contenders/build-073-stable-hashes/`, was created to capture this contender result with its own `TECHNICAL_ANALYSIS.md`:
- The 30-layer culling architecture
- The three-phase culling pipeline
- Why stable hashes matter for Toolkit replacement
- Why this configuration deserves to be the baseline for future work

That technical analysis is now [[Stable-Hashes-Technical-Analysis]] in this wiki.

## What this epoch established

- **Two vertex pipelines** in TRL: SHORT4 static (needs expansion + null VS) and FLOAT3 character (pre-view-space, needs null VS + save/restore but no expansion).
- **The FLOAT3 fix.** The pattern that finally made Lara visible. Reusable for any port that has CPU-pre-transformed character geometry.
- **Layer 31 — the BVH frustum culler.** The biggest single culling win. Reusable for any cdcEngine-derived game.
- **`useVertexCapture = True` produces stable hashes.** Counter-intuitive but proven.
- **Draws collapse over time** — still an open mystery; the patches are confirmed in place each frame but per-frame draw counts decay. Suspect: an unpatched layer somewhere upstream.
- **The contender pattern.** Build that passes one criterion but fails the headline criterion gets a `contenders/` folder with its own technical analysis. Sets a precedent for nuanced results.

## Open issues at end of epoch

- Stage lights still missing (now we know the geometry IS rendering, so the lights aren't culled — they just aren't anchoring)
- The white-dot artifact (later confirmed denoiser)
- Draws-collapse-over-time mystery
- mod.usda anchor hashes are starting to look stale (no specific evidence yet)

These pick up in [[Build-074-077-Asset-Pipeline]].

## See also

- [[Build-History-Index]]
- [[Stable-Hashes-Technical-Analysis]] — the contender deep dive
- [[Hash-Stability]] — why useVertexCapture stabilizes hashes
- [[SHORT4-Vertex-Decoding]] — the SHORT4 expansion that lives in builds 045–073's proxy
- [[Dead-Ends]] — items #8, #9, #10, #11, #12, #13 originate here
