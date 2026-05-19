# Changelog

Development log for the TRL RTX Remix port. Updated at the end of every session.
Format: `[YYYY-MM-DD HH:MM] LABEL — Summary` followed by findings, patches, test results, dead ends.
Full build history: [`docs/status/WHITEBOARD.md`](docs/status/WHITEBOARD.md)

---

## [2026-05-19 11:53] BUILD 084 — MIRACLE — iter3 of build 081 cache+replay — PASS

### Result
**PASS**. Lara's hash-debug colours are pixel-identical across both
screenshots. Cache fired with `hits=10000, misses=2, entries=2`.

### What Changed
Single literal in `useLaraCache` guard:
```diff
-    && nv > 0 && nv <= 16384);
+    && nv > 0 && nv <= 65535);
```
Edited both `proxy/d3d9_device.c:3953–3958` and
`patches/TombRaiderLegend/proxy/d3d9_device.c:3953–3958`.
Backup: `patches/TombRaiderLegend/backups/2026-05-19_1149_iter3-raise-nv-cap/`.

### Proxy log telemetry
```
LaraVB cache: first bind-pose snapshot committed
    nv=21845
    pc=1
    stride=24
  LaraVB cache hits=100      misses=2  entries=2
  LaraVB cache hits=1000     misses=2  entries=2
  LaraVB cache hits=10000    misses=2  entries=2
```
2 unique signatures captured, ≥10000 replays, zero post-capture misses.

### Iteration trace this session
- 082 (iter1, as-shipped 081): FAIL — gate `FLOAT3+FLOAT4tex` never matched
  any main-menu draw (decls are FLOAT3+FLOAT2tex and SHORT4 only).
- 083 (iter2, widen gate to FLOAT3-only): FAIL — gate canary fires, but
  inner `useLaraCache` guard `nv <= 16384` rejects main-menu draws which
  pass `nv=21845` (whole-buffer NumVertices).
- 084 (iter3, raise nv cap to 65535): **PASS**.

### Side-finding worth keeping
40 VB content fingerprint dumps all matched `csum=0x519E4D0B` with the
same first-vertex position. Main-menu Lara's VB content is **statically
stable**, not CPU-skinned. The hash drift in iters 1–2 was Remix
recomputing geometry-asset hashes from different stream bindings
per draw, not from changing vertex bytes. The cache fixes this by
re-binding the same private VB on every replay so Remix always sees
the same buffer pointer + content.

### Open follow-ups (not in scope tonight)
1. Gameplay-tier validation — this PASS is main-menu only. Re-test at Peru.
2. The startup help-text `SkinnedFloat3Route: null_vs (Lara-class
   FLOAT3+FLOAT4tex ...)` no longer matches semantics after iter2 widened
   to FLOAT3-only. Cosmetic, low-priority.
3. Build pipeline lints `proxy/` vs `patches/TombRaiderLegend/proxy/`
   divergence — without the source-tree sync, build 081 was silently
   bypassed on the first iter1 attempt.

### Archive
`TRL tests/build-084-miracle-iter3-PASS-lara-hash-stable/`

---

## [2026-05-19 11:46] BUILD 083 — iter2 of build 081 cache+replay — FAIL (nv cap blocks main-menu draws)

### Result
**FAIL**. Hashes still drift; cache still didn't fire (no `LaraVB cache hits=`
in proxy log).

### What Changed
Variant 6 — widened `TRL_ForceSkinnedNullVS` gate. Before: `FLOAT3 pos &&
FLOAT4 tex0` (gameplay character signature). After: `FLOAT3 pos` only,
regardless of tex0 type. Edited both `proxy/d3d9_device.c:1198–1209` and
`patches/TombRaiderLegend/proxy/d3d9_device.c:1198–1209`.
Backup: `patches/TombRaiderLegend/backups/2026-05-19_1142_iter2-widen-gate/`.

### Proxy log evidence
- Line 57: `MOVABLE forced null_vs: first occurrence` — gate canary now fires.
- Lines 58+: 40 `VBfp` dumps (cap), all with `vb=0x019277D0`, `nv=21845`,
  `stride=24`, `csum=0x519E4D0B`. The VB content is stable across draws
  (same checksum, same first-vertex position) — main-menu FLOAT3 draws are
  not CPU-skinned; they're plain static menu geometry with per-draw world
  matrix. This is a useful new fact about main-menu Lara.

### Root cause
`useLaraCache` carries a secondary guard `nv > 0 && nv <= 16384` inside the
DIP path (`proxy/d3d9_device.c:3948–3953`). 21845 > 16384, so the cache
short-circuit short-circuits the wrong way and the capture/lookup path
never runs. The 16384 was a safety bound on snapshot alloc; bumping to
65535 (uint16 max NumVertices) grows worst-case from ~24 MiB to ~96 MiB.

### Decision
Iter3 = raise the nv cap to 65535 — single literal change in
`useLaraCache`. If the cache then fires but hashes still drift, fall back
to variant 1 (drop tex0 from the cache key).

### Archive
`TRL tests/build-083-iter2-FAIL-nv-cap-blocked/`

---

## [2026-05-19 11:37] BUILD 082 — iter1 of build 081 cache+replay — FAIL (gate never fires on main menu)

### Result
**FAIL**. Both PASS criteria missed:
1. Lara's hash-debug colors drift between screenshot 1 and screenshot 2.
2. `ffp_proxy.log` contains zero `LaraVB cache hits=N` lines — the cache code path never executed.

### Pre-iter1 source sync
Repo has two parallel proxy trees: `proxy/` (canonical, build 081) and
`patches/TombRaiderLegend/proxy/` (older build 080). `run.py` builds the
patches/ tree. First test produced a `d3d9.dll` with `NormalizeSkinnedDecl`
but no `laraVB` / `SkinnedFloat3Route` / `LaraClassBindPose` symbols. Synced
root `proxy/{d3d9_device.c, d3d9_main.c, d3d9_wrapper.c, d3d9_skinning.h, proxy.ini, d3d9.def}`
into `patches/TombRaiderLegend/proxy/`, kept the patches/ tree's `build.bat`
because it carries the VS18 Community fallback the canonical script lacks.
Backup at `patches/TombRaiderLegend/backups/2026-05-19_1133_pre-build-081-sync/`.

### Decls observed at main menu (debug 277, debug delay 50000ms)
- `DECL 0x017FBB90`: numElems=4, hasBW=0, hasBI=0, posType=2 (FLOAT3),
  tex0 type=1 (FLOAT2). Menu/UI FLOAT3 decl. 12 front-end draws,
  21 of the 600 gameplay-latched draws.
- `DECL 0x1E09C218`: numElems=5, posType=7 (SHORT4), 4 tex elements.
  World-geometry SHORT4. 579 of 600 gameplay-latched draws.

Cap is 16 unique decls. Only 2 were ever emitted at the main menu, so
these are exhaustive — every draw on the main menu uses one or the other.

### Root cause
`TRL_ForceSkinnedNullVS` (`proxy/d3d9_device.c:1198`) gates on
`curDeclPosType==FLOAT3 && curDeclTexcoordType==FLOAT4` (the gameplay
character "Lara-class" signature). Main-menu Lara is drawn through the
menu FLOAT3+FLOAT2tex decl, not the gameplay decl, so the gate evaluates
to 0 for every observed draw. The cache short-circuit
`useLaraCache = (forceSkinnedNullVS && laraClassBindPoseCacheEnabled && ...)`
is never entered. `MOVABLE forced null_vs: first occurrence` log line —
the canary the gate is supposed to emit — never appears in the log.

### Decision
Briefing variants 1–4 modify the cache key (drop tex0, lock-after-N,
snapshot whole VB, bump cache size) but cannot help when the gate above
them never fires. Variant 5 (disable the cache) is a sanity check that
also can't help. Variant 6 — widen the gate to all FLOAT3 draws
regardless of texcoord type — is the only listed mutation that lets the
cache code path execute under the current main-menu-only test driver.
Proceeding directly to iter2 = variant 6.

### Archive
`TRL tests/build-082-iter1-FAIL-gate-never-fired/`

---

## [2026-05-18 22:50] SESSION — Lara/movable hash drift root-caused, bind-pose VB cache+replay shipped (UNTESTED)

### Objective
Stabilize the asset hash for Lara, the practice dummy, and the soccer ball so
their meshes can carry light anchors and replacement assets. Build 080's
intermediate states verified along the way, then pivoted to a model-2 fix once
ground truth was known.

### Diagnostic Cycles This Session
Three iterative proxy builds with increasing instrumentation, each test cycle
isolating one variable so failure was bisectable.

#### Cycle 1 — `SkinnedFloat3Route=null_vs` (gate: `curDeclIsSkinned`)
- Added an INI toggle that forces *skinned* FLOAT3 draws onto the null-VS path
  even when `rtx.useVertexCapture=True` would otherwise force the shader route.
- Gate condition: `curDeclIsSkinned` (BLENDWEIGHT + BLENDINDICES present in
  the vertex decl).
