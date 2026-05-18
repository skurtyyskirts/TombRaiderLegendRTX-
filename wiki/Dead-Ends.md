# Dead Ends — Do Not Retry

> Every approach that was attempted, did not work, and consumed a measurable amount of project time. **Do not retry these without new evidence.** Each row records the build that proved the approach wrong, the specific failure mode, and the lesson.

This page is consulted before every patching attempt. The `hypothesis-tester` agent explicitly checks against this list before queuing new experiments.

## The full table

| # | Approach | Why it failed | Build |
|---|----------|--------------|-------|
| 1 | Re-parenting lights to largest mesh (`mesh_7DFF31ACB21B3988`) | Worse — large mesh not always drawn. Mesh size has no correlation with always-drawn status. | 042 |
| 2 | Aggressive 7-NOP set in `SceneTraversal_CullAndSubmit` | Crashed; build not preserved. The function's submission body has internal dependencies on at least some of these checks. | 043 |
| 3 | Assuming "red at distance" in builds 019-037 was the real red stage light | Was actually the **fallback light** — proven in build 038 by changing `fallbackLightRadiance` to neutral white and seeing both lights vanish. Wasted ~18 builds chasing the wrong target. | 019–037 |
| 4 | NOPing all 11 conditional exits in `SceneTraversal_CullAndSubmit` (`0x407150`) | Draw counts hit 190K but anchors still vanish at distance. Culling is upstream, not inside this function. | 040 |
| 5 | Config flag at `0x01075BE0` ("disable extra static light culling") | Found via config-table lookup at `0xF1325C` → string at `0xEFF384`. **No code xrefs to the global** — stamping has no effect. Config table is read but the value never reaches a code path that gates light collection. | 032 |
| 6 | Pending-render flag NOPs (`0x603832`, `0x60E30D`) | No effect on the bottleneck. Pending-render flags drive a different deferred path. | 025 |
| 7 | `LightVolume_UpdateVisibility` state NOPs (5 addresses) | Patches not confirmed in proxy log — silent `VirtualProtect` failure. Even if applied, the broader `Light_VisibilityTest` (`0x60B050`) gates these. | 026 |
| 8 | `D3DPOOL_MANAGED` + per-frame VB flush | Flush too aggressive (512 VB creates per frame); rendering came out uniform brown/amber. `D3DPOOL_MANAGED` itself is correct — the flush is what broke things. | 045 |
| 9 | Null VS for **ALL** draws (content fingerprint cache disabled the shader for every DIP) | Breaks view-space geometry — FLOAT3 view-space positions render at extreme scale because the game CPU has already pre-transformed FLOAT3 character meshes into view space before upload. Nulling the VS removes the only remaining transform (projection). Lara's face fills the entire screen. | 046 |
| 10 | Removing `positions` from `geometryAssetHashRuleString` | Catastrophic hash collision — all geometry gets the same hash. `indices,texcoords,geometrydescriptor` alone is not unique; many TRL meshes share index layouts and stride. **`positions` is required in the asset hash.** | 047 |
| 11 | Draw cache disabled (`DRAW_CACHE_ENABLED 0`) | No effect. The cache only replays 3 draws per scene; the "stale pointer" concern was unfounded. | 066 |
| 12 | Removing VP inverse epsilon threshold from `mat4_changed()` | No effect. VP changes between camera frames are large enough to always exceed the 1e-4 threshold in practice. Removing the threshold just makes the check redundant. | 067 |
| 13 | `RenderQueue_FrustumCull` bypass alone (`0x40C430` → `0x40C390`) | The bypass works mechanically (+29% draws, no crash) but stage lights are still absent. The blocker is not in this culling layer — it's the anchor hash mismatch downstream. | 072 |
| 14 | `user.conf` containing `rtx.enableReplacementAssets = False` | **The single most consequential discovery in the archive.** The in-game Remix menu wrote this override file to the game directory, silently disabling ALL mod content for **58 consecutive builds (016–074)**. `rtx.conf` had `True`, `user.conf` had `False`, `user.conf` wins. Fixed in build 075. The full story: [[Build-074-077-Asset-Pipeline]]. | 075 |
| 15 | CPU smooth normals in proxy (stream 1 FLOAT3 injection) | Built and compiled but did not work at runtime. Changes the geometry descriptor hash of every mesh (because the descriptor includes stream 1 layout), and there were likely conflicts with index-buffer lock timing and Remix's vertex capture. The proxy approach was abandoned in favor of `rtx.smoothNormalsTextures` (Remix-side, no proxy changes). | 075+ |

## Cross-cutting failure modes

Beyond the individual dead ends, three categories of failure recur:

### Build-path bugs

Edits to `proxy/d3d9_device.c` at the repo root are not compiled into the running DLL. `run.py` builds from `patches/TombRaiderLegend/proxy/`. Symptom: source change is correct but expected log lines never appear. Fixed in build 028; rediscovered in build 079 when the test harness deployed to the wrong game directory (a sibling `Tomb Raider Legend/` stub instead of `AlmightyBackups/.../Tomb Raider Legend/`).

**Rule:** Always run `cmp proxy/d3d9.dll patches/TombRaiderLegend/proxy/d3d9.dll` after a build. They should be byte-identical (the build script copies the canonical source's output to the repo root).

### Silent VirtualProtect failures

`VirtualProtect(PAGE_READWRITE)` can fail without raising — typically because a target page is not present in the binary's `.text` section or has been marked `EXECUTE_READ` by a prior call that wasn't released. Build 026 hit this on the `LightVolume_UpdateVisibility` NOPs.

**Rule:** A patch that doesn't appear in the `[PATCH OK]` log lines did NOT take effect. Don't trust source code as evidence of runtime behavior. Always check the proxy log.

### Macro / input delivery bugs

Builds 016, 019, 021 all logged as PASS but were later proven false-positive because the test harness's macro replay either:
- Wasn't reaching the game (`SendInput` without `KEYEVENTF_SCANCODE` ignored by DirectInput games — fixed build 018)
- Was selecting the wrong screenshots (camera-pan shots instead of post-movement shots — fixed build 020)
- Wasn't moving Lara at all because the game window had lost focus (fixed by `focus_hwnd()` re-call before HOLD tokens — also build 020)

**Rule:** A PASS that can't be reproduced by manual play is not a PASS. Visual confirmation of Lara's position in screenshots is mandatory. The 3-screenshot test must show Lara in three different positions, with lights shifting accordingly.

## Reframes (not dead ends — these were corrections of misconceptions)

| What we thought | What it actually was | Build it was reframed |
|---|---|---|
| The "red at distance" was the red stage light | It was the fallback light masquerading as red | 038 |
| `TerrainDrawable` (`0x40ACF0`) is the terrain culling logic | It's the **constructor**. Real terrain dispatch is at `0x40AE20`. Zero culling logic in the constructor. | 045–063 (terrain investigation) |
| The proxy patches `0x407150` with a RET at entry | It NOPs the 11 internal exits; entry byte is unchanged | 070 (corrected CLAUDE.md) |
| `useVertexCapture=True` would destabilize hashes | It produces the first **stable** world hashes (build 073) | 073 |
| The 8 building hashes in `mod.usda` were good | They're stale, captured under a previous Remix config | 075 |
| Cold launch was stable in all builds | It crashed at NRC initialization due to a DrawCache use-after-free, hidden because tests always used TR7.arg to skip the menu | 077 |

## See also

- [[Current-Status]] — what's open right now
- [[Build-History-Index]] — every build with one-line summary
- [[Build-074-077-Asset-Pipeline]] — the `user.conf` story (Dead End #14)
- [[Build-045-073-Hash-Pipeline]] — Dead Ends #8, #9, #10, #11, #12
- [[36-Layer-Culling-Map]] — Dead End #4 and #13 in their proper culling-layer context
