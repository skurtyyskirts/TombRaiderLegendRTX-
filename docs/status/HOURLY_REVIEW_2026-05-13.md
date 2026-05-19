# TRL RTX Remix — Hourly Review Report
**Date:** 2026-05-13 | **Last build:** 079 (skinned decl normalization, FAIL: shader-route mismatch) | **Days since build 075 proved pipeline:** 34

---

## Critical Finding: 34-Day Stall on Known-Easy Fix

The primary blocker — **stale anchor mesh hashes in mod.usda** — has been documented as the fix since build 075 (April 9). The solution is unchanged: fresh Remix capture near Peru stage, extract current mesh hash IDs, update mod.usda. This requires manual Remix developer menu interaction.

Since the last hourly review (April 28, 15 days stalled), **two builds shipped** (078 perf, 079 skinned decl), but neither addresses the primary blocker. A secondary workstream (skinned character hash drift) has also stalled on an unanswered diagnostic question.

---

## New Theories

### 1. Skinned Decl Normalization Could Work on the Shader Route If Done ONLY for the Remix-Facing Decl

Build 079's decl normalization (strip BLENDWEIGHT/BLENDINDICES) only fires on the null-VS path (line 3706). The shader route (line 3691-3697) passes through without swapping. The reasoning was that removing blend elements from the decl would break the bound VS. But there's a subtlety: **Remix's vertex capture reads the bound decl to determine geometrydescriptor hash BEFORE the draw executes.** If we could set the normalized decl, let Remix observe it, then swap back to the original decl before the actual draw... the timing doesn't work in D3D9 (decl is read at draw time), so this approach is invalid.

The correct path remains: route skinned draws to null-VS and accept bind-pose through Remix.

### 2. Lara Is Almost Certainly SHORT4, Not FLOAT3

Build 079 log shows 579 SHORT4 vs 21 FLOAT3 draws per frame. Lara is the dominant renderable character. The 21 FLOAT3 draws are likely particles, water, or HUD elements. If Lara is SHORT4 skinned, she goes through `TRL_ShouldShaderRouteAnimatedShort4Draw()` → returns 1 (shader route) because `useVertexCaptureEffective=true`. The fix should target the SHORT4 animated path, not the FLOAT3 path.

**Validation:** Deploy build 079 DLL to the correct game directory. The always-on `SKINNED decl=` log will confirm Lara's position type and the exact decl elements present.

### 3. `rtx.geometryHashGenerationRoundPosTo` Is a Zero-Cost Experiment

This Remix setting is documented in the reference (`docs/reference/rtx-conf-reference.md:31`, `docs/reference/hash-stability.md:87`) but has NEVER been added to rtx.conf. It rounds vertex positions before generation hashing. Adding `rtx.geometryHashGenerationRoundPosTo = 0.01` could absorb skinned mesh position jitter in the generation hash without any proxy changes. It won't help if the problem is asset hash drift (since asset hash uses model-space positions under vertex capture), but it's a 30-second config test.

### 4. Skinned Hash Drift Is Almost Certainly Generation-Hash Only (NEW EVIDENCE)

dxvk-remix source confirms `SkinningData::computeHash()` produces a `boneHash` separate from the geometry asset hash. The asset hash is controlled by `geometryAssetHashRuleString` and **does not include blend weight/index data**. Since TRL's asset hash rule is `positions,indices,texcoords,geometrydescriptor`, and blend data is excluded, **the asset hash for skinned meshes should already be stable**. The observed drift in debug view 277 is almost certainly the generation hash flickering — which is expected behavior for animated meshes.

**This may close the skinned hash drift issue entirely.** The user just needs to confirm they're looking at the generation hash view (composite 2), not the asset hash view (composite 1).

### 5. Anti-Culling Config Drift Between Documentation and rtx.conf

The task prompt states anti-culling is active (`enable = True`, `fovScale=2`, `farPlaneScale=10`, `numObjectsToKeep=10000`). The actual rtx.conf has `enable = False` with `numObjectsToKeep = 1000`. This mismatch suggests the task prompt's "Active Config" section is stale. The proxy's own draw cache handles anti-culling instead. No action needed, but the task prompt should be updated.

---

## Priority Experiments (Ranked)

