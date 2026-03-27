/*
 * Wrapped IDirect3DDevice9 — FFP conversion layer for RTX Remix.
 *
 * Intercepts ~15 of 119 device methods; the rest relay via naked ASM thunks.
 * Sections marked GAME-SPECIFIC need per-game updates.
 * See the dx9-ffp-port prompt and extensions/skinning/README.md for full docs.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* No-CRT memcpy: the compiler emits memcpy calls for struct/array copies */
#pragma function(memcpy)
void * __cdecl memcpy(void *dst, const void *src, unsigned int n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}

/* Logging (from d3d9_main.c) */
extern void log_str(const char *s);
extern void log_hex(const char *prefix, unsigned int val);
extern void log_int(const char *prefix, int val);
extern void log_floats(const char *prefix, float *data, unsigned int count);
extern void log_float_val(const char *prefix, float f);
extern void log_floats_dec(const char *prefix, float *data, unsigned int count);

/* ============================================================
 * GAME-SPECIFIC: VS Constant Register Layout
 *
 * These define which vertex shader constant registers hold the
 * View, Projection, and World matrices. Every game engine uses
 * different register assignments. Discover yours with:
 *
 *   python scripts/find_vs_constants.py <your_game.exe>
 *   python -m livetools trace <SetVSConstF_call_addr> \
 *       --count 50 --read "[esp+4]:4:uint32; [esp+8]:4:uint32"
 *
 * Common patterns across engines:
 *   - View+Proj often adjacent (c0-c7 or c4-c11)
 *   - World matrix at a fixed register (c0, c8, c16, etc.)
 *   - Bone palette starts after world, 3 or 4 regs per bone
 *
 * ============================================================ */
/*
 * Tomb Raider Legend — Shader Passthrough + Transform Override
 *
 * TRL fuses transforms into a single WorldViewProjection (WVP) in c0-c3.
 * Remix requires separate W/V/P. We read the authoritative View and Projection
 * directly from game memory and compute World = WVP * inverse(V * P).
 *
 * Shaders stay active — TRL uses SHORT4 vertex positions that Remix cannot
 * interpret without the game's native vertex shaders. Remix captures post-shader
 * vertex positions via rtx.useVertexCapture=True.
 *
 * Register layout (from CTAB):
 *   c0-c3:   WorldViewProject (4x4 combined WVP, column-major for HLSL)
 *   c4-c6:   World (3x4 packed)
 *   c12-c15: ViewProject (4x4 combined VP)
 *   c48-c95: SkinMatrices (48 regs = 16 bones * 3 regs each)
 */
#define VS_REG_WVP_START        0   /* WorldViewProject matrix */
#define VS_REG_WVP_END          4
#define VS_REG_VP_START        12   /* ViewProject (used by skinned shaders) */
#define VS_REG_VP_END          16

/* Game memory addresses for authoritative View and Projection matrices (row-major, 16 floats each) */
#define TRL_VIEW_MATRIX_ADDR    0x010FC780
#define TRL_PROJ_MATRIX_ADDR    0x01002530

/* Frustum/culling memory patch addresses */
#define TRL_FRUSTUM_THRESHOLD_ADDR  0x00EFDD64
#define TRL_CULL_CONDITIONAL_ADDR   0x0040EEA7

/* Cull mode globals — the renderer caches these and calls SetRenderState only
 * on transitions, bypassing our proxy hook. Stamp them to D3DCULL_NONE (1). */
#define TRL_CULL_MODE_PASS1_ADDR    0x00F2A0D4  /* g_cullMode_pass1 */
#define TRL_CULL_MODE_PASS2_ADDR    0x00F2A0D8  /* g_cullMode_pass2 */
#define TRL_CULL_MODE_PASS2_INV_ADDR 0x00F2A0DC /* g_cullMode_pass2_inverse */

/* Light frustum rejection: 6-byte JNP at this address skips lights that fail
 * the 6-plane frustum test in RenderLights_FrustumCull. NOP to keep all lights. */
#define TRL_LIGHT_FRUSTUM_REJECT_ADDR 0x0060CE20


/* VP inverse cache: only recompute when camera moves more than this */
#define VP_CHANGE_THRESHOLD     1e-4f
/* World matrix quantization grid */
#define WORLD_QUANT_GRID        1e-3f
/* Screen-space quad detection: WVP vs Proj tolerance */
#define QUAD_DETECT_TOLERANCE   0.05f

/* Template compatibility aliases (unused in passthrough mode but kept for compile) */
#define VS_REG_VIEW_START      0
#define VS_REG_VIEW_END        4
#define VS_REG_PROJ_START      0
#define VS_REG_PROJ_END        4

/* Bone palette detection (only matters when ENABLE_SKINNING=1) */
#define VS_REG_BONE_THRESHOLD  48   /* SkinMatrices start at c48 */
#define VS_REGS_PER_BONE        3   /* Registers per bone (3 = 4x3 packed) */

/* GAME-SPECIFIC: Skinning — off by default. See extensions/skinning/README.md */
#define ENABLE_SKINNING 0
#define EXPAND_SKIN_VERTICES 0      /* 0=use original VB (default), 1=expand to fixed 48-byte layout */

/* ---- Diagnostic logging ---- */
#define DIAG_LOG_FRAMES 3
#define DIAG_DELAY_MS 50000   /* 50 seconds after device creation */
#define DIAG_ENABLED 1

#define DIAG_ACTIVE(self) \
    (DIAG_ENABLED && (self)->diagLoggedFrames < DIAG_LOG_FRAMES && \
     GetTickCount() - (self)->createTick >= DIAG_DELAY_MS)

/* ---- D3D9 Constants ---- */

#define D3DTS_VIEW          2
#define D3DTS_PROJECTION    3
#define D3DTS_WORLD         256
#define D3DTS_TEXTURE0      16

#define D3DRS_ZENABLE           7
#define D3DRS_FILLMODE          8
#define D3DRS_LIGHTING          137
#define D3DRS_AMBIENT           139
#define D3DRS_COLORVERTEX       141
#define D3DRS_SPECULARENABLE    29
#define D3DRS_DIFFUSEMATERIALSOURCE   145
#define D3DRS_AMBIENTMATERIALSOURCE   147
#define D3DRS_NORMALIZENORMALS  143
#define D3DRS_ALPHABLENDENABLE  27
#define D3DRS_SRCBLEND          19
#define D3DRS_DESTBLEND         20
#define D3DRS_CULLMODE          22
#define D3DRS_FOGENABLE         28

#define D3DTSS_COLOROP     1
#define D3DTSS_COLORARG1   2
#define D3DTSS_COLORARG2   3
#define D3DTSS_ALPHAOP     4
#define D3DTSS_ALPHAARG1   5
#define D3DTSS_ALPHAARG2   6
#define D3DTSS_TEXCOORDINDEX 11
#define D3DTSS_TEXTURETRANSFORMFLAGS 24

#define D3DTOP_DISABLE     1
#define D3DTOP_MODULATE    4

#define D3DTA_TEXTURE      2
#define D3DTA_DIFFUSE      0
#define D3DTA_CURRENT      1

#define D3DLIGHT_DIRECTIONAL 3

#define D3DVBF_DISABLE  0
#define D3DVBF_1WEIGHTS  1
#define D3DVBF_2WEIGHTS  2
#define D3DVBF_3WEIGHTS  3

#define D3DRS_VERTEXBLEND              151
#define D3DRS_INDEXEDVERTEXBLENDENABLE  167

#define D3DTS_WORLDMATRIX(n) (256 + (n))

#define D3DDECL_END_STREAM 0xFF
#define D3DDECLUSAGE_POSITION     0
#define D3DDECLUSAGE_BLENDWEIGHT  1
#define D3DDECLUSAGE_BLENDINDICES 2
#define D3DDECLUSAGE_NORMAL       3
#define D3DDECLUSAGE_TEXCOORD     5
#define D3DDECLUSAGE_COLOR        10
#define D3DDECLUSAGE_POSITIONT    9   /* pre-transformed screen-space coords — skips FFP transform */

#define MAX_FFP_BONES 48

#define D3DDECLTYPE_FLOAT1    0
#define D3DDECLTYPE_FLOAT2    1
#define D3DDECLTYPE_FLOAT3    2
#define D3DDECLTYPE_FLOAT4    3
#define D3DDECLTYPE_UBYTE4    5
#define D3DDECLTYPE_UBYTE4N   8
#define D3DDECLTYPE_SHORT4N   10
#define D3DDECLTYPE_UDEC3     13
#define D3DDECLTYPE_DEC3N     14
#define D3DDECLTYPE_FLOAT16_2 15

/* FVF flags — for stripping normals from FVF-based draws */
#define D3DFVF_NORMAL 0x010

#if ENABLE_SKINNING && EXPAND_SKIN_VERTICES
/* Expanded skinned vertex layout: FLOAT3 pos + FLOAT3 weights + UBYTE4 idx + FLOAT3 normal + FLOAT2 uv */
#define SKIN_VTX_SIZE   48
#define SKIN_CACHE_SIZE 64
#endif

/* ---- Device vtable slot indices ---- */
enum {
    SLOT_QueryInterface = 0,
    SLOT_AddRef = 1,
    SLOT_Release = 2,
    SLOT_TestCooperativeLevel = 3,
    SLOT_GetAvailableTextureMem = 4,
    SLOT_EvictManagedResources = 5,
    SLOT_GetDirect3D = 6,
    SLOT_GetDeviceCaps = 7,
    SLOT_GetDisplayMode = 8,
    SLOT_GetCreationParameters = 9,
    SLOT_SetCursorProperties = 10,
    SLOT_SetCursorPosition = 11,
    SLOT_ShowCursor = 12,
    SLOT_CreateAdditionalSwapChain = 13,
    SLOT_GetSwapChain = 14,
    SLOT_GetNumberOfSwapChains = 15,
    SLOT_Reset = 16,
    SLOT_Present = 17,
    SLOT_GetBackBuffer = 18,
    SLOT_GetRasterStatus = 19,
    SLOT_SetDialogBoxMode = 20,
    SLOT_SetGammaRamp = 21,
    SLOT_GetGammaRamp = 22,
    SLOT_CreateTexture = 23,
    SLOT_CreateVolumeTexture = 24,
    SLOT_CreateCubeTexture = 25,
    SLOT_CreateVertexBuffer = 26,
    SLOT_CreateIndexBuffer = 27,
    SLOT_CreateRenderTarget = 28,
    SLOT_CreateDepthStencilSurface = 29,
    SLOT_UpdateSurface = 30,
    SLOT_UpdateTexture = 31,
    SLOT_GetRenderTargetData = 32,
    SLOT_GetFrontBufferData = 33,
    SLOT_StretchRect = 34,
    SLOT_ColorFill = 35,
    SLOT_CreateOffscreenPlainSurface = 36,
    SLOT_SetRenderTarget = 37,
    SLOT_GetRenderTarget = 38,
    SLOT_SetDepthStencilSurface = 39,
    SLOT_GetDepthStencilSurface = 40,
    SLOT_BeginScene = 41,
    SLOT_EndScene = 42,
    SLOT_Clear = 43,
    SLOT_SetTransform = 44,
    SLOT_GetTransform = 45,
    SLOT_MultiplyTransform = 46,
    SLOT_SetViewport = 47,
    SLOT_GetViewport = 48,
    SLOT_SetMaterial = 49,
    SLOT_GetMaterial = 50,
    SLOT_SetLight = 51,
    SLOT_GetLight = 52,
    SLOT_LightEnable = 53,
    SLOT_GetLightEnable = 54,
    SLOT_SetClipPlane = 55,
    SLOT_GetClipPlane = 56,
    SLOT_SetRenderState = 57,
    SLOT_GetRenderState = 58,
    SLOT_CreateStateBlock = 59,
    SLOT_BeginStateBlock = 60,
    SLOT_EndStateBlock = 61,
    SLOT_SetClipStatus = 62,
    SLOT_GetClipStatus = 63,
    SLOT_GetTexture = 64,
    SLOT_SetTexture = 65,
    SLOT_GetTextureStageState = 66,
    SLOT_SetTextureStageState = 67,
    SLOT_GetSamplerState = 68,
    SLOT_SetSamplerState = 69,
    SLOT_ValidateDevice = 70,
    SLOT_SetPaletteEntries = 71,
    SLOT_GetPaletteEntries = 72,
    SLOT_SetCurrentTexturePalette = 73,
    SLOT_GetCurrentTexturePalette = 74,
    SLOT_SetScissorRect = 75,
    SLOT_GetScissorRect = 76,
    SLOT_SetSoftwareVertexProcessing = 77,
    SLOT_GetSoftwareVertexProcessing = 78,
    SLOT_SetNPatchMode = 79,
    SLOT_GetNPatchMode = 80,
    SLOT_DrawPrimitive = 81,
    SLOT_DrawIndexedPrimitive = 82,
    SLOT_DrawPrimitiveUP = 83,
    SLOT_DrawIndexedPrimitiveUP = 84,
    SLOT_ProcessVertices = 85,
    SLOT_CreateVertexDeclaration = 86,
    SLOT_SetVertexDeclaration = 87,
    SLOT_GetVertexDeclaration = 88,
    SLOT_SetFVF = 89,
    SLOT_GetFVF = 90,
    SLOT_CreateVertexShader = 91,
    SLOT_SetVertexShader = 92,
    SLOT_GetVertexShader = 93,
    SLOT_SetVertexShaderConstantF = 94,
    SLOT_GetVertexShaderConstantF = 95,
    SLOT_SetVertexShaderConstantI = 96,
    SLOT_GetVertexShaderConstantI = 97,
    SLOT_SetVertexShaderConstantB = 98,
    SLOT_GetVertexShaderConstantB = 99,
    SLOT_SetStreamSource = 100,
    SLOT_GetStreamSource = 101,
    SLOT_SetStreamSourceFreq = 102,
    SLOT_GetStreamSourceFreq = 103,
    SLOT_SetIndices = 104,
    SLOT_GetIndices = 105,
    SLOT_CreatePixelShader = 106,
    SLOT_SetPixelShader = 107,
    SLOT_GetPixelShader = 108,
    SLOT_SetPixelShaderConstantF = 109,
    SLOT_GetPixelShaderConstantF = 110,
    SLOT_SetPixelShaderConstantI = 111,
    SLOT_GetPixelShaderConstantI = 112,
    SLOT_SetPixelShaderConstantB = 113,
    SLOT_GetPixelShaderConstantB = 114,
    SLOT_DrawRectPatch = 115,
    SLOT_DrawTriPatch = 116,
    SLOT_DeletePatch = 117,
    SLOT_CreateQuery = 118,
    DEVICE_VTABLE_SIZE = 119
};

