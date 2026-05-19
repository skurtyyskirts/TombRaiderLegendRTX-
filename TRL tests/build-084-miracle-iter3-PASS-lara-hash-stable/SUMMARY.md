# build-084-miracle — iter3 of build 081 cache+replay — PASS

**Date:** 2026-05-19 11:53 CDT
**Variant applied this iteration:** raise `useLaraCache` nv cap from 16384 to 65535. Sequenced on top of iter2's widen-gate (FLOAT3 only, drop FLOAT4tex requirement) and the pre-iter1 source-tree sync.

## Result

**PASS** — both PASS criteria met.

1. **Hash stability**: screenshots `11.52.35.01.png` and `11.52.38.10.png` are visually identical. Every body-region hash colour on Lara matches across the 3-second delay between captures: green head + face, blue scarf, maroon torso, orange skirt, green legs/gloves. Background hash patches also pin in place.
2. **Cache fired**: `ffp_proxy.log` records
   ```
   LaraVB cache: first bind-pose snapshot committed
       nv=21845
       pc=1
       stride=24
     LaraVB cache hits=100      misses=2  entries=2
     LaraVB cache hits=1000     misses=2  entries=2
     LaraVB cache hits=10000    misses=2  entries=2
   ```
   2 unique submesh signatures captured, ≥10000 cache replays, zero subsequent misses.

## What Changed Between iter2 and iter3

`proxy/d3d9_device.c:3953–3958` and `patches/TombRaiderLegend/proxy/d3d9_device.c:3953–3958`.

```diff
                     int useLaraCache = (forceSkinnedNullVS
                         && self->laraClassBindPoseCacheEnabled
                         && self->streamVB[0]
                         && self->streamStride[0] > 0
-                        && nv > 0 && nv <= 16384);
+                        && nv > 0 && nv <= 65535);
```

Backup: `patches/TombRaiderLegend/backups/2026-05-19_1149_iter3-raise-nv-cap/`.

## Full Iteration Trace (082 → 084)

| Build | Variant | Result | Why |
|---|---|---|---|
| 082 (iter1) | as-shipped build 081 | FAIL | `TRL_ForceSkinnedNullVS` gate required FLOAT3+FLOAT4tex; main-menu draws use FLOAT3+FLOAT2tex; gate returned 0 for every draw, cache code never reached. |
| 083 (iter2) | variant 6 — widen gate to FLOAT3-only | FAIL | Gate canary fired (`MOVABLE forced null_vs: first occurrence`), but inner `useLaraCache` carried `nv <= 16384` guard. Main-menu FLOAT3 draws pass `nv=21845`, so cache code still skipped. |
| 084 (iter3) | raise nv cap 16384 → 65535 | **PASS** | Both gates now admit the menu draws. Cache captures 2 unique submesh signatures on first sight, replays them every subsequent frame. |

Pre-iter1 prerequisite (also required to make any iteration meaningful): source-tree sync. `run.py` builds from `patches/TombRaiderLegend/proxy/`, but build 081 (commit `dd5cb611`) only landed in canonical `proxy/`. Synced the canonical tree into the patches/ tree before iter1 — without that step, the build produces a `d3d9.dll` with no Lara symbols at all. Sync backup: `patches/TombRaiderLegend/backups/2026-05-19_1133_pre-build-081-sync/`.

## Screenshot Analysis

`11.52.35.01.png` vs `11.52.38.10.png`: pixel-level identical Lara — same colour patches at the same spatial locations on her body. Background menu hash colours also match. This is the inverse of the iter1 result, where every Lara body-region colour differed between shots.

## Proxy Log Summary

Configuration loaded (build 081 + iter2 + iter3 active):
```
Float3RoutingMode: auto
Float3Route effective: shader
SkinnedFloat3Route: null_vs   (Lara-class FLOAT3+FLOAT4tex forced to null-VS for stable asset hash)
NormalizeSkinnedDecl: 1
LaraClassBindPoseCache: 1
```

Note: the `SkinnedFloat3Route` log message still says "Lara-class FLOAT3+FLOAT4tex" even though iter2 widened the gate to all FLOAT3 — the help text in the loader still reflects build 081's narrower semantics. Cosmetic only; not blocking.

Cache pipeline behaviour during the run:
- 40 VB content fingerprint dumps (the diagnostic cap) all reported `csum=0x519E4D0B`, `x*1000=-3843`, `y*1000=-3080`, `z*1000=16000` — the menu VB content is **statically stable**, not CPU-skinned. The drift in iters 1–2 was Remix's hash being recomputed from differently-bound stream state per draw, not from changing vertex data.
- 2 distinct cache slots used. Hits ramped 100 → 1000 → 10000 with zero additional misses. The two slots map to the two distinct DIP signatures emitted on the menu (Lara model body + menu UI / title geometry).

## Engine State Sanity Checks

Memory patches applied at gameplay-scene latch (scene=1312) — all 36 culling-layer patches reported successful application. No crashes, no patch reverts. Sky isolation warmup completed normally. The build is also a confirmed clean cold-launch.

## Open Questions and Follow-Ups (Out of Scope for PASS)

1. **Gameplay validation is still required.** This PASS is main-menu only. The original cache target was in-level Lara (gameplay character decl FLOAT3+FLOAT4tex). The widened gate now also catches the menu FLOAT3 decl, which was incidental. A future test should verify gameplay Lara still gets stable hashes — the widened gate should still match her draws, but the FLOAT4 tex0 path inside `Lara_LookupCacheSlot` keys on `tex0` so different gameplay textures land in different slots and the cap of 64 may be tight. Re-evaluate at Peru.
2. **Texcoord-type help text drift.** `SkinnedFloat3Route: null_vs (Lara-class FLOAT3+FLOAT4tex ...)` log message no longer matches actual gate semantics after iter2. Update string to "FLOAT3 position (texcoord type ignored)".
3. **Source-tree dual-tree liability.** The `proxy/` ↔ `patches/TombRaiderLegend/proxy/` divergence silently bypassed build 081 entirely on the first iter1 attempt. Adding a `run.py` preflight that diffs the trees and refuses to build with stale patches/ would prevent the same trap. Filed informally — outside this session's scope.