### 1. [HIGH] Fresh Remix Capture → Update mod.usda Hashes
**Still the #1 blocker. No alternative path exists.**
- Position Lara near the Peru stage buildings
- Press X (Remix menu key) → Capture
- Extract the 5+ building mesh hash IDs from the capture
- Update `mod.usda` anchor hashes
- Run `python patches/TombRaiderLegend/run.py test --build`

**Expected outcome:** Red and green stage lights appear.
**Validation:** Both colors visible in all 3 camera positions, lights shift with camera pan.

### 2. [HIGH] Correctly Deploy Build 079 and Read SKINNED Log
Build 079 was deployed to the wrong directory. The `SKINNED decl=` log entries (always-on, not gated by DIAG_ENABLED) will answer:
- Is Lara FLOAT3 or SHORT4? (Almost certainly SHORT4 based on draw mix)
- How many unique skinned decls exist?
- Are the normalized clones being created?

This data determines the correct fix path for skinned hash drift.

**Expected outcome:** Log reveals Lara is SHORT4 skinned with BLENDWEIGHT+BLENDINDICES elements.
**Validation:** `ffp_proxy.log` contains `SKINNED decl=` entries with element type breakdown.

### 3. [HIGH] Confirm Asset Hash vs Generation Hash View
The user's hash debug screenshots show Lara flickering. This could be:
- **Generation hash view** (debug view 277, composite 2): Expected to flicker on skinned meshes — positions change each frame due to animation. NOT a bug.
- **Asset hash view** (debug view 277, composite 1): Should be stable because asset hash uses model-space positions. If THIS flickers, it's a real bug requiring proxy fix.

**Validation:** Ask user to cycle composite views in Remix debug mode. Asset hash = composite 1; generation hash = composite 2. If asset hash is stable, skinned drift is a non-issue.

### 4. [MED] Add rtx.geometryHashGenerationRoundPosTo to rtx.conf
Zero-cost experiment. Add to rtx.conf:
```ini
rtx.geometryHashGenerationRoundPosTo = 0.01
```
**Expected outcome:** Generation hash flicker reduced on skinned meshes.
**Validation:** Hash debug view (generation) shows fewer color changes per frame on Lara.

### 5. [MED] Override Skinned SHORT4 Routing to S4_ExpandAndDraw
If experiment #2 confirms Lara is SHORT4, modify `TRL_ShouldShaderRouteAnimatedShort4Draw()` to return 0 for skinned draws (new INI toggle `SkinnedShort4Route=null_vs`). This routes Lara through `S4_ExpandAndDraw` (already null-VS), where the decl normalization swap will engage.

**Tradeoff:** Lara renders in bind-pose through Remix (no bone animation visible in path-traced output). The rasterized game-side output still shows animated Lara.

**Expected outcome:** Lara's asset hash stabilizes.
**Validation:** Hash debug (asset) shows solid color on Lara across frames.

### 6. [LOW] LOD Alpha Fade Investigation (Layer 14)
Address 0x446580 with 10 callers. Still unexplored after 79 builds. Low priority because 3749 draws/scene confirms geometry IS being submitted. If LOD fade were actively hiding geometry, draw counts would be lower. However, LOD fade could affect distant building meshes specifically — worth investigating if hash capture reveals missing building draws.

### 7. [LOW] TRLAU-Menu-Hook Source Code Grep
The TRLAU-menu-hook has portal visualization code that likely contains sector/portal addresses. Could reveal layers 14-15 without RE work. No new commits since last check.

---

## Stale Experiments to Remove from Pending List

| Experiment | Why Stale |
|-----------|-----------|
| Tiered frustum threshold binary search | All 36 culling layers mapped, 32 patched. 3749 draws/scene. Culling is solved. |
| Per-frame view distance ramping | Same — culling solved. |
| Aggressive Remix anti-culling | Anti-culling disabled (causes freeze). Proxy draw cache handles retention. |
| Box culling around player | All culling layers patched. No remaining culling to work around. |
| VB content stability check | Confirmed stable under `useVertexCapture=True` + CPU SHORT4→FLOAT3 expansion. |
| cdcEngine SceneLayer identification | No new commits since Dec 2024. Not useful for current blockers. |
| Static object World matrix stability validation | Addressed by VP lock (H4). World geometry hashes confirmed stable since build 073. |
| RenderLights_FrustumCull patch at 0x0060C7D0 | Already patched (build 072). Dead end #13 — adds draws but doesn't fix hash mismatch. |
| Light_VisibilityTest sub-function surgical analysis | Already done — `mov al,1; ret 4` at 0x60B050 since build 031. Light submission is not the problem. |