/* ---- WrappedDevice ---- */

typedef struct WrappedDevice {
    void **vtbl;
    void *pReal;            /* real IDirect3DDevice9* */
    int refCount;
    unsigned int frameCount;
    int ffpSetup;           /* whether FFP state has been configured this frame */

    float vsConst[256 * 4]; /* vertex shader constants (up to 256 vec4) */
    float psConst[32 * 4];  /* pixel shader constants (up to 32 vec4) */
    int worldDirty;         /* world matrix registers changed since last SetTransform */
    int viewProjDirty;      /* view/proj registers changed since last SetTransform */
    int psConstDirty;

    void *lastVS;           /* last vertex shader set by the game */
    void *lastPS;           /* last pixel shader set by the game */
    int viewProjValid;      /* set once both View and Proj register ranges have been written */
    int ffpActive;          /* real device currently has NULL shaders (FFP mode) */

    void *lastDecl;         /* current IDirect3DVertexDeclaration9* */
    int curDeclIsSkinned;   /* 1 if current decl has BLENDWEIGHT+BLENDINDICES */

#if ENABLE_SKINNING
    int curDeclNumWeights;   /* number of blend weights (1-3) */
    int numBones;            /* bones uploaded this object (immediate upload counter) */
    int prevNumBones;        /* bone count from previous object (for stale clearing) */
    int bonesDrawn;          /* set to 1 after a skinned draw; triggers reset on next bone write */
    int lastBoneStartReg;    /* startReg of most recent bone write (startReg-jump detection) */
    int skinningSetup;       /* whether FFP skinning state has been configured */

#if EXPAND_SKIN_VERTICES
    /* Per-element offsets/types for skinned vertex expansion */
    int curDeclNormalOff;       /* byte offset of NORMAL in source vertex */
    int curDeclNormalType;      /* D3DDECLTYPE of NORMAL, or -1 if none */
    int curDeclBlendWeightOff;  /* byte offset of BLENDWEIGHT in source vertex */
    int curDeclBlendWeightType; /* D3DDECLTYPE of BLENDWEIGHT element */
    int curDeclBlendIndicesOff; /* byte offset of BLENDINDICES in source vertex */
    int curDeclPosOff;          /* byte offset of POSITION in source vertex */

    /* Skinned vertex expansion cache */
    void        *skinExpVB[SKIN_CACHE_SIZE];   /* cached expanded IDirect3DVertexBuffer9* */
    unsigned int skinExpKey[SKIN_CACHE_SIZE];   /* hash key per slot */
    unsigned int skinExpNv[SKIN_CACHE_SIZE];    /* vertex count per slot */
    void        *skinExpDecl;                  /* IDirect3DVertexDeclaration9* for expanded layout */
#endif /* EXPAND_SKIN_VERTICES */
#endif /* ENABLE_SKINNING */

    /* Vertex element tracking */
    int curDeclHasTexcoord;
    int curDeclHasNormal;
    int curDeclHasColor;
    int curDeclColorOff;    /* byte offset of COLOR[0] in vertex (-1 if none) */
    int curDeclHasPosT;     /* 1 if current decl has POSITIONT (screen-space, skips FFP transform) */
    int curDeclHasMorph;    /* 1 if current decl has POSITION[1] (morph target — Lara blend shapes) */
    int curDeclPosType;     /* D3DDECLTYPE of POSITION element (2=FLOAT3, 6=SHORT4, etc.) */

    /* Texcoord format for diagnostics and skinned vertex expansion */
    int curDeclTexcoordType; /* D3DDECLTYPE of TEXCOORD[0], or -1 if none */
    int curDeclTexcoordOff;  /* byte offset of TEXCOORD[0] in vertex */

    /* Texture tracking (stages 0-7) */
    void *curTexture[8];
    int albedoStage;

    /* Stream source tracking (streams 0-3) */
    void *streamVB[4];
    unsigned int streamOffset[4];
    unsigned int streamStride[4];

    /* Transform override state (shader passthrough mode) */
    float cachedVPInverse[16];  /* cached inverse(View * Projection) */
    float lastVP[16];           /* last VP used for cache invalidation */
    float cached3DProj[16];     /* first valid 3D projection (for quad detection) */
    int proj3DCached;           /* 1 once cached3DProj is set */
    int vpInverseValid;         /* 1 if cachedVPInverse is current */
    int transformOverrideActive; /* 1 while proxy-applied SetTransform is active */
    int memoryPatchesApplied;   /* one-shot flag for game memory patches */
    int diagMemLogged;          /* one-shot flag for matrix verification log */

    /* Diagnostic state */
    void *loggedDecls[32];
    int loggedDeclCount;
    void *diagTexSeen[8][32];
    int diagTexUniq[8];
    unsigned int createTick;
    unsigned int diagLoggedFrames;
    unsigned int drawCallCount;
    unsigned int sceneCount;
    int vsConstWriteLog[256];

    /* Per-frame draw routing counters (logged at Present without delay) */
    unsigned int drawsProcessed;    /* went through TRL_PrepDraw */
    unsigned int drawsSkippedQuad;  /* caught by TRL_IsScreenSpaceQuad */
    unsigned int drawsPassthrough;  /* viewProjValid=0 or POSITIONT */
    unsigned int drawsTotal;        /* all DIP+DP calls this frame */
    unsigned int transformsBlocked; /* external SetTransform V/P/W blocked */
    unsigned int frameSummaryCount; /* how many frame summaries logged */

    /* Stripped-normal declaration cache: maps original decl → decl with NORMAL removed.
     * Remix's game capturer asserts normals are FLOAT3; TRL uses SHORT4N/DEC3N.
     * Stripping NORMAL from the declaration prevents the assertion while Remix
     * computes smooth normals via path tracing. */
    void *strippedDeclOrig[64];   /* original declaration pointer (lookup key) */
    void *strippedDeclFixed[64];  /* modified declaration with NORMAL removed */
    int strippedDeclCount;
} WrappedDevice;

#define REAL(self) (((WrappedDevice*)(self))->pReal)
#define REAL_VT(self) (*(void***)(REAL(self)))

static __inline void** RealVtbl(WrappedDevice *self) {
    return *(void***)(self->pReal);
}

static __inline void shader_addref(void *pShader) {
    if (pShader) {
        typedef unsigned long (__stdcall *FN)(void*);
        ((FN)(*(void***)pShader)[1])(pShader);
    }
}
static __inline void shader_release(void *pShader) {
    if (pShader) {
        typedef unsigned long (__stdcall *FN)(void*);
        ((FN)(*(void***)pShader)[2])(pShader);
    }
}

/* ---- FFP State Setup ---- */

typedef struct {
    float Diffuse[4];
    float Ambient[4];
    float Specular[4];
    float Emissive[4];
    float Power;
} D3DMATERIAL9;

/*
 * Setup lighting for FFP mode.
 * Disables FFP lighting since vertex declarations typically lack normals
 * and RTX Remix handles lighting via ray tracing. Sets a white material
 * so unlit FFP output is visible.
 */
static void FFP_SetupLighting(WrappedDevice *self) {
    typedef int (__stdcall *FN_SetRenderState)(void*, unsigned int, unsigned int);
    typedef int (__stdcall *FN_SetMaterial)(void*, D3DMATERIAL9*);
    void **vt = RealVtbl(self);
    D3DMATERIAL9 mat;
    int i;

    ((FN_SetRenderState)vt[SLOT_SetRenderState])(self->pReal, D3DRS_LIGHTING, 0);

    for (i = 0; i < 4; i++) {
        mat.Diffuse[i] = 1.0f;
        mat.Ambient[i] = 1.0f;
        mat.Specular[i] = 0.0f;
        mat.Emissive[i] = 0.0f;
    }
    mat.Power = 0.0f;
    ((FN_SetMaterial)vt[SLOT_SetMaterial])(self->pReal, &mat);
}

/*
 * Setup texture stages for FFP mode.
 * Stage 0: modulate texture color with vertex/material diffuse.
 * Stage 1+: disabled.
 */
