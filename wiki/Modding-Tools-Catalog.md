# Tomb Raider Legend — Modding & Reverse Engineering Tool Arsenal

Comprehensive catalog of every known tool, resource, and community project for reverse engineering Tomb Raider Legend (2006, Crystal Dynamics, PC). Organized by category with links and relevance notes.

Last updated: 2026-03-27

---

## Table of Contents

- [Engine Decompilation & Source RE](#engine-decompilation--source-re)
- [Archive Extraction (Bigfile / DRM)](#archive-extraction-bigfile--drm)
- [Runtime Mod Loading & Debugging](#runtime-mod-loading--debugging)
- [3D Model Tools](#3d-model-tools)
- [Texture Tools](#texture-tools)
- [Audio Tools](#audio-tools)
- [Binary Format Documentation](#binary-format-documentation)
- [File Format Specs (cdcengine.re)](#file-format-specs-cdcenginere)
- [Memory Addresses & Cheat Engine](#memory-addresses--cheat-engine)
- [Graphics Mods & D3D9 Hooking](#graphics-mods--d3d9-hooking)
- [Translation & Localization](#translation--localization)
- [Open Engine Reimplementations](#open-engine-reimplementations)
- [Community Hubs](#community-hubs)
- [Quick Reference Table](#quick-reference-table)

---

## Engine Decompilation & Source RE

### cdcEngine Decompilation
- **Link:** https://github.com/TheIndra55/cdcEngine
- **What:** Partial decompilation of `trl.exe` / the CDC engine as compilable C++. The single highest-value resource for understanding TRL internals.
- **Key recovered structures:**
  - `PCModelData` (112 bytes) — mesh file header: bounding volumes, prim group/batch/bone/material offsets and counts, model type enum (Rigid, Skinned, Vmo, Bend, Dummy)
  - `PCModelBatch` (172 bytes) — `D3DVERTEXELEMENT9 vertexElements[16]`, vertex buffer pointer, format/stride/counts, skin map
  - `PCPrimGroup` (20 bytes) — baseIndex, primitiveCount, vertexCount, shaderFlags, materialIndex
  - `PCRenderDevice` — D3D9 device wrapper with `BeginFrame/EndFrame`, `BeginScene/EndScene`. Static singleton: `RenderDevice::s_pcInstance`
  - `SceneLayer` — `SceneLayer::s_enabled` (bool), `SceneLayer::s_pGlobalScene` (cdc::IScene*), `SceneLayer::Render(cdc::ISceneCell* pStartCell)` — scene traversal entry
  - `InstanceDrawable` — `Draw(cdc::Matrix* localToWorld, bool isMoved)` — per-instance draw call
  - `Instance/BaseInstance` — game object node with linked list, Object pointer, currentRenderModel
  - Terrain structs: `TerrainRenderVertex`, `OctreeSphere`, `TERRAIN_DrawUnits()`
- **Source layout:** `tr7/cdc/runtime/cdcRender/pc/` (rendering), `tr7/cdc/runtime/cdcScene/Source/` (scene), `tr7/game/Scene/` (game layer)
- **Status:** Active community project

### cdcResearch
- **Link:** https://github.com/TheIndra55/cdcResearch
- **What:** Research notes on CDC engine internals, companion to cdcEngine decompilation
- **Status:** Reference material

---

## Archive Extraction (Bigfile / DRM)

### Gibbed.TombRaider
- **Link:** https://github.com/LazyBui/gibbedtr (maintained fork) / https://github.com/sephiroth99/gibbedtr (original)
- **What:** Unpacks and repacks `bigfile.000/.001/...` archives. TRL = "Tomb Raider 7" / version 1 bigfiles. Includes `DRMEdit` for DRM manipulation and `RebuildFileLists` for manifest regeneration.
- **Supported games:** TRL, TRA, TRU
- **Status:** Abandoned (last commits ~2021), still the primary bigfile tool

### cdcEngineTools
- **Link:** https://github.com/Gh0stBlade/cdcEngineTools (original) / https://github.com/TombRaiderModding/cdcEngineTools (fork, updated July 2024)
- **What:** Four extraction tools:
  - `DRM.EXE` — extract/decompress DRM sections (drag-drop `.drm` onto it for TRL)
  - `CDRM.EXE` — decompress CDRM blocks (TRU only, skip for TRL)
  - `PCD2DDS` — convert `.pcd` textures to `.dds`
  - `WAVE2WAV` — audio conversion
- **Pipeline for TRL:** bigfile unpack (Gibbed) → drag `.drm` onto `DRM.EXE` → `PCD2DDS` on extracted textures
- **Status:** Original abandoned (2017). Fork updated 2024.

### BigfileLogger
- **Link:** https://github.com/TombRaiderModding/BigfileLogger
- **What:** Runtime DLL that logs all bigfile read requests — shows which DRM files the game loads and when. Useful for correlating asset hashes to DRM filenames.
- **Status:** Active (updated March 2026)

### Yura (Bigfile Browser)
- **Link:** https://github.com/TheIndra55/Yura
- **What:** C# bigfile browser for CDC engine games (TRL through Shadow of the Tomb Raider). Maps CRC-32 hashes to filenames via external file lists. Supports image preview and deep search.
- **Status:** Active

### GraveRobber
- **What:** Older DRM/texture extractor (RAW→TGA, TRLMODEL→textured OBJ). Predates cdcEngineTools.
- **Forum:** https://www.tombraiderforums.com/showthread.php?t=180339
- **Status:** Abandoned. Links may be dead. Superseded by cdcEngineTools + Noesis plugins.

---

## Runtime Mod Loading & Debugging

### TRLAU-Menu-Hook
- **Link:** https://github.com/TheIndra55/TRLAU-menu-hook
- **What:** The most feature-rich RE/debugging tool for TRL/TRA/TRU. DLL injection with ImGui debug overlay. Features:
  - Free camera, FOV slider
  - Instance viewer and spawner
  - Level selector
  - Draw group toggle (per-model render flag manipulation)
  - Collision/portal/markup/signal/trigger mesh visualization
  - Wireframe rendering mode
  - Material parameter editor (live constant patching)
  - Script debugger (TRU)
  - Outfit switcher
  - Restored hidden debug features from dev builds
  - `patches.ini` for scriptable memory patches
  - DEP crash fix, intro skip, letterbox disable
- **Status:** Active (v2.5, February 2026). 103 stars. C++/ImGui.
- **RE value:** Contains significant reversed game internals. Source code has exact addresses for TRL render state globals. The portal/render toggle features likely reference globals near `0xF2A0D4/D8` (our known culling globals).

### ModLoader
- **Link:** https://github.com/TombRaiderModding/ModLoader
- **What:** DLL that intercepts bigfile loading — loose files in `mods/` folder override game assets without repacking. Useful for iterating on asset replacements.
- **Supported games:** TRL, TRA, TRU, LCGoL
- **Status:** Active (v1.2, July 2024). GPL-3.0, C++.

---

## 3D Model Tools

### ModelEx
- **Link:** https://github.com/TheSerioliOfNosgoth/ModelEx
- **What:** Windows app for viewing and exporting 3D models from TRL/TRA. Renders full scenes with object placement, exports to Collada (`.dae`). Reads `.drm` and `.pcm` files. Also supports Legacy of Kain series.
- **Status:** Active (v6.1, January 2025). 315 commits. C#.
- **RE value:** C# source contains full struct parsers for `PCModelData`, `PCPrimGroup`, `PCModelBatch` — most complete existing documentation of TRL mesh binary format.

### TRLAU Noesis Plugin ("Raq's Plugin")
- **Link:** https://tombraidermodding.com/tool/trulau-mod-tools
- **What:** Noesis plugin for importing/exporting models and textures across TRL, TRA, TRU, LCGoL, Temple of Osiris. Handles `.drm`, `.tr7aemesh`, `.tr8mesh`, `.pcd`, `.raw` formats.
- **Requires:** [Noesis](https://richwhitehouse.com/index.php?content=inc_projects.php&showproject=91) (free model viewer/converter)
- **Status:** Active (v1.2, September 2024)

### TR7AE-Mesh-Exporter
- **Link:** https://github.com/Raq1/TR7AE-Mesh-Exporter
- **What:** Noesis plugin for exporting custom models BACK INTO the TR7AE mesh format (import to game). Handles `.drm`, `.gnc`, `.pcd`, `.raw`. Python source (`fmt_tr7ae.py`) contains parsers that cross-validate `PCModelBatch` vertex element layout.
- **Limits:** Max 21,850 vertices total; 10,922 polygons per mesh; no Next Gen model support; no level geometry editing
- **Status:** Complete/static

### TR7AE Level Viewer
- **Link:** https://github.com/TheIndra55/TR7AE-level-viewer
- **What:** Browser-based (Three.js/TypeScript) 3D viewer for TRL/TRA level geometry. Reads `.drm` files directly. Visualization/research only, not an editor.
- **Status:** WIP (51 commits, last updated May 2024)

---

## Texture Tools

### TexMod
- **What:** Classic D3D texture-intercept tool. Hooks D3D at runtime to enumerate, extract, and replace textures by hash without touching game files.
- **Tutorial for TRL:** https://www.tombraiderhub.com/tr7/modding/texmod/tutorial.html
- **Status:** Abandoned (2009-era). Still works on TRL, may have issues on modern Windows.

### TexModAutomator
- **Link:** https://www.nexusmods.com/tombraiderlegend/mods/92
- **What:** Automation wrapper for TexMod to simplify batch texture replacement for TRL.

### PCD2DDS
- Part of cdcEngineTools (see above). Converts `.pcd` (TRL internal texture format) to `.dds`.

---

## Audio Tools

### MulDeMu
- **Link:** https://github.com/sephiroth99/MulDeMu
- **What:** Demultiplexes TRL/TRA/TRU `.mul` files (combined audio+cinematic container) into PCM WAVE. Can also extract raw cinematic data.
- **Status:** Abandoned (last commit 2016). Public domain. Still functional.

### foo_tr7ae (foobar2000 plugin)
- **Link:** https://github.com/TombRaiderModding/foo_tr7ae
- **What:** foobar2000 component for playing TRL/TRA/TRU audio formats directly.
- **Status:** Active (updated November 2024)

---

## Binary Format Documentation

### 010 Editor Templates
- **Link:** https://github.com/TombRaiderModding/Templates
- **What:** Binary templates for inspecting TRL/TRA/TRU file formats in 010 Editor:
  - `tr7ae_object.bt` — game object binary layout
  - `tr7ae_section.bt` — DRM section structures
  - `tr7ae_raw.bt` — raw data format
  - `tr7ae_mul.bt` — .mul container format
  - `tr7ae_fxanim.bt` — effects animations
  - `tr7ae_sound.bt` — audio data
  - `tr7ae_wave.bt` — waveform data
  - `tr8_cdrm.bt` — TRU CDRM format
  - `tr8_section.bt` — TRU section format
- **Status:** Active (updated April 2025). GPL-3.0.
- **RE value:** `tr7ae_section.bt` documents the in-memory DRM section layout that maps directly to what the renderer submits.

---

## File Format Specs (cdcengine.re)

### CDC Engine Documentation Site
- **Link:** https://cdcengine.re/docs/
- **What:** Community-maintained documentation site covering CDC engine formats and tools.
- **Key pages:**
  - `/files/bigfile/` — full bigfile format spec
  - `/files/drm/` — full DRM format spec (see below)
  - `/files/image/` — texture format (PCD)
  - `/files/multiplexstream/` — .mul audio/video container
  - `/files/objectlist/`, `/files/idmap/`, `/files/schemafile/`
  - `/engine/event/`, `/engine/filesystem/`, `/engine/messaging/`
  - `/tools/gibbed/`, `/tools/modelex/`, `/tools/noesis/`, `/tools/yura/`

#### DRM Section Format (from cdcengine.re):
```c
struct SectionList {
    int32_t version;       // 14 = TRL
    int32_t numSections;
    SectionInfo sections[numSections];
};

struct SectionInfo {
    int32_t  size;
    uint8_t  sectionType;  // 0=General, 1=Empty, 2=Animation, 3-4=Pushbuffer,
                           // 5=Texture, 6=Wave, 7=DTPData, 8=Script, 9=ShaderLib
    uint8_t  pad;
    uint16_t versionID;
    uint32_t packedData;   // numRelocations = packedData >> 8
    uint32_t id;           // asset hash (CRC-32)
    uint32_t specMask;
};

struct Relocation {
    int16_t type:3;
    int16_t sectionIndexOrType:13;
    int16_t typeSpecific;
    uint32_t offset;
};
```

#### Bigfile Format (from cdcengine.re):
```c
struct ArchiveFile {
    uint32_t numRecords;
    uint32_t hashes[numRecords];          // CRC-32 poly 0x4C11DB7
    struct {
        uint32_t size;
        uint32_t offset;                  // position = offset << 11 (2048-byte sectors)
        uint32_t specMask;
        int32_t  compressedLength;        // >0 = zlib compressed
    } records[numRecords];
};
// TRL/TRA multi-archive split alignment: 0x9600000
```

---

## Memory Addresses & Cheat Engine

### Speedrunner Autosplitter (Confirmed Addresses)
- **Link:** https://github.com/FluxMonkii/Autosplitters
- **What:** LiveSplit autosplitter for TRL with confirmed static addresses for two executable variants:

| Variable | `trl.exe` | `tr7.exe` | Type | Meaning |
|----------|-----------|-----------|------|---------|
| `loading` | `0xCC0A54` | `0xCC80D4` | bool | Loading screen active |
| `level` | `0xD59318` | `0xD60C18` | string12 | Current level name |
| `bosshealth` | `0xB14A40` | `0xB1C0B0` | float | Active boss HP |

Non-ASLR 32-bit addresses, directly usable with retools `datarefs.py` or `readmem.py`.

### FearlessRevolution CE Table (v1.2 Steam)
- **Link:** https://fearlessrevolution.com/viewtopic.php?t=14926
- **What:** Cheat Engine table with documented addresses for:
  - Player health (float), enemy health, 1-hit kill
  - Ammo, no-reload, medipack count, grenade count
  - Oxygen level, lamp fuel
  - Motorcycle health
  - XYZ teleport coordinates (floats)
  - Active suit/costume index
- **Forum thread with disassembly screenshots:** https://www.cheatengine.org/forum/viewtopic.php?t=112614

### Speedrun Technical Mechanics
- **Link:** https://www.speedrun.com/trl
- **Documented exploits:** Bug Jump (geometry/collision glitch), framerate-locked tricks (frame-dependent physics confirmed), Airwalking (persistent mid-air state), reverse grapple on slopes, grenade alignment via floor crack detection

---

## Graphics Mods & D3D9 Hooking

### Helix Mod (Stereo3D Shader Fix)
- **Link:** https://helixmod.blogspot.com/2012/05/tomb-raider-legend.html
- **What:** `d3d9.dll` drop-in with `ShaderOverride/` folder. Modifies constant register `c220` (UI convergence offset).
- **RE value:** Confirms shader hash `E32A6B30` is a valid TRL DX9 shader identifier (UI shader). Uses `dx9settings.ini` for key bindings. Requires "Next Gen" DX9 mode.

### Nexus Mods — Care Package (dgVoodoo + ReShade)
- **Link:** https://www.nexusmods.com/tombraiderlegend/mods/3
- **What:** Custom `d3d9.dll` (dgVoodoo) with ReShade chained. Ambient occlusion, sharpening, relief shaders.
- **RE value:** Proves the game accepts a chained `d3d9.dll` without issues.

### PCGW Technical Page
- **Link:** https://www.pcgamingwiki.com/wiki/Tomb_Raider:_Legend
- **What:** Executable versions, "Next Gen" DX9 SM2.0 mode toggle, known crash locations in DX9 mode.

---

## Translation & Localization

### Translatr
- **Link:** https://github.com/sephiroth99/translatr
- **What:** Extracts all localized text/subtitle strings from bigfile/patch data to editable `translations.xml`, then repacks. Supports TRL, TRA, TRU, LCGoL. 13 languages.
- **Status:** Abandoned (~2014). C#, public domain.

---

## Save File Documentation

### Stella's Tomb Raider Site
- **Link:** https://tombraiders.net/stella/savegame/TR7saves.html
- **What:** Save file format docs. `.dat` files in `Documents\Eidos\Tomb Raider - Legend\Saved Games\`. Filename encodes elapsed time, level name, completion %. Contains inventory, kill counts, reward state.

---

## Open Engine Reimplementations

**None cover TRL.** These projects target the older Core Design engine (TR1-TR5):

- **OpenTomb** — https://github.com/opentomb/OpenTomb — TR1-TR5 only
- **OpenLara** — https://github.com/XProger/OpenLara — TR1 only

The Crystal Dynamics CDC engine used by TRL/TRA/TRU has no open-source reimplementation.

---

## RTX Remix Status

No published RTX Remix mod exists for Tomb Raider Legend. All existing TR RTX work targets TR1 via OpenLara. The `skurtyyskirts/TombRaiderLegendRTX-` project is the only known active TRL RTX Remix effort.

---

## Community Hubs

| Hub | Link | Notes |
|-----|------|-------|
| TombRaiderModding (GitHub org) | https://github.com/TombRaiderModding | Templates, ModLoader, BigfileLogger, cdcEngineTools fork |
| Tomb Raider Forums (Technical) | https://www.tombraiderforums.com | Primary community for TRL/TRA/TRU modding discussion |
| cdcengine.re | https://cdcengine.re/docs/ | Official community docs site for CDC engine |
| tombraidermodding.com | https://tombraidermodding.com | Tool downloads and tutorials |
| Nexus Mods (TRL) | https://www.nexusmods.com/tombraiderlegend | Texture/graphics mods |
| Speedrun.com (TRL) | https://www.speedrun.com/trl | Technical mechanics, memory addresses |

---

## Quick Reference Table

| Tool | Purpose | TRL? | Status | Link |
|------|---------|------|--------|------|
| **cdcEngine** | Engine decompilation (C++) | Yes | Active | [GitHub](https://github.com/TheIndra55/cdcEngine) |
| **TRLAU-Menu-Hook** | Debug menu + RE hooks | Yes | Active (v2.5) | [GitHub](https://github.com/TheIndra55/TRLAU-menu-hook) |
| **Gibbed.TombRaider** | Bigfile pack/unpack | Yes | Abandoned | [GitHub](https://github.com/LazyBui/gibbedtr) |
| **cdcEngineTools** | DRM extract, PCD→DDS | Yes | Fork active | [GitHub](https://github.com/TombRaiderModding/cdcEngineTools) |
| **BigfileLogger** | Runtime asset load logging | Yes | Active | [GitHub](https://github.com/TombRaiderModding/BigfileLogger) |
| **ModLoader** | Loose-file mod loading | Yes | Active (v1.2) | [GitHub](https://github.com/TombRaiderModding/ModLoader) |
| **ModelEx** | 3D model viewer/exporter | Yes | Active (v6.1) | [GitHub](https://github.com/TheSerioliOfNosgoth/ModelEx) |
| **Noesis Plugin** | Model + texture import/export | Yes | Active (v1.2) | [tombraidermodding.com](https://tombraidermodding.com/tool/trulau-mod-tools) |
| **TR7AE-Mesh-Exporter** | Custom model → game format | Yes | Complete | [GitHub](https://github.com/Raq1/TR7AE-Mesh-Exporter) |
| **TR7AE Level Viewer** | Browser-based level viewer | Yes | WIP | [GitHub](https://github.com/TheIndra55/TR7AE-level-viewer) |
| **Yura** | Bigfile browser (hash→name) | Yes | Active | [GitHub](https://github.com/TheIndra55/Yura) |
| **010 Editor Templates** | Binary format inspection | Yes | Active | [GitHub](https://github.com/TombRaiderModding/Templates) |
| **TexMod** | Runtime texture replace | Yes | Abandoned | Various mirrors |
| **TexModAutomator** | Batch TexMod automation | Yes | Available | [Nexus](https://www.nexusmods.com/tombraiderlegend/mods/92) |
| **MulDeMu** | .mul audio demux | Yes | Abandoned | [GitHub](https://github.com/sephiroth99/MulDeMu) |
| **foo_tr7ae** | foobar2000 audio player | Yes | Active | [GitHub](https://github.com/TombRaiderModding/foo_tr7ae) |
| **Translatr** | Text/subtitle extraction | Yes | Abandoned | [GitHub](https://github.com/sephiroth99/translatr) |
| **Autosplitter** | Confirmed memory addresses | Yes | Active | [GitHub](https://github.com/FluxMonkii/Autosplitters) |
| **CE Table** | Health/ammo/XYZ addresses | Yes | Available | [FearlessRevolution](https://fearlessrevolution.com/viewtopic.php?t=14926) |
| **Helix Mod** | Stereo3D shader fix | Yes | Abandoned | [Blog](https://helixmod.blogspot.com/2012/05/tomb-raider-legend.html) |
| **dgVoodoo+ReShade** | Graphics enhancement | Yes | Available | [Nexus](https://www.nexusmods.com/tombraiderlegend/mods/3) |

---

## Key File Formats

| Extension | Purpose |
|-----------|---------|
| `bigfile.000/.001/...` | Main game archive (all assets) |
| `.drm` | Digital Resource Module — sections of meshes, textures, scripts, objects |
| `.pcd` / `.pcd9` | Texture format (converts to DDS via PCD2DDS) |
| `.tr7aemesh` | Mesh data |
| `.gnc` | Geometry/normal cache (baked lighting) |
| `.mul` | Multiplexed audio + cinematics |
| `.raw` | Raw texture/data |
| `.pcm` | Model data (read by ModelEx) |

---

## Highest-Value Targets for Our RTX Remix RE Work

1. **TRLAU-Menu-Hook source** — grep for addresses near `0xF2A0D4/D8` (our culling globals) and `0x60C7D0` (RenderLights_FrustumCull). The render toggle features almost certainly reference the same globals.
2. **cdcEngine decompilation** — `SceneLayer::Render(ISceneCell*)` is the scene traversal entry point. `InstanceDrawable::Draw()` → `RenderModelInstance::Draw()` is the draw submission path.
3. **BigfileLogger** — deploy alongside our proxy to correlate asset hash → DRM filename. Would identify which DRM files contain the stage light geometry being culled.
4. **010 Editor Templates** — `tr7ae_section.bt` documents the DRM section layout mapping to rendered geometry.
5. **Autosplitter addresses** — three confirmed static VAs: loading=`0xCC0A54`, level=`0xD59318`, bosshealth=`0xB14A40` (trl.exe)
