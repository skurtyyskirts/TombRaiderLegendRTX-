# Builds 078–079 — Performance and Skinned-Character Drift

> Two builds. Build 078: pure proxy optimization, 13.6% DLL size reduction with byte-identical geometry submission. Build 079: attempted fix for the open skinned-character hash drift; fix doesn't engage due to route-mismatch.

## Build 078 — Perf build

After build 077 stabilized cold launches, build 078 was a pure CPU-side optimization pass on the proxy. Goal: measure the actual proxy cost, then remove the obvious waste.

### What changed

- **`DIAG_ENABLED 1 → 0`.** Strips 11 per-draw `GetTickCount` checks, the VB lock + first-32-bytes dump, the 256-register zero-loop on Present, the `sprintf` label generation, and the 8-stage TSS iteration. All were live in build 077.
- **`PINNED_REPLAY_INTERVAL 60 → 600`.** The proxy's pinned-draw replay subsystem fires `PinnedDraw_ReplayMissing` every N frames; bumping the interval from 60 to 600 cuts that overhead by 10×.
- **Per-draw `SetTransform` now cached via `TRL_ApplyTransformsCached`.** `memcmp` per W/V/P slot; only changed slots fire. Previously every draw issued three `SetTransform` calls unconditionally.
- **`PERF_LOG_ENABLED 1`.** Emits `PERF frames=600 ms=N fps=N` every ~10 seconds — the proxy's first FPS-reporting feature, ground truth for tuning.

### Result

- **DLL: 56,320 → 48,640 bytes (-13.6%)**
- **Byte-identical geometry submission preserved** — diffed the JSONL traces from the dx9tracer before and after; not a single draw call differs.

In-game FPS measurement was still pending as of build 078; the `PERF_LOG_ENABLED` line was added to ground that.

### Supporting documents

Build 078 ships with two extra technical analysis files that the wiki preserves:

- **HOTPATH_AUDIT.md** — Per-draw / per-frame CPU cost map of the proxy *before* build 078, with line references in `d3d9_device.c` and frequency analysis. The base data that drove the optimization plan.
- **OPTIMIZATION_CANDIDATES.md** — Ordered list of further perf wins not yet implemented. Top of the list: BeginScene stamping → once-per-frame at top of `BeginScene` instead of per-DrawCall. This is the proxy's perf roadmap.

These survive as [[Proxy-Performance-Audit]] in the wiki.

## Build 079 — Skinned decl normalization (FAIL: shader route mismatch)

Open issue from earlier builds: **Lara and distant NPCs show drifting hash-debug colors between frames** while world geometry stays constant. World hashes were proven stable in build 073 with `useVertexCapture=True`, but skinned meshes flicker.

### Hypothesis

The skinned vertex declaration includes `BLENDWEIGHT` and `BLENDINDICES` elements that drive software skinning per frame. Remix's geometry-descriptor hash includes the vertex layout — so the declaration changing (or being interpreted differently) frame-to-frame would produce different hashes for the same logical mesh.

### What changed

- Added **`BuildSkinnedNormalizedDecl()`** — clones every skinned vertex declaration with `BLENDWEIGHT` / `BLENDINDICES` stripped.
- Swaps the normalized decl in/out around the FFP-facing `DrawIndexedPrimitive` call (in the null-VS path).
- First 8 unique skinned decls dumped to log for diagnostics.
- INI toggle `[FFP] NormalizeSkinnedDecl=1`.

DLL: 50,176 bytes.

### Why it failed

**The fix doesn't engage.** Lara takes the **shader route**, not the null-VS route.

`ffp_proxy.log` confirms:
```
Float3Route effective: shader
```

This happens because `rtx.useVertexCapture = True` (set in build 073) forces auto-mode to the shader path. The null-VS path is reserved for SHORT4 static geometry. Lara is FLOAT3 and goes through `useVertexCapture`, where the proxy doesn't intercept the decl.

### Compounding problem: workspace deployment

The build was also deployed under the wrong path initially — the test harness wrote to a sibling `Tomb Raider Legend/` stub directory instead of the real `AlmightyBackups/.../Tomb Raider Legend/` game directory. The workspace deployment rule (always deploy via `patches/TombRaiderLegend/deploy_build.py`, which knows the canonical game directory) is now documented in [[Setup-Guide]].

### Open questions for the next attempt

1. **Is the user looking at the *asset* hash debug view or the *generation* hash view?** Generation hash is *expected* to flicker on skinned meshes by design (see `TECHNICAL_ANALYSIS.md` in the contender folder). The flicker may be a non-issue.
2. **Is Lara FLOAT3 or SHORT4 skinned?** The build 079 log will tell us once retested with the deployed DLL (always-on `SKINNED decl=` log entries fire even on the shader route).
3. **If true asset-hash drift, branch the fix:**
   - SHORT4 skinned → extend `S4_ExpandAndDraw` with the same decl swap (already null-VS, so swap is safe).
   - FLOAT3 skinned → new INI toggle `[FFP] SkinnedFloat3Route=null_vs` to override `useVertexCapture` for skinned-only. Tradeoff: Lara through Remix renders bind-pose.

### Lessons preserved in `static_analysis_log.md`

Build 079 includes a `static_analysis_log.md` that demonstrates the **dual-baseline split**:

- **Static patches** baked into a curated `trl_dump_SCY.exe` — the reference binary used by `retools` for static analysis. The 32 patched culling layers are committed to this binary.
- **Runtime patches** applied at first `BeginScene` to the actual running `trl.exe`. Both must stay in sync.

This is the template for the static-analyzer subagent's verification output. Every patch-touching build should now include a comparable verification table.

This file is preserved as part of [[Reverse-Engineering-Toolkit]] documentation.

## What this epoch established

- **The proxy has measurable headroom.** Build 078 proved a 13.6% DLL size reduction is achievable without changing behavior. The `OPTIMIZATION_CANDIDATES.md` roadmap is the next ~30% reduction available.
- **`useVertexCapture` changes the route map.** With `useVertexCapture=True`, FLOAT3 character meshes go through the shader route, not the null-VS route. Any fix targeted at the null-VS path will silently skip skinned characters.
- **Workspace deployment is a real production concern.** The same patch can be present in the source tree but absent from the game directory. Always deploy via the canonical script.
- **The dual-baseline split.** Static-analyzer subagents work against a stable reference binary; the live game has a moving target. Cross-checks must be explicit.
- **Generation hash flicker on skinned meshes is by design.** Frequently confused with a real bug — needs a clear callout in the diagnostic workflow.

## Open issues going into 080+

- Skinned-character hash drift — needs branched fix (SHORT4 vs FLOAT3 paths)
- mod.usda anchor refresh — still THE blocker
- Real-time FPS measurement — `PERF_LOG_ENABLED` provides data; build 080+ should measure and tune
- BeginScene stamping → once-per-frame at top of BeginScene (top of `OPTIMIZATION_CANDIDATES.md`)

## See also

- [[Build-History-Index]]
- [[Current-Status]]
- [[Proxy-Performance-Audit]] — HOTPATH_AUDIT.md and OPTIMIZATION_CANDIDATES.md
- [[Hash-Stability]] — section on skinned-mesh hashing
- [[Reverse-Engineering-Toolkit]] — dual-baseline pattern
- [[Stable-Hashes-Technical-Analysis]] — why generation hash flicker on skinned meshes is design
