# Build 077 — DrawCache Use-After-Free Fix

**Date:** 2026-04-13  
**Result:** CRASH FIXED — game now launches and runs stably from cold start without TR7.arg  
**Commit:** `4ce784e fix: AddRef draw cache resources to prevent use-after-free crash at launch`

---

## Result

**FIXED.** The game was crashing at launch (within ~60–70 seconds of start) every time it was launched without a TR7.arg file. The crash has been identified, root-caused, and patched. The game now runs for 90+ seconds from cold menu start with 2,468 draw calls per scene, no crash.

This crash was never observed in automated testing (builds 001–076) because those tests used TR7.arg to start directly in the Peru level, bypassing the menu-to-level transition that triggered the bug.

---

## The Crash

### Symptoms

- Game launched via `NvRemixLauncher32.exe "trl.exe"` from the game directory
- Game appeared to start (proxy log confirmed DLL loaded, all patches applied)
- Game showed main menu rendering (12 draws/scene, all FLOAT3) for ~60–70 seconds
- Then crashed silently — no Windows Error Dialog, no `trl.exe` in task list
- Windows Event Log: `Faulting module: d3d9_remix.dll` at offset `0x001654dc`, exception `0xc0000409` (STATUS_STACK_BUFFER_OVERRUN / `__fastfail`)
- Bridge64 log: `"The client process has unexpectedly exited"` — the 32-bit bridge client (d3d9_remix.dll inside trl.exe) crashed, not the 64-bit server
- Last bridge commands before crash: `SetStreamSource → DrawIndexedPrimitive` (repeated pattern)
- Remix log: crash occurred right as **Neural Radiance Cache (NRC)** initialized for the first raytrace frame

### Timing

```
13:32:26  Game window appears, menu rendering begins (12 draws/scene)
13:32:28  Remix: "Trying to raytrace but not detecting a valid camera"
13:32:34  Remix: NRC v0.13 initialized, "Integrate Indirect Mode: Neural Radiance Cache - activated"
13:32:34  CRASH in d3d9_remix.dll — first raytrace DrawIndexedPrimitive
13:32:38  Bridge64 server detects client exited, shuts down
```

The crash coincided with the `DrawCache: replayed 12 culled draws` message in the proxy log — the first time the draw cache had to replay missing geometry. The 12 menu draws were absent from that frame (game transitioning), DrawCache replayed them with freed VB/IB pointers, and Remix crashed reading the freed vertex data.

---

## Root Cause

### DrawCache Use-After-Free

`DrawCache_Record()` stored raw, un-referenced COM pointers to:
- `vb` — game's vertex buffer
- `ib` — game's index buffer (GetIndices AddRefs it, but the code was immediately releasing that ref)
- `decl` — vertex declaration
- `tex0` — texture

When the game transitions from the main menu to a level:
1. Menu geometry (VBs, IBs, etc.) is freed by the game
2. DrawCache still holds the old raw pointers — they're now dangling
3. On the next Present, `DrawCache_Replay()` calls `SetStreamSource(freed_vb, ...)` then `DrawIndexedPrimitive()`
4. The Remix bridge client reads vertex data from the freed buffer → crash

### Why It Never Triggered Before

Automated tests (builds 001–076) used `TR7.arg` (chapter=4) to jump directly into the Peru level, bypassing the main menu entirely. The Peru level geometry stays loaded throughout the test — no resources are freed mid-session. The draw cache's dangling-pointer bug was always present but never exercised.

The **first manual launch** (without TR7.arg) hit the menu-to-level transition and immediately exposed the bug.

### GetIndices Bug (Secondary)

`DrawCache_Record` called `GetIndices`, which AddRefs the IB, then **immediately released** that extra ref:
```c
((FN_GetIndices)RealVtbl(self)[SLOT_GetIndices])(self->pReal, &ib);
if (ib) {
    /* GetIndices AddRefs, release the extra ref */
    ((FN_Rel)(*(void***)ib)[2])(ib);
}
```
This left the stored `c->ib` with no extra reference — the game's release could free it while the cache still held the pointer.

---

## Fix Applied

### Changes to `patches/TombRaiderLegend/proxy/d3d9_device.c`

**New helper: `DrawCache_ReleaseEntry()`**  
Releases COM refs (vb, ib, decl, tex0) for a single cache slot and marks it inactive.

**New function: `DrawCache_Clear()`**  
Iterates all active entries, calls `DrawCache_ReleaseEntry` on each, zeros the count and log-once flag. Replaces raw `s_drawCacheCount = 0` assignments at all 3 sites.

**`DrawCache_Record()` — AddRef resources**  
- `c->vb`: `com_addref(c->vb)` after storing
- `c->ib`: **removed** the immediate Release after `GetIndices` — that AddRef is now our cache reference
- `c->decl`: `com_addref(c->decl)` after storing
- `c->tex0`: `com_addref(c->tex0)` after storing
- When updating an existing slot: `DrawCache_ReleaseEntry(c)` before overwriting
- When cache is full: release the GetIndices-AddRef'd ib before returning

**`DrawCache_Replay()` — release on stale eviction**  
Stale entries (not seen in 120+ frames) now call `DrawCache_ReleaseEntry(c)` instead of just `c->active = 0`.

**`WD_Release` / `WD_Reset` / transition flush**  
All three `s_drawCacheCount = 0` assignments replaced with `#if DRAW_CACHE_ENABLED / DrawCache_Clear() / #endif`.

---

## Current Game Directory State

All files in `Tomb Raider Legend/` at time of investigation:

| File | Size | Modified | Purpose |
|------|------|----------|---------|
| `d3d9.dll` | 43,520 | 2026-04-13 13:46 | **FFP proxy (build 077, current)** |
| `d3d9_remix.dll` | 2,076,160 | 2026-03-21 | RTX Remix bridge client (32-bit) |
| `dxwrapper.dll` | 7,975,936 | 2026-03-24 | DXWrapper — loads d3d9.dll in chain |
| `NvRemixLauncher32.exe` | 138,240 | 2026-03-24 | Remix launcher (spawns trl.exe) |
| `trl.exe` | 13,668,352 | 2026-03-25 | Game executable |
| `proxy.ini` | 957 | 2026-03-27 | FFP proxy config — chains to d3d9_remix.dll |
| `rtx.conf` | 2,821 | 2026-04-13 | RTX Remix config (authoritative, see below) |
| `user.conf` | 2,120 | 2026-04-13 | Remix user overrides (written by Remix menu) |
| `remix-comp-proxy.ini` | 2,355 | 2026-04-02 | Legacy proxy config (not read by current d3d9.dll) |
| `d3d9_ffp.dll` | 160,768 | 2026-04-03 | Old proxy build backup |
| `d3d9_proxy_backup.dll` | 160,768 | 2026-04-03 | Old proxy build backup |
| `d3d9_new.dll` | 28,672 | 2026-04-07 | Experimental build backup |
| `d3d9_trace.dll` | 160,768 | 2026-03-26 | dx9tracer DLL (swap in for frame capture) |
| `d3d9.dll.old_c_proxy.bak` | 23,552 | 2026-04-02 | Very old proxy build backup |
| `dxvk.conf` | 14,231 | 2026-03-29 | DXVK settings |
| `trl.dxvk-cache` | 1,795,280 | 2026-04-13 | Vulkan shader cache |
| `imgui.ini` | 191 | 2026-03-27 | Remix ImGui window positions |
| `nrc_session_log.txt` | 0 | 2026-04-13 | NRC session log (empty after crash) |
| `metrics.txt` | 0 | 2026-04-13 | NRC metrics (empty after crash) |
| `dxtrace_frame.jsonl` | 80,696 | 2026-04-06 | Last dx9tracer capture |
| `title.bik` | 468 | 2026-04-03 | Blank title video (stub to skip Crystal logo) |
| `nvidia.bik` | 468 | 2026-04-03 | Blank NVIDIA video (stub to skip intro) |
| `trl_dump_SCY.exe` | 18,299,392 | 2026-03-27 | Ghidra analysis target |
| `bigfile.000–.050` | ~150MB each | 2026-03-19 | Game data archives |

### DLL Chain (as loaded)
```
NvRemixLauncher32.exe
  └→ trl.exe (game process, 32-bit)
       └→ dxwrapper.dll  [d3d9 stub, routes to next in chain]
            └→ d3d9.dll  [FFP proxy — this project]
                 └→ d3d9_remix.dll  [RTX Remix bridge client, 32-bit]
                      └→ [bridge to 64-bit Remix server process]
```

### Active proxy.ini
```ini
[Remix]
Enabled=1
DLLName=d3d9_remix.dll

[Chain]
PreLoad=

[FFP]
AlbedoStage=0
```

### Active rtx.conf (key settings)
```ini
rtx.fusedWorldViewMode = 0          # proxy provides W/V/P via SetTransform
rtx.useWorldMatricesForShaders = True
rtx.zUp = True
rtx.sceneScale = 0.0001
rtx.useVertexCapture = False        # CPU-side SHORT4→FLOAT3 expansion, no VS capture
rtx.enableRaytracing = True
rtx.enableReplacementAssets = True  # was False in user.conf pre-build-075
rtx.geometryAssetHashRuleString = positions,indices,texcoords,geometrydescriptor
rtx.geometryGenerationHashRuleString = positions,indices,texcoords,geometrydescriptor,vertexlayout,vertexshader
rtx.calculateAxisAlignedBoundingBox = True
rtx.antiCulling.object.enable = False  # causes freeze with TRL
rtx.antiCulling.light.enable = False
rtx.qualityDLSS = 2
```

### user.conf (Remix-written, overrides rtx.conf)
Key active overrides from Remix menu settings:
- `rtx.enableReplacementAssets = True` — previously `False` (fixed build 075)
- `rtx.integrateIndirectMode = 2` — NRC mode
- `rtx.neuralRadianceCache.qualityPreset = 2`
- `rtx.graphicsPreset = 4`
- `rtx.dlssPreset = 2`
- `rtx.renderPassGBufferRaytraceMode = 0` — Ray Query (CS)

---

## Active Patches in Current Proxy (Build 077)

All confirmed applied in proxy log from this session:

| Patch | Address(es) | Method | Confirmed in Log |
|-------|-------------|--------|-----------------|
| Frustum threshold | 0xEFDD64 | Stamp -1e30f per BeginScene | Yes — matrix verification shows correct |
| Cull jumps (11x) | 0x407150 entry + 7 scene traversal + 4 exits | NOP | "NOPed cull jumps: 11/11" |
| Null-check trampoline | 0x4071D9 / cave at 0xEDF9E3 | Trampoline | "Patched 0x4071D9" |
| ProcessPendingRemovals | 0xEE88AD | Skip field_48 deref | "Patched ProcessPendingRemovals" |
| Sector visibility | 0x46C194, 0x46C19D | NOP | "NOPed sector visibility checks: 2/2" |
| Camera proximity filter | 0x46B85A | NOP | "NOPed sector-object camera proximity filter" |
| Sector already-rendered | 0x46B7F2 | NOP | "NOPed sector already-rendered skip" |
| Frustum screen-size rejection | 0x46C242, 0x46C25B | NOP | "NOPed frustum screen-size rejection" |
| SectorPortalVisibility resets | 4 sites | NOP | "NOPed SectorPortalVisibility reset writes: 4/4" |
| Cull mode globals | 0xF2A0D4/D8/DC | Stamp D3DCULL_NONE | "Patched cull mode globals to D3DCULL_NONE" |
| Light_VisibilityTest | 0x60B050 | `mov al,1; ret 4` | "Patched Light_VisibilityTest to always TRUE" |
| Sector light count gate | 0xEC6337 | NOP | "NOPed sector light count gate" |
| RenderLights gate | 0x60E3B1 | NOP | "NOPed RenderLights gate" |
| Terrain cull gate | 0x40AE3E | NOP | "NOPed terrain cull gate" |
| MeshSubmit_VisibilityGate | 0x454AB0 | `xor eax,eax; ret` | "Patched MeshSubmit_VisibilityGate" |
| Post-sector enable flag | 0xF12016 | Stamp 1 | "Stamped post-sector enable flag to 1" |
| Stream unload gate | 0x415C51 | NOP | "NOPed stream unload gate write" |
| Post-sector/stream gate | 0x10024E8 | Clear | "Cleared post-sector/stream gate" |
| Post-sector bitmask cull | 0x40E30F | NOP | "NOPed post-sector bitmask cull" |
| Post-sector distance cull | 0x40E3B0 | NOP | "NOPed post-sector distance cull" |
| Post-sector bitmask | — | Stamp 0xFFFFFFFF | "Stamped post-sector bitmask" |
| Sector_SubmitObject gates | 0x40C666, 0x40C68B | NOP | "NOPed Sector_SubmitObject gates: 2/2" |
| Mesh eviction | SectorEviction x2 + ObjectTracker_Evict | NOP | "NOPed mesh eviction" |
| Level writers | 0x46CCB4, 0x4E6DFA | NOP | "NOPed _level writers: 2/2" |
| RenderQueue_FrustumCull | 0x40C430 → 0x40C390 | JMP redirect | "Patched RenderQueue_FrustumCull -> NoCull" |
| Data pages | 8 pages | VirtualProtect permanent unlock | "Permanently unlocked data pages: 8/8" |

