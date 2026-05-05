# Hot Path Audit — TRL FFP Proxy (Pre-Build-078 State)

This is the per-draw / per-frame CPU cost map of the proxy as of build 077, used to plan the build 078 perf changes. Future researchers should read this **before** profiling so they know what's already been investigated and what's already been gated/eliminated.

All line references in this doc point to the **build 077 baseline**, not the post-build-078 source — match against [proxy_changes.diff](proxy_changes.diff) when comparing.

---

## Tier A — Eliminated in Build 078

These were the highest-impact items found in the audit. Already addressed.

### A1. Per-draw `GetTickCount()` syscall via `DIAG_ACTIVE`
- **File:** `d3d9_device.c:296-298` (the `DIAG_ACTIVE(self)` macro)
- **Cost:** 1 syscall per `if (DIAG_ACTIVE(self))` check, even after the 3-frame log window closes
- **Frequency:** 11 per-draw + per-VS-write check sites (lines 2267, 3207, 3278, 3409, 3510, 3637, 3654, 3868, 3967, 4021, 4307)
- **Resolution:** `DIAG_ENABLED 1 → 0` strips all `#if DIAG_ENABLED` blocks at preprocessor time

### A2. Redundant `SetTransform` calls per draw
- **Files:** `d3d9_device.c:2293-2295` (`TRL_ApplyTransformOverrides`) + `d3d9_device.c:2779-2781` (`S4_ExpandAndDraw`)
- **Cost:** 3 vtable thunks + 3 D3D9 internal `SetTransform` calls per FFP draw
- **Frequency:** ~3,749 draws/scene × multiple scenes/frame
- **Why redundant:** View typically constant within a frame; Projection rarely changes; only World changes per object. The proxy was pushing all three on every draw.
- **Resolution:** `TRL_ApplyTransformsCached` helper does `memcmp` per slot, fires `SetTransform` only on the slots that actually changed. Cache held in `appliedWorld/View/Proj`.
- **Safety basis:** `WD_SetTransform` at `d3d9_device.c:3927-3942` already blocks all external V/P/W writes once `viewProjValid=1`. Proxy is the single writer; cache is authoritative. Reset clears the cache.

### A3. `PinnedDraw_ReplayMissing` once per second
- **File:** `d3d9_device.c:554` (`#define PINNED_REPLAY_INTERVAL 60`)
- **Cost:** Opens its own `BeginScene/EndScene`, walks 512-entry `pinnedDraws` array
- **Why dead with current setup:** All 36 engine culling layers disabled → game submits everything every frame → replay finds zero missing draws
- **Resolution:** `PINNED_REPLAY_INTERVAL 60 → 600` (10× less frequent). Kept enabled so it remains correct if any culling layer ever re-engages.

### A4. Residual diagnostic logging not gated by `#if DIAG_ENABLED`
- **Files:** `d3d9_device.c:3240-3250` (`frameSummaryCount` / FRAME log), `d3d9_device.c:3360-3368` (per-scene census S<n>), `d3d9_device.c:3386-3396` (PostLatch log)
- **Cost:** I/O to `ffp_proxy.log`, branch checks every Present / EndScene
- **Resolution:** All three wrapped in `#if DIAG_ENABLED`.

---

## Tier B — Already Cheap or Properly Gated (No Work Needed)

These were investigated and confirmed as non-issues. Document so future research doesn't re-investigate.

### B1. `PinnedDraw_Capture` linear scan during first 120 frames
- **Cost:** O(N) scan of `pinnedDrawCount` (max 512) per draw, only for first 120 frames (~2 sec)
- **After capture window:** `pinnedCaptureComplete=1` short-circuits the whole function at `d3d9_device.c:1934`
- **Verdict:** Bounded warmup cost, zero steady-state cost. ✓

### B2. `strippedDeclOrig[64]` linear scan
- **Cost:** O(64) scan per draw with unfamiliar declaration
- **Reality:** TRL uses ~4-6 unique declarations total. After first hit, lookup is O(1) for the lifetime of the cached entry.
- **Verdict:** Negligible in practice. ✓

