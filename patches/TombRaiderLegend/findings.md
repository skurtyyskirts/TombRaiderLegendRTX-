## Static Analysis: Patch Site Verification and Proxy Log Review -- 2026-03-26

### Summary

All runtime memory patches applied by the proxy DLL target correct addresses with correct assumptions about original byte patterns. The on-disk binary at every patched address matches the expected original bytes. The proxy log shows all patches applied successfully (7/7 cull NOPs, 2/2 sector NOPs, frustum RET, light frustum NOP, cull globals stamped). The proxy is operating nominally: vpValid=1 on all scene reports, zero passthrough/skipped/xformBlocked draws, and draw counts are consistent. No crashes or errors detected in the log.

### Key Addresses

| Address | Description | On-Disk Bytes | Runtime Patch | Status |
|---------|-------------|---------------|---------------|--------|
| 0x407150 | SceneTraversal_CullAndSubmit entry | `55 8B EC 83` (push ebp; mov ebp,esp; and...) | `C3` (ret) | CORRECT -- 0x55 replaced with 0xC3 at runtime |
| 0x4072BD | Distance cull jump 1 | `0F 85 ...` (jne 0x4078CD) | 6x NOP (0x90) | CORRECT -- 6-byte near conditional jump |
| 0x4072D2 | Distance cull jump 2 | `0F 85 ...` (jne 0x4078CD) | 6x NOP (0x90) | CORRECT -- 6-byte near conditional jump |
| 0x407AF1 | Distance cull jump 3 | `0F 8B ...` (jnp 0x40804E) | 6x NOP (0x90) | CORRECT -- 6-byte near conditional jump |
| 0x407B30 | Screen boundary jump 1 | `0F 85 ...` (jne 0x40804E) | 6x NOP (0x90) | CORRECT -- 6-byte near conditional jump |
| 0x407B49 | Screen boundary jump 2 | `0F 85 ...` (jne 0x40804E) | 6x NOP (0x90) | CORRECT -- 6-byte near conditional jump |
| 0x407B62 | Screen boundary jump 3 | `0F 8A ...` (jp 0x40804E) | 6x NOP (0x90) | CORRECT -- 6-byte near conditional jump |
| 0x407B7B | Screen boundary jump 4 | `0F 8A ...` (jp 0x40804E) | 6x NOP (0x90) | CORRECT -- 6-byte near conditional jump |
| 0x46C194 | Sector visibility JE | `0F 84 1B 01 00 00` (je 0x46C2B5) | 6x NOP (0x90) | CORRECT -- 6-byte near conditional jump |
| 0x46C19D | Sector visibility JNE | `0F 85 12 01 00 00` (jne 0x46C2B5) | 6x NOP (0x90) | CORRECT -- 6-byte near conditional jump, starts at offset +9 from 0x46C194 |
| 0x60CE20 | Light frustum rejection JNP | `0F 8B 8D 01 00 00` (jnp 0x60CFB3) | 6x NOP (0x90) | CORRECT -- 6-byte JNP, skips light draw if outside frustum planes |
| 0xEFDD64 | Frustum threshold float | Original game value | `-1e30f` | CORRECT -- .rdata float, ensures distance cull never triggers |
| 0xF2A0D4 | g_cullMode_pass1 | Original game value | `1` (D3DCULL_NONE) | CORRECT -- 4-byte global |
| 0xF2A0D8 | g_cullMode_pass2 | Original game value | `1` (D3DCULL_NONE) | CORRECT -- 4-byte global |
| 0xF2A0DC | g_cullMode_pass2_inverse | Original game value | `1` (D3DCULL_NONE) | CORRECT -- 4-byte global |

### Details

#### 1. Frustum Cull Function (0x407150) -- RET Patch

**On-disk disassembly:**
```asm
0x00407150: push     ebp                    ; original first byte = 0x55
0x00407151: mov      ebp, esp
0x00407153: and      esp, 0xfffffff0
0x00407156: sub      esp, 0x1e4
```

The proxy overwrites byte at 0x407150 from `0x55` (push ebp) to `0xC3` (ret). This is a safe patch because the function uses cdecl convention with caller cleanup -- a bare `ret` returns immediately and the caller handles stack adjustment. The function body (approx 0x900 bytes through 0x407A00+) is never reached.

**Note:** The 7 NOP patches at 0x4072BD..0x407B7B are inside this same function and are technically redundant with the RET patch (if the function returns immediately, none of those jumps execute). However, the proxy applies both as defense-in-depth -- if the RET patch were ever removed, the NOP patches would still disable individual cull checks.

#### 2. Scene Traversal Cull Jumps (0x4070F0 region) -- NOP Patches

The disassembly at 0x4070F0 shows **the end of a different, preceding function** (not 0x407150). The function at 0x4070F0 ends with `ret` at 0x407107, followed by int3 padding to 0x407110 where another small function begins. The requested address 0x4070F0 is not a patch site itself -- the actual patch sites are at 0x4072BD-0x407B7B, all inside the function starting at 0x407150.

All 7 jump addresses are confirmed as 6-byte conditional near jumps (`0F 8x` prefix):
- 0x4072BD: `jne` (0F 85) -- distance-based cull
- 0x4072D2: `jne` (0F 85) -- distance-based cull
- 0x407AF1: `jnp` (0F 8B) -- FPU-based screen boundary
- 0x407B30: `jne` (0F 85) -- screen boundary
- 0x407B49: `jne` (0F 85) -- screen boundary
- 0x407B62: `jp`  (0F 8A) -- screen boundary
- 0x407B7B: `jp`  (0F 8A) -- screen boundary

#### 3. Sector Visibility (0x46C194, 0x46C19D) -- NOP Patches

**On-disk disassembly at 0x46C190:**
```asm
0x0046C190: test     byte ptr [esi + 1], 8      ; check sector visibility bit
0x0046C194: je       0x46c2b5                    ; skip if bit not set (0F 84 1B 01 00 00)
0x0046C19A: cmp      byte ptr [esi], 0           ; check sector enabled byte
0x0046C19D: jne      0x46c2b5                    ; skip if disabled (0F 85 12 01 00 00)
```

**Raw bytes confirmed:** `0F 84 1B 01 00 00` at 0x46C194, followed by `80 3E 00` (cmp byte ptr [esi], 0), then `0F 85 12 01 00 00` at 0x46C19D. Both are exactly 6 bytes. The proxy NOPs both, forcing all 8 sectors to render regardless of portal visibility.

**Correctness:** After NOPing the JE at 0x46C194, execution falls through to `cmp byte ptr [esi], 0` at 0x46C19A. After NOPing the JNE at 0x46C19D, execution falls through to `mov eax, [esi+0x3F]` at 0x46C1A3. This is correct -- the code continues to the sector rendering logic without skipping. No stack or register corruption from the NOP sled.

#### 4. Light Frustum Rejection (0x60CE20) -- NOP Patch

**On-disk disassembly:**
```asm
0x0060CE17: fcomp    dword ptr [esp + 0x10]    ; compare bounding sphere vs plane
0x0060CE1B: fnstsw   ax                        ; FPU status to AX
0x0060CE1D: test     ah, 5                     ; test CF and PF
0x0060CE20: jnp      0x60cfb3                  ; SKIP light if outside frustum (0F 8B 8D 01 00 00)
0x0060CE26: inc      edx                       ; next frustum plane
0x0060CE27: add      ecx, 0x20                 ; advance plane pointer
0x0060CE2A: cmp      edx, 6                    ; tested all 6 planes?
0x0060CE2D: jl       0x60cdf1                  ; loop back
0x0060CE2F: ...                                ; passed all planes -> draw the light
0x0060CE42: call     dword ptr [eax + 0x18]    ; vtable[6] = LightVolume::Draw
```

**Raw bytes confirmed:** `0F 8B 8D 01 00 00` at 0x60CE20. This is a 6-byte JNP (jump if not parity). NOPing it means the light sphere-vs-plane test result is ignored, all 6 planes pass trivially, and every light reaches the `call [eax+0x18]` draw dispatch at 0x60CE42.

#### 5. Proxy Log Analysis

**Initialization (lines 1-47):**
- Remix DLL loaded successfully from expected path
- Device created with Real device at 0x01A52C00
- View matrix addr: 0x010FC780, Proj matrix addr: 0x01002530
- All patches reported successful:
  - "Patched frustum threshold to -1e30"
  - "NOPed cull jumps: 7/7"
  - "Patched frustum cull function to ret (0x407150)"
  - "NOPed sector visibility checks: 2/2"
  - "Patched cull mode globals to D3DCULL_NONE"
  - "NOPed light frustum rejection at 0x0060CE20"
- Initial matrices verified (View, Proj, VP, World all valid, non-zero)

**Scene counters (lines 48-187+):**
| Scene Range | Total Draws | Notes |
|-------------|-------------|-------|
| 120-600 | 1416-1440 | Early frames, loading/menu |
| 720 | 955 | Transition |
| 840 | 33,219 | Scene loading, geometry count spike |
| 960-1200 | ~189,700-189,960 | Full scene, steady state, ~190K draws per 120-frame window |
| 1320 | 80,897 | Transitional |
| 1440 | 27,710 | Transitional |
| 1560-2400 | ~93,500-95,280 | Stabilized in-game, ~94K per window |

**All scene reports show:**
- `vpValid=1` -- view-projection matrix always valid
- `passthrough=0` -- no draws falling through to shader passthrough
- `skippedQuad=0` -- no quad skips
- `xformBlocked=0` -- no transforms blocked
- `total == processed` -- every draw call processed through FFP pipeline

**Final diagnostics (lines 1938-12042):**
- Three detailed SCENE snapshots at frames 3583, 3584, 3585
- VS registers written: c0-c3, c8-c15, c28 (consistent across all three)
- Frame 3583 also shows c4, c6, c39 (occasional extended uploads)
- Per-frame draw counts: 2,224,973 (cumulative for SCENE 3583), then 524 and 502 (per-diagnostics-window for SCENE 3584/3585)
- Multiple active vertex shaders cycled per frame: 0x1E7D8D20, 0x1E7D8E88, 0x1E7D94A0, 0x01A19438, 0x01A195A0

**No errors, crashes, or anomalies detected in the entire 12,042-line log.**

### Observation: Redundant Patches

The proxy applies patches in two places: once at device creation (lines ~1033-1047) and again in a dedicated `applyMemoryPatches` function (lines ~1910-2022). The frustum threshold and cull mode globals are written identically in both. This double-write is harmless but worth noting -- the device creation patches fire first, then the dedicated function re-applies them. If the proxy architecture changes, one of these should be removed to avoid confusion about which is authoritative.

### Suggested Live Verification

1. **Confirm runtime bytes at patch sites** -- attach livetools and `mem read 0x407150 1` to verify 0xC3 is present at runtime. Similarly check 0x46C194 and 0x60CE20 for 0x90 bytes.
2. **Trace light draw dispatch** -- `livetools trace 0x60CE42 --count 20` to confirm lights are being dispatched through vtable[6] (LightVolume::Draw at 0x6124E0).
3. **Sector iteration count** -- `livetools trace 0x46C1A3 --count 50` to verify all 8 sectors are being processed per frame (should see 8 hits per frame traversal).
4. **Draw call volume** -- `livetools dipcnt on` then `dipcnt read` after a few seconds to confirm the high draw count (~500+ per frame) matches expectations for full-scene rendering with culling disabled.

## Build 031 Analysis: Light Culling Pipeline Deep Dive -- 2026-03-27

### Summary

Light disappearance when Lara walks far from the stage is caused by **two additional culling gates upstream** of the frustum plane test we already patched (0x60CE20). The full light visibility pipeline has **three independent kill points**, and build 030 only patched one of them:

1. **`LightVisibilityCheck` (0x60B050)** -- thiscall, mode-dependent AABB test. Called at 0x60CDDB. If it returns 0, the light is skipped entirely (je at 0x60CDE2) -- never even reaches the frustum test. This is the **primary culprit** for lights disappearing at distance.
2. **Frustum plane test (0x60CDF1-0x60CE2D)** -- 6-plane dot product loop. Already patched (JNP at 0x60CE20 NOPed).
3. **`RenderLights_Caller` gate (0x60E345-0x60E354)** -- checks `[this+0x1B0]` (light count). If zero, `byte [esp+0x17]` is set to 0 and the entire RenderLights_FrustumCull call is skipped via JE at 0x60E3B1. This is a structural gate, not a culling issue.

The `ret 4` patch at 0x60B050 (`mov al, 1; ret 4`) is correct in calling convention -- confirmed thiscall with 1 stack arg. However, **it must be applied at runtime**, not on-disk, because the on-disk bytes are the original function.

### Key Addresses

| Address | Description |
|---------|-------------|
| 0x60B050 | `LightVisibilityCheck` -- thiscall, 1 stack arg (lightData ptr). Returns bool in AL. Mode switch on `[this+0x74]->[+0x448]`: 0=spotlight(always pass), 1=call 0x60AD20, 2=call 0x60AC80+0x5F9BE0 |
| 0x60CDDB | Call site of LightVisibilityCheck inside RenderLights_FrustumCull |
| 0x60CDE2 | `je 0x60CE45` -- **CULL GATE**: skips light if LightVisibilityCheck returns 0 (2 bytes: 74 61) |
| 0x60CE20 | `jnp 0x60CFB3` -- frustum plane rejection (already NOPed in build 030) |
| 0x60CE42 | `call [eax+0x18]` -- LightVolume::Draw dispatch (lights that pass all tests) |
| 0x60E345 | Light count check: `[this+0x1B0]` > 0 sets byte[esp+0x17]=1, enabling RenderLights_FrustumCull |
| 0x60E3B1 | `je 0x60E4B6` -- skips entire RenderLights_FrustumCull if no lights in sector list |
| 0x60AD20 | Subroutine called for spotlight visibility (mode 0 path) |
| 0x60AC80 | Subroutine called for point/directional visibility (mode 1/2, computes AABB) |
| 0x5F9BE0 | AABB intersection test called after 0x60AC80 |
| 0x5F9A60 | Alternate AABB test for mode 2 (directional lights) |

### Details

#### LightVisibilityCheck (0x60B050) -- Full Analysis

**Calling convention:** `__thiscall` with `ecx` = LightGroup object, 1 stack arg = lightData pointer. Returns `bool` in AL. Confirmed by `funcinfo`: all 4 ret instructions use `ret 4`.

**On-disk bytes at entry:**
```asm
0x0060B050: push     ebp               ; 55
0x0060B051: mov      ebp, esp          ; 8B EC
0x0060B053: and      esp, 0xfffffff0   ; 83 E4 F0
0x0060B056: sub      esp, 0x3c         ; 83 EC 3C
0x0060B059: push     esi               ; 56
0x0060B05A: mov      esi, ecx          ; 8B F1  (this -> esi)
0x0060B05C: mov      ecx, [esi+0x74]   ; 8B 4E 74
0x0060B05F: mov      eax, [ecx+0x448]  ; 8B 81 48 04 00 00
0x0060B065: sub      eax, 0            ; 83 E8 00 (mode switch)
0x0060B068: je       0x60B159          ; mode 0 -> spotlight path
0x0060B06E: dec      eax               ; 48
0x0060B06F: je       0x60B0E8          ; mode 1 -> pointlight path
0x0060B071: dec      eax               ; 48
0x0060B072: je       0x60B07D          ; mode 2 -> directional path
0x0060B074: mov      al, 1             ; default: return true
0x0060B076: pop      esi
0x0060B077: mov      esp, ebp
0x0060B079: pop      ebp
0x0060B07A: ret      4
```

**Mode dispatch:**
- Mode 0 (spotlight): Calls `0x60AD20` -- simple test, likely always passes for game's spot lights
- Mode 1 (pointlight): Calls `0x60AC80` (compute AABB from radius * scaling) then `0x5F9BE0` (AABB intersection test). This is the **distance-dependent** test that rejects lights when Lara is far away
- Mode 2 (directional): Calls `0x60AC80` then `0x5F9A60` (different AABB test)
- Default (mode >= 3): Returns `AL=1` (always visible)

**The AABB tests at 0x5F9BE0/0x5F9A60 compare the light's bounding volume against the camera's current view AABB.** When Lara walks far from the stage, the light volumes no longer intersect the camera AABB, so `LightVisibilityCheck` returns 0 and the light is skipped at 0x60CDE2 BEFORE even reaching the frustum plane test.

#### Caller Site Context (0x60CDBB-0x60CE54)

```asm
; Light iteration loop
0x60CDC0: mov  eax, [ebx+0x1B8]       ; light object array pointer
0x60CDC6: mov  ecx, [esp+0x28]        ; loop counter (light index)
0x60CDCA: mov  esi, [eax+ecx*4]       ; esi = LightVolume* for this light
0x60CDCD: mov  edx, [esi]             ; edx = vtable
0x60CDCF: mov  ecx, esi               ; ecx = this (for thiscall)
0x60CDD1: call [edx+0x14]             ; vtable[5] = GetBoundingSphere -> eax = sphere data
0x60CDD4: mov  ecx, [esp+0x2C]        ; ecx = LightGroup this (for LightVisibilityCheck thiscall)
0x60CDD8: mov  ebx, eax               ; ebx = bounding sphere result
0x60CDDA: push ebx                    ; arg1 = bounding sphere data
0x60CDDB: call 0x60B050               ; LightVisibilityCheck(this=LightGroup, lightData=sphere)
0x60CDE0: test al, al
0x60CDE2: je   0x60CE45               ; *** SKIP LIGHT IF VISIBILITY CHECK FAILS ***
; --- if passed, proceed to 6-plane frustum test ---
0x60CDE4: movaps xmm1, [ebx+0x10]    ; load bounding sphere
; ... 6-plane loop at 0x60CDF1-0x60CE2D ...
; ... if all 6 pass: call [eax+0x18] (Draw) at 0x60CE42 ...

; Loop increment
0x60CE45: mov  eax, [esp+0x28]        ; current index
0x60CE4D: inc  eax                    ; next light
0x60CE4E: cmp  eax, ecx              ; compare with total count
0x60CE54: jb   0x60CDBB              ; continue loop
```

#### RenderLights_Caller (0x60E2D0) -- Upstream Gate

The `byte [esp+0x17]` flag controls whether `RenderLights_FrustumCull` runs at all:

```asm
0x60E345: mov  eax, [ebx+0x1B0]       ; light count
0x60E34B: test eax, eax
0x60E34D: mov  byte [esp+0x17], 1     ; assume lights exist
0x60E352: jne  0x60E359               ; if count > 0, proceed
0x60E354: mov  byte [esp+0x17], 0     ; no lights -> skip
; ...
0x60E3AB: mov  al, [esp+0x17]
0x60E3AF: test al, al
0x60E3B1: je   0x60E4B6               ; SKIP RenderLights_FrustumCull entirely
0x60E3B7: mov  ecx, ebx
0x60E3B9: call 0x60C7D0              ; RenderLights_FrustumCull
```

This gate is NOT a culling issue -- it simply checks whether the sector has any lights at all. If `[this+0x1B0]` is 0, there are no lights to process. This is correct behavior.

The additional prerequisite flags at 0x60E30D-0x60E33A check:
- `[this+0x84]` != 0 (light data pointer exists)
- `[g_pEngineRoot+0x166]` != 0 (global light enable)
- `[renderer+0x444] & 1` (renderer light capability bit)
- `[this+0x150]` != 0 (light setup pointer)

If any of these are false, no lights render regardless of our patches.

#### RenderLights_FrustumCull (0x60C7D0) -- Frustum Setup

The decompilation confirms the function:
1. Reads global `[0x1392E18]` (g_pEngineRoot) to get renderer at `+0x214`
2. Checks light type at `[ecx+0x448]` -- if type == 2, uses different frustum plane setup
3. Builds 6 frustum planes from camera FOV/orientation (calls `0x5DDC70` four times for plane construction, then `0x5F8410` six times for plane normalization)
4. Iterates `[ebx+0x1B0]` lights, calling `LightVisibilityCheck` then the 6-plane test
5. Lights passing: drawn immediately via `call [eax+0x18]` (mode=1)
6. Lights failing frustum: deferred to `g_deferredLightIndices` (0x13107FC), drawn later with mode=0

**Global state at 0x60CD58:** On first call, initializes `g_deferredLightInitFlag` (0x1310800) bit 0, zeroes the deferred light count/capacity/pointer, and registers cleanup callback at `0xEFC3B0`.

### Root Cause Analysis

The build 030 failure (lights disappear when Lara walks away) is caused by `LightVisibilityCheck` at 0x60B050 returning 0 for distant lights. The JE at 0x60CDE2 then skips the light before it ever reaches the frustum plane test (which we already NOPed).

**Fix for build 031:** The proxy must patch `LightVisibilityCheck` to always return true. Two approaches:

**Option A (function entry patch):** Write `B0 01 C2 04 00` at 0x60B050 (`mov al, 1; ret 4`). This is 5 bytes, overwriting `push ebp; mov ebp, esp` (5 bytes). Clean and minimal.

**Option B (call-site patch):** NOP the JE at 0x60CDE2 (2 bytes: `74 61` -> `90 90`). This skips the visibility check result but still calls the function (wasted cycles). Less clean but equally effective.

**Option A is preferred** because it prevents the function from executing at all (no wasted AABB computations).

**IMPORTANT: The `ret 4` patch is confirmed correct.** All 4 return paths use `ret 4` (stdcall/thiscall with 1 stack arg). The proxy's existing patch `B0 01 C2 04 00` (`mov al, 1; ret 4`) matches the calling convention exactly.

### Potential Additional Issue: Sector Light List

The gate at 0x60E345 checks `[this+0x1B0]` for the light count. This is the **per-sector** light list. If lights are only associated with the sector containing the stage, Lara walking to a different sector would result in `[this+0x1B0] == 0` for that sector, and the entire `RenderLights_FrustumCull` is skipped.

**This is a sector-boundary issue, not a culling issue.** The lights exist in sector N's light list. When Lara enters sector M, the render call for sector M has no lights, so `byte [esp+0x17] = 0` and `RenderLights_FrustumCull` never runs.

**To verify:** Use `livetools trace 0x60E345 --count 20 --read "[ebx+0x1B0]:4:uint32"` while Lara walks between positions. If the light count drops to 0 at distant positions, the sector light list is the root cause, not LightVisibilityCheck.

**If the sector light list IS the issue**, patching `LightVisibilityCheck` alone won't help -- the lights never reach that code path. The fix would need to either:
1. Force all sectors to include the stage's light list
2. Patch the gate at 0x60E354 to always set `byte [esp+0x17] = 1` and provide a fallback light list
3. Find where sector light lists are built and ensure all lights are added globally

### Suggested Live Verification

1. **Confirm LightVisibilityCheck patch is active:** `livetools mem read 0x60B050 5` -- should be `B0 01 C2 04 00`
2. **Trace the sector light count gate:** `livetools trace 0x60E345 --count 30 --read "[ebx+0x1B0]:4:uint32"` -- compare values at baseline vs distant position
3. **Trace LightVisibilityCheck calls:** `livetools collect 0x60CDDB --duration 5` -- at baseline position, should see hits. At distant position, if sector light list is empty, there will be zero hits (the function is never called)
4. **Trace RenderLights_FrustumCull entry:** `livetools collect 0x60C7D0 --duration 5` -- zero hits at distant position = sector gate is the culprit
5. **Read sector light count at runtime:** At the `this` pointer for the current light group, read `[this+0x1B0]` to see if it changes with position
6. **If sector light list is confirmed as issue:** Need to find the function that populates `[lightGroup+0x1B0]` and `[lightGroup+0x1B8]` -- use `livetools memwatch 0x60E345` or `datarefs` on the light count field

## Patch Site Verification (Static) -- 2026-03-27

### Summary

All proxy patch sites verified against the on-disk `trl.exe` binary. Every address targets the expected original instruction. The proxy log confirms all 7 cull NOPs, the frustum RET patch, 2 sector visibility NOPs, cull mode global stamps, light frustum rejection NOP, Light_VisibilityTest forced-TRUE, and sector light count gate NOP all applied successfully.

### Key Addresses

| Address | Original Instruction | Patch Action | Status |
|---------|---------------------|-------------|--------|
| 0x407150 | `push ebp` (0x55) | Overwrite with `ret` (0xC3) | VERIFIED -- function prologue present on-disk |
| 0x4072BD | `jne 0x4078CD` (6-byte near jump) | NOP x6 | VERIFIED -- distance cull jump 1 |
| 0x4072D2 | `jne 0x4078CD` (6-byte near jump) | NOP x6 | VERIFIED -- distance cull jump 2 |
| 0x407AF1 | `jnp 0x40804E` (6-byte near jump) | NOP x6 | VERIFIED -- distance cull jump 3 |
| 0x407B30 | `jne 0x40804E` (6-byte near jump) | NOP x6 | VERIFIED -- screen boundary jump 1 |
| 0x407B49 | `jne 0x40804E` (6-byte near jump) | NOP x6 | VERIFIED -- screen boundary jump 2 |
| 0x407B62 | `jp 0x40804E` (6-byte near jump) | NOP x6 | VERIFIED -- screen boundary jump 3 |
| 0x407B7B | `jp 0x40804E` (6-byte near jump) | NOP x6 | VERIFIED -- screen boundary jump 4 |
| 0x46C194 | `je 0x46C2B5` (6-byte near jump) | NOP x6 | VERIFIED -- sector visibility check 1 |
| 0x46C19D | `jne 0x46C2B5` (6-byte near jump) | NOP x6 | VERIFIED -- sector visibility check 2 |
| 0x60B050 | `push ebp` (0x55) | `mov al,1; ret 4` (B0 01 C2 04 00) | VERIFIED -- Light_VisibilityTest prologue present |
| 0x60CE20 | `jnp 0x60CFB3` (6-byte near jump) | NOP x6 | VERIFIED -- light frustum rejection in RenderLights_FrustumCull |
| 0xEC6337 | `je 0xEC6341` (2-byte short jump) | NOP x2 | VERIFIED -- sector light count gate |

### Details

**0x407150 (SceneTraversal_CullAndSubmit frustum cull)**
On-disk bytes start with `push ebp; mov ebp, esp; and esp, 0xFFFFFFF0; sub esp, 0x1E4` -- standard function prologue for a large stack-frame function. The proxy overwrites byte 0 with 0xC3 (RET). Since the function is cdecl and the caller cleans the stack, a bare RET is safe. The function body spans ~4KB and performs frustum plane tests.

**0x4070F0 region (requested disassembly)**
The range 0x4070F0-0x407107 is the tail of a *different* function that ends with `ret` at 0x407107, followed by INT3 padding (0x407108-0x40710F), then another small function at 0x407110. This confirms 0x407150 is a separate function entry point (the SceneTraversal cull function), not within the 0x4070F0 function.

**7 cull jumps inside SceneTraversal (0x4072BD-0x407B7B)**
All 7 addresses contain 6-byte conditional near jumps (opcode 0F 8x). The first two (0x4072BD, 0x4072D2) jump to 0x4078CD (distance-based skip). The remaining five (0x407AF1-0x407B7B) jump to 0x40804E (screen boundary skip). All are correctly targeted for 6-byte NOP replacement.

**Sector visibility checks (0x46C194, 0x46C19D)**
Both are 6-byte conditional jumps to 0x46C2B5 (skip sector rendering). The first is JE (skip if sector type byte is zero), the second is JNE (skip if visibility bit not set). NOPing both forces all sectors to render every frame.

**Light patches (0x60B050, 0x60CE20, 0xEC6337)**
- 0x60B050 (Light_VisibilityTest): Original prologue `push ebp; mov ebp, esp` confirms a __thiscall function. Proxy replaces first 5 bytes with `mov al, 1; ret 4`, forcing all lights visible.
- 0x60CE20 (RenderLights_FrustumCull JNP): A 6-byte `jnp 0x60CFB3` that skips lights failing the 6-plane frustum test. NOPed to keep all lights.
- 0xEC6337 (sector light count gate): A 2-byte `je 0xEC6341` that skips loading light count when sector visibility is zero. The JE jumps to `xor eax, eax` (light count = 0); falling through reaches `mov eax, [ebx+0x664]` (loads actual light count). NOP forces the fall-through path, always loading the real count.

**Proxy log confirmation**
The log reports all patches applied successfully: "NOPed cull jumps: 7/7", "Patched frustum cull function to ret (0x407150)", "NOPed sector visibility checks: 2/2", "Patched cull mode globals to D3DCULL_NONE", "NOPed light frustum rejection at 0x0060CE20", "Patched Light_VisibilityTest to always return TRUE (0x60B050)", "NOPed sector light count gate at 0x00EC6337".

### Suggested Live Verification

1. **Confirm RET patch at runtime:** `livetools mem read 0x407150 1` -- should be `C3`
2. **Confirm NOP patches at runtime:** `livetools mem read 0x4072BD 6` -- should be `90 90 90 90 90 90`
3. **Confirm Light_VisibilityTest patch:** `livetools mem read 0x60B050 5` -- should be `B0 01 C2 04 00`
4. **Confirm sector light gate NOP:** `livetools mem read 0xEC6337 2` -- should be `90 90`

## Full SceneTraversal_CullAndSubmit (0x407150) Disassembly Analysis -- 2026-03-27

### Summary

Complete manual analysis of `SceneTraversal_CullAndSubmit` from disassembly (0x407150-0x40808C, 3901 bytes). The function contains **two major processing loops** with distinct culling logic, and the current RET patch at 0x407150 kills the entire function including BOTH loops. This analysis identifies every conditional branch that rejects geometry without submitting it, which branches are already NOPed, and which ones remain unpatched.

**Critical finding**: The current approach patches 0x407150 to `RET`, which prevents the function from executing at all. This means the 7 NOP patches inside the function body (0x4072BD, 0x4072D2, 0x407AF1, 0x407B30, 0x407B49, 0x407B62, 0x407B7B) are IRRELEVANT when the RET patch is active -- they never execute. If the plan is to remove the RET patch and let the function run (so it actually submits geometry), then ALL cull branches below must be addressed.

### Function Architecture

The function has two sequential processing phases:

**Phase A: Scene Graph Node Loop** (0x4071C2-0x4078D9)
- Entry: `ebx = [arg+0x24]` (head of linked list)
- Iteration: `ebx = [ebx+4]` (next pointer at +4)
- Termination: `ebx == 0` at 0x4071BC/0x4078D3
- Processes oriented bounding box (OBB) nodes
- Calls: `FrustumCull_ExpandBounds` (0x406EF0), `SubmitBillboard` (0x406240), `SubmitMesh_Generic` (0x604BE0)

**Phase B: Renderable Object Loop** (0x407940-0x40805E)
- Entry: `esi = [arg+0x2C]` (head of linked list)
- Iteration: `esi = [esi+4]` (next pointer), `ebx = [esi+4]` (linked to next)
- Termination: `esi/ebx == 0` at 0x40805A/0x40805E
- Processes individual renderable objects (sprites, meshes, billboards)
- Contains the LOD selection, draw-distance, and screen-space culling
- Calls: `SubmitAxisAlignedSprite` (0x406DA0), `SubmitRotatedSprite` (0x406ED0), `SubmitMesh_WithFlags` (0x406640), `SubmitMesh_Generic` (0x604BE0)

**Between phases** (0x4078D9-0x407940): Global render mode check and camera matrix setup.

### Complete Conditional Exit Catalog

#### Phase A Exits (Scene Graph Nodes)

