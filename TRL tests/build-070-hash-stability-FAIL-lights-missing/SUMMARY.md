# Build 070 — Hash Stability Test

## Result
**FAIL-lights-missing** — No red or green stage lights visible in any of the 3 clean render screenshots. Scene uniformly dark with only fallback lighting.

## Test Configuration
- Date: 2026-04-08 07:16-07:18 UTC
- Level: Peru (Chapter 4, -NOMAINMENU -CHAPTER 4)
- Camera: Mouse-only pan (300px left, 600px right)
- Debug view: 277 (Phase 1), 0 (Phase 2)
- Asset hash rule: `positions,indices,texcoords,geometrydescriptor`
- useVertexCapture: False
- Anti-culling: Disabled

## Phase 1: Hash Debug Analysis
3 screenshots captured with camera pan (center/left/right). Hash debug view shows colored geometry blocks. Camera clearly moved between shots (different viewpoints visible). Hash stability is indeterminate — the different camera angles make direct color comparison difficult without overlay tools.

## Phase 2: Light Anchor Analysis
3 screenshots captured. **No red or green stage lights visible in ANY screenshot.** The scene is uniformly dark with cobblestone ground and wooden buildings, lit only by the fallback light. The anchor geometry for the 5 light meshes is not being submitted by the engine.

Camera did move between the 3 shots (different building perspectives confirm pan worked).

## Phase 3: Live Diagnostics

### Draw Call Census
`dipcnt` returned "Not installed" for all 3 camera positions — the DIP counter hook failed to install. Draw counts were tracked via proxy log instead.

**Proxy log draw counts (d= total draws per diagnostic frame):**
| Frame | Total (d) | S4 draws | F3 draws |
|-------|-----------|----------|----------|
| S502 (initial) | 2833 | 2459 | 374 |
| S516 (~7s later) | 670 | 429 | 241 |
| S560 (~30s) | 639 | 429 | 210 |
| S1498 (end) | 185 | 64 | 121 |

**Draw counts collapsed by 93%** (2833 → 185) over the session. The engine's culling systems are progressively removing geometry despite all 22+ patches.

### Patch Integrity
| Address | Expected | Actual | Status |
|---------|----------|--------|--------|
| 0xEFDD64 (frustum threshold) | -1e30 float | CA F2 49 F1 (-1e30) | OK |
| 0xF2A0D4/D8/DC (cull modes) | D3DCULL_NONE (1) | 01 00 00 00 x3 | OK |
| 0x407150 (cull function entry) | C3 (RET) | 55 (PUSH EBP) | **NOT PATCHED** |
| 0x60B050 (LightVisibilityTest) | B0 01 C2 04 | B0 01 C2 04 | OK |

**Note:** The proxy does NOT RET the cull function at 0x407150. Instead, it NOPs 11 conditional jumps inside the function (confirmed by proxy log: "NOPed cull jumps: 11/11"). The 22-layer table in CLAUDE.md says "RET at entry" but the actual proxy implementation uses internal NOP patching.

### Memory Watch
No VB mutation check was performed (memwatch not exercised).

### Function Collection
49,973 records of SetWorldMatrix (0x00413950) over 15 seconds (~3,331 calls/sec). This confirms the world matrix submission path is active.

SetWorldMatrix trace sample showed caller at 0x004150DF.

## Phase 4: Frame Capture Analysis
Skipped in this run (dx9tracer not deployed).

### Draw Call Diff
N/A

### Constant Evolution
N/A

### Vertex Format Consistency
N/A

### Shader Map
N/A

## Phase 5: Static Analysis
(Pending — static-analyzer subagent running in background. Findings will be in `patches/TombRaiderLegend/findings.md`)

Key observation from on-disk binary: All patch sites show original unpatched bytes, confirming patches are runtime-only (applied by proxy at CreateDevice time via VirtualProtect + memcpy). The static binary is unmodified.

## Phase 6: Vision Analysis
**Hash Debug (Phase 1):** The 3 screenshots show distinctly different camera angles with colored geometry blocks. The large ground plane changes color between shots (pink center, green left/right), suggesting generation hash instability as expected. Building geometry colors also shift between views.

**Clean Render (Phase 2):** All 3 screenshots show a dark Peru street scene with cobblestone ground and wooden buildings. NO colored lights (red or green) are visible in any shot. The scene is lit only by dim ambient/fallback lighting.

## Proxy Log Summary
- All patches applied at startup (11 cull jumps, sector visibility, light visibility, terrain cull gate, mesh submit gate, sector/portal/eviction/stream NOPs)
- New patches this build: MeshSubmit_VisibilityGate (0x454AB0), post-sector enable flag, stream unload gate, post-sector bitmask/distance cull, Sector_SubmitObject gates, mesh eviction NOPs, _level writers NOPs
- Draw counts: 2833 → 185 (93% drop over session lifetime)
- DrawCache: replayed 3 culled draws (minimal)
- S4 expanded declaration created (stride 32)

## Brainstorming: New Hash Stability Ideas
Hash stability is secondary to the lights-missing blocker. The generation hash (which includes positions) flickers with camera as expected; asset hash stability remains unverified without Toolkit mesh replacement testing.

## Open Hypotheses

1. **Progressive draw count collapse is the primary symptom.** Despite 22+ patches, draws drop from 2833 to 185. Something is actively removing geometry over time — possibly a streaming/eviction system or a culling path not yet identified.

2. **The draw count drop pattern (2833 → 670 → 185) suggests two distinct culling phases:**
   - Phase A (~S514→S516): 2833→670 (76% drop) — likely sector/portal visibility settling
   - Phase B (~S516→end): 670→185 (72% drop) — likely streaming/LOD eviction

3. **Mesh eviction was NOPed but may have secondary paths.** The proxy NOPs "SectorEviction x2 + ObjectTracker_Evict" but the draw count still collapses, suggesting additional eviction/unload mechanisms.

4. **TerrainDrawable (0x40ACF0) terrain cull gate at 0x40AE3E was NOPed** but the function is a constructor/initializer, not a per-frame render call. The actual terrain draw path may be elsewhere.

5. **The 0x407150 function is NOT being disabled** — only its internal conditional jumps are NOPed. If the function has additional early-exit logic or if newly-added code paths bypass it entirely, geometry could still be culled.

## Next Steps
1. **Investigate the draw count collapse timeline more precisely.** Add per-frame draw count logging to identify the exact frame where draws drop.
2. **Trace which draw calls disappear.** Use dx9tracer to capture frames at the initial high draw count (S502) vs. collapsed draw count (S1498) and diff them.
3. **Check if the streaming system is unloading sectors.** The proxy NOPs _level writers and stream unload gate, but there may be a separate streaming controller that evicts sector data from memory entirely.
4. **Verify all Sector_SubmitObject paths.** The proxy patches 2 gates but there may be additional submit paths not covered.