static void FFP_SetupTextureStages(WrappedDevice *self) {
    typedef int (__stdcall *FN_SetTSS)(void*, unsigned int, unsigned int, unsigned int);
    void **vt = RealVtbl(self);

    ((FN_SetTSS)vt[SLOT_SetTextureStageState])(self->pReal, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    ((FN_SetTSS)vt[SLOT_SetTextureStageState])(self->pReal, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    ((FN_SetTSS)vt[SLOT_SetTextureStageState])(self->pReal, 0, D3DTSS_COLORARG2, D3DTA_CURRENT);
    ((FN_SetTSS)vt[SLOT_SetTextureStageState])(self->pReal, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    ((FN_SetTSS)vt[SLOT_SetTextureStageState])(self->pReal, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    ((FN_SetTSS)vt[SLOT_SetTextureStageState])(self->pReal, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    ((FN_SetTSS)vt[SLOT_SetTextureStageState])(self->pReal, 0, D3DTSS_TEXCOORDINDEX, 0);
    ((FN_SetTSS)vt[SLOT_SetTextureStageState])(self->pReal, 0, D3DTSS_TEXTURETRANSFORMFLAGS, 0);

    /* Disable stages 1-7: the game binds shadow maps, LUTs, normal maps etc.
     * on higher stages for its pixel shaders. In FFP mode those stages become
     * active and Remix may consume the wrong textures. */
    {
        int s;
        for (s = 1; s <= 7; s++) {
            ((FN_SetTSS)vt[SLOT_SetTextureStageState])(self->pReal, s, D3DTSS_COLOROP, D3DTOP_DISABLE);
            ((FN_SetTSS)vt[SLOT_SetTextureStageState])(self->pReal, s, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        }
    }
}

/* Transpose a 4x4 matrix (column-major -> row-major or vice versa) */
static void mat4_transpose(float *dst, const float *src) {
    dst[0]  = src[0];  dst[1]  = src[4];  dst[2]  = src[8];  dst[3]  = src[12];
    dst[4]  = src[1];  dst[5]  = src[5];  dst[6]  = src[9];  dst[7]  = src[13];
    dst[8]  = src[2];  dst[9]  = src[6];  dst[10] = src[10]; dst[11] = src[14];
    dst[12] = src[3];  dst[13] = src[7];  dst[14] = src[11]; dst[15] = src[15];
}

/* Multiply two 4x4 row-major matrices: dst = A * B */
static void mat4_multiply(float *dst, const float *A, const float *B) {
    int i, j;
    float tmp[16];
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            tmp[i*4+j] = A[i*4+0]*B[0*4+j] + A[i*4+1]*B[1*4+j]
                        + A[i*4+2]*B[2*4+j] + A[i*4+3]*B[3*4+j];
        }
    }
    for (i = 0; i < 16; i++) dst[i] = tmp[i];
}

/* Invert a 4x4 row-major matrix via cofactor expansion. Returns 0 on singular. */
static int mat4_invert(float *dst, const float *m) {
    float inv[16], det;
    int i;

    inv[0]  =  m[5]*(m[10]*m[15]-m[11]*m[14]) - m[9]*(m[6]*m[15]-m[7]*m[14]) + m[13]*(m[6]*m[11]-m[7]*m[10]);
    inv[4]  = -m[4]*(m[10]*m[15]-m[11]*m[14]) + m[8]*(m[6]*m[15]-m[7]*m[14]) - m[12]*(m[6]*m[11]-m[7]*m[10]);
    inv[8]  =  m[4]*(m[9]*m[15]-m[11]*m[13])  - m[8]*(m[5]*m[15]-m[7]*m[13]) + m[12]*(m[5]*m[11]-m[7]*m[9]);
    inv[12] = -m[4]*(m[9]*m[14]-m[10]*m[13])  + m[8]*(m[5]*m[14]-m[6]*m[13]) - m[12]*(m[5]*m[10]-m[6]*m[9]);

    inv[1]  = -m[1]*(m[10]*m[15]-m[11]*m[14]) + m[9]*(m[2]*m[15]-m[3]*m[14]) - m[13]*(m[2]*m[11]-m[3]*m[10]);
    inv[5]  =  m[0]*(m[10]*m[15]-m[11]*m[14]) - m[8]*(m[2]*m[15]-m[3]*m[14]) + m[12]*(m[2]*m[11]-m[3]*m[10]);
    inv[9]  = -m[0]*(m[9]*m[15]-m[11]*m[13])  + m[8]*(m[1]*m[15]-m[3]*m[13]) - m[12]*(m[1]*m[11]-m[3]*m[9]);
    inv[13] =  m[0]*(m[9]*m[14]-m[10]*m[13])  - m[8]*(m[1]*m[14]-m[2]*m[13]) + m[12]*(m[1]*m[10]-m[2]*m[9]);

    inv[2]  =  m[1]*(m[6]*m[15]-m[7]*m[14]) - m[5]*(m[2]*m[15]-m[3]*m[14]) + m[13]*(m[2]*m[7]-m[3]*m[6]);
    inv[6]  = -m[0]*(m[6]*m[15]-m[7]*m[14]) + m[4]*(m[2]*m[15]-m[3]*m[14]) - m[12]*(m[2]*m[7]-m[3]*m[6]);
    inv[10] =  m[0]*(m[5]*m[15]-m[7]*m[13])  - m[4]*(m[1]*m[15]-m[3]*m[13]) + m[12]*(m[1]*m[7]-m[3]*m[5]);
    inv[14] = -m[0]*(m[5]*m[14]-m[6]*m[13])  + m[4]*(m[1]*m[14]-m[2]*m[13]) - m[12]*(m[1]*m[6]-m[2]*m[5]);

    inv[3]  = -m[1]*(m[6]*m[11]-m[7]*m[10]) + m[5]*(m[2]*m[11]-m[3]*m[10]) - m[9]*(m[2]*m[7]-m[3]*m[6]);
    inv[7]  =  m[0]*(m[6]*m[11]-m[7]*m[10]) - m[4]*(m[2]*m[11]-m[3]*m[10]) + m[8]*(m[2]*m[7]-m[3]*m[6]);
    inv[11] = -m[0]*(m[5]*m[11]-m[7]*m[9])  + m[4]*(m[1]*m[11]-m[3]*m[9])  - m[8]*(m[1]*m[7]-m[3]*m[5]);
    inv[15] =  m[0]*(m[5]*m[10]-m[6]*m[9])  - m[4]*(m[1]*m[10]-m[2]*m[9])  + m[8]*(m[1]*m[6]-m[2]*m[5]);

    det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (det > -1e-10f && det < 1e-10f) return 0;

    det = 1.0f / det;
    for (i = 0; i < 16; i++) dst[i] = inv[i] * det;
    return 1;
}

/* Quantize matrix elements to a grid to stabilize hashes */
static void mat4_quantize(float *m, float grid) {
    int i;
    float inv = 1.0f / grid;
    for (i = 0; i < 16; i++) {
        float v = m[i] * inv;
        m[i] = ((v >= 0) ? (float)(int)(v + 0.5f) : (float)(int)(v - 0.5f)) * grid;
    }
}

/* Check if two 4x4 matrices differ by more than threshold in any element */
static int mat4_changed(const float *a, const float *b, float threshold) {
    int i;
    for (i = 0; i < 16; i++) {
        float d = a[i] - b[i];
        if (d > threshold || d < -threshold) return 1;
    }
    return 0;
}

/* Check if two 4x4 matrices are approximately equal within tolerance */
static int mat4_approx_equal(const float *a, const float *b, float tol) {
    int i;
    for (i = 0; i < 16; i++) {
        float d = a[i] - b[i];
        if (d > tol || d < -tol) return 0;
    }
    return 1;
}

/* Returns true if a 4x4 matrix is non-zero and non-identity (worth logging) */
static int mat4_is_interesting(const float *m) {
    int all_zero = 1, i;
    for (i = 0; i < 16; i++) {
        if (m[i] != 0.0f) { all_zero = 0; break; }
    }
    if (all_zero) return 0;
    if (m[0]==1.0f && m[1]==0.0f && m[2]==0.0f  && m[3]==0.0f &&
        m[4]==0.0f && m[5]==1.0f && m[6]==0.0f  && m[7]==0.0f &&
        m[8]==0.0f && m[9]==0.0f && m[10]==1.0f && m[11]==0.0f &&
        m[12]==0.0f && m[13]==0.0f && m[14]==0.0f && m[15]==1.0f) return 0;
    return 1;
}

/* Log a 4x4 matrix row by row (for diagnostics) */
static void diag_log_matrix(const char *name, const float *m) {
    log_str(name);
    log_str(":\r\n");
    log_floats_dec("  row0: ", (float*)&m[0], 4);
    log_floats_dec("  row1: ", (float*)&m[4], 4);
    log_floats_dec("  row2: ", (float*)&m[8], 4);
    log_floats_dec("  row3: ", (float*)&m[12], 4);
}

/*
 * Read View and Projection from game memory, decompose World from WVP.
 *
 * TRL fuses W*V*P into c0-c3 (column-major). We read the authoritative
 * View and Projection from hardcoded addresses, compute VP, cache its
 * inverse, and derive World = WVP * inverse(VP). The World matrix is
 * quantized to a 1e-3 grid for Remix hash stability.
 *
 * Shaders remain active — we only call SetTransform to inform Remix.
 */
static void TRL_ApplyTransformOverrides(WrappedDevice *self) {
    typedef int (__stdcall *FN_SetTransform)(void*, unsigned int, float*);
    void **vt = RealVtbl(self);
    float view[16], proj[16], vp[16], world[16];
    const float *gameView, *gameProj;
    static float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    int i;

    if (!self->viewProjDirty && !self->worldDirty)
        return;

    /* Read View and Projection directly from game memory (row-major) */
    gameView = (const float *)TRL_VIEW_MATRIX_ADDR;
    gameProj = (const float *)TRL_PROJ_MATRIX_ADDR;
    for (i = 0; i < 16; i++) {
        view[i] = gameView[i];
        proj[i] = gameProj[i];
    }

    /* Cache the first valid 3D projection for quad detection.
     * The game overwrites the Proj address for UI/overlay passes, so the
     * quad filter must compare against the original 3D projection, not the
     * live value (which would match UI draws → false-positive skip). */
    if (!self->proj3DCached && proj[0] != 0.0f) {
        for (i = 0; i < 16; i++) self->cached3DProj[i] = proj[i];
        self->proj3DCached = 1;
    }

    /* Compute VP = View * Projection */
    mat4_multiply(vp, view, proj);

    /* VP inverse caching: only recompute when camera/projection changes */
    if (!self->vpInverseValid || mat4_changed(vp, self->lastVP, VP_CHANGE_THRESHOLD)) {
        if (!mat4_invert(self->cachedVPInverse, vp)) {
            for (i = 0; i < 16; i++) self->cachedVPInverse[i] = identity[i];
        }
        for (i = 0; i < 16; i++) self->lastVP[i] = vp[i];
        self->vpInverseValid = 1;
    }

    /*
     * All TRL shaders use c0-c3 = WVP as the only transform matrix.
     * c4-c6 contains fog/lighting parameters, NOT a World matrix (confirmed
     * by live tracing — CTAB label "World" is misleading).
     * Decompose: World = WVP * inverse(VP) for all draw types.
     * For skinned draws where c0-c3 = VP, this yields identity — correct
     * since bones/morphs produce world-space positions and Remix uses
     * vertex capture for final positions.
     */
    {
        float wvp_row[16];
        mat4_transpose(wvp_row, &self->vsConst[VS_REG_WVP_START * 4]);
        mat4_multiply(world, wvp_row, self->cachedVPInverse);
        mat4_quantize(world, WORLD_QUANT_GRID);
    }

    /* Log game memory matrices once to verify addresses are correct */
#if DIAG_ENABLED
    if (!self->diagMemLogged) {
        self->diagMemLogged = 1;
        log_str("=== Game Memory Matrix Verification ===\r\n");
        diag_log_matrix("  View (0x010FC780)", view);
        diag_log_matrix("  Proj (0x01002530)", proj);
        diag_log_matrix("  VP (computed)", vp);
        diag_log_matrix("  World (applied)", world);
        log_int("  skinned=", self->curDeclIsSkinned);
    }
#endif

    /* Apply all three transforms (rtx.fusedWorldViewMode=0 treats them independently) */
    self->transformOverrideActive = 1;
    ((FN_SetTransform)vt[SLOT_SetTransform])(self->pReal, D3DTS_WORLD, world);
    ((FN_SetTransform)vt[SLOT_SetTransform])(self->pReal, D3DTS_VIEW, view);
    ((FN_SetTransform)vt[SLOT_SetTransform])(self->pReal, D3DTS_PROJECTION, proj);
    self->transformOverrideActive = 0;

    self->viewProjDirty = 0;
    self->worldDirty = 0;
}

#if ENABLE_SKINNING
#include "d3d9_skinning.h"
#endif

/*
 * Shader Passthrough mode: keep shaders active, apply transform overrides.
 * Shaders run normally (SHORT4 positions need the VS to decode), but we
 * call SetTransform so Remix sees decomposed W/V/P for path tracing.
 */
static void TRL_PrepDraw(WrappedDevice *self) {
    TRL_ApplyTransformOverrides(self);
    self->ffpActive = 1; /* marks that we've applied overrides this draw */
}

/*
 * Screen-space quad detection — DISABLED.
 * dxwrapper routes post-processing draws through DrawPrimitiveUP, not DIP/DP.
 * The matrix-comparison approach caused false positives (all UI draws skipped)
 * because the game updates the Proj memory address for each render pass.
 * All draws that reach our DIP/DP/UP interceptors are real geometry or UI
 * that should be rendered.
 */
static int TRL_IsScreenSpaceQuad(WrappedDevice *self) {
    (void)self;
    return 0;
}

/* No-op replacements for FFP_Engage/FFP_Disengage — shaders stay active */
#define FFP_Engage(self) TRL_PrepDraw(self)
#define FFP_Disengage(self) ((void)0)

/* ---- Vtable method implementations ---- */

static void *s_device_vtbl[DEVICE_VTABLE_SIZE];

/* 0: QueryInterface */
static int __stdcall WD_QueryInterface(WrappedDevice *self, void *riid, void **ppv) {
    typedef int (__stdcall *FN)(void*, void*, void**);
    return ((FN)RealVtbl(self)[0])(self->pReal, riid, ppv);
}

/* 1: AddRef */
static unsigned long __stdcall WD_AddRef(WrappedDevice *self) {
    typedef unsigned long (__stdcall *FN)(void*);
    self->refCount++;
    return ((FN)RealVtbl(self)[1])(self->pReal);
}

/* 2: Release */
static unsigned long __stdcall WD_Release(WrappedDevice *self) {
    typedef unsigned long (__stdcall *FN)(void*);
    unsigned long rc = ((FN)RealVtbl(self)[2])(self->pReal);
    self->refCount--;
    if (self->refCount <= 0) {
        log_str("WrappedDevice released\r\n");
        shader_release(self->lastVS);
        shader_release(self->lastPS);
        self->lastVS = NULL;
        self->lastPS = NULL;
        /* Release cached stripped declarations */
        {
            typedef unsigned long (__stdcall *FN_Rel)(void*);
            int sd;
            for (sd = 0; sd < self->strippedDeclCount; sd++) {
                if (self->strippedDeclFixed[sd])
                    ((FN_Rel)(*(void***)self->strippedDeclFixed[sd])[2])(self->strippedDeclFixed[sd]);
            }
        }
#if ENABLE_SKINNING
        Skin_ReleaseDevice(self);
#endif
        HeapFree(GetProcessHeap(), 0, self);
    }
    return rc;
}

/* ---- Relay thunks for non-intercepted methods ---- */

#ifdef _MSC_VER
/* MSVC x86 naked thunks: replace 'this' with pReal and jump to real vtable */
#define RELAY_THUNK(name, slot) \
    static __declspec(naked) void __stdcall name(void) { \
        __asm { mov eax, [esp+4] }      /* eax = WrappedDevice* */ \
        __asm { mov ecx, [eax+4] }      /* ecx = pReal */ \
        __asm { mov [esp+4], ecx }      /* replace this */ \
        __asm { mov eax, [ecx] }        /* eax = real vtable */ \
        __asm { jmp dword ptr [eax + slot*4] } \
    }

RELAY_THUNK(Relay_03, 3)    /* TestCooperativeLevel */
RELAY_THUNK(Relay_04, 4)    /* GetAvailableTextureMem */
RELAY_THUNK(Relay_05, 5)    /* EvictManagedResources */
RELAY_THUNK(Relay_06, 6)    /* GetDirect3D */
RELAY_THUNK(Relay_07, 7)    /* GetDeviceCaps */
RELAY_THUNK(Relay_08, 8)    /* GetDisplayMode */
RELAY_THUNK(Relay_09, 9)    /* GetCreationParameters */
RELAY_THUNK(Relay_10, 10)   /* SetCursorProperties */
RELAY_THUNK(Relay_11, 11)   /* SetCursorPosition */
RELAY_THUNK(Relay_12, 12)   /* ShowCursor */
RELAY_THUNK(Relay_13, 13)   /* CreateAdditionalSwapChain */
RELAY_THUNK(Relay_14, 14)   /* GetSwapChain */
RELAY_THUNK(Relay_15, 15)   /* GetNumberOfSwapChains */
RELAY_THUNK(Relay_18, 18)   /* GetBackBuffer */
RELAY_THUNK(Relay_19, 19)   /* GetRasterStatus */
RELAY_THUNK(Relay_20, 20)   /* SetDialogBoxMode */
RELAY_THUNK(Relay_21, 21)   /* SetGammaRamp */
RELAY_THUNK(Relay_22, 22)   /* GetGammaRamp */
RELAY_THUNK(Relay_23, 23)   /* CreateTexture */
RELAY_THUNK(Relay_24, 24)   /* CreateVolumeTexture */
RELAY_THUNK(Relay_25, 25)   /* CreateCubeTexture */
RELAY_THUNK(Relay_26, 26)   /* CreateVertexBuffer */
RELAY_THUNK(Relay_27, 27)   /* CreateIndexBuffer */
RELAY_THUNK(Relay_28, 28)   /* CreateRenderTarget */
RELAY_THUNK(Relay_29, 29)   /* CreateDepthStencilSurface */
RELAY_THUNK(Relay_30, 30)   /* UpdateSurface */
RELAY_THUNK(Relay_31, 31)   /* UpdateTexture */
RELAY_THUNK(Relay_32, 32)   /* GetRenderTargetData */
RELAY_THUNK(Relay_33, 33)   /* GetFrontBufferData */
RELAY_THUNK(Relay_34, 34)   /* StretchRect */
RELAY_THUNK(Relay_35, 35)   /* ColorFill */
RELAY_THUNK(Relay_36, 36)   /* CreateOffscreenPlainSurface */
RELAY_THUNK(Relay_37, 37)   /* SetRenderTarget */
RELAY_THUNK(Relay_38, 38)   /* GetRenderTarget */
RELAY_THUNK(Relay_39, 39)   /* SetDepthStencilSurface */
RELAY_THUNK(Relay_40, 40)   /* GetDepthStencilSurface */
RELAY_THUNK(Relay_43, 43)   /* Clear */
/* Relay_44 removed — SetTransform is now intercepted */
RELAY_THUNK(Relay_45, 45)   /* GetTransform */
RELAY_THUNK(Relay_46, 46)   /* MultiplyTransform */
RELAY_THUNK(Relay_47, 47)   /* SetViewport */
RELAY_THUNK(Relay_48, 48)   /* GetViewport */
RELAY_THUNK(Relay_49, 49)   /* SetMaterial */
RELAY_THUNK(Relay_50, 50)   /* GetMaterial */
RELAY_THUNK(Relay_51, 51)   /* SetLight */
RELAY_THUNK(Relay_52, 52)   /* GetLight */
RELAY_THUNK(Relay_53, 53)   /* LightEnable */
RELAY_THUNK(Relay_54, 54)   /* GetLightEnable */
RELAY_THUNK(Relay_55, 55)   /* SetClipPlane */
RELAY_THUNK(Relay_56, 56)   /* GetClipPlane */
/* Relay_57 removed — SetRenderState is now intercepted */
RELAY_THUNK(Relay_58, 58)   /* GetRenderState */
RELAY_THUNK(Relay_59, 59)   /* CreateStateBlock */
RELAY_THUNK(Relay_60, 60)   /* BeginStateBlock */
RELAY_THUNK(Relay_61, 61)   /* EndStateBlock */
RELAY_THUNK(Relay_62, 62)   /* SetClipStatus */
RELAY_THUNK(Relay_63, 63)   /* GetClipStatus */
RELAY_THUNK(Relay_64, 64)   /* GetTexture */
RELAY_THUNK(Relay_66, 66)   /* GetTextureStageState */
RELAY_THUNK(Relay_67, 67)   /* SetTextureStageState */
RELAY_THUNK(Relay_68, 68)   /* GetSamplerState */
RELAY_THUNK(Relay_69, 69)   /* SetSamplerState */
RELAY_THUNK(Relay_70, 70)   /* ValidateDevice */
RELAY_THUNK(Relay_71, 71)   /* SetPaletteEntries */
RELAY_THUNK(Relay_72, 72)   /* GetPaletteEntries */
RELAY_THUNK(Relay_73, 73)   /* SetCurrentTexturePalette */
RELAY_THUNK(Relay_74, 74)   /* GetCurrentTexturePalette */
RELAY_THUNK(Relay_75, 75)   /* SetScissorRect */
RELAY_THUNK(Relay_76, 76)   /* GetScissorRect */
RELAY_THUNK(Relay_77, 77)   /* SetSoftwareVertexProcessing */
RELAY_THUNK(Relay_78, 78)   /* GetSoftwareVertexProcessing */
RELAY_THUNK(Relay_79, 79)   /* SetNPatchMode */
RELAY_THUNK(Relay_80, 80)   /* GetNPatchMode */
/* DrawPrimitiveUP and DrawIndexedPrimitiveUP are intercepted below */
RELAY_THUNK(Relay_85, 85)   /* ProcessVertices */
RELAY_THUNK(Relay_86, 86)   /* CreateVertexDeclaration */
RELAY_THUNK(Relay_88, 88)   /* GetVertexDeclaration */
/* Relay_89 removed — SetFVF is now intercepted */
RELAY_THUNK(Relay_90, 90)   /* GetFVF */
RELAY_THUNK(Relay_91, 91)   /* CreateVertexShader */
RELAY_THUNK(Relay_93, 93)   /* GetVertexShader */
RELAY_THUNK(Relay_95, 95)   /* GetVertexShaderConstantF */
RELAY_THUNK(Relay_96, 96)   /* SetVertexShaderConstantI */
RELAY_THUNK(Relay_97, 97)   /* GetVertexShaderConstantI */
RELAY_THUNK(Relay_98, 98)   /* SetVertexShaderConstantB */
RELAY_THUNK(Relay_99, 99)   /* GetVertexShaderConstantB */
RELAY_THUNK(Relay_101, 101) /* GetStreamSource */
RELAY_THUNK(Relay_102, 102) /* SetStreamSourceFreq */
RELAY_THUNK(Relay_103, 103) /* GetStreamSourceFreq */
RELAY_THUNK(Relay_104, 104) /* SetIndices */
RELAY_THUNK(Relay_105, 105) /* GetIndices */
RELAY_THUNK(Relay_106, 106) /* CreatePixelShader */
RELAY_THUNK(Relay_108, 108) /* GetPixelShader */
RELAY_THUNK(Relay_110, 110) /* GetPixelShaderConstantF */
RELAY_THUNK(Relay_111, 111) /* SetPixelShaderConstantI */
RELAY_THUNK(Relay_112, 112) /* GetPixelShaderConstantI */
RELAY_THUNK(Relay_113, 113) /* SetPixelShaderConstantB */
RELAY_THUNK(Relay_114, 114) /* GetPixelShaderConstantB */
RELAY_THUNK(Relay_115, 115) /* DrawRectPatch */
RELAY_THUNK(Relay_116, 116) /* DrawTriPatch */
RELAY_THUNK(Relay_117, 117) /* DeletePatch */
RELAY_THUNK(Relay_118, 118) /* CreateQuery */

#else
#error "Only MSVC x86 is supported (needs __declspec(naked) + inline asm)"
#endif

/* ---- Intercepted method implementations ---- */

/* 16: Reset — invalidates all resources */
static int __stdcall WD_Reset(WrappedDevice *self, void *pPresentParams) {
    typedef int (__stdcall *FN)(void*, void*);
    int hr;

    log_str("== Device Reset ==\r\n");

    shader_release(self->lastVS);
    shader_release(self->lastPS);
    self->lastVS = NULL;
    self->lastPS = NULL;
    self->viewProjValid = 0;
    self->ffpSetup = 0;
    self->worldDirty = 0;
    self->viewProjDirty = 0;
    self->psConstDirty = 0;
    self->ffpActive = 0;
    self->vpInverseValid = 0;
    /* Stripped declarations are device resources — release before Reset */
    {
        typedef unsigned long (__stdcall *FN_Rel)(void*);
        int sd;
        for (sd = 0; sd < self->strippedDeclCount; sd++) {
            if (self->strippedDeclFixed[sd])
                ((FN_Rel)(*(void***)self->strippedDeclFixed[sd])[2])(self->strippedDeclFixed[sd]);
        }
        self->strippedDeclCount = 0;
    }
#if ENABLE_SKINNING && EXPAND_SKIN_VERTICES
    SkinVB_ReleaseCache(self);
#endif

    hr = ((FN)RealVtbl(self)[SLOT_Reset])(self->pReal, pPresentParams);
    log_hex("  Reset hr=", hr);
    return hr;
}

/* 17: Present */
static int __stdcall WD_Present(WrappedDevice *self, void *a, void *b, void *c, void *d) {
    typedef int (__stdcall *FN)(void*, void*, void*, void*, void*);
    int hr;

#if DIAG_ENABLED
    if (DIAG_ACTIVE(self)) {
        log_str("==== PRESENT frame ");
        log_int("", self->frameCount);
        log_int("  diagFrame: ", self->diagLoggedFrames);
        log_int("  drawCalls: ", self->drawCallCount);
        log_int("  scenes: ", self->sceneCount);
        {
            int r;
            log_str("  VS regs written: ");
            for (r = 0; r < 256; r++) {
                if (self->vsConstWriteLog[r]) {
                    log_int("c", r);
                }
            }
            log_str("\r\n");
        }
        {
            int ts;
            log_str("  Unique textures per stage:\r\n");
            for (ts = 0; ts < 8; ts++) {
                if (self->diagTexUniq[ts] > 0) {
                    log_int("    stage ", ts);
                    log_int("      unique=", self->diagTexUniq[ts]);
                }
            }
        }
        self->diagLoggedFrames++;
        { int ts; for (ts = 0; ts < 8; ts++) self->diagTexUniq[ts] = 0; }
    }
#endif

    /* Log frame draw routing summary every 60 frames (~1/sec), no delay */
    if (self->frameSummaryCount < 10 && self->frameCount > 0 && (self->frameCount % 60) == 0) {
        log_str("== FRAME ");
        log_int("", self->frameCount);
        log_int("  total=", self->drawsTotal);
        log_int("  processed=", self->drawsProcessed);
        log_int("  skippedQuad=", self->drawsSkippedQuad);
        log_int("  passthrough=", self->drawsPassthrough);
        log_int("  xformBlocked=", self->transformsBlocked);
        log_int("  vpValid=", self->viewProjValid);
        self->frameSummaryCount++;
    }

    self->frameCount++;
    self->ffpSetup = 0;
    self->ffpActive = 0;
    self->drawCallCount = 0;
    self->sceneCount = 0;
    self->drawsProcessed = 0;
    self->drawsSkippedQuad = 0;
    self->drawsPassthrough = 0;
    self->drawsTotal = 0;
    self->transformsBlocked = 0;
    {
        int r;
        for (r = 0; r < 256; r++) self->vsConstWriteLog[r] = 0;
    }
    hr = ((FN)RealVtbl(self)[SLOT_Present])(self->pReal, a, b, c, d);

    return hr;
}

/* 41: BeginScene — behavioral resets only (ffpActive, ffpSetup).
 * Counter resets happen in EndScene to capture the full multi-scene frame.
 * dxwrapper calls SwapChain::Present (not Device::Present), so we use
 * EndScene as the frame boundary trigger instead.
 * Re-applies frustum threshold every scene — the game recomputes it per-frame,
 * overwriting the one-shot patch from device creation. */
static int __stdcall WD_BeginScene(WrappedDevice *self) {
    typedef int (__stdcall *FN)(void*);
    self->ffpSetup = 0;
    self->ffpActive = 0;
    self->sceneCount++;

    /* Re-stamp frustum threshold every scene — the game recomputes it from
     * camera parameters each frame, overwriting the one-shot patch. */
    {
        DWORD oldProtect;
        if (VirtualProtect((void*)TRL_FRUSTUM_THRESHOLD_ADDR, 4, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            *(float*)TRL_FRUSTUM_THRESHOLD_ADDR = -1e30f;
            VirtualProtect((void*)TRL_FRUSTUM_THRESHOLD_ADDR, 4, oldProtect, &oldProtect);
        }
    }

    /* Re-stamp cull globals every scene — the renderer reads these cached values
     * and only calls SetRenderState on transitions, skipping our proxy hook. */
    {
        DWORD oldProtect;
        if (VirtualProtect((void*)TRL_CULL_MODE_PASS1_ADDR, 12, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            *(unsigned int*)TRL_CULL_MODE_PASS1_ADDR    = 1; /* D3DCULL_NONE */
            *(unsigned int*)TRL_CULL_MODE_PASS2_ADDR    = 1;
            *(unsigned int*)TRL_CULL_MODE_PASS2_INV_ADDR = 1;
            VirtualProtect((void*)TRL_CULL_MODE_PASS1_ADDR, 12, oldProtect, &oldProtect);
        }
    }

    return ((FN)RealVtbl(self)[SLOT_BeginScene])(self->pReal);
}

/* 42: EndScene — frame boundary detection.
 * TRL uses multiple BeginScene/EndScene pairs per frame. The last EndScene
 * before SwapChain::Present is the real frame boundary. We detect this by
 * checking if the NEXT BeginScene starts a new logical frame (indicated by
 * endSceneDrawCount > 0, meaning draws happened in this scene). We log and
 * reset at every EndScene where draws occurred, accumulating across scenes
 * via the running counters. */
static int __stdcall WD_EndScene(WrappedDevice *self) {
    typedef int (__stdcall *FN)(void*);

    /* Log frame summary every 120 scenes (~60 frames if 2 scenes/frame) */
    if (self->frameSummaryCount < 20 && self->sceneCount > 0 && (self->sceneCount % 120) == 0) {
        log_str("== SCENE ");
        log_int("", self->sceneCount);
        log_int("  total=", self->drawsTotal);
        log_int("  processed=", self->drawsProcessed);
        log_int("  skippedQuad=", self->drawsSkippedQuad);
        log_int("  passthrough=", self->drawsPassthrough);
        log_int("  xformBlocked=", self->transformsBlocked);
        log_int("  vpValid=", self->viewProjValid);
        self->frameSummaryCount++;

        self->drawsProcessed = 0;
        self->drawsSkippedQuad = 0;
        self->drawsPassthrough = 0;
        self->drawsTotal = 0;
        self->transformsBlocked = 0;
    }

#if DIAG_ENABLED
    if (DIAG_ACTIVE(self)) {
        log_str("==== SCENE ");
        log_int("", self->sceneCount);
        log_int("  drawCalls: ", self->drawCallCount);
        {
            int r;
            log_str("  VS regs written: ");
            for (r = 0; r < 256; r++) {
                if (self->vsConstWriteLog[r]) log_int("c", r);
            }
            log_str("\r\n");
        }
        self->diagLoggedFrames++;
        self->drawCallCount = 0;
        { int r; for (r = 0; r < 256; r++) self->vsConstWriteLog[r] = 0; }
    }
#endif

    self->frameCount++;
    return ((FN)RealVtbl(self)[SLOT_EndScene])(self->pReal);
}

/* 81: DrawPrimitive — GAME-SPECIFIC draw routing for non-indexed draws */
static int __stdcall WD_DrawPrimitive(WrappedDevice *self, unsigned int pt, unsigned int sv, unsigned int pc) {
    typedef int (__stdcall *FN)(void*, unsigned int, unsigned int, unsigned int);
    int hr;
    self->drawCallCount++;

    self->drawsTotal++;
    /* Guard: skip degenerate draws and draws without proper state.
     * Draws without viewProjValid, a vertex shader, or a declaration would
     * reach Remix without vertex capture producing position data, causing
     * empty position hashes. Suppress them entirely. */
    if (pc == 0 || !self->lastVS || !self->lastDecl) {
        self->drawsPassthrough++;
        return 0;
    }
    if (self->viewProjValid && !self->curDeclHasPosT) {
        if (TRL_IsScreenSpaceQuad(self)) {
            hr = 0;
            self->drawsSkippedQuad++;
        } else {
            TRL_PrepDraw(self);
            hr = ((FN)RealVtbl(self)[SLOT_DrawPrimitive])(self->pReal, pt, sv, pc);
            self->drawsProcessed++;
        }
    } else {
        /* No valid transforms or POSITIONT — suppress to prevent empty position hash */
        self->drawsPassthrough++;
        return 0;
    }

#if DIAG_ENABLED
    if (DIAG_ACTIVE(self) && self->drawCallCount <= 200) {
        log_int("  DP #", self->drawCallCount);
        log_int("    type=", pt);
        log_int("    startVtx=", sv);
        log_int("    primCount=", pc);
        log_hex("    hr=", hr);
    }
#endif
    return hr;
}

/* 82: DrawIndexedPrimitive — GAME-SPECIFIC draw routing (see prompt for decision tree) */
static int __stdcall WD_DrawIndexedPrimitive(WrappedDevice *self,
    unsigned int pt, int bvi, unsigned int mi, unsigned int nv,
    unsigned int si, unsigned int pc)
{
    typedef int (__stdcall *FN)(void*, unsigned int, int, unsigned int, unsigned int, unsigned int, unsigned int);
    int hr;
    self->drawCallCount++;

    self->drawsTotal++;
    /* Guard: skip degenerate draws and draws without proper state */
    if (pc == 0 || nv == 0 || !self->lastVS || !self->lastDecl) {
        self->drawsPassthrough++;
        return 0;
    }
    if (self->viewProjValid) {
        if (TRL_IsScreenSpaceQuad(self)) {
            hr = 0;
            self->drawsSkippedQuad++;
        } else {
            TRL_PrepDraw(self);
            hr = ((FN)RealVtbl(self)[SLOT_DrawIndexedPrimitive])(self->pReal, pt, bvi, mi, nv, si, pc);
            self->drawsProcessed++;
        }
    } else {
        /* No valid transforms — suppress to prevent empty position hash */
        self->drawsPassthrough++;
        return 0;
    }

#if DIAG_ENABLED
    /* Track unique textures per stage for this frame */
    if (DIAG_ACTIVE(self)) {
        int ts;
        for (ts = 0; ts < 8; ts++) {
            if (self->curTexture[ts]) {
                int found = 0, k;
                for (k = 0; k < self->diagTexUniq[ts] && k < 32; k++) {
                    if (self->diagTexSeen[ts][k] == self->curTexture[ts]) { found = 1; break; }
                }
                if (!found && self->diagTexUniq[ts] < 32) {
                    self->diagTexSeen[ts][self->diagTexUniq[ts]] = self->curTexture[ts];
                    self->diagTexUniq[ts]++;
                }
            }
        }
    }
    if (DIAG_ACTIVE(self) && self->drawCallCount <= 200) {
        log_int("  DIP #", self->drawCallCount);
        log_hex("    decl=", (unsigned int)self->lastDecl);
        log_int("    type=", pt);
        log_int("    baseVtx=", bvi);
        log_int("    numVerts=", nv);
        log_int("    primCount=", pc);
        log_hex("    hr=", hr);
        log_int("    stride0=", self->streamStride[0]);
        log_int("    stride1=", self->streamStride[1]);
        if (self->curDeclIsSkinned) {
            log_str("    [SKINNED]\r\n");
#if ENABLE_SKINNING
            log_int("    numBones=", self->numBones);
            log_int("    bonesDrawn=", self->bonesDrawn);
#endif
        }
        log_int("    posType=", self->curDeclPosType);
        log_int("    hasNormal=", self->curDeclHasNormal);
        log_int("    hasTexcoord=", self->curDeclHasTexcoord);
        log_int("    tcType=", self->curDeclTexcoordType);
        {
            int ts;
            for (ts = 0; ts < 8; ts++) {
                if (self->curTexture[ts]) {
                    log_int("    tex", ts);
                    log_hex("     =", (unsigned int)self->curTexture[ts]);
                }
            }
        }
        /* Log raw bytes of first vertex for early calls (helps diagnose vertex layout) */
        if (self->drawCallCount <= 10 && self->streamVB[0] && self->streamStride[0] > 0) {
            typedef int (__stdcall *FN_Lock)(void*, unsigned int, unsigned int, void**, unsigned int);
            typedef int (__stdcall *FN_Unlock)(void*);
            void **vbVt = *(void***)self->streamVB[0];
            unsigned char *vbData = NULL;
            unsigned int readOff = self->streamOffset[0] + (unsigned int)bvi * self->streamStride[0];
            int lockHr = ((FN_Lock)vbVt[11])(self->streamVB[0], readOff, self->streamStride[0] * 2, (void**)&vbData, 0x10 /*READONLY*/);
            if (lockHr == 0 && vbData) {
                unsigned int stride = self->streamStride[0];
                unsigned int b;
                log_int("    vtx0 raw (", stride);
                log_str(" bytes):\r\n      ");
                for (b = 0; b < stride && b < 64; b++) {
                    const char *hex = "0123456789ABCDEF";
                    char hx[4];
                    hx[0] = hex[(vbData[b] >> 4) & 0xF];
                    hx[1] = hex[vbData[b] & 0xF];
                    hx[2] = ' '; hx[3] = 0;
                    log_str(hx);
                    if (b == 11 || b == 15 || b == 19 || b == 23 || b == 27 || b == 31) log_str("| ");
                }
                log_str("\r\n");
                if (stride >= 12) {
                    float *fp = (float*)vbData;
                    log_floats_dec("      pos: ", fp, 3);
                }
                ((FN_Unlock)vbVt[12])(self->streamVB[0]);
            }
        }
        /* Log key VS constant register blocks on first 5 calls */
        if (self->drawCallCount <= 5) {
            if (mat4_is_interesting(&self->vsConst[0]))        diag_log_matrix("    c0-c3",   &self->vsConst[0]);
            if (mat4_is_interesting(&self->vsConst[4*4]))      diag_log_matrix("    c4-c7",   &self->vsConst[4*4]);
            if (mat4_is_interesting(&self->vsConst[8*4]))      diag_log_matrix("    c8-c11",  &self->vsConst[8*4]);
            if (mat4_is_interesting(&self->vsConst[12*4]))     diag_log_matrix("    c12-c15", &self->vsConst[12*4]);
            if (mat4_is_interesting(&self->vsConst[16*4]))     diag_log_matrix("    c16-c19", &self->vsConst[16*4]);
            if (mat4_is_interesting(&self->vsConst[20*4]))     diag_log_matrix("    c20-c23", &self->vsConst[20*4]);
            if (mat4_is_interesting(&self->vsConst[36*4]))     diag_log_matrix("    c36-c39", &self->vsConst[36*4]);
#if ENABLE_SKINNING
            if (self->curDeclIsSkinned && self->numBones > 0) {
                log_int("    bones uploaded=", self->numBones);
                log_int("    lastBoneStartReg=", self->lastBoneStartReg);
            }
#endif
        }
    }
#endif
    return hr;
}

/*
 * Neutralize vertex colors in UP draw data for hash stability.
 *
 * TRL bakes per-vertex lighting as D3DCOLOR in every vertex. These change
 * when the camera/player moves. Since UP draws pass vertex data inline,
 * Remix hashes the raw bytes — changing colors → changing hash → lights
 * and materials lose their anchor.
 *
 * Fix: copy vertex data to a scratch buffer and set all COLOR[0] to white
 * (0xFFFFFFFF). Remix handles lighting via path tracing, so vertex colors
 * aren't needed. The scratch buffer is static to avoid per-draw allocation.
 */
#define UP_SCRATCH_SIZE (1024 * 1024)  /* 1 MB — covers any single UP draw */
static unsigned char s_upScratch[UP_SCRATCH_SIZE];

static const void* NeutralizeVertexColors(WrappedDevice *self,
    const void *pVtxData, unsigned int numVerts, unsigned int stride)
{
    unsigned int totalBytes, v;
    int colorOff;

    if (!self->curDeclHasColor || self->curDeclColorOff < 0)
        return pVtxData;
    if (!pVtxData || stride == 0 || numVerts == 0)
        return pVtxData;

    totalBytes = numVerts * stride;
    if (totalBytes > UP_SCRATCH_SIZE)
        return pVtxData; /* too large, pass through unchanged */

    colorOff = self->curDeclColorOff;
    memcpy(s_upScratch, pVtxData, totalBytes);

    /* Set every COLOR[0] to white (0xFFFFFFFF) */
    for (v = 0; v < numVerts; v++) {
        unsigned int off = v * stride + colorOff;
        s_upScratch[off]   = 0xFF;
        s_upScratch[off+1] = 0xFF;
        s_upScratch[off+2] = 0xFF;
        s_upScratch[off+3] = 0xFF;
    }
    return (const void*)s_upScratch;
}

/* Vertex count from primitive type and count */
static unsigned int PrimCountToVertices(unsigned int primType, unsigned int primCount) {
    switch (primType) {
        case 1: return primCount;          /* POINTLIST */
        case 2: return primCount * 2;      /* LINELIST */
        case 3: return primCount + 1;      /* LINESTRIP */
        case 4: return primCount * 3;      /* TRIANGLELIST */
        case 5: return primCount + 2;      /* TRIANGLESTRIP */
        case 6: return primCount + 2;      /* TRIANGLEFAN */
        default: return primCount * 3;
    }
}

/* 83: DrawPrimitiveUP — dxwrapper routes most D3D8 draws through UP variants.
 * Must intercept to apply transform overrides, otherwise Remix sees only
 * dxwrapper's identity View/Proj transforms. Vertex colors are neutralized
 * for hash stability. */
static int __stdcall WD_DrawPrimitiveUP(WrappedDevice *self,
    unsigned int pt, unsigned int pc, const void *pVertexStreamZeroData,
    unsigned int vertexStreamZeroStride)
{
    typedef int (__stdcall *FN)(void*, unsigned int, unsigned int, const void*, unsigned int);
    unsigned int nv;
    self->drawCallCount++;
    self->drawsTotal++;

    /* Guard: skip degenerate/stateless draws */
    if (pc == 0 || !pVertexStreamZeroData || vertexStreamZeroStride == 0) {
        self->drawsPassthrough++;
        return 0;
    }
    if (self->viewProjValid && !self->curDeclHasPosT) {
        if (TRL_IsScreenSpaceQuad(self)) {
            self->drawsSkippedQuad++;
            return 0;
        }
        TRL_PrepDraw(self);
        self->drawsProcessed++;
    } else {
        /* No valid transforms — suppress */
        self->drawsPassthrough++;
        return 0;
    }

    nv = PrimCountToVertices(pt, pc);
    pVertexStreamZeroData = NeutralizeVertexColors(self, pVertexStreamZeroData, nv, vertexStreamZeroStride);

    return ((FN)RealVtbl(self)[83])(self->pReal, pt, pc, pVertexStreamZeroData, vertexStreamZeroStride);
}

/* 84: DrawIndexedPrimitiveUP — same as above for indexed UP draws. */
static int __stdcall WD_DrawIndexedPrimitiveUP(WrappedDevice *self,
    unsigned int pt, unsigned int minVertexIndex, unsigned int numVertices,
    unsigned int primitiveCount, const void *pIndexData, unsigned int indexDataFormat,
    const void *pVertexStreamZeroData, unsigned int vertexStreamZeroStride)
{
    typedef int (__stdcall *FN)(void*, unsigned int, unsigned int, unsigned int, unsigned int,
                                const void*, unsigned int, const void*, unsigned int);
    self->drawCallCount++;
    self->drawsTotal++;

    /* Guard: skip degenerate/stateless draws */
    if (primitiveCount == 0 || numVertices == 0 || !pVertexStreamZeroData || vertexStreamZeroStride == 0) {
        self->drawsPassthrough++;
        return 0;
    }
    if (self->viewProjValid && !self->curDeclHasPosT) {
        if (TRL_IsScreenSpaceQuad(self)) {
            self->drawsSkippedQuad++;
            return 0;
        }
        TRL_PrepDraw(self);
        self->drawsProcessed++;
    } else {
        /* No valid transforms — suppress */
        self->drawsPassthrough++;
        return 0;
    }

    pVertexStreamZeroData = NeutralizeVertexColors(self, pVertexStreamZeroData,
        numVertices, vertexStreamZeroStride);

    return ((FN)RealVtbl(self)[84])(self->pReal, pt, minVertexIndex, numVertices,
        primitiveCount, pIndexData, indexDataFormat, pVertexStreamZeroData, vertexStreamZeroStride);
}

/* 92: SetVertexShader */
static int __stdcall WD_SetVertexShader(WrappedDevice *self, void *pShader) {
    typedef int (__stdcall *FN)(void*, void*);
#if DIAG_ENABLED
    if (DIAG_ACTIVE(self)) {
        log_hex("  SetVS shader=", (unsigned int)pShader);
    }
#endif
    shader_addref(pShader);
    shader_release(self->lastVS);
    self->lastVS = pShader;
    self->ffpActive = 0;
    return ((FN)RealVtbl(self)[SLOT_SetVertexShader])(self->pReal, pShader);
}

/* 57: SetRenderState — force D3DCULL_NONE for 360-degree visibility.
 * Remix path tracing needs all backfaces visible so rays from any direction
 * can hit geometry. The handoff doc confirms this was in the working build. */
static int __stdcall WD_SetRenderState(WrappedDevice *self, unsigned int state, unsigned int value) {
    typedef int (__stdcall *FN)(void*, unsigned int, unsigned int);
    if (state == D3DRS_CULLMODE)
        value = 1; /* D3DCULL_NONE */
    return ((FN)RealVtbl(self)[SLOT_SetRenderState])(self->pReal, state, value);
}

/* 44: SetTransform — Block dxwrapper identity overrides permanently.
 * dxwrapper (D3D8→D3D9) sends ~1296 SetTransform calls per frame with
 * View=identity, Proj=identity, World=WVP-combined. These stomp our
 * decomposed W/V/P if allowed through. Once viewProjValid is set (first
 * c0-c3 write), ALL external SetTransform for V/P/W are blocked forever.
 * Our own calls go through via transformOverrideActive. */
static int __stdcall WD_SetTransform(WrappedDevice *self, unsigned int state, float *pMatrix) {
    typedef int (__stdcall *FN)(void*, unsigned int, float*);

    /* Always allow our own calls (transformOverrideActive is 1 inside TRL_ApplyTransformOverrides) */
    if (self->transformOverrideActive)
        return ((FN)RealVtbl(self)[SLOT_SetTransform])(self->pReal, state, pMatrix);

    /* Block ALL external V/P/W once we have valid transforms — dxwrapper sends identity
     * View/Proj between draws which breaks Remix's camera inference */
    if (self->viewProjValid &&
        (state == D3DTS_VIEW || state == D3DTS_PROJECTION || state == D3DTS_WORLD)) {
        self->transformsBlocked++;
        return 0;
    }

    return ((FN)RealVtbl(self)[SLOT_SetTransform])(self->pReal, state, pMatrix);
}

/* 94: SetVertexShaderConstantF — GAME-SPECIFIC: dirty tracking uses VS_REG_* defines */
static int __stdcall WD_SetVertexShaderConstantF(WrappedDevice *self,
    unsigned int startReg, float *pData, unsigned int count)
{
    typedef int (__stdcall *FN)(void*, unsigned int, float*, unsigned int);
    unsigned int i;

    if (pData && startReg + count <= 256) {
        for (i = 0; i < count * 4; i++) {
            self->vsConst[(startReg * 4) + i] = pData[i];
        }

        /* Dirty tracking: WVP (c0-c3), World (c4-c7), or ViewProject (c12-c15) triggers recompute */
        {
            unsigned int endReg = startReg + count;
            if (startReg < VS_REG_WVP_END && endReg > VS_REG_WVP_START) {
                self->viewProjDirty = 1;
                self->worldDirty = 1;
            }
            /* c4-c7: World matrix for skinned shaders */
            if (startReg < 8 && endReg > 4) {
                self->worldDirty = 1;
            }
            if (startReg < VS_REG_VP_END && endReg > VS_REG_VP_START) {
                self->viewProjDirty = 1;
                self->worldDirty = 1;
            }
        }

        /* Valid once WVP (c0-c3) or VP (c12-c15) has been written */
        if ((startReg <= VS_REG_WVP_START && startReg + count >= VS_REG_WVP_END) ||
            (startReg <= VS_REG_VP_START && startReg + count >= VS_REG_VP_END))
            self->viewProjValid = 1;

        for (i = 0; i < count; i++) {
            if (startReg + i < 256)
                self->vsConstWriteLog[startReg + i] = 1;
        }

#if ENABLE_SKINNING
        /* Immediate bone upload: transpose 4x3→4x4 and SetTransform now.
         * Supports per-bone (count==3) and bulk (count==N*3) writes. */
        if (startReg >= VS_REG_BONE_THRESHOLD &&
            count >= VS_REGS_PER_BONE &&
            (count % VS_REGS_PER_BONE) == 0 &&
            self->curDeclIsSkinned) {

            /* Detect object boundary and reset stale bones */
            int needsReset = self->bonesDrawn;
            if (!needsReset && self->numBones > 0 && startReg < self->lastBoneStartReg)
                needsReset = 1; /* startReg jumped backward → new object */

            if (needsReset) {
                typedef int (__stdcall *FN_ST)(void*, unsigned int, float*);
                static float ident[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
                int slot;
                for (slot = 0; slot < self->numBones; slot++)
                    ((FN_ST)RealVtbl(self)[SLOT_SetTransform])(
                        self->pReal, D3DTS_WORLDMATRIX(slot), ident);
                self->prevNumBones = self->numBones;
                self->numBones = 0;
                self->bonesDrawn = 0;
            }
            self->lastBoneStartReg = startReg;

            /* Upload each bone immediately */
            {
                typedef int (__stdcall *FN_SetTransform)(void*, unsigned int, float*);
                int b, nBatch = count / VS_REGS_PER_BONE;
                for (b = 0; b < nBatch && self->numBones < MAX_FFP_BONES; b++) {
                    float boneMat[16];
                    const float *src = &pData[b * VS_REGS_PER_BONE * 4];
                    boneMat[0]  = src[0];  boneMat[1]  = src[4];  boneMat[2]  = src[8];   boneMat[3]  = 0.0f;
                    boneMat[4]  = src[1];  boneMat[5]  = src[5];  boneMat[6]  = src[9];   boneMat[7]  = 0.0f;
                    boneMat[8]  = src[2];  boneMat[9]  = src[6];  boneMat[10] = src[10];  boneMat[11] = 0.0f;
                    boneMat[12] = src[3];  boneMat[13] = src[7];  boneMat[14] = src[11];  boneMat[15] = 1.0f;
                    ((FN_SetTransform)RealVtbl(self)[SLOT_SetTransform])(self->pReal,
                        D3DTS_WORLDMATRIX(self->numBones), boneMat);
                    self->numBones++;
                }
            }
        }
#endif

#if DIAG_ENABLED
        if (DIAG_ACTIVE(self)) {
            log_int("  SetVSConstF start=", startReg);
            log_int("    count=", count);
            if (count == 16) {
                /* 16-register pack: log as 4 separate 4x4 matrices */
                char label[32];
                int m;
                for (m = 0; m < 4; m++) {
                    int s = startReg + m * 4, e2 = s + 3, p2 = 0;
                    label[p2++] = 'c';
                    if (s >= 100) label[p2++] = '0' + (s / 100);
                    if (s >= 10)  label[p2++] = '0' + ((s / 10) % 10);
                    label[p2++] = '0' + (s % 10);
                    label[p2++] = '-'; label[p2++] = 'c';
                    if (e2 >= 100) label[p2++] = '0' + (e2 / 100);
                    if (e2 >= 10)  label[p2++] = '0' + ((e2 / 10) % 10);
                    label[p2++] = '0' + (e2 % 10);
                    label[p2] = '\0';
                    diag_log_matrix(label, &pData[m * 16]);
                }
            } else if (count == 4) {
                char label[32];
                int s = startReg, e2 = startReg + 3, p2 = 0;
                label[p2++] = 'c';
                if (s >= 100) label[p2++] = '0' + (s / 100);
                if (s >= 10)  label[p2++] = '0' + ((s / 10) % 10);
                label[p2++] = '0' + (s % 10);
                label[p2++] = '-'; label[p2++] = 'c';
                if (e2 >= 100) label[p2++] = '0' + (e2 / 100);
                if (e2 >= 10)  label[p2++] = '0' + ((e2 / 10) % 10);
                label[p2++] = '0' + (e2 % 10);
                label[p2] = '\0';
                diag_log_matrix(label, pData);
            } else if (count >= 1 && count <= 2) {
                log_floats_dec("    data: ", pData, count * 4);
            } else if (count >= 5) {
                log_str("    (large write, first 4x4):\r\n");
                log_floats_dec("      ", pData, 16);
            }
        }
#endif
    }

    return ((FN)RealVtbl(self)[SLOT_SetVertexShaderConstantF])(self->pReal, startReg, pData, count);
}

/* 107: SetPixelShader — always forward in passthrough mode.
 * The ffpActive swallowing was designed for full FFP conversion (where PS is set
 * to NULL), but in passthrough mode shaders stay active and PS must pass through
 * so each draw uses its correct pixel shader. */
static int __stdcall WD_SetPixelShader(WrappedDevice *self, void *pShader) {
    typedef int (__stdcall *FN)(void*, void*);
    shader_addref(pShader);
    shader_release(self->lastPS);
    self->lastPS = pShader;
    return ((FN)RealVtbl(self)[SLOT_SetPixelShader])(self->pReal, pShader);
}

/* 109: SetPixelShaderConstantF */
static int __stdcall WD_SetPixelShaderConstantF(WrappedDevice *self,
    unsigned int startReg, float *pData, unsigned int count)
{
    typedef int (__stdcall *FN)(void*, unsigned int, float*, unsigned int);
    unsigned int i;
    if (pData && startReg + count <= 32) {
        for (i = 0; i < count * 4; i++) {
            self->psConst[(startReg * 4) + i] = pData[i];
        }
        self->psConstDirty = 1;
    }
    return ((FN)RealVtbl(self)[SLOT_SetPixelShaderConstantF])(self->pReal, startReg, pData, count);
}

/* 65: SetTexture */
static int __stdcall WD_SetTexture(WrappedDevice *self, unsigned int stage, void *pTexture) {
    typedef int (__stdcall *FN)(void*, unsigned int, void*);
    if (stage < 8) {
        self->curTexture[stage] = pTexture;
    }
    return ((FN)RealVtbl(self)[SLOT_SetTexture])(self->pReal, stage, pTexture);
}

/* 100: SetStreamSource */
static int __stdcall WD_SetStreamSource(WrappedDevice *self,
    unsigned int stream, void *pVB, unsigned int offset, unsigned int stride)
{
    typedef int (__stdcall *FN)(void*, unsigned int, void*, unsigned int, unsigned int);
    if (stream < 4) {
        self->streamVB[stream] = pVB;
        self->streamOffset[stream] = offset;
        self->streamStride[stream] = stride;
    }
    return ((FN)RealVtbl(self)[SLOT_SetStreamSource])(self->pReal, stream, pVB, offset, stride);
}

/*
 * Get or create a stripped declaration with non-FLOAT3 NORMAL elements removed.
 * Remix's game capturer asserts normals are VK_FORMAT_R32G32B32_SFLOAT (FLOAT3).
 * TRL uses SHORT4N/DEC3N normals. Stripping them prevents the assertion; Remix
 * computes smooth normals via path tracing so input normals aren't needed.
 */
static void *GetStrippedDecl(WrappedDevice *self, void *pOrigDecl,
    unsigned char *elemBuf, unsigned int numElems, int hasNonFloat3Normal)
{
    typedef int (__stdcall *FN_CreateDecl)(void*, void*, void**);
    int i;
    unsigned char filtered[8 * 32];
    unsigned int outIdx = 0;
    void *newDecl = NULL;

    if (!hasNonFloat3Normal) return pOrigDecl;

    /* Check cache */
    for (i = 0; i < self->strippedDeclCount; i++) {
        if (self->strippedDeclOrig[i] == pOrigDecl)
            return self->strippedDeclFixed[i];
    }

    /* Build filtered elements: copy everything except non-FLOAT3 NORMAL */
    for (i = 0; (unsigned int)i < numElems; i++) {
        unsigned char *el = &elemBuf[i * 8];
        unsigned short stream = *(unsigned short*)&el[0];
        unsigned char  type   = el[4];
        unsigned char  usage  = el[6];

        if (stream == 0xFF || stream == 0xFFFF) {
            /* D3DDECL_END — always copy */
            memcpy(&filtered[outIdx * 8], el, 8);
            outIdx++;
            break;
        }
        if (usage == D3DDECLUSAGE_NORMAL && type != D3DDECLTYPE_FLOAT3) {
            continue; /* skip non-FLOAT3 normals */
        }
        memcpy(&filtered[outIdx * 8], el, 8);
        outIdx++;
    }

    /* Create new declaration on the real device */
    if (((FN_CreateDecl)RealVtbl(self)[SLOT_CreateVertexDeclaration])(
            self->pReal, filtered, &newDecl) == 0 && newDecl) {
        if (self->strippedDeclCount < 64) {
            self->strippedDeclOrig[self->strippedDeclCount] = pOrigDecl;
            self->strippedDeclFixed[self->strippedDeclCount] = newDecl;
            self->strippedDeclCount++;
        }
        log_hex("  Created stripped decl (NORMAL removed): ", (unsigned int)newDecl);
        return newDecl;
    }

    return pOrigDecl; /* fallback: use original */
}

/* 87: SetVertexDeclaration — Parse vertex elements, detect skinning */
static int __stdcall WD_SetVertexDeclaration(WrappedDevice *self, void *pDecl) {
    typedef int (__stdcall *FN)(void*, void*);
    void *declForDevice = pDecl; /* may be replaced with stripped version */

#if ENABLE_SKINNING
    /* Declaration change while bones are loaded → likely new object */
    if (self->curDeclIsSkinned && self->numBones > 0 && pDecl != self->lastDecl)
        self->bonesDrawn = 1;
#endif

    /* Force transform recompute on declaration change — the World derivation
     * may differ between draw types even when c0-c3 hasn't been rewritten. */
    if (pDecl != self->lastDecl)
        self->worldDirty = 1;

    self->lastDecl = pDecl;
    self->curDeclIsSkinned = 0;
    self->curDeclHasTexcoord = 0;
    self->curDeclHasNormal = 0;
    self->curDeclHasColor = 0;
    self->curDeclColorOff = -1;
    self->curDeclHasPosT = 0;
    self->curDeclHasMorph = 0;
    self->curDeclPosType = -1;
    self->curDeclTexcoordType = -1;
    self->curDeclTexcoordOff = 0;
#if ENABLE_SKINNING
    self->curDeclNumWeights = 0;
#if EXPAND_SKIN_VERTICES
    self->curDeclNormalOff = 0;    self->curDeclNormalType = -1;
    self->curDeclBlendWeightOff = 0;
    self->curDeclBlendWeightType = 0;
    self->curDeclBlendIndicesOff = 0;
    self->curDeclPosOff = 0;
#endif
#endif

    if (pDecl) {
        typedef int (__stdcall *FN_GetDecl)(void*, void*, unsigned int*);
        void **declVt = *(void***)pDecl;
        unsigned char elemBuf[8 * 32];
        unsigned int numElems = 0;
        int hr2 = ((FN_GetDecl)declVt[4])(pDecl, NULL, &numElems);
        if (hr2 == 0 && numElems > 0 && numElems <= 32) {
            hr2 = ((FN_GetDecl)declVt[4])(pDecl, elemBuf, &numElems);
            if (hr2 == 0) {
                unsigned int e;
                int hasBlendWeight = 0, hasBlendIndices = 0;
                int hasPosition1 = 0;
                int blendWeightType = 0;
                int hasNonFloat3Normal = 0;

                for (e = 0; e < numElems; e++) {
                    unsigned char *el = &elemBuf[e * 8];
                    unsigned short stream  = *(unsigned short*)&el[0];
                    unsigned short offset  = *(unsigned short*)&el[2];
                    unsigned char  type    = el[4];
                    unsigned char  usage   = el[6];
                    unsigned char  usageIdx = el[7];
                    if (stream == 0xFF || stream == 0xFFFF) break;

                    if (usage == D3DDECLUSAGE_POSITIONT) {
                        self->curDeclHasPosT = 1;
                    }
                    if (usage == D3DDECLUSAGE_BLENDWEIGHT) {
                        hasBlendWeight = 1;
                        blendWeightType = type;
#if ENABLE_SKINNING && EXPAND_SKIN_VERTICES
                        self->curDeclBlendWeightOff  = offset;
                        self->curDeclBlendWeightType = type;
#endif
                    }
                    if (usage == D3DDECLUSAGE_BLENDINDICES) {
                        hasBlendIndices = 1;
#if ENABLE_SKINNING && EXPAND_SKIN_VERTICES
                        self->curDeclBlendIndicesOff = offset;
#endif
                    }
                    if (usage == D3DDECLUSAGE_POSITION && stream == 0 && usageIdx == 0) {
                        self->curDeclPosType = type;
#if ENABLE_SKINNING && EXPAND_SKIN_VERTICES
                        self->curDeclPosOff = offset;
#endif
                    }
                    if (usage == D3DDECLUSAGE_POSITION && usageIdx == 1) {
                        hasPosition1 = 1;
                        self->curDeclHasMorph = 1;
                    }
                    if (usage == D3DDECLUSAGE_NORMAL && stream == 0) {
                        self->curDeclHasNormal = 1;
                        if (type != D3DDECLTYPE_FLOAT3)
                            hasNonFloat3Normal = 1;
#if ENABLE_SKINNING && EXPAND_SKIN_VERTICES
                        self->curDeclNormalOff  = offset;
                        self->curDeclNormalType = type;
#endif
                    }
                    if (usage == D3DDECLUSAGE_TEXCOORD && usageIdx == 0 && stream == 0) {
                        self->curDeclHasTexcoord    = 1;
                        self->curDeclTexcoordType   = type;
                        self->curDeclTexcoordOff    = offset;
                    }
                    if (usage == D3DDECLUSAGE_COLOR && usageIdx == 0) {
                        self->curDeclHasColor = 1;
                        self->curDeclColorOff = offset;
                    }
                }

                if (hasBlendWeight && hasBlendIndices) {
                    self->curDeclIsSkinned = 1;
#if ENABLE_SKINNING
                    switch (blendWeightType) {
                        case D3DDECLTYPE_FLOAT1:  self->curDeclNumWeights = 1; break;
                        case D3DDECLTYPE_FLOAT2:  self->curDeclNumWeights = 2; break;
                        case D3DDECLTYPE_FLOAT3:  self->curDeclNumWeights = 3; break;
                        case D3DDECLTYPE_FLOAT4:  self->curDeclNumWeights = 3; break;
                        case D3DDECLTYPE_UBYTE4N: self->curDeclNumWeights = 3; break;
                        default:                  self->curDeclNumWeights = 3; break;
                    }
#endif
                }

                /* Strip non-FLOAT3 normals to prevent Remix game capturer assertion */
                if (hasNonFloat3Normal) {
                    declForDevice = GetStrippedDecl(self, pDecl, elemBuf, numElems, 1);
                }

#if DIAG_ENABLED
                if (DIAG_ACTIVE(self)) {
                    int alreadyLogged = 0, di;
                    for (di = 0; di < self->loggedDeclCount; di++) {
                        if (self->loggedDecls[di] == pDecl) { alreadyLogged = 1; break; }
                    }
                    if (!alreadyLogged && self->loggedDeclCount < 32) {
                        static const char *usageNames[] = {
                            "POSITION", "BLENDWEIGHT", "BLENDINDICES", "NORMAL",
                            "PSIZE", "TEXCOORD", "TANGENT", "BINORMAL",
                            "TESSFACTOR", "POSITIONT", "COLOR", "FOG", "DEPTH", "SAMPLE"
                        };
                        static const char *typeNames[] = {
                            "FLOAT1", "FLOAT2", "FLOAT3", "FLOAT4", "D3DCOLOR",
                            "UBYTE4", "SHORT2", "SHORT4", "UBYTE4N", "SHORT2N",
                            "SHORT4N", "USHORT2N", "USHORT4N", "UDEC3", "DEC3N",
                            "FLOAT16_2", "FLOAT16_4", "UNUSED"
                        };
                        self->loggedDecls[self->loggedDeclCount++] = pDecl;
                        log_hex("  DECL decl=", (unsigned int)pDecl);
                        log_int("    numElems=", numElems);
                        if (self->curDeclIsSkinned) {
#if ENABLE_SKINNING
                            log_int("    SKINNED numWeights=", self->curDeclNumWeights);
#else
                            log_str("    SKINNED\r\n");
#endif
                        }
                        if (self->curDeclHasPosT) {
                            log_str("    POSITIONT\r\n");
                        }
                        if (hasNonFloat3Normal) {
                            log_str("    NORMAL stripped (non-FLOAT3)\r\n");
                        }
                        for (e = 0; e < numElems; e++) {
                            unsigned char *el = &elemBuf[e * 8];
                            unsigned short eStream = *(unsigned short*)&el[0];
                            unsigned short eOff    = *(unsigned short*)&el[2];
                            unsigned char  eType   = el[4];
                            unsigned char  eUsage  = el[6];
                            unsigned char  eUIdx   = el[7];
                            if (eStream == 0xFF || eStream == 0xFFFF) break;
                            log_str("    [s");
                            log_int("", eStream);
                            log_str("    +");
                            log_int("", eOff);
                            log_str("    ] ");
                            if (eUsage < 14) log_str(usageNames[eUsage]);
                            else log_int("usage=", eUsage);
                            log_str("[");
                            {
                                char ub[4]; ub[0] = '0' + eUIdx; ub[1] = ']'; ub[2] = ' '; ub[3] = 0;
                                log_str(ub);
                            }
                            if (eType <= 17) log_str(typeNames[eType]);
                            else log_int("type=", eType);
                            log_str("\r\n");
                        }
                    }
                }
#endif
            }
        }
    }

    return ((FN)RealVtbl(self)[SLOT_SetVertexDeclaration])(self->pReal, declForDevice);
}

/* 89: SetFVF — Strip D3DFVF_NORMAL flag from FVF codes.
 * dxwrapper's D3D8→D3D9 conversion may use SetFVF instead of SetVertexDeclaration.
 * Remix's game capturer asserts normals are FLOAT3, but FVF normals may not match
 * this expectation. Stripping the flag prevents the assertion; Remix computes smooth
 * normals via path tracing. */
static int __stdcall WD_SetFVF(WrappedDevice *self, unsigned int fvf) {
    typedef int (__stdcall *FN)(void*, unsigned int);
    if (fvf & D3DFVF_NORMAL) {
        fvf &= ~D3DFVF_NORMAL;
    }
    return ((FN)RealVtbl(self)[SLOT_SetFVF])(self->pReal, fvf);
}

/*
 * Apply game memory patches for culling and frustum visibility.
 * These are write-once patches to the running process memory.
 */
static void TRL_ApplyMemoryPatches(WrappedDevice *self) {
    DWORD oldProtect;

    if (self->memoryPatchesApplied) return;
    self->memoryPatchesApplied = 1;

    /* Frustum threshold: set to -1e30 so distance-based culling never triggers.
     * The game skips objects when distance <= threshold. Negative infinity catches
     * objects behind the camera that 0.0 would still cull. */
    if (VirtualProtect((void*)TRL_FRUSTUM_THRESHOLD_ADDR, 4, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        *(float*)TRL_FRUSTUM_THRESHOLD_ADDR = -1e30f;
        VirtualProtect((void*)TRL_FRUSTUM_THRESHOLD_ADDR, 4, oldProtect, &oldProtect);
        log_str("  Patched frustum threshold to -1e30\r\n");
    }

    /* NOP scene traversal cull jumps inside 0x407150 (SceneTraversal_CullAndSubmit).
     * These conditional jumps skip geometry based on distance and screen boundary checks.
     * Each is a 6-byte conditional near jump (0x0F 0x8x ...) replaced with 6x NOP. */
    {
        static const unsigned int cullJumpAddrs[] = {
            0x004072BD,  /* distance cull jump 1 */
            0x004072D2,  /* distance cull jump 2 */
            0x00407AF1,  /* distance cull jump 3 */
            0x00407B30,  /* screen boundary jump 1 */
            0x00407B49,  /* screen boundary jump 2 */
            0x00407B62,  /* screen boundary jump 3 */
            0x00407B7B,  /* screen boundary jump 4 */
        };
        int nopCount = 0;
        int i;
        for (i = 0; i < 7; i++) {
            unsigned char *p = (unsigned char *)cullJumpAddrs[i];
            if (VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                p[0] = 0x90; p[1] = 0x90; p[2] = 0x90;
                p[3] = 0x90; p[4] = 0x90; p[5] = 0x90;
                VirtualProtect(p, 6, oldProtect, &oldProtect);
                nopCount++;
            }
        }
        log_str("  NOPed cull jumps: ");
        log_int("", nopCount);
        log_str("/7\r\n");
    }

    /* Disable frustum culling entirely: patch the visibility function at 0x407150
     * to return immediately. This 4KB function tests objects against the camera
     * frustum and marks invisible objects for skipping. With RTX Remix, all
     * geometry must be submitted (rays come from any direction). The function
     * is cdecl with stack cleanup by caller, so a bare 'ret' is safe.
     * Original bytes: 55 8B EC 83 (push ebp; mov ebp, esp; and esp, ...)
     * Patched bytes:  C3 (ret) */
    {
        unsigned char *pFrustumFunc = (unsigned char *)0x00407150;
        if (VirtualProtect(pFrustumFunc, 1, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            pFrustumFunc[0] = 0xC3; /* ret */
            VirtualProtect(pFrustumFunc, 1, oldProtect, &oldProtect);
            log_str("  Patched frustum cull function to ret (0x407150)\r\n");
        }
    }

    /* Force all sectors visible in RenderVisibleSectors (0x46C180).
     * The game divides the level into sectors connected by portals. Only sectors
     * reachable from the camera's current sector via portal traversal get their
     * visibility flag set. Sectors without the flag are skipped entirely — all
     * their geometry vanishes, taking Remix hash anchors with it.
     *
     * Two conditional jumps gate sector rendering:
     *   0x46C194: JE  (0F 84 ...) — skip if sector byte[+0] != 0 (disabled)
     *   0x46C19D: JNE (0F 85 ...) — skip if sector byte[+1] bit 3 not set (not visible)
     * NOP both to force all 8 sector entries to render every frame. */
    {
        unsigned char *p;
        int sectorNops = 0;

        p = (unsigned char *)0x0046C194;
        if (VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0] = 0x90; p[1] = 0x90; p[2] = 0x90;
            p[3] = 0x90; p[4] = 0x90; p[5] = 0x90;
            VirtualProtect(p, 6, oldProtect, &oldProtect);
            sectorNops++;
        }
        p = (unsigned char *)0x0046C19D;
        if (VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0] = 0x90; p[1] = 0x90; p[2] = 0x90;
            p[3] = 0x90; p[4] = 0x90; p[5] = 0x90;
            VirtualProtect(p, 6, oldProtect, &oldProtect);
            sectorNops++;
        }
        log_str("  NOPed sector visibility checks: ");
        log_int("", sectorNops);
        log_str("/2\r\n");
    }

    /* Stamp cull mode globals to D3DCULL_NONE. The renderer caches these and
     * only calls SetRenderState on transitions — if the cached value already
     * matches the desired cull mode, the call never reaches our proxy hook. */
    if (VirtualProtect((void*)TRL_CULL_MODE_PASS1_ADDR, 12, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        *(unsigned int*)TRL_CULL_MODE_PASS1_ADDR    = 1; /* D3DCULL_NONE */
        *(unsigned int*)TRL_CULL_MODE_PASS2_ADDR    = 1;
        *(unsigned int*)TRL_CULL_MODE_PASS2_INV_ADDR = 1;
        VirtualProtect((void*)TRL_CULL_MODE_PASS1_ADDR, 12, oldProtect, &oldProtect);
        log_str("  Patched cull mode globals to D3DCULL_NONE\r\n");
    }

    /* NOP light frustum rejection: the 6-byte JNP at 0x0060CE20 in
     * RenderLights_FrustumCull skips lights that fail the 6-plane frustum test.
     * RTX Remix needs all lights submitted (rays come from any direction). */
    {
        unsigned char *p = (unsigned char *)TRL_LIGHT_FRUSTUM_REJECT_ADDR;
        if (VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0] = 0x90; p[1] = 0x90; p[2] = 0x90;
            p[3] = 0x90; p[4] = 0x90; p[5] = 0x90;
            VirtualProtect(p, 6, oldProtect, &oldProtect);
            log_str("  NOPed light frustum rejection at 0x0060CE20\r\n");
        }
    }

}

/* ---- Build vtable ---- */

WrappedDevice* WrappedDevice_Create(void *pRealDevice) {
    WrappedDevice *w;

    w = (WrappedDevice*)HeapAlloc(GetProcessHeap(), 8 /*HEAP_ZERO_MEMORY*/, sizeof(WrappedDevice));
    if (!w) return NULL;

    s_device_vtbl[0]  = (void*)WD_QueryInterface;
    s_device_vtbl[1]  = (void*)WD_AddRef;
    s_device_vtbl[2]  = (void*)WD_Release;
    s_device_vtbl[3]  = (void*)Relay_03;
    s_device_vtbl[4]  = (void*)Relay_04;
    s_device_vtbl[5]  = (void*)Relay_05;
    s_device_vtbl[6]  = (void*)Relay_06;
    s_device_vtbl[7]  = (void*)Relay_07;
    s_device_vtbl[8]  = (void*)Relay_08;
    s_device_vtbl[9]  = (void*)Relay_09;
    s_device_vtbl[10] = (void*)Relay_10;
    s_device_vtbl[11] = (void*)Relay_11;
    s_device_vtbl[12] = (void*)Relay_12;
    s_device_vtbl[13] = (void*)Relay_13;
    s_device_vtbl[14] = (void*)Relay_14;
    s_device_vtbl[15] = (void*)Relay_15;
    s_device_vtbl[16] = (void*)WD_Reset;             /* INTERCEPTED */
    s_device_vtbl[17] = (void*)WD_Present;           /* INTERCEPTED */
    s_device_vtbl[18] = (void*)Relay_18;
    s_device_vtbl[19] = (void*)Relay_19;
    s_device_vtbl[20] = (void*)Relay_20;
    s_device_vtbl[21] = (void*)Relay_21;
    s_device_vtbl[22] = (void*)Relay_22;
    s_device_vtbl[23] = (void*)Relay_23;
    s_device_vtbl[24] = (void*)Relay_24;
    s_device_vtbl[25] = (void*)Relay_25;
    s_device_vtbl[26] = (void*)Relay_26;
    s_device_vtbl[27] = (void*)Relay_27;
    s_device_vtbl[28] = (void*)Relay_28;
    s_device_vtbl[29] = (void*)Relay_29;
    s_device_vtbl[30] = (void*)Relay_30;
    s_device_vtbl[31] = (void*)Relay_31;
    s_device_vtbl[32] = (void*)Relay_32;
    s_device_vtbl[33] = (void*)Relay_33;
    s_device_vtbl[34] = (void*)Relay_34;
    s_device_vtbl[35] = (void*)Relay_35;
    s_device_vtbl[36] = (void*)Relay_36;
    s_device_vtbl[37] = (void*)Relay_37;
    s_device_vtbl[38] = (void*)Relay_38;
    s_device_vtbl[39] = (void*)Relay_39;
    s_device_vtbl[40] = (void*)Relay_40;
    s_device_vtbl[41] = (void*)WD_BeginScene;        /* INTERCEPTED */
    s_device_vtbl[42] = (void*)WD_EndScene;           /* INTERCEPTED */
    s_device_vtbl[43] = (void*)Relay_43;
    s_device_vtbl[44] = (void*)WD_SetTransform;       /* INTERCEPTED */
    s_device_vtbl[45] = (void*)Relay_45;
    s_device_vtbl[46] = (void*)Relay_46;
    s_device_vtbl[47] = (void*)Relay_47;
    s_device_vtbl[48] = (void*)Relay_48;
    s_device_vtbl[49] = (void*)Relay_49;
    s_device_vtbl[50] = (void*)Relay_50;
    s_device_vtbl[51] = (void*)Relay_51;
    s_device_vtbl[52] = (void*)Relay_52;
    s_device_vtbl[53] = (void*)Relay_53;
    s_device_vtbl[54] = (void*)Relay_54;
    s_device_vtbl[55] = (void*)Relay_55;
    s_device_vtbl[56] = (void*)Relay_56;
    s_device_vtbl[57] = (void*)WD_SetRenderState;     /* INTERCEPTED */
    s_device_vtbl[58] = (void*)Relay_58;
    s_device_vtbl[59] = (void*)Relay_59;
    s_device_vtbl[60] = (void*)Relay_60;
    s_device_vtbl[61] = (void*)Relay_61;
    s_device_vtbl[62] = (void*)Relay_62;
    s_device_vtbl[63] = (void*)Relay_63;
    s_device_vtbl[64] = (void*)Relay_64;
    s_device_vtbl[65] = (void*)WD_SetTexture;        /* INTERCEPTED */
    s_device_vtbl[66] = (void*)Relay_66;
    s_device_vtbl[67] = (void*)Relay_67;
    s_device_vtbl[68] = (void*)Relay_68;
    s_device_vtbl[69] = (void*)Relay_69;
    s_device_vtbl[70] = (void*)Relay_70;
    s_device_vtbl[71] = (void*)Relay_71;
    s_device_vtbl[72] = (void*)Relay_72;
    s_device_vtbl[73] = (void*)Relay_73;
    s_device_vtbl[74] = (void*)Relay_74;
    s_device_vtbl[75] = (void*)Relay_75;
    s_device_vtbl[76] = (void*)Relay_76;
    s_device_vtbl[77] = (void*)Relay_77;
    s_device_vtbl[78] = (void*)Relay_78;
    s_device_vtbl[79] = (void*)Relay_79;
    s_device_vtbl[80] = (void*)Relay_80;
    s_device_vtbl[81] = (void*)WD_DrawPrimitive;     /* INTERCEPTED */
    s_device_vtbl[82] = (void*)WD_DrawIndexedPrimitive; /* INTERCEPTED */
    s_device_vtbl[83] = (void*)WD_DrawPrimitiveUP;     /* INTERCEPTED */
    s_device_vtbl[84] = (void*)WD_DrawIndexedPrimitiveUP; /* INTERCEPTED */
    s_device_vtbl[85] = (void*)Relay_85;
    s_device_vtbl[86] = (void*)Relay_86;
    s_device_vtbl[87] = (void*)WD_SetVertexDeclaration; /* INTERCEPTED */
    s_device_vtbl[88] = (void*)Relay_88;
    s_device_vtbl[89] = (void*)WD_SetFVF;          /* INTERCEPTED */
    s_device_vtbl[90] = (void*)Relay_90;
    s_device_vtbl[91] = (void*)Relay_91;
    s_device_vtbl[92] = (void*)WD_SetVertexShader;   /* INTERCEPTED */
    s_device_vtbl[93] = (void*)Relay_93;
    s_device_vtbl[94] = (void*)WD_SetVertexShaderConstantF; /* INTERCEPTED */
    s_device_vtbl[95] = (void*)Relay_95;
    s_device_vtbl[96] = (void*)Relay_96;
    s_device_vtbl[97] = (void*)Relay_97;
    s_device_vtbl[98] = (void*)Relay_98;
    s_device_vtbl[99] = (void*)Relay_99;
    s_device_vtbl[100] = (void*)WD_SetStreamSource;  /* INTERCEPTED */
    s_device_vtbl[101] = (void*)Relay_101;
    s_device_vtbl[102] = (void*)Relay_102;
    s_device_vtbl[103] = (void*)Relay_103;
    s_device_vtbl[104] = (void*)Relay_104;
    s_device_vtbl[105] = (void*)Relay_105;
    s_device_vtbl[106] = (void*)Relay_106;
    s_device_vtbl[107] = (void*)WD_SetPixelShader;   /* INTERCEPTED */
    s_device_vtbl[108] = (void*)Relay_108;
    s_device_vtbl[109] = (void*)WD_SetPixelShaderConstantF; /* INTERCEPTED */
    s_device_vtbl[110] = (void*)Relay_110;
    s_device_vtbl[111] = (void*)Relay_111;
    s_device_vtbl[112] = (void*)Relay_112;
    s_device_vtbl[113] = (void*)Relay_113;
    s_device_vtbl[114] = (void*)Relay_114;
    s_device_vtbl[115] = (void*)Relay_115;
    s_device_vtbl[116] = (void*)Relay_116;
    s_device_vtbl[117] = (void*)Relay_117;
    s_device_vtbl[118] = (void*)Relay_118;

    w->vtbl = s_device_vtbl;
    w->pReal = pRealDevice;
    w->refCount = 1;
    w->frameCount = 0;
    w->ffpSetup = 0;
    w->worldDirty = 0;
    w->viewProjDirty = 0;
    w->psConstDirty = 0;
    w->lastVS = NULL;
    w->lastPS = NULL;
    w->viewProjValid = 0;
    w->lastDecl = NULL;
    w->curDeclIsSkinned = 0;
    w->curDeclHasTexcoord = 0;
    w->curDeclHasNormal = 0;
    w->curDeclHasColor = 0;
    w->curDeclColorOff = -1;
    w->curDeclHasPosT = 0;
    w->curDeclHasMorph = 0;
    w->curDeclTexcoordType = -1;
    w->curDeclTexcoordOff = 0;
    w->vpInverseValid = 0;
    w->proj3DCached = 0;
    w->transformOverrideActive = 0;
    w->memoryPatchesApplied = 0;
    w->diagMemLogged = 0;
    { int i; for (i = 0; i < 16; i++) { w->cachedVPInverse[i] = 0; w->lastVP[i] = 0; } }
#if ENABLE_SKINNING
    Skin_InitDevice(w, pRealDevice);
#endif
    { int s; for (s = 0; s < 4; s++) { w->streamVB[s] = NULL; w->streamOffset[s] = 0; w->streamStride[s] = 0; } }
    { int t; for (t = 0; t < 8; t++) w->curTexture[t] = NULL; }
    w->loggedDeclCount = 0;
    w->strippedDeclCount = 0;
    { int ts; for (ts = 0; ts < 8; ts++) w->diagTexUniq[ts] = 0; }
    w->createTick = GetTickCount();
    w->diagLoggedFrames = 0;

    /* Read AlbedoStage from INI */
    {
        char iniBuf[260];
        extern HINSTANCE g_hInstance;
        int i, lastSlash = -1;
        GetModuleFileNameA(g_hInstance, iniBuf, 260);
        for (i = 0; iniBuf[i]; i++) {
            if (iniBuf[i] == '\\' || iniBuf[i] == '/') lastSlash = i;
        }
        if (lastSlash >= 0) {
            const char *fn = "proxy.ini";
            int p = lastSlash + 1, j;
            for (j = 0; fn[j]; j++) iniBuf[p++] = fn[j];
            iniBuf[p] = '\0';
        }
        w->albedoStage = GetPrivateProfileIntA("FFP", "AlbedoStage", 0, iniBuf);
        if (w->albedoStage < 0 || w->albedoStage > 7) w->albedoStage = 0;
    }

    log_str("WrappedDevice created with shader passthrough + transform override\r\n");
    log_int("  Diag delay (ms): ", DIAG_DELAY_MS);
    log_int("  AlbedoStage: ", w->albedoStage);
    log_hex("  Real device: ", (unsigned int)pRealDevice);
    log_hex("  View matrix addr: ", TRL_VIEW_MATRIX_ADDR);
    log_hex("  Proj matrix addr: ", TRL_PROJ_MATRIX_ADDR);

    /* Apply one-time game memory patches for culling/frustum */
    TRL_ApplyMemoryPatches(w);

    return w;
}