### B3. `s4VBCache[512]` linear scan
- **File:** `d3d9_device.c:2591-2600`
- **Cost:** O(512) worst case per SHORT4 draw
- **Reality:** Character/object meshes repeat; cache typically hits within first 5-10 entries
- **Verdict:** Cache works as intended. Could be improved to O(1) with a hash map for large geometry counts (see [OPTIMIZATION_CANDIDATES.md](OPTIMIZATION_CANDIDATES.md) item #3). ✓

### B4. `WD_SetVertexShaderConstantF` overhead
- **File:** `d3d9_device.c:3946-3996`
- **Cost per call:** 1× memcpy (up to 64 bytes), ~10 dirty-flag branch checks
- **Logging:** Already `#if DIAG_ENABLED` gated at 3998-4005
- **Skinning bone upload:** `#if ENABLE_SKINNING 0` strips it entirely
- **Verdict:** Tight, no allocation, no scan. ✓

### B5. `WD_BeginScene` anti-cull stamping
- **File:** `d3d9_device.c:3393-3404`
- **Cost:** 8 volatile writes per BeginScene; `if (memoryPatchesApplied)` gated
- **Reality:** Source comment claims globals get rewritten per-frame, hence per-scene stamping is the safe default. Could potentially move to first-BeginScene-of-frame only (see candidate #1). Untouched in build 078 pending verification.
- **Verdict:** Cheap as-is, with a known optimization pending verification. ✓

### B6. `DrawCache_Replay` per-EndScene scan post-capture
- **File:** `d3d9_device.c:2931-2964`
- **Cost:** O(s_drawCacheCount) scan per EndScene
- **Reality:** With engine culling disabled, every entry hits `if (c->lastSeenFrame == self->frameCount) continue;` early. Just loop overhead.
- **Verdict:** Already bounded. Nice-to-have but not material. ✓

### B7. Vtable thunk overhead (all 119 intercepted methods)
- **Cost per call:** 1× `RealVtbl(self)` deref (inline) + chained method call
- **Verdict:** Minimum possible for a proxy DLL. ✓

### B8. SkyIso per-draw entry lookup
- **File:** `d3d9_device.c:1373-1412`, gated at 1525 by `skyIsolationEnable`
- **Fast path:** `skyIsoLastOrigTex` cache hits on consecutive same-texture draws (likely)
- **Slow path:** O(SKY_ISO_MAX) linear scan + 3× `mat4_approx_equal` calls
- **Disable runtime:** `proxy.ini [Sky] EnableIsolation=0` short-circuits at the gate
- **Verdict:** Cheap with manual sky tagging in `rtx.conf`. ✓

---

## Build Configuration (Already Optimal)

[proxy/build.bat](proxy/build.bat):
- `/O2` — speed-optimized
- `/Oi` — intrinsics
- `/fp:fast` — fast float math
- `/GL` — whole-program optimization
- `/GS-` — security checks off (correct for no-CRT DLL)
- `/Zl` — no default library
- `/D NDEBUG` — release define
- `/LTCG` — link-time code generation

No debug symbols (`/Zi`) ship in the deployed DLL.

---

## How to Profile Going Forward

1. **`PERF_LOG`** in `ffp_proxy.log` — built-in periodic FPS sample. Read directly.
2. **NVIDIA overlay** — for visible FPS during gameplay.
3. **Live function tracing** — `python -m livetools collect <addr>` to count hit rates of suspected hot functions in the proxy.
4. **dx9tracer frame capture** — `python -m graphics.directx.dx9.tracer trigger --game-dir ...` then `analyze --classify-draws --hotpaths` to identify where draw-call time goes inside Remix.
5. **Disassemble built proxy** — `dumpbin /disasm Tomb Raider Legend\d3d9.dll | findstr GetTickCount` etc. to verify what the compiler stripped.

---

## Reading the Source

The proxy is a single big file ([proxy/d3d9_device.c](proxy/d3d9_device.c), 5,339 lines). Logical sections:

| Lines | Section |
|---|---|
| 1-300 | Defines, enums, struct fields, DIAG/PERF macros |
| 300-700 | `WrappedDevice` struct definition |
| 700-1200 | Helpers (math, COM refcount, log, ini parsing) |
| 1200-1700 | Sky isolation system (bit-mutated texture clones for hash separation) |
| 1700-1900 | TRL gameplay-camera detection + scene classification |
| 1900-2150 | PinnedDraw cache (`anti-cull replay`) |
| 2173-2192 | **`TRL_ApplyTransformsCached` helper (build 078 — matrix cache)** |
| 2200-2300 | `TRL_ApplyTransformOverrides` — main matrix recovery from VS constants |
| 2400-2700 | SHORT4 → FLOAT3 vertex buffer expansion |
| 2700-2900 | DrawCache (per-frame draw replay) |
| 2900-3200 | RELAY_THUNK declarations + `WD_Reset` |
| 3200-3450 | `WD_Present`, `WD_BeginScene`, `WD_EndScene` |
| 3450-3900 | Per-draw routing: `WD_DrawIndexedPrimitive`, `WD_DrawPrimitive`, `WD_SetRenderState`, `WD_SetTransform` |
| 3945-4060 | `WD_SetVertexShaderConstantF` (matrix capture for recovery) |
| 4100-4400 | Other intercepted methods (declarations, shaders, textures, etc.) |
| 4400-5000 | Memory patches (`TRL_ApplyMemoryPatches` — applies all 36 culling-layer NOPs) |
| 5000-5339 | `WrappedDevice_Create`, vtable construction, init |

---

## Key Constants for Future Tuning

| Constant | Current | Effect |
|---|---|---|
| `DIAG_ENABLED` | 0 | Gate for all diagnostic logging — set to 1 to recapture per-draw logs |
| `PERF_LOG_ENABLED` | 1 | Gate for `PERF` lines in `ffp_proxy.log` — set to 0 for final ship build |
| `PERF_LOG_INTERVAL` | 600 | Frames per FPS sample (~10s @ 60fps, ~5s @ 120fps) |
| `PINNED_REPLAY_INTERVAL` | 600 | Frames between replay-cache scans |
| `PINNED_CAPTURE_FRAMES` | 120 | Capture window for PinnedDraw |
| `PINNED_DRAW_MAX` | 512 | Cache size for pinned draws |
| `S4_VB_CACHE_SIZE` | 512 | Cache size for SHORT4 expanded VBs |
| `DRAW_CACHE_ENABLED` | 1 | Enables per-frame draw replay (anti-cull) |
| `ENABLE_SKINNING` | 0 | Off — never flip without explicit ask |
