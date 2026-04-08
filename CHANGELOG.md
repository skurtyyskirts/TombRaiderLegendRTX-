# CHANGELOG.md — TombRaiderLegendRTX Session Log

> **Purpose:** Cross-session memory for Claude Code. Every session reads this first, every session updates it.
> **Format:** `[YYYY-MM-DD HH:MM] LABEL — Summary` followed by findings, patches, test results, dead ends.
> **Full build history:** See `docs/status/WHITEBOARD.md` for the complete build narrative.

---

## [2026-04-08] BUILDS-069-073 — Layer 31 patch, FLOAT3 fix, hash verification needed

### Objective
Patch RenderQueue_FrustumCull (Layer 31, identified in terrain analysis), fix FLOAT3 draw path so character geometry goes through FFP, and begin hash anchor verification.

### Findings
- **Build 070**: Draw counts collapse 93% over session when anti-culling is disabled — engine is progressively submitting less. Proxy implementation confirmed: 11 NOP jumps inside 0x407150, NOT a RET at entry (CLAUDE.md corrected).
- **Build 071b**: FLOAT3 draw path was wrong — FLOAT3 draws were submitted with VS still bound. Remix ignores shader-bound draws when `useVertexCapture=False`. Fix: null VS, set FFP texture/lighting state, draw, restore VS. **Lara is now visible** for the first time.
- **Build 072**: Layer 31 (RenderQueue_FrustumCull at 0x40C430) bypassed via JMP → 0x40C390. Draw counts +29% (2845 → 3657). No crash. Lights still absent — anchor hashes likely stale.
- **Build 073**: `useVertexCapture=True` — small white dots appear in screenshots. May be stage lights at extreme HDR overexposure (`intensity=10000000, exposure=20`). Color unresolvable.
- **Anchor hash mismatch hypothesis**: mod.usda was built under different Remix settings. Current config (`useVertexCapture=False` + Layer 31 bypass) may produce different mesh hash IDs. Fresh capture needed.

### Patches Applied
- 0x40C430 → JMP to 0x40C390 (RenderQueue_NoCull): Layer 31, redirects recursive BVH frustum culler

### Test Results
- Build 069: FAIL — dipcnt failed, ~670 draws, patch integrity confirmed
- Build 070: FAIL — draw count collapse; anti-culling disabled baseline
- Build 071: FAIL — 8 anchor hashes; Lara not visible (FLOAT3 unpatched in this build)
- Build 071b: FAIL — Lara visible! FLOAT3 FFP fix; lights still absent
- Build 072: FAIL — Layer 31 bypass; +29% draws; lights still absent
- Build 073: FAIL — useVertexCapture=True; white dots visible (possible lights at overexposure)

### Dead Ends Discovered
- Layer 31 (RenderQueue_FrustumCull bypass) — adds draws but doesn't reveal anchor lights; hash mismatch likely cause

### Open Questions Updated
- [x] Can Layer 31 frustum culler (0x40C430) be safely bypassed? → Yes, no crash, +29% draws
- [ ] Are white dots in build 073 actually the stage lights at overexposure?
- [ ] Do current draw calls contain the 8 anchor mesh hashes from mod.usda?

### Next Steps
1. **HIGHEST**: Lower mod light intensity to ~1000 and test — confirm white dots turn red/green
2. **HIGH**: Fresh Remix capture near stage; compare mesh hashes against mod.usda
3. **MEDIUM**: If hashes correct — livetools memory search for anchor mesh objects near vs far

---

## [2026-04-08 03:45] TERRAIN-ANALYSIS — Complete terrain rendering path documented

### Objective
Decompile TerrainDrawable at 0x40ACF0, cross-reference with cdcEngine decompilation source, document the full terrain rendering pipeline.

### Findings
- **0x40ACF0 is a constructor**, not a draw function. Builds a 0x30-byte terrain draw descriptor. Zero culling logic.
- **The real draw function is TerrainDrawable_Dispatch at 0x40AE20** with two gates:
  - Gate 1 (0x40AE3E): flag 0x20000 check — already patched (NOP)
  - Gate 2 (0x40B0F4): NULL renderer pointer — must NOT be patched (crash guard)
- **Terrain is NOT an independent render path** as initially hypothesized. It shares the same three-layer sector rendering architecture as regular meshes.
- **cdcEngine source confirms**: `TERRAIN_DrawUnits` iterates 8 stream slots, `TERRAIN_CommonRenderLevel` iterates terrain groups, `DrawOctreeSphere` traverses octree — none contain distance/LOD culling. All culling is in the sector/portal and render queue layers.
- **14 conditional gates** identified across 5 functions in the terrain→DIP pipeline. 11 are patched.
- **Layer 3 frustum culler at 0x40C430 is the remaining bottleneck**: recursive bounding-volume intersection test that drops objects outside camera frustum, including distant light-anchor geometry.

