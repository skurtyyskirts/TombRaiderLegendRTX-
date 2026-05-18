# Builds 016–044 — The Anti-Culling Campaign

> Twenty-five builds across approximately ten months. The first systematic attack on cdcEngine's culling pipeline. Outcome: 11 of the eventual 36 culling layers patched, lights still missing — but the build-038 reframe (the "red light" being a fallback) reset the entire investigation.

The narrative of this epoch is the **discovery of how many culling layers cdcEngine actually has.** Each NOP patched one suspected gate; the next test always revealed another. The eventual count would reach 36.

## Builds 016–022 — First contact with the real problem

### Build 016 — Anti-culling NOP (PASS\*)

The first attempt at attacking culling. Patches:
- Frustum threshold corrected from `1e30 → 0.0` (the earlier 1e30 was overly aggressive and broke geometry in some cases)
- 7 scene-traversal cull jumps NOPed at `0x4072BD, 0x4072D2, 0x407AF1, 0x407B30, 0x407B49, 0x407B62, 0x407B7B`
- Per-BeginScene re-stamp
- `enableReplacementAssets = True`

Draw count stabilized at 91,800 (was wildly oscillating 40K–189K before). Both lights visible in screenshots.

**Why this was a false positive:** Movement input was not reaching the game. The macro fired `SendInput` calls but TRL reads scancodes via DirectInput, which ignores virtual-key codes. Lara stayed still in all 3 screenshots; the lights were the same lights at the same camera angle.

### Builds 017, 018 — Fixing the input pipeline

- Build 017 moved cull NOPs from a separate ASI patcher into the proxy source itself (eliminating dependency on a second DLL). First real movement test fails.
- Build 018 added `KEYEVENTF_SCANCODE` (`0x0008`) to `_make_key_input()` in `livetools/gamectl.py`. **First time Lara actually moves.** Green light disappears after 8s D-strafe. All prior PASS results retroactively suspect.

This is the genesis of the "build 019 PASS retracted" pattern that recurs throughout the project. After build 018, every PASS is required to demonstrate that Lara moved between screenshots.

### Build 019 — The retracted PASS

Both lights visible in all 3 clean screenshots. Looked like a clean win.

**Retracted in build 020** when screenshot selection was audited: the test harness was evaluating the macro's intermediate camera-pan screenshots (taken while Lara stood still) rather than the post-movement ones. With correct screenshots, the red light was missing in 2 of 3.

### Builds 020, 021, 022 — Tightening test integrity

- Build 020 fixed screenshot selection; added `focus_hwnd()` re-call before HOLD tokens (the game window was losing focus when keys were held long).
- Build 021 mapped the renderer chain: `g_pEngineRoot (0x01392E18) → +0x214 → TRLRenderer* → +0x0C → IDirect3DDevice9*`. PASS again — but Lara didn't move, so retracted.
- Build 022 confirmed via the static-analyzer subagent that all patches were runtime-only (on-disk binary unmodified). 2.18M cumulative draws across the session. The `0xF2A0D4/D8` cull globals cache pattern was identified.

## Builds 023–028 — The light-system attack

### Build 023 — The build-path bug

NOPs added at `0x60CDE2` (broad-visibility test JZ) and `0x60CE20` (frustum 6-plane test JNP). Inexplicable improvement in screenshots.

**Reason:** The NOPs were added to `proxy/d3d9_device.c` at the repo root, but `run.py` builds from `patches/TombRaiderLegend/proxy/`. The patches never compiled into the running DLL. The apparent improvement remains unexplained — most likely run-to-run variance.

This was the first build-path-bug discovery and led to the project's `cmp` check after every build.

### Build 024 — Fixed build path, both lights in shot 1

Same two NOPs, now in the right source file. Shot 1 shows both lights. Shots 2–3 fail. On-disk bytes verified.

**Discovery:** Two draw paths exist in `RenderLights_FrustumCull` — immediate (mode=1) and deferred (mode=0). Patching the frustum-test JNP only affects one path.

### Builds 025–028 — Pending flags, visibility state, sector visibility

- Build 025: NOPs at `0x603832` and `0x60E30D` (pending-render flags). **No effect** — bottleneck is upstream. ([[Dead-Ends]] #6)
- Build 026: 5 NOPs in `LightVolume_UpdateVisibility` (0x6124E0). **Silent VirtualProtect failure** — patches never appeared in proxy log. ([[Dead-Ends]] #7)
- Build 027: Same 5 NOPs properly applied. Lights still fade at distance. Deferred-light second loop at `0x60CF18` identified.
- Build 028: NOPs at `0x46C194 + 0x46C19D` (sector/portal visibility). **Draw count jumps from ~1,440 to 93K-189K (65×).** Geometry IS submitting; the problem must be Remix light range.

Build 028 is the moment the project realizes draws were being skipped at the *sector* level, not just the per-object level. The geometry pipeline opens up after this build.

## Builds 029–033 — Light side effects and the dead-end config flag

### Build 029 — Frustum threshold to -1e30f

Frustum threshold flipped from `0.0` (which still rejected objects behind the camera) to `-1e30f` (rejects nothing). Cull mode globals stamped to `D3DCULL_NONE` every BeginScene. 189,829 draws at scene 600.

Also attempted: bypassing `Light_VisibilityTest` (`0x60B050`). **Reverted immediately** — killed native fill lighting because the function has side effects:
- Type 0 lights: calls `FUN_0060ad20` (sphere visibility update)
- Type 1 lights: calls `FUN_005f9a60` (cone visibility update)
- Type 2 lights: always passes

This is the lesson behind the `culling-patch-reviewer` agent that audits every new patch proposal.

### Builds 030–033 — Light visibility test patch and the config flag

- Build 030: No code change; baseline + Ghidra evidence collection. **Root cause identified**: `Light_VisibilityTest` is the unpatched culling gate; only 1 caller — safe to patch.
- Build 031: Runtime patch at `0x60B050`: `B0 01 C2 04` (`mov al,1; ret 4` for `__thiscall` with 1 stack arg). All 7 patches active. Per-sector gating identified: `RenderScene_Main` (`0x603810`) iterates sectors, gates on `sector+0x84 + sector+0x94 != 0`; light list at `sector+0x1B0` (count) / `sector+0x1B8` (array). Config flag at `0x01075BE0` ("Disable extra static light culling") found via table at `0xF1325C` → string at `0xEFF384`.
- Build 032: Stamped `*(DWORD*)0x01075BE0 = 1`. **No effect** — config flag has no code xrefs. ([[Dead-Ends]] #5)
- Build 033: No proxy change. Macro left game on pause menu — all 6 screenshots show menu overlay. 8 patches active and verified. Gate at `0xEC6337` added for next build.

## Builds 035–037 — Per-sector light count gates

### Build 035 — Green light works

NOP at `0xEC6337` (sector light count gate — forces load from `[sector_data+0x664]`). Re-enabled `0x60B050` patch. Fallback `rtx.fallbackLightRadiance = 3, 0.3, 0.3`.

**Discovery:** Sector light list at `+0x1B0/+0x1B8` is populated by `FUN_00EC62A0` from `[sector_data+0x664]`. **Only `mesh_AB241947CA588F11` (green) is in a sector with non-zero static light data.** Most sectors have `[sector_data+0x664] = 0`.

This is why builds 035–037 partial-passed only the green light. The red anchor meshes live in sectors with zero light data, so the per-sector iteration finds no lights to render even with `Light_VisibilityTest` forced TRUE.

### Builds 036, 037 — Sector boundary and render gate

- Build 036: `ctypes.cast` crash fix in `run.py` setup automation (replaced with `ctypes.addressof`); no proxy code change. Green vanishes at sector boundary.
- Build 037: NOPs at `0x60E3B1` (RenderLights gate — 6-byte JE skipping `RenderLights_FrustumCull` on zero sector light count) and `0x603AE6` (6-byte MOV that zeros `[eax+0x1B0]` per frame). Green still vanishes at distance.

## Build 038 — The reframe

**The single most important diagnostic build of this epoch.**

Changed only `rtx.fallbackLightRadiance` from `3, 0.3, 0.3` (red-tinted) to `1, 1, 1` (neutral white). Both lights are gone in the screenshots.

**Reframe:** The "red light at distance" visible in builds 019-037 was the **fallback light** all along, not the red stage light. Neutral fallback proved it wasn't a real red light — it was a fallback masquerading as one because of the red-tinted radiance setting.

This invalidates the partial-PASS results of builds 035–037 (the visible "red" was not the real red stage light, it was the colored fallback). Worse, it means the lighting work done in builds 023–037 was largely chasing the wrong target.

**The new question:** Why is the geometry not visible? Lights aren't the problem — geometry submission is.

## Builds 039–044 — Geometry submission

### Build 039 — Remove the RET

Removed the `RET` patch at `0x407150` that builds 001–038 had inherited from the original baseline. The function now runs with its 7 internal cull jumps NOPed but the **submission body intact**.

Draw counts jump from ~93K to ~180K. The previous RET was silently skipping 87K draws per scene. Green light visible at extreme distance for the first time.

### Build 040 — 11 NOPs inside SceneTraversal_CullAndSubmit

NOPed 4 more cull jumps: `0x4071CE` (obj disable bit 0x10 phase A), `0x407976` (phase B), `0x407B06` (far clip), `0x407ABC` (draw distance fade). 11 of 12 conditional exits in `0x407150` now NOPed. ~190K draws.

**Result: lights still culled.** ([[Dead-Ends]] #4) The culling cannot be inside `SceneTraversal_CullAndSubmit`. It must be **upstream** — sector iteration, scene graph traversal, the render queue.

### Builds 041, 042, 044 — Far clip, mesh reparenting, sector proximity

- Build 041: Per-scene stamp of `g_farClipDistance` at `0x10FC910 = 1e30f`. No effect. Confirms anchor meshes are sector-list-bound, not distance-culled.
- Build 042: `mod.usda` parented all 3 lights to `mesh_7DFF31ACB21B3988` (the largest 88KB mesh). Worse — largest != always-drawn. ([[Dead-Ends]] #1)
- Build 043: Crashed; not preserved. Attempted aggressive 7-NOP set in SceneTraversal. ([[Dead-Ends]] #2)
- Build 044: NOP at `0x46B85A` (2-byte JNE, camera-sector proximity filter in `RenderSector` 0x46B7D0). Full call chain mapped:
  ```
  RenderFrame (0x450B00)
   ├─ RenderVisibleSectors (0x46C180)
   │    └─ RenderSector (0x46B7D0)
   ├─ SceneTraversal wrapper (0x443C20)
   │    └─ 0x407150
   └─ post-sector loop at 0x40E2C0
  ```
  
Terrain path (`TerrainDrawable` at `0x40ACF0` + `TERRAIN_DrawUnits`) flagged as the next suspect. This sets up the work in builds 045–063 (terrain investigation, not archived).

## What this epoch established

- **The culling map became real.** Builds 016–044 grew the culling-layer inventory from 4 layers (the original baseline) to ~17 layers. The eventual 36-layer count grew from continued work in builds 045+.
- **The fallback-light reframe.** The single most consequential mid-project correction — re-pointed everything at geometry submission instead of light visibility.
- **Build-path discipline.** Build 023 → 028 was the canonical bug → fix cycle for this; the lesson sticks for the rest of the project.
- **The static-analyzer subagent workflow.** Started in build 022, became standard for every test build going forward.
- **`Light_VisibilityTest` has side effects.** Lesson #1 in the culling-patch-reviewer's audit checklist.

## Open issues at end of epoch

- ~190K draws submitted but anchor meshes still appear culled
- Terrain path entirely uninvestigated
- Sector-light-list population path (`FUN_006033d0` / `FUN_00602aa0`) not yet decompiled
- No hash debug view used yet (introduced in build 045)
- SHORT4 vertex format not yet expanded — hashes might already be drifting but no one is looking

These pick up in [[Build-045-073-Hash-Pipeline]].

## See also

- [[Build-History-Index]] — full one-line-per-build table
- [[36-Layer-Culling-Map]] — the canonical layer inventory with addresses
- [[Engine-Memory-Map]] — globals and sector structure discovered here
- [[Dead-Ends]] — items #1, #2, #4, #5, #6, #7 originate here
