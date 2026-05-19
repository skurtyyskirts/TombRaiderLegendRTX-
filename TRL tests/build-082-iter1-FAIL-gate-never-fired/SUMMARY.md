# build-082 — iteration 1 of build 081 bind-pose VB cache — FAIL

**Date:** 2026-05-19 11:37 CDT
**HEAD:** `dd5cb611` (claude/sweet-raman-8c5b83)
**Test:** `python patches/TombRaiderLegend/run.py test-hash --build`
**Worktree:** `.claude/worktrees/sweet-raman-8c5b83`

## Result

**FAIL** — both PASS criteria missed:
1. Hash colors drift between screenshot 1 (`11.37.47.94.png`) and screenshot 2 (`11.37.51.02.png`). On Lara's torso/face the per-mesh hash-debug colors are visibly different between the two captures.
2. `ffp_proxy.log` shows zero `LaraVB cache hits=N` lines. The cache code path never executed.

## What Changed (vs starting state)

Pre-iter1 fix only: synced root `proxy/*` into `patches/TombRaiderLegend/proxy/*`. The repo carries two parallel proxy source trees — `proxy/` (canonical, where commit `dd5cb611` landed build 081's `laraVBCache`, `Lara_LookupCacheSlot`, `Lara_CaptureCacheSlot`, `TRL_ForceSkinnedNullVS`) and `patches/TombRaiderLegend/proxy/` (older build 080, what `run.py` actually builds). Without the sync, the first test run built the old source and produced a `d3d9.dll` with no Lara symbols at all. The sync restored the build 081 code into the build path. Build 081's `build.bat` lacks the VS18 Community fallback that the patches/-tree `build.bat` had, so `build.bat` was kept from the backup. Source files (`d3d9_device.c`, `d3d9_main.c`, `d3d9_wrapper.c`, `d3d9_skinning.h`), `proxy.ini`, and `d3d9.def` are now build 081.

No proxy code change — this is iteration 1 (as-shipped build 081).

## Screenshot Analysis

| Screenshot | Time | Observation |
|---|---|---|
| 1 (`11.37.47.94`) | T+1.0s after UP/DOWN/UP | Lara visible right side of frame, debug-277 hash patches: torso green+pink, head bluish, chest pink |
| 2 (`11.37.51.02`) | T+4.0s | Lara same pose region, but torso patches are green-dominant, head greenish, chest patch shifted upward |

Drift is unambiguous — the geometry-asset hash for the body submeshes is changing frame-to-frame. This reproduces the original "Lara hash drift" diagnosis that build 081 was meant to fix.

## Proxy Log Summary

Startup config (verified build 081 source is active):
```
Float3RoutingMode: auto
Float3Route effective: shader
SkinnedFloat3Route: null_vs   (Lara-class FLOAT3+FLOAT4tex forced to null-VS for stable asset hash)
NormalizeSkinnedDecl: 1
LaraClassBindPoseCache: 1
```

Decls observed at main menu (always-on decl dumper, cap 16):
- `DECL 0x017FBB90`: numElems=4, hasBW=0, hasBI=0, **posType=2 (FLOAT3)**, tex0 type=1 (**FLOAT2**), usage=5 idx=0. Menu/UI FLOAT3 decl.
- `DECL 0x1E09C218`: numElems=5, posType=7 (SHORT4), 4 tex elements. World-geometry SHORT4 decl.

Scene counters:
```
front-end:   d=12,  s4=0,   f3=12
gameplay:    d=600, s4=579, f3=21
```

`LaraVB cache hits=N` lines: **none**.
`MOVABLE forced null_vs: first occurrence` log line: **none**.

## Root Cause

`TRL_ForceSkinnedNullVS` (proxy/d3d9_device.c:1198) requires `curDeclPosType==FLOAT3 && curDeclTexcoordType==FLOAT4`. Main menu draws use only two decls; neither carries a FLOAT4 texcoord0. The gate returns 0 for every observed draw, so `forceSkinnedNullVS == 0`, and the cache short-circuit inside `useLaraCache = (forceSkinnedNullVS && ...)` is never reached.

The 21 FLOAT3 draws/frame at gameplay-scene-latch are presumably the menu Lara model + UI text, all drawn through the menu FLOAT3+FLOAT2tex decl rather than the gameplay character decl (FLOAT3+FLOAT4tex). Build 081's gate is correctly scoped for in-level character draws but excludes main-menu Lara.

## Next Build Plan

Iteration 2 = variant 6 from the briefing ("Widen the gate: cache ALL draws with FLOAT3 position regardless of texcoord type"). Variants 1–4 modify the cache key or capture, but cannot help because the cache code path itself is unreachable on the main menu. Variant 6 is the only listed mutation that lets the cache execute under the current test driver.

Change site: `TRL_ForceSkinnedNullVS` in `proxy/d3d9_device.c:1198–1206`. Drop the `curDeclTexcoordType==FLOAT4` half of the `isLaraClass` predicate so the gate fires on any FLOAT3-position draw when `SkinnedFloat3Route=null_vs`.

Risk: caching menu UI geometry. Mitigation: menu UI is non-skinned and bytes are stable per draw, so capture-then-replay is a no-op (key matches → cache hit → identical bytes replayed). Expected outcome: cache hits > 0, hashes stabilize across screenshots if the cause is CPU-skinning of the menu Lara model.