---

## Config Changes to Test

| Setting | Current | Proposed | Expected Effect |
|---------|---------|----------|----------------|
| `rtx.geometryHashGenerationRoundPosTo` | (unset) | `0.01` | Absorb float jitter in generation hash for skinned meshes |

No other config changes recommended until fresh capture resolves the hash blocker.

---

## Community Intel (2026-05-13)

### dxvk-remix — Critical Skinning Fix (May 6, 2026)

**Commit `95a5ecb` — `[REMIX-5347]` Fix crashes with too many bones or invalid blend weights on skinned meshes.** The fixed-size `m_stagedBones` vector (hard cap 256x256) was replaced with a dynamic `SkinningMatrixPool`. A second fix adds graceful handling for skinned meshes arriving without a blend weight buffer — previously crashed/asserted, now logs warning and returns early.

**Direct relevance to TRL:** The null-VS FLOAT3 path sends skinned draws without valid blend weight buffers. This was a live crash path in older Remix builds. **Update to a dxvk-remix build after May 6, 2026.**

### dxvk-remix — Asset Hash Does NOT Include Blend Data (confirmed from source)

`SkinningData::computeHash()` produces a `boneHash` that is **separate from the geometry asset hash**. The asset hash is controlled by `geometryAssetHashRuleString` which does not include bone data. This means: **BLENDWEIGHT/BLENDINDICES changing frame-to-frame do NOT affect the asset hash — only the generation hash.** This strongly supports hypothesis #2 (user is seeing generation hash drift, which is expected and not a bug).

### RTX Remix Issues

- **#528 — Anti-culling geometry deterioration** (Sonic Adventure DX): Open, unresolved. Progressive corruption when anti-culling enabled. Confirms the proxy's in-engine NOP approach is correct — do NOT switch to Remix-side anti-culling.
- **#775 — Partially skinned mesh seams**: Closed Apr 30, 2026. Fix for HW/SW skinning strip boundaries. Not directly relevant to TRL.

### TRLAU-Menu-Hook v2.5 (Feb 28, 2026)

"Fix crash with next generation graphics in Legend" — addresses TRL's D3D shader 3.0 path. The proxy already strips PUREDEVICE (build 076). Worth investigating whether v2.5 patches a different address that should be added to `TRL_ApplyMemoryPatches`.

### cdcEngine — SceneLayer::s_enabled Confirmed

`SceneLayer::s_enabled` (static bool) controls scene traversal. Its address in `trl.exe` could provide another culling layer toggle. Find via `datarefs.py` searching for the `SceneLayer::Render` call site with a `test byte ptr [addr], 1` guard pattern.

### cdcEngine — No New Commits (still Dec 2024)

---

## State Machine: Where We Are

```
[DONE] Pipeline confirmed (build 075: purple test light visible)
[DONE] Cold launch stable (build 077: DrawCache use-after-free fixed)
[DONE] Perf optimized (build 078: -13.6% DLL, SetTransform cache)
[FAIL] Skinned hash drift (build 079: fix doesn't engage on shader route)
  └─ BLOCKED ON: diagnostic question (asset vs generation hash view)
  └─ BLOCKED ON: confirm Lara is SHORT4 (redeploy build 079)
[BLOCKED] Stage lights visible → BLOCKED ON: fresh Remix capture for hash IDs
[NOT STARTED] Final PASS build with both stage lights
```

**Single highest-priority action:** Fresh Remix capture near Peru stage → extract mesh hashes → update mod.usda → test. This has been the answer for 34 days.

**If user cannot do manual capture:** Anchor all test lights to `mesh_574EDF0EAD7FC51D` (known-stable from build 075) as interim proof that the full pipeline works. Stage lights won't be at correct building positions, but they'll be visible.
