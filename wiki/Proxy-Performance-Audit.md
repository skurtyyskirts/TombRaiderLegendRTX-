# Proxy Performance Audit

> Per-draw / per-frame CPU cost map of the FFP proxy, the optimizations applied in build 078, and the roadmap of further candidates. The two source documents from build 078 (`HOTPATH_AUDIT.md` and `OPTIMIZATION_CANDIDATES.md`) are folded into one wiki page here.

This page is essential reading before any further proxy optimization work. Many obvious candidates have already been investigated and either resolved or proven irrelevant.

---

## Part 1: Hot Path Audit (build 077 baseline)

The per-draw / per-frame CPU cost map of the proxy as of build 077, used to plan the build 078 perf changes. **Future researchers should read this before profiling** so they know what's already been investigated and what's already been gated or eliminated.

### Tier A — Eliminated in build 078

These were the highest-impact items in the audit.

#### A1. Per-draw `GetTickCount()` syscall via `DIAG_ACTIVE`
- **File:** `d3d9_device.c:296-298` (the `DIAG_ACTIVE(self)` macro)
- **Cost:** 1 syscall per `if (DIAG_ACTIVE(self))` check, even after the 3-frame log window closes
- **Frequency:** 11 per-draw + per-VS-write check sites (lines 2267, 3207, 3278, 3409, 3510, 3637, 3654, 3868, 3967, 4021, 4307)
- **Resolution:** `DIAG_ENABLED 1 → 0` strips all `#if DIAG_ENABLED` blocks at preprocessor time

#### A2. Redundant `SetTransform` calls per draw
- **Files:** `d3d9_device.c:2293-2295` (`TRL_ApplyTransformOverrides`) + `d3d9_device.c:2779-2781` (`S4_ExpandAndDraw`)
- **Cost:** 3 vtable thunks + 3 D3D9 internal `SetTransform` calls per FFP draw
- **Frequency:** ~3,749 draws/scene × multiple scenes/frame
- **Why redundant:** View typically constant within a frame; Projection rarely changes; only World changes per object. The proxy was pushing all three on every draw.
- **Resolution:** `TRL_ApplyTransformsCached` helper does `memcmp` per slot, fires `SetTransform` only on slots that actually changed. Cache held in `appliedWorld/View/Proj`.
- **Safety basis:** `WD_SetTransform` at `d3d9_device.c:3927-3942` already blocks all external V/P/W writes once `viewProjValid=1`. Proxy is the single writer; cache is authoritative. Reset clears the cache.

#### A3. `PinnedDraw_ReplayMissing` once per second
- **File:** `d3d9_device.c:554` (`#define PINNED_REPLAY_INTERVAL 60`)
- **Cost:** Opens its own `BeginScene/EndScene`, walks 512-entry `pinnedDraws` array
- **Why dead with current setup:** All 36 engine culling layers disabled → game submits everything every frame → replay finds zero missing draws
- **Resolution:** `PINNED_REPLAY_INTERVAL 60 → 600` (10× less frequent). Kept enabled so it remains correct if any culling layer ever re-engages.

#### A4. Residual diagnostic logging not gated by `#if DIAG_ENABLED`
- **Files:** `d3d9_device.c:3240-3250` (`frameSummaryCount` / FRAME log), `d3d9_device.c:3360-3368` (per-scene census S<n>), `d3d9_device.c:3386-3396` (PostLatch log)
- **Cost:** I/O to `ffp_proxy.log`, branch checks every Present / EndScene
- **Resolution:** All three wrapped in `#if DIAG_ENABLED`.

### Tier B — Already cheap or properly gated

These were investigated and confirmed as non-issues. Document so future research doesn't re-investigate.

#### B1. `PinnedDraw_Capture` linear scan during first 120 frames
- O(N) scan of `pinnedDrawCount` (max 512) per draw, only for first 120 frames (~2 sec)
- After capture window: `pinnedCaptureComplete=1` short-circuits at `d3d9_device.c:1934`
- **Verdict:** Bounded warmup cost, zero steady-state cost ✓

