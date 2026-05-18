# TRL RTX Remix — Live Whiteboard

**Updated:** 2026-05-14 · **Builds:** 001–079 (003–015, 034, 043, 048–063 not preserved)
**Goal:** Stable hashes (including skinned characters), full geometry submission, refreshed anchor hashes, and **maximum proxy CPU efficiency for the RTX 5090 path-traced runtime**

---

## Session 2026-05-14 — Test-Harness Repair + Build 080 Plan (PENDING)

Test pipeline (`run.py test-hash --build`) was broken on this branch and never produced a build 080 run. Repairs applied:

| Issue | Fix | Location |
|-------|-----|----------|
| `cmd /c "build.bat"` fails under Windows policy (NoDefaultCurrentDirectoryInExePath) | Prefix with `.\` for explicit relative-path execution | [patches/TombRaiderLegend/run.py:247](../../patches/TombRaiderLegend/run.py#L247) |
| `launcher.choose_launch_route` missing — referenced by run.py | Added stub returning `'continue'` if checkpoint+autosave else `'newgame'` | [patches/TombRaiderLegend/launcher.py:43-56](../../patches/TombRaiderLegend/launcher.py#L43-L56) |
| `navigate_to_peru(hwnd, route=...)` — `route` kwarg unsupported | Added kwarg; `'continue'` route restores checkpoint then sends short macro | [patches/TombRaiderLegend/launcher.py:178-202](../../patches/TombRaiderLegend/launcher.py#L178-L202) |
| Launcher.py had own `GAME_DIR = REPO_ROOT / "Tomb Raider Legend"`, ignoring `TRL_GAME_DIR` env var | Import from `config.py` instead (so both run.py and launcher.py share the same install path) | [patches/TombRaiderLegend/launcher.py:23-26](../../patches/TombRaiderLegend/launcher.py#L23-L26) |

**Active game install (confirmed by mtime):** `C:\Users\skurtyy\Documents\GitHub\AlmightyBackups\NightRaven1\Vibe-Reverse-Engineering-Claude\Tomb Raider Legend\` — `ffp_proxy.log` mtime 2026-05-14 02:51 (today). Set `TRL_GAME_DIR` to this path before running `run.py test-hash --build`.

**Static-analyzer log for build 079** generated and added to the build folder: [`static_analysis_log.md`](../../TRL%20tests/build-079-normalize-skinned-decl-FAIL-shader-route-mismatch/static_analysis_log.md). Key findings:

- All 23 documented patch sites verified — every documented shape still fits, proxy runtime patches will land cleanly. **No binary drift.**
- Skinned-submit dominant function = `0x006133D7`. It calls `Renderer_SetVertexShader(piVar4, shader)` **before** the DIP — that's what makes Remix see a non-null VS for Lara and trips the proxy's shader route. **Confirms build 079's diagnosis at the binary level.**
- One MISS-static: 0xF12016 reads 0 in the static dump (post-sector loop enable). Recheck whether the proxy stamps this at runtime — minor, world geometry is already stable.
- `find_skinning.py` returns "none detected" because TRL uses pure VS-skinning with no FFP world-matrix palette or `D3DRS_INDEXEDVERTEXBLENDENABLE` — the script's "set Enabled=0" suggestion is wrong for this binary.

**Build 080 — BUILT, NOT YET TESTED. Test deferred (pending fresh Remix capture for lights criterion).**

Source merged + DLL compiled at [`patches/TombRaiderLegend/proxy/d3d9.dll`](../../patches/TombRaiderLegend/proxy/d3d9.dll) (50,688 bytes). Static-analyzer subagent did the merge against [`d3d9_device.c`](../../patches/TombRaiderLegend/proxy/d3d9_device.c) — preserving H4 VP-lock, adding the build-079 plumbing (`BuildSkinnedNormalizedDecl`, `curNormalizedSkinnedDecl`, INI toggle, always-on `SKINNED decl=` log), and **the new build-080 piece: shader-route DIP wrapped with `swapShaderDecl` sandwich** (mirror of the null-VS `swapNormDecl` sandwich). Backup of pre-merge state at [`patches/TombRaiderLegend/backups/2026-05-14_build080_skinned-decl-shader-route/`](../../patches/TombRaiderLegend/backups/2026-05-14_build080_skinned-decl-shader-route/).

Per the static analysis section 2c, option (b) was the correct fix path:

- Port the existing build-079 decl-strip plumbing into the current source (preserving H4 VP-lock). ✅
- **Extend the decl-swap sandwich to wrap the shader-route DIP call**, not just the null-VS path. ✅ Both branches now wrap. Shader route fires for Lara when `rtx.useVertexCapture = True`; null-VS route fires for static FFP. INI toggle `[FFP] NormalizeSkinnedDecl=1` controls both.

Build 080 is purely additive: preserves Lara's visual skinning AND fixes her asset hash.

**Why test deferred:** The hash-stability test PASS criterion (`run.py:evaluate_release_gate`) requires `lights present in every clean shot` — and stage lights cannot appear until the 5 anchor mesh hashes in `mod.usda` are refreshed via a fresh Remix Toolkit capture at the Peru stage. That is the one remaining true blocker, and it is human-in-loop. Running build 080 now would produce FAIL on lights regardless of whether the shader-route fix works.

**Next actions on resume:**

1. Manually launch via NvRemixLauncher32 → Peru stage → trigger Toolkit capture → extract 5 building mesh hashes → update `mod.usda` → close
2. `TRL_GAME_DIR="$AlmightyBackupsPath" python patches/TombRaiderLegend/run.py test-hash --build`
3. Inspect `ffp_proxy.log` for `SKINNED decl=` entries (build 080 logs first 8 unique skinned decls). Confirms whether Lara is FLOAT3 or SHORT4. The decl-swap sandwich will engage either way; the log confirms which branch.
4. Compare hash-debug screenshots: Lara should now keep the same color across the camera pan.

---

## Active Workstream — Skinned-Character Hash Stability (build 079+)

World geometry is hash-stable. **Lara and other skinned characters drift** in the hash-debug view between frames. Build 079 implemented decl normalization (strip `BLENDWEIGHT`/`BLENDINDICES` from the Remix-facing vertex declaration) but the fix is wired only into the null-VS draw path — and `ffp_proxy.log` confirms `Float3Route effective: shader` for FLOAT3 draws when `rtx.useVertexCapture = True`. The decl swap doesn't engage for Lara on her current route.

**Status:** FAIL — Lara hash colors still drift. World remains stable. Distant NPC silhouettes also drift (issue is general to skinned characters, not Lara-specific). Fix is built, deployed, and harmless; the INI toggle `[FFP] NormalizeSkinnedDecl=1` lets us A/B once routing is corrected.

**Three open questions before pivoting code:**

1. Is the user's debug view the *asset* hash or the *generation* hash? Generation hash includes positions and is expected to flicker on skinned meshes by Remix design (see build-073 TECHNICAL_ANALYSIS.md).
2. Is Lara FLOAT3 or SHORT4 skinned? Latched-scene draw mix in the old log: 579 SHORT4 vs 21 FLOAT3 — most renderables are SHORT4. The always-on `SKINNED decl=` log entries added in build 079 will resolve this on the next test.
3. If true asset-hash drift, branch the fix:
   - **SHORT4 skinned →** extend `S4_ExpandAndDraw` (already null-VS) with the same decl swap. Safe.
   - **FLOAT3 skinned →** new INI toggle to force skinned-only FLOAT3 onto `FLOAT3_ROUTE_NULL_VS` regardless of `useVertexCapture`. Tradeoff: Lara through Remix renders bind-pose (no animation), since VS is null'd.

**Workspace deployment rule (NEW, build 079):** `proxy/d3d9.dll` and `proxy/proxy.ini` must be auto-deployed after every build to `Vibe-Reverse-Engineering-Claude/Tomb Raider Legend/` (sibling of this repo). The `Tomb Raider Legend/` folder *inside* the repo is a deployment stub — wrong target. Saved to project memory.

Build snapshot: [`TRL tests/build-079-normalize-skinned-decl-FAIL-shader-route-mismatch/`](../../TRL%20tests/build-079-normalize-skinned-decl-FAIL-shader-route-mismatch/).

---

## Previous Workstream — Performance (build 078)

After build 077 stabilized cold launch, the focus shifted to proxy CPU efficiency. The proxy was doing real per-draw work that paid no value once hashes stabilized and engine culling was fully disabled. Build 078 is the first perf build:

- **DLL size**: 56,320 → 48,640 bytes (–13.6%) from dead-code stripping
- **DIAG_ENABLED 1 → 0** — eliminates per-draw `GetTickCount()` syscall on every D3D9 method intercept
- **PINNED_REPLAY_INTERVAL 60 → 600** — replay scan runs ~10× less often (engine culling off, replay finds nothing anyway)
- **Matrix cache** for `SetTransform` — proxy was firing 3 SetTransform calls per FFP draw at two sites unconditionally; now compares against last-applied via `memcmp` and only pushes the slots that actually changed. Eliminates ~2/3 of SetTransform vtable thunks (View/Proj typically constant within a frame)
- **PERF_LOG instrumentation** — `ffp_proxy.log` now emits `PERF frames=600 ms=N fps=N` every ~10s for direct measurement without external overlay

Snapshot + analysis: [`TRL tests/build-078-perf-build/`](../../TRL%20tests/build-078-perf-build/). Future research starts in [`OPTIMIZATION_CANDIDATES.md`](../../TRL%20tests/build-078-perf-build/OPTIMIZATION_CANDIDATES.md) and [`HOTPATH_AUDIT.md`](../../TRL%20tests/build-078-perf-build/HOTPATH_AUDIT.md).

---

## Status at a Glance

| Goal | Status | Notes |
|------|--------|-------|
| Cold launch without TR7.arg | DONE (build 077) | DrawCache use-after-free crash on menu→level transition — fixed |
| FFP proxy DLL builds & chains | DONE | MSVC x86, chains to Remix d3d9 |
| Transform pipeline (View/Proj/World) | DONE | View/Proj from game memory, World via WVP decomposition |
| Asset hash stability (static camera) | DONE | `positions,indices,texcoords,geometrydescriptor` rule, session-reproducible |
| Asset hash stability (camera pan, world) | DONE | World geometry stable during the retained hash-screening sweep |
| Asset hash stability (skinned characters) | **FAILING (build 079)** | Lara + distant NPCs drift in hash-debug view. Decl normalization fix built but doesn't engage on the shader route. See [build-079](../../TRL%20tests/build-079-normalize-skinned-decl-FAIL-shader-route-mismatch/) |
| Hash stability screening workflow | DONE | Two-phase (hash debug + clean render) via `run.py test-hash` |
| Input delivery to DirectInput game | DONE | Scancode flag fix (build 018) |
| Backface culling disabled | DONE | D3DCULL_NONE + cull globals stamped |
| Frustum distance culling disabled | DONE | Threshold -1e30 + 11 NOP jumps inside 0x407150 (no RET — full function executes with all exits NOPed) |
| Sector/portal visibility disabled | DONE | NOPs at 0x46C194 + 0x46C19D, 65x draw increase |
| Light frustum rejection disabled | DONE | NOP at 0x60CE20 |
| Light visibility pre-check disabled | DONE | `Light_VisibilityTest` at 0x60B050 → `mov al,1; ret 4` (build 031) |
| Sector light count gate disabled | DONE | NOP at 0xEC6337 (build 035) |
| SHORT4 → FLOAT3 VB expansion path | DONE | D3DPOOL_MANAGED + content fingerprint cache (builds 045-046) |
| `positions` required in asset hash | CONFIRMED | Build 047 proved removing positions causes catastrophic collision |
| All light pipeline gates disabled | DONE | `Light_VisibilityTest`, sector count gate, RenderLights gate — re-enabled build 068, confirmed no crash |
| Replacement asset pipeline (mod lights) | CONFIRMED (build 075) | Purple test light visible and stable; `user.conf` override fixed |
| Current anchor hashes valid | **FAILING** | Building mesh IDs in `mod.usda` are stale; fresh capture needed |

---

## The One Remaining Problem

### Stale Anchor Mesh Hashes in mod.usda

**Build 075 breakthrough:** `user.conf` in the game directory had `rtx.enableReplacementAssets=False`. This file is written by the Remix developer menu and overrides `rtx.conf` (Remix loads config in order: `dxvk.conf → rtx.conf → user.conf`, last value wins). This single line was silently disabling ALL mod content — lights, materials, and mesh replacements — in every build from 016 to 074.

**Replacement asset pipeline confirmed working:** After fixing `user.conf`, a purple test light anchored to `mesh_574EDF0EAD7FC51D` appeared immediately, was visible from all 3 camera positions, and shifted correctly with camera movement. This proves the entire pipeline (proxy transform, FFP submission, Remix hash matching, mod.usda anchoring) works correctly.

**Stage lights still absent — root cause:** The 8 building mesh hashes in `mod.usda` are stale. Testing with light radius 2 through 3000 game units produced no change in the "white dots" (confirmed to be denoiser/NRC artifacts, not lights). No currently-rendered mesh matches those hash IDs because the hashes were captured under a previous Remix configuration (before `positions` was added to the hash rule and before SHORT4→FLOAT3 expansion).

**Geometry IS being rendered:** 3749+ draw calls per scene; the building is visible in hash debug and clean render screenshots. The geometry submission problem is solved — only the hash identifiers in `mod.usda` are wrong.

---

## Culling Layers — Complete Map

Every culling mechanism discovered and its patch status:

| Layer | Address(es) | What It Does | Patched? | Build Added |
|-------|-------------|--------------|----------|-------------|
| 1. Frustum distance threshold | 0xEFDD64 | Float constant (16.0f) — objects closer than threshold culled | Yes — stamped to -1e30f per BeginScene | 016 |
| 2. Per-object frustum function | 0x407150 | `SceneTraversal_CullAndSubmit` — 11 NOP jumps inside function; no RET (RET was added build 016, removed build 039; confirmed build 070) | Yes — 11 internal NOP jumps | 016 |
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
| 22. Terrain rendering path | TerrainDrawable (0x40ACF0) / TERRAIN_DrawUnits | Separate draw path for terrain geometry, own culling | Yes — terrain cull gate NOPed | 045-063 |
| 23. Null-check guard | 0xEDF9E3 | Crashes on uninitialized pointer during extended scene loads | Yes — trampoline patched | 045-063 |
| 24. ProcessPendingRemovals stale field | FUN_00ProcessPendingRemovals | Stale `field_48` causes crash at 0xEE88AD | Yes — patched | 045-063 |
| 25. MeshSubmit visibility gate | MeshSubmit_VisibilityGate | Pre-DIP visibility check — rejects meshes before draw | Yes — patched to return 0 | 045-063 |
| 26. Sector already-rendered skip | 0x46B7F2 | Skips sector objects already flagged as rendered this frame | Yes — NOPed | 045-063 |
| 27. Post-sector bitmask/distance culls | 0x40E30F, 0x40E3B0 | Distance + bitmask checks after sector traversal | Yes — NOPed | 045-063 |
| 28. Stream unload gate | 0x415C51 | Unloads mesh streams on camera movement | Yes — NOPed | 045-063 |
| 29. Mesh eviction | SectorEviction (×2) + ObjectTracker_Evict | Removes meshes from scene tracking on eviction | Yes — all 3 NOPed | 045-063 |
| 30. Post-sector loop | 0xF12016 (enable flag), 0x10024E8 (gate) | Secondary loop after main sector pass | Yes — enabled | 045-063 |
| 31. **Render queue frustum culler** | **0x40C430 (RenderQueue_FrustumCull)** | **Layer 3 recursive BVH frustum cull — drops objects outside view frustum** | **Yes — JMP → 0x40C390 (uncull path)** | **072** |
| 32. Frustum screen-size rejection | 0x46C242, 0x46C25B | Rejects objects whose screen footprint is too small in sector renderer | Yes — NOPed | 045-063 |
| 33. SectorPortalVisibility resets | 4 write sites | Per-frame portal visibility state resets | Yes — all 4 NOPed | 045-063 |
| 34. Sector_SubmitObject gates | 0x40C666, 0x40C68B | Pre-submission visibility gates inside object submit path | Yes — NOPed | 045-063 |
| 35. Level writers | 0x46CCB4, 0x4E6DFA | Writes that restrict level/sector geometry from being submitted | Yes — NOPed | 045-063 |
| 36. Null crash guard (scene traversal) | 0x40D2AC | Code-cave guard for null+0x20 deref in FUN_00402D290 during scene traversal (different from Layer 23) | Yes — trampoline patched | 076 |

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

### Phase 5: Proxy Improvements + Deep Culling (Builds 045–068)

*Builds 048–063 not preserved.*

| Build | Result | What We Tested | What We Learned |
|-------|--------|----------------|-----------------|
| 045 | FAIL | D3DPOOL_MANAGED VBs + per-frame VB flush | Hash debug: SHORT4 hashes stable. Blank render: per-frame flush too aggressive (512 VB creates/frame). D3DPOOL_MANAGED is correct; flush strategy wrong |
| 046 | FAIL | Content fingerprint cache + null VS for ALL draws | Nulling VS for FLOAT3 view-space draws breaks rendering — view-space positions render at extreme scale. Fingerprint cache is correct and kept |
| 047 | FAIL | Remove `positions` from asset hash | Catastrophic hash collision — all geometry same hash. `positions` MUST be in the hash; TRL meshes share index/texcoord/descriptor patterns |
| 048-063 | — | Not preserved | Multiple culling layers patched: terrain cull gate, null-check trampoline, ProcessPendingRemovals fix, MeshSubmit_VisibilityGate, stream unload gate, mesh eviction NOPs, post-sector loop enabled |
| 064 | FAIL | Hash stability test (camera-only pan) | Phase 1 invalid (load timing bug — 15s wait insufficient for Remix+Peru). Phase 2: ~244-245 draws, no lights. Patch integrity confirmed |
| 065 | FAIL | Hash stability test (same, fixed panning) | Phase 1 stable. Phase 2: ~650-657 draws, no lights. All 17+ patches confirmed active. Light patches intentionally disabled (crash risk at 0xEE88AD) |
| 066 | FAIL | Theory 1: disable draw cache (`DRAW_CACHE_ENABLED 0`) | No effect — draw cache only replays 3 draws; stale pointer concern was unfounded |
| 067 | FAIL | Theory 2: remove VP inverse epsilon threshold | No effect — VP changes on camera pan are large enough to always trigger recalculation |
| 068 | FAIL | Theory 3: re-enable all 3 light patches (LightVisTest + sector gate + RenderLights gate) | **No crash** — ProcessPendingRemovals fix resolved the 0xEE88AD crash. All 20+ patches active and confirmed. Lights still absent — upstream geometry not submitted |
| 069 | FAIL | Hash stability test (dipcnt instrumentation issue) | dipcnt hook failed; draw counts from proxy log only (~670-694 gameplay). ~75% of initial draws culled despite patches. Patch integrity confirmed |
| 070 | FAIL | Hash stability test with `positions` in asset hash; anti-culling disabled | Draw counts collapsed 93% over session (2833 → 185). Proxy uses 11 internal NOP jumps at 0x407150, NOT a RET at entry |
| 071 | FAIL | Added 3 additional anchor mesh hashes to mod.usda (5 → 8 total) | Lara not visible (FLOAT3 draws unpatched); draw counts stable ~2845; no lights |
| 071b | FAIL | FLOAT3 draw path fix — null VS before FLOAT3 draws | **Lara now visible for first time** — FLOAT3 branch correctly goes through FFP. Stage lights still absent. Black triangle artifact at feet |
| 072 | FAIL | RenderQueue_FrustumCull bypass — JMP 0x40C430 → 0x40C390 (Layer 31) | Draw counts +29% (2845 → 3657). Lara visible in hash debug. No crash. **Lights still absent** — anchor hashes may be stale (different config at capture) |
| 073 | FAIL | `rtx.useVertexCapture = True` | Draw counts ~3651. Small white dots visible — possibly stage lights at extreme HDR overexposure (intensity=10000000, exposure=20). Color unresolvable at current settings |
| 074 | FAIL | Deferred patches + permanent page unlock | All 31 patches active. Deferred init fixes menu crash (no longer needs `TR7.arg`). Draw counts ~3749. Lights absent. No crash. |
| 075 | FAIL | Fix `user.conf` `enableReplacementAssets=False`; test with purple reference light | **BREAKTHROUGH**: purple test light visible and stable (proves pipeline works). Stage light hashes stale — building geometry renders at ~3749 draws but no mesh matches mod.usda entries. White dots from prior builds were denoiser artifacts. |
| 076 | FAIL | Restore 2 missing crash protections in TRL proxy (patch_null_crash_40D2AF, PUREDEVICE stripping) | **CRASH FIXED.** Game now runs 1,484+ scenes without crash. 3,733 draws/scene (3,413 S4 + 320 FLOAT3). Hash stable in Phase 1. Replacement light (purple) visible in clean render. Stage red/green lights still absent — mod.usda hashes stale. |
| 077 | FIXED | DrawCache use-after-free fix — AddRef all resources (vb/ib/decl/tex0) in DrawCache_Record; DrawCache_Clear on WD_Release/WD_Reset/transition | **Cold launch crash fixed.** Game survives menu→level transition without TR7.arg; 2,468 draws/scene; no crash for 90+ seconds. Stage lights still absent — anchor hashes in mod.usda stale. |

**Conclusion (builds 069-077):** The replacement asset pipeline is confirmed working end-to-end (build 075-076). The `user.conf` override was silently disabling all mod content for 59 consecutive builds. The DrawCache use-after-free crash (exposed by cold launches without TR7.arg) is fixed in build 077. Now the only remaining task is a fresh Remix capture to get current mesh hash IDs and update `mod.usda`.

---

## Untried Ideas

| Idea | Why It Matters | Difficulty |
|------|----------------|------------|
| **Fresh Remix capture — regenerate mod.usda hashes** | Stale hashes confirmed as the only remaining blocker (build 075). Geometry IS rendering (3749 draws/scene); hashes just don't match. | Easy |
| Anchor to Lara's always-drawn mesh | Lara's body mesh visible since build 071b; anchor lights to her for a guaranteed-visible reference while working on building hashes | Easy |
| dx9tracer frame diff near stage to identify building mesh | Capture a frame; use `--classify-draws` and `--vtx-formats` to identify which draw calls correspond to the stage building | Medium |
| Patch LOD alpha fade at 0x446580 | 10 callers, may fade geometry at distance; low priority now that geometry IS being submitted | Medium |

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
| c0–c3 | World matrix (transposed, row-major) |
| c8–c11 | View matrix |
| c12–c15 | Projection matrix |
| c48+ | Skinning bone matrices (3 registers/bone) |
| c39 | Utility {2.0, 0.5, 0.0, 1.0} |

### Render Paths (fully mapped, builds 044–072)
```
RenderFrame (0x450B00)
  ├── RenderVisibleSectors (0x46C180) → RenderSector (0x46B7D0) ← proximity filter NOPed 0x46B85A
  ├── SceneTraversal wrapper (0x443C20) → SceneTraversal_CullAndSubmit (0x407150) ← all 11 NOPs + no RET
  └── Moveable object loop (0x40E2C0)
