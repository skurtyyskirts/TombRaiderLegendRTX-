// LA Noire Knowledge Base - DX9 Rendering Pipeline
// Binary: LANoire.exe

// ============================================================
// GLOBAL POINTERS
// ============================================================
// 0x15264E0 = Renderer* g_pRenderer;          // 1065 xrefs, main singleton
// 0x13F1331 = char g_bUseDX11;                // NZ = DX11 path
// 0x13F12C2 = char g_bMultithreaded;          // NZ = threaded rendering
// 0x141D210 = float g_GlobalMatrix[5][4];     // Copied into renderer at init
// 0x141D288 = CRITICAL_SECTION g_RenderCmdCS; // Command queue lock
// 0x141D2A0 = LARGE_INTEGER g_PerfCounter;
// 0x141D2B8 = int g_FrameSyncFlag;

// ============================================================
// PACKED SHADER CONSTANT HANDLE
// ============================================================
// Bits 24-31: count (number of float4 registers)
// Bits  8-15: VS start register (0xFF = unused)
// Bits  0-7:  PS start register (0xFF = unused)
typedef unsigned int ShaderConstHandle;

// ============================================================
// RENDERER CLASS (vtable at 0x124CC54)
// ============================================================
// Size: very large (0x1A10+ bytes)
struct Renderer {
    void* vtable;                               // +0x000  = 0x124CC54
    // ... many fields ...
    // char padding[0x1EC - 4];
    // Shader constant handles (DWORDs at these offsets):
    //   [0xa2*4] = EyePosition handle
    //   [0xa3*4] = EyeDirection handle
    //   [0xa4*4] = ViewInverse handle
    //   [0xa5*4] = GlobalTintColour handle
    //   [0xa6*4] = AlphaFade handle
    //   [0xa8*4] = ModelViewProjectionMatrix handle
    //   [0xaa*4] = NormalMatrix handle
    //   [0xac*4] = DOF_Focus handle
    //   [0xad*4] = DOF_Near handle
    //   [0xae*4] = DOF_Far handle
    //   [0xaf*4] = DOF_Clamp handle
    //   [0xb0*4] = NearPlane handle
    //   [0xb1*4] = FarPlane handle
    //   [0xb2*4] = WindowSize handle
    //   [0xb8*4] = RenderTargetSize handle
    //   [0xc4*4] = PositionScale handle
    //   [0xc5*4] = LowLodTextureBlend handle
    //   [0xc6*4] = CameraParams handle
    //   [0xc7*4] = DepthSlopeBias handle
    //   [0xc8*4] = VertexLightsSettings handle
    //   [0xc9*4] = Apparent3DDepthOf2DElements handle
    //   [0xca*4] = FacingRegisterFlip handle
    //   [0xcb*4] = ClothEnabled handle
    //   [0x5b8*4] = AbstractDevice* pDevice
    //   [0x5c0*4 .. 0x5cf*4] = float projMatrix[4][4]
    //   [0x690*4] = float windowWidth
    //   [0x691*4] = float windowHeight
};

// ============================================================
// AbstractDevice VTABLE (base class for DX9/DX11)
// ============================================================
// AbstractDevice9  RTTI: 0x013DF93C  ".?AVAbstractDevice9@@"
// AbstractDevice11 RTTI: 0x013DF7B4  ".?AVAbstractDevice11@@"
//
// Vtable layout (offsets from vtable base):
//   [0x04] SetRenderState(int state, int value)
//   [0x0C] SetSamplerState(int stage, int type, int value)
//   [0x14] SetVertexShaderConstantF(int startRegister, float* data, int vec4Count)
//   [0x18] SetPixelShaderConstantF(int startRegister, float* data, int vec4Count)
//   [0x28] SetTexture(int stage, void* texture)
//   [0x38] ???
//   [0x48] ???
//   [0x4C] CreateVertexBuffer(...)
//   [0x50] CreateIndexBuffer(...)
//   [0x60] CreateTexture(...)
//   [0x64] CreateRenderTarget(...)
//   [0x6C] SetVertexDeclaration(...)
//   [0x70] SetRenderTarget(...)
//   [0x7C] Present/Swap?
//   [0xBC] DrawPrimitive?