#### B2. `strippedDeclOrig[64]` linear scan
- O(64) scan per draw with unfamiliar declaration
- TRL uses ~4-6 unique declarations total — after first hit, lookup is O(1)
- **Verdict:** Negligible in practice ✓

#### B3. `s4VBCache[512]` linear scan
- **File:** `d3d9_device.c:2591-2600`
- O(512) worst case per SHORT4 draw
- Character/object meshes repeat; cache typically hits within first 5-10 entries
- **Verdict:** Cache works as intended. Hash-map upgrade is Candidate #3 below ✓

#### B4. `WD_SetVertexShaderConstantF` overhead
- **File:** `d3d9_device.c:3946-3996`
- 1× memcpy (up to 64 bytes), ~10 dirty-flag branch checks per call
- Logging already `#if DIAG_ENABLED` gated. Skinning bone upload `#if ENABLE_SKINNING 0` strips entirely
- **Verdict:** Tight, no allocation, no scan ✓

#### B5. `WD_BeginScene` anti-cull stamping
- **File:** `d3d9_device.c:3393-3404`
- 8 volatile writes per BeginScene, `if (memoryPatchesApplied)` gated
- Could potentially move to first-BeginScene-of-frame only (see Candidate #1)
- **Verdict:** Cheap as-is, with a known optimization pending verification ✓

#### B6. `DrawCache_Replay` per-EndScene scan post-capture
- **File:** `d3d9_device.c:2931-2964`
- With engine culling disabled, every entry hits `if (c->lastSeenFrame == self->frameCount) continue;` early — just loop overhead
- **Verdict:** Already bounded ✓

#### B7. Vtable thunk overhead (all 119 intercepted methods)
- 1× `RealVtbl(self)` deref (inline) + chained method call per call
- **Verdict:** Minimum possible for a proxy DLL ✓

#### B8. SkyIso per-draw entry lookup
- **File:** `d3d9_device.c:1373-1412`, gated at 1525 by `skyIsolationEnable`
- Fast path: `skyIsoLastOrigTex` cache hits on consecutive same-texture draws
- Slow path: O(SKY_ISO_MAX) linear scan + 3× `mat4_approx_equal` calls
- Runtime-disable: `proxy.ini [Sky] EnableIsolation=0`
- **Verdict:** Cheap with manual sky tagging in `rtx.conf` ✓

### Build configuration (already optimal)

`proxy/build.bat`:
- `/O2` — speed-optimized
- `/Oi` — intrinsics
- `/fp:fast` — fast float math
- `/GL` — whole-program optimization
- `/GS-` — security checks off (correct for no-CRT DLL)
- `/Zl` — no default library
- `/D NDEBUG` — release define
- `/LTCG` — link-time code generation

No debug symbols (`/Zi`) ship in the deployed DLL.

### How to profile going forward

1. **`PERF_LOG` in `ffp_proxy.log`** — built-in periodic FPS sample
2. **NVIDIA overlay** — visible FPS during gameplay
3. **Live function tracing** — `python -m livetools collect <addr>` to count hit rates of suspected hot functions
4. **dx9tracer frame capture** — `python -m graphics.directx.dx9.tracer trigger` then `analyze --classify-draws --hotpaths`
5. **Disassemble built proxy** — `dumpbin /disasm Tomb Raider Legend/d3d9.dll | findstr GetTickCount` to verify what the compiler stripped

### Reading the source

The proxy is a single big file (`proxy/d3d9_device.c`, 5,339 lines). Logical sections:

| Lines | Section |
|---|---|
| 1-300 | Defines, enums, struct fields, DIAG/PERF macros |
| 300-700 | `WrappedDevice` struct definition |
| 700-1200 | Helpers (math, COM refcount, log, ini parsing) |
| 1200-1700 | Sky isolation system |
| 1700-1900 | TRL gameplay-camera detection + scene classification |
| 1900-2150 | PinnedDraw cache (anti-cull replay) |
| 2173-2192 | **`TRL_ApplyTransformsCached` helper (build 078)** |
| 2200-2300 | `TRL_ApplyTransformOverrides` — main matrix recovery from VS constants |
| 2400-2700 | SHORT4 → FLOAT3 vertex buffer expansion |
| 2700-2900 | DrawCache (per-frame draw replay) |
| 2900-3200 | RELAY_THUNK declarations + `WD_Reset` |
| 3200-3450 | `WD_Present`, `WD_BeginScene`, `WD_EndScene` |
| 3450-3900 | Per-draw routing: `WD_DrawIndexedPrimitive`, `WD_DrawPrimitive`, `WD_SetRenderState`, `WD_SetTransform` |
| 3945-4060 | `WD_SetVertexShaderConstantF` (matrix capture for recovery) |
| 4100-4400 | Other intercepted methods |
| 4400-5000 | Memory patches (`TRL_ApplyMemoryPatches` — applies all 36 culling-layer NOPs) |
| 5000-5339 | `WrappedDevice_Create`, vtable construction, init |

### Key constants for future tuning

| Constant | Current | Effect |
|---|---|---|
| `DIAG_ENABLED` | 0 | Gate for all diagnostic logging |
| `PERF_LOG_ENABLED` | 1 | Gate for `PERF` lines in `ffp_proxy.log` — set to 0 for final ship build |
| `PERF_LOG_INTERVAL` | 600 | Frames per FPS sample |
| `PINNED_REPLAY_INTERVAL` | 600 | Frames between replay-cache scans |
| `PINNED_CAPTURE_FRAMES` | 120 | Capture window for PinnedDraw |
| `PINNED_DRAW_MAX` | 512 | Cache size for pinned draws |
| `S4_VB_CACHE_SIZE` | 512 | Cache size for SHORT4 expanded VBs |
| `DRAW_CACHE_ENABLED` | 1 | Enables per-frame draw replay (anti-cull) |
| `ENABLE_SKINNING` | 0 | Off — never flip without explicit ask |

---

## Part 2: Optimization Candidates — Not Yet Implemented

Ordered by expected FPS impact (highest first). Each item lists file:line citations, reasoning, expected gain, risk, and how to verify. **Future research starts here.**

Hard constraints from the project still apply: anything that risks hash stability, the 36 culling-layer NOPs, or the 5 anchored stage-light meshes is out of scope.

### 1. BeginScene anti-cull stamping → once-per-frame

**Where:** `proxy/d3d9_device.c:3393-3404`

TRL has 5–15 BeginScene/EndScene pairs per frame. Stamping runs every BeginScene = up to 15× per-frame. The source comment says: *"Re-stamps frustum threshold every scene — the game recomputes it **per-frame**, overwriting the one-shot patch from device creation."* — note **per-frame**, not per-scene. If correct, 14 of 15 stamps are wasted.

**Proposed:** Add a `stampedThisFrame` flag to `WrappedDevice`. In `WD_BeginScene`, gate the stamping on `!self->stampedThisFrame`, then set it. In `WD_Present` (after `frameCount++`), clear it. Saves ~9 volatile writes × 14 BeginScenes = ~126 stores per frame.

**Risk:** NEEDS VERIFICATION. If the game actually rewrites these globals per-scene (not per-frame), some scenes within a frame would render with culling re-engaged → hash drift, missing geometry, possible crash. Verify with `livetools memwatch` on each address before changing.

**Verification:**
```bash
python -m livetools attach trl.exe
python -m livetools memwatch start 0xEFDD64 4
# play for 5 seconds
python -m livetools memwatch read
# look for write count per frame
```

**Expected gain:** ~50µs/frame.

### 2. PGO (Profile-Guided Optimization)

**Where:** `proxy/build.bat`

Two-pass build: instrument → train → final. The optimizer learns which branches are hot and which to inline.

**Expected gain:** 5–10% on hot paths. Particularly helps the per-draw routing tree in `WD_DrawIndexedPrimitive`.

**Effort:** Medium — needs a clean training run (~10 min game time per iteration).

**Reference:** https://learn.microsoft.com/en-us/cpp/build/profile-guided-optimizations

### 3. `s4VBCache` linear scan → hash map

**Where:** `proxy/d3d9_device.c:2591-2600`

O(N) scan up to 512 entries per SHORT4 draw. Replace with hash map. Hash key = `(srcVB ^ srcOff ^ baseVtx ^ nv)` mod 1024.

**Effort:** Medium — needs a small open-addressing hash table in C (no CRT).

**Expected gain:** Modest. If cache hits within first 5–10 entries on average (which the audit suggests), the linear scan is already near-O(1). Win is on cache misses.

### 4. `WrappedDevice` struct field gating

**Where:** `proxy/d3d9_device.c:691-720`

~2 KB of fields referenced only inside `#if DIAG_ENABLED` blocks but the struct still allocates them:
- `vsConstWriteLog[256]` (~1 KB)
- `diagTexSeen[8][32]` and `diagTexUniq[8]`
- `loggedDecls[32]` and `loggedDeclCount`
- `diagLoggedFrames`, `diagMemLogged`, `diagSkinWorldLogged`
- `frameSummaryCount`, `createTick`

Wrap each in `#if DIAG_ENABLED`.

**Effort:** Low — every access site needs the same gate.

**Risk:** Build-break if any access site is missed. Linker error catches it.

**Expected gain:** Smaller struct → better cache residency on `self->...` accesses. Real but small.

### 5. Sky isolation — runtime-disable when user has tagged sky textures

**Where:** `proxy/d3d9_device.c:1525-1526` (already gated by `skyIsolationEnable`)

When user has manually tagged sky textures via `rtx.skyBoxTextures` in `rtx.conf`, the proxy's sky isolation does redundant work. Document `proxy.ini [Sky] EnableIsolation=0` as the recommended setting when manual sky tagging is in use.

**Effort:** Trivial (config + doc).

### 6. dxvk.conf perf tuning

**Where:** `Tomb Raider Legend/dxvk.conf` — currently has only `d3d9.shaderModel = 2`.

**Proposed:**
- `d3d9.maxFrameLatency = 1` — reduces input lag
- `dxvk.numCompilerThreads = 0` — use all CPU cores for shader compile

**Excluded:** `d3d9.presentInterval = 0` — interferes with DLFG/Reflex pacing on RTX 5090.

### 7. Identity-matrix fast-path in `TRL_ApplyTransformsCached`

**Where:** `proxy/d3d9_device.c:2173-2192` (the helper added in build 078)

UI/HUD draws hit the FFP path with `world=identity, view=identity, proj=identity`. Track an `appliedAreIdentity` flag.

**Expected gain:** Marginal. Only worth pursuing after measuring with `PERF_LOG`.

### 8. Replace `RealVtbl(self)[SLOT_X]` with cached function pointers

Many sites — e.g. `proxy/d3d9_device.c:2293`. Cache `vtSetTransform`, `vtSetStreamSource`, etc. as direct function pointers at init time.

**Expected gain:** 1–2 ns per call × hundreds of thousands of calls/sec = a few µs/frame.

### Already investigated and rejected

- **Disabling `DRAW_CACHE_ENABLED`**: Tested in build 066, no effect on perf, kept on for anti-cull safety net
- **Removing VP inverse threshold**: Tested in build 067, no effect
- **Re-enabling some engine culling layers**: Hashes destabilize, defeats the build-073 win
- **CPU smooth normals in proxy**: Tested 075+, changes geometry descriptor hashes, breaks anchors ([[Dead-Ends]] #15)

### How to add a new candidate

1. Identify a hot path or wasted work via `PERF_LOG` data, NVIDIA overlay, or `livetools collect` traces.
2. File:line citation.
3. Build a small test that demonstrates the cost (e.g. `livetools trace` showing call frequency).
4. Estimate gain with napkin math (calls/sec × ns/call).
5. Estimate risk (does it touch hashes / culling / anchored meshes?).
6. Add to this list with the same template.

If implemented, document in the Changelog under a new build number, snapshot source into a new `TRL tests/build-NNN-...` folder, push.

## See also

- [[Build-078-079-Performance-and-Skinning]] — the build that landed Tier A
- [[FFP-Proxy-Pipeline]] — what each section of the proxy does
- [[Reverse-Engineering-Toolkit]] — `livetools` commands for verification