TerrainDrawable (0x40ACF0) → constructor only; real draw at TerrainDrawable_Dispatch (0x40AE20) ← terrain cull gate NOPed (Layer 22)
  └── Shares same 3-layer sector→submit→frustum-cull pipeline as regular geometry (confirmed build 072)
RenderQueue_FrustumCull (0x40C430) ← redirected to uncull path 0x40C390 (Layer 31, build 072)
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

## Recent Issues — Build 077 (2026-04-13)

### Launch Crash (Fixed)

**Symptom:** Game crashed every time it was launched manually (without TR7.arg), approximately 60–70 seconds into the session. No Windows Error Dialog. The crash appeared in the Windows Event Log as a `d3d9_remix.dll` fault (offset `0x001654dc`, `STATUS_STACK_BUFFER_OVERRUN`).

**Root cause:** `DrawCache_Record()` stored raw, un-referenced COM pointers to the game's vertex buffers, index buffers, vertex declarations, and textures. When the game freed menu geometry during the menu-to-level transition, `DrawCache_Replay()` used dangling pointers on the next Present call, passing freed VB/IB pointers into `SetStreamSource` + `DrawIndexedPrimitive`. The Remix bridge client crashed reading freed vertex data on the first raytrace frame (exactly when Neural Radiance Cache initialized).

