# CHANGELOG.md — TombRaiderLegendRTX Session Log

> **Purpose:** Cross-session memory for Claude Code. Every session reads this first, every session updates it.
> **Format:** `[YYYY-MM-DD HH:MM] LABEL — Summary` followed by findings, patches, test results, dead ends.
> **Full build history:** See `docs/status/WHITEBOARD.md` for the complete 44-build narrative.

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
- [ ] Can Layer 3 frustum culler (0x40C430) be safely bypassed by redirecting to 0x40C390?
- [ ] Does LOD alpha fade (0x446580) affect distant geometry visibility?

### Next Steps
1. **HIGHEST**: Patch Layer 3 frustum culler — redirect 0x40C430 entry to 0x40C390 (uncull path) at runtime
2. **HIGH**: Live-verify with `livetools collect 0x40C430 0x40C390 0x40ACB0 --duration 5` to measure Layer 3 kill rate
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
| 1 | 042 | Re-parent lights to largest mesh (7DFF31ACB21B3988) | Worse — large mesh not always drawn || 2 | 043 | Aggressive 7-NOP set in SceneTraversal | Crashed, not preserved |
| 3 | 019–037 | Treating "red at distance" as real stage light | Was fallback light — reframed build 038 |
| 4 | 040 | All 11 conditional exits in SceneTraversal (0x407150) | Draw counts 190K but anchors still vanish |
| 5 | 032 | Config flag at 0x01075BE0 | No code xrefs, not connected |
| 6 | 025 | Pending-render flag NOPs (0x603832, 0x60E30D) | No effect on bottleneck |
| 7 | 026 | LightVolume_UpdateVisibility state NOPs | Patches not confirmed in log — silent failure |

---

## Open Questions

- [x] What does TerrainDrawable (0x40ACF0) do? → Constructor for 0x30-byte draw descriptor; real draw at 0x40AE20
- [x] Are the anchor meshes terrain geometry going through the terrain path? → Shared 3-layer pipeline, not separate
- [x] Are there additional render paths beyond the 3 identified? → No; terrain uses same sector→submit→frustum-cull pipeline
- [ ] Does dx9tracer capture show anchor mesh hashes present at near but absent at far?
- [ ] What is Lara's character mesh hash? (always drawn — guaranteed anchor)
- [ ] Does LOD alpha fade (0x446580) affect post-sector-patch visibility?
- [ ] Can Layer 3 frustum culler (0x40C430) be safely bypassed by redirecting to 0x40C390?

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