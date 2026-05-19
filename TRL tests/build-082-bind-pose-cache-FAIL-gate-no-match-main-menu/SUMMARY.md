# Build 082 — bind-pose VB cache verification at main menu — FAIL (gate no-match)

**Date:** 2026-05-19 10:11 CT
**Test driver:** `python patches/TombRaiderLegend/run_iter_wrapper.py test-hash --build --main-menu`
**Source commit:** dd5cb61 (build 081 untested) + sync root proxy → patches proxy
**Variant:** as-shipped build 081 (iteration 1 of bind-pose cache verification cycle)

## Result

**FAIL — gate-no-match-main-menu**

Lara hash colors drift between shot 1 (10:11:35) and shot 2 (10:11:38). Cache
never activated: `LaraVB cache hits=N` telemetry absent at every threshold
(100/1000/10000) in `ffp_proxy.log`. No crash.

## What Changed This Build

- Synced `proxy/d3d9_device.c` (root, 247534 B, dd5cb61 cache code) over
  `patches/TombRaiderLegend/proxy/d3d9_device.c` (was 230671 B, pre-cache).
- Synced `proxy/proxy.ini` (root, with `SkinnedFloat3Route=null_vs` +
  `LaraClassBindPoseCache=1`) over the patches copy.
- Added `launch_game_to_main_menu`, `main_menu_capture_lara_hashes`,
  `do_main_menu_hash_capture` to `patches/TombRaiderLegend/run.py`. Wired
  `--main-menu` flag onto the `test-hash` subcommand.

## Screenshot Analysis

| Region | Shot 1 (10:11:35) | Shot 2 (10:11:38) | Stable? |
|--------|-------------------|-------------------|---------|
| Lara face | green | green | yes |
| Lara shoulder/upper arm | orange / pink | blue / dark | **NO** |
| Lara torso | mixed orange + magenta | dominated blue/purple | **NO** |
| Lara lower body | bright magenta | lighter pink/purple | **NO** |
| Background pyramid panels | green/magenta/orange | green/magenta/orange | yes |
| Menu text/UI overlay | unchanged | unchanged | yes (UI not hashed) |

Lara's face hash is stable across both shots (single submesh, possibly already
caught by an earlier code path). The torso/arm/lower-body submeshes drift
between frames — the cache is not snapshotting them.

## Proxy Log Summary (cache hits/misses)

```
LaraClassBindPoseCache: 1
SkinnedFloat3Route: null_vs (Lara-class FLOAT3+FLOAT4tex forced to null-VS for stable asset hash)
Float3Route effective: shader
```

- INI flags read correctly.
- **Zero `LaraVB cache` lines** at any verbosity. No 100/1000/10000 hit
  telemetry. No fingerprint dumps. The gate (`TRL_ForceSkinnedNullVS`) never
  returns true for any draw at the main menu.

### Decl observed at the menu

```
DECL seen=0x0187CBC8
  numElems=4
  hasBW=0  hasBI=0  posType=2 (FLOAT3)
  s0 off=0   type=2 (FLOAT3)   usage=0  (POSITION)
  s0 off=12  type=4 (D3DCOLOR) usage=10 (COLOR)
  s0 off=16  type=1 (FLOAT2)   usage=5  (TEXCOORD0)
```

Menu Lara decl is **POSITION FLOAT3 + COLOR + TEXCOORD0 FLOAT2** (Decl A in
the comments). The cache gate at `d3d9_device.c:1199` requires `FLOAT3 +
TEXCOORD0 FLOAT4` (Decl C — gameplay character/movable signature). Decl A
fails the gate, so the null-VS route and bind-pose snapshot path never run.

The proxy author's comment at line 1191 explicitly states:
> Menu/UI FLOAT3 (Decl A): TEXCOORD0 FLOAT2 — left on shader route

i.e. menu Lara was *intentionally* left on the shader route. That decision
means the cache cannot stabilize hashes at the main menu by design. The
plan's PASS criterion (cache hits > 0 + stable hashes during main-menu
capture) is therefore unreachable with the as-shipped gate.

## Root Cause

Wrong gate condition for the test scope. The cache works only on Decl C
(gameplay character/movable). Variants 2–6 in the iteration plan (key
adjustments, lock-after-N, snapshot region size, capacity bump,
disable-cache-only A/B) all assume the gate fires; they would be no-ops at
the main menu against this decl. Variant 7 (widen gate) is the only one
that can produce a non-zero hit count here.

## Next Build Plan

**Build 083 — variant 7, widen Lara-class gate.** Drop the `FLOAT4
texcoord0` requirement from `TRL_ForceSkinnedNullVS`. New condition:
`posType==FLOAT3 && curDeclHasColor`. The COLOR component is present on
both Decl A (menu Lara) and Decl C (gameplay Lara) and absent on plain
HUD/UI FLOAT3 draws, so it keeps the gate selective.

Expected result on PASS: shot 1 and shot 2 show identical hash patches on
Lara's torso/arms/legs; `ffp_proxy.log` shows `LaraVB cache hits=` lines.

## Open Hypotheses

1. Widening the gate to `FLOAT3 + COLOR` may pull in additional menu UI
   meshes (e.g. animated logo, level-select cube) that share that signature.
   If post-widen the cache fingerprint cap fills with non-Lara meshes, fall
   back to also requiring `numElems >= 4` or `curDeclHasMorph`.
2. The cache key `(nv, pc, tex0, bvi, mi)` may collide across submeshes if
   `tex0` is null/uniform at the menu. If hashes still drift after the gate
   widens, drop `tex0` from the key (variant 2 from the plan).