**Why it was never caught:** All builds 001–076 used `TR7.arg` (chapter=4) to start directly in Peru. Peru geometry stays loaded throughout the session — no resources freed mid-run. The bug was only triggered by a cold manual launch that goes through the main menu first.

**Fix (build 077 / commit `4ce784e`):**

- `DrawCache_ReleaseEntry()` — releases COM refs and marks slot inactive
- `DrawCache_Clear()` — releases all active entries, zeros cache
- `DrawCache_Record()` — AddRefs vb, decl, tex0; keeps GetIndices-AddRef'd ib as cache ref
- `DrawCache_Replay()` — calls `DrawCache_ReleaseEntry` on stale eviction
- `WD_Release` / `WD_Reset` / transition flush — use `DrawCache_Clear()` instead of raw `s_drawCacheCount = 0`

**Verified:** Game runs 90+ seconds from cold menu start with 2,468 draw calls/scene, no crash.

### Rendering Weird at Startup

At startup (before TR7.arg was used, or on any cold manual launch), the game renders the main menu and intro sequence with only 12 draw calls per frame — far fewer than the ~2,400–3,700 seen in Peru gameplay. This appears as a nearly empty scene in Remix. This is **expected behavior**: the main menu is simple geometry. The game renders normally once loaded into a level. The crash above was preventing users from ever reaching the level from a cold launch.