---

## Verified Game State (Build 077 Run)

From proxy log after fix:
- **Scene 3342** observed in log (50s+ of gameplay)
- **2,468 draw calls per scene** — full Peru level geometry submitting
- **VS registers written per scene:** c0–c3 (WVP), c8–c15 (View+Proj), c28 (additional)
- No crash for 90 seconds of monitoring

---

## What This Session Did NOT Change

- No changes to `rtx.conf` or `user.conf`
- No changes to patch addresses or patch logic
- No changes to the SHORT4→FLOAT3 expansion path
- No changes to PinnedDraw system
- No changes to the S4 VB cache (S4_FlushVBCache from previous commit is intact)
- Only `DrawCache_Record`, `DrawCache_Replay`, `WD_Release`, `WD_Reset`, and the viewProjValid transition flush changed

---

## How the Crash Was Diagnosed

1. Launched game with NvRemixLauncher32 and monitored proxy log — observed empty log initially
2. Discovered NvRemixLauncher requires full path to trl.exe argument (relative path fails silently)
3. After correct launch: proxy log showed normal initialization, 12 FLOAT3 draws/scene (main menu)
4. After ~60s, game crashed. Windows Event Log: crash in `d3d9_remix.dll` offset `0x001654dc`, code `0xc0000409`
5. Bridge64 log: "client process unexpectedly exited" — last commands were SetStreamSource + DrawIndexedPrimitive
6. Remix log: crash happened at the exact frame NRC activated (first raytrace frame)
7. Proxy log: last entry was `"DrawCache: replayed 12 culled draws"` — first time draw cache replayed
8. Conclusion: draw cache held dangling pointers to freed menu geometry; first raytrace frame tried to use them

---

## Open Issues

1. **Stage lights still absent** — anchor hashes in `mod.usda` are stale (same issue as build 075). Now that the game survives a manual cold launch, a fresh Remix capture can actually be taken.
2. **`NvRemixLauncher32` relative-path behavior** — passing just `trl.exe` (relative) causes the launcher to silently fail to spawn the game. Must pass full path. The `run.py` script already does this correctly (`str(GAME_EXE)` = full path). Only affects manual launches.
3. **Stale backup DLLs in game directory** — several old proxy builds exist as `d3d9_ffp.dll`, `d3d9_proxy_backup.dll`, `d3d9_new.dll`. These are inert (not in the load chain) but could cause confusion.

---

## Next Step

Fresh Remix capture to update `mod.usda` with current mesh hashes. The geometry IS rendering (2,468+ draws/scene). The only thing standing between current state and visible stage lights is matching the hash IDs.
