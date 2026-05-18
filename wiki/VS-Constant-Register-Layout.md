## VS Constant Register Layout Analysis — 2026-03-22

### Summary

Tomb Raider Legend uses a custom renderer that wraps D3D9 SetVertexShaderConstantF calls through `Renderer_SetVSConstantF` at 0x00ECBA40. The engine uploads matrices in transposed form (column-major for HLSL) via a dedicated transpose function at 0x00ECBAA0. The main matrix upload path at 0x00ECBB00 uploads two batches of 8 float4 registers: c0-c7 for WorldViewProjection-related matrices and c8-c15 for View/Projection-related matrices. There are 34 call sites to the generic SetVSConstantF wrapper, covering registers c0 through c96.

### Key Addresses

| Address | Description |
|---------|-------------|
| 0x00ECBA40 | `Renderer_SetVSConstantF` — generic wrapper, forwards (startReg, data, count) to device->SetVertexShaderConstantF |
| 0x00ECBB00 | `Renderer_UploadViewProjMatrices` — uploads transposed matrices to c0-c7 and c8-c15 |
| 0x00ECBAA0 | `MatrixTranspose4x4` — transposes 4x4 matrix (row-major to column-major) |
| 0x005DD910 | `MatrixMultiply4x4` — standard 4x4 matrix multiply: result = matA * matB |
| 0x00402990 | `MatrixCopy4x4` — copies 16 floats (one 4x4 matrix) |
| 0x00ECBC20 | `Renderer_SetBlendMode` — 20-case switch setting render states (NOT SetVertexShaderConstantF despite 0xECC3C4 being listed) |
| 0x00ECC180 | `Renderer_Init` — initializes device, sets c39 = {2.0, 0.5, 0.0, 1.0} |
| 0x01392E18 | `g_pEngineRoot` — global engine root pointer, device at [root+0x20] |

### VS Constant Register Map

| Register(s) | StartReg | Count | Purpose | Evidence |
|-------------|----------|-------|---------|----------|
| c0-c3 | 0 | 4 | **World matrix** (transposed) | Set at 0x40B07F with count 4; also first 4 regs of 8-reg batch from ECBB00 |
| c0-c7 | 0 | 8 | **WorldViewProjection** (two transposed 4x4 matrices) | Set in ECBB00 second path: MatMul(this+0x500, this+0x540) then transpose both |
| c4-c7 | 4 | 4 | **Fog / lighting parameters** | Set at 0x413F06 |
| c6 | 6 | 1 | **Fog distance** {0, 0, fogEnd, epsilon} | Set at 0x413C42 and 0x415E06 with value {0, 0, dist, 0x39800000} |
| c8 | 8 | 1 | **Misc per-object param** | Standalone upload at 0x41415A |
| c8-c15 | 8 | 8 | **ViewProjection** (two transposed 4x4 matrices) | Set in ECBB00 first path: MatMul(this+0x480, this+0x4C0) then transpose both |
| c16 | 16 | N | **Bone/skin matrices** (per-object, variable count) | Set at 0x40AA4E with count = numBones*2 |
| c17 | 17 | 1 | **Per-draw constant** (depth bias?) | Set at 0x60FAA4: {value, 0, 0, 0} |
| c18 | 18 | 1-2 | **Ambient / material color** | Set at 0x60C88A, 0x60BA74, 0x60FA1E (sometimes count 2) |
| c19 | 19 | 1 | **Light direction / color** | Set at 0x60C860 |
| c21 | 21 | 1 | **Object-space camera data** | Set at 0x60A618 |
| c22-c23 | 22 | 2 | **Normal map / additional light data** | Set at 0x60FA39, 0x60FA54 |
| c24-c27 | 24 | 4 | **Texture transform / UV animation** | Set at 0x60EF65 and 0x60FB3F |
| c28 | 28 | 1 | **Per-object parameter** | Set at 0x413F75 |
| c30 | 30 | 1 | **Screen / viewport params** | Set at 0x60BA5B and 0x60DB6B (near/far plane data) |
| c30+ | 30 | N | **Dynamic per-draw** (shared with c30, context-dependent) | Variable uses via add ecx, 0x1e at 0x604ADF |
| c37 | 37 | 1 | **Light parameter** {value, 0, 0, 0} | Set at 0x60FB73 |
| c38 | 38 | 1 | **Scale / bias** | Set at 0x60A72F (multiplied by 2.0 broadcast), 0x618255 from static data at 0xF08B38 |
| c39 | 39 | 1 | **Utility constants** {2.0, 0.5, 0.0, 1.0} | Set once in Renderer_Init at 0xECC3C4; hex: {0x40000000, 0x3F000000, 0, 0x3F800000} |
| c40-c41 | 40 | 2 | **Camera position / parameters** | Set at 0x40AEDC |
| c44 | 44 | 1 | **Camera direction** {x, y, z, 0.5} | Set at 0x40AF1B with last component = 0x3F000000 (0.5) |
| c48+ | 48 | N | **Skinning / bone matrices** (alternative slot) | Set at 0x60ECFD and 0x6133D7, count varies (up to 8+) |
| c96 | 96 | 1 | **Far clip / depth params** | Set at 0x6052B2, 0x605300 from offset 0xFC70/0xFC80 in a data structure |

### Matrix Upload Path Detail (ECBB00)

The function `Renderer_UploadViewProjMatrices` at 0x00ECBB00 is the primary matrix upload path, called from the render loop at 0x60182A, 0x60C89F, 0x610CEF, 0x6133B1, and 0x601FD8.

**First batch (c8-c15, startRegister=8, count=8):**
1. Multiplies `this+0x480` (view source A) by `this+0x4C0` (view source B) via `MatrixMultiply4x4`
2. Copies result into `this+0x500` via `MatrixCopy4x4`
3. Transposes `this+0x4C0` into buffer[0..3] (4 float4s)
4. Transposes `this+0x500` (the multiply result) into buffer[4..7] (4 float4s)
5. Calls `SetVertexShaderConstantF(device, 8, buffer, 8)`

**Second batch (c0-c7, startRegister=0, count=8):**
1. Multiplies `this+0x500` (world source A) by `this+0x540` (world source B) via `MatrixMultiply4x4`
2. Transposes the result into buffer[0..3] (4 float4s)
3. Transposes `this+0x540` into buffer[4..7] (4 float4s)
4. Calls `SetVertexShaderConstantF(device, 0, buffer, 8)`
5. Clears dirty flags at `this+0x580` and `this+0x581`

The dirty flags at `this+0x580` (view dirty) and `this+0x581` (proj dirty) control which batch gets uploaded. If only the view is dirty, only c8-c15 is updated. If either projection or both are dirty, both batches are uploaded.

### Transpose Function Detail (ECBAA0)

The transpose maps source indices to destination as follows:
```
dst[0]  = src[0]    dst[1]  = src[4]    dst[2]  = src[8]    dst[3]  = src[12]
dst[4]  = src[1]    dst[5]  = src[5]    dst[6]  = src[9]    dst[7]  = src[13]
dst[8]  = src[2]    dst[9]  = src[6]    dst[10] = src[10]   dst[11] = src[14]
dst[12] = src[3]    dst[13] = src[7]    dst[14] = src[11]   dst[15] = src[15]
```
This is a standard row-major to column-major transpose, confirming the engine stores matrices in row-major order internally but uploads them transposed (column-major) for HLSL shaders.

### Blend Mode Function (ECBC20) — NOT SetVertexShaderConstantF

The function at 0x00ECBC20 was listed as containing a SetVertexShaderConstantF call at 0xECC3C4, but that address actually belongs to the `Renderer_Init` function at 0x00ECC180. The ECBC20 function is a render state setter with a 20-case switch that maps blend mode IDs to D3D render states via `SetRenderState` (vtable 0xE4). The D3D render state constants used:

| Hex | D3DRENDERSTATETYPE |
|-----|--------------------|
| 0x0E | D3DRS_ZWRITEENABLE |
| 0x0F | D3DRS_ALPHATESTENABLE |
| 0x13 | D3DRS_SRCBLEND |
| 0x14 | D3DRS_DESTBLEND |
| 0x18 | D3DRS_ALPHAREF |
| 0x19 | D3DRS_ALPHAFUNC |
| 0x1B | D3DRS_ALPHABLENDENABLE |
| 0x22 | D3DRS_FOGCOLOR |
| 0xAB | D3DRS_SEPARATEALPHABLENDENABLE |
| 0xC1 | D3DRS_TEXTUREFACTOR |

### Indirect Reference Clarification

Several of the "indirect" addresses listed (0x569A55, 0x57FA8A, 0x415D0E, 0x440EDE, 0x584512) read `[struct + 0x178]` where the 0x178 is a **struct field offset**, not a vtable call. These are NOT SetVertexShaderConstantF calls. They read a field at offset +0x178 in a game-specific struct (likely a camera/animation state index). The 0x415D0E site reads it to check a condition before calling SetRenderState.

### Suggested Live Verification

- Trace `0x00ECBA40` with `--read` to capture actual startRegister values and data pointers at runtime: `livetools trace 0x00ECBA40 --count 50 --read esp+4:uint16 esp+8:ptr esp+12:uint16`
- Trace `0x00ECBB00` to confirm it's called per-frame and the dirty flags drive upload: `livetools trace 0x00ECBB00 --count 20`
- Set breakpoint at `0x00ECBA40` and inspect the float data at [esp+8] to verify matrix contents for different register slots
- Use `livetools memwatch` on `this+0x480` and `this+0x500` to see when matrices are written
- Capture a dx9tracer frame and analyze with `--const-provenance` to see the full per-draw constant layout

## Culling Analysis — 2026-03-23

### Summary

TRL has two distinct culling systems: (1) D3D hardware backface culling via `SetRenderState(D3DRS_CULLMODE, ...)` controlled by render state bit 0x200000 in function 0x40EE8C, and (2) software frustum/visibility culling in the large scene traversal function at 0x4072A0 that checks object distance against a threshold at 0xEFDD64 (value: 16.0) and screen-boundary checks against globals at 0xEFD404/0xEFD40C. The frustum culling is the cause of objects disappearing when the camera turns -- it removes objects from the draw list before any D3D9 calls are made.

No code writes to 0xEFDD64; it is a read-only .rdata constant (16.0f = 0x41800000). Patching it in the PE or in memory will persist.

### Key Addresses

| Address | Description |
|---------|-------------|
| 0x0040EE8C | `Renderer_ApplyRenderStateChanges` — function that processes dirty render state bits |
| 0x0040EEA5 | `je 0x40eec6` (2 bytes) — skips D3DRS_CULLMODE block if bit 0x200000 not dirty |
| 0x0040EEA7 | `test ebx, 0x200000` (6 bytes) — tests if cull mode should be CW or CCW |
| 0x0040EEBB | `call 0x40E470` — Renderer_SetRenderStateCached(0x16, value) for D3DRS_CULLMODE |
| 0x004072A0 | `FrustumCull_SceneTraversal` — main visibility/frustum culling loop |
| 0x004072AF | `fcomp [0xEFDD64]` — first distance-vs-threshold check (skips object if distance < 16.0) |
| 0x004072BD | `jne 0x4078CD` (6 bytes) — **SKIP OBJECT** if distance below threshold |
| 0x004072C7 | `fcomp [0xEFDD64]` — second distance-vs-threshold check |
| 0x004072D2 | `jne 0x4078CD` (6 bytes) — **SKIP OBJECT** if second distance below threshold |
| 0x00407AE3 | `fcomp [0xEFDD64]` — distance threshold check in alternate draw path |
| 0x00407AF1 | `jnp 0x40804E` (6 bytes) — **SKIP OBJECT** if distance below threshold |
| 0x00407B25 | `fcomp [0xEFD404]` — screen-boundary check (left/top) |
| 0x00407B30 | `jne 0x40804E` (6 bytes) — skip if off-screen left/top |
| 0x00407B3E | `fcomp [0xEFD404]` — second screen boundary |
| 0x00407B49 | `jne 0x40804E` (6 bytes) — skip if off-screen |
| 0x00407B57 | `fcomp [0xEFD40C]` — screen-boundary check (right/bottom) |
| 0x00407B62 | `jp 0x40804E` (6 bytes) — skip if off-screen right/bottom |
| 0x00407B70 | `fcomp [0xEFD40C]` — fourth screen boundary |
| 0x00407B7B | `jp 0x40804E` (6 bytes) — skip if off-screen |
| 0x00EFDD64 | Frustum distance threshold constant: 16.0f (0x41800000) — READ-ONLY, no writers |
| 0x00EFD404 | Screen boundary min (viewport left/top cull limit) |
| 0x00EFD40C | Screen boundary max (viewport right/bottom cull limit) |
| 0x010FC910 | Far clip distance (used at 0x407AFB) |

### Details

#### 1. D3DRS_CULLMODE Control (0x40EEA7)

The function at 0x40EE8C processes a bitmask of dirty render states. Bit 0x200000 controls D3DRS_CULLMODE (state 0x16 = 22). The code at 0x40EEA7:

```asm
0x0040EE9F: test     ebp, 0x200000      ; check if cull mode dirty bit set
0x0040EEA5: je       0x40eec6           ; skip if not dirty (2-byte jump)
0x0040EEA7: test     ebx, 0x200000      ; check desired cull state
0x0040EEAD: mov      ecx, 0             ; ecx = 0
0x0040EEB2: setne    cl                  ; cl = (ebx & 0x200000) ? 1 : 0
0x0040EEB5: inc      ecx                ; ecx = 1 (CCW) or 2 (CW)
0x0040EEB6: push     ecx                ; value: D3DCULL_CCW(1) or D3DCULL_CW(2)
0x0040EEB7: push     0x16               ; D3DRS_CULLMODE
0x0040EEB9: mov      ecx, esi           ; this
0x0040EEBB: call     0x40e470           ; Renderer_SetRenderStateCached
0x0040EEC0: and      ebp, 0xffdfffff    ; clear dirty bit
```

**To force D3DCULL_NONE**: NOP the entire block from 0x40EEA7 to 0x40EEBF (24 bytes: `test ebx` through `call`), OR patch the `push ecx` at 0x40EEB6 to `push 1` (D3DCULL_NONE). But this only controls hardware backface culling, not the object visibility problem.

**Instruction at 0x40EEA7**: `test ebx, 0x200000` = bytes `F7 C3 00 00 20 00` = **6 bytes**.

#### 2. Frustum/Distance Culling (0x4072A0) — THE MAIN PROBLEM

The large function at 0x4072A0 is the scene object traversal loop. For each object, it checks:

**Check A (distance threshold, early path):**
```asm
0x004072AB: fld      dword ptr [esp + 0x68]    ; load object distance (axis 1)
0x004072AF: fcomp    dword ptr [0xefdd64]       ; compare with 16.0
0x004072B5: fnstsw   ax
0x004072BA: test     ah, 0x41                   ; check if distance <= 16.0
0x004072BD: jne      0x4078cd                   ; SKIP OBJECT if too close (6 bytes: 0F 85 xx xx xx xx)

0x004072C3: fld      dword ptr [esp + 0x78]    ; load object distance (axis 2)
0x004072C7: fcomp    dword ptr [0xefdd64]       ; compare with 16.0
0x004072CD: fnstsw   ax
0x004072CF: test     ah, 0x41                   ; check if distance <= 16.0
0x004072D2: jne      0x4078cd                   ; SKIP OBJECT if too close (6 bytes)
```

**Check B (distance threshold, draw path):**
```asm
0x00407ADF: fld      dword ptr [esp + 0x60]    ; load distance
0x00407AE3: fcomp    dword ptr [0xefdd64]       ; compare with 16.0
0x00407AE9: add      esp, 8
0x00407AEC: fnstsw   ax
0x00407AEE: test     ah, 5                      ; check if distance < 16.0
0x00407AF1: jnp      0x40804e                   ; SKIP OBJECT if below threshold (6 bytes)
```

**Check C (screen-boundary culling at 0x407B25-0x407B7B):**
After distance passes, 4 screen-boundary checks cull objects outside the viewport using globals at 0xEFD404 (min) and 0xEFD40C (max). Each check is `fcomp + fnstsw + test + jcc` with a 6-byte conditional jump to 0x40804E.

#### 3. Frustum Threshold at 0xEFDD64

Value: **16.0f** (0x41800000). All 15 references are reads (fcomp, fld, fmul, fadd, fsubr, movss). No code writes to this address. It is a constant in .rdata.

Additional code sites that use this threshold:
- 0x41F96A — another object visibility check
- 0x446B5A, 0x446BE0 — particle/effect distance culling
- 0x448580, 0x451390 — scale/LOD calculations using threshold as multiplier
- 0x4B16B5, 0x5A9229 — more distance comparisons

#### 4. String Evidence

- `"Disable extra static light culling and fading"` at 0xEFF384 — a debug/config string referenced from a table at 0xF1325C (not directly from code). Suggests the engine has a configurable culling toggle, but it may only affect lights.

### NOP Patch Plan — Disable All Frustum Culling

**Option A: Patch the threshold (simplest, broadest effect)**
Change the float at 0xEFDD64 from 16.0 (0x41800000) to a very small value like 0.0 (0x00000000) or negative (-1.0 = 0xBF800000). Since the checks skip objects when `distance <= threshold`, setting threshold to 0 or negative means objects are never skipped by distance. This affects all 15 reference sites.

Patch: `mem write 0xEFDD64 00000000` (set to 0.0)

**Option B: NOP the conditional jumps (surgical, only in main traversal)**

| Address | Instruction | Size | NOP bytes |
|---------|-------------|------|-----------|
| 0x4072BD | `jne 0x4078CD` | 6 | `90 90 90 90 90 90` |
| 0x4072D2 | `jne 0x4078CD` | 6 | `90 90 90 90 90 90` |
| 0x407AF1 | `jnp 0x40804E` | 6 | `90 90 90 90 90 90` |
| 0x407B30 | `jne 0x40804E` | 6 | `90 90 90 90 90 90` |
| 0x407B49 | `jne 0x40804E` | 6 | `90 90 90 90 90 90` |
| 0x407B62 | `jp 0x40804E`  | 6 | `90 90 90 90 90 90` |
| 0x407B7B | `jp 0x40804E`  | 6 | `90 90 90 90 90 90` |

Total: 42 bytes of NOPs across 7 sites in the main traversal function.

**Option C: Patch the threshold AND NOP screen-boundary checks**
Combine Option A (threshold = 0.0) with NOPing the 4 screen-boundary jumps (0x407B30, 0x407B49, 0x407B62, 0x407B7B). This ensures objects aren't culled by distance OR by being "off-screen" in the engine's calculation.

**Recommendation**: Start with Option A (patch 0xEFDD64 to 0.0) as it's a single 4-byte write affecting all distance culling globally. If objects still disappear at screen edges, add the screen-boundary NOP patches from Option B.

### Additional Culling Sites (secondary)

These also use the 0xEFDD64 threshold and may need patching if objects still disappear:

| Address | Context |
|---------|---------|
| 0x41F96A | Object visibility check — similar fcomp + jne pattern |
| 0x439AE4 | Particle/effect distance culling |
| 0x439BDB | Particle/effect distance culling (second path) |
| 0x446B5A | Effect/sprite distance culling |
| 0x446BE0 | Effect/sprite distance culling (second axis) |
| 0x4B16B5 | Unknown object distance check |
| 0x5A9229 | Unknown object distance check |

### Suggested Live Verification

- `livetools mem write 0xEFDD64 00000000` — set threshold to 0.0, see if objects stop disappearing
- `livetools mem write 0xEFDD64 00000080` — set threshold to very small negative, more aggressive
- `livetools collect 0x4072BD 0x407AF1 --duration 5` — count how often the cull jumps are taken
- `livetools trace 0x4072AB --count 10 --read esp+0x68:float` — see actual distance values being compared
- If threshold patch works, make it permanent with ASI patcher or PE edit at file offset for 0xEFDD64
- `livetools memwatch start 0xEFD404 4` — watch if screen boundary globals change (they may be viewport-dependent)

## Crash Analysis at 0x0040EE6C — 2026-03-23

### Summary

The crash "Write to address 0x0000000F" at reported EIP 0x0040EE6C does NOT correspond to a valid instruction boundary. Address 0x0040EE6C falls at offset +2 inside the 5-byte instruction `mov ebx, 8` (bytes `BB 08 00 00 00` at 0x0040EE6A). This instruction does not perform any memory write, so the crash is either: (a) happening inside one of the calls made just before this point, with the crash reporter showing the return address rather than the faulting EIP, or (b) the address refers to the faulting data address rather than the EIP.

The most likely crash scenario: the function `SetTextureStageState_Cached` at 0x0040E980 is called at 0x0040EE6F (the very next instruction after `mov ebx, 8`), or `Renderer_SetRenderStateCached` at 0x0040E470 is called multiple times just before at 0x40EE41/0x40EE4F/0x40EE5A/0x40EE65. These functions dereference the D3D device pointer at `[esi + 0xC]` (or via g_pEngineRoot), and if the device vtable pointer is corrupt or the proxy DLL's vtable is not fully initialized at the time of the first call, dereferencing it could crash.

Given EAX=0x0000000F: in the `SetTextureStageState_Cached` function at 0x40E980, `mov eax, [0x1392e18]` loads g_pEngineRoot, then `mov edi, [eax + 0x214]`, then `mov eax, [edi + 0xC]` loads the device pointer. If g_pEngineRoot or the sub-object chain is corrupt/uninitialized, EAX could end up as 0xF. The subsequent `mov ecx, [eax]` at 0x40E9A2 would then attempt to read from address 0xF, causing the access violation.

### Key Addresses

| Address | Description |
|---------|-------------|
| 0x0040EAB0 | Actual function start (prologue: `push ebx; mov ebx, [esp+8]`) -- this is the real `Renderer_ApplyRenderStateChanges` |
| 0x0040EE34 | Switch case for blend mode 0x12000 -- the case that contains the crash area |
| 0x0040EE3B-0x0040EE65 | Four calls to `Renderer_SetRenderStateCached(0x1B=1, 0xAB=1, 0x13=2, 0x14=1)` |
| 0x0040EE6A | `mov ebx, 8` -- sets texture stage state mask to 8 |
| 0x0040EE6F | `call 0x40E980` -- calls `SetTextureStageState_Cached(mask=8)` |
| 0x0040E980 | `SetTextureStageState_Cached` -- reads g_pEngineRoot -> [+0x214] -> [+0xC] device, calls `[vtable + 0xE4]` (SetRenderState via vtable offset 0xE4) |
| 0x0040E470 | `Renderer_SetRenderStateCached` -- reads `[this + 0xC]` as device, calls `[vtable + 0xE4]` |
| 0x0040E48E | `call [ecx + 0xE4]` in SetRenderStateCached -- actual D3D9 SetRenderState call site |
| 0x0040E9AB | `call [ecx + 0xE4]` in SetTextureStageState_Cached -- actual D3D9 SetRenderState call site |

### Detailed Disassembly — Crash Area (0x40EE34-0x40EE8C)

This is the switch case for blend mode value 0x12000:

```asm
; --- Case 0x12000: blend mode setup ---
0x0040EE34: cmp      eax, 0x12000          ; check if blend mode matches
0x0040EE39: jne      0x40ee90              ; skip to post-switch if not

; Set D3DRS_ALPHABLENDENABLE = 1
0x0040EE3B: push     1                     ; value = TRUE
0x0040EE3D: push     0x1b                  ; state = D3DRS_ALPHABLENDENABLE (27)
0x0040EE3F: mov      ecx, esi              ; this = renderer
0x0040EE41: call     0x40e470              ; Renderer_SetRenderStateCached

; Set D3DRS_SEPARATEALPHABLENDENABLE = 1
0x0040EE46: push     1                     ; value = TRUE
0x0040EE48: push     0xab                  ; state = D3DRS_SEPARATEALPHABLENDENABLE (171)
0x0040EE4D: mov      ecx, esi              ; this = renderer
0x0040EE4F: call     0x40e470              ; Renderer_SetRenderStateCached

; Set D3DRS_SRCBLEND = 2 (D3DBLEND_ONE)
0x0040EE54: push     2                     ; value = D3DBLEND_ONE
0x0040EE56: push     0x13                  ; state = D3DRS_SRCBLEND (19)
0x0040EE58: mov      ecx, esi              ; this = renderer
0x0040EE5A: call     0x40e470              ; Renderer_SetRenderStateCached

; Set D3DRS_DESTBLEND = 1 (D3DBLEND_ZERO)
0x0040EE5F: push     1                     ; value = D3DBLEND_ZERO
0x0040EE61: push     0x14                  ; state = D3DRS_DESTBLEND (20)
0x0040EE63: mov      ecx, esi              ; this = renderer
0x0040EE65: call     0x40e470              ; Renderer_SetRenderStateCached
                                           ;  <-- returns here at 0x0040EE6A

; *** REPORTED CRASH ADDRESS 0x0040EE6C IS INSIDE THIS INSTRUCTION ***
0x0040EE6A: mov      ebx, 8                ; texture stage state mask = 8
                                           ; (BB 08 00 00 00 — 0x40EE6C is at byte +2)

; Call SetTextureStageState_Cached with mask 8
0x0040EE6F: call     0x40e980              ; SetTextureStageState_Cached

; Set flag 0x1000 in both ebp and [esp+0x14]
0x0040EE74: mov      eax, [esp + 0x14]     ; load current desired-states
0x0040EE78: or       ebp, 0x1000           ; set bit in dirty mask
0x0040EE7E: or       eax, 0x1000           ; set bit in desired states
0x0040EE83: mov      [esp + 0x14], eax     ; store back
0x0040EE87: mov      [0xffa720], eax       ; store to global

; --- Fall through to post-switch ---
0x0040EE8C: mov      ebx, [esp + 0x14]     ; reload desired states into ebx
```

### SetTextureStageState_Cached (0x40E980) — Crash Path

```asm
0x0040E980: mov      eax, [0x1392e18]      ; eax = g_pEngineRoot
0x0040E985: push     esi
0x0040E986: mov      esi, [0xf127e0]       ; esi = cached texture stage state mask
0x0040E98C: push     edi
0x0040E98D: mov      edi, [eax + 0x214]    ; edi = engineRoot->renderStateManager (+0x214)
0x0040E993: mov      eax, [edi + 0x38c]    ; eax = cached state value
0x0040E999: and      esi, ebx              ; esi = desired & mask (ebx from caller)
0x0040E99B: cmp      eax, esi              ; compare cache with desired
0x0040E99D: je       0x40e9b7              ; skip if no change

; Cache miss — need to call D3D device
0x0040E99F: mov      eax, [edi + 0xc]      ; eax = device pointer  <-- IF CORRUPT, EAX=0xF
0x0040E9A2: mov      ecx, [eax]            ; ecx = device vtable   <-- CRASH: READ FROM 0xF
0x0040E9A4: push     esi                   ; value
0x0040E9A5: push     0xa8                  ; state = D3DRS_COLORWRITEENABLE (168)
0x0040E9AA: push     eax                   ; device
0x0040E9AB: call     [ecx + 0xe4]          ; SetRenderState(device, 168, value)
0x0040E9B1: mov      [edi + 0x38c], esi    ; update cache

0x0040E9B7: pop      edi
0x0040E9B8: mov      [0xf127e4], ebx       ; store mask globally
0x0040E9BE: pop      esi
0x0040E9BF: ret
```

### Root Cause Analysis

**The crash is almost certainly at 0x40E9A2** (`mov ecx, [eax]`), not at 0x40EE6C. The crash reporter likely shows the return address (0x40EE74, which the reporter may round to 0x40EE6C) rather than the actual faulting EIP inside the callee.

The call chain leading to the crash:
1. `Renderer_ApplyRenderStateChanges` (0x40EAB0) enters case 0x12000
2. Four `Renderer_SetRenderStateCached` calls succeed (0x40EE3B-0x40EE65)
3. `SetTextureStageState_Cached` is called at 0x40EE6F with ebx=8
4. Inside it, the engine reads `g_pEngineRoot` -> `[+0x214]` -> `[+0xC]` to get the device pointer
5. If `[edi + 0xC]` contains 0xF instead of a valid device pointer, `mov ecx, [eax]` crashes trying to read vtable from address 0xF

**Why the device pointer could be 0xF**: The object at `[g_pEngineRoot + 0x214]` has the device at offset +0xC. This is a DIFFERENT device pointer path than `Renderer_SetRenderStateCached` (which uses `[esi + 0xC]` directly from the renderer object). If the proxy DLL intercepts `Direct3DCreate9` or `CreateDevice` and the game stores the device pointer in multiple places, the object at `[g_pEngineRoot + 0x214] + 0xC` might not have been updated to point to the proxy's device. Value 0xF looks like an uninitialized or partially-written field (0xF = 15, could be a render state value that leaked into the wrong field).

**Alternative theory**: The `Renderer_SetRenderStateCached` calls at 0x40EE41-0x40EE65 use the renderer object's device pointer at `[esi + 0xC]`. These succeed. But `SetTextureStageState_Cached` at 0x40E980 uses a DIFFERENT path: `[g_pEngineRoot + 0x214 + 0xC]`. If these point to different objects and the second one is corrupt, only the 0x40E980 path crashes.

**Connection to proxy DLL**: The proxy DLL intercepts D3D9 at the DLL level. The game is D3D8 using d3d8to9 wrapper. The chain is: game -> d3d8to9 -> proxy d3d9.dll -> real d3d9. If the proxy or d3d8to9 is not fully initialized when these calls first happen during startup, the device pointers stored in various engine objects may be inconsistent.

### Suggested Live Verification

- `livetools bp add 0x40E99F` then `regs` — check what `[edi + 0xC]` actually contains when the crash path is about to execute
- `livetools trace 0x40E980 --count 5 --read [0x1392e18]+0x214+0xC:ptr` — read the device pointer used by this function
- `livetools mem read <address_of_esi+0xC> 4 --as ptr` — compare device pointer in renderer vs the one in g_pEngineRoot path
- If the crash only happens at startup before BeginScene: consider making the proxy DLL hook/patch the code at 0x40E980 to redirect `[edi+0xC]` to the correct device, or delay all memory patches until the device is fully initialized
- Check if there's a timing issue: the blend mode switch (case 0x12000) might be hit during initialization before the device is ready

---

## Automation Pipeline — Iteration 1 Baseline — 2026-03-24

### Setup

Built autonomous test pipeline: `run.py record` captures user inputs (keyboard + mouse + mouse movement) via Win32 low-level hooks, `run.py test --build` replays them after building and deploying the proxy. Added `gamectl.py` input recorder with `MOVETO:X,Y`, `CLICK:X,Y`, `HOLD:KEY:N` token support. Setup dialog auto-dismissed via Win32 `BM_CLICK`. NVIDIA `]` key screenshots captured and collected (last 2 minutes).

### Test Results

- **Build**: OK (MSVC x86, no CRT, 21KB DLL)
- **Launch**: No setup dialog, no crash, 20s settle time
- **Macro replay**: 143 actions sent (45 keys, 4 screenshots, D/A movement holds)
- **Crash**: None — game ran clean through entire test
- **Proxy log**: 23 scene summaries
  - `vpValid=1` across all frames
  - `passthrough=0` across all frames (100% draws processed)
  - `total` ranging from 537 to ~94K draws per batch
- **Screenshots**: 4 NVIDIA captures collected (before/after Remix debug, strafe left/right)
- **Hash stability**: Asset hash rule = `indices,texcoords,geometrydescriptor` (excludes positions)

### Configuration

- Resolution: 1024x768 (registry-controlled, needs 4K fix)
- RTX Remix: `rtx.remixMenuKeyBinds = X`, `rtx.useVertexCapture = True`
- All TRL graphics effects disabled (shadows, reflections, water, DoF, FSAA, next-gen)
- Frustum culling patched (threshold set to 1e30, cull function returns immediately)
