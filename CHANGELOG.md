# CHANGELOG.md — TombRaiderLegendRTX Session Log

> **Purpose:** Cross-session memory for Claude Code. Every session reads this first, every session updates it.
> **Format:** `[YYYY-MM-DD HH:MM] LABEL — Summary` followed by findings, patches, test results, dead ends.
> **Full build history:** See `docs/status/WHITEBOARD.md` for the complete 44-build narrative.

---

## [2026-04-07] BOOTSTRAP — Autonomous workflow initialized
- Created CLAUDE.md encoding all institutional knowledge from 44 builds + 116 commits
- Created CHANGELOG.md for cross-session continuity
- **ONE blocker remains:** Anchor geometry not submitted at distance (both stage lights vanish)
- Hash instability is RESOLVED — asset hashes stable, generation hash cosmetic flash only
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

- [ ] What does TerrainDrawable (0x40ACF0) do? What struct? Where is its distance/frustum check?
- [ ] Are the anchor meshes terrain geometry going through the terrain path?
- [ ] Does dx9tracer capture show anchor mesh hashes present at near but absent at far?
- [ ] What is Lara's character mesh hash? (always drawn — guaranteed anchor)
- [ ] Does LOD alpha fade (0x446580) affect post-sector-patch visibility?
- [ ] Are there additional render paths beyond the 3 identified (RenderVisibleSectors, SceneTraversal, moveable loop)?

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