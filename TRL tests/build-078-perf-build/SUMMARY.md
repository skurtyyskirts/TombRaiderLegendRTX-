# Build 078 — Performance Build (Proxy CPU Hot-Path)

**Date:** 2026-05-05
**Result:** BUILT, DEPLOYED, AWAITING IN-GAME MEASUREMENT
**Scope:** D3D9 FFP proxy DLL (`d3d9.dll`) only — `rtx.conf` untouched (user tunes Remix-side via in-game menu)
**Hardware target:** RTX 5090 (DLSS / DLFG / Reflex / DLSS-RR all available — user toggles via Remix menu X)
**Predecessor:** build 077 (DrawCache use-after-free fix; cold launch stable)

---

## Result

**Built and deployed.** No FPS measurement yet — the perf build adds `PERF_LOG` instrumentation that emits `PERF frames=600 ms=N fps=N` to `ffp_proxy.log` every ~10 seconds of gameplay. Run the hash-stability test or play live to capture a number.

The build is a **pure CPU-side optimization of the proxy hot path**. Every byte of geometry submitted to Remix is byte-identical to build 077 — no hash impact, no anchor mesh impact, no culling-layer impact. The two prior wins (stable hashes, all 36 culling layers disabled) are preserved.

DLL size: **48,640 bytes** vs build 077's **56,320 bytes** (–13.6%). The shrink is mostly dead-code removal from `DIAG_ENABLED 0`.

---

## What Changed

### 1. Diagnostic compilation disabled — `DIAG_ENABLED 1 → 0`
File: [proxy/d3d9_device.c:294](proxy/d3d9_device.c)

The `DIAG_ACTIVE(self)` macro at line 296 calls `GetTickCount()` on **every per-draw branch check** even after the 3-frame log window closed at 50s. With `DIAG_ENABLED=1`, that's a syscall per draw call (~3,749 draws/scene × multiple scenes/frame × 60+ FPS) for the entire session. The preprocessor now strips all 11 `#if DIAG_ENABLED` blocks. Eliminates:
- Per-draw `GetTickCount()` syscall
- Per-draw VB lock + hex dump (line 3691)
- 256-register zero-loop on every Present (line 3281)
- Per-VS-write matrix label sprintf (lines 4022-4061)
- Per-draw 8-stage texture stage iteration (lines 3638-3653)
- Per-VS-constant-write `vsConstWriteLog[startReg + i] = 1` (line 3971)

### 2. Pinned-draw replay interval — `60 → 600` frames
File: [proxy/d3d9_device.c:554](proxy/d3d9_device.c)

`PinnedDraw_ReplayMissing` opens its own `BeginScene/EndScene` and walks 512 entries every invocation. With all 36 engine culling layers disabled, every draw is submitted every frame, so `replayMissing` finds zero work each call. Now runs once per ~10s instead of once per second.

### 3. Performance logging — `PERF_LOG_ENABLED 1`
Files: [proxy/d3d9_device.c:301-302](proxy/d3d9_device.c), [proxy/d3d9_device.c:3315-3328](proxy/d3d9_device.c)

Every 600 frames in `WD_Present`, emits `PERF frames=600 ms=N fps=N` to `ffp_proxy.log`. Cost: 1 `GetTickCount` per 600 frames. Toggle off via `PERF_LOG_ENABLED 0` for the final ship build once tuning is complete.

Independent of `DIAG_ENABLED` so it survives the diagnostic strip.

### 4. Residual diagnostic logging gated
File: [proxy/d3d9_device.c:3260-3271](proxy/d3d9_device.c), [proxy/d3d9_device.c:3402-3413](proxy/d3d9_device.c), [proxy/d3d9_device.c:3429-3441](proxy/d3d9_device.c)

Three log-emitting blocks were running outside `#if DIAG_ENABLED`:
- Frame summary (every 60 frames, first 10 firings)
- Per-scene census (every other scene during scenes 500–1500)
- PostLatch logging (after level latch, ~30 firings)

All three now `#if DIAG_ENABLED` gated. With DIAG=0, `ffp_proxy.log` only carries init lines + the PERF lines.

### 5. Matrix cache for `SetTransform` — biggest win
Files:
- New struct fields [proxy/d3d9_device.c:738-746](proxy/d3d9_device.c): `appliedWorld[16]`, `appliedView[16]`, `appliedProj[16]`
- New helper [proxy/d3d9_device.c:2173-2192](proxy/d3d9_device.c): `TRL_ApplyTransformsCached`
- Replaces SetTransform-3-call site at [proxy/d3d9_device.c:2293](proxy/d3d9_device.c) (`TRL_ApplyTransformOverrides`)
- Replaces SetTransform-3-call site at [proxy/d3d9_device.c:2811](proxy/d3d9_device.c) (`S4_ExpandAndDraw`)
- Cache invalidation in `WD_Reset` at [proxy/d3d9_device.c:3215-3219](proxy/d3d9_device.c)

**Problem identified:** the proxy was firing `SetTransform(WORLD)` + `SetTransform(VIEW)` + `SetTransform(PROJECTION)` **unconditionally on every FFP-routed draw** at two call sites. View and Projection are typically constant within a frame; only World changes per object. With ~3,749 draws/scene, that's tens of thousands of redundant vtable thunks + D3D9 internal `SetTransform` calls per second.

**Solution:** track the last-pushed matrix per slot. Compare new matrix to cached via `memcmp(64 bytes)`. Only push when changed.

**Safety:** the proxy's own `WD_SetTransform` already blocks all external V/P/W writes once `viewProjValid=1` ([proxy/d3d9_device.c:3936-3940](proxy/d3d9_device.c)) — so the proxy is the only path to the device's transform state, and the cache is authoritative. On `WD_Reset`, cache is zeroed (matches device's reset state).

**Expected impact:** ~2/3 of `SetTransform` calls eliminated for steady-state gameplay. Order of magnitude: ~150K vtable thunks + D3D9 internal calls saved per second.

---

## Verification

`ffp_proxy.log` should now contain:
- Init lines (rtx.conf parsing, `=== FFP proxy init ===`, etc.)
- `TRL: applying gameplay memory patches at scene=...` (once)
- `PinnedDraw: capture complete, N unique draws cached` (once at frame 120)
- **`PERF frames=600 ms=N fps=N`** lines starting ~10s into gameplay
- Sky isolation observations (bounded to warmup window)

Should NOT contain:
- `==== PRESENT frame ...` blocks (DIAG=0)
- `== FRAME ... total= processed= ...` summaries (DIAG=0)
- `S<n> d= s4= f3= p= q=` per-scene logs (DIAG=0)
- `PostLatch scene= d= ...` (DIAG=0)
- Per-draw DIP/DP debug dumps (DIAG=0)

---

## What's Next for Future Optimization Work

See [OPTIMIZATION_CANDIDATES.md](OPTIMIZATION_CANDIDATES.md) for the full list with file:line citations and impact estimates. Top candidates not yet implemented:

1. **BeginScene anti-cull stamping → per-frame instead of per-scene** (TRL has 5–15 BeginScenes/frame, stamping happens 14× more often than necessary if globals only get rewritten per-frame). Risk: source comment claims per-frame is the correct cadence; needs runtime verification before changing.
2. **Sky isolation per-draw cost** when `EnableIsolation=1` — already gated by `skyIsolationEnable` flag (free), but if user has tagged sky textures manually in `rtx.conf`, the auto-cloning warmup is wasted. Set `proxy.ini [Sky] EnableIsolation=0` at runtime.
3. **`s4VBCache` linear scan (512 entries)** — could be replaced with a hash map indexed by `(srcVB, srcOff, baseVtx)` for O(1) lookups on SHORT4 draw cache hits. Modest gain.
4. **Struct field gating with `#if DIAG_ENABLED`** — saves ~2 KB of `WrappedDevice` cache footprint. Low priority, low gain.
5. **PGO (profile-guided optimization)** — `/GENPROFILE` build → game session → `/USEPROFILE` build. Typically 5–10% perf gain on hot paths. Not yet attempted.

See [HOTPATH_AUDIT.md](HOTPATH_AUDIT.md) for the full per-draw / per-frame cost map captured before the build (input to this build's prioritization).

---

## Build & Deploy

```
cd patches/TombRaiderLegend/proxy
build.bat
# Auto-deploy via run.py:
python patches/TombRaiderLegend/run.py test-hash --build
```

Compiler flags (already optimal): `/O2 /Oi /fp:fast /GL /GS- /Zl + NDEBUG + /LTCG`

---

## Constraints (Hard — Do Not Change)

- 36 culling-layer NOPs in `TRL_ApplyMemoryPatches` — all confirmed (frustum/sector/light/render-queue gates all disabled)
- `DRAW_CACHE_ENABLED 1` (anti-cull replay; needed if any cull layer ever re-engages)
- `ENABLE_SKINNING 0` (per `dx9-ffp-port.md` — never flip without ask)
- VS register layout (c0-c3 W, c8-c11 V, c12-c15 P)
- SHORT4→FLOAT3 expansion path + content fingerprint cache
- `useVertexCapture=True`, hash rules, `sceneScale=0.0001`, `zUp=True`, `fusedWorldViewMode=0`
- The 5 anchored stage-light mesh hashes
- `rtx.captureInstances=True`, `enableReplacementAssets=True`
- Both `patches/TombRaiderLegend/proxy/d3d9_device.c` and root `proxy/d3d9_device.c` must remain byte-identical (memory: `feedback_proxy_sync` cites an 8-day crash from missing this)

---

## Files in This Folder

| File | Purpose |
|---|---|
| [SUMMARY.md](SUMMARY.md) | This file |
| [HOTPATH_AUDIT.md](HOTPATH_AUDIT.md) | Pre-build audit of every per-draw / per-frame cost in the proxy |
| [OPTIMIZATION_CANDIDATES.md](OPTIMIZATION_CANDIDATES.md) | Optimizations not yet implemented, with file:line citations |
| [proxy_changes.diff](proxy_changes.diff) | Unified diff of all source changes vs build 077 |
| [proxy/d3d9_device.c](proxy/d3d9_device.c) | Full source snapshot (5,339 lines) |
| [proxy/d3d9_main.c](proxy/d3d9_main.c) | DLL entry, chain loading |
| [proxy/d3d9_wrapper.c](proxy/d3d9_wrapper.c) | IDirect3D9 wrapper |
| [proxy/d3d9_skinning.h](proxy/d3d9_skinning.h) | Skinning extension (compiled out, `ENABLE_SKINNING 0`) |
| [proxy/build.bat](proxy/build.bat) | MSVC x86 build — already optimal flags |
| [proxy/proxy.ini](proxy/proxy.ini) | Runtime config (Remix chain, FFP routing, sky iso) |
| [proxy/d3d9.dll](proxy/d3d9.dll) | Built binary, 48,640 bytes |
| [proxy/d3d9.def](proxy/d3d9.def) | Module definition |
