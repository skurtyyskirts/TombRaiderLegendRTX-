# Deep Research Queries — Run in claude.ai Research Mode

## Query 1: Hash Stability Across All Remix Mods
```
What techniques have RTX Remix compatibility mod developers used to achieve stable geometry hashes? Cover all known mods: FEAR RTX, BioShock RTX, SWAT4 RTX, NFS Carbon RTX, Guitar Hero 3 RTX, Black Mesa RTX, Half-Life 2 RTX, GTA IV RTX, and Unreal Engine 2 games via ue2fixes. For each: what was the hash instability cause and how was it solved? Include dxvk-remix config, proxy DLL code patterns, or engine patches.
```

## Query 2: cdcEngine Terrain Rendering
```
Research the Crystal Dynamics cdcEngine terrain rendering system used in Tomb Raider: Legend (2006), Anniversary (2007), and Underworld (2008). Focus on: how terrain is submitted as draw calls, terrain chunk culling, distance-based LOD for terrain, and any reverse engineering efforts documenting TerrainDrawable or TERRAIN_DrawUnits functions. Include information from TR modding communities, OpenTomb, decompilation projects, and any Ghidra/IDA analysis.
```

## Query 3: dxvk-remix Anti-Culling Internals
```
Analyze the NVIDIA dxvk-remix source code on GitHub (NVIDIAGameWorks/dxvk-remix). Focus on the anti-culling system: rtx.antiCulling.object.enable, how it extends mesh lifetime, what happens when the game engine never submits a draw call at all (vs stopping submission), and whether there's any mechanism to force-keep geometry that was seen once but stopped being submitted. Include recent 2025-2026 commits.
```