# Build History — Master Index

> One row per build, in chronological order. **49 numbered builds plus 071b and the two specials (contenders/build-073-stable-hashes, session-000-early-dev).**

Gap policy:
- Builds 003–015: pre-archive, not preserved
- Build 034: not preserved
- Build 043: crashed, not preserved
- Builds 048–063: 16-build stretch of unsuccessful proxy iteration, not preserved (the wins from this stretch are folded into the build-064 baseline)
- Build 071b: an inline corrigendum to 071

For the underlying evidence (screenshots, proxy logs, source snapshots) see `TRL tests/build-NNN-*/` in the repo. The wiki carries the narrative, not the binary artifacts.

## At-a-glance

- **Total numbered builds in archive:** 50 (49 + 071b)
- **Specials:** 2 (`contenders/build-073-stable-hashes`, `session-000-early-dev`)
- **Confirmed PASS results:** builds 001, 002, 016*, 019*, 076 (hash-stability), 077 (cold-launch), `contenders/build-073-stable-hashes` (world hashes only)
- *Builds 016 and 019 PASSes later confirmed false-positive (input wasn't reaching the game / wrong screenshots evaluated)
- **Open blocker as of build 079:** Stale anchor mesh hashes in `mod.usda` (see [[Build-074-077-Asset-Pipeline]])

The full per-phase narrative lives in the four chapter pages:

- [[Build-001-to-015-Baseline]] — early baselines, hash-stability proofs (001–015)
- [[Build-016-to-044-Anti-Culling]] — the long culling-layer attack (016–044)
- [[Build-045-073-Hash-Pipeline]] — VB management, FLOAT3 fixes, hash stability (045–073)
- [[Build-074-077-Asset-Pipeline]] — `user.conf` foot-gun + cold-launch crash fix (074–077)
- [[Build-078-079-Performance-and-Skinning]] — proxy optimization + skinned-char drift (078–079)

## Complete table

| Build | Status | What changed | Key finding |
|---|---|---|---|
| 001 | PASS | Shader passthrough + transform override; asset hash rule `indices,texcoords,geometrydescriptor`; ENABLE_SKINNING=0; frustum threshold patched to `1e30` + RET at `0x407150` | Asset hashes stable across A/D strafe and across sessions in Bolivia cave. 1,440 draws/120-frame batch. View matrix `0x010FC780`, Proj `0x01002530`. `vpValid=1` always. |
| 002 | PASS | Two-phase test: debug view 277 (hash) + clean RTX render | Asset hashes stable frame-to-frame and across camera movement; generation hash still flickers (positions included, by design). |
| 016 | PASS* | Frustum threshold corrected `1e30 → 0.0`, 7 scene-traversal cull jumps NOPed, per-BeginScene re-stamp, `enableReplacementAssets=True` | Draw count stabilized at 91,800 (was 40K-189K). Confirmed false positive later — movement input wasn't reaching the game. |
| 017 | FAIL | Moved cull NOPs from ASI patcher into proxy source; per-frame `0xEFDD64=0.0f` in BeginScene; VK_MAP for `]` screenshot key | First real movement test; both lights vanish on D-strafe, hash colors shift between positions. |
| 018 | FAIL | Added `KEYEVENTF_SCANCODE` (0x0008) to `_make_key_input()` in `livetools/gamectl.py` — TRL reads scancodes via DirectInput | Movement now real (Lara visibly moves). Green light disappears after 8s D-strafe; all prior "PASS" results are suspect. |
| 019 | PASS* | No proxy change vs 018; re-test with randomized D=9293ms, A=8270ms | Both lights visible in all 3 clean screenshots. Later confirmed false positive — the macro's camera-pan screenshots were being evaluated instead of post-movement ones. |
| 020 | FAIL | Fixed screenshot selection; added `focus_hwnd()` re-call before HOLD tokens | Red light missing in 2/3 shots. Build-019's PASS officially retracted. |
| 021 | PASS* | VS 2026 Insiders detection in `build.bat`; TR7_Analyze.py category fix; kb.h additions (`g_pEngineRoot=0x01392E18`, cull globals `0xF2A0D4/D8`) | Renderer chain mapped: `g_pEngineRoot → +0x214 → TRLRenderer* → +0x0C → IDirect3DDevice9*`. False positive — Lara didn't move. |
| 022 | FAIL | No proxy change; randomized A=2588ms, D=3362ms; first run of static-analyzer subagent flow | Static-analyzer confirms patches are runtime-only (on-disk unmodified). `RenderLights_FrustumCull` (`0x60C7D0`) still active; cull globals `0xF2A0D4/D8` cache pattern identified. |
| 023 | FAIL | Added NOPs at `0x60CDE2` and `0x60CE20` — BUT in wrong source file (repo-root `proxy/`, not `patches/TombRaiderLegend/proxy/`) | **Build path bug**: patches never compiled into running DLL. Apparent improvement unexplained. |
| 024 | FAIL | Moved `0x60CDE2 + 0x60CE20` NOPs to correct source file; both patches now in proxy log | Shot 1 shows BOTH lights (first real success of light NOPs); shots 2-3 fail. Two draw paths exist in `RenderLights_FrustumCull` — immediate (mode=1) and deferred (mode=0). |
| 025 | FAIL | NOPs at `0x603832` (scene-list pending-flag skip) and `0x60E30D` (render-gate pending-flag check) | No change. Bottleneck not in caller chain; per-sector light count check at `[sector+0x1B0]` identified as likely gate. |
| 026 | FAIL | 5 NOPs in `LightVolume_UpdateVisibility` (0x6124E0): `0x6125EC, 0x61264C, 0x6126AA, 0x612701, 0x61279A` | New patches silently failed — never appeared in proxy log (VirtualProtect may have failed). |
| 027 | FAIL | LightVolume visibility NOPs (5×) added properly this build | Randomized A=1.1s, D=8.9s; lights fade at distance. Deferred-light second loop at `0x60CF18` identified. |
| 028 | FAIL | NOPs at `0x46C194` + `0x46C19D`; removed native light NOPs as irrelevant for Remix anchors; **fixed build path** | Draw count jumped from ~1,440 to **93K-189K** (65× increase). Geometry IS submitting; problem is Remix light range. |
| 029 | FAIL | Frustum threshold `0.0 → -1e30f`; cull mode globals stamped to `D3DCULL_NONE` every BeginScene; re-NOPed `0x60CE20` | `Light_VisibilityTest` (`0x60B050`) bypass attempted and reverted — killed native fill lighting (has side effects in `FUN_0060ac80`, `FUN_0060ad20`, `FUN_005f9a60`). |
| 030 | FAIL | No code change — baseline + Ghidra evidence collection | **Root cause identified**: `Light_VisibilityTest` (`0x60B050`) is unpatched culling gate. Only 1 caller — safe to patch. |
| 031 | FAIL | Runtime patch at `0x60B050`: `B0 01 C2 04` (`mov al,1; ret 4`) | All 7 patches active. Per-sector gating identified. Config flag at `0x01075BE0` found. |
| 032 | FAIL | Stamped `*(DWORD*)0x01075BE0 = 1` | No effect — config flag has no code xrefs. |
| 033 | FAIL | No proxy change; added gate at `0xEC6337` | Macro left game on pause menu — all 6 screenshots show menu overlay. 8 patches active and verified. |
| 035 | FAIL | NOP at `0xEC6337`; re-enabled `0x60B050` patch; red fallback `rtx.fallbackLightRadiance = 3, 0.3, 0.3` | Sector light list at `+0x1B0/+0x1B8` populated by `FUN_00EC62A0` from `[sector_data+0x664]`. Only `mesh_AB241947CA588F11` (green) is in a sector with non-zero static light data. |
| 036 | FAIL | `ctypes.cast` crash fix in `run.py` setup automation | Green vanishes at sector boundary. |
| 037 | FAIL | NOPs at `0x60E3B1` (RenderLights gate) and `0x603AE6` (zeroes `[eax+0x1B0]`) | Green still vanishes at distance. |
| 038 | FAIL | `rtx.fallbackLightRadiance` `3, 0.3, 0.3` → `1, 1, 1` (neutral white) | **MAJOR REFRAME**: "red at distance" in builds 019-037 was the fallback light, not the red stage light. The problem is **geometry submission**. |
| 039 | FAIL | Removed RET patch at `0x407150` — function now runs with its 7 internal cull jumps NOPed | Draw counts ~93K → ~180K (87K previously silently skipped). Green visible at extreme distance for first time. |
| 040 | FAIL | NOPed 4 more cull jumps inside `0x407150`: `0x4071CE, 0x407976, 0x407B06, 0x407ABC` | 11/12 conditional exits NOPed; ~190K draws. Culling NOT in this function — must be upstream. |
| 041 | FAIL | Per-scene stamp of `g_farClipDistance` at `0x10FC910 = 1e30f` | No effect. Confirms anchor meshes are sector-list-bound, not distance-culled. |
| 042 | FAIL | mod.usda: parented all 3 lights to `mesh_7DFF31ACB21B3988` (largest 88KB mesh) | Worse — mesh isn't always drawn. Largest != always-drawn. ([[Dead-Ends]] #1) |
| 044 | FAIL | NOP at `0x46B85A` (camera-sector proximity filter); build 043 crashed and was not preserved | Full call chain mapped. Terrain path flagged as next suspect. |
| 045 | FAIL | `S4_GetCachedExpVB` switched to `D3DPOOL_MANAGED` (1); per-frame `S4_FlushVBCache()` | Hash debug stable. Clean render: uniform brown/amber (flush too aggressive — 512 VB creates/frame). |
| 046 | FAIL | Content fingerprint cache (XOR of first 32 VB bytes); nulled VS for ALL DIP draws | Rendering broken — Lara's face fills screen. **FLOAT3 positions are pre-transformed to view space by game CPU**; nulling VS for FLOAT3 breaks the pipeline. ([[Dead-Ends]] #9) |
| 047 | FAIL | Removed `positions` from `geometryAssetHashRuleString` | **Catastrophic hash collision** — entire scene one hot pink. **Positions are required.** ([[Dead-Ends]] #10) |
| 064 | FAIL | First in new hash-stability test workflow (camera-only pan, no WASD) | Phase 1 INVALID — only loading screen captured (15s wait insufficient for Remix + Peru init). Draw counts 244-245 stable. |
| 065 | FAIL | No code change; longer load wait | Hash debug stable. ~650-657 draws. Light anchor hashes documented. |
| 066 | FAIL | `DRAW_CACHE_ENABLED 0` — disabled 4096-entry draw replay cache | No change. ([[Dead-Ends]] #11) |
| 067 | FAIL | Removed 1e-4 epsilon from `mat4_changed()` — VP inverse now always recalculates | No change. ([[Dead-Ends]] #12) |
| 068 | FAIL | Re-enabled `0x60B050 / 0xEC6337 / 0x60E3B1` patches | **No crash** — crash at `0xEE88AD` is fixed by `ProcessPendingRemovals` patch + null-check trampoline. 20+ patches confirmed active. |
| 069 | FAIL | No code change | Draw counts collapse during session: 2647 → 1390 → 911 → 673 (75% culled despite patches). |
| 070 | FAIL | No code change | Draw counts collapsed 93% over session: 2833 → 670 → 639 → 185. Confirmed proxy at `0x407150` uses NOP-jump strategy (not RET). |
| 071 | FAIL | Expanded `mod.usda` from 5 to 8 anchor mesh hashes | Draws stable at ~2845. SetWorldMatrix 51,484 calls in 15s. Lara not visible. |
| 071b | FAIL | **FLOAT3 draw path fixed**: null VS + FFP state + draw + restore VS, mirroring `S4_ExpandAndDraw` | **Lara now visible for the first time**. ~684 draws/frame (429 S4 + 255 F3). Still no stage lights. |
| 072 | FAIL | **Layer 31**: `RenderQueue_FrustumCull` (`0x40C430`) → JMP to `RenderQueue_NoCull` (`0x40C390`) | Draws +29% (2845 → 3657). Lara visible in hash debug for first time. |
| 073 | FAIL | `rtx.useVertexCapture = True` (was False since build 045) | Draws ~3651. Small white dots appear (later confirmed denoiser/NRC artifacts). |
| `contenders/build-073-stable-hashes` | PASS (world hashes) | Same proxy state as build-073 | First ever stable world hashes — promoted as a contender. See [[Build-045-073-Hash-Pipeline]]. |
| 074 | FAIL | Deferred `TRL_ApplyMemoryPatches()` to first BeginScene with `viewProjValid=1`; permanent page unlock; release ordering fix; DIAG gating | All 31 patches applied. 3749 draws/scene. mod.usda hashes flagged as stale. |
| 075 | FAIL | **CRITICAL**: discovered `user.conf` had `rtx.enableReplacementAssets = False` overriding `rtx.conf`. Fixed to True | **BREAKTHROUGH**: Replacement asset pipeline confirmed end-to-end. Purple light at `mesh_574EDF0EAD7FC51D` visible. **The 8 building hashes in mod.usda are stale.** ([[Dead-Ends]] #14) |
| 076 | PASS (hash stability) | Restored `patch_null_crash_40D2AF()`; restored PUREDEVICE stripping; FourCC format rejection; build.bat VS 18 Community fallback | Hash stability PASS. Baseline test confirmed purple light still visible. 3,733 draws/scene crash-free. |
| 077 | FIXED | DrawCache use-after-free: now `AddRef`s on record, `Release` on eviction | **Cold launch crash resolved**. Game survives 90+s menu→Peru transition. |
| 078 | PERF | `DIAG_ENABLED 1→0`; `PINNED_REPLAY_INTERVAL 60→600`; per-draw SetTransform now cached | DLL: 56,320 → 48,640 bytes (-13.6%). Byte-identical geometry submission preserved. |
| 079 | FAIL | Added `BuildSkinnedNormalizedDecl()` — clone of every skinned decl with `BLENDWEIGHT`/`BLENDINDICES` stripped | Fix doesn't engage — Lara takes shader route (`Float3Route effective: shader`). Build deployed under wrong path initially. |
| `session-000-early-dev` | (no result) | Pre-archive exploratory screenshots | 13 raw NVIDIA captures from 2026-03-24 across 4 runs. Predates build-001. |

## Storage footprint

A representative mid-archive build totals ~1.9 MB (3× hash-debug PNGs, 3× clean render PNGs, proxy source snapshot, SUMMARY.md). The full archive is ~350–400 MB, dominated by:
- Phase 2 clean render PNGs (highest-resolution path-traced shots are 3–3.5 MB each in builds with full PNGs)
- `session-000-early-dev` (32 MB raw NVIDIA captures)
- Builds 045, 046, 064, 069 (10–44 MB each)

The smallest build is `build-077` at 20 KB (text-only, no screenshots because it was a crash-fix verification, not a render test).

## See also

- [[Current-Status]] — live state (one blocker remaining)
- [[Dead-Ends]] — every approach that was tried and failed, with why
- [[36-Layer-Culling-Map]] — the canonical culling-layer inventory
- [[Build-074-077-Asset-Pipeline]] — the `user.conf` foot-gun, the most consequential discovery in the archive