- **Deploy bug repeated**: the canonical game directory at
  `AlmightyBackups/NightRaven1/Vibe-Reverse-Engineering-Claude/Tomb Raider Legend\`
  had a stale May-11 `d3d9.dll`. First "FAIL" was the user testing the wrong
  binary — same class of issue as build 075's `user.conf` regression, and
  `user.conf` had silently flipped `rtx.enableReplacementAssets=False` again
  via the in-game Remix menu. Both reset before re-test.
- After clean deploy: zero `SKINNED decl=` log entries across the entire
  session. The override gate never fired because no draw was classified as
  skinned.

#### Cycle 2 — Always-on per-decl dumper
- Added a DIAG-independent first-encounter dumper (cap 16) so we could see
  every unique vertex declaration regardless of skinning classification or
  the 50-second diagnostic delay.
- **Three unique decls observed** during gameplay:
  - **Decl A** (`0x01944D40`) — front-end / menu backdrop: `POSITION FLOAT3 +
    COLOR D3DCOLOR + TEXCOORD0 FLOAT2`.
  - **Decl B** (`0x019450C0`) — world geometry, 579 of 599 draws/scene:
    `POSITION SHORT4 + COLOR + TEXCOORD0 SHORT2 + TEXCOORD1 SHORT2`. Already
    routed through `S4_ExpandAndDraw`; hashes stable since build 075.
  - **Decl C** (`0x01944950`) — gameplay characters/movables (Lara, dummy,
    soccer ball, NPCs): `POSITION FLOAT3 + COLOR D3DCOLOR + TEXCOORD0
    **FLOAT4**`. ~20 draws/scene. **No BLENDWEIGHT, no BLENDINDICES, no
    NORMAL.** That ruled out the assumption baked into all prior skinning
    work — TRL does not surface formal blend usages anywhere in its character
    vertex format.

#### Cycle 2.5 — Broadened gate
- Re-targeted `TRL_ForceSkinnedNullVS` from `curDeclIsSkinned` to a runtime
  signature gate: `curDeclPosType == FLOAT3 && curDeclTexcoordType == FLOAT4`.
  That matches Decl C exclusively (front-end is FLOAT2 texcoord, world
  geometry is SHORT4 position) so the override scope is character/movable
  only.
- `MOVABLE forced null_vs: first occurrence` log line confirmed the override
  finally engaged for at least one Lara-class draw.
- **Hash still drifted in debug view.** Override active and ineffective.

#### Cycle 3 — Per-draw VB content fingerprint
- Added a fingerprint logger inside the override path (cap 40 dumps): for each
  Lara-class draw, lock the source VB at the start vertex, log VB pointer +
  vertex count + stride + FNV-1a checksum of the first 32 bytes + first
  vertex XYZ.
- **Result (smoking gun)**:
  - Every draw uses the same VB pointer `0x017A3CD8`. Single shared buffer.
  - Draws 0–13 form 7 pairs (drawIdx 0+1, 2+3, …, 12+13): each pair identical
    csum and position, distinct between pairs — likely 7 submeshes drawn
    twice each (shadow/depth + color pass).
  - Draws 14–39 all have the **same first-vertex position** `(289.206,
    151.305, 337.065)` but **every draw has a different csum**. The
    non-position bytes (D3DCOLOR + 16-byte FLOAT4 TEXCOORD) change between
    consecutive draws.
- **Conclusion**: TRL streams CPU-skinned vertex data into one shared VB,
  rewriting the relevant region between draws. Remix snapshots whatever is
  in the VB at DIP time, and the content is different every draw. **No
  routing decision in the proxy can stabilize the hash with the live VB** —
  the bytes themselves differ.

### Fix Shipped (Untested) — Lara-class Bind-Pose VB Cache+Replay
- Per-draw signature `(nv, pc, tex0, bvi, mi)` keys a private cache (capacity
  64) of bind-pose vertex buffers owned by the proxy.
  - First sight of a signature → snapshot: allocate a managed VB,
    `D3DLOCK_READONLY` the live VB at the draw's start offset for `nv*stride`
    bytes, copy into the snapshot VB.
  - Subsequent draws matching the signature → replay: `SetStreamSource` to
    the snapshot, DIP with `bvi = -(int)mi` so the cached VB's vertex 0 is
    indexed correctly, then restore stream to the live VB.
- INI toggle `[FFP] LaraClassBindPoseCache=1` (default on when
  `SkinnedFloat3Route=null_vs`). Cleanup wired into `WD_Release`.
- Telemetry: `LaraVB cache hits=N misses=M entries=K` log lines at 100,
  1000, 10000 hits.
- **Visual tradeoff is now explicit**: through the Remix view, Lara/dummy/
  ball appear frozen in whatever pose was current when each submesh was
  first captured. The game's raster path is unaffected (game still animates
  via its own pipeline; the cache only changes what Remix's d3d9 layer
  receives). The stable hash unlocks the Toolkit replacement-asset workflow,
  which is the actual end goal.

### Files Modified
- `proxy/d3d9_device.c`:
  - New struct fields: `laraVBCache[64]`, `laraVBCacheCount`, `laraVBCacheHits`,
    `laraVBCacheMisses`, `laraClassBindPoseCacheEnabled`,
    `skinnedFloat3RoutingMode`, `vbFingerprintCount`, `allDeclsSeen[16]`,
    `allDeclsLoggedCount`, `skinnedForcedNullVSLogged`.
  - New helpers: `TRL_ForceSkinnedNullVS` (signature-gated), `Lara_LookupCacheSlot`,
    `Lara_CaptureCacheSlot`, `Lara_ReleaseCache`.
  - DIP path now branches snapshot/replay vs live VB inside the null-VS arm.
- `proxy/proxy.ini`: added `[FFP] SkinnedFloat3Route` and `[FFP]
  LaraClassBindPoseCache` toggles with explanatory comments.
- Backup snapshot at
  `patches/TombRaiderLegend/backups/2026-05-18_skinned-float3-route-null-vs/`.

### Open Verification
The cache implementation is shipped and deployed but NOT YET TESTED end-to-end.
Pending in-game retest. Success criterion: hash-debug view 277 shows each
unique Lara submesh holding a single color across all 3 camera-pan
screenshots. Telemetry `LaraVB cache hits=100` should appear within seconds
of gameplay if the cache is engaging.

### Dead Ends Confirmed This Session
- Routing skinned FLOAT3 draws to null-VS does *not* stabilize the hash on
  its own — the VB content changes between draws, so the proxy and Remix
  both see different bytes regardless of routing.
- Gating any skinning-related logic on `curDeclIsSkinned` (BLENDWEIGHT +
  BLENDINDICES) is wrong for TRL — those usages never appear in the game's
  vertex decls (confirmed via always-on decl dumper, only 3 unique decls
  observed across an entire gameplay session).

---

## [2026-05-14 03:15] SESSION — Test-harness repair + build 080 source merge (untested)

### Objective
Resume the build-079 workstream. Repair the broken test pipeline, generate the
missing static-analysis artifact for build 079, and prepare build 080 to
address the shader-route mismatch identified in 079.

### What Changed
- **run.py**: prefix `build.bat` with `.\` so cmd.exe finds it under modern
  Windows `NoDefaultCurrentDirectoryInExePath` policy.
- **launcher.py**: add `choose_launch_route()` (continue vs newgame); accept
  `route=` kwarg on `navigate_to_peru`; import `GAME_DIR`/`GAME_EXE`/`LAUNCHER`
  from `config.py` so the `TRL_GAME_DIR` env var actually takes effect.
- **proxy/d3d9_device.c**: merged build-079 decl-strip plumbing into the
  current source (preserving H4 VP-lock) — `BuildSkinnedNormalizedDecl`,
  `curNormalizedSkinnedDecl`, INI toggle `[FFP] NormalizeSkinnedDecl=1`,
  always-on `SKINNED decl=` log. **New for build 080**: wrap the shader-route
  DIP with the same `SetVertexDeclaration` sandwich used by the null-VS
  branch. Both branches now swap to the normalized decl before DIP and
  restore after — fixing 079's "decl swap doesn't engage for Lara because
  she takes the shader route" diagnosis.
- DLL builds clean at 50,688 bytes. Pre-merge backup retained at
  `patches/TombRaiderLegend/backups/2026-05-14_build080_skinned-decl-shader-route/`.

### Static Analysis Log (build 079)
Generated by static-analyzer subagent against
`AlmightyBackups/NightRaven1/Vibe-Reverse-Engineering-Claude/Tomb Raider Legend/trl_dump_SCY.exe`.
Written to `TRL tests/build-079-normalize-skinned-decl-FAIL-shader-route-mismatch/static_analysis_log.md`.

- All 23 documented patch sites verified — no binary drift. Every documented
  instruction shape still fits, so proxy runtime patches land cleanly.
- Skinned-submit dominant function = `0x006133D7`. It calls
  `Renderer_SetVertexShader(piVar4, shader)` **before** the DIP — this is
  why Remix sees a non-null VS for Lara and the proxy's shader-route
  branch fires. Confirms 079's diagnosis at the binary level.
- One MISS-static: `0xF12016` reads 0 in the static dump (post-sector loop
  enable) — recheck whether the proxy stamps it at runtime.
- `find_skinning.py` returns "none detected" because TRL is pure VS-skinning,
  no FFP `D3DRS_INDEXEDVERTEXBLENDENABLE` palette. The script's "set
  Enabled=0" recommendation is wrong for this binary.

### Test Result
**NOT RUN.** PASS criterion requires lights visible in every clean shot, and
the 5 anchor mesh hashes in `mod.usda` are still stale (the actual remaining
blocker per WHITEBOARD). Running build 080 now would FAIL on lights
regardless of whether the shader-route fix works. Held off pending a fresh
Remix Toolkit capture at the Peru stage to refresh `mod.usda`.

### Game Install Reconciliation
Multiple TRL installs exist across sibling repos. Active one (mtime
2026-05-14 02:51 — today):
`C:\Users\skurtyy\Documents\GitHub\AlmightyBackups\NightRaven1\Vibe-Reverse-Engineering-Claude\Tomb Raider Legend\`.
The repo's local `Tomb Raider Legend/` folder is a deploy-only stub (no
`trl.exe`). Set `TRL_GAME_DIR` to the AlmightyBackups path before running
`run.py test-hash --build`.

### Next Build Plan
1. Manual: launch via `NvRemixLauncher32.exe` → Peru stage → Remix Toolkit
   capture → extract the 5 building mesh hashes → update `mod.usda`.
2. `TRL_GAME_DIR=… python patches/TombRaiderLegend/run.py test-hash --build`
   (build 080 DLL already built and ready to redeploy).
3. Inspect `ffp_proxy.log` for `SKINNED decl=` entries (proxy logs first 8
   unique skinned decls). Confirms whether Lara is FLOAT3 or SHORT4 and
   which branch the new decl-swap sandwich engaged on.
4. Compare hash-debug screenshots: Lara should keep the same color across
   the camera pan.

### Commits
- `2200f5b` revert idea-generation.yml back to claude-opus-4-7
- `2d74cfb` test-harness fixes + build 080 source merge + static analysis log + WHITEBOARD
- `e2ff7d1` remove ralph-loop state file (user wants testing on hold)

---

## [2026-05-11 19:38] BUILD-079 — Normalize skinned decl (FAIL: shader-route mismatch)

### Objective
Stabilize Lara Croft's asset hash. World geometry hashes are stable, but Lara's mesh-hash colors drift between frames in the hash-debug view (and Toolkit hash IDs reportedly change frame-to-frame). Goal: anchor lights / material replacements to Lara reliably.

### What Changed
- New helper `BuildSkinnedNormalizedDecl()` in `proxy/d3d9_device.c` — clones a skinned vertex declaration with `BLENDWEIGHT` + `BLENDINDICES` removed (preserves offsets/types for everything else). Mirrors the existing `BuildStrippedDeclIfNeeded` pattern.
- `WD_SetVertexDeclaration`: when `curDeclIsSkinned`, builds the normalized clone and stashes it in `self->curNormalizedSkinnedDecl`. First 8 unique skinned decls logged to `ffp_proxy.log` always-on (not gated by `DIAG_ENABLED`).
- `WD_DrawIndexedPrimitive` null-VS path: swaps to normalized decl around the FFP draw, restores `lastDecl` after.
- New `WrappedDevice` fields: `skinnedNormDecl{Orig,Fixed}[64]`, `curNormalizedSkinnedDecl`, `normalizeSkinnedDecl` (INI toggle, default 1), `skinnedDeclsLogged`.
- Cleanup hooks in `Reset` and `~WrappedDevice` release the normalized-decl cache.
- `proxy.ini` gained `[FFP] NormalizeSkinnedDecl=1` for A/B testing.
- DLL grew 48,640 → 50,176 bytes.

### Test Result
**FAIL — Lara still drifts.** World remains stable. Distant NPC silhouettes also drift (consistent with "all skinned characters affected, not just Lara").

Two compounding issues:

1. **Wrong deployment target** — initially deployed to `TombRaiderLegendRTX-/Tomb Raider Legend/` (a stub inside the repo containing only `d3d9.dll`/`rtx.conf`/`rtx-remix/`, no `trl.exe`). User's actual game install is one level up at `Vibe-Reverse-Engineering-Claude/Tomb Raider Legend/` (with `trl.exe`, `NvRemixLauncher32.exe`, the `bigfile.NNN` archives, and the `ffp_proxy.log` from the latest run). The first test ran the old build-078 DLL. **Workspace deployment rule saved to project memory** at `memory/feedback_proxy_deployment.md`.
2. **Route mismatch** — `ffp_proxy.log` confirms `Float3Route effective: shader`. With `rtx.useVertexCapture = True` in `rtx.conf` and default `Float3RoutingMode=auto`, FLOAT3 draws like Lara take the **shader route** (lines 3644-3650 in `d3d9_device.c`), not the null-VS path where my decl swap is wired (lines 3651-3666). The fix is correctly built and deployed (verified after the workspace-rule learning), but it does not engage for Lara.

### Open Hypotheses

1. **(Strongest)** Skinned draws go through `useVertexCapture` shader path → Remix captures VS-output positions → asset-hash drift is a property of how Remix hashes VS-captured skinned geometry. Fix must engage on the shader route or force Lara onto null-VS.
2. The "debug geometry view" the user is toggling may be the *generation* hash visualization (positions-based, expected to flicker for skinned per build-073 TECHNICAL_ANALYSIS.md), not the *asset* hash. Confirm before pivoting code.
3. Lara may be SHORT4 skinned, not FLOAT3 skinned. Latched-scene draw mix in the old log: 579 SHORT4 vs 21 FLOAT3 — most of the world is SHORT4. If Lara is SHORT4, the hook is `TRL_ShouldShaderRouteAnimatedShort4Draw` (line ~1044), not the FLOAT3 branch.

### Next Build Plan
1. Confirm hypothesis #2 with the user — which hash view are they actually seeing?
2. If true positive on asset-hash drift: retest with **this build correctly deployed** and read the `SKINNED decl=` log entries to determine Lara's vertex format and decl signature(s).
3. Branch the fix: either (a) extend the decl swap into the SHORT4 `S4_ExpandAndDraw` path if Lara is SHORT4, or (b) add an INI toggle to force skinned FLOAT3 through `FLOAT3_ROUTE_NULL_VS` regardless of `useVertexCapture` (Lara visual through Remix would become bind-pose — known tradeoff).

### Dead End Candidates (not yet)
The fix isn't a dead end — it just doesn't fire for Lara on the current route. Leave the code in place; the INI toggle lets us disable for A/B if needed. Re-evaluate after the next test cycle confirms whether decl normalization on the right route fixes it.

---

## [2026-05-05] BUILD-078 — Perf build: proxy CPU hot-path optimization

### Objective
Strip every byte of dead per-draw / per-frame work from the proxy now that hashes are stable and all 36 culling layers are disabled. Goal: maximum proxy CPU efficiency for the RTX 5090 path-traced runtime, **without touching `rtx.conf`** (user tunes Remix-side via the in-game menu X). Hash stability and the 36 culling-layer NOPs are preserved byte-for-byte.

### Findings
- `DIAG_ACTIVE(self)` macro (used at 11 per-draw / per-VS-write sites) calls `GetTickCount()` on every check, even after the 3-frame log window closes at 50s. Was burning a syscall per D3D9 draw call (~3,749 draws/scene × 60+ FPS) for the entire session.
- The proxy was firing `SetTransform(WORLD)` + `SetTransform(VIEW)` + `SetTransform(PROJECTION)` **unconditionally on every FFP-routed draw** at two call sites (`TRL_ApplyTransformOverrides` and `S4_ExpandAndDraw`). View/Proj are typically constant within a frame; only World changes per object → ~2/3 of `SetTransform` calls are redundant.
- `PinnedDraw_ReplayMissing` walks 512 entries every 60 frames and finds zero work post-capture (engine culling fully disabled means no draws are missing). Pure overhead.
- Three diagnostic-logging blocks were running outside `#if DIAG_ENABLED` (frame summary, per-scene census, PostLatch). Now properly gated.
- Build flags already optimal — `/O2 /Oi /fp:fast /GL /GS- /Zl + NDEBUG + /LTCG`. No room there.
- `WD_SetTransform` already blocks all external V/P/W writes once `viewProjValid=1` ([d3d9_device.c:3936-3940](patches/TombRaiderLegend/proxy/d3d9_device.c)) → proxy is the **only** writer, so an "applied transforms" cache is authoritative.

### Patches Applied (Build 078)
- `DIAG_ENABLED 1 → 0` — preprocessor strips all 11 `#if DIAG_ENABLED` blocks (per-draw GetTickCount, VB lock + hex dump, 256-register zero-loop on Present, sprintf in SetVSConstantF, 8-stage texture iteration)
- `PINNED_REPLAY_INTERVAL 60 → 600` — replay runs ~10× less often
- `PERF_LOG_ENABLED 1` + `PERF_LOG_INTERVAL 600` — emits `PERF frames=600 ms=N fps=N` to `ffp_proxy.log` every ~10s (independent of DIAG so survives the strip)
- New `appliedWorld[16]`, `appliedView[16]`, `appliedProj[16]` fields in `WrappedDevice` (zero-init via `HEAP_ZERO_MEMORY`)
- New `TRL_ApplyTransformsCached` helper — does `memcmp(64)` per slot, fires `SetTransform` only on changed slots
- Both `SetTransform`-3-call sites replaced with single helper call (`TRL_ApplyTransformOverrides`, `S4_ExpandAndDraw`)
- Cache invalidated to zeros in `WD_Reset` (matches device's reset state)
- Wrapped 3 residual diagnostic logging blocks in `#if DIAG_ENABLED` (frame summary, per-scene census S<n>, PostLatch)
- Mirrored byte-identical to root [proxy/d3d9_device.c](proxy/d3d9_device.c) per `feedback_proxy_sync` (memory cites prior 8-day crash from missing this sync)

### Test Results
- Build 078: BUILT, DEPLOYED, AWAITING IN-GAME MEASUREMENT
- DLL: 48,640 bytes (vs build 077's 56,320 bytes — −13.6% from dead-code strip)
- Both proxy copies (`patches/TombRaiderLegend/proxy/` and root `proxy/`) byte-identical
- Backup: `patches/TombRaiderLegend/backups/2026-05-05T045306Z_perf-build-pre/`

### Risk Assessment
- Hash stability: zero impact — matrix values delivered to D3D9/Remix are byte-identical to prior; only redundant calls are elided
- Stage lights: zero impact — no anchor mesh hash changes
- Culling: zero impact — all 36 culling-layer NOPs intact
- Cold launch: zero impact — `WD_Reset` invalidation keeps cache correct across device reset

### Open Questions / Next Steps
- Run hash-stability test with `python patches/TombRaiderLegend/run.py test --build` to confirm:
  - All hash PASS criteria still met
  - `PERF` lines appearing in `ffp_proxy.log`
- Capture an FPS baseline via `PERF_LOG` or NVIDIA overlay for future delta measurement
- Pursue next-tier optimizations from [TRL tests/build-078-perf-build/OPTIMIZATION_CANDIDATES.md](TRL%20tests/build-078-perf-build/OPTIMIZATION_CANDIDATES.md):
  1. BeginScene anti-cull stamping → once-per-frame (needs `livetools memwatch` verification first)
  2. PGO (Profile-Guided Optimization) — 5-10% on hot paths
  3. `s4VBCache` linear scan → hash map
  4. `WrappedDevice` struct field gating with `#if DIAG_ENABLED` (deferred from build 078, ~2KB cache footprint)
  5. dxvk.conf tuning (`d3d9.maxFrameLatency=1`, `dxvk.numCompilerThreads=0`)

### Handoff Materials
[`TRL tests/build-078-perf-build/`](TRL%20tests/build-078-perf-build/) contains:
- `SUMMARY.md` — this build's full analysis
- `HOTPATH_AUDIT.md` — pre-build per-draw / per-frame cost map
- `OPTIMIZATION_CANDIDATES.md` — ranked list of optimizations not yet implemented
- `proxy_changes.diff` — unified diff vs build 077
- `proxy/` — full source snapshot (5,339 lines) + `d3d9.dll` binary

---

## [2026-04-13] BUILDS-076-077 — Crash protections restored, cold launch stable

### Objective
Restore accidentally dropped crash protections and fix cold launch crash triggered by menu→level transition.

### Findings
- **Build 076**: Null-crash guard at `0x40D2AF` and PUREDEVICE stripping in `W9_CreateDevice` were accidentally removed in a prior session. Restoring both (plus FourCC format rejection and VS 18 build chain fallback) brings game back to full render health: 3,733 draws/scene, all 31 patches active, replacement (purple) light visible in clean render.
- **Build 077 — ROOT CAUSE FOUND**: All automated test builds (001–076) used `TR7.arg` to jump directly into Peru, bypassing the main menu. The first manual cold launch (without `TR7.arg`) crashed ~60–70 seconds in. Root cause: `DrawCache_Record()` stored raw, un-referenced COM pointers (vb/ib/decl/tex0). The menu→level transition freed menu geometry while the cache still held dangling pointers. On the next `Present`, `DrawCache_Replay()` passed freed pointers into `SetStreamSource` + `DrawIndexedPrimitive`, crashing the Remix bridge the moment NRC activated for the first raytrace frame.
- **Fix**: `DrawCache_Record` now `AddRef`s all four resources (vb, ib, decl, tex0); `DrawCache_Clear()` releases all refs; all three `s_drawCacheCount = 0` sites replaced with `DrawCache_Clear()`. Game survives cold launch, 90+ seconds stable.

### Patches Applied (Build 076)
- Restored `patch_null_crash_40D2AF()` — code-cave trampoline at `0x40D2AC` guards null deref at `0x40D2AF`
- Restored PUREDEVICE stripping: `behFlags &= ~0x00000010` in `W9_CreateDevice`
- Restored FourCC format rejection: `D3DERR_NOTAVAILABLE` for `cf > 0xFF` in `W9_CheckDeviceFormat`
- Added VS 18 (`Microsoft Visual Studio\18\Community`) to `build.bat` VSDIR detection chain

### Patches Applied (Build 077)
- `DrawCache_Record`: `com_addref(c->vb)`, `com_addref(c->decl)`, `com_addref(c->tex0)`; removed immediate `Release` after `GetIndices` (kept that AddRef as cache ref)
- New `DrawCache_ReleaseEntry()`: releases com refs, marks slot inactive
- New `DrawCache_Clear()`: iterates all active entries, calls `DrawCache_ReleaseEntry`, zeros count
- `DrawCache_Replay()`: stale eviction now calls `DrawCache_ReleaseEntry` instead of `c->active = 0`
- `WD_Release`, `WD_Reset`, transition flush: all three `s_drawCacheCount = 0` → `DrawCache_Clear()`

### Test Results
- Build 076: FAIL-lights-missing — 3,733 draws/scene, crash-free, replacement light visible, stage lights absent (stale hashes)
- Build 077: FIXED — game runs 90+ seconds from cold menu start, 2,468 draws/scene, no crash

### Open Questions Updated
- [x] Can the game launch stably without TR7.arg? → **YES** (build 077)
- [x] Did build 076 restore full rendering after the dropped crash guards? → **YES** (3,733 draws/scene, all patches active)

### Next Steps
1. **HIGHEST**: Fresh Remix capture near Peru stage → extract building mesh hashes → update `mod.usda`
2. **HIGH**: Re-test with updated hashes — expect red+green lights to appear
3. **FALLBACK**: Anchor lights to Lara's body mesh (always visible since build 071b) as a guaranteed-visible reference

---

## [2026-04-09] BUILDS-074-075 — Deferred patches, user.conf breakthrough, replacement assets confirmed

### Objective
Optimize proxy initialization (deferred patches, permanent page unlock), then verify anchor mesh hashes by testing with replacement assets enabled.

### Findings
- **Build 074**: Deferred patches (now run on first valid `BeginScene` instead of device creation) fix main menu crash. Permanent `VirtualProtect` page unlock eliminates ~28 kernel calls/frame. All 31 patches confirmed. Draw counts stable at ~3749. Lights still absent.
- **Build 075 — ROOT CAUSE FOUND**: `user.conf` in the game directory had `rtx.enableReplacementAssets=False`. This file is written by the Remix developer menu and overrides `rtx.conf` (Remix loads: `dxvk.conf → rtx.conf → user.conf`). This single line was silently disabling ALL mod content (lights, materials, meshes) in EVERY build from 016 to 074.
- **Replacement asset pipeline CONFIRMED WORKING**: After fixing `user.conf`, a purple test light (`mesh_574EDF0EAD7FC51D`) appeared, remained stable across all 3 camera positions, and shifted correctly with camera movement. End-to-end proof that the pipeline works.
- **Stage lights still absent**: The 8 building mesh hashes in `mod.usda` are stale. Testing with `useVertexCapture=True` and `False`, and light radius 2→3000 produced no change — proving no currently-rendered mesh matches those hash IDs. The "white dots" from builds 072-073 were denoiser/NRC artifacts, not anchor lights.
- **Hash rule updated**: `positions,indices,texcoords,geometrydescriptor` (positions added back — required for generation hash, doesn't affect asset hash stability because Remix hash rules are independent).

### Patches Applied (Build 074)
- Deferred `TRL_ApplyMemoryPatches()` to first valid `BeginScene` (fixes menu crash; no longer needs `TR7.arg` skip)
- Permanent page unlock: `VirtualProtect(PAGE_READWRITE)` once per data page, removes ~28 kernel calls/frame

### Test Results
- Build 074: FAIL — all patches active, 3749 draws/scene, no lights, no crash
- Build 075: FAIL — purple test light visible (pipeline confirmed!), stage light hashes stale, needs fresh capture

### Dead Ends Discovered
- `user.conf` `enableReplacementAssets=False` — silently disabled all mod content builds 016-074 (fixed 075)

### Open Questions Updated
- [x] Does the replacement asset pipeline actually work? → **YES** (build 075 purple light)
- [x] Are the white dots in build 073 the stage lights? → **NO** — denoiser/NRC artifacts; radius 2→3000 produced no change
- [ ] What are the current mesh hash IDs for the building geometry at the Peru start position?

### Next Steps
1. **HIGHEST**: Fresh Remix capture near Peru stage → extract building mesh hashes → update `mod.usda`
2. **HIGH**: Re-test with updated hashes — expect red+green lights to appear
3. **FALLBACK**: Anchor lights to Lara's body mesh (always visible since build 071b) as a guaranteed-visible reference

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
| 5 | 032 | Config flag at 0x01075BE0 ("disable extra static light culling") | No code xrefs, not connected to light collection |
| 6 | 025 | Pending-render flag NOPs (0x603832, 0x60E30D) | No effect on bottleneck |
| 7 | 026 | LightVolume_UpdateVisibility state NOPs (5 addrs) | Patches not confirmed in proxy log — silent VirtualProtect failure |
| 8 | 045 | D3DPOOL_MANAGED + per-frame VB flush | Flush too aggressive (512 VB creates/frame); D3DPOOL_MANAGED is correct, flush is not |
| 9 | 046 | Null VS for ALL draws (content fingerprint cache) | Breaks view-space geometry — FLOAT3 view-space positions render at extreme scale |
| 10 | 047 | Remove `positions` from asset hash | Catastrophic hash collision — all geometry gets same hash; `positions` is required |
| 11 | 066 | Draw cache disabled (`DRAW_CACHE_ENABLED 0`) | No effect — cache only replays 3 draws; stale pointer concern was unfounded |
| 12 | 067 | Remove VP inverse epsilon threshold | No effect — VP changes on camera pan are always large enough to trigger recalculation |
| 13 | 072 | RenderQueue_FrustumCull bypass (0x40C430 → 0x40C390) | +29% draws, no crash — lights still absent; anchor hash mismatch was the real cause |
| 14 | 016–074 | `user.conf` `rtx.enableReplacementAssets=False` | Remix developer menu regenerates `user.conf` and resets this flag to `False`; silently disabled all mod content for 59 builds — **fixed in build 075** |

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

