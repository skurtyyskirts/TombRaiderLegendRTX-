# build-083 — iter2 of build 081 cache+replay — FAIL (nv cap rejects main-menu draws)

**Date:** 2026-05-19 11:46 CDT
**Variant applied:** #6 from briefing — widen `TRL_ForceSkinnedNullVS` gate to accept any FLOAT3-position draw regardless of texcoord0 type.

## Result

**FAIL** — hashes still drift on Lara, cache still didn't fire (no `LaraVB cache hits=N` line in proxy log).

## What Changed

`TRL_ForceSkinnedNullVS` (`proxy/d3d9_device.c:1198–1209` and `patches/TombRaiderLegend/proxy/d3d9_device.c:1198–1209`).

Before:
```c
int isLaraClass = (self->curDeclPosType == D3DDECLTYPE_FLOAT3
                   && self->curDeclTexcoordType == D3DDECLTYPE_FLOAT4);
if (!isLaraClass) return 0;
```

After:
```c
int isFloat3 = (self->curDeclPosType == D3DDECLTYPE_FLOAT3);
if (!isFloat3) return 0;
```

Backup: `patches/TombRaiderLegend/backups/2026-05-19_1142_iter2-widen-gate/`.

## Screenshot Analysis

| Screenshot | Time | Lara appearance |
|---|---|---|
| 1 (`11.45.47.15`) | T+1.0s post UP/DOWN/UP | Lara more pastel/uniform than iter1 — torso green, top-strap gray, face dark with light highlights. Null-VS path is now active for her draws. |
| 2 (`11.45.50.23`) | T+4.0s | Vibrant rainbow per-body-region: head green, neck blue, torso red/pink, waist yellow. |

Drift remains visible. Lara's hash colors per body-region are different between shots — the cache replay never executed, so each frame still produces fresh per-draw hashes.

## Proxy Log Summary

Startup unchanged (build 081 config). Key new evidence:
```
line  57: MOVABLE forced null_vs: first occurrence (Lara-class FLOAT3+FLOAT4tex → bind-pose to Remix)
line  58+: 40 VBfp dumps (cap), all with vb=0x019277D0, nv=21845, stride=24,
           csum=0x519E4D0B, x*1000=-3843, y*1000=-3080, z*1000=16000
```

So the gate fires (line 57 canary), and the VB content fingerprint is **stable** across the first 40 draws — same VB pointer, same checksum, same first-vertex position. The draws are not CPU-skinned on this VB; they're plain static menu geometry with per-draw world matrix.

`LaraVB cache hits=` / `LaraVB cache: first bind-pose snapshot committed` / `LaraVB cache: capacity reached` — **none** appear in the log.

## Root Cause

`useLaraCache` (`proxy/d3d9_device.c:3948–3953`) gates on:
```c
useLaraCache = (forceSkinnedNullVS
                && self->laraClassBindPoseCacheEnabled
                && self->streamVB[0]
                && self->streamStride[0] > 0
                && nv > 0 && nv <= 16384);
```

VBfp dumps show every main-menu FLOAT3 draw uses `nv=21845` (game passes the whole-buffer NumVertices into DIP). 21845 > 16384, so `useLaraCache = 0` and the cache code is skipped.

The 16384 cap was a safety bound for snapshot allocation (16384 × 24 = 384 KiB per slot × 64 slots ≈ 24 MiB worst case). Bumping it to 65535 (uint16 NumVertices ceiling) grows that to ≈ 96 MiB worst case, still within budget.

## Next Build Plan

Iteration 3 = raise the `nv <= 16384` cap in `useLaraCache` to `nv <= 65535`. Single-byte literal change. After this, the cache should start capturing on the first matching draw, and the `LaraVB cache: first bind-pose snapshot committed` line should appear in the log near `MOVABLE forced null_vs: first occurrence`. If hits/misses telemetry then shows hits >> misses, and screenshots show Lara hashes pinned across the 3 s delay, that's PASS.

If iter3 produces hits but hashes still drift, drop to variant 1 (drop tex0 from cache key) — the menu draws all share tex0 already, but gameplay-tier Lara may not.

If iter3 produces 0 hits despite passing the gate and the cap, escalate to checking `streamVB[0]`/`streamStride[0]` paths — they may not be populated for the draws we care about.
