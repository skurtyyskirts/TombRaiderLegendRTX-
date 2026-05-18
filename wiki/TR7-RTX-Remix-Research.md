# Tomb Raider: Legend — RTX Remix Compatibility Research

## Document Information

| Field | Value |
|-------|-------|
| Game | Tomb Raider: Legend (2006) |
| Developer | Crystal Dynamics |
| Engine | cdcEngine (Crystal Dynamics Engine) |
| Target Binary | trl.exe (Steam, Steamless-unpacked) |
| Architecture | 32-bit x86 PE |
| Graphics API | Direct3D 9.0c (Shader Model 2.0/3.0) |
| Analysis Date | March 2026 |
| Status | **Incompatible** — Requires compatibility mod |

---

## Executive Summary

Tomb Raider: Legend uses a **fully programmable shader pipeline** (SM 2.0/3.0) rather than the fixed-function pipeline (FFP) that RTX Remix requires for automatic scene reconstruction. This analysis reverse-engineered the cdcEngine rendering architecture to identify the exact hooks needed for a compatibility mod.

### Key Findings

1. **D3D9 is loaded dynamically** via LoadLibrary/GetProcAddress — not linked through IAT
2. **Transforms are passed via shader constants** (c0-c7) instead of FFP SetTransform calls
3. **The device pointer lives inside a wrapper structure** at a fixed global address
4. **Three SetVertexShaderConstantF call sites** handle all world/view/projection matrix updates
5. **A compatibility mod is feasible** following the xoxor4d methodology

---

## 1. D3D9 Initialization Architecture

### 1.1 Dynamic Loading Pattern

TR7 does not statically link to d3d9.dll. Instead, it uses runtime loading:

```
Location: FUN_00ec7410 (D3D9_Init_Function)
Strings:  "d3d9.dll"         @ 0x00F0900C
          "Direct3DCreate9"  @ 0x00F08FFC
```

**Initialization Flow:**

```c
// Pseudocode reconstruction of FUN_00ec7410
int D3D9_Init(void) {
    if (g_cdcWrapper != NULL) {
        g_cdcWrapper->refCount++;
        return g_cdcWrapper;
    }
    
    HMODULE hD3D9 = LoadLibraryA("d3d9.dll");
    if (!hD3D9) return 0;
    
    FARPROC pCreate = GetProcAddress(hD3D9, "Direct3DCreate9");
    if (!pCreate) {
        FreeLibrary(hD3D9);
        return 0;
    }
    
    IDirect3D9* pD3D9 = pCreate(D3D_SDK_VERSION);  // 0x20 = 32
    if (!pD3D9) {
        FreeLibrary(hD3D9);
        return 0;
    }
    
    // Get adapter count
    int adapterCount = pD3D9->GetAdapterCount();  // vtable[4] = offset 0x10
    if (adapterCount == 0) return 0;
    
    // Allocate wrapper structure (0x228 bytes)
    void* wrapper = cdcAlloc(0x228);
    if (!wrapper) return 0;
    
    // Initialize wrapper via constructor
    wrapper = cdcD3D9Wrapper_Init(hD3D9, pD3D9);  // FUN_00ec7310
    
    g_cdcWrapper = wrapper;  // Store at DAT_01392e18
    
    if (wrapper->device == NULL) {  // offset 0x218
        cdcD3D9Wrapper_Cleanup();
        return 0;
    }
    
    return g_cdcWrapper;
}
```

### 1.2 Wrapper Structure Constructor

```
Location: FUN_00ec7310 (cdcD3D9Wrapper_Init)
```

**Structure Layout (partial):**

```c
struct cdcD3D9Wrapper {
    /* 0x000 */ void*              vtable;          // &PTR_LAB_00f08ff8
    /* 0x004 */ int                field_04;        // 0
    /* 0x008 */ int                field_08;        // 0
    /* 0x00C */ int                field_0C;        // 0
    /* 0x010 */ int                field_10;        // -1
    /* 0x014 */ int                field_14;        // 0
    /* 0x018 */ HMODULE            hD3D9Module;     // param_2 (d3d9.dll handle)
    /* 0x01C */ IDirect3D9*        pD3D9;           // param_3 (from Direct3DCreate9)
    /* 0x020 */ int                field_20;        // 0
    // ...
    /* 0x154 */ char               flag_154;        // 0
    /* 0x198 */ char               flag_198;        // 0
    /* 0x199 */ char               flag_199;        // 0 (checked in render path)
    // ...
    /* 0x1E0-0x20F */ int          fields[12];      // Various state
    /* 0x210 */ void*              renderStateBlock;
    /* 0x214 */ cdcRenderContext*  pRenderContext;  // Render context pointer
    /* 0x218 */ IDirect3DDevice9*  pD3DDevice;      // THE DEVICE POINTER
    /* 0x21C */ int                adapterArraySize;
    /* 0x220 */ void*              adapterArray;
    // Total size: 0x228 bytes
};
```

### 1.3 Global Pointer Location

```
Global Variable:    DAT_01392e18
Address:            0x01392E18
Type:               cdcD3D9Wrapper*
```

**Access Patterns:**

```c
// Get the D3D9 device directly:
IDirect3DDevice9* device = *(IDirect3DDevice9**)(0x01392E18 + 0x218);

// Or via render context:
cdcRenderContext* ctx = *(cdcRenderContext**)(0x01392E18 + 0x214);
IDirect3DDevice9* device = *(IDirect3DDevice9**)(ctx + 0x0C);
```

---

## 2. Render Context Structure

The render context is a large structure that holds current render state, including the device pointer and matrix slots.

```
Access Path:  *(0x01392E18 + 0x214) → cdcRenderContext*
```

**Structure Layout (partial):**

```c
struct cdcRenderContext {
    /* 0x000 */ void*              vtable;
    /* 0x004 */ int                field_04;
    /* 0x008 */ int                field_08;
    /* 0x00C */ IDirect3DDevice9*  pDevice;         // Device pointer used in calls
    /* 0x010 */ void*              field_10;
    /* 0x014 */ void*              renderTarget;
    /* 0x018 */ void*              depthStencil;
    // ... many fields ...
    
    /* 0x480 */ float              matrixA[16];     // Matrix source A
    /* 0x4C0 */ float              matrixB[16];     // Matrix source B
    /* 0x500 */ float              worldMatrix[16]; // World transform
    /* 0x540 */ float              viewProjMatrix[16]; // ViewProj combined
    
    /* 0x580 */ char               needsC8Update;   // Flag: update c8-c15
    /* 0x581 */ char               needsC0Update;   // Flag: update c0-c7
    // ...
};
```

---

## 3. Transform Submission System

### 3.1 SetVertexShaderConstantF Call Sites

TR7 uses shader constants instead of FFP SetTransform. We identified **3 call sites**:

| Address | Function | StartReg | Count | Purpose |
|---------|----------|----------|-------|---------|
| 0x00ECBA57 | FUN_00ecba40 | Variable | Variable | General constant upload |
| 0x00ECBB89 | FUN_00ecbb00 | 8 | 8 | Secondary matrices (c8-c15) |
| 0x00ECBC01 | FUN_00ecbb00 | **0** | **8** | **World + ViewProj (c0-c7)** |

### 3.2 cdcRender_SetWorldMatrix Analysis

```
Function:   FUN_00ecbb00
Address:    0x00ECBB00
Purpose:    Upload world and view-projection matrices to vertex shader
```

**Decompiled with Annotations:**

```c
void __fastcall cdcRender_SetWorldMatrix(cdcRenderContext* ctx) {
    float localMatrixA[16];   // local_110
    float localMatrixB[16];   // local_d0
    float constantData[32];   // local_90 (holds c0-c15)
    
    // Check if secondary matrices need update
    if (ctx->needsC8Update != 0) {
        // Multiply matrices: localMatrixB = matrixA × matrixB
        MatrixMultiply(localMatrixB, ctx->matrixA, ctx->matrixB);
        
        // Copy to localMatrixA
        memcpy(localMatrixA, localMatrixB, 64);
        
        // Some transform processing
        FUN_00402990(localMatrixA);
        
        // Prepare constant data
        FUN_00ecbaa0();
        FUN_00ecbaa0();
        
        // Upload to c8-c15 (8 vectors = 2 matrices)
        IDirect3DDevice9* device = ctx->pDevice;
        device->SetVertexShaderConstantF(8, constantData, 8);
        
        if (ctx->needsC8Update == 0) {
            goto check_c0_update;
        }
    }
    
    // Check if primary matrices (world/VP) need update
    if (ctx->needsC0Update == 0) {
        return;
    }
    
check_c0_update:
    // Multiply: localMatrixA = worldMatrix × viewProjMatrix
    MatrixMultiply(localMatrixA, ctx->worldMatrix, ctx->viewProjMatrix);
    
    // Copy to localMatrixB
    memcpy(localMatrixB, localMatrixA, 64);
    
    // Prepare constant data
    FUN_00ecbaa0();
    FUN_00ecbaa0();
    
    // Upload to c0-c7 (8 vectors = world + viewproj)
    // c0-c3 = World matrix rows
    // c4-c7 = ViewProj matrix rows
    IDirect3DDevice9* device = ctx->pDevice;
    device->SetVertexShaderConstantF(0, constantData, 8);
    
    // Clear update flags
    ctx->needsC8Update = 0;
    ctx->needsC0Update = 0;
}
```

### 3.3 Vertex Shader Constant Register Layout

Based on analysis of FUN_00ecbb00:

| Register | Content | Matrix Row |
|----------|---------|------------|
| c0 | World matrix | Row 0 (M11, M12, M13, M14) |
| c1 | World matrix | Row 1 (M21, M22, M23, M24) |
| c2 | World matrix | Row 2 (M31, M32, M33, M34) |
| c3 | World matrix | Row 3 (M41, M42, M43, M44) — Translation |
| c4 | ViewProj matrix | Row 0 |
| c5 | ViewProj matrix | Row 1 |
| c6 | ViewProj matrix | Row 2 |
| c7 | ViewProj matrix | Row 3 |
| c8-c15 | Secondary matrices | Bone/skinning or shadow matrices |

---

## 4. Render Call Hierarchy

### 4.1 Call Chain to SetVertexShaderConstantF

```
Game Main Loop
    │
    ▼
cdcRender_Frame (not yet identified — likely around 0x0060xxxx)
    │
    ▼
FUN_0060fd80 ─────────────────────────────────────────┐
    │  Accesses: DAT_01392e18 + 0x214 (render context) │
    │  Accesses: DAT_01392e18 + 0x199 (state flag)     │
    ▼                                                  │
FUN_0060ebf0                                           │
    │  Sets: ctx->needsC0Update = 1                    │
    │  Calls: cdcRender_SetWorldMatrix                 │
    ▼                                                  │
FUN_00ecbb00 (cdcRender_SetWorldMatrix) ◄──────────────┘
    │
    ▼
SetVertexShaderConstantF(device, 0, data, 8)
    │
    ▼
Vertex Shader reads c0-c7 for transforms
```

### 4.2 Draw Call Sites

DrawIndexedPrimitive (vtable offset 328/0x148) calls found in **4 functions**:

| Address | Function | Likely Purpose |
|---------|----------|----------------|
| 0x0040AE20 | FUN_0040ae20 | General draw wrapper |
| 0x00415AB0 | FUN_00415ab0 | Unknown |
| 0x0060EB50 | FUN_0060eb50 | Scene rendering |
| 0x00613000 | FUN_00613000 | Unknown |

---

## 5. Additional Discovered Addresses

### 5.1 Confirmed Addresses

| Address | Symbol | Purpose |
|---------|--------|---------|
| 0x01392E18 | g_cdcWrapper | Global cdcD3D9Wrapper* pointer |
| 0x00EC7410 | D3D9_Init | LoadLibrary + Direct3DCreate9 + wrapper init |
| 0x00EC7310 | cdcWrapper_Construct | Wrapper structure constructor |
| 0x00ECBB00 | cdcRender_SetWorldMatrix | Uploads c0-c7 (world/VP matrices) |
| 0x00ECBA40 | cdcRender_SetConstants | General shader constant upload |
| 0x0060FD80 | cdcRender_PrepareFrame | Prepares render state, calls matrix update |
| 0x0060EBF0 | cdcRender_SetupTransforms | Transform setup, triggers SVSF |

### 5.2 Structure Offsets

**From g_cdcWrapper (0x01392E18):**

| Offset | Type | Content |
|--------|------|---------|
| +0x018 | HMODULE | d3d9.dll module handle |
| +0x01C | IDirect3D9* | D3D9 interface pointer |
| +0x199 | char | Render state flag |
| +0x214 | cdcRenderContext* | Current render context |
| +0x218 | IDirect3DDevice9* | **D3D9 Device pointer** |
| +0x21C | int | Adapter array size |
| +0x220 | void* | Adapter info array |

**From cdcRenderContext (at wrapper+0x214):**

| Offset | Type | Content |
|--------|------|---------|
| +0x00C | IDirect3DDevice9* | Device pointer (same as wrapper+0x218) |
| +0x480 | float[16] | Matrix A (multiply source) |
| +0x4C0 | float[16] | Matrix B (multiply source) |
| +0x500 | float[16] | World matrix |
| +0x540 | float[16] | ViewProj matrix |
| +0x580 | char | Flag: needs c8-c15 update |
| +0x581 | char | Flag: needs c0-c7 update |

### 5.3 Other Potential Globals

| Address | Purpose | Notes |
|---------|---------|-------|
| 0x01159DB4 | D3D9 init flag | Checked in FUN_00ec6e80 |
| 0x01159DBC | D3D object pointer | Used with vtable+0x38 call |
| 0x01310A68 | Matrix/bone data | Written in FUN_0060ebf0 |

---

## 6. RTX Remix Compatibility Analysis

### 6.1 Why TR7 Is Incompatible

RTX Remix reconstructs 3D scenes by intercepting **fixed-function pipeline** (FFP) calls:

| FFP Call | What Remix Extracts |
|----------|---------------------|
| SetTransform(D3DTS_WORLD) | Object position/orientation |
| SetTransform(D3DTS_VIEW) | Camera position |
| SetTransform(D3DTS_PROJECTION) | Camera FOV, frustum |
| SetLight() | Light sources |
| SetMaterial() | Surface properties |
| SetTexture() | Texture binding (for hashing) |
| DrawPrimitive() | Geometry data |

**TR7 uses NONE of these for transforms.** Instead:

| TR7 Does This | Remix Sees |
|---------------|------------|
| SetVertexShader(customVS) | "Shader active, can't intercept" |
| SetVertexShaderConstantF(0, worldMat, 4) | Nothing (opaque constant data) |
| DrawIndexedPrimitive() | Geometry, but no transform info |

**Result:** Remix passes draw calls through to rasterization unchanged.

### 6.2 Compatibility Mod Requirements

To make TR7 work with RTX Remix, a mod must:

#### Layer 1: Transform Bridge (ANALYZED ✅)

```cpp
// Hook SetVertexShaderConstantF
// Extract world matrix from c0-c3
// Call SetTransform(D3DTS_WORLD, &worldMatrix) for Remix
```

#### Layer 2: Shader Bypass (NOT YET ANALYZED)

```cpp
// Option A: Disable vertex/pixel shaders
// Force FFP T&L path
// Remix can then intercept everything

// Option B: Pass-through shaders
// Replace with minimal shaders that Remix can handle
```

#### Layer 3: Material Bridge (NOT YET ANALYZED)

```cpp
// Ensure SetTexture() and SetMaterial() are called
// Remix needs these for PBR material hashing
```

#### Layer 4: Anti-Culling (NOT YET ANALYZED)

```cpp
// Either patch game's culling functions
// Or rely on Remix settings:
//   rtx.antiCulling.object.enable = True
//   rtx.antiCulling.light.enable = True
```

### 6.3 Hash Stability Concerns

Even after fixing transforms, hash stability issues may arise:

| Potential Issue | Cause | Investigation Needed |
|-----------------|-------|---------------------|
| CPU skinning | Bone transforms modify vertices before GPU | Check if TR7 uses CPU or GPU skinning |
| Dynamic batching | Multiple objects batched differently per frame | Analyze vertex buffer creation patterns |
| Frustum culling | Objects appear/disappear unpredictably | Identify culling function |
| Level streaming | DRM section loading changes active geometry | Analyze cdcDRM_LoadSection |

---

## 7. Discovered Function Signatures

### 7.1 Core Functions

```c
// D3D9 Initialization
int D3D9_Init(void);
// Address: 0x00EC7410
// Returns: cdcD3D9Wrapper* or 0 on failure
// Stores result in DAT_01392e18

// Wrapper Constructor
cdcD3D9Wrapper* cdcWrapper_Construct(
    cdcD3D9Wrapper* this,
    HMODULE hD3D9,
    IDirect3D9* pD3D9
);
// Address: 0x00EC7310
// Initializes wrapper struct, calls FUN_00ec6e80 to create device

// World Matrix Upload
void __fastcall cdcRender_SetWorldMatrix(cdcRenderContext* ctx);
// Address: 0x00ECBB00
// Uploads c0-c7 via SetVertexShaderConstantF
// Called when ctx->needsC0Update or ctx->needsC8Update is set
```

### 7.2 Utility Functions

```c
// Matrix Multiply (presumed)
void MatrixMultiply(float* out, float* a, float* b);
// Address: 0x005DD910
// Multiplies 4x4 matrices

// Memory Allocation
void* cdcAlloc(size_t size);
// Address: 0x005E2DE0
// cdcEngine memory allocator

// Matrix Transform
void FUN_00402990(float* matrix);
// Address: 0x00402990
// Some matrix transformation/preparation
```

---

## 8. Recommended Hook Implementation

### 8.1 Transform Extraction Hook

```cpp
#include <d3d9.h>
#include <cstring>

// Addresses from analysis
constexpr uintptr_t CDC_WRAPPER_ADDR = 0x01392E18;
constexpr uintptr_t DEVICE_OFFSET = 0x218;
constexpr uintptr_t SVSF_CALL_SITE_1 = 0x00ECBA57;
constexpr uintptr_t SVSF_CALL_SITE_2 = 0x00ECBB89;
constexpr uintptr_t SVSF_CALL_SITE_3 = 0x00ECBC01;

// Original function pointer
typedef HRESULT (WINAPI *SetVertexShaderConstantF_t)(
    IDirect3DDevice9* device,
    UINT StartRegister,
    const float* pConstantData,
    UINT Vector4fCount
);
SetVertexShaderConstantF_t Original_SVSF = nullptr;

// Get device from TR7's wrapper
IDirect3DDevice9* GetTR7Device() {
    void** pWrapper = (void**)CDC_WRAPPER_ADDR;
    if (!pWrapper || !*pWrapper) return nullptr;
    
    char* wrapper = (char*)(*pWrapper);
    return *(IDirect3DDevice9**)(wrapper + DEVICE_OFFSET);
}

// Hook function
HRESULT WINAPI Hook_SetVertexShaderConstantF(
    IDirect3DDevice9* device,
    UINT StartRegister,
    const float* pConstantData,
    UINT Vector4fCount
) {
    // Intercept world matrix at c0-c3
    if (StartRegister == 0 && Vector4fCount >= 4) {
        D3DMATRIX worldMatrix;
        std::memcpy(&worldMatrix, pConstantData, sizeof(D3DMATRIX));
        
        // Bridge to FFP for RTX Remix
        device->SetTransform(D3DTS_WORLD, &worldMatrix);
    }
    
    // Intercept view-projection at c4-c7
    if (StartRegister <= 4 && (StartRegister + Vector4fCount) >= 8) {
        // c4-c7 contains combined ViewProj
        // For Remix, we may need to decompose this into separate View and Proj
        // For now, setting as view (Remix can often work with just world)
        
        const float* vpData = pConstantData + (4 - StartRegister) * 4;
        D3DMATRIX viewProjMatrix;
        std::memcpy(&viewProjMatrix, vpData, sizeof(D3DMATRIX));
        
        // This is combined VP - Remix may need them separate
        // Advanced: Decompose into View and Projection matrices
        device->SetTransform(D3DTS_VIEW, &viewProjMatrix);
    }
    
    // Call original
    return Original_SVSF(device, StartRegister, pConstantData, Vector4fCount);
}
```

### 8.2 Shader Bypass Hook (Conceptual)

```cpp
// Original function pointers
typedef HRESULT (WINAPI *SetVertexShader_t)(
    IDirect3DDevice9* device,
    IDirect3DVertexShader9* pShader
);
typedef HRESULT (WINAPI *SetPixelShader_t)(
    IDirect3DDevice9* device,
    IDirect3DPixelShader9* pShader
);

SetVertexShader_t Original_SetVS = nullptr;
SetPixelShader_t Original_SetPS = nullptr;

// Option A: Disable shaders entirely
HRESULT WINAPI Hook_SetVertexShader(
    IDirect3DDevice9* device,
    IDirect3DVertexShader9* pShader
) {
    // Don't set any vertex shader - force FFP
    return D3D_OK;
}

HRESULT WINAPI Hook_SetPixelShader(
    IDirect3DDevice9* device,
    IDirect3DPixelShader9* pShader
) {
    // Don't set any pixel shader - force FFP
    return D3D_OK;
}

// Option B: Selective bypass (more compatible)
HRESULT WINAPI Hook_SetVertexShader_Selective(
    IDirect3DDevice9* device,
    IDirect3DVertexShader9* pShader
) {
    // Check if this is a "complex" shader we need to bypass
    // vs. a simple one Remix might handle
    
    // For now, bypass all
    return D3D_OK;
}
```

---

## 9. Testing Recommendations

### 9.1 Phase 1: Transform Hook Only

1. Implement SetVertexShaderConstantF hook
2. Run game with RTX Remix installed
3. Check developer menu (Alt+X) for:
   - Does geometry appear in capture?
   - Do transforms look correct?
   - Are objects positioned correctly?

### 9.2 Phase 2: Enable Remix Workarounds

Test these rtx.conf settings:

```ini
# May help with shader-based games
rtx.useWorldMatricesForShaders = True

# Anti-culling
rtx.antiCulling.object.enable = True
rtx.antiCulling.object.enableHighPrecisionAntiCulling = True

# Hash stability
rtx.ignoreVertexColor = True
rtx.geometryAssetHashRuleString = positions,indices,geometrydescriptor
```

### 9.3 Phase 3: Full FFP Bypass

If Phase 1-2 don't work:

1. Add shader bypass hooks
2. May need to manually handle lighting (SetLight calls)
3. May need to manually handle textures (ensure SetTexture FFP path)

---

## 10. Files Produced

| Filename | Type | Purpose |
|----------|------|---------|
| TR7_Analyze.py | Ghidra Script | Initial analysis, finds SVSF call sites |
| TR7_CreateGDT.py | Ghidra Script | Creates data type archive |
| TR7_AnalyzeAdvanced.py | Ghidra Script | Auto-discovers key addresses |
| TR7_FindDevicePointer.py | Ghidra Script | Traces D3D9 init to find device |
| TR7_ShaderAnalyze.py | Python 3 | Analyzes DXBC shader constants |

---

## 11. References

### 11.1 RTX Remix Documentation

- GitHub: <https://github.com/NVIDIAGameWorks/rtx-remix>
- TR7 Issue: <https://github.com/NVIDIAGameWorks/rtx-remix/issues/287> (closed as incompatible)

### 11.2 Compatibility Mod Examples

- xoxor4d BioShock: <https://github.com/xoxor4d/bioshock-rtx>
- SWAT 4 Remix: Community mod following same pattern
- FEAR Remix: Community mod

### 11.3 D3D9 Technical Reference

- IDirect3DDevice9 vtable: 119 methods (476 bytes)
- SetVertexShaderConstantF: vtable offset 376 (0x178), slot 94
- SetTransform: vtable offset 176 (0xB0), slot 44
- DrawIndexedPrimitive: vtable offset 328 (0x148), slot 82

---

## 12. Conclusion

Tomb Raider: Legend's rendering architecture is well-understood. The game uses shader constants for transforms, which bypasses RTX Remix's FFP interception. A compatibility mod following the transform bridge approach is technically feasible:

**Minimum Viable Mod:**

1. Hook SetVertexShaderConstantF at vtable offset 0x178
2. Extract world matrix from c0-c3 when StartRegister=0
3. Call SetTransform(D3DTS_WORLD) to bridge to Remix

**Full Compatibility Mod:**

1. All of the above
2. Disable or replace vertex/pixel shaders
3. Ensure textures/materials flow through FFP path
4. Handle any culling or hash stability issues

The discovered addresses and structures provide a solid foundation for implementation.

---

*End of Research Document*