| # | Address | Instruction | Condition | Effect | Currently Patched? |
|---|---------|-------------|-----------|--------|--------------------|
| A1 | 0x4071BC | `je 0x4078D9` | `[arg+0x24] == NULL` | No nodes to process, skip to Phase B | NO (not a cull -- null list guard) |
| A2 | 0x4071CE | `jne 0x4078CD` | `[ebx+8] & 0x10` | Node flag bit 4 set = skip this node (disabled/hidden flag) | NO -- **object disable flag, should NOP** |
| A3 | 0x4072BD | `jne 0x4078CD` | `screenMin.x <= g_frustumDistanceThreshold` (0xEFDD64=16.0) | Frustum X min test: object behind camera or too far left | YES (known cull jump #1) |
| A4 | 0x4072D2 | `jne 0x4078CD` | `screenMin.y <= g_frustumDistanceThreshold` (0xEFDD64=16.0) | Frustum Y min test: object below camera or too far down | YES (known cull jump #2) |

Note: A3 and A4 compare the screen-projected min coordinate against `g_frustumDistanceThreshold` (0xEFDD64 = 16.0). The `-1e30` patch to this global would make the threshold so negative that the `fcomp` + `test ah, 0x41` (checks C0 or ZF -- "less than or equal") would always be false, meaning the JNE would never be taken. **The -1e30 patch IS effective for these two branches.**

Target `0x4078CD` is the "skip to next node" address: `mov ebx, [esp+0x3C]` (load next pointer).

#### Phase A -- Non-Cull Branches (inside node processing, not exits)

| Address | Instruction | Purpose |
|---------|-------------|---------|
| 0x407184 | `je 0x4071AC` | Scene flag check -- selects scale computation path |
| 0x4071D7 | `jne 0x407240` | `[ebx+8] & 0x400` -- toggles between transform-and-copy vs identity path |
| 0x407262 | `jns 0x40738D` | `[ebx+8] & 0x8000` -- selects OBB vs AABB bounding volume path |
| 0x407390 | `je 0x407596` | `[ebx+8] & 4` -- selects billboard vs mesh processing |

These are mode-selection branches, not rejection paths. They all lead to submission calls eventually.

#### Phase B Exits (Renderable Objects)

| # | Address | Instruction | Condition | Effect | Currently Patched? |
|---|---------|-------------|-----------|--------|--------------------|
| B1 | 0x407976 | `jne 0x40805A` | `[esi+8] & 0x10` | Object flag bit 4 set = skip (same disable flag as A2) | NO -- **object disable flag, should NOP** |
| B2 | 0x407A1F | `je 0x40805A` | `edi == 0` (LOD level count) | No valid LOD levels = skip object | NO -- **LOD rejection, should NOP** |
| B3 | 0x407AF1 | `jnp 0x40804E` | Screen-projected Z <= g_frustumDistanceThreshold (0xEFDD64) | Depth/distance cull: object beyond frustum distance | YES (known cull jump #3) |
| B4 | 0x407B06 | `je 0x40804E` | `screenZ <= g_farClipDistance` (0x10FC910) | Far clip rejection: object beyond far plane | NO -- **far clip cull, should NOP** |
| B5 | 0x407B30 | `jne 0x40804E` | `screenMinX + projectedSize < g_screenBoundsMin` (-1.0) | Left screen edge cull: object entirely off left | YES (known cull jump #4) |
| B6 | 0x407B49 | `jne 0x40804E` | `screenMinY + projectedSize < g_screenBoundsMin` (-1.0) | Bottom screen edge cull: object entirely off bottom | YES (known cull jump #5) |
| B7 | 0x407B62 | `jp 0x40804E` | `screenMaxX - projectedSize > g_screenBoundsMax` (1.0) | Right screen edge cull: object entirely off right | YES (known cull jump #6) |
| B8 | 0x407B7B | `jp 0x40804E` | `screenMaxY - projectedSize > g_screenBoundsMax` (1.0) | Top screen edge cull: object entirely off top | YES (known cull jump #7) |
| B9 | 0x407ABC | `je 0x40804E` | `drawDistance - objectDist <= objectDist` | Draw distance fade-out: object beyond its draw distance | NO -- **draw distance cull, should NOP** |

Target `0x40804E`: `test edi, edi; jne 0x407A25` -- decrements LOD counter and loops, or falls through to `0x40805A` which advances to next object in linked list.

Target `0x40805A`: `test ebx, ebx; mov esi, ebx; jne 0x407940` -- advance to next object.

#### Phase B -- LOD Inner Loop

Phase B has an inner LOD loop (0x407A25-0x408050). For objects with multiple LOD levels, `edi` counts down from N to 0. Each LOD iteration can be rejected by B3-B9. After all LODs processed (or all rejected), the function moves to the next object.

The LOD loop structure:
```
0x407A16: test edi, edi          ; edi = LOD count
0x407A1F: je 0x40805A            ; B2: no LODs -> skip object
0x407A25: [start LOD processing]  ; inner loop entry (edi decremented each pass)
  ...cull tests B3-B8...
  ...submit geometry if passed...
0x40804A: mov edi, [esp+0x24]    ; restore edi (LOD counter - 1)
0x40804E: test edi, edi          ; more LODs?
0x408050: jne 0x407A25           ; yes -> loop
0x408056: mov ebx, [esp+0x3C]    ; no -> next object
```

#### Additional Branch: Draw Distance with Fade (Phase B)

At 0x407A8E-0x407ACF, there is a draw-distance fade path that computes a distance-based alpha:
```asm
0x407A92: fld [esp+0x34]         ; load view-adjusted distance
0x407A96: mov eax, [esi+0x14]    ; object far extent
0x407A99: fsub [esi+0x18]        ; subtract near extent
0x407AB4: fcomp [esi+0x18]       ; compare remaining dist vs near extent
0x407AB9: test ah, 0x41          ; if remaining <= near (fully faded out)
0x407ABC: je 0x40804E            ; B9: SKIP object (draw distance rejection)
```

This is a **soft draw-distance cull**: when the camera distance exceeds the object's draw distance (defined per-object at `[esi+0x18]`/`[esi+0x14]`), the object fades out. At full fade, B9 rejects it.

### Frustum Threshold Analysis (0xEFDD64)

`g_frustumDistanceThreshold` at 0xEFDD64 is used at:
- **0x4072AF** (Phase A): `fcomp [0xEFDD64]` -- screen-projected X min vs threshold
- **0x4072C7** (Phase A): `fcomp [0xEFDD64]` -- screen-projected Y min vs threshold
- **0x407AE3** (Phase B): `fcomp [0xEFDD64]` -- screen-projected Z depth vs threshold
- **0x407BC4** (Phase B): `fcomp [0xEFDD64]` -- offset Z distance vs threshold (billboard Z offset check)

The on-disk value is 16.0. Patching to -1e30 ensures all comparisons using "less than or equal to threshold" will be false (since no screen coordinate is <= -1e30), effectively disabling these frustum distance tests.

**However**, 0xEFDD64 is in `.rdata` (read-only data). The -1e30 patch must be applied at runtime via memory write, not to the PE file. The proxy does this already.

### Geometry Submission Points

The function submits geometry through these calls:

| Address | Target | Description |
|---------|--------|-------------|
| 0x407383 | 0x406EF0 (`FrustumCull_ExpandBounds`) | Phase A: expands frustum bounds for OBB nodes |
| 0x407863 | 0x604BE0 (`SubmitMesh_Generic`) | Phase A: submits OBB-style mesh with transform |
| 0x4078C5 | 0x406240 (`SubmitBillboard`) | Phase A: submits billboard/sprite nodes |
| 0x407CB2 | 0x406ED0 (`SubmitRotatedSprite`) | Phase B: submits rotated sprite with LOD |
| 0x407CE9 | 0x406DA0 (`SubmitAxisAlignedSprite`) | Phase B: submits axis-aligned sprite with LOD |
| 0x408014 | 0x604BE0 (`SubmitMesh_Generic`) | Phase B: submits oriented mesh (rotated path) |
| 0x408042 | 0x406640 (`SubmitMesh_WithFlags`) | Phase B: submits mesh with flags (standard path) |

### UN-PATCHED Cull Branches That Need Attention

| Priority | Address | Bytes | What to Patch | Why |
|----------|---------|-------|---------------|-----|
| **HIGH** | 0x4071CE | `0F 85 F9 06 00 00` (6 bytes) | NOP 6 bytes | Object disable flag in Phase A -- could hide geometry that Remix needs |
| **HIGH** | 0x407976 | `0F 85 DE 05 00 00` (6 bytes) | NOP 6 bytes | Object disable flag in Phase B -- same as A2 but for renderable objects |
| **HIGH** | 0x407A1F | `0F 84 35 06 00 00` (6 bytes) | NOP 6 bytes | LOD count == 0 rejection -- some objects may have LODs computed to 0 |
| **MEDIUM** | 0x407B06 | `0F 84 42 05 00 00` (6 bytes) | NOP 6 bytes | Far clip distance rejection -- objects beyond far plane |
| **MEDIUM** | 0x407ABC | `0F 84 8C 05 00 00` (6 bytes) | NOP 6 bytes | Draw distance fade-out -- objects beyond per-object draw distance |

**Note on A2 (0x4071CE) and B1 (0x407976)**: These check bit 0x10 of the object's flags byte at `[node+8]`. This is likely an intentional "disabled" flag (e.g., objects that are scripted off, destroyed, or part of cutscene-only geometry). NOPing these could cause rendering of objects that should be invisible for gameplay reasons. **Recommend NOPing with caution** -- test first to see if unwanted geometry appears.

**Note on B2 (0x407A1F)**: If an object has zero valid LOD levels, submitting it would likely crash or render garbage. **DO NOT NOP this unless you're certain LOD=0 objects have valid mesh data.**

**Note on B4 (0x407B06)**: This compares against `g_farClipDistance` (0x10FC910), which is a runtime value set per level. NOPing this means objects beyond the far plane would be submitted -- probably harmless for RTX Remix since the rasterizer clips them anyway.

**Note on B9 (0x407ABC)**: This is the draw-distance fade-out. Objects beyond their per-object draw distance get alpha-faded to zero and then rejected. NOPing means fully-faded objects would still be submitted with zero alpha -- Remix might still hash them but they'd be invisible.

### Recommendation: Which Approach?

**Option 1: Keep RET at 0x407150** (current approach)
- Pros: Kills ALL culling in this function guaranteed
- Cons: Kills ALL geometry submission too -- nothing from this function reaches the renderer
- This is only viable if the geometry is submitted elsewhere (which it is NOT for this path)

**Option 2: Remove RET, NOP all cull branches**
- Remove the RET patch at 0x407150
- Keep the 7 existing NOPs (A3, A4, B3, B5, B6, B7, B8)
- Add NOPs for: B4 (0x407B06, far clip), B9 (0x407ABC, draw distance)
- Optionally NOP: A2 (0x4071CE), B1 (0x407976) for disabled-object flags
- Do NOT NOP B2 (0x407A1F) -- zero-LOD objects would crash
- Patch 0xEFDD64 to -1e30 at runtime (already done by proxy)

**Option 2 is the correct approach.** The RET patch prevents this function from ever submitting geometry, which defeats the purpose. The function needs to RUN but with all distance/frustum/screen-space rejection disabled.

### Suggested Live Verification

1. **Remove RET patch**: Instead of `C3` at 0x407150, restore original bytes `55 8B EC`
2. **Verify all existing NOPs still work** at 0x4072BD, 0x4072D2, 0x407AF1, 0x407B30, 0x407B49, 0x407B62, 0x407B7B
3. **Add new NOPs**: 0x407B06 (6 bytes), 0x407ABC (6 bytes)
4. **Trace function entry**: `livetools trace 0x407150 --count 5` to confirm it executes
5. **Trace submission calls**: `livetools collect 0x406240 0x406DA0 0x406ED0 0x406640 0x604BE0 --duration 5` to confirm geometry is being submitted


## SceneTraversal_CullAndSubmit (0x407150) -- Caller Chain Analysis -- 2026-03-27

### Summary

SceneTraversal_CullAndSubmit at 0x407150 has exactly ONE call site, forming a strict linear call chain: 0x450DE0 -> 0x450B00 -> 0x443C20 -> 0x407150. The function 0x443C20 is a thin wrapper that calls 0x407150 with one of its arguments. The real orchestration happens at 0x450B00 (the main render frame function) which calls RenderVisibleSectors at 0x46C180 BEFORE calling 0x443C20/0x407150. This means sector-level culling in 0x46C180 is a SEPARATE culling pass from the object-level culling in 0x407150. Both must be defeated to disable all culling.

### Key Addresses

| Address | Description |
|---------|-------------|
| 0x407150 | SceneTraversal_CullAndSubmit -- per-object frustum/distance culling and geometry submission |
| 0x443C20 | SceneTraversal_Wrapper -- thin wrapper, calls 0x402B10, then 0x407150, then 0x442D40 |
| 0x450B00 | RenderFrame -- main per-frame render orchestrator |
| 0x450DE0 | RenderFrame_Outer -- outermost entry, checks 0xF17904 flag, calls 0x40CA60 then 0x450B00 |
| 0x46C180 | RenderVisibleSectors -- iterates sector table, frustum-tests sectors, calls 0x46B7D0 per visible sector |
| 0x46B7D0 | RenderSector -- iterates objects within a sector, applies per-object flag/visibility filters, calls 0x40C650 to submit |
| 0x46B890 | RenderSector_Alt -- alternate sector render path (called for sector type == 2) |
| 0x40C650 | Object submission function called from 0x46B7D0 |
| 0x40D290 | Object submission function called from the post-RenderVisibleSectors loop at 0x40E3CD |
| 0x40E2C0 | Post-sector object loop -- iterates moveable objects with distance/flag culling |

### Details

#### Call Chain: 0x450DE0 -> 0x450B00 -> 0x443C20 -> 0x407150

**0x450DE0 (RenderFrame_Outer)**:
- Checks global flag at 0xF17904 (if set, calls 0x5C4070 first)
- Calls 0x40CA60 with arg=1
- Calls 0x450B00 (the main render frame function) with the frame argument
- On return, checks 0x10C0A39 and 0x10C0A38 for post-frame behavior
- Then falls through to a fade/transition controller at 0x40CBE0
- No culling decisions here -- this is a thin orchestrator.

**0x450B00 (RenderFrame) -- THE KEY FUNCTION**:

The main per-frame render function. Critical render sequence:

1. 0x4F7410() -- begin frame setup
2. 0x450430() -- frame init
3. 0x447910() -- init
4. 0x462DC0() -- init
5. 0x48E450(0x10FC660) -- viewport/scissor setup
6. Walk entity list from 0x10C5AA4 (linked list at +0x08):
   - For each entity with flag [+0xA4] & 0x800 clear:
     - Check [+0x23C] -> [+0x54] chain for attached objects
     - Call 0x531B10(entity, 0x1F/0x24/0x2A) for type-specific setup
7. Check 0x1117560 for cutscene object, if present call 0x44B7A0
8. 0x46D2D0() -- pre-sector setup
9. Check 0xF17904: if set -> 0x5C3C50(0) else -> 0x46C180(edi) *** SECTOR RENDERING
10. 0x443C20(0x107F6B8, 0x10FC780) *** SCENE TRAVERSAL (calls 0x407150)
11. 0x447950() -- post-traversal
12. 0x604BE0() -- flush/present
13. 0x5627E0(renderer, frame_data) -- finalize
14. 0x4269B0(frame_arg) -- cleanup

Critical observation: Step 9 (0x46C180 RenderVisibleSectors) and Step 10 (0x443C20 -> 0x407150 SceneTraversal) are SEPARATE passes. 0x46C180 handles sector-level visibility and submits sector geometry. 0x407150 handles scene graph objects.

**0x443C20 (SceneTraversal_Wrapper)**:
- push(arg2=renderList 0x107F6B8); call 0x402B10 -- push render list context
- push(arg1=cullData 0x10FC780); call 0x407150 -- SceneTraversal_CullAndSubmit
- push(*(0x10E5380)); call 0x442D40 -- post-traversal processing
- Check 0x1089E40 / 0x1089E44 for deferred work
- call 0x604BE0 -- flush
- call 0x446D90(0) then 0x446D90(1) -- reset state
- No culling decisions. Args are global addresses: 0x107F6B8=render list, 0x10FC780=frustum data.

#### RenderVisibleSectors (0x46C180) -- Sector-Level Culling

Sector table at 0x11582FD, entry size 0x5C (92 bytes), ends at 0x11585DD (~8 entries):

For each sector entry (esi):
- FILTER 1: byte[esi+1] & 0x08 must be set (sector visibility flag from PVS/portal system)
- FILTER 2: byte[esi+0] must be 0 (sector active flag)
- FILTER 3: dword[esi+0x3F] sector type:
  - Type 1: Compute AABB from words at esi+0x37..0x3D, convert to float -> 0x10FC900..0x10FC90C
    - FRUSTUM TEST: Compare AABB against frustum bounds at 0x10FC920, 0x10FC924
    - If AABB outside frustum -> skip entire sector
    - If passes -> call 0x48E430(0x10FC660) then 0x46B7D0(esi-5)
  - Type 2: Always renders (no frustum test!) -> call 0x48E430 then 0x46B890(esi-5)
  - Other types: skip

Post-sector: sets up viewport, calls 0x463400, then enters object loop at 0x40E2C0.

#### RenderSector (0x46B7D0) -- Per-Object Filtering Within Sectors

Once a sector passes frustum test, 0x46B7D0 iterates over its object list:
- objectArray = *(sector+8); meshBase = *objectArray
- count = objectArray[+0x14]; each entry = 0xB0 (176) bytes at meshBase[+0x18]

Per-object filters:
- FILTER A: flags[+0x20] & 0x01 -> skip (object disabled)
- FILTER B: flags[+0x20] & 0x20000 -> skip (hidden/engine-culled)
- FILTER C: flags[+0x20] & 0x200000 absent + sector != camera sector (0x10E5438) -> skip
  - This is a camera-proximity filter: objects without the 0x200000 flag only render in the camera sector
  - If flag 0x200000 IS set: applies camera-relative offset from XMMWORD at 0x10FC670

If all filters pass -> call 0x40C650(meshBase, entry) to submit geometry.

#### Post-Sector Object Loop (0x40E2C0-0x40E44D)

A secondary loop after sector rendering that processes moveable/animated objects:
- Array indexed by count at [baseArray], entries spaced 0x130 (304) bytes
- Per-object bitmask filter: global at 0xFFA718, bit = (1 << index)
- Object pointer at entry[+0x110], walks linked list via [+0x228] and [+0x22C]
- Per-object filters:
  - byte at [obj+0x94]+0x76 == 0xFF -> skip
  - flags [obj+0xA4] & 0x800 -> skip
  - flags [obj+0xA8] & 0x10000 -> skip
- Distance culling against globals: 0xEFDE58 (max draw distance), 0xEFDDB0/0xEFDE50 (LOD thresholds)
- If passes all -> call 0x40D290 to submit

### Culling Decision Summary -- Three Separate Layers

| Layer | Function | What it culls | How to defeat |
|-------|----------|---------------|---------------|
| Sector frustum | 0x46C180 | Entire sectors outside camera frustum | Patch jnp jumps at 0x46C22E and 0x46C25B (already done by proxy as sector NOPs) |
| Sector-object flags | 0x46B7D0 | Objects within sectors by flag bits | NOP jumps at 0x46B83B, 0x46B843, 0x46B84E |
| Scene traversal | 0x407150 | Objects in scene graph by frustum/distance/screen-size | NOP culling jumps (existing 7 NOPs + additional ones from prior analysis) |
| Post-sector objects | 0x40E300-0x40E44D | Moveable objects by bitmask, flags, distance | Patch bitmask at 0x40E308, flag checks, distance at 0x40E392 |

### Observations

1. Two render paths converge: The sector path (0x46C180 -> 0x46B7D0) handles static/level geometry. The scene traversal path (0x407150) handles dynamic/scene-graph objects. The post-sector loop (0x40E2C0) handles moveable/animated objects.

2. The RenderFrame function at 0x450B00 confirms 0x46C180 is called BEFORE 0x443C20/0x407150. Sector culling removing geometry is independent from scene traversal culling.

3. Global 0xF17904: When this byte is set, 0x5C3C50(0) is called instead of 0x46C180 -- this bypasses normal sector rendering entirely. Might be a debug/cutscene mode.

4. Entity pre-processing loop (0x10C5AA4 linked list): Before rendering, 0x450B00 walks entities and calls 0x531B10 with type IDs (0x1F=31, 0x24=36, 0x2A=42). These may set visibility flags checked by later render passes.

5. The 0x443C20 wrapper always passes fixed globals: 0x107F6B8 and 0x10FC780 -- the render submission list and frustum/cull data respectively.

### Suggested Live Verification

1. Trace 0x46B7D0 entry: livetools trace 0x46B7D0 --count 20 (how many sectors rendered per frame)
2. Trace object skip in 0x46B7D0: livetools collect 0x46B83B 0x46B843 0x46B86D --duration 5 (objects passing vs filtered)
3. Trace 0x40E315 (post-sector object loop): livetools trace 0x40E315 --count 20 (how many post-sector objects exist)
4. Read 0xFFA718: livetools mem read 0xFFA718 4 (visibility bitmask for post-sector objects)
5. Check 0xF17904: livetools mem read 0xF17904 1 (if 1, 0x46C180 is never called -- sector bypass flag)
6. Patch 0x46B7D0 object filters: NOP jumps at 0x46B83B (je -> nop6), 0x46B843 (je -> nop6) to force all sector objects to render

## Exact Byte Patterns for Culling Patches — 2026-03-27

### Summary

Disassembled three patch sites to extract exact opcode bytes for all conditional jumps that skip object rendering. These are the precise bytes needed for NOP/JMP patching in the ASI or proxy.

### Region 1: Per-Object Flag Checks (0x46B830 loop body)

This is the inner loop of the sector object iterator at 0x46B7D0. Each iteration loads object flags from `[eax+edi+0x20]` and applies three filter tests. All skip jumps go to 0x46B877 (loop increment — object skipped).

| Address | Bytes | Size | Instruction | Effect |
|---------|-------|------|-------------|--------|
| 0x46B83C | `75 39` | 2 | `jne 0x46B877` | Skip if flag bit 0 set (bit 0 of flags = "hidden/inactive") |
| 0x46B844 | `75 31` | 2 | `jne 0x46B877` | Skip if flag bit 17 set (0x20000 = "don't render" flag) |
| 0x46B84C | `74 1F` | 2 | `je 0x46B86D` | If bit 21 NOT set (0x200000), skip the camera-sector check — jump TO rendering. This is the OPPOSITE direction: it jumps PAST the sector check to submit the object. |
| 0x46B85A | `75 1B` | 2 | `jne 0x46B877` | Skip if object's sector != camera sector (sector mismatch cull) |

**Patch plan for Region 1:**
- NOP `0x46B83C` (2 bytes: `75 39` -> `90 90`) — force-render hidden objects
- NOP `0x46B844` (2 bytes: `75 31` -> `90 90`) — force-render "don't render" flagged objects
- NOP `0x46B85A` (2 bytes: `75 1B` -> `90 90`) — disable sector mismatch cull
- Leave `0x46B84C` alone — its jump ENABLES rendering (skips the sector check for objects without the 0x200000 flag)

**After patching:** All objects reach `call 0x40C650` at 0x46B86F (the object submission call), regardless of flags or sector.

### Region 2: Post-Sector Object Filters (0x40E300 loop body)

This is a separate object loop that processes post-sector objects (portals, effects, sector-boundary objects). It has an early bitmask gate, then three flag-based skips that jump to 0x40E40E (the loop continuation — object skipped), plus a distance-based LOD cull.

| Address | Bytes | Size | Instruction | Effect |
|---------|-------|------|-------------|--------|
| 0x40E30F | `0F 84 38 01 00 00` | 6 | `je 0x40E44D` | Skip entire sector if bit not set in visibility bitmask at 0xFFA718. **This exits the whole sector, not just one object.** |
| 0x40E31D | `0F 84 2A 01 00 00` | 6 | `je 0x40E44D` | Skip if object list pointer is NULL (safety check — leave this one) |
| 0x40E33A | `0F 84 CE 00 00 00` | 6 | `je 0x40E40E` | Skip if `[obj+0x94]+0x76 == 0xFF` (sector index = unassigned) |
| 0x40E349 | `0F 85 BF 00 00 00` | 6 | `jne 0x40E40E` | Skip if flag bit 11 set in `[obj+0xA4]` (ch & 8 = bit 11 of dword) |
| 0x40E359 | `0F 85 AF 00 00 00` | 6 | `jne 0x40E40E` | Skip if flag bit 16 set in `[obj+0xA8]` (0x10000) |
| 0x40E3B0 | `75 5A` | 2 | `jne 0x40E40C` | Distance cull — skip if object too far (FPU comparison fails) |

**Patch plan for Region 2:**
- NOP `0x40E30F` (6 bytes: `0F 84 38 01 00 00` -> `90 90 90 90 90 90`) — disable sector visibility bitmask gate
- Leave `0x40E31D` — NULL pointer safety check, must keep
- NOP `0x40E33A` (6 bytes -> `90 90 90 90 90 90`) — render objects with unassigned sector
- NOP `0x40E349` (6 bytes -> `90 90 90 90 90 90`) — ignore "don't render" flag bit 11
- NOP `0x40E359` (6 bytes -> `90 90 90 90 90 90`) — ignore flag bit 16
- NOP `0x40E3B0` (2 bytes: `75 5A` -> `90 90`) — disable distance cull

### Key Addresses

| Address | Description |
|---------|-------------|
| 0x46B830 | Sector object loop body start — loads flags |
| 0x46B83C | Flag bit 0 skip (2-byte jne) |
| 0x46B844 | Flag bit 17 skip (2-byte jne) |
| 0x46B84C | Flag bit 21 check — jumps TO render, do NOT patch |
| 0x46B85A | Sector mismatch skip (2-byte jne) |
| 0x46B86F | Object submission call (0x40C650) — render target |
| 0x46B877 | Loop increment (skip target) |
| 0x40E30F | Sector visibility bitmask gate (6-byte je) |
| 0x40E31D | NULL pointer guard (6-byte je) — keep |
| 0x40E33A | Unassigned sector skip (6-byte je) |
| 0x40E349 | Flag bit 11 skip (6-byte jne) |
| 0x40E359 | Flag bit 16 skip (6-byte jne) |
| 0x40E3B0 | Distance cull (2-byte jne) |
| 0x40E402 | Object render call (0x40D290) — render target for Region 2 |

### Patch Byte Summary (copy-paste ready)

```
# Region 1 — sector object flag checks (all 2-byte)
0x46B83C: 75 39 -> 90 90   # disable hidden flag cull
0x46B844: 75 31 -> 90 90   # disable "don't render" flag cull
0x46B85A: 75 1B -> 90 90   # disable sector mismatch cull

# Region 2 — post-sector object filters (mix of 6-byte and 2-byte)
0x40E30F: 0F 84 38 01 00 00 -> 90 90 90 90 90 90  # disable visibility bitmask gate
0x40E33A: 0F 84 CE 00 00 00 -> 90 90 90 90 90 90  # render unassigned-sector objects
0x40E349: 0F 85 BF 00 00 00 -> 90 90 90 90 90 90  # disable flag bit 11 cull
0x40E359: 0F 85 AF 00 00 00 -> 90 90 90 90 90 90  # disable flag bit 16 cull
0x40E3B0: 75 5A -> 90 90                            # disable distance cull
```

### Suggested Live Verification

1. `livetools collect 0x46B83C 0x46B844 0x46B85A 0x46B86F --duration 5` — measure how many objects hit each filter vs. reaching the render call BEFORE patching
2. Apply patches with `livetools mem write` and re-collect to confirm more objects reach 0x46B86F
3. `livetools collect 0x40E30F 0x40E33A 0x40E349 0x40E359 0x40E3B0 0x40E402 --duration 5` — same for Region 2

---

## Crash Analysis: Access Violation at 0x4071E2 in SceneTraversal function (0x407150)

### Summary

The crash at `0x004071E2` is a NULL pointer dereference. The instruction `mov [edx+0x40], eax` writes to address `0x00000040` because EDX is zero. EDX is loaded from `[ebx+0x8C]` at `0x4071D9` — this is a sub-object pointer (likely a render/transform context) within a scene node. The scene node (EBX) itself passes its null check, but its `+0x8C` field is NULL, meaning the node exists but lacks an associated render context. The original `RET` patch at `0x407150` prevented this crash by skipping the entire function. ESI=0 in the register dump is incidental — ESI is not involved at the crash site; it was last set during function prologue (`push esi`).

### Key Addresses

| Address | Description |
|---------|-------------|
| 0x407150 | Function entry — `SceneTraversal_CullAndSubmit` (scene node linked-list processor) |
| 0x4071B4 | Load first node: `ebx = [arg+0x24]` (scene node linked list head) |
| 0x4071BA | Null check on EBX (node pointer) — `test ebx, ebx; je 0x4078D9` |
| 0x4071D9 | `mov edx, [ebx+0x8C]` — loads sub-object/render-context pointer |
| 0x4071E2 | **CRASH**: `mov [edx+0x40], eax` — writes node position X to render context |
| 0x4071E5 | `mov ecx, [ebx+0x8C]` — same field, writes Y to `[ecx+0x44]` |
| 0x4071F1 | `mov eax, [ebx+0x8C]` — writes Z to `[eax+0x48]` |
| 0x407203 | Writes `1.0f` (0x3F800000) to `[edx+0x4C]` — W component |
| 0x4078CD | Loop-back: `ebx = [esp+0x3C]` (next node via `[ebx+4]`), loops to 0x4071C2 |
| 0x4078D9 | Exit path when node list is exhausted (ebx == NULL) |
| 0x443C30 | Only caller of 0x407150 |

### Details

**Function structure at 0x407150:**

The function processes two linked lists from the input argument:
1. **First loop** (0x4071B4–0x4078CD): Walks `[arg+0x24]` linked list via `[node+0x4]` (next pointer). For each node:
   - Checks `[node+0x8]` flags: bit 4 (`test al, 0x10`) skips to next node; bit 10 (`test ah, 4`) takes alternate path
   - Copies position from `[node+0x10..0x18]` (XYZ) into `[node+0x8C]+0x40..0x4C` (render context position + W=1.0)
   - Calls matrix transform at 0x5DD910 and submit at 0x402B10
2. **Second loop** (0x407932+): Walks `[arg+0x2C]` linked list via `[node+0x4]` into ESI. This is where ESI=0 originates if the second list is empty.

**Crash path (first loop):**
```
0x4071B4: eax = [ebp+8]           // arg pointer
0x4071B7: ebx = [eax+0x24]        // first node in linked list
0x4071BA: test ebx, ebx           // null check on node
0x4071BC: je 0x4078D9             // bail if null — OK
0x4071C2: eax = [ebx+8]           // node flags
0x4071C5: test al, 0x10           // flag bit 4
0x4071CE: jne 0x4078CD            // skip this node (goes to next)
0x4071D4: test ah, 4              // flag bit 10
0x4071D7: jne 0x407240            // alternate processing
0x4071D9: edx = [ebx+0x8C]       // <-- render context pointer (CAN BE NULL)
0x4071E2: [edx+0x40] = eax       // <-- CRASH: edx=0, writes to 0x40
```

**The bug**: There is NO null check on `[ebx+0x8C]` between 0x4071D9 and 0x4071E2. The node exists and passes the EBX null check, and its flags do not trigger the skip paths, but its render context at offset +0x8C is NULL. This happens when a scene node is in the traversal list but hasn't been fully initialized (no render context allocated).

**Scene node layout (partial, from EBX):**
```c
struct SceneNode {
    /* +0x00 */  void* vtable_or_type;
    /* +0x04 */  SceneNode* next;        // linked list next pointer
    /* +0x08 */  uint32_t flags;         // bit 4 = skip, bit 10 = alt path
    /* +0x0C */  uint16_t id;            // passed to render submit
    /* +0x10 */  float posX;
    /* +0x14 */  float posY;
    /* +0x18 */  float posZ;
    // ...
    /* +0x2E */  uint16_t renderFlags;   // used in second loop
    /* +0x2F */  uint8_t extraFlags;
    /* +0x3C */  uint32_t sortKey;
    // ...
    /* +0x8C */  RenderContext* ctx;      // CAN BE NULL — crash source
    /* +0xB7 */  uint8_t lodLevel;
};
```

**RenderContext layout (at [node+0x8C]):**
```c
struct RenderContext {
    // ...
    /* +0x10 */  float matrix[16];       // 4x4 transform matrix
    /* +0x40 */  float posX;             // written from node position
    /* +0x44 */  float posY;
    /* +0x48 */  float posZ;
    /* +0x4C */  float posW;             // always 1.0f
};
```

### Why the RET Patch at 0x407150 Prevented This

Putting `RET` at the function entry (`0xC3` at 0x407150) skipped the entire function, so neither linked list was ever traversed. No nodes were processed, no `[ebx+0x8C]` was ever dereferenced. This eliminated the crash but also eliminated all scene traversal/culling/submission — which is the desired behavior for disabling culling in RTX Remix.

### Fix Options

**Option A: Restore the RET patch at 0x407150** (safest, recommended)
- `0x407150: C3` — single-byte RET, skips entire function
- Pros: Completely prevents the crash, fully disables culling (desired for Remix)
- Cons: None for the RTX Remix use case — this function performs frustum culling and scene submission that Remix handles differently

**Option B: Add a null check before 0x4071E2** (surgical)
- Patch at 0x4071D9: if `[ebx+0x8C]` is NULL, jump to the next-node path (0x4078CD)
- This would require a code cave or expanding the instruction sequence
- Implementation: Replace `0x4071D9: mov edx, [ebx+0x8C]` with a jump to a code cave that does:
  ```asm
  mov edx, [ebx+0x8C]
  test edx, edx
  jz 0x4078CD          ; skip to next node if no render context
  jmp 0x4071DF         ; continue normal path
  ```
- Pros: Keeps the function running (partial culling still active)
- Cons: More complex, and we want culling disabled anyway

**Option C: NOP the write instructions** (fragile)
- NOP out 0x4071E2-0x40720A (the position copy block)
- Cons: The function continues and likely crashes elsewhere accessing the same null pointer

**Recommendation**: Option A (RET at 0x407150). Since the goal is to disable frustum culling for RTX Remix, there is no benefit to keeping this function active. The RET patch was correct and should be restored.

### Suggested Live Verification

1. Apply the RET patch: `livetools mem write 0x407150 C3` — verify no crash
2. Confirm culling is disabled: walk around the scene and check that geometry remains visible from all angles
3. If the function must remain active for any reason, use Option B with a code cave — verify with `livetools trace 0x4071D9 --count 100 --read "[ebx+0x8C]:4:hex"` to see how often the render context is NULL

## Code Cave Search for Null-Check Trampoline at 0x4071D9 -- 2026-03-27

### Summary

Confirmed the instruction encoding at the patch site and identified two usable code caves in the .text section. The 6-byte `mov edx, [ebx+0x8C]` at 0x4071D9 is confirmed. The nearest cave (15 bytes at 0x408121) is too small for the 20-byte trampoline. Two 29-30 byte caves exist at 0xEDF9E3 and 0xEE2602, both within .text and reachable via rel32 JMP.

### Key Addresses

| Address | Description |
|---------|-------------|
| 0x4071D9 | `mov edx, [ebx+0x8C]` -- 6 bytes (8B 93 8C 00 00 00), loads RenderContext* |
| 0x4071DF | `mov eax, [ebx+0x10]` -- 3 bytes (8B 43 10), first field store source |
| 0x4071E2 | `mov [edx+0x40], eax` -- 3 bytes (89 42 40), **CRASH SITE** when edx=NULL |
| 0x4071E5 | `mov ecx, [ebx+0x8C]` -- next instruction, safe return point |
| 0x4078CD | `mov ebx, [esp+0x3C]` -- skip-node target (loads next node, loops via jne 0x4071C2) |
| 0x408121 | 15-byte INT3 cave (too small -- need 20 bytes) |
| 0xEDF9E3 | **29-byte INT3 cave** (0xEDF9E3-0xEDF9FF), usable |
| 0xEE2602 | **30-byte INT3 cave** (0xEE2602-0xEE261F), usable |

### Details

#### Patch Site Byte Layout
```
0x4071D9: 8B 93 8C 00 00 00   mov edx, [ebx+0x8C]    ; RenderContext*
0x4071DF: 8B 43 10            mov eax, [ebx+0x10]     ; field value
0x4071E2: 89 42 40            mov [edx+0x40], eax     ; CRASH when edx=0
0x4071E5: 8B 8B 8C 00 00 00   mov ecx, [ebx+0x8C]    ; reloads same ptr
0x4071EB: 8B 53 14            mov edx, [ebx+0x14]
0x4071EE: 89 51 44            mov [ecx+0x44], edx
```

Pattern: the function repeatedly loads `[ebx+0x8C]` into a register and stores `[ebx+0xNN]` through it at offsets +0x40, +0x44, +0x48, etc.

#### Skip Target at 0x4078CD
```
0x4078CD: mov ebx, [esp+0x3C]   ; load next scene node
0x4078D1: test ebx, ebx
0x4078D3: jne 0x4071C2          ; loop back if more nodes
```
This is confirmed as the "skip this node and continue iteration" point.

#### Code Cave Analysis

**Cave 1: 0x408121 (15 bytes)** -- immediately after current function (0x407150-0x408120). Only 15 bytes of CC padding before next function at 0x408130. Too small for a 20-byte trampoline.

**Cave 2: 0xEDF9E3 (29 bytes)** -- between two functions in .text tail. Raw: `C3 CC CC...CC CC 55` (ret, 29 CCs, push ebp). Plenty of room.

**Cave 3: 0xEE2602 (30 bytes)** -- similar tail padding. 30 CCs available.

All caves are in `.text` section (0x401000-0xEFD000, flags: CODE|EXECUTE|READ = 0x60000020).

#### Recommended Patch Plan

**Best cave: 0xEDF9E3** (29 bytes available, 20 needed).

**Patch site (0x4071D9, overwrite 6 bytes):**
```
E9 XX XX XX XX 90     ; JMP 0xEDF9E3; NOP
```
Replaces: `mov edx, [ebx+0x8C]` (6 bytes)

**Trampoline at 0xEDF9E3 (20 bytes):**
```asm
; 0xEDF9E3:
mov edx, [ebx+0x8C]    ; 6 bytes (8B 93 8C 00 00 00) -- displaced original
test edx, edx          ; 2 bytes (85 D2)
jnz continue           ; 2 bytes (75 05) -- short jump over the skip-jmp
jmp 0x4078CD           ; 5 bytes (E9 XX XX XX XX) -- skip node
; continue:
jmp 0x4071DF           ; 5 bytes (E9 XX XX XX XX) -- return to original flow
```
Total: 6 + 2 + 2 + 5 + 5 = 20 bytes. Fits in 29-byte cave.

**Computed JMP offsets:**
- Patch site JMP: target=0xEDF9E3, from=0x4071D9+5=0x4071DE, offset=0xEDF9E3-0x4071DE = 0x00A98805
- Skip JMP: target=0x4078CD, from=0xEDF9EF+5=0xEDF9F4, offset=0x4078CD-0xEDF9F4 = 0xFF527ED9
- Return JMP: target=0x4071DF, from=0xEDF9F4+5=0xEDF9F9, offset=0x4071DF-0xEDF9F9 = 0xFF5277E6

**Final patch bytes at 0x4071D9:**
```
E9 05 88 A9 00 90
```

**Final trampoline bytes at 0xEDF9E3:**
```
8B 93 8C 00 00 00 85 D2 75 05 E9 D9 7E 52 FF E9 E6 77 52 FF
```

### Suggested Live Verification

1. `livetools mem write 0x4071D9 E90588A90090` -- patch the jump
2. `livetools mem write 0xEDF9E3 8B938C00000085D27505E9D97E52FFE9E67752FF` -- write trampoline
3. `livetools trace 0xEDF9E3 --count 20 --read "edx:4:uint32"` -- verify null checks are happening
4. `livetools trace 0xEDF9ED --count 5 --read "edx:4:uint32"` -- verify skip path is taken when edx=0

## Proxy Patch Site Verification — 2026-03-29

### Summary

Verified the two primary ASI/proxy patch sites in the unmodified on-disk `trl.exe`. Neither site contains RET or NOP bytes — both are stock game code with standard function prologues. This confirms that any culling patches must be applied at runtime (via the proxy DLL or livetools `mem write`), as the on-disk binary is unpatched.

### Key Addresses

| Address | Description |
|---------|-------------|
| 0x407150 | Cull function entry — standard `push ebp; mov ebp, esp` prologue. No RET byte at entry. |
| 0x4070F0 | Scene traversal region — `and al, 0x18` followed by a `call 0x5DDC70`, then movaps/ret sequence. No NOPs present. |
| 0x407108-0x40710F | INT3 padding between functions (8 bytes of 0xCC) — normal compiler alignment, not patch NOPs. |
| 0x407110 | Next function entry — another `push ebp; mov ebp, esp` prologue. |

### Details — 0x407150 (Cull Function)

```asm
0x00407150: push     ebp
0x00407151: mov      ebp, esp
0x00407153: and      esp, 0xfffffff0
0x00407156: sub      esp, 0x1e4
0x0040715C: fld      dword ptr [0xf11d0c]
0x00407162: mov      eax, dword ptr [0x10e537c]
0x00407167: fmul     dword ptr [0xefd8e4]
0x0040716D: mov      cl, byte ptr [eax + 0xd2]
0x00407173: test     cl, cl
0x00407175: push     ebx
```

**Status: UNPATCHED.** This is the original function prologue. The proxy or ASI patcher would need to overwrite the first bytes with a RET (0xC3) or a JMP to a trampoline to disable this cull function at runtime.

### Details — 0x4070F0 (Scene Traversal Cull Region)

```asm
0x004070F0: and      al, 0x18
0x004070F2: push     edx
0x004070F3: call     0x5ddc70
0x004070F8: movaps   xmm0, xmmword ptr [eax]
0x004070FB: mov      eax, dword ptr [ebp + 8]
0x004070FE: add      esp, 0xc
0x00407101: movaps   xmmword ptr [eax], xmm0
0x00407104: mov      esp, ebp
0x00407106: pop      ebp
0x00407107: ret
0x00407108: int3      (x8 — alignment padding)
0x00407110: push     ebp          ; next function
0x00407111: mov      ebp, esp
...
0x0040712A: ret      4
```

**Status: UNPATCHED.** The region at 0x4070F0 is the tail of one function (ending with `ret` at 0x407107), followed by INT3 padding, then a new function at 0x407110. No NOP (0x90) bytes are present. The proxy would need to write NOPs over conditional jump instructions that perform frustum culling to disable scene traversal culling.

### Suggested Live Verification

1. After launching the game with the proxy, use `livetools mem read 0x407150 16` to confirm the proxy has patched the cull function entry
2. Use `livetools mem read 0x4070F0 48` to confirm NOP patches in the scene traversal cull region
3. If patches are not present, check the proxy log (`ffp_proxy.log`) for patch application messages

---

## Terrain Rendering Path — Culling Analysis (2026-04-03)

### Summary

Full reverse engineering of the terrain rendering pipeline, independent of the already-patched SceneTraversal path. The analysis covers four target functions: TerrainDrawable_Draw (0x40ACF0), MeshSubmit_VisibilityGate (0x454AB0), MeshSubmit (0x458630), and PostSector_ObjectLoop (0x40E2C0). Together these contain **11 culling-related conditional jumps** across three distinct subsystems that can prevent terrain/mesh geometry from reaching the D3D draw calls.

### Architecture Overview

The terrain rendering pipeline has three layers of culling:

1. **TerrainDrawable_Draw (0x40ACF0)** — Constructor/setup only. Does NOT contain culling jumps itself. It builds a terrain draw descriptor (0x30 bytes at `esi`) and returns it. The actual rendering happens in the **dispatch function at 0x40AE20** which is a separate thiscall method on the same object.

2. **TerrainDrawable_Dispatch (0x40AE20)** — The real terrain draw function. Contains the critical early-out that skips terrain if a flag is set, plus the null-draw-submitter check that aborts the entire draw.

3. **MeshSubmit_VisibilityGate (0x454AB0)** — Per-mesh visibility check using a sector-linked-list walk and a bitfield visibility lookup. Gates every mesh submission.

4. **PostSector_ObjectLoop (0x40E2C0)** — Post-sector moveable object iteration with sector bitmask, flag filtering, and distance culling via FPU comparisons.

### Key Addresses — Culling Jumps to NOP

| # | Address | Bytes | Instruction | Size | What It Culls | Recommendation |
|---|---------|-------|-------------|------|---------------|----------------|
| 1 | 0x40AE3E | `0F 85 62 03 00 00` | `jne 0x40B1A6` | 6 | TerrainDrawable: skips entire draw if flag 0x20000 set in [esi+0x1C] when arg==0x1000 | **NOP** |
| 2 | 0x40B0F4 | `0F 84 AC 00 00 00` | `je 0x40B1A6` | 6 | TerrainDrawable: skips draw if draw submitter ptr is NULL | **DO NOT NOP** (null deref crash) |
| 3 | 0x454ABA | `74 13` | `je 0x454ACF` | 2 | VisibilityGate: jumps to secondary check if object list head is NULL | Part of cull chain |
| 4 | 0x454AC6 | `74 24` | `je 0x454AEC` | 2 | VisibilityGate: found matching entry in linked list -> CULL (return 1) | Part of cull chain |
| 5 | 0x454ADD | `75 0D` | `jne 0x454AEC` | 2 | VisibilityGate: sector-search found match -> CULL (return 1) | Part of cull chain |
| 6 | 0x454AEA | `74 0E` | `je 0x454AFA` | 2 | VisibilityGate: bitfield visibility test passed -> return 0 (VISIBLE) | Part of cull chain |
| 7 | 0x458648 | `74 0B` | `je 0x458655` | 2 | MeshSubmit: if VisibilityGate returned 0 -> skip cull, proceed to draw | Gate consumer |
| 8 | 0x45864F | `0F 84 2B 03 00 00` | `je 0x458980` | 6 | MeshSubmit: if VisibilityGate returned nonzero AND forceVisible==0 -> SKIP mesh | **NOP** (or patch VisibilityGate) |
| 9 | 0x40E2CA | `0F 84 96 01 00 00` | `je 0x40E466` | 6 | PostSector: skips entire loop if global byte [0xF12016] == 0 | **NOP** |
| 10 | 0x40E30F | `0F 84 38 01 00 00` | `je 0x40E44D` | 6 | PostSector: skips object N if sector bit N not set in g_postSectorVisibilityMask (0xFFA718) | **NOP** |
| 11 | 0x40E3B0 | `75 5A` | `jne 0x40E40C` | 2 | PostSector: skips object if distance exceeds threshold (FPU compare against 0xEFDDB0) | **NOP** |

### Detailed Analysis Per Function

#### 1. TerrainDrawable_Draw (0x40ACF0) — Setup, No Culling

This function is a **constructor** for a terrain draw descriptor. It:
- Takes 4 stack args: `[esp+4]=pTerrainData`, `[esp+8]=pMeshBlock`, `[esp+0x14]=pFlags`, `[esp+0x18]=pContext`
- ECX = `this` (output descriptor at `esi`, 0x30 bytes)
- Sets vtable pointers: `[esi]=0xEFDE08`, `[esi+4]=0xF12864`
- Copies mesh block flags from `[pMeshBlock]` to `[esi+0x1C]`
- Tests `[pContext+0x20] & 0x1100000` for flag manipulation
- Calls `0x414280` (shader selection) and `0xECB0B0` (vertex buffer lookup)
- Returns the descriptor in EAX
- **No culling jumps** — all branches are flag manipulation, not skip-draw decisions

Function ends at `0x40ADED` with `ret 0x10`.

#### 2. TerrainDrawable_Dispatch (0x40AE20) — The Real Draw Function

Starts immediately after TerrainDrawable_Draw. This is the `thiscall` draw method invoked later:

```asm
0x40AE20: push ebp / mov ebp,esp / and esp,0xFFFFFFF0 / sub esp,0x74
0x40AE29: cmp  [ebp+8], 0x1000        ; check draw mode
0x40AE35: jne  0x40AE44               ; if mode != 0x1000, skip flag check
0x40AE37: test [esi+0x1C], 0x20000    ; check terrain flag bit 17
0x40AE3E: jne  0x40B1A6               ; *** CULL JUMP 1: skip entire draw ***
```

**CULL JUMP 1 at 0x40AE3E**: `0F 85 62 03 00 00` (JNE, 6 bytes). When the draw mode is 0x1000 (terrain batch mode) and bit 17 (0x20000) is set in the terrain descriptor's flags, the entire draw is skipped. This is a terrain-specific LOD or type gate.

Later in the function:
```asm
0x40B0F2: test eax, eax               ; eax = draw submitter ptr
0x40B0F4: je   0x40B1A6               ; *** NULL CHECK: skip if no submitter ***
```

**NULL CHECK at 0x40B0F4**: `0F 84 AC 00 00 00` (JE, 6 bytes). This checks if the draw submitter pointer ([renderer+0x20]) is NULL. NOPing this would cause a null pointer dereference crash. **Do NOT patch.**

The rest of the function (0x40AF2E to 0x40B1A6) handles:
- Setting vertex shader constants (world matrix at 0x40AFB0-0x40B06C, 4x4 matrix multiply loop)
- Vertex buffer binding (call 0x40AA60)
- Index buffer setup (call 0x40A950)
- Final DrawIndexedPrimitive dispatch via vtable at either 0xEC91B0 or [eax+0x148]

No additional culling jumps in this section — once past the two gates, geometry is drawn.

#### 3. MeshSubmit_VisibilityGate (0x454AB0) — Sector Visibility Lookup

Complete function, 0x454AB0 to 0x454AFD:

```asm
0x454AB0: mov eax, [0x10C5AA4]         ; g_pObjectListHead
0x454AB5: push edi
0x454AB6: xor edi, edi                  ; edi = 0 (return value: visible)
0x454AB8: test eax, eax
0x454ABA: je 0x454ACF                   ; if list empty, go to fallback checks

; Walk linked list looking for matching sector ID
0x454AC0: cmp ecx, [eax+0x1D0]         ; ecx = mesh sector ID ([esi+0x54])
0x454AC6: je 0x454AEC                   ; FOUND -> jump to CULL (return 1)
0x454AC8: mov eax, [eax+8]             ; next in list
0x454ACB: test eax, eax
0x454ACD: jne 0x454AC0                  ; continue walking

; Fallback: call sub_5BE540 (sector command buffer search)
0x454ACF: mov eax, [esi+0x54]
0x454AD2: push eax
0x454AD3: call 0x5BE540                 ; search sector command buffer for ID
0x454ADB: test eax, eax
0x454ADD: jne 0x454AEC                  ; if found -> CULL (return 1)

; Fallback 2: call sub_5BE7B0 (bitfield visibility lookup)
0x454ADF: push esi
0x454AE0: call 0x5BE7B0                 ; check visibility bitfield
0x454AE8: test eax, eax
0x454AEA: je 0x454AFA                   ; if 0 -> return 0 (VISIBLE)

; CULL path: set flag and return 1
0x454AEC: or [esi+0x5C], 0x80000000    ; set "culled" flag bit 31
0x454AF3: mov eax, 1                    ; return 1 (CULLED)
0x454AF8: pop edi
0x454AF9: ret

; VISIBLE path: return 0
0x454AFA: mov eax, edi                  ; edi = 0
0x454AFC: pop edi
0x454AFD: ret
```

**How it works**: The function checks if a mesh's sector ID (`[esi+0x54]`) appears in the "active/visible sectors" data structures. Three checks, any positive match = CULL:
1. Walk linked list at `g_pObjectListHead` (0x10C5AA4), comparing [node+0x1D0] to sector ID
2. Search sector command buffer via `sub_5BE540` (scans word-aligned entries at 0x11397C0+0x3854)
3. Check per-sector visibility bitfield via `sub_5BE7B0` (reads byte at [0x11397C0+0x33D8+idx/8] and tests bit)

**sub_5BE7B0 analysis**: Takes mesh object as arg, reads sector index from [obj+0x54], bounds-checks (0..0x3FFF, index/8 < 0x47C), then tests a single bit in a 0x47C-byte visibility bitfield at [0x11397C0+0x33D8]. Returns 1 if bit is SET (meaning "this sector was already rendered/is active" = cull the mesh to avoid double-draw). Returns 0 if bit clear (mesh is in an unvisited sector = should be drawn).

**Patch recommendation**: The SAFEST and most complete fix is to **patch MeshSubmit_VisibilityGate itself** to always return 0:
```
0x454AB0: 33 C0    xor eax, eax   (2 bytes)
0x454AB2: C3       ret            (1 byte)
```
Total: 3 bytes at 0x454AB0. This forces all meshes to be considered visible regardless of sector state. The original code at 0x454AB0 was `A1 A4 5A 0C 01` (mov eax, [0x10C5AA4]) so we overwrite 3 of the 5 bytes and the remaining 2 (`0C 01`) become harmless dead code that is never reached.

**Alternative**: NOP the consumer at MeshSubmit 0x45864F (the 6-byte `je 0x458980` that skips mesh when VisibilityGate returns nonzero). This is also safe.

#### 4. MeshSubmit (0x458630) — VisibilityGate Consumer

```asm
0x458630: push ebp / mov ebp,esp ...
0x45863C: mov edi, [ebp+8]             ; meshEntry
0x45863F: mov esi, edi
0x458641: call 0x454AB0                 ; MeshSubmit_VisibilityGate(esi=meshEntry)
0x458646: test eax, eax
0x458648: je 0x458655                   ; if 0 (visible), continue to draw setup
0x45864A: mov al, [ebp+0x10]           ; forceVisible arg
0x45864D: test al, al
0x45864F: je 0x458980                   ; *** CULL: if not forced visible, skip mesh ***
```

**CULL at 0x45864F**: `0F 84 2B 03 00 00` (JE, 6 bytes). If VisibilityGate returned nonzero (culled) AND the `forceVisible` parameter (3rd arg at ebp+0x10) is 0, the mesh is completely skipped. After this gate, mesh proceeds to material lookup and draw submission.

Additional gates in MeshSubmit (NOT culling-related, safe to leave):
- `0x458668: je 0x458980` — skips if material lookup returns NULL (no material = can't draw)
- `0x458673: jne 0x458980` — skips if material type != 2 (wrong material class)
- `0x45868C: jne 0x45896C` — flags check, routes to alternate path, not a cull

#### 5. PostSector_ObjectLoop (0x40E2C0) — Distance & Sector Culling

```asm
0x40E2C0: mov al, [0xF12016]           ; global enable flag
0x40E2C8: test al, al
0x40E2CA: je 0x40E466                   ; *** CULL 1: skip entire loop if disabled ***

0x40E2D0: mov eax, [0x10024E8]         ; g_postSectorLoopDisable
0x40E2D7: jne 0x40E466                  ; *** CULL 2: skip if disable flag set ***

; Loop body per object:
0x40E300: mov ecx, edi                  ; edi = object index
0x40E302: mov edx, 1
0x40E307: shl edx, cl
0x40E309: test [0xFFA718], edx          ; g_postSectorVisibilityMask
0x40E30F: je 0x40E44D                   ; *** CULL 3: skip if sector bit not set ***

0x40E315: mov esi, [ebx+0x110]         ; object data ptr
0x40E31B: test esi, esi
0x40E31D: je 0x40E44D                   ; skip if null (not a cull, safety check)

; Flag filtering:
0x40E336: cmp byte [eax+0x76], 0xFF
0x40E33A: je 0x40E40E                   ; skip if flag == 0xFF (inactive object marker)
0x40E346: test ch, 8                    ; [esi+0xA4] & 0x800
0x40E349: jne 0x40E40E                  ; skip if hidden flag set
0x40E34F: test [esi+0xA8], 0x10000
0x40E359: jne 0x40E40E                  ; skip if "do not render" flag set

; Distance culling (FPU):
0x40E393: call 0x455A50                 ; Object_ComputeDistance(esi)
0x40E398: fsubr [0xEFD40C]             ; 1.0 - distance_factor
0x40E3A1: fmul [esp+0x10]              ; * scale factor
0x40E3A5: fcom [0xEFDDB0]              ; compare against g_nearLODThreshold
0x40E3AB: fnstsw ax
0x40E3AD: test ah, 1                    ; CF set if result < threshold
0x40E3B0: jne 0x40E40C                  ; *** CULL 4: skip if too far away ***
```

**Culling jumps in PostSector_ObjectLoop**:

| Address | Bytes | Instruction | Size | Purpose |
|---------|-------|-------------|------|---------|
| 0x40E2CA | `0F 84 96 01 00 00` | JE | 6 | Master enable gate: skips all post-sector objects |
| 0x40E2D7 | `0F 85 89 01 00 00` | JNE | 6 | Disable flag gate: skips if g_postSectorLoopDisable != 0 |
| 0x40E30F | `0F 84 38 01 00 00` | JE | 6 | Per-object sector visibility bitmask check |
| 0x40E33A | `0F 84 CE 00 00 00` | JE | 6 | Object inactive marker (0xFF) check |
| 0x40E349 | `0F 85 BF 00 00 00` | JNE | 6 | Hidden flag (0x800) check |
| 0x40E359 | `0F 85 AF 00 00 00` | JNE | 6 | "Do not render" flag (0x10000) check |
| 0x40E3B0 | `75 5A` | JNE | 2 | Distance culling (too far) |

### Recommended NOP Patch Set

**Tier 1 — Most likely to fix missing terrain/anchors:**

| Address | Size | Original | NOP Bytes | Reason |
|---------|------|----------|-----------|--------|
| 0x40AE3E | 6 | `0F 85 62 03 00 00` | `90 90 90 90 90 90` | Terrain flag 0x20000 gate — blocks terrain batches |
| 0x454AB0 | 3 | `A1 A4 5A 0C 01` | `33 C0 C3` (xor eax,eax; ret) | Force VisibilityGate to always return 0 (visible) |
| 0x40E30F | 6 | `0F 84 38 01 00 00` | `90 90 90 90 90 90` | Per-object sector bitmask — blocks objects in "unvisited" sectors |
| 0x40E3B0 | 2 | `75 5A` | `90 90` | Distance culling for post-sector objects |

**Tier 2 — Additional safety NOPs (may cause duplicate draws but ensures nothing culled):**

| Address | Size | Original | NOP Bytes | Reason |
|---------|------|----------|-----------|--------|
| 0x40E2CA | 6 | `0F 84 96 01 00 00` | `90 90 90 90 90 90` | Master enable for post-sector loop |
| 0x40E2D7 | 6 | `0F 85 89 01 00 00` | `90 90 90 90 90 90` | Disable flag gate |
| 0x40E33A | 6 | `0F 84 CE 00 00 00` | `90 90 90 90 90 90` | Inactive object marker |
| 0x40E349 | 6 | `0F 85 BF 00 00 00` | `90 90 90 90 90 90` | Hidden flag gate |
| 0x40E359 | 6 | `0F 85 AF 00 00 00` | `90 90 90 90 90 90` | Do-not-render flag gate |

**Tier 3 — Alternative to VisibilityGate patch (NOP the consumer instead):**

| Address | Size | Original | NOP Bytes | Reason |
|---------|------|----------|-----------|--------|
| 0x45864F | 6 | `0F 84 2B 03 00 00` | `90 90 90 90 90 90` | MeshSubmit: skip the "cull mesh" branch entirely |

### Callers

- **TerrainDrawable_Draw (0x40ACF0)** called from exactly 1 site: `0x40C0E9` inside the terrain rendering loop at 0x40C040. The loop iterates a terrain mesh array from [sceneData+0x90], with no pre-culling before calling TerrainDrawable_Draw.

- **MeshSubmit_VisibilityGate (0x454AB0)** called from exactly 1 site: `0x458641` inside MeshSubmit (0x458630).

### Callees of TerrainDrawable_Draw

```
0x40ACF0 (5 children)
  +- 0x40A8E0  (vertex count computation)
  +- 0x413D70  (mesh data lookup)
  +- 0x414280  (shader/material selection)
  +- 0xEC9DC0  (vertex buffer allocation)
  +- 0xECB0B0  (vertex shader constant lookup)
```

### Safety Assessment: Patching VisibilityGate to Return 0

Patching `0x454AB0` to `xor eax,eax; ret` (33 C0 C3) is **safe** because:
1. The function has no side effects except setting [esi+0x5C] |= 0x80000000 on the CULL path (which we skip)
2. The only caller (MeshSubmit at 0x458641) simply tests the return value and branches
3. Returning 0 means "visible" — the mesh proceeds to normal draw setup
4. The `forceVisible` parameter check at 0x45864D-0x45864F becomes irrelevant when VisibilityGate always returns 0
5. Worst case: some meshes draw that would normally be hidden (sector double-draw), which is acceptable for RTX Remix where we want all geometry visible

### Suggested Live Verification

1. After patching, use `livetools mem read 0x454AB0 4` to confirm bytes are `33 C0 C3 xx`
2. Use `livetools mem read 0x40AE3E 6` to confirm NOPs at terrain flag gate
3. Use `livetools mem read 0x40E30F 6` to confirm NOPs at sector bitmask gate
4. Use `livetools mem read 0x40E3B0 2` to confirm NOPs at distance cull
5. Use `livetools collect 0x40AE20 0x40B113 0x40B156 --duration 5` to confirm terrain draw dispatch is being reached and draw calls are executing
6. Walk around in-game to verify terrain and distant objects remain visible at all camera angles

## Patch Site Byte Verification — 2026-04-03

### Summary

All 9 patch target addresses verified against the on-disk binary. Every address contains the expected instruction opcode and encoding. All are safe to patch.

### Tier 1 Patches

| # | Address | Expected | Actual Bytes | Disassembly | Match |
|---|---------|----------|-------------|-------------|-------|
| 1 | `0x0040AE3E` | 6-byte JNE (0F 85) | `0F 85 62 03 00 00` | `jne 0x40B1A6` | YES |
| 2 | `0x00454AB0` | Function prologue | `A1 A4 5A 0C 01 57` | `mov eax, [0x10C5AA4]; push edi` | YES |
| 3 | `0x0040E30F` | 6-byte JE (0F 84) | `0F 84 38 01 00 00` | `je 0x40E44D` | YES |
| 4 | `0x0040E3B0` | 2-byte JNE (75) | `75 5A` | `jne 0x40E40C` | YES |

### Tier 1 Context Disassembly

**#1 — Terrain flag 0x20000 gate (0x0040AE38 +5):**
```
0x0040AE38: inc      esi               ; (tail of prior insn, disasm artifact)
0x0040AE39: sbb      al, 0             ; (mid-instruction bytes)
0x0040AE3B: add      byte ptr [edx], al
0x0040AE3D: add      byte ptr [edi], cl
0x0040AE3F: test     dword ptr [edx + 3], esp
```
Note: Starting at 0x40AE38 mid-instruction produced garbage. The key byte sequence at 0x40AE3E is confirmed `0F 85 62 03 00 00` = JNE +0x362 (to 0x40B1A6). 6-byte NOP patch is safe.

**#2 — MeshSubmit_VisibilityGate (0x00454AB0 +10):**
```
0x00454AB0: mov      eax, dword ptr [0x10c5aa4]   ; global pointer load
0x00454AB5: push     edi
0x00454AB6: xor      edi, edi
0x00454AB8: test     eax, eax
0x00454ABA: je       0x454acf
0x00454ABC: mov      ecx, dword ptr [esi + 0x54]
0x00454ABF: nop
0x00454AC0: cmp      ecx, dword ptr [eax + 0x1d0]
0x00454AC6: je       0x454aec
0x00454AC8: mov      eax, dword ptr [eax + 8]
```
Clean function entry. Prologue starts with `mov eax, [global]` not the typical `push ebp; mov ebp, esp` — this is a leaf/optimized function. `xor edi, edi; ret` or early-return patch at entry is viable.

**#3 — Sector bitmask gate (0x0040E309 +5):**
```
0x0040E309: test     dword ptr [0xffa718], edx     ; (mid-insn artifact)
0x0040E30F: je       0x40e44d                       ; TARGET: 6-byte JE
0x0040E315: mov      esi, dword ptr [ebx + 0x110]
0x0040E31B: test     esi, esi
0x0040E31D: je       0x40e44d
```
JE at 0x40E30F jumps to 0x40E44D (skip path). NOPing this forces fall-through to the object processing at 0x40E315.

**#4 — Distance culling (0x0040E3AA +5):**
```
0x0040E3AA: add      bh, bl            ; (mid-insn artifact)
0x0040E3AC: loopne   0x40e3a4          ; (mid-insn artifact)
0x0040E3AE: les      eax, ptr [ecx]    ; (mid-insn artifact)
0x0040E3B0: jne      0x40e40c          ; TARGET: 2-byte JNE (75 5A)
0x0040E3B2: fmul     dword ptr [0xefde54]
```
Short JNE confirmed at 0x40E3B0: `75 5A`. 2-byte NOP patch is safe.

### Tier 2 Patches

| # | Address | Expected | Actual Bytes | Disassembly | Match |
|---|---------|----------|-------------|-------------|-------|
| 5 | `0x0040E2CA` | 6-byte JE (0F 84) | `0F 84 96 01 00 00` | `je 0x40E466` | YES |
| 6 | `0x0040E2D7` | 6-byte JNE (0F 85) | `0F 85 89 01 00 00` | `jne 0x40E466` | YES |
| 7 | `0x0040E33A` | 6-byte JE (0F 84) | `0F 84 CE 00 00 00` | `je 0x40E40E` | YES |
| 8 | `0x0040E349` | 6-byte JNE (0F 85) | `0F 85 BF 00 00 00` | `jne 0x40E40E` | YES |
| 9 | `0x0040E359` | 6-byte JNE (0F 85) | `0F 85 AF 00 00 00` | `jne 0x40E40E` | YES |

### Tier 2 Context Disassembly

**#5 — Master enable gate (0x40E2CA):**
```
0x0040E2C8: test     al, al
0x0040E2CA: je       0x40e466          ; if !enabled, skip entire object loop
0x0040E2D0: mov      eax, dword ptr [0x10024e8]
```
JE skips to 0x40E466 when `al == 0`. NOPing forces unconditional fall-through.

**#6 — Disable flag gate (0x40E2D7):**
```
0x0040E2D5: test     eax, eax
0x0040E2D7: jne      0x40e466          ; if disable_flag != 0, skip
0x0040E2DD: mov      eax, dword ptr [esp + 0x10]
```
JNE skips to 0x40E466. NOPing means objects render even when disable flag is set.

**#7 — Inactive marker (0x40E33A):**
```
0x0040E338: jbe      0x40e339          ; (prior insn tail)
0x0040E33A: je       0x40e40e          ; if inactive, skip object
0x0040E340: mov      ecx, dword ptr [esi + 0xa4]
```
JE skips to 0x40E40E. NOPing forces processing of inactive objects.

**#8 — Hidden flag 0x800 (0x40E349):**
```
0x0040E347: lds      ecx, ptr [eax]    ; (mid-insn artifact)
0x0040E349: jne      0x40e40e          ; if hidden flag set, skip
0x0040E34F: test     dword ptr [esi + 0xa8], 0x10000
```
JNE skips to 0x40E40E. The instruction after (0x40E34F) tests the 0x10000 flag — this is the gate leading into patch #9.

**#9 — Do-not-render flag 0x10000 (0x40E359):**
```
0x0040E357: add      dword ptr [eax], eax  ; (mid-insn artifact)
0x0040E359: jne      0x40e40e              ; if do-not-render, skip
0x0040E35F: movsx    ecx, byte ptr [eax + 0x76]
```
JNE skips to 0x40E40E. Last gate before the object render path.

### Jump Target Analysis

Patches #5 and #6 share skip target `0x40E466` — this is the "skip entire sector objects" exit.
Patches #7, #8, #9 share skip target `0x40E40E` — this is the "skip this individual object" continue point (likely `continue` in the object loop).

### Concerns

- **None for byte verification** — all 9 addresses contain exactly the expected opcodes.
- **Tier 2 patches #7-#9 disable per-object visibility flags.** If the game uses these to hide objects that should genuinely be hidden (e.g., objects behind doors not yet opened), NOPing them will cause visual artifacts. These are fine for anti-culling but may need conditional logic if selectivity is required.
- **Patch #5 (master enable)** is the most aggressive — it bypasses the entire enable check. If the game sets this flag for legitimate reasons (e.g., area not loaded), forcing it could cause crashes or render uninitialized data.
- **The mid-instruction disassembly artifacts** at addresses 0x40AE38, 0x40E309, 0x40E3AA, 0x40E347, 0x40E357 are expected — these addresses fall inside multi-byte instructions. The actual patch targets at +2 or +6 offset are correctly aligned instruction boundaries.

### Suggested Live Verification

- Set breakpoints at each address with `livetools bp` and check hit counts to confirm which gates are actively filtering objects during gameplay.
- For Tier 2, trace 0x40E2CA with `--read "al:1:uint8"` to see how often the master enable is false.
- Trace 0x40E349 with `--read "[esi+0xa4]:4:uint32"` to see what flag values objects have when hidden.

## Crash Analysis: NULL dereference in PostSector_SubmitObject at 0x0040D2AF — 2026-04-03

### Summary

The crash is a null pointer dereference at `0x0040D2AF` inside `PostSector_SubmitObject` (0x0040D290). The instruction `mov ecx, [esi+0x20]` reads from offset +0x20 of the second parameter (`lodColor`), which is NULL. The function's only caller at `0x0040E402` (inside `PostSector_ObjectLoop` at 0x0040E2C0) passes a pointer derived from the object array without null-checking it first. The function itself has an early-exit gate checking `[0x1392E18]+0x10` (a global renderer state), but no null check on its `lodColor` parameter before dereferencing it.

### Key Addresses

| Address | Description |
|---------|-------------|
| 0x0040D2AF | **Crash site**: `mov ecx, [esi+0x20]` — reads lodColor->field_0x20 (matrix/transform pointer) |
| 0x0040D2AC | `mov esi, [ebp+0xc]` — loads the second parameter (lodColor) into ESI |
| 0x0040D290 | `PostSector_SubmitObject` function entry |
| 0x0040D299 | `mov eax, [0x1392E18]` — loads global renderer object pointer |
| 0x0040D29E | `mov ecx, [eax+0x10]` — reads renderer state flag |
| 0x0040D2A1 | `test ecx, ecx` / `jne 0x40D3EB` — early exit if renderer flag is non-zero |
| 0x0040E402 | **Single caller**: inside `PostSector_ObjectLoop` (0x0040E2C0) |
| 0x0040E2C0 | `PostSector_ObjectLoop` — iterates objects, pushes args, calls 0x40D290 |
| 0x01392E18 | Global pointer to renderer/scene state object |

### Crash Details

```
Exception: Access violation (0xC0000005) — Read of address 0x00000020
EIP = 0x0040D2AF    ECX = 0x00000000    ESI = 0x00000000
EBX = 0x04487FD0    EDX = 0x044AF950    EDI = 0x058E32B0
```

### Crashing Instruction Sequence

```asm
0x0040D290: push     ebp                    ; function prologue
0x0040D291: mov      ebp, esp
0x0040D293: and      esp, 0xFFFFFFF0
0x0040D296: sub      esp, 0x54
0x0040D299: mov      eax, [0x1392E18]       ; global renderer ptr
0x0040D29E: mov      ecx, [eax+0x10]        ; renderer state flag
0x0040D2A1: test     ecx, ecx
0x0040D2A3: push     ebx
0x0040D2A4: push     esi
0x0040D2A5: push     edi
0x0040D2A6: jne      0x40D3EB               ; skip if flag != 0 (early return)
0x0040D2AC: mov      esi, [ebp+0xC]         ; ESI = param2 (lodColor) — NULL!
0x0040D2AF: mov      ecx, [esi+0x20]        ; CRASH: [NULL+0x20] = 0x20
```

### Decompiled Function (PostSector_SubmitObject)

The decompiler names the second parameter `lodColor`. It is an object/struct pointer used extensively:
- `lodColor+0x20` — matrix/transform pointer (first dereference, crash site)
- `lodColor+0x04` — count field A
- `lodColor+0x08` — count field B
- `lodColor+0x0C` — sub-object array pointer
- `lodColor+0x24` — base address for matrix index computation
- `lodColor+0x58` — linked list head for secondary processing

The function iterates over sub-objects (count = field_0x04 + field_0x08), calls `MatrixMultiply4x4` for each visible one, then walks a linked list at field_0x58 calling submission helpers.

### Root Cause

The caller (`PostSector_ObjectLoop` at 0x0040E2C0) passes a NULL pointer as the second argument to `PostSector_SubmitObject`. This is the `lodColor`/LOD-data parameter. The function does NOT check if this parameter is NULL before using it. The only guard in the function checks the global renderer state at `[0x1392E18]+0x10`, which is a different concern (whether the renderer is in a specific mode).

**Missing null check**: There is no `test esi, esi` / `jz` after `mov esi, [ebp+0xc]` at 0x0040D2AC.

### Possible Causes for NULL Parameter

1. **Object not yet loaded**: The post-sector object loop iterates an array where some entries may reference LOD data that hasn't been streamed in yet.
2. **Culling-related**: Our anti-culling patches (documented in prior findings) force objects to be submitted that would normally be skipped. If an object was culled before its LOD data was resolved, the LOD pointer remains NULL.
3. **Race condition**: If post-sector objects are populated asynchronously, the LOD data pointer may be NULL during a brief window.

The most likely cause is **#2** — our culling patches at 0x0040E2C0 (documented in the KB as having NOP patches for visibility checks) force objects through the submission pipeline even when they lack valid LOD data.

### Fix Options

**Option A — Null guard in PostSector_SubmitObject (recommended)**:
Patch at 0x0040D2AC to add a null check:
```asm
; At 0x0040D2AC:
mov  esi, [ebp+0xc]     ; existing
test esi, esi            ; ADD: null check
jz   0x0040D3EB          ; ADD: jump to epilogue (same as early-exit target)
```
This requires 4 extra bytes (test esi,esi = 2 bytes + jz = 6 bytes). The function has no slack space at this point, so this would need to be a trampoline or the early-exit `jne` at 0x0040D2A6 could be restructured.

**Option B — Null guard in the caller (PostSector_ObjectLoop)**:
Check the lodColor pointer before calling 0x40D290. The caller pushes it from a computed offset — add a null check there.

**Option C — Conditional culling restoration**:
Instead of fully NOPing the visibility checks in PostSector_ObjectLoop, make them skip only when the LOD pointer is valid. This is the cleanest but most complex.

### Suggested Live Verification

- Attach livetools and trace `0x0040D2AC` with `--read "[ebp+0xc]:4:uint32"` to see how often lodColor is NULL during normal gameplay.
- Trace `0x0040E402` (the call site) with `--read "[esp+4]:4:uint32"` to see what values are pushed as the lodColor parameter.
- If anti-culling patches are active, temporarily disable them and confirm the crash goes away — this would confirm cause #2.
- For a runtime fix: `livetools mem write 0x0040D2AC` to inject `test esi,esi; jz 0x40D3EB` as a hot-patch.

## RenderLights_FrustumCull Call Chain Analysis — 2026-04-06

### Summary

Traced the complete call chain from `RenderLights_FrustumCull` (0x60C7D0) upward through 4 levels. The function is reachable ONLY through a single path, and every level has gating conditions that can prevent execution. The top-level function is dispatched via a C++ vtable, not a direct call.

### Call Chain (bottom to top)

| Level | Address | Function | Called By |
|-------|---------|----------|-----------|
| 0 | 0x60C7D0 | `RenderLights_FrustumCull` | 0x60E3B9 (in RenderLights_Caller) |
| 1 | 0x60E2D0 | `RenderLights_Caller` | 0x60383A (in LightGroupArray_RenderAll) |
| 2 | 0x603810 | `LightGroupArray_RenderAll` | 0x60A62C (in RenderLights_TopLevel) |
| 3 | 0x60A0F0 | `RenderLights_TopLevel` | vtable slot [6] at 0xF08498 (indirect dispatch) |

### Gate Conditions at Each Level

**Level 3 — RenderLights_TopLevel (0x60A0F0):**
- `[this+0xFA2E] != 0` → early exit (rendering disabled)
- `[this+0xFA2C] == 0` → early exit (lights not initialized)
- `[*(0x1392E18)+0x10] != 0` → early exit (scene loading/transition)

**Level 2 — LightGroupArray_RenderAll (0x603810):**
- Iterates `[param+0x14]` light groups; skips any group where `[group+0x94] + [group+0x84] == 0`

**Level 1 — RenderLights_Caller (0x60E2D0):**
- Three conditions for shadow setup (AND-ed):
  1. `[this+0x84] != 0` (light data pointer must exist)
  2. `[*(0x1392E18)+0x166] != 0` (scene-level light enable)
  3. `[this+0x74]->[+0x444] & 1` (renderer light capability bit)
- FrustumCull specifically guarded by `[this+0x1B0]` (light count):
  - If `[this+0x1B0] != 0`, byte at `esp+0x17` set to 1 (0x60E34D)
  - At 0x60E3B1: `je 0x60E4B6` — skips FrustumCull if `esp+0x17 == 0`

**Level 0 — RenderLights_FrustumCull (0x60C7D0):**
- Uses cull globals: `*0xF2A0D4` (pass 1 cull mode) and `*0xF2A0D8` (pass 2 cull mode)
- Iterates `[this+0x1B0]` lights, calls `LightVisibilityCheck` (0x60B050), then 6-plane frustum test
- Lights visible: drawn immediately (mode=1); lights culled by planes: deferred and drawn (mode=0)

### Key Findings

1. **Single call path**: There is exactly ONE path to `RenderLights_FrustumCull`. No redundant or alternative paths exist.

2. **Virtual dispatch at top**: `RenderLights_TopLevel` (0x60A0F0) has ZERO direct call xrefs. It is vtable slot [6] (+0x18) at vtable address 0xF08480. Callers use `call [reg+0x18]` — there are 109 such indirect call sites in the binary.

3. **0x60E300 and 0x60E345 have NO callers**: These are not separate functions — they are mid-function addresses within `RenderLights_Caller` (which starts at 0x60E2D0). 0x60E345 is an internal jump target (label `code_r0x0060e345` in decompilation).

4. **Critical gating variable**: `[this+0x1B0]` at level 1 is the light count. If this is 0, FrustumCull is never called regardless of other conditions. This is the SAME field that FrustumCull itself iterates over — if the light list is empty at the object level, the function is skipped.

5. **Three early exits at top level**: The most likely reason FrustumCull is not reached at runtime is one of:
   - `[this+0xFA2C] == 0` — lights never marked as initialized
   - `[*(0x1392E18)+0x10] != 0` — scene still in "loading" state
   - The vtable dispatch at slot [6] is simply never invoked by the render loop

### Suggested Live Verification

To determine WHY RenderLights_FrustumCull is not called at runtime:

1. **Trace vtable dispatch**: `livetools collect 0x60A0F0 0x60E2D0 0x603810 0x60C7D0` for 10 seconds — determines which level is reached and which is not.
2. **If 0x60A0F0 is never hit**: The vtable dispatch is never invoked — investigate the render loop's call to vtable[6].
3. **If 0x60A0F0 IS hit but 0x603810 is not**: Read the gate variables:
   - `livetools bp add 0x60A0F0` → `regs` → check `[ecx+0xFA2E]`, `[ecx+0xFA2C]`, `[*(0x1392E18)+0x10]`
4. **If 0x603810 IS hit but 0x60E2D0 is not**: Light group array is empty — `[param+0x14] == 0`.
5. **If 0x60E2D0 IS hit but 0x60C7D0 is not**: Check `[this+0x1B0]` (light count per group) and the three AND-ed conditions.


## Crash Analysis: Access Violation at 0x00EE88AD — 2026-04-06

### Summary

The crash at EIP=0x00EE88AD is inside the MSVC CRT `_output` function (the internal printf/sprintf formatter), specifically in the `%s` narrow-string handler's `strnlen` loop. The function (0x00EE8383–0x00EE8E3B, 2744 bytes) is the core state-machine that processes format specifiers (`%d`, `%s`, `%x`, `%c`, etc.) for all printf-family calls. A caller passed a `%s` argument with the garbage pointer 0x00008EF1 — not NULL (so the `"(null)"` fallback at 0xF2B3F8 was not triggered), but not a valid address either. This is a **corrupted va_arg** or a **use-after-free/dangling pointer** passed as a string argument to sprintf or similar.

### Key Addresses

| Address | Description |
|---------|-------------|
| 0x00EE8383 | `_output` — CRT internal printf formatter (state machine for format specifiers) |
| 0x00EE888D | Entry point for narrow `%s` strnlen path (code label within _output) |
| 0x00EE889B | Narrow `%s` handler: NULL check on string pointer |
| 0x00EE88AC | `strnlen` loop: `dec ecx; cmp byte ptr [eax], 0; je done; inc eax; test ecx, ecx; jne loop` |
| 0x00EE88AD | **CRASH SITE**: `cmp byte ptr [eax], 0` — EAX=0x8EF1 is unmapped |
| 0x00EE534E | `sprintf(buf, fmt, ...)` — CRT wrapper calling _output |
| 0x00EE52F5 | Another sprintf variant calling _output |
| 0x00EE64F0 | Printf-family caller of _output (likely `_snprintf` or `fprintf`) |
| 0x00EE6724 | Printf-family caller of _output |
| 0x00EE7043 | Printf-family caller of _output |

### Crash Register Context

| Register | Value | Meaning |
|----------|-------|---------|
| EIP | 0x00EE88AD | Inside `_output` strnlen loop |
| EAX | 0x00008EF1 | Bad string pointer from va_arg (not NULL, not valid) |
| ECX | 0x7FFFFFFE | Precision = INT_MAX-1 → no explicit precision in format (default `%s`) |
| ESI | 0xFFFFFFFB | -5 (likely unrelated local or counter) |
| EBX | 0x73 | ASCII 's' — the format specifier character being processed |

### Code Path to Crash

```
1. Some game function calls sprintf(buf, "...%s...", bad_ptr)
2. sprintf (0xEE534E) calls _output (0xEE8383)
3. _output state machine hits '%s' specifier (BL = 0x73 = 's')
4. At 0xEE8638: va_arg advances → loads string pointer = 0x8EF1 from stack
5. At 0xEE8651: je 0xEE889B (narrow string path, not wide)
6. At 0xEE889B: test eax, eax → EAX=0x8EF1 ≠ 0, skip "(null)" fallback
7. At 0xEE88AC-88B5: strnlen loop begins with ECX=0x7FFFFFFE
8. At 0xEE88AD: cmp byte ptr [eax], 0 → ACCESS VIOLATION reading 0x8EF1
```

### Decompiled _output (relevant %s excerpt)

```c
// At code_r0x00ee862b — handles 's' and 'S' format specifiers:
precision = (precision == -1) ? 0x7FFFFFFF : precision;  // default = no limit
va_list_ptr += 4;
str_ptr = *(va_list_ptr - 4);   // <-- this loaded 0x8EF1

if (!(flags & 0x810)) {         // narrow string path (not 'l' or 'w' modifier)
    goto narrow_strnlen;         // → 0xEE889B
}
// wide string path...

// narrow_strnlen (0xEE889B):
if (str_ptr == NULL) {
    str_ptr = global_null_string;  // "(null)" at [0xF2B3F8]
}
// strnlen loop:
p = str_ptr;
while (precision-- && *p != 0) p++;  // CRASH: *0x8EF1 is unmapped
length = p - str_ptr;
```

### Reachability from Patched Addresses

| Patch Address | Description | Reaches _output? |
|---------------|-------------|------------------|
| 0x60E3B1 | RenderLights gate | YES — via 0x60BCF0 → 0x60AFC0 → CRT printf chain |
| 0x603AE6 | Light count clear | NO — not in callgraph to _output |
| 0xEC6337 | Sector light gate | NO — not in callgraph to _output |
| 0x46C33E | Mesh cull flag | YES — via 0x458630 → deep call chain → 0xEE534E (sprintf) |

The crash is reachable from both the RenderLights gate (0x60E3B1) and the mesh cull flag (0x46C33E) patches, but the cause is **not the patches themselves** — the crash is a bad string pointer passed to `sprintf` by game code. The patches may have allowed execution of a code path that was previously culled/skipped, where the game accesses an uninitialized or freed string.

### Root Cause Analysis

The pointer 0x8EF1 has hallmarks of a **corrupted or garbage value**:
- Too small to be a valid heap/stack address (those are in the 0x00100000+ range)
- Not NULL (would have been caught by the `"(null)"` fallback)
- Could be a truncated pointer (only lower 16 bits valid), an uninitialized local, or a struct field read from an object that was freed or never initialized

Most likely scenario: **one of the cull-disable patches forced rendering of an object/sector whose associated data structure was never fully initialized** because the original code would have skipped it. The game's rendering code then passed an uninitialized string field to `sprintf` (for debug logging, error reporting, or material/texture name resolution).

### Suggested Live Verification

1. **Set a breakpoint on 0xEE534E** (sprintf) to catch the call and inspect the format string and arguments before they reach `_output`
2. **Trace 0x60AFC0** to see if the RenderLights path triggers sprintf calls with string args
3. **Check if the crash is deterministic** — if it only happens sometimes, it's likely a use-after-free; if always, it's an uninitialized field
4. **Temporarily re-enable one cull patch at a time** to isolate which patch allows the code path that produces the bad pointer:
   - First restore 0x46C33E (mesh cull flag) and test
   - Then restore 0x60E3B1 (RenderLights gate) and test


## Complete Culling Gate Analysis -- All Conditional Jumps That Skip DrawIndexedPrimitive -- 2026-04-06

### Summary

Traced the full draw call pipeline from three entry points (TerrainDrawable_Dispatch, Sector_RenderMeshes, Sector_IterateMeshArray) down to where DrawIndexedPrimitive actually fires. Found **14 distinct conditional gates** that can suppress draw calls. The existing patches (NOP at 0x40AE3E, 0x46B85A, 0x46C33E, and RET at 0x454AB0) cover only 4 of them. The remaining 10 gates -- particularly the _object+0x10 gate, the 0x10024E8 global gate, and the sector-level skip -- are likely responsible for the drop from ~2,800 to ~178 draws.

---

### Function 1: TerrainDrawable_Dispatch (0x40AE20)

Called per terrain tile via vtable. this param in ECX, mode in [ebp+8].

| # | Address | Instruction | Condition | What It Skips | Already Patched? |
|---|---------|-------------|-----------|---------------|------------------|
| 1 | 0x40AE3E | jne 0x40B1A6 | [esi+0x1C] & 0x20000 != 0 when this==0x1000 | Entire function -- skips to return | **YES** (NOP) |
| 2 | 0x40B0F4 | je 0x40B1A6 | _object+0x20 == NULL (render target pointer) | Entire draw section -- no DIP issued | NO |
| 3 | 0x40B125 | je 0x40B167 | (_object+0x1DC >> 5) & 1 == 0 | Chooses between two draw paths (both draw) | NO -- both paths draw |

**Key insight for TerrainDrawable_Dispatch**: Gate #2 at 0x40B0F4 is critical. _object is the global at [0x1392E18], and _object+0x20 must be non-NULL for ANY draw to happen. If NULL, the terrain tile is silently skipped.

---

### Function 2: Sector_RenderMeshes (0x46B7D0)

Called per sector. Iterates objects within sector mesh data.

| # | Address | Instruction | Condition | What It Skips | Already Patched? |
|---|---------|-------------|-----------|---------------|------------------|
| 4 | 0x46B7EA+0x46B7F2 | jns+je combo | [0x10E5384]>>8 >= 0 AND sectorData+8 == [0x10E5438] | Entire function -- returns immediately | NO |
| 5 | 0x46B825 | jle 0x46B885 | meshBase+0x14 <= 0 (object count) | Loop does not execute | NO (data-driven) |
| 6 | 0x46B83C | jne 0x46B877 | objectEntry+0x20 & 1 -- hidden flag bit 0 | Skips this object entirely | NO |
| 7 | 0x46B844 | jne 0x46B877 | objectEntry+0x20 & 0x20000 -- cull flag | Skips this object entirely | NO |
| 8 | 0x46B85A | jne 0x46B877 | sectorData+8 != [0x10E5438] when 0x200000 set | Proximity filter | **YES** (NOP) |

**Gate #4 is a sector-level early exit.** Global at 0x10E5384 is render-pass state. When bits 8-15 are positive AND sector matches current-sector at 0x10E5438, entire sector skipped. NOP the je at 0x46B7F2 (6 bytes).

**Gates #6 and #7** per-object flag checks. NOP both: 0x46B83C (2 bytes) and 0x46B844 (2 bytes).

---

### Function 3: Sector_IterateMeshArray (0x46C320)

| # | Address | Instruction | Condition | What It Skips | Already Patched? |
|---|---------|-------------|-----------|---------------|------------------|
| 9 | 0x46C33E | jne 0x46C34C | [esi+0x5C] & 0x82000000 != 0 | Skips MeshSubmit | **YES** (NOP) |

---

### Function 4: Sector_SubmitObject (0x40C650) -- called FROM Sector_RenderMeshes

THREE early-exit gates before any draw work happens.

| # | Address | Instruction | Condition | What It Skips | Already Patched? |
|---|---------|-------------|-----------|---------------|------------------|
| 10 | 0x40C666 | jne 0x40C920 | _object+0x10 != 0 -- global renderer flag | Entire function | NO |
| 11 | 0x40C674+0x40C67E | jns/je combo | objectEntry+0x20 < 0 AND _object+0x182 == 0 | Negative-flagged objects | NO |
| 12 | 0x40C68B | jne 0x40C920 | [0x10024E8] != 0 -- global submission lock | Entire function | NO |

**Gate #10 is HUGE.** _object+0x10 being non-zero kills ALL object submission.

**Gate #12 is also critical.** Global at 0x10024E8 is a submission lock.

---

### Function 5: MeshSubmit (0x458630) -- called FROM Sector_IterateMeshArray

| # | Address | Instruction | Condition | What It Skips | Already Patched? |
|---|---------|-------------|-----------|---------------|------------------|
| 13 | 0x454AEA | je 0x454AFA | MeshSubmit_VisibilityGate returns 0 (not in PVS) | Entire MeshSubmit | **YES** (RET 0) |
| 14 | top of 0x458630 | iVar4==0 check | VisibilityGate result (unless forceVisible) | MeshSubmit body | Covered by #13 |

---

### Priority NOP Targets (Not Yet Patched)

| Priority | Address | Bytes | Patch | What It Disables |
|----------|---------|-------|-------|------------------|
| **P0** | 0x40C666 | 6 | 0F 85 B4 02 00 00 -> 90x6 | _object+0x10 renderer gate |
| **P0** | 0x40C68B | 6 | 0F 85 8F 02 00 00 -> 90x6 | [0x10024E8] submission lock |
| **P1** | 0x46B7F2 | 6 | 0F 84 8D 00 00 00 -> 90x6 | Sector already-rendered skip |
| **P1** | 0x46B83C | 2 | 75 39 -> 90 90 | Per-object hidden flag (bit 0) |
| **P1** | 0x46B844 | 2 | 75 31 -> 90 90 | Per-object cull flag (bit 17) |
| **P2** | 0x40C674+0x40C67E | 2+6 | NOP both | Negative-flag + renderer byte gate |
| **P3** | 0x40B0F4 | 6 | 0F 84 AC 00 00 00 -> 90x6 | _object+0x20 NULL check |

### Key Addresses

| Address | Description |
|---------|-------------|
| 0x40AE20 | TerrainDrawable_Dispatch -- per-tile terrain draw vtable entry |
| 0x40AE3E | [PATCHED] Flag check & 0x20000 |
| 0x40B0F4 | _object+0x20 NULL gate -- skips terrain DIP |
| 0x40C650 | Sector_SubmitObject -- 3 early-exit gates |
| 0x40C666 | **[CRITICAL]** _object+0x10 gate -- kills ALL submissions |
| 0x40C68B | **[CRITICAL]** [0x10024E8] global gate -- kills ALL submissions |
| 0x46B7D0 | Sector_RenderMeshes -- per-sector iteration |
| 0x46B7F2 | Sector already-rendered skip |
| 0x46B83C | Per-object bit 0 (hidden) filter |
| 0x46B844 | Per-object bit 17 (0x20000 cull) filter |
| 0x46B85A | [PATCHED] Proximity filter |
| 0x46C33E | [PATCHED] Mesh cull flags & 0x82000000 |
| 0x454AB0 | [PATCHED] MeshSubmit_VisibilityGate -- PVS bitfield |
| 0x1392E18 | _object global -- renderer state object |
| 0x10E5384 | Render pass state global |
| 0x10E5438 | Current sector pointer |
| 0x10024E8 | Global submission lock flag |

### Suggested Live Verification

1. **Read _object+0x10**: mem read ptr at 0x1392E18, then read ptr+0x10. If non-zero when draws drop, this is the primary gate.
2. **Read [0x10024E8]**: If non-zero during the drop, NOP 0x40C68B.
3. **Read [0x10E5384]**: Check bits 8-15 during near/far camera positions.
4. **Test P0 patches at runtime**: mem write 0x40C666 909090909090 and mem write 0x40C68B 909090909090, then dipcnt.
5. **Test P1 patches**: mem write 0x46B7F2 909090909090 and mem write 0x46B83C 9090 and mem write 0x46B844 9090.


## Mesh Streaming/LOD System Analysis -- 2026-04-06

### Summary

The draw call drop from ~2,800 to ~178 at distance is caused by the Object Tracker resource management system, NOT conditional culling jumps. The game uses a streaming architecture where mesh data (vertex/index buffers, materials, textures) is loaded on-demand via async file I/O from BIGFILE.DAT. The Object Tracker has a hard limit of 94 slots (MAX_OBJECTS=0x5E) for loaded objects. When all slots are consumed, an eviction function marks least-recently-used objects for unloading. With all sectors forced visible, nearby sectors consume all 94 slots, and distant sector mesh data either never loads or gets immediately evicted.

### Architecture Overview

Three-tier system:
1. Sector Table (8 entries at 0x11582F8, stride 0x5C) -- manages which world sectors are loaded
2. Object Tracker (94 entries at 0x11585D8, stride 0x24) -- manages individual mesh objects within sectors
3. Async File Loader (via 0x5BADD0) -- reads mesh data from BIGFILE.DAT

Object Tracker entry layout (0x24 bytes each):

| Offset | Type | Description |
|--------|------|-------------|
| +0x00 | uint32 | Load request handle |
| +0x04 | uint32 | Hash/resource ID |
| +0x08 | ptr | Object data pointer (mesh header, materials, etc.) |
| +0x0C | int16 | Resource key (index into resource map at [0x10EFC90]) |
| +0x0E | int16 | State: 0=free, 1=loading, 2=loaded, 3=marked-evict, 4=evicting, 5=freed |
| +0x10 | uint32 | Reference count |
| +0x12 | int8 | Dependency count |
| +0x13+ | int8[] | Dependency indices |

Object states:
- 0 (free): Slot available for reuse
- 1 (loading): Async load in progress, NOT yet drawable
- 2 (loaded): Fully loaded, mesh data available for rendering -- only state where draw calls happen
- 3 (marked for eviction): About to be freed
- 5 (freed): Data released, slot pending reuse

Sector table entry layout (0x5C bytes each at 0x11582F8):

| Offset | Type | Description |
|--------|------|-------------|
| +0x00 | int32 | Sector index |
| +0x04 | byte | State: 0=free, 1=loading, 2=loaded, 4=dumping |
| +0x05 | byte | Flags byte 1 |
| +0x06 | uint16 | Flags: bit 0 = KEEP LOADED (protects from eviction!) |
| +0x08 | ptr | Sector data pointer |
| +0x38 | int32 | Last-accessed frame counter |
| +0x3F | byte | Sector type: 1=standard, 2=fullscreen |

### Key Addresses

| Address | Description |
|---------|-------------|
| 0x11585D8 | Object Tracker table base (94 entries x 0x24 bytes) |
| 0x11582F8 | Sector table base (8 entries x 0x5C bytes) |
| 0x5D5390 | ObjectTracker_FindOrLoad -- main lookup; initiates async load if not found |
| 0x5D4240 | ObjectTracker_Resolve -- returns object data ptr (or NULL if not loaded) |
| 0x5D44C0 | ObjectTracker_EvictUnneeded -- marks unreferenced objects as state 3 then frees |
| 0x5D4380 | ObjectTracker_FreeEntry -- releases object data, sets state to 5 then 0 |
| 0x5D4F30 | SectorEviction_ScanAndUnload -- iterates sectors, unloads those not accessed this frame |
| 0x5D4DA0 | Sector_Unload -- unloads a single sector, sets state to 4 |
| 0x5D5B60 | Sector_LoadOrFind -- finds/creates sector entry, initiates async load |
| 0x5D5D80 | Sector_WaitForLoad -- blocks until sector load completes |
| 0x5C3C20 | StreamUnitDataLoaded -- callback when stream unit finishes loading |
| 0x5C2010 | StreamUnitDataDumped -- callback when stream unit is evicted |
| 0x5BADD0 | AsyncLoader_StartLoad -- initiates async file read from BIGFILE.DAT |
| 0x5D5200 | ObjectTracker_LoadComplete -- callback sets state to 2 (loaded/drawable) |
| 0x458630 | MeshSubmit -- submits mesh for rendering; calls ObjectTracker_Resolve for data |
| 0x10E5424 | g_frameCounter -- incremented each frame, used for LRU eviction |
| 0x10EFC90 | Resource map pointer -- maps resource keys to object tracker indices |
| 0x1159314 | Sector-to-Object mapping pointer |

### The Draw Drop Mechanism

1. SectorVisibility_RenderVisibleSectors (0x46C180) iterates sectors, calls Sector_RenderMeshes (0x46B7D0)
2. Sector_RenderMeshes iterates mesh entries, calls Sector_SubmitObject (0x40C650)
3. Inside Sector_SubmitObject, meshes are submitted to the draw queue
4. BUT: MeshSubmit (0x458630) first calls ObjectTracker_Resolve (0x5D4240)
5. ObjectTracker_Resolve calls ObjectTracker_FindOrLoad (0x5D5390)
6. If the object is not in the tracker, it starts an async load and returns state=1 (loading)
7. ObjectTracker_Resolve checks: if state != 2, returns NULL (mesh not drawable yet)
8. MeshSubmit checks: if resolve returned NULL OR type != 2, skips the mesh entirely
9. Result: all sectors are visible but distant mesh data was evicted, so draws = 0 for those sectors

### Eviction Decision Flow

SectorEviction_ScanAndUnload (0x5D4F30) runs during level transitions and evicts sectors when:
  state != 0 AND state != 4 AND lastFrameAccessed != g_frameCounter AND (flags & 1) == 0

ObjectTracker_EvictUnneeded (0x5D44C0) runs when the tracker is full (94/94) and evicts objects when:
  state == 2 AND (flags >> 8) >= 0 AND lastFrameUsed != g_currentFrame
  AND object not referenced by any loaded sector dependency list
  AND object not in g_hiddenMeshList or active scene object list

### Patch Strategies (Ranked by Feasibility)

**Strategy A: Prevent sector eviction (SIMPLEST -- runtime patch)**
NOP the call to SectorEviction_ScanAndUnload at its two call sites:
- 0x5D31D9: E8 52 1D 00 00 -> 90 90 90 90 90 (5 bytes)
- 0x5D5F59: E8 D2 EF FF FF -> 90 90 90 90 90 (5 bytes)
This prevents sectors from being unloaded when a new sector loads. With only 8 sector slots, this limits the game to 8 simultaneously loaded sectors (usually sufficient for one level).

**Strategy B: Prevent object eviction from tracker (MODERATE)**
NOP the call to ObjectTracker_EvictUnneeded (0x5D44C0) at 0x5D5436 (inside ObjectTracker_FindOrLoad).
Risk: if all 94 slots fill up and nothing can be evicted, new objects cannot load -> "Object tracker is full" error.

**Strategy C: Set KEEP_LOADED flag on all sectors (TARGETED)**
At runtime, write bit 0 of the flags word at sector_entry+6 for all 8 entries:
  for each sector at 0x11582F8 + i*0x5C: OR byte at [sector + 6] with 0x01
This tells the eviction scanner to skip that sector. Must be re-applied each frame or after level loads.

**Strategy D: Increase MAX_OBJECTS limit (COMPLEX)**
Change all 8 comparisons against 0x5E to a larger value (e.g., 0xFF = 255). But the object table at 0x11585D8 is a fixed-size array immediately followed by other data at 0x1159310. Would need to relocate the table to a new memory region.

**Strategy E: Force async loads to complete synchronously (NUCLEAR)**
Patch ObjectTracker_FindOrLoad to block (spin-wait) until the async load completes before returning, so state is always 2 by the time MeshSubmit checks it. Risk: stalls the render thread, may cause hitching.

**Strategy F: Patch MeshSubmit to accept state 1 objects (DANGEROUS)**
Change the type == 2 check to type >= 1 so loading-in-progress objects still submit draws. Risk: crashes from accessing partially-loaded mesh data (NULL vertex/index buffers).

### Recommended Approach

**Strategy A + C combined**: Prevent sector eviction AND set keep-loaded flags on all sectors.

1. NOP both calls to SectorEviction_ScanAndUnload (0x5D4F30) -- 10 bytes total
2. Set keep-loaded flag (bit 0 at sector+6) on all 8 entries each frame

The 94-slot object tracker limit may still cause issues if all 8 sectors objects exceed 94 total. In that case, also NOP the eviction call inside ObjectTracker_FindOrLoad (Strategy B) and accept that the game may log "Object tracker is full" but will not crash.

### Distance-Related Globals

| Address | Value | Purpose |
|---------|-------|---------|
| 0xEFDDB0 | 0.001 | g_objectDistanceCullThreshold -- post-sector objects |
| 0xEFD40C | 1.0 | g_objectDistanceBoundary -- distance boundary |
| 0xEFDE58 | 2.0 | g_maxObjectDrawDistance -- clamp |
| 0xEFDE50 | 255.0 | g_farLODThreshold |
| 0xF11D0C | 512.0 | g_viewDistance -- base view distance |

Post-sector distance formula: cull if g_objectDistanceCullThreshold > (g_objectDistanceBoundary - objectDistance) * scaleFactor

Setting g_objectDistanceBoundary (0xEFD40C) to a very large value (e.g., 100000.0) at runtime would disable post-sector distance culling. But this alone will not help since the underlying mesh data is still not loaded.

### Suggested Live Verification

1. Attach livetools and read the object tracker table:
   mem read 0x11585D8 0x870 -- dump all 94 entries
   Count how many have state==2 (loaded) vs state==0 (free)
   Near stage: expect many objects loaded
   Far from stage: check if objects get evicted to state 0/5

2. Set breakpoint on ObjectTracker_EvictUnneeded (0x5D44C0):
   bp add 0x5D44C0 -- check if eviction fires during normal gameplay

3. Test Strategy A at runtime:
   mem write 0x5D31DA 9090909090 -- NOP first eviction scanner call
   mem write 0x5D5F5A 9090909090 -- NOP second eviction scanner call
   Walk far from stage, check if draw count stays at ~2800

4. Set keep-loaded flags at runtime:
   mem write 0x11582FE 01 -- sector 0 flags |= 1
   mem write 0x115835A 01 -- sector 1 flags |= 1
   (etc. for all 8, stride 0x5C)

5. Increase distance globals at runtime:
   mem write 0xEFD40C 461C4000 -- set boundary to 10000.0
   mem write 0xEFDE58 461C4000 -- set max draw distance to 10000.0

## Portal/PVS Traversal System — Complete Architecture — 2026-04-06

### Summary

The portal/PVS system has been fully reverse-engineered. Geometry disappearing at distance comes from the **sector mesh rendering path** (not the scene graph). The sector system uses a portal walk from the camera's sector to determine which of 8 loaded sectors are reachable. Unreachable sectors get their screen-space bounding rectangles reset to negative (impossible) values by `SectorPortalVisibility` (0x46D1D0), causing them to fail the width/height check in `SectorVisibility_RenderVisibleSectors` (0x46C180). A 15-byte patch at `0x46D1E0-0x46D1EE` forces all sector bounds to fullscreen defaults, ensuring every loaded sector renders regardless of portal reachability.

### Architecture: Two Independent Rendering Paths

**Path 1 — Sector Mesh Rendering** (the path that drops geometry at distance):

```
RenderFrame (0x450B00)
  GetCameraSector (0x450A80)          — find camera's current sector
  SetupCameraSector (0x46C4F0)        — portal walk from camera sector
      portal loop: stride 0xA0 per portal connection entry
      for reachable sectors: compute screen-space bounding rect [+0x3C..+0x42]
  SectorPortalVisibility (0x46D1D0)   — reset all bounds to "invisible", set portal flags
      RESETS all sector bounds to negative width/height
      for portal-visible sectors: ORs visibility into sector[+0x44]
  SectorVisibility_RenderVisibleSectors (0x46C180) — renders sectors passing checks
      for each of 8 sector table entries:
        CHECK: sector[+6] & 8 (loaded flag — always set for loaded sectors)
        CHECK: sector[+5] == 0 (no special state)
        CHECK: sectorType at [+0x43]: 1=standard, 2=fullscreen
        CHECK: screen width >= g_minSectorScreenWidth (0x10FC920)
        CHECK: screen height >= g_minSectorScreenHeight (0x10FC924)
        Sector_RenderMeshes (0x46B7D0) — submit mesh draw calls
```

**Path 2 — Scene Graph Rendering** (effects, particles, billboards — NOT the problem):

```
RenderFrame (0x450B00)
  RenderScene (0x443C20)
      SceneTraversal_CullAndSubmit (0x407150)
          list at sceneGraph+0x24: mesh effect nodes (linked list walk)
          list at sceneGraph+0x2c: billboard/sprite nodes (linked list walk)
      SceneGraphBuilder (0x436AF0 -> 0x408130)
          list at sceneGraph+0x34: scene objects with frustum culling
```

### The Portal Walk Mechanism (0x46C4F0)

SetupCameraSector implements portal-based sector discovery:

1. Gets camera sector via `GetCameraSector` (0x450A80):
   - Reads player struct at `[0x10FCAE8]+0xB2` (sector index)
   - Calls `Sector_FindByIndex` (0x5D4870) to look up in sector table
   - Falls back to `g_fallbackSectorIndex` (0x10E5458) if primary fails
   - Camera override object at `0x10E579C` can redirect via vtable[0x28]

2. Portal connection array (per-sector):
   - Located at `[sectorData+8] -> [[+0]+0x10]`
   - Count at `[[+0]+0xC]`
   - Each entry stride 0xA0, containing:
     - `[+0x04]`: pointer to connected sector entry
     - `[+0x28..+0x68]`: portal plane/geometry data (normals, extents)
     - `[+0x90..+0x98]`: portal normal vector

3. Per-portal checks:
   - Connected sector loaded? (`[sector+4] == 2`)
   - Connected sector active? (`[sector+6] & 8`)
   - Camera on correct side of portal plane? (dot product test at `0x46D0A0`)
   - Portal visible through current frustum? (rect clipping at `0x46C360`)

4. For visible portals: computes screen-space bounding rect in sector[+0x3C..+0x42]

### The Bounds Reset — Root Cause (0x46D1D0)

After the portal walk, `SectorPortalVisibility` runs and resets ALL sector bounds:

```asm
0x46D1DB: mov  eax, 0x1158336      ; sector table + 0x3E (loop start)
0x46D1E0: mov  edx, 0x200          ; default X = 512
0x46D1E5: mov  edi, 0xFFFFFE00     ; default W = -512 (MAKES WIDTH NEGATIVE)
0x46D1EA: mov  esi, 0xFFFFFE40     ; default H = -448 (MAKES HEIGHT NEGATIVE)
0x46D1EF: xor  ebp, ebp            ; zero for flags
; Loop over all 8 sectors (stride 0x5C):
0x46D1F1: mov  [eax-2], dx         ; sector[+0x3C] = 0x200 (x)
0x46D1F5: mov  [eax], 0x1C0        ; sector[+0x3E] = 0x1C0 (y)
0x46D1FA: mov  [eax+2], di         ; sector[+0x40] = 0xFE00 (w = -512)
0x46D1FE: mov  [eax+4], si         ; sector[+0x42] = 0xFE40 (h = -448)
0x46D202: mov  [eax+6], ebp        ; sector[+0x44] = 0 (portal flags)
0x46D205: mov  [eax+0xA], ebp      ; sector[+0x48] = 0
```

Screen width calculation: `(x + w) * scale - x * scale = w * scale = -512 * scale` (NEGATIVE).
Width check at 0x46C237: `-512 * scale < threshold` → **fails** → sector skipped.

Sectors reachable through portals have their bounds overwritten by `SetupCameraSector` with proper positive values. Unreachable sectors keep the negative defaults and are never rendered.

### Sector Table Layout (8 entries at 0x11582F8, stride 0x5C)

```
Offset  Size   Field               Description
+0x00   dword  sectorIndex         Sector ID number
+0x04   byte   sectorState         0=free, 1=loading, 2=loaded, 4=dumping
+0x05   byte   flags1              General flags
+0x06   byte   flags2              Bit 3 (0x08) = loaded/visible flag
+0x08   ptr    sectorDataPtr       Pointer to sector data structure
+0x24   ptr    pendingLoadPtr      Pending load data
+0x3C   word   boundX              Screen-space bounding rect X
+0x3E   word   boundY              Screen-space bounding rect Y
+0x40   word   boundW              Screen-space bounding rect W (signed)
+0x42   word   boundH              Screen-space bounding rect H (signed)
+0x43   dword  sectorType          1=standard mesh sector, 2=fullscreen overlay
+0x44   dword  portalFlags         Portal visibility result bits
+0x48   dword  reserved            Cleared each frame
```

### Key Addresses

| Address | Description |
|---------|-------------|
| 0x450B00 | RenderFrame — main render pipeline orchestrator |
| 0x450A80 | GetCameraSector — camera sector lookup from player struct |
| 0x46C4F0 | SetupCameraSector — portal walk, builds sector bounding rects |
| 0x46D0A0 | PortalVisibilityTest — camera-vs-portal plane dot product |
| 0x46C360 | PortalRectClip — 2D portal rectangle clipping |
| 0x46D1D0 | SectorPortalVisibility — resets bounds, applies portal flags |
| **0x46D1E0** | **PATCH: `mov edx, 0x200`** — default X position |
| **0x46D1E5** | **PATCH: `mov edi, 0xFFFFFE00`** — negative width default |
| **0x46D1EA** | **PATCH: `mov esi, 0xFFFFFE40`** — negative height default |
| 0x46C180 | SectorVisibility_RenderVisibleSectors — sector render loop |
| 0x46C237 | Width >= threshold check (fcomp against 0x10FC920) |
| 0x46C242 | Width check skip jump (jnp) — 2 bytes |
| 0x46C250 | Height >= threshold check (fcomp against 0x10FC924) |
| 0x46C25B | Height check skip jump (jnp) — 2 bytes |
| 0x403720 | Sector_ActivateAndResolveTextures — sets sector[+6] \|= 8 |
| 0x5D59D7 | Caller of Sector_ActivateAndResolveTextures (sector load path) |
| 0x46B900 | Sector_GetCameraFrustumParams — frustum data per sector |
| 0x436AF0 | SceneGraphBuilder — calls 0x408130 if sceneGraph+0x34 list exists |
| 0x408130 | SceneObjectWalker — iterates scene object linked list with culling |
| 0x45BD30 | ListLinker — doubly-linked list insert for scene graph nodes |
| 0x436110 | SceneNodeAllocator — allocates from pool at sceneGraph+0x38/+0x40 |
| 0x11582F8 | g_sectorTable — 8 sector entries |
| 0x10FC920 | g_minSectorScreenWidth — float threshold |
| 0x10FC924 | g_minSectorScreenHeight — float threshold |
| 0x107F6B8 | Scene graph root (billboards/effects, NOT sector meshes) |
| 0x10FCAE8 | g_pPlayerCamera — player struct, sector index at +0xB2 |
| 0x10E579C | g_pCameraOverrideObj — overrides sector lookup |
| 0x10E5438 | g_currentSectorPtr — written from camera sector during render |

### Proposed Patches

#### Patch A (RECOMMENDED) — Force Fullscreen Sector Bounds

Change the "invisible" default bounds in the reset loop so every loaded sector has fullscreen-filling bounding rects. 15 bytes total.

```
0x46D1E0: BA 00 02 00 00 → 31 D2 90 90 90   ; mov edx,0x200 → xor edx,edx; nop*3
                                               ; x = 0 (left edge at screen left)
0x46D1E5: BF 00 FE FF FF → BF 00 02 00 00   ; mov edi,0xFFFFFE00 → mov edi,0x200
                                               ; w = +512 (right = x+w = 512 = full width)
0x46D1EA: BE 40 FE FF FF → BE C0 01 00 00   ; mov esi,0xFFFFFE40 → mov esi,0x1C0
                                               ; h = +448 (bottom = y+h = 0x1C0+0x1C0 = full height)
```

Effect: All 8 sectors get bounds (x=0, y=0x1C0, w=0x200, h=0x1C0):
- Screen width = w * scale = 512 * scale (PASSES any threshold)
- Screen height = h * scale = 448 * scale (PASSES any threshold)

The portal walk in SetupCameraSector still runs and may overwrite bounds for portal-visible sectors with tighter values, but unreachable sectors now get fullscreen bounds instead of impossible negative bounds.

Runtime (livetools) equivalent:
```
mem write 0x46D1E0 31D2909090           ; xor edx,edx; nop*3
mem write 0x46D1E5 BF00020000           ; mov edi, 0x200
mem write 0x46D1EA BEC0010000           ; mov esi, 0x1C0
```

#### Patch B (ALTERNATIVE) — NOP Width/Height Checks

NOP the conditional jumps that skip rendering when sector bounds are too small. 4 bytes total.

```
0x46C242: 75 71 → 90 90    ; jnp 0x46C2B5 → nop nop (width check)
0x46C25B: 75 58 → 90 90    ; jnp 0x46C2B5 → nop nop (height check)
```

Simpler patch but less clean: leaves the FPU compare instructions running with no consumer. Also means the scissor rect for unreachable sectors will have garbage values (the negative defaults), which may cause visual artifacts.

Runtime equivalent:
```
mem write 0x46C242 9090
mem write 0x46C25B 9090
```

#### Patch C (BELT AND SUSPENDERS) — Both A + B

Apply both patches for maximum robustness. Patch A gives unreachable sectors valid bounds, Patch B removes the check entirely as defense-in-depth.

### SceneTraversal_CullAndSubmit (0x407150) — Clarification

This function is NOT part of the portal/sector system. It walks pre-built linked lists of scene EFFECT nodes (particles, sprites, billboards) that are populated by game code (particle spawners, effect systems). The linked lists are:
- `sceneGraph+0x24`: mesh effect nodes (first loop, walk via `node+0x04` next pointer)
- `sceneGraph+0x2c`: billboard/sprite nodes (second loop, same walk pattern)

Each node has: `[+0x00]` = list pointer, `[+0x04]` = next, `[+0x08]` = flags, `[+0x0C]` = mesh ID, `[+0x10..+0x1C]` = position, `[+0x20..+0x3C]` = bounding box, `[+0x3C]` = color/alpha, `[+0x8C]` = data pointer.

The existing RET patch at 0x407150 disables all culling within this function, but this has NO effect on sector mesh rendering (path 1). Both paths are completely independent.

### Suggested Live Verification

1. Apply Patch A at runtime via livetools:
   ```
   mem write 0x46D1E0 31D2909090
   mem write 0x46D1E5 BF00020000
   mem write 0x46D1EA BEC0010000
   ```

2. Walk Lara away from the stage lights — verify they remain visible.

3. Monitor sector table visibility to confirm all sectors render:
   ```
   mem read 0x11582F8 0x2E0 --as hex    ; read all 8 sector entries
   ```

4. If Patch A alone fails, add Patch B:
   ```
   mem write 0x46C242 9090
   mem write 0x46C25B 9090
   ```

5. Check for render artifacts — if sectors render with wrong scissor rects:
   ```
   mem read 0x10FC900 16 --as float32   ; read current scissor rect values
   ```

6. If both patches work: promote to proxy C source in `TRL_ApplyMemoryPatches`.