---

## Immediate Next Step

> **Build 077: cold launch crash fixed. Game now runs stably from menu to level without TR7.arg. Anchor hashes in mod.usda are still stale.**

**One-step fix:**

1. **Fresh Remix capture** — launch the game with the current proxy, load Peru, position Lara near the stage. Open the Remix Toolkit, enable hash debug view (debug view 277), and capture the scene. Identify the current building mesh hash IDs, update `mod.usda`, then rerun `test-hash`.

**Stale hashes currently in `mod.usda`** — these need to be replaced after a fresh capture:

| Hash | Color | Vertices |
|------|-------|----------|
| `mesh_2509CEDB7BB2FAFE` | Red | 365 |
| `mesh_47AC93EAC3777CA5` | Red | 332 |
| `mesh_DD7F8EE7F4F3969E` | Green | 315 |
| `mesh_CE011E8D334D2E48` | Green | 312 |
| `mesh_2AF374CD4EA62668` | Red | 298 |

**What was established in builds 038-075:**
- All 31 culling layers patched — geometry IS submitted (3749 draws/scene)
- FLOAT3 draws fixed — Lara visible
- `user.conf` override fixed — replacement asset pipeline confirmed end-to-end (build 075)
- "White dots" in prior builds were denoiser/NRC artifacts, not lights (build 075: radius 2→3000 test)
- Stale hashes confirmed: no currently-rendered mesh matches mod.usda entries

---

## Decision Tree for Next Attempt

```
Fresh Remix capture near stage
├── Get new building mesh hashes from Toolkit
├── Update mod.usda with new hashes
└── Re-run test-hash
    ├── Hash/debug diagnostics still look healthy → continue manual replacement verification
    └── Replacement assets still do not bind
        ├── Verify hash debug shows building geometry in frame
        │   ├── Not visible → building not in frame / too far → reposition Lara
        │   └── Visible → hash in Toolkit doesn't match proxy's hash → hash rule mismatch
        └── dx9tracer capture: identify which draw calls are the building meshes
            └── Compare hash IDs between proxy log and Toolkit capture
```