// ============================================================
// RENDER CONTEXT (TLS-stored, used by constant setter)
// ============================================================
// Access chain: FS:[0x2C] -> [0] -> [+8] -> *ptr = RenderContext
// RenderContext offsets (in DWORDs):
//   [0x40..0x4F] = float viewData[5][4]  (5 rows of 4 floats: rotation + translation)
//   [0x50..0x53] = float lastRow[4] of view
//   [0x54]       = some view flag
//   [0x68..0x6F] = ???
//   [0x78..0x87] = float viewProjMatrix[4][4]
//   [0x88..0x8B] = float4 eyeForward
//   [0x8C..0x9B] = float viewMatrix[4][4]
//   [0x9C..0xAB] = float invViewMatrix[4][4]
//   [0xC1]       = byte reflectionFlag
//   [0xC2]       = byte reflectionEyeOffset[3]  (x,y,z offsets)
//   [0xCC..0xDB] = float reflectionViewMatrix[4][4]
//   [0xE1]       = byte isReflectionCamera
//   [0xE4]       = void* additionalTransformMatrix

// ============================================================
// FUNCTION SIGNATURES
// ============================================================

// 0x00D58E88 - Renderer::Init()
// void __fastcall Renderer_Init(Renderer* this, void* constTable, ...);
// Resolves all shader constant names, loads programs, creates resources.

// 0x00D5E910 - SetCameraMatrices
// void __cdecl SetCameraMatrices(float* viewMatrix, int arg2, int writeSnapshot);
// Writes view/projection data into TLS render context.

// 0x00E14CF2 - SetPerDrawConstants
// void __fastcall SetPerDrawConstants(ShaderConstHandle mvpHandle, void* constTableLookup);
// Sets per-object matrices and lighting for one draw call.

// 0x00D60AB0 - SetShaderConstant (295 call sites)
// void __thiscall SetShaderConstant(ShaderConstHandle handle, void* data);
// ECX = packed handle, dispatches to VS/PS register writes.

// 0x00D5F990 - Renderer::BeginScene()
// void __fastcall Renderer_BeginScene(Renderer* this);

// 0x00D5FB70 - Renderer::EndScene()  -> delegates to 0x00D68020
// void __fastcall Renderer_EndScene(Renderer* this);

// 0x00D5F7B5 - SetDefaultRenderStates (inside larger function)
// void SetDefaultRenderStates();

// 0x00D66A10 - SetBlendMode
// void SetBlendMode(char mode);  // mode 0-4

// 0x00D582B0 - Renderer destructor
// void __fastcall Renderer_Dtor(Renderer* this);

// 0x00D4D080 - ProcessCommandQueue
// void ProcessCommandQueue(Renderer* this);

// 0x00D60AB0 disassembly reference:
//   mov  eax, fs:[0x2c]
//   mov  edx, [eax]
//   mov  eax, [edx+8]         ; TLS render context ptr-to-ptr
//   mov  edi, [eax]           ; deref -> AbstractDevice*
//   mov  edx, [edi]           ; vtable
//   ; extract VS register = (handle >> 8) & 0xFF
//   ; extract PS register = handle & 0xFF
//   ; extract count = handle >> 24
//   ; if VS_reg != 0xFF: call [vtable+0x14](VS_reg, dataPtr, count)
//   ; if PS_reg != 0xFF: call [vtable+0x18](PS_reg, dataPtr, count)

// ============================================================
// D3D RENDER STATE CONSTANTS (used in SetDefaultRenderStates)
// ============================================================
// 0x07 = D3DRS_ZENABLE
// 0x0E = D3DRS_ZWRITEENABLE
// 0x0F = D3DRS_ALPHATESTENABLE
// 0x13 = D3DRS_SRCBLEND
// 0x14 = D3DRS_DESTBLEND
// 0x17 = D3DRS_FILLMODE
// 0x1B = D3DRS_ALPHABLENDENABLE
// 0x34 = D3DRS_STENCILENABLE
// 0x98 = D3DRS_COLORWRITEENABLE
// 0xAB = D3DRS_SEPARATEALPHABLENDENABLE

// ============================================================
// STRING TABLE (key rendering strings)
// ============================================================
// 0x1238E74 = "ModelViewProjectionMatrix"
// 0x01249C14 = "out/graphicsdata/programs.vfp.dx9"
// 0x01249C38 = "out/graphicsdata/programs.vfp.dx11"
// 0x01249E14 = "SetDefaultRenderStates"
// 0x0124F3EC = "cbuffer RenderStuff%d : register (b%d)"