### Key Sources
- TheIndra55/cdcEngine decompilation (terrain.h, terrain.cpp structs and loops)
- cdcengine.re documentation site
- Prior static analysis: patches/TombRaiderLegend/findings.md (lines 1042-2507)
- Knowledge base: patches/TombRaiderLegend/kb.h (lines 690-740)

### Documents Created
- `docs/TERRAIN_ANALYSIS.md` — comprehensive terrain rendering analysis with cross-references

### Open Questions Updated
- [x] What does TerrainDrawable (0x40ACF0) do? → Constructor for 0x30-byte draw descriptor
- [x] Are the anchor meshes terrain geometry going through the terrain path? → They share the same 3-layer pipeline
- [x] Can Layer 31 frustum culler (0x40C430) be safely bypassed by redirecting to 0x40C390? → Yes (build 072)
- [ ] Does LOD alpha fade (0x446580) affect distant geometry visibility?

### Next Steps
1. **HIGHEST**: Lower mod light intensity and test — confirm if white dots (build 073) are colored stage lights
2. **HIGH**: Fresh Remix capture near stage; verify anchor hashes match mod.usda
3. **MEDIUM**: Investigate LOD_AlphaBlend (0x446580) — 10 callers, may fade geometry at distance

---

## [2026-04-07] BOOTSTRAP — Autonomous workflow initialized
- Created CLAUDE.md encoding all institutional knowledge from 44 builds + 116 commits
- Created CHANGELOG.md for cross-session continuity
- **TWO blockers remain:**
  1. Anchor geometry not submitted at distance (both stage lights vanish)
  2. Hash instability — debug geometry view always shows changing colors; never verified with actual Toolkit mesh replacements
- Hash instability was INCORRECTLY marked as resolved — the claim that generation hash flickering is cosmetic was never verified end-to-end
- 22 culling layers identified, 20 patched — all exhausted except:
  - **Layer 22: TerrainDrawable (0x40ACF0) — UNEXPLORED, PRIME SUSPECT**
  - Layer 14: LOD alpha fade (0x446580) — unexplored
  - Layer 15: Scene graph sector early-outs — unexplored
- Next priorities:
  1. **HIGHEST:** Decompile TerrainDrawable at 0x40ACF0 via GhidraMCP — find its culling logic
  2. **HIGH:** dx9tracer frame capture at near vs far position — definitively shows which draw calls disappear
  3. **MEDIUM:** Find Lara's character mesh hash — anchoring to always-drawn mesh as workaround
  4. **MEDIUM:** Investigate LOD alpha fade at 0x446580 (10 callers)

---

## Dead Ends (Cumulative — DO NOT RETRY)

| # | Build | Approach | Why It Failed |
|---|-------|----------|--------------|
| 1 | 042 | Re-parent lights to largest mesh (7DFF31ACB21B3988) | Worse — large mesh not always drawn |
| 2 | 043 | Aggressive 7-NOP set in SceneTraversal | Crashed, not preserved |
| 3 | 019–037 | Treating "red at distance" as real stage light | Was fallback light — reframed build 038 |
| 4 | 040 | All 11 conditional exits in SceneTraversal (0x407150) | Draw counts 190K but anchors still vanish |
| 5 | 032 | Config flag at 0x01075BE0 | No code xrefs, not connected |
| 6 | 025 | Pending-render flag NOPs (0x603832, 0x60E30D) | No effect on bottleneck |
| 7 | 026 | LightVolume_UpdateVisibility state NOPs | Patches not confirmed in log — silent failure |
| 8 | 072 | Layer 31 (RenderQueue_FrustumCull bypass) | Adds 29% more draws but anchor lights still absent — hash mismatch likely root cause |

---

## Open Questions

- [x] What does TerrainDrawable (0x40ACF0) do? → Constructor for 0x30-byte draw descriptor; real draw at 0x40AE20
- [x] Are the anchor meshes terrain geometry going through the terrain path? → Shared 3-layer pipeline, not separate
- [x] Are there additional render paths beyond the 3 identified? → No; terrain uses same sector→submit→frustum-cull pipeline
- [x] Can Layer 31 frustum culler (0x40C430) be safely bypassed? → Yes (build 072), +29% draws, no crash
- [ ] Are white dots in build 073 the stage lights at extreme overexposure?
- [ ] Do current draw calls contain the 8 anchor mesh hashes in mod.usda?
- [ ] What is Lara's character mesh hash? (now visible in build 071b+ — guaranteed anchor candidate)
- [ ] Does LOD alpha fade (0x446580) affect post-sector-patch visibility?

---

## Session Template

```
## [YYYY-MM-DD HH:MM] LABEL — One-line summary

### Objective
What this session set out to do.

### Findings
- Finding 1 (with addresses if applicable)
- Finding 2

### Patches Applied
- Address: 0xNNNNNN — description of patch
### Test Results
- Build NNN: PASS/FAIL — description

### Dead Ends Discovered
- Approach → why it failed (add to cumulative table)

### Next Steps
- What the next session should tackle
```