/*
 * Wrapped IDirect3DDevice9 — FFP conversion layer for RTX Remix.
 *
 * Intercepts ~15 of 119 device methods; the rest relay via naked ASM thunks.
 * Sections marked GAME-SPECIFIC need per-game updates.
 * See the dx9-ffp-port prompt and extensions/skinning/README.md for full docs.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* No-CRT memcpy: #pragma function allows defining the fallback that the linker
 * uses for variable-size copies. #pragma intrinsic immediately after restores
 * the compiler intrinsic (rep movsd / SSE) for all known-size copies in the
 * rest of the file — best of both worlds. */
#pragma function(memcpy)
void * __cdecl memcpy(void *dst, const void *src, unsigned int n) {
    unsigned int *d32 = (unsigned int *)dst;
    const unsigned int *s32 = (const unsigned int *)src;
    unsigned int dwords = n >> 2;
    unsigned int rem = n & 3;
    while (dwords--) *d32++ = *s32++;
    { unsigned char *d8 = (unsigned char*)d32; const unsigned char *s8 = (const unsigned char*)s32;
      while (rem--) *d8++ = *s8++; }
    return dst;
}
#pragma intrinsic(memcpy)

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
 * TRL uses SHORT4 vertex positions. The proxy expands SHORT4 → FLOAT3 on the
 * CPU before each draw, nulls the vertex shader, and draws in FFP mode. This
 * eliminates rtx.useVertexCapture and gives stable position hashes.
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
#define VS_REG_TEX0_SCROLL     17   /* Per-draw UV scroll offset */
#define VS_REG_TEX0_XFORM_START 24  /* Per-draw UV transform matrix */
#define VS_REG_TEX0_XFORM_END   28

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

/* Light_VisibilityTest: per-light pre-frustum gate at 0x60B050. For light types
 * 0 and 1, performs distance/sphere/cone checks and rejects lights that are
 * "too far." Patch to always return TRUE so all lights pass. */
#define TRL_LIGHT_VISIBILITY_TEST_ADDR 0x0060B050

/* Engine config flag (no effect, kept for reference). */
#define TRL_LIGHT_CULLING_DISABLE_FLAG 0x01075BE0

/* Sector light count gate: the JZ at 0xEC6337 skips loading the sector's
 * static light count when a visibility flag (param_2[10]) is zero.
 * NOP it to force ALL sectors to load their light count from static data,
 * ensuring stage lights are submitted regardless of Lara's position. */
#define TRL_SECTOR_LIGHT_GATE_ADDR 0x00EC6337

/* Prevent per-frame clearing of the sector light count (+0x1B0). The cleanup
 * function at 0x603AD0 zeroes this field for all sectors at end of frame.
 * NOP the MOV instruction so the light count persists across frames, ensuring
 * sectors that had lights on any previous frame retain them. */
#define TRL_LIGHT_COUNT_CLEAR_ADDR 0x00603AE6

/* RenderLights gate: the JE at 0x60E3B1 checks the stack flag [esp+0x17]
 * which is set from the per-sector light count [ebx+0x1B0]. When a sector
 * has 0 lights, this jump skips the entire RenderLights_FrustumCull call.
 * NOP it to force light rendering regardless of sector light count. */
#define TRL_RENDER_LIGHTS_GATE_ADDR 0x0060E3B1

/* Render flags global: bit 20 (0x100000) skips the entire post-sector object
 * rendering loop at 0x40E2C0. Clear this bit per-scene to ensure the loop runs. */
#define TRL_RENDER_FLAGS_ADDR 0x010E5384

/* SectorPortalVisibility (0x46D1D0) bounds reset: every frame, this function
 * initializes all 8 sector bounding rects to (x=512, y=448, w=-512, h=-448).
 * Only sectors reachable through portal traversal get overwritten with positive
 * values. Unreachable sectors fail the screen-size check and never render.
 * This is the ROOT CAUSE of camera-angle-dependent geometry culling.
 *
 * Fix: patch the register loads AND loop body to write fullscreen bounds
 * matching the camera sector: (x=0, y=0, w=512, h=448).
 *   0x46D1E5: mov edi, 0xFFFFFE00 → mov edi, 0x200    (w = +512)
 *   0x46D1EA: mov esi, 0xFFFFFE40 → mov esi, 0x1C0    (h = +448)
 *   0x46D1F1: mov [eax-2], dx     → mov [eax-2], bp   (x = 0, not 512)
 *   0x46D1F5: mov [eax], 0x1C0    → mov [eax], bp     (y = 0, not 448)
 * ebp is 0 (set at 0x46D1EF), dx=0x200 preserved for camera sector setup. */
#define TRL_SECTOR_BOUNDS_W_ADDR     0x0046D1E5  /* mov edi, -512 → mov edi, +512 */
#define TRL_SECTOR_BOUNDS_H_ADDR     0x0046D1EA  /* mov esi, -448 → mov esi, +448 */
#define TRL_SECTOR_BOUNDS_X_LOOP     0x0046D1F1  /* mov [eax-2], dx → mov [eax-2], bp */
#define TRL_SECTOR_BOUNDS_Y_LOOP     0x0046D1F5  /* mov [eax], 0x1C0 → mov [eax], bp */

/* Terrain draw function (0x40AE20, __thiscall): 6-byte JNE at 0x40AE3E skips
 * all terrain rendering when flag 0x20000 is set in the terrain drawable's
 * render flags [esi+0x1C]. This is the primary terrain culling gate — NOP it
 * so terrain geometry is always submitted to the render pipeline. */
#define TRL_TERRAIN_CULL_GATE_ADDR 0x0040AE3E

/* MeshSubmit_VisibilityGate (0x454AB0): per-mesh visibility check called at
 * MeshSubmit (0x458630) entry. Returns 1 to cull, 0 to render. Walks a linked
 * list and calls two external visibility tests. Patch to always return 0 so
 * all meshes pass visibility. */
#define TRL_MESH_VISIBILITY_GATE_ADDR 0x00454AB0

/* Post-sector rendering loop (0x40E2C0) cull gates:
 * This loop processes objects in the scene graph after sector rendering.
 * Has its own enable flag and per-object distance/flag culling. */
#define TRL_POSTSECTOR_ENABLE_ADDR    0x00F12016  /* byte: must be nonzero for loop to run */
#define TRL_POSTSECTOR_GATE_ADDR      0x010024E8  /* dword: must be zero for loop to run */
#define TRL_POSTSECTOR_SECTOR_BITS_ADDR 0x00FFA718 /* per-sector enable bitmask */
#define TRL_POSTSECTOR_ALPHA_CULL_ADDR  0x0040E33A /* 2-byte JE: skip if alpha=0xFF */
#define TRL_POSTSECTOR_FLAG1_CULL_ADDR  0x0040E349 /* 6-byte JNE: skip if flag 0x800 */
#define TRL_POSTSECTOR_FLAG2_CULL_ADDR  0x0040E359 /* 6-byte JNE: skip if flag 0x10000 */
#define TRL_POSTSECTOR_DIST_CULL_ADDR   0x0040E3B0 /* 2-byte JNE: distance/LOD fade cull */
#define TRL_POSTSECTOR_BITMASK_CULL_ADDR 0x0040E30F /* 6-byte JE: sector bitmask check */

/* Light-has-data check (0x6037D0): iterates light volume entries, returns 0
 * if ALL entries have [+0x94]+[+0x84]==0, which gates the entire light render
 * path at 0x60A53C. Patch to always return 1 so the light volume rendering
 * system is always invoked (the caller at 0x60A62C calls RenderLights_Caller). */
#define TRL_LIGHT_HAS_DATA_CHECK_ADDR 0x006037D0

/* Sector_IterateMeshArray (0x46C320) mesh cull flag check: the JNE at 0x46C33E
 * skips meshes with [mesh+0x5C] & 0x82000000 set. The 0x80000000 bit is
 * prevented by patched MeshSubmit_VisibilityGate, but 0x02000000 may be set
 * by a different code path, silently culling meshes. NOP to force all meshes
 * through to MeshSubmit. */
#define TRL_MESH_CULL_FLAG_JNE_ADDR 0x0046C33E

/* Layer 3 frustum culler: RenderQueue_FrustumCull (0x40C430) performs
 * recursive bounding-volume frustum tests AFTER sector/mesh submission.
 * Objects outside the camera frustum are culled here even when Layers 1+2
 * are fully patched. Redirect to RenderQueue_DirectDispatch (0x40C390)
 * which has the same signature but skips all frustum math. */
#define TRL_FRUSTUM_CULLER_ADDR      0x0040C430
#define TRL_DIRECT_DISPATCH_ADDR     0x0040C390

/* _level writers: two instructions set the far-clip global at 0x10FC910
 * each frame, overwriting our BeginScene stamp. NOP both so 1e30 persists. */
#define TRL_LEVEL_WRITER_1_ADDR      0x0046CCB4  /* scene setup: mov [0x10FC910], ecx */
#define TRL_LEVEL_WRITER_2_ADDR      0x004E6DFA  /* camera setup: mov [0x10FC910], ecx */

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

/* ---- Draw call cache (anti-culling) ----
 * Records every DIP call's full state. In EndScene, replays any draw from
 * the previous frame that wasn't submitted this frame. This keeps culled
 * geometry alive for Remix hash anchors. */
#define DRAW_CACHE_ENABLED 1
#define DRAW_CACHE_MAX 4096

/* FLOAT3 routing modes:
 * auto    = use shader route when rtx.useVertexCapture is enabled, else null VS
 * null_vs = always force null VS + FFP state for FLOAT3 draws
 * shader  = always keep shader path for FLOAT3 draws */
#define FLOAT3_ROUTE_AUTO     0
#define FLOAT3_ROUTE_NULL_VS  1
#define FLOAT3_ROUTE_SHADER   2

/* Sky isolation: duplicate candidate sky textures with a one-bit mutation so
 * sky tagging in Remix can target sky-only hashes without touching world draws. */
#define SKY_ISO_MAX 256
#define SKY_ISO_AUTO_MAX_TEXTURES 4
#define SKY_ISO_AUTO_MIN_SCENES 8
#define SKY_ISO_AUTO_MIN_SCORE_PCT 35
#define SKY_ISO_CONTAM_ANIMATED      0x0001u
#define SKY_ISO_CONTAM_NON_ID_VIEW   0x0002u
#define SKY_ISO_CONTAM_NON_ID_WORLD  0x0004u
#define SKY_ISO_CONTAM_SHORT4        0x0008u
#define SKY_ISO_CONTAM_DEFORMED      0x0010u
#define SKY_ISO_CONTAM_AUX_STREAM    0x0020u
#define SKY_ISO_CONTAM_NON_FLOAT3    0x0040u
#define SKY_ISO_CONTAM_NON_CANDIDATE 0x0080u

typedef struct SkyIsoEntry {
    void *origTex;       /* source stage texture pointer */
    void *isoTex;        /* cloned + bit-mutated texture (owned, Release on reset) */
    int candidate;       /* promoted after warm-up screening */
    int cloneAttempted;  /* avoid expensive repeated clone attempts */
    int cloneReady;      /* isoTex is valid */
    unsigned int candidateDraws;
    unsigned int candidateScenes;
    unsigned int candidateLastScene;
    unsigned int candidateMaxVerts;
    unsigned int candidateMaxPrims;
    unsigned int candidateScore;
    unsigned int contaminatedDraws;
    unsigned int contaminatedScenes;
    unsigned int contaminatedLastScene;
    unsigned int contaminatedScore;
    unsigned int contaminationFlags;
} SkyIsoEntry;

typedef struct {
    /* Fingerprint — identifies "same" draw across frames */
    void *vb;               /* vertex buffer */
    void *ib;               /* index buffer */
    unsigned int si;        /* start index */
    unsigned int pc;        /* prim count */
    unsigned int nv;        /* num vertices */
    /* DIP params */
    unsigned int pt;        /* primitive type */
    int bvi;                /* base vertex index */
    unsigned int mi;        /* min index */
    /* Saved state for replay */
    void *decl;             /* vertex declaration */
    void *tex0;             /* texture stage 0 */
    unsigned int streamOff; /* stream 0 offset */
    unsigned int streamStr; /* stream 0 stride */
    float world[16];        /* computed world matrix */
    int isViewSpace;        /* 1 if live draw used view-space path (View=Identity) */
    /* SHORT4 expansion state for replay */
    int isShort4;           /* 1 if original draw had SHORT4 position */
    int posOff;             /* position offset in source vertex */
    /* Frame tracking */
    unsigned int lastSeenFrame;
    int active;             /* 1 = slot in use */
} CachedDraw;

/* ---- Diagnostic logging ---- */
#define DIAG_LOG_FRAMES 3
#define DIAG_DELAY_MS 50000   /* 50 seconds after device creation */
#define DIAG_ENABLED 0        /* perf build: strip per-draw GetTickCount + log paths */

#define DIAG_ACTIVE(self) \
    (DIAG_ENABLED && (self)->diagLoggedFrames < DIAG_LOG_FRAMES && \
     GetTickCount() - (self)->createTick >= DIAG_DELAY_MS)

/* ---- Performance logging (independent of DIAG, survives DIAG=0) ---- */
#define PERF_LOG_ENABLED 1     /* set to 0 for the final ship build once tuning done */
#define PERF_LOG_INTERVAL 600  /* frames between FPS samples (~10s @60fps, ~5s @120fps) */

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

#define D3DTTFF_DISABLE    0
#define D3DTTFF_COUNT2     2

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
#define D3DDECLTYPE_SHORT2    6
#define D3DDECLTYPE_SHORT4    7
#define D3DDECLTYPE_SHORT2N   9
#define D3DDECLTYPE_SHORT4N   10
#define D3DDECLTYPE_UDEC3     13
#define D3DDECLTYPE_DEC3N     14
#define D3DDECLTYPE_FLOAT16_2 15

/* FVF flags — for stripping normals from FVF-based draws */
#define D3DFVF_NORMAL 0x010

/* D3D lock/usage/pool flags for vertex buffer creation */
#define D3DUSAGE_DYNAMIC      0x200
#define D3DUSAGE_WRITEONLY    0x08
#define D3DPOOL_DEFAULT       0
#define D3DPOOL_MANAGED       1
#define D3DLOCK_READONLY      0x10
#define D3DLOCK_DISCARD       0x2000
#define D3DRTYPE_TEXTURE      3
#define D3DFMT_DXT1           0x31545844u
#define D3DFMT_DXT2           0x32545844u
#define D3DFMT_DXT3           0x33545844u
#define D3DFMT_DXT4           0x34545844u
#define D3DFMT_DXT5           0x35545844u

typedef struct {
    int Pitch;
    void *pBits;
} D3DLOCKED_RECT;

typedef struct {
    unsigned int Format;
    unsigned int Type;
    unsigned int Usage;
    unsigned int Pool;
    unsigned int MultiSampleType;
    unsigned int MultiSampleQuality;
    unsigned int Width;
    unsigned int Height;
} D3DSURFACE_DESC;

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

/* ---- Draw Call Replay Cache ----
 *
 * Captures unique draw call fingerprints during the first N frames (while all
 * geometry is visible near the stage). On subsequent frames, any fingerprint
 * NOT resubmitted by the game is replayed by the proxy, keeping Remix's
 * anti-culling alive for anchor meshes that the game culls at distance.
 *
 * Fingerprint = VB pointer + IB pointer + texture[0] pointer.
 * Stored state = everything needed to replay: VS constants, declaration,
 * shaders, stream source, textures, primitive params.
 */
#define PINNED_DRAW_MAX     512     /* max unique draws to cache */
#define PINNED_CAPTURE_FRAMES 120   /* capture during first 120 frames (~2 sec) */
#define PINNED_REPLAY_INTERVAL 600  /* replay missing draws every 600 frames; engine culling is fully disabled so replay rarely has work */

typedef struct PinnedDraw {
    /* Fingerprint for dedup */
    void *fpVB;             /* vertex buffer pointer */
    void *fpIB;             /* index buffer pointer */
    void *fpTex0;           /* texture stage 0 */

    /* Draw call parameters */
    unsigned int primType;
    int baseVertexIndex;
    unsigned int minVertexIndex;
    unsigned int numVertices;
    unsigned int startIndex;
    unsigned int primCount;

    /* State needed for replay */
    void *vertexShader;     /* AddRef'd */
    void *pixelShader;      /* AddRef'd */
    void *vertexDecl;       /* AddRef'd */
    void *textures[4];      /* stages 0-3, AddRef'd */
    void *vertexBuffer;     /* stream 0 VB, AddRef'd */
    unsigned int vbOffset;
    unsigned int vbStride;
    void *indexBuffer;      /* AddRef'd */

    /* Transform state: cached WVP (c0-c3) for this draw */
    float wvpConst[16];     /* VS constants c0-c3 at capture time */
    int isViewSpace;        /* 1 if live draw used view-space path (View=Identity) */
    int isShort4;           /* 1 if original draw had SHORT4 position */
    int posOff;             /* POSITION byte offset for SHORT4 expansion */

    int active;             /* 1 = slot in use */
    int submittedThisFrame; /* reset each frame, set when game submits matching draw */
} PinnedDraw;

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
    float tex0ScrollConst[4];     /* c17: UV scroll offset */
    float tex0TransformConst[16]; /* c24-c27: UV animation matrix */
    void *tex0TransformVS;        /* shader that wrote the current UV animation state */
    int tex0ScrollValid;
    int tex0TransformValid;

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
    int curDeclUsesAuxStreams; /* 1 if the declaration references stream 1+ */

    /* Texcoord format for diagnostics and skinned vertex expansion */
    int curDeclTexcoordType; /* D3DDECLTYPE of TEXCOORD[0], or -1 if none */
    int curDeclTexcoordOff;  /* byte offset of TEXCOORD[0] in vertex */

    /* Texture tracking (stages 0-7) */
    void *curTexture[8];
    int albedoStage;
    int useVertexCaptureEffective; /* effective rtx.useVertexCapture (user.conf > rtx.conf) */
    int float3RoutingMode;         /* FLOAT3_ROUTE_* */
    int short4AnimatedRoutingMode; /* FLOAT3_ROUTE_* applied to animated/deformed SHORT4 draws */

    /* Sky isolation runtime config/state */
    int skyIsolationEnable;
    unsigned int skyCandidateMinVerts;
    unsigned int skyCandidateMinPrims;
    unsigned int skyWarmupScenes;
    unsigned int skyWarmupStartScene;
    int skyWarmupStarted;
    SkyIsoEntry skyIso[SKY_ISO_MAX];
    int skyIsoCount;
    void *skyIsoLastOrigTex;        /* fast-path for consecutive same-texture draws */
    SkyIsoEntry *skyIsoLastEntry;   /* cached match for skyIsoLastOrigTex */
    unsigned int skyIsoRefreshScene;/* last scene that recomputed candidate ranking */
    int skyIsoRefreshDirty;         /* observations changed since last ranking pass */

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
    int diagSkinWorldLogged;    /* one-shot flag for skinned World matrix log */

    /* Diagnostic state */
    void *loggedDecls[32];
    int loggedDeclCount;
    void *diagTexSeen[8][32];
    int diagTexUniq[8];
    unsigned int createTick;
    unsigned int diagLoggedFrames;
    /* PERF_LOG: independent of DIAG, used to emit periodic FPS samples to ffp_proxy.log */
    unsigned int perfLastTick;
    unsigned int perfLastFrame;
    unsigned int drawCallCount;
    unsigned int sceneCount;
    unsigned int skySceneCounter;   /* global BeginScene count (not reset per frame) */
    unsigned int lastSceneDrawsProcessed;
    unsigned int postLatchSceneLogsRemaining;
    int sawMenuScene;               /* front-end loop observed */
    int sawTransitionBlip;          /* tiny handoff scene observed */
    int levelSceneDetected;         /* first real in-level scene detected */
    int replayFeaturesEnabled;      /* draw replay/capture path gate */
    int vsConstWriteLog[256];

    /* Per-frame draw routing counters (logged at Present without delay) */
    unsigned int drawsProcessed;    /* went through TRL_PrepDraw */
    unsigned int drawsSkippedQuad;  /* caught by TRL_IsScreenSpaceQuad */
    unsigned int drawsPassthrough;  /* viewProjValid=0 or POSITIONT */
    unsigned int drawsTotal;        /* all DIP+DP calls this frame */
    unsigned int transformsBlocked; /* external SetTransform V/P/W blocked */
    unsigned int drawsS4;           /* SHORT4 draws (expanded rigid path or shader-routed dynamic path) */
    unsigned int drawsF3;           /* FLOAT3/other draws */
    unsigned int frameSummaryCount; /* how many frame summaries logged */

    /* Stripped-normal declaration cache: maps original decl → decl with NORMAL removed.
     * Remix's game capturer asserts normals are FLOAT3; TRL uses SHORT4N/DEC3N.
     * Stripping NORMAL from the declaration prevents the assertion while Remix
     * computes smooth normals via path tracing. */
    void *strippedDeclOrig[64];   /* original declaration pointer (lookup key) */
    void *strippedDeclFixed[64];  /* modified declaration with NORMAL removed */
    int strippedDeclCount;

    /* Last computed transforms (saved by TRL_ApplyTransformOverrides for reuse) */
    float savedWorld[16];
    float savedView[16];
    float savedProj[16];

    /* Last *applied* transforms (cache of values pushed via SetTransform).
     * Used to skip redundant SetTransform calls when the matrix is unchanged
     * — View/Projection are typically constant within a frame, and World is
     * frequently identical across consecutive draws of the same object. */
    float appliedWorld[16];
    float appliedView[16];
    float appliedProj[16];

    /* SHORT4 → FLOAT3 position expansion (CPU-side FFP conversion).
     * Eliminates useVertexCapture dependency for hash stability. */
    int s4PosOff;                 /* byte offset of POSITION in current decl (always tracked) */
    void *s4DeclOrig[64];         /* cache: original decl → expanded decl */
    void *s4DeclExp[64];
    int s4Stride[64];             /* expanded stride per cached decl */
    int s4DeclCount;

    /* Expanded VB cache: keyed by (srcVB, srcOff, bvi, nv, fingerprint).
     * Fingerprint = XOR of first 32 bytes of source VB region to detect
     * dynamic VBs where the game reuses the pointer with new content. */
#define S4_VB_CACHE_SIZE 512
    struct {
        void *srcVB;            /* source VB pointer (lookup key) */
        unsigned int srcOff;    /* stream offset */
        int bvi;                /* base vertex index */
        unsigned int nv;        /* vertex count */
        unsigned int fingerprint; /* XOR of first 32 bytes for staleness check */
        void *expVB;            /* expanded VB (owned, must release) */
        unsigned int expStride; /* expanded stride */
    } s4VBCache[S4_VB_CACHE_SIZE];
    int s4VBCacheCount;

    /* Draw call replay cache — keeps anchor geometry alive for Remix */
    PinnedDraw pinnedDraws[PINNED_DRAW_MAX];
    int pinnedDrawCount;
    int pinnedCaptureComplete;  /* 1 after capture window closes */
} WrappedDevice;

#define REAL(self) (((WrappedDevice*)(self))->pReal)
#define REAL_VT(self) (*(void***)(REAL(self)))

static __inline void** RealVtbl(WrappedDevice *self) {
    return *(void***)(self->pReal);
}

/* Forward declarations for cross-section helpers used before definitions */
static void TRL_ApplyFFPDrawState(WrappedDevice *self, int applyTex0Transform);
static int S4_ExpandAndDraw(WrappedDevice *self,
    void *srcVB, unsigned int srcOff, unsigned int srcStride, void *origDecl, int posOff,
    unsigned int pt, int bvi, unsigned int mi, unsigned int nv, unsigned int si, unsigned int pc,
    float *world, float *view, float *proj);

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

static const float s_identity4x4[16] = {
    1,0,0,0,
    0,1,0,0,
    0,0,1,0,
    0,0,0,1
};

static int ascii_tolower(int c) {
    if (c >= 'A' && c <= 'Z')
        return c + ('a' - 'A');
    return c;
}

static int ascii_is_space(int c) {
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

static int str_ieq_a(const char *a, const char *b) {
    unsigned int i = 0;
    while (a[i] && b[i]) {
        if (ascii_tolower((unsigned char)a[i]) != ascii_tolower((unsigned char)b[i]))
            return 0;
        i++;
    }
    return (a[i] == '\0' && b[i] == '\0') ? 1 : 0;
}

static int str_ieq_span_a(const char *a, int aLen, const char *b) {
    int i;
    for (i = 0; i < aLen; i++) {
        if (b[i] == '\0')
            return 0;
        if (ascii_tolower((unsigned char)a[i]) != ascii_tolower((unsigned char)b[i]))
            return 0;
    }
    return (b[aLen] == '\0') ? 1 : 0;
}

static int parse_bool_token(const char *s, int len, int *outVal) {
    if (!s || len <= 0 || !outVal)
        return 0;
    if (len == 1) {
        if (s[0] == '1') { *outVal = 1; return 1; }
        if (s[0] == '0') { *outVal = 0; return 1; }
    }
    if (str_ieq_span_a(s, len, "true") || str_ieq_span_a(s, len, "yes") || str_ieq_span_a(s, len, "on")) {
        *outVal = 1;
        return 1;
    }
    if (str_ieq_span_a(s, len, "false") || str_ieq_span_a(s, len, "no") || str_ieq_span_a(s, len, "off")) {
        *outVal = 0;
        return 1;
    }
    return 0;
}

static void get_dll_sibling_path_local(char *out, int outSize, const char *filename) {
    int i, lastSlash = -1, p;
    extern HINSTANCE g_hInstance;
    GetModuleFileNameA(g_hInstance, out, outSize);
    for (i = 0; out[i]; i++) {
        if (out[i] == '\\' || out[i] == '/')
            lastSlash = i;
    }
    p = (lastSlash >= 0) ? (lastSlash + 1) : 0;
    for (i = 0; filename[i] && p < outSize - 1; i++)
        out[p++] = filename[i];
    out[p] = '\0';
}

static int read_conf_bool_key(const char *path, const char *key, int *outFound, int *outVal) {
    HANDLE hFile;
    DWORD fileSize, bytesRead = 0;
    char *buf;
    int found = 0;
    int value = 0;
    int pos, total;

    if (outFound) *outFound = 0;
    if (!path || !key || !outFound || !outVal)
        return 0;

    hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return 0;

    fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0xFFFFFFFFu || fileSize == 0 || fileSize > (1024u * 1024u)) {
        CloseHandle(hFile);
        return 0;
    }

    buf = (char*)HeapAlloc(GetProcessHeap(), 0, fileSize + 1);
    if (!buf) {
        CloseHandle(hFile);
        return 0;
    }

    if (!ReadFile(hFile, buf, fileSize, &bytesRead, NULL)) {
        HeapFree(GetProcessHeap(), 0, buf);
        CloseHandle(hFile);
        return 0;
    }
    CloseHandle(hFile);
    buf[bytesRead] = '\0';

    total = (int)bytesRead;
    pos = 0;
    while (pos < total) {
        int lineStart = pos;
        int lineEnd = pos;
        int contentEnd;
        int left, right, eq;

        while (lineEnd < total && buf[lineEnd] != '\n' && buf[lineEnd] != '\r')
            lineEnd++;
        pos = lineEnd;
        while (pos < total && (buf[pos] == '\n' || buf[pos] == '\r'))
            pos++;

        contentEnd = lineEnd;
        {
            int i;
            for (i = lineStart; i < lineEnd; i++) {
                if (buf[i] == '#' || buf[i] == ';') {
                    contentEnd = i;
                    break;
                }
            }
        }

        left = lineStart;
        right = contentEnd;
        while (left < right && ascii_is_space((unsigned char)buf[left])) left++;
        while (right > left && ascii_is_space((unsigned char)buf[right - 1])) right--;
        if (left >= right)
            continue;

        eq = -1;
        {
            int i;
            for (i = left; i < right; i++) {
                if (buf[i] == '=') {
                    eq = i;
                    break;
                }
            }
        }
        if (eq <= left)
            continue;

        {
            int keyL = left;
            int keyR = eq;
            int valL = eq + 1;
            int valR = right;
            int tokenR;
            int parsedVal;

            while (keyL < keyR && ascii_is_space((unsigned char)buf[keyL])) keyL++;
            while (keyR > keyL && ascii_is_space((unsigned char)buf[keyR - 1])) keyR--;
            while (valL < valR && ascii_is_space((unsigned char)buf[valL])) valL++;
            while (valR > valL && ascii_is_space((unsigned char)buf[valR - 1])) valR--;

            if (keyL >= keyR || valL >= valR)
                continue;
            if (!str_ieq_span_a(&buf[keyL], keyR - keyL, key))
                continue;

            tokenR = valL;
            while (tokenR < valR &&
                !ascii_is_space((unsigned char)buf[tokenR]) &&
                buf[tokenR] != ',')
            {
                tokenR++;
            }

            if (parse_bool_token(&buf[valL], tokenR - valL, &parsedVal)) {
                found = 1;
                value = parsedVal;
            }
        }
    }

    HeapFree(GetProcessHeap(), 0, buf);
    *outFound = found;
    *outVal = value;
    return found ? 1 : 0;
}

static int parse_float3_routing_mode(const char *modeText) {
    if (!modeText)
        return FLOAT3_ROUTE_AUTO;
    if (str_ieq_a(modeText, "auto"))
        return FLOAT3_ROUTE_AUTO;
    if (str_ieq_a(modeText, "null_vs") || str_ieq_a(modeText, "null-vs") || str_ieq_a(modeText, "nullvs"))
        return FLOAT3_ROUTE_NULL_VS;
    if (str_ieq_a(modeText, "shader"))
        return FLOAT3_ROUTE_SHADER;
    return FLOAT3_ROUTE_AUTO;
}

static int TRL_GetEffectiveFloat3Route(WrappedDevice *self) {
    if (self->float3RoutingMode == FLOAT3_ROUTE_NULL_VS)
        return FLOAT3_ROUTE_NULL_VS;
    if (self->float3RoutingMode == FLOAT3_ROUTE_SHADER)
        return FLOAT3_ROUTE_SHADER;
    return self->useVertexCaptureEffective ? FLOAT3_ROUTE_SHADER : FLOAT3_ROUTE_NULL_VS;
}

static int TRL_IsAnimatedOrDeformedShort4Draw(WrappedDevice *self, int animatedTex0) {
    if (self->curDeclPosType != D3DDECLTYPE_SHORT4)
        return 0;
    if (animatedTex0)
        return 1;
    if (self->curDeclHasMorph)
        return 1;
    if (self->curDeclIsSkinned)
        return 1;
    if (self->curDeclUsesAuxStreams)
        return 1;
    return 0;
}

static int TRL_ShouldShaderRouteAnimatedShort4Draw(WrappedDevice *self, int animatedTex0) {
    if (!TRL_IsAnimatedOrDeformedShort4Draw(self, animatedTex0))
        return 0;
    if (self->short4AnimatedRoutingMode == FLOAT3_ROUTE_NULL_VS)
        return 0;
    if (self->short4AnimatedRoutingMode == FLOAT3_ROUTE_SHADER)
        return 1;
    return self->useVertexCaptureEffective ? 1 : 0;
}

static int TRL_ReadEffectiveUseVertexCapture(void) {
    char userConf[MAX_PATH];
    char rtxConf[MAX_PATH];
    int found = 0;
    int value = 1;

    get_dll_sibling_path_local(userConf, MAX_PATH, "user.conf");
    if (read_conf_bool_key(userConf, "rtx.useVertexCapture", &found, &value) && found) {
        log_int("  useVertexCapture (user.conf): ", value);
        return value ? 1 : 0;
    }

    found = 0;
    value = 1;
    get_dll_sibling_path_local(rtxConf, MAX_PATH, "rtx.conf");
    if (read_conf_bool_key(rtxConf, "rtx.useVertexCapture", &found, &value) && found) {
        log_int("  useVertexCapture (rtx.conf): ", value);
        return value ? 1 : 0;
    }

    log_str("  useVertexCapture: default True (key missing)\r\n");
    return 1;
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

static void mat4_identity(float *dst) {
    memcpy(dst, s_identity4x4, 64);
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

static int TRL_HasGameplayCamera(void) {
    const float *gameView = (const float *)TRL_VIEW_MATRIX_ADDR;
    return !mat4_approx_equal(gameView, s_identity4x4, 1e-5f);
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

static int mat4_is_zero(const float *m) {
    int i;
    for (i = 0; i < 16; i++) {
        if (m[i] != 0.0f)
            return 0;
    }
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

static void TRL_ResetTexture0AnimationState(WrappedDevice *self) {
    int i;
    self->tex0TransformVS = NULL;
    self->tex0ScrollValid = 0;
    self->tex0TransformValid = 0;
    for (i = 0; i < 4; i++)
        self->tex0ScrollConst[i] = 0.0f;
    for (i = 0; i < 16; i++)
        self->tex0TransformConst[i] = 0.0f;
}

static int TRL_BuildTexture0Transform(WrappedDevice *self, float *outTexMatrix) {
    float texMatrix[16];
    float scrollU = 0.0f;
    float scrollV = 0.0f;
    int active = 0;

    mat4_identity(outTexMatrix);

    if (!self->tex0TransformVS || self->tex0TransformVS != self->lastVS)
        return 0;

    if (self->tex0TransformValid) {
        mat4_transpose(texMatrix, self->tex0TransformConst);
        if (!mat4_is_zero(texMatrix)) {
            memcpy(outTexMatrix, texMatrix, 64);
            if (!mat4_approx_equal(outTexMatrix, s_identity4x4, 1e-6f))
                active = 1;
        }
    }

    if (self->tex0ScrollValid) {
        scrollU = self->tex0ScrollConst[0];
        scrollV = self->tex0ScrollConst[1];
        if (scrollU > 1e-6f || scrollU < -1e-6f ||
            scrollV > 1e-6f || scrollV < -1e-6f) {
            outTexMatrix[12] += scrollU;
            outTexMatrix[13] += scrollV;
            active = 1;
        }
    }

    return active;
}

static int TRL_DrawHasAnimatedTexture0(WrappedDevice *self) {
    float texMatrix[16];
    float c6y, c6z, c6w;

    if (TRL_BuildTexture0Transform(self, texMatrix))
        return 1;

    /* animatedTex0: detect VS-driven UV scroll via c6.y/z/w.
     * TRL water never issues SetTransform and keeps stage-0
     * TEXTURETRANSFORMFLAGS at DISABLE, so the FFP tex0 matrix is
     * identity for every draw. All water UV motion is instead carried
     * by vertex shader constant c6 (observed c6.y in ~0.33..0.68 range,
     * c6.w ~0.0002, c6.x always 0). Match this signature so the draw
     * is kept on the shader route and UV scroll survives. */
    c6y = self->vsConst[6 * 4 + 1];
    c6z = self->vsConst[6 * 4 + 2];
    c6w = self->vsConst[6 * 4 + 3];
    if (c6y != 0.0f || c6z != 0.0f)
        return 1;
    if (c6w > 1e-4f || c6w < -1e-4f)
        return 1;
    return 0;
}

static int SkyIso_IsCompressedFormat(unsigned int fmt) {
    return (fmt == D3DFMT_DXT1 ||
            fmt == D3DFMT_DXT2 ||
            fmt == D3DFMT_DXT3 ||
            fmt == D3DFMT_DXT4 ||
            fmt == D3DFMT_DXT5) ? 1 : 0;
}

static unsigned int SkyIso_CopyRowsForLevel(const D3DSURFACE_DESC *desc) {
    unsigned int rows = desc->Height;
    if (SkyIso_IsCompressedFormat(desc->Format))
        rows = (rows + 3) / 4;
    if (rows == 0)
        rows = 1;
    return rows;
}

static void SkyIso_ReleaseTexture(void *pTexture) {
    if (pTexture) {
        typedef unsigned long (__stdcall *FN_Rel)(void*);
        ((FN_Rel)(*(void***)pTexture)[2])(pTexture);
    }
}

static void SkyIso_Clear(WrappedDevice *self) {
    int i;
    for (i = 0; i < self->skyIsoCount; i++) {
        SkyIsoEntry *entry = &self->skyIso[i];
        if (entry->isoTex)
            SkyIso_ReleaseTexture(entry->isoTex);
        entry->origTex = NULL;
        entry->isoTex = NULL;
        entry->candidate = 0;
        entry->cloneAttempted = 0;
        entry->cloneReady = 0;
        entry->candidateDraws = 0;
        entry->candidateScenes = 0;
        entry->candidateLastScene = 0;
        entry->candidateMaxVerts = 0;
        entry->candidateMaxPrims = 0;
        entry->candidateScore = 0;
        entry->contaminatedDraws = 0;
        entry->contaminatedScenes = 0;
        entry->contaminatedLastScene = 0;
        entry->contaminatedScore = 0;
        entry->contaminationFlags = 0;
    }
    self->skyIsoCount = 0;
    self->skyWarmupStarted = 0;
    self->skyWarmupStartScene = 0;
    self->skyIsoLastOrigTex = NULL;
    self->skyIsoLastEntry = NULL;
    self->skyIsoRefreshScene = 0;
    self->skyIsoRefreshDirty = 0;
}

static SkyIsoEntry *SkyIso_FindOrCreateEntry(WrappedDevice *self, void *origTex, int createIfMissing) {
    int i;

    if (self->skyIsoLastOrigTex == origTex &&
        self->skyIsoLastEntry &&
        self->skyIsoLastEntry->origTex == origTex) {
        return self->skyIsoLastEntry;
    }

    for (i = 0; i < self->skyIsoCount; i++) {
        if (self->skyIso[i].origTex == origTex) {
            self->skyIsoLastOrigTex = origTex;
            self->skyIsoLastEntry = &self->skyIso[i];
            return &self->skyIso[i];
        }
    }
    if (!createIfMissing || self->skyIsoCount >= SKY_ISO_MAX)
        return NULL;
    {
        SkyIsoEntry *entry = &self->skyIso[self->skyIsoCount++];
        entry->origTex = origTex;
        entry->isoTex = NULL;
        entry->candidate = 0;
        entry->cloneAttempted = 0;
        entry->cloneReady = 0;
        entry->candidateDraws = 0;
        entry->candidateScenes = 0;
        entry->candidateLastScene = 0;
        entry->candidateMaxVerts = 0;
        entry->candidateMaxPrims = 0;
        entry->candidateScore = 0;
        entry->contaminatedDraws = 0;
        entry->contaminatedScenes = 0;
        entry->contaminatedLastScene = 0;
        entry->contaminatedScore = 0;
        entry->contaminationFlags = 0;
        self->skyIsoLastOrigTex = origTex;
        self->skyIsoLastEntry = entry;
        return entry;
    }
}

static int SkyIso_IsWarmupActive(WrappedDevice *self) {
    if (!self->skyWarmupStarted || self->skyWarmupScenes == 0)
        return 0;
    if (self->skySceneCounter < self->skyWarmupStartScene)
        return 0;
    return ((self->skySceneCounter - self->skyWarmupStartScene) < self->skyWarmupScenes) ? 1 : 0;
}

static void SkyIso_AccumulateScore(unsigned int *score, unsigned int nv, unsigned int pc) {
    unsigned int drawScore;

    if (!score)
        return;

    drawScore = nv + (pc * 64u);
    if (0xFFFFFFFFu - *score < drawScore)
        *score = 0xFFFFFFFFu;
    else
        *score += drawScore;
}

static void SkyIso_RecordObservation(WrappedDevice *self, SkyIsoEntry *entry, unsigned int nv, unsigned int pc,
    int candidateDraw, unsigned int contaminationFlags)
{
    unsigned int *draws;
    unsigned int *scenes;
    unsigned int *lastScene;
    unsigned int *score;

    if (!entry)
        return;

    if (candidateDraw) {
        draws = &entry->candidateDraws;
        scenes = &entry->candidateScenes;
        lastScene = &entry->candidateLastScene;
        score = &entry->candidateScore;
    } else {
        draws = &entry->contaminatedDraws;
        scenes = &entry->contaminatedScenes;
        lastScene = &entry->contaminatedLastScene;
        score = &entry->contaminatedScore;
        entry->contaminationFlags |= contaminationFlags;
    }

    if (*lastScene != self->skySceneCounter) {
        *lastScene = self->skySceneCounter;
        (*scenes)++;
    }

    (*draws)++;
    if (candidateDraw) {
        if (nv > entry->candidateMaxVerts)
            entry->candidateMaxVerts = nv;
        if (pc > entry->candidateMaxPrims)
            entry->candidateMaxPrims = pc;
    }
    SkyIso_AccumulateScore(score, nv, pc);
    self->skyIsoRefreshDirty = 1;
}

static int SkyIso_IsCandidateBaseDraw(WrappedDevice *self, unsigned int nv, unsigned int pc, unsigned int stage);

static int SkyIso_IsCandidateCleanDraw(WrappedDevice *self, unsigned int nv, unsigned int pc, unsigned int stage, int animatedTex0) {
    if (!SkyIso_IsCandidateBaseDraw(self, nv, pc, stage))
        return 0;
    if (animatedTex0)
        return 0;
    if (self->curDeclHasMorph)
        return 0;
    if (self->curDeclIsSkinned)
        return 0;
    if (self->curDeclUsesAuxStreams)
        return 0;
    return 1;
}

static unsigned int SkyIso_GetContaminationFlags(WrappedDevice *self, int candidateDraw, int animatedTex0) {
    unsigned int flags = 0;

    if (candidateDraw)
        return 0;

    if (animatedTex0)
        flags |= SKY_ISO_CONTAM_ANIMATED;
    if (!mat4_approx_equal(self->savedView, s_identity4x4, 1e-5f))
        flags |= SKY_ISO_CONTAM_NON_ID_VIEW;
    if (!mat4_approx_equal(self->savedWorld, s_identity4x4, 1e-5f))
        flags |= SKY_ISO_CONTAM_NON_ID_WORLD;
    if (self->curDeclPosType != D3DDECLTYPE_FLOAT3)
        flags |= SKY_ISO_CONTAM_NON_FLOAT3;
    if (self->curDeclPosType == D3DDECLTYPE_SHORT4)
        flags |= SKY_ISO_CONTAM_SHORT4;
    if (self->curDeclHasMorph || self->curDeclIsSkinned)
        flags |= SKY_ISO_CONTAM_DEFORMED;
    if (self->curDeclUsesAuxStreams)
        flags |= SKY_ISO_CONTAM_AUX_STREAM;

    return flags | SKY_ISO_CONTAM_NON_CANDIDATE;
}

static SkyIsoEntry *SkyIso_ObserveDraw(WrappedDevice *self, unsigned int stage, unsigned int nv, unsigned int pc, int animatedTex0) {
    void *origTex;
    SkyIsoEntry *entry;
    int candidateDraw;
    unsigned int contaminationFlags;

    if (!self->levelSceneDetected)
        return NULL;

    if (!self->skyIsolationEnable || stage >= 8)
        return NULL;

    /* Defer sky-texture cloning until gameplay patches are active.
     * Early startup/menu scenes are the known cold-start crash path. */
    if (!self->memoryPatchesApplied)
        return NULL;

    if (!self->skyWarmupStarted && self->viewProjValid) {
        self->skyWarmupStarted = 1;
        self->skyWarmupStartScene = self->skySceneCounter;
        log_int("SkyIso: warmup start scene=", (int)self->skyWarmupStartScene);
        log_int("SkyIso: warmup scenes=", (int)self->skyWarmupScenes);
    }

    origTex = self->curTexture[stage];
    if (!origTex)
        return NULL;

    entry = SkyIso_FindOrCreateEntry(self, origTex, 1);
    if (!entry)
        return NULL;

    if (entry->candidateDraws == 0 && entry->contaminatedDraws == 0)
        log_hex("SkyIso: observed texture=", (unsigned int)origTex);

    candidateDraw = SkyIso_IsCandidateCleanDraw(self, nv, pc, stage, animatedTex0);
    contaminationFlags = SkyIso_GetContaminationFlags(self, candidateDraw, animatedTex0);
    SkyIso_RecordObservation(self, entry, nv, pc, candidateDraw, contaminationFlags);
    return entry;
}

static int SkyIso_IsRankEligible(const SkyIsoEntry *entry, unsigned int bestScenes, unsigned int bestScore) {
    unsigned int minScenes;

    if (!entry || !entry->origTex || entry->candidateScenes == 0 || entry->candidateScore == 0)
        return 0;
    if (entry->contaminatedDraws > 0 || entry->contaminatedScenes > 0 ||
        entry->contaminatedScore > 0 || entry->contaminationFlags != 0)
        return 0;

    minScenes = bestScenes;
    if (minScenes > SKY_ISO_AUTO_MIN_SCENES)
        minScenes = SKY_ISO_AUTO_MIN_SCENES;
    if (bestScenes >= 2 && minScenes < 2)
        minScenes = 2;
    if (entry->candidateScenes < minScenes)
        return 0;

    if (bestScenes > 1 && (entry->candidateScenes * 2u) < bestScenes)
        return 0;

    if (bestScore > 0) {
        unsigned __int64 lhs = (unsigned __int64)entry->candidateScore * 100u;
        unsigned __int64 rhs = (unsigned __int64)bestScore * SKY_ISO_AUTO_MIN_SCORE_PCT;
        if (lhs < rhs)
            return 0;
    }

    return 1;
}

static void SkyIso_RefreshCandidateSet(WrappedDevice *self) {
    int prevCandidate[SKY_ISO_MAX];
    int taken[SKY_ISO_MAX];
    int i, slot;
    unsigned int bestScenes = 0;
    unsigned int bestScore = 0;

    /* Ranking only needs scene cadence; redoing it on every eligible draw is
     * wasted work once the current scene has already been classified. */
    if (!self->skyIsoRefreshDirty || self->skyIsoRefreshScene == self->skySceneCounter)
        return;

    for (i = 0; i < self->skyIsoCount; i++) {
        prevCandidate[i] = self->skyIso[i].candidate;
        self->skyIso[i].candidate = 0;
        taken[i] = 0;
        if (self->skyIso[i].candidateScenes > bestScenes)
            bestScenes = self->skyIso[i].candidateScenes;
        if (self->skyIso[i].candidateScore > bestScore)
            bestScore = self->skyIso[i].candidateScore;
    }

    for (slot = 0; slot < SKY_ISO_AUTO_MAX_TEXTURES; slot++) {
        int bestIdx = -1;
        for (i = 0; i < self->skyIsoCount; i++) {
            SkyIsoEntry *entry = &self->skyIso[i];
            if (taken[i] || !SkyIso_IsRankEligible(entry, bestScenes, bestScore))
                continue;

            if (bestIdx < 0 ||
                entry->candidateScore > self->skyIso[bestIdx].candidateScore ||
                (entry->candidateScore == self->skyIso[bestIdx].candidateScore &&
                 entry->candidateScenes > self->skyIso[bestIdx].candidateScenes) ||
                (entry->candidateScore == self->skyIso[bestIdx].candidateScore &&
                 entry->candidateScenes == self->skyIso[bestIdx].candidateScenes &&
                 entry->candidateMaxVerts > self->skyIso[bestIdx].candidateMaxVerts)) {
                bestIdx = i;
            }
        }
        if (bestIdx < 0)
            break;
        self->skyIso[bestIdx].candidate = 1;
        taken[bestIdx] = 1;
    }

    for (i = 0; i < self->skyIsoCount; i++) {
        SkyIsoEntry *entry = &self->skyIso[i];
        if (entry->candidate && !prevCandidate[i]) {
            log_hex("SkyIso: promoted texture=", (unsigned int)entry->origTex);
            log_int("SkyIso: promoted candidate scenes=", (int)entry->candidateScenes);
            log_int("SkyIso: promoted candidate draws=", (int)entry->candidateDraws);
            log_int("SkyIso: promoted contamination scenes=", (int)entry->contaminatedScenes);
            log_int("SkyIso: promoted contamination draws=", (int)entry->contaminatedDraws);
            log_hex("SkyIso: promoted contamination flags=", entry->contaminationFlags);
        }
    }

    self->skyIsoRefreshScene = self->skySceneCounter;
    self->skyIsoRefreshDirty = 0;
}

static int SkyIso_CloneTexture(WrappedDevice *self, SkyIsoEntry *entry) {
    typedef unsigned int (__stdcall *FN_GetType)(void*);
    typedef unsigned int (__stdcall *FN_TexGetLevelCount)(void*);
    typedef int (__stdcall *FN_TexGetLevelDesc)(void*, unsigned int, D3DSURFACE_DESC*);
    typedef int (__stdcall *FN_TexLockRect)(void*, unsigned int, D3DLOCKED_RECT*, void*, unsigned int);
    typedef int (__stdcall *FN_TexUnlockRect)(void*, unsigned int);
    typedef int (__stdcall *FN_CreateTexture)(void*, unsigned int, unsigned int, unsigned int,
        unsigned int, unsigned int, unsigned int, void**, void*);

    void *origTex;
    void *cloneTex = NULL;
    void **origVt;
    void **cloneVt;
    unsigned int levelCount;
    unsigned int level;
    int ok = 1;

    if (!entry || !entry->origTex)
        return 0;
    if (entry->cloneReady && entry->isoTex)
        return 1;
    if (entry->cloneAttempted)
        return 0;
    entry->cloneAttempted = 1;
    origTex = entry->origTex;

    origVt = *(void***)origTex;
    if (((FN_GetType)origVt[10])(origTex) != D3DRTYPE_TEXTURE)
        return 0;

    levelCount = ((FN_TexGetLevelCount)origVt[13])(origTex);
    if (levelCount == 0)
        levelCount = 1;

    {
        D3DSURFACE_DESC desc0;
        if (((FN_TexGetLevelDesc)origVt[17])(origTex, 0, &desc0) != 0)
            return 0;
        if (((FN_CreateTexture)RealVtbl(self)[SLOT_CreateTexture])(
                self->pReal,
                desc0.Width,
                desc0.Height,
                levelCount,
                0,
                desc0.Format,
                D3DPOOL_MANAGED,
                &cloneTex,
                NULL) != 0 || !cloneTex) {
            return 0;
        }
    }

    cloneVt = *(void***)cloneTex;
    for (level = 0; level < levelCount; level++) {
        D3DSURFACE_DESC levelDesc;
        D3DLOCKED_RECT srcRect;
        D3DLOCKED_RECT dstRect;
        unsigned int copyRows;
        unsigned int row;
        unsigned int rowBytes;

        if (((FN_TexGetLevelDesc)origVt[17])(origTex, level, &levelDesc) != 0) {
            ok = 0;
            break;
        }
        if (((FN_TexLockRect)origVt[19])(origTex, level, &srcRect, NULL, D3DLOCK_READONLY) != 0) {
            ok = 0;
            break;
        }
        if (((FN_TexLockRect)cloneVt[19])(cloneTex, level, &dstRect, NULL, 0) != 0) {
            ((FN_TexUnlockRect)origVt[20])(origTex, level);
            ok = 0;
            break;
        }

        copyRows = SkyIso_CopyRowsForLevel(&levelDesc);
        rowBytes = (unsigned int)((srcRect.Pitch < dstRect.Pitch) ? srcRect.Pitch : dstRect.Pitch);
        for (row = 0; row < copyRows; row++) {
            unsigned char *src = (unsigned char*)srcRect.pBits + row * (unsigned int)srcRect.Pitch;
            unsigned char *dst = (unsigned char*)dstRect.pBits + row * (unsigned int)dstRect.Pitch;
            memcpy(dst, src, rowBytes);
        }

        ((FN_TexUnlockRect)cloneVt[20])(cloneTex, level);
        ((FN_TexUnlockRect)origVt[20])(origTex, level);
    }

    if (!ok) {
        SkyIso_ReleaseTexture(cloneTex);
        return 0;
    }

    {
        D3DLOCKED_RECT mutateRect;
        if (((FN_TexLockRect)cloneVt[19])(cloneTex, 0, &mutateRect, NULL, 0) == 0) {
            if (mutateRect.pBits && mutateRect.Pitch > 0) {
                unsigned char *firstByte = (unsigned char*)mutateRect.pBits;
                firstByte[0] ^= 0x01; /* 1-bit mutation to split texture hash identity */
            }
            ((FN_TexUnlockRect)cloneVt[20])(cloneTex, 0);
        }
    }

    entry->isoTex = cloneTex;
    entry->cloneReady = 1;
    log_hex("SkyIso: cloned sky texture orig=", (unsigned int)entry->origTex);
    log_hex("SkyIso: cloned sky texture iso =", (unsigned int)entry->isoTex);
    return 1;
}

static int SkyIso_IsCandidateBaseDraw(WrappedDevice *self, unsigned int nv, unsigned int pc, unsigned int stage) {
    if (!self->skyIsolationEnable || stage >= 8)
        return 0;
    if (self->curDeclPosType != D3DDECLTYPE_FLOAT3)
        return 0;
    if (!self->curTexture[stage])
        return 0;
    if (nv < self->skyCandidateMinVerts || pc < self->skyCandidateMinPrims)
        return 0;
    if (!mat4_approx_equal(self->savedView, s_identity4x4, 1e-5f))
        return 0;
    if (!mat4_approx_equal(self->savedWorld, s_identity4x4, 1e-5f))
        return 0;
    return 1;
}

static unsigned int SkyIso_EstimateVertexCount(unsigned int pt, unsigned int pc) {
    switch (pt) {
        case 1: return pc;
        case 2: return pc * 2u;
        case 3: return pc + 1u;
        case 4: return pc * 3u;
        case 5: return pc + 2u;
        case 6: return pc + 2u;
        default: return pc * 3u;
    }
}

static void *SkyIso_BindForDraw(WrappedDevice *self, unsigned int stage, SkyIsoEntry *entry,
    unsigned int nv, unsigned int pc, int animatedTex0)
{
    int warmupActive;
    static int s_animatedSkipLogs = 0;
    static int s_applyLogs = 0;

    if (!entry || stage >= 8)
        return NULL;

    if (!SkyIso_IsCandidateCleanDraw(self, nv, pc, stage, animatedTex0)) {
        if (s_animatedSkipLogs < 16) {
            if (animatedTex0)
                log_str("SkyIso: skip animated texture draw\r\n");
            s_animatedSkipLogs++;
        }
        return NULL;
    }

    warmupActive = SkyIso_IsWarmupActive(self);
    if (warmupActive)
        return NULL;

    SkyIso_RefreshCandidateSet(self);
    if (!entry->candidate)
        return NULL;

    if (!entry->cloneReady && !entry->cloneAttempted)
        SkyIso_CloneTexture(self, entry);
    if (!entry->cloneReady || !entry->isoTex)
        return NULL;

    {
        typedef int (__stdcall *FN_SetTex)(void*, unsigned int, void*);
        ((FN_SetTex)RealVtbl(self)[SLOT_SetTexture])(self->pReal, stage, entry->isoTex);
    }

    if (s_applyLogs < 32) {
        log_hex("SkyIso: applied orig=", (unsigned int)entry->origTex);
        log_hex("SkyIso: applied iso =", (unsigned int)entry->isoTex);
        s_applyLogs++;
    }

    return entry->origTex;
}

static void PinnedDraw_ReleaseAll(WrappedDevice *self);
static void DrawCache_Clear(void);

static int TRL_IsMenuLikeScene(WrappedDevice *self) {
    if (self->drawsTotal == 0)
        return 0;
    return self->drawsProcessed <= 32 && self->drawsS4 == 0 && self->drawsF3 <= 32;
}

static int TRL_IsTransitionBlipScene(WrappedDevice *self) {
    if (self->drawsTotal == 0)
        return 0;
    return self->drawsProcessed <= 1 && self->drawsS4 == 0 && self->drawsF3 <= 1;
}

static int TRL_IsGameplayReadyScene(WrappedDevice *self) {
    if (self->drawsTotal == 0)
        return 0;
    return self->drawsProcessed >= 512 && self->drawsS4 >= 256;
}

static void TRL_LogTransitionScene(WrappedDevice *self, const char *tag) {
    log_str(tag);
    log_int(" scene=", (int)self->sceneCount);
    log_int(" d=", (int)self->drawsProcessed);
    log_int(" s4=", (int)self->drawsS4);
    log_int(" f3=", (int)self->drawsF3);
    log_str("\r\n");
}

static void TRL_OnLevelSceneDetected(WrappedDevice *self) {
    self->levelSceneDetected = 1;
    self->replayFeaturesEnabled = 0;
    self->skyWarmupStarted = 0;
    self->skyWarmupStartScene = 0;
    self->vpInverseValid = 0;
    self->proj3DCached = 0;
    self->postLatchSceneLogsRemaining = 30;
    SkyIso_Clear(self);
    PinnedDraw_ReleaseAll(self);
#if DRAW_CACHE_ENABLED
    DrawCache_Clear();
#endif
    TRL_LogTransitionScene(self, "Transition: gameplay scene latched");
    log_str("Transition: replay features remain disabled for TRL stability\r\n");
}

static void SkyIso_RestoreAfterDraw(WrappedDevice *self, unsigned int stage, void *origTex) {
    if (!origTex || stage >= 8)
        return;
    {
        typedef int (__stdcall *FN_SetTex)(void*, unsigned int, void*);
        ((FN_SetTex)RealVtbl(self)[SLOT_SetTexture])(self->pReal, stage, origTex);
    }
}

static void TRL_ClearTexture0TransformState(WrappedDevice *self) {
    typedef int (__stdcall *FN_SetTransform)(void*, unsigned int, float*);
    typedef int (__stdcall *FN_SetTSS)(void*, unsigned int, unsigned int, unsigned int);
    void **vt = RealVtbl(self);
    float texMatrix[16];

    mat4_identity(texMatrix);
    ((FN_SetTransform)vt[SLOT_SetTransform])(self->pReal, D3DTS_TEXTURE0, texMatrix);
    ((FN_SetTSS)vt[SLOT_SetTextureStageState])(self->pReal, 0,
        D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
}

static void TRL_ApplyTexture0TransformState(WrappedDevice *self) {
    typedef int (__stdcall *FN_SetTransform)(void*, unsigned int, float*);
    typedef int (__stdcall *FN_SetTSS)(void*, unsigned int, unsigned int, unsigned int);
    void **vt = RealVtbl(self);
    float texMatrix[16];

    if (!TRL_BuildTexture0Transform(self, texMatrix)) {
        TRL_ClearTexture0TransformState(self);
        return;
    }

    ((FN_SetTransform)vt[SLOT_SetTransform])(self->pReal, D3DTS_TEXTURE0, texMatrix);
    ((FN_SetTSS)vt[SLOT_SetTextureStageState])(self->pReal, 0,
        D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
}

/* ---- Pinned Draw Helpers ---- */

static void com_addref(void *p) {
    if (p) { typedef unsigned long (__stdcall *FN)(void*); ((FN)(*(void***)p)[1])(p); }
}
static void com_release(void *p) {
    if (p) { typedef unsigned long (__stdcall *FN)(void*); ((FN)(*(void***)p)[2])(p); }
}

/*
 * Capture a draw call into the pinned cache during the capture window.
 * Deduplicates by VB+IB+tex0 fingerprint. AddRefs all COM objects.
 */
static void PinnedDraw_Capture(WrappedDevice *self) {
    void *fpVB, *fpIB, *fpTex0;
    int i;
    PinnedDraw *pd;

    if (self->pinnedCaptureComplete || self->pinnedDrawCount >= PINNED_DRAW_MAX)
        return;

    fpVB   = self->streamVB[0];
    fpTex0 = self->curTexture[0];

    /* Get current index buffer */
    {
        typedef int (__stdcall *FN_GetIndices)(void*, void**);
        void **vt = RealVtbl(self);
        fpIB = NULL;
        ((FN_GetIndices)vt[SLOT_GetIndices])(self->pReal, &fpIB);
        if (fpIB) com_release(fpIB); /* GetIndices AddRefs, we just need the pointer */
    }

    /* Skip draws without geometry (no VB or no IB) */
    if (!fpVB) return;

    /* Dedup: check if this fingerprint already exists */
    for (i = 0; i < self->pinnedDrawCount; i++) {
        if (self->pinnedDraws[i].fpVB == fpVB &&
            self->pinnedDraws[i].fpIB == fpIB &&
            self->pinnedDraws[i].fpTex0 == fpTex0)
        {
            return; /* already captured */
        }
    }

    /* Capture new pinned draw */
    pd = &self->pinnedDraws[self->pinnedDrawCount];
    pd->fpVB = fpVB;
    pd->fpIB = fpIB;
    pd->fpTex0 = fpTex0;
    pd->active = 1;
    pd->submittedThisFrame = 1;
    pd->isViewSpace = mat4_approx_equal(self->savedView, s_identity4x4, 1e-6f);
    pd->isShort4 = (self->curDeclPosType == D3DDECLTYPE_SHORT4) ? 1 : 0;
    pd->posOff = self->s4PosOff;

    /* Save VS constants c0-c3 (WVP matrix at capture time) */
    for (i = 0; i < 16; i++) pd->wvpConst[i] = self->vsConst[i];

    /* AddRef and save COM objects */
    pd->vertexShader = self->lastVS;   com_addref(pd->vertexShader);
    pd->pixelShader  = self->lastPS;   com_addref(pd->pixelShader);
    pd->vertexDecl   = self->lastDecl; com_addref(pd->vertexDecl);
    pd->vertexBuffer = fpVB;           com_addref(pd->vertexBuffer);
    pd->vbOffset     = self->streamOffset[0];
    pd->vbStride     = self->streamStride[0];
    { /* Get and AddRef IB */
        typedef int (__stdcall *FN_GetIndices)(void*, void**);
        void **vt = RealVtbl(self);
        pd->indexBuffer = NULL;
        ((FN_GetIndices)vt[SLOT_GetIndices])(self->pReal, &pd->indexBuffer);
        /* GetIndices already AddRef'd */
    }
    { int t; for (t = 0; t < 4; t++) { pd->textures[t] = self->curTexture[t]; com_addref(pd->textures[t]); } }

    self->pinnedDrawCount++;
}

/*
 * Mark a draw as submitted this frame (by fingerprint match).
 */
static void PinnedDraw_MarkSubmitted(WrappedDevice *self) {
    void *fpVB  = self->streamVB[0];
    void *fpTex0 = self->curTexture[0];
    void *fpIB = NULL;
    int i;

    if (self->pinnedDrawCount == 0) return;

    {
        typedef int (__stdcall *FN_GetIndices)(void*, void**);
        void **vt = RealVtbl(self);
        ((FN_GetIndices)vt[SLOT_GetIndices])(self->pReal, &fpIB);
        if (fpIB) com_release(fpIB);
    }

    for (i = 0; i < self->pinnedDrawCount; i++) {
        PinnedDraw *pd = &self->pinnedDraws[i];
        if (pd->active && pd->fpVB == fpVB && pd->fpIB == fpIB && pd->fpTex0 == fpTex0) {
            pd->submittedThisFrame = 1;
            return;
        }
    }
}

/*
 * Replay all pinned draws NOT submitted this frame.
 * Called from WD_Present before the real Present.
 * Restores full D3D9 state for each draw, then restores original state after.
 */
static void PinnedDraw_ReplayMissing(WrappedDevice *self) {
    typedef int (__stdcall *FN_DIP)(void*, unsigned int, int, unsigned int, unsigned int, unsigned int, unsigned int);
    typedef int (__stdcall *FN_SetVS)(void*, void*);
    typedef int (__stdcall *FN_SetPS)(void*, void*);
    typedef int (__stdcall *FN_SetDecl)(void*, void*);
    typedef int (__stdcall *FN_SetTex)(void*, unsigned int, void*);
    typedef int (__stdcall *FN_SetSS)(void*, unsigned int, void*, unsigned int, unsigned int);
    typedef int (__stdcall *FN_SetIndices)(void*, void*);
    typedef int (__stdcall *FN_SetVSCF)(void*, unsigned int, const float*, unsigned int);
    typedef int (__stdcall *FN_SetTransform)(void*, unsigned int, float*);
    typedef int (__stdcall *FN_BeginScene)(void*);
    typedef int (__stdcall *FN_EndScene)(void*);

    void **vt = RealVtbl(self);
    int i, replayed = 0;

    if (self->pinnedDrawCount == 0) return;

    /* Begin a scene for the replay draws */
    ((FN_BeginScene)vt[SLOT_BeginScene])(self->pReal);
    TRL_ClearTexture0TransformState(self);

    for (i = 0; i < self->pinnedDrawCount; i++) {
        PinnedDraw *pd = &self->pinnedDraws[i];
        if (!pd->active || pd->submittedThisFrame)
            continue;

        /* Restore state for this draw */
        if (pd->vertexDecl)
            ((FN_SetDecl)vt[SLOT_SetVertexDeclaration])(self->pReal, pd->vertexDecl);
        if (pd->pixelShader)
            ((FN_SetPS)vt[SLOT_SetPixelShader])(self->pReal, pd->pixelShader);
        if (pd->vertexBuffer)
            ((FN_SetSS)vt[SLOT_SetStreamSource])(self->pReal, 0, pd->vertexBuffer, pd->vbOffset, pd->vbStride);
        if (pd->indexBuffer)
            ((FN_SetIndices)vt[SLOT_SetIndices])(self->pReal, pd->indexBuffer);
        { int t; for (t = 0; t < 4; t++)
            ((FN_SetTex)vt[SLOT_SetTexture])(self->pReal, t, pd->textures[t]); }

        /* Restore VS constants c0-c3 (WVP from capture time) */
        ((FN_SetVSCF)vt[SLOT_SetVertexShaderConstantF])(self->pReal, 0, pd->wvpConst, 4);

        /* Apply transform overrides using cached WVP */
        {
            float wvp_row[16], view[16], proj[16], vp[16], world[16];
            const float *gameView = (const float *)TRL_VIEW_MATRIX_ADDR;
            const float *gameProj = (const float *)TRL_PROJ_MATRIX_ADDR;
            static float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
            int j;

            for (j = 0; j < 16; j++) { view[j] = gameView[j]; proj[j] = gameProj[j]; }
            if (pd->isViewSpace) {
                for (j = 0; j < 16; j++) {
                    world[j] = identity[j];
                    view[j] = identity[j];
                }
            } else {
                mat4_transpose(wvp_row, pd->wvpConst);
                mat4_multiply(vp, view, proj);

                if (self->vpInverseValid) {
                    mat4_multiply(world, wvp_row, self->cachedVPInverse);
                } else {
                    for (j = 0; j < 16; j++) world[j] = identity[j];
                }
            }

            if (pd->isShort4 && pd->vertexBuffer && pd->vertexDecl) {
                if (!S4_ExpandAndDraw(self, pd->vertexBuffer, pd->vbOffset, pd->vbStride,
                                      pd->vertexDecl, pd->posOff,
                                      pd->primType, pd->baseVertexIndex, pd->minVertexIndex, pd->numVertices,
                                      pd->startIndex, pd->primCount,
                                      world, view, proj)) {
                    ((FN_SetTransform)vt[SLOT_SetTransform])(self->pReal, D3DTS_WORLD, world);
                    ((FN_SetTransform)vt[SLOT_SetTransform])(self->pReal, D3DTS_VIEW, view);
                    ((FN_SetTransform)vt[SLOT_SetTransform])(self->pReal, D3DTS_PROJECTION, proj);
                    ((FN_SetVS)vt[SLOT_SetVertexShader])(self->pReal, NULL);
                    TRL_ApplyFFPDrawState(self, 0);
                    ((FN_DIP)vt[SLOT_DrawIndexedPrimitive])(self->pReal,
                        pd->primType, pd->baseVertexIndex, pd->minVertexIndex,
                        pd->numVertices, pd->startIndex, pd->primCount);
                }
            } else {
                ((FN_SetTransform)vt[SLOT_SetTransform])(self->pReal, D3DTS_WORLD, world);
                ((FN_SetTransform)vt[SLOT_SetTransform])(self->pReal, D3DTS_VIEW, view);
                ((FN_SetTransform)vt[SLOT_SetTransform])(self->pReal, D3DTS_PROJECTION, proj);
                ((FN_SetVS)vt[SLOT_SetVertexShader])(self->pReal, NULL);
                TRL_ApplyFFPDrawState(self, 0);
                ((FN_DIP)vt[SLOT_DrawIndexedPrimitive])(self->pReal,
                    pd->primType, pd->baseVertexIndex, pd->minVertexIndex,
                    pd->numVertices, pd->startIndex, pd->primCount);
            }
        }
        replayed++;
    }

    if (self->lastVS)
        ((FN_SetVS)vt[SLOT_SetVertexShader])(self->pReal, self->lastVS);

    ((FN_EndScene)vt[SLOT_EndScene])(self->pReal);

    if (replayed > 0) {
        log_str("  PinnedDraw: replayed ");
        log_int("", replayed);
        log_str(" cached draws\r\n");
    }
}

/*
 * Reset per-frame tracking. Called at frame start.
 */
static void PinnedDraw_FrameReset(WrappedDevice *self) {
    int i;
    for (i = 0; i < self->pinnedDrawCount; i++)
        self->pinnedDraws[i].submittedThisFrame = 0;
}

/*
 * Release all pinned draw COM references. Called on device release.
 */
static void PinnedDraw_ReleaseAll(WrappedDevice *self) {
    int i;
    for (i = 0; i < self->pinnedDrawCount; i++) {
        PinnedDraw *pd = &self->pinnedDraws[i];
        if (!pd->active) continue;
        com_release(pd->vertexShader);
        com_release(pd->pixelShader);
        com_release(pd->vertexDecl);
        com_release(pd->vertexBuffer);
        com_release(pd->indexBuffer);
        { int t; for (t = 0; t < 4; t++) com_release(pd->textures[t]); }
        pd->active = 0;
    }
    self->pinnedDrawCount = 0;
}

/*
 * Cached SetTransform: skip pushing matrices that are byte-identical to
 * the previously applied value. View/Projection rarely change within a
 * frame; World is frequently identical across consecutive draws of the
 * same object. Eliminates the bulk of SetTransform vtable thunks.
 *
 * The proxy is the only path to the device's transform state once
 * viewProjValid=1 (WD_SetTransform blocks all external V/P/W writes), so
 * appliedWorld/View/Proj are an authoritative cache. They are reset to
 * zero in WD_Reset to handle device-resource invalidation.
 */
static __inline void TRL_ApplyTransformsCached(WrappedDevice *self,
    float *world, float *view, float *proj)
{
    typedef int (__stdcall *FN_SetTransform)(void*, unsigned int, float*);
    void **vt = RealVtbl(self);
    FN_SetTransform setT = (FN_SetTransform)vt[SLOT_SetTransform];
    int needWorld = memcmp(self->appliedWorld, world, 64) != 0;
    int needView  = memcmp(self->appliedView,  view,  64) != 0;
    int needProj  = memcmp(self->appliedProj,  proj,  64) != 0;
    if (!needWorld && !needView && !needProj)
        return;
    self->transformOverrideActive = 1;
    if (needWorld) { memcpy(self->appliedWorld, world, 64); setT(self->pReal, D3DTS_WORLD,      world); }
    if (needView)  { memcpy(self->appliedView,  view,  64); setT(self->pReal, D3DTS_VIEW,       view);  }
    if (needProj)  { memcpy(self->appliedProj,  proj,  64); setT(self->pReal, D3DTS_PROJECTION, proj);  }
    self->transformOverrideActive = 0;
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
     * TRL has two vertex shader paths:
     *
     * 1. SHORT4 position (world geometry): c0-c3 = full WVP
     *    → decompose World = WVP * inverse(VP)
     *
     * 2. FLOAT3 position (hair, eyelashes, foliage, characters):
     *    c0-c3 = projection-only matrix. Vertex positions are already
     *    in view space (pre-multiplied by World*View on CPU).
     *    → set World=Identity, View=Identity, Proj=game projection
     *    so Remix captures these as view-space geometry.
     *
     * Detection: FLOAT3 draws have curDeclPosType==2 (D3DDECLTYPE_FLOAT3)
     * and c0-c3 row3 matches a perspective projection pattern [~0,~0,~1,~0].
     */
    {
        float wvp_row[16];
        int isViewSpaceDraw = 0;

        mat4_transpose(wvp_row, &self->vsConst[VS_REG_WVP_START * 4]);

        /* Detect FLOAT3 draws with projection-only c0-c3:
         * Perspective projection row3 = [0, 0, +-1, 0] (w passthrough).
         * Check: row3[0]~0, row3[1]~0, |row3[2]|~1, row3[3]~0
         * Also: row0[1]==0, row0[2]==0, row1[0]==0, row1[2]==0 (diagonal structure) */
        if (self->curDeclPosType == D3DDECLTYPE_FLOAT3) {
            float r30 = wvp_row[12], r31 = wvp_row[13], r32 = wvp_row[14], r33 = wvp_row[15];
            if (r30 > -0.01f && r30 < 0.01f &&
                r31 > -0.01f && r31 < 0.01f &&
                (r32 > 0.9f || r32 < -0.9f) &&
                r33 > -0.5f && r33 < 0.5f &&
                wvp_row[1] > -0.01f && wvp_row[1] < 0.01f &&
                wvp_row[2] > -0.01f && wvp_row[2] < 0.01f) {
                isViewSpaceDraw = 1;
            }
        }

        if (isViewSpaceDraw) {
            /* View-space positions: set World=Identity, View=Identity,
             * override proj with the raw game projection from memory.
             * The vertex shader applies c0-c3 (projection) to view-space
             * positions, so Remix sees identity W*V and correct projection. */
            for (i = 0; i < 16; i++) world[i] = identity[i];
            for (i = 0; i < 16; i++) view[i] = identity[i];
            /* proj already loaded from game memory — use it as-is */
        } else if (self->curDeclIsSkinned) {
            /* Skinned draws: use c4-c6 packed 4x3 World matrix */
            const float *src = &self->vsConst[4 * 4];
            world[0]  = src[0]; world[1]  = src[4]; world[2]  = src[8];  world[3]  = 0.0f;
            world[4]  = src[1]; world[5]  = src[5]; world[6]  = src[9];  world[7]  = 0.0f;
            world[8]  = src[2]; world[9]  = src[6]; world[10] = src[10]; world[11] = 0.0f;
            world[12] = src[3]; world[13] = src[7]; world[14] = src[11]; world[15] = 1.0f;
        } else {
            /* Standard: decompose World = WVP * inverse(VP).
             * No quantization — rounding rotation components by 1e-3 causes
             * visible position drift at large translation distances (~8000 units). */
            mat4_multiply(world, wvp_row, self->cachedVPInverse);
        }
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

    /* Save computed matrices for S4_ExpandAndDraw and DrawCache */
    memcpy(self->savedWorld, world, 64);
    memcpy(self->savedView, view, 64);
    memcpy(self->savedProj, proj, 64);

    /* Apply all three transforms (rtx.fusedWorldViewMode=0 treats them independently).
     * Cached helper skips redundant SetTransform calls when the matrix hasn't changed. */
    TRL_ApplyTransformsCached(self, world, view, proj);

    self->viewProjDirty = 0;
    self->worldDirty = 0;
}

#if ENABLE_SKINNING
#include "d3d9_skinning.h"
#endif

/*
 * Inject a proxy-side directional light so Remix always has illumination.
 * Called once per frame (on first draw). The light provides baseline
 * brightness — Remix's path tracer uses it as an additional light source.
 */
static void TRL_InjectLight(WrappedDevice *self) {
    typedef int (__stdcall *FN_SetLight)(void*, unsigned int, void*);
    typedef int (__stdcall *FN_LightEnable)(void*, unsigned int, int);
    void **vt = RealVtbl(self);

    /* D3DLIGHT9 structure: Type(4) + Diffuse(16) + Specular(16) + Ambient(16)
     * + Position(12) + Direction(12) + Range(4) + Falloff(4) + Atten0-2(12) + Theta(4) + Phi(4) = 104 bytes */
    float light[26]; /* 104 bytes / 4 = 26 floats */
    int i;
    for (i = 0; i < 26; i++) light[i] = 0.0f;

    *(unsigned int*)&light[0] = D3DLIGHT_DIRECTIONAL; /* Type */
    /* Diffuse: warm white */
    light[1] = 1.0f; light[2] = 0.95f; light[3] = 0.9f; light[4] = 1.0f;
    /* Direction: slightly down and forward */
    light[9] = 0.3f; light[10] = -0.7f; light[11] = 0.3f;

    ((FN_SetLight)vt[SLOT_SetLight])(self->pReal, 7, light);
    ((FN_LightEnable)vt[SLOT_LightEnable])(self->pReal, 7, 1);
}

/*
 * Configure fixed-function texture and lighting state for null-VS draws.
 * Keep stage-0 texture transforms disabled for rigid FFP draws: TRL's shader
 * UV-animation constants are not stable enough to project into generic FFP
 * state without making world textures wobble. Animated/deformed draws stay on
 * the shader route instead.
 */
static void TRL_ApplyFFPDrawState(WrappedDevice *self, int applyTex0Transform) {
    typedef int (__stdcall *FN_SetRS)(void*, unsigned int, unsigned int);
    typedef int (__stdcall *FN_SetTSS)(void*, unsigned int, unsigned int, unsigned int);
    void **vt = RealVtbl(self);
    (void)applyTex0Transform;

    ((FN_SetTSS)vt[SLOT_SetTextureStageState])(self->pReal, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    ((FN_SetTSS)vt[SLOT_SetTextureStageState])(self->pReal, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    ((FN_SetTSS)vt[SLOT_SetTextureStageState])(self->pReal, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    ((FN_SetTSS)vt[SLOT_SetTextureStageState])(self->pReal, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    ((FN_SetTSS)vt[SLOT_SetTextureStageState])(self->pReal, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    ((FN_SetTSS)vt[SLOT_SetTextureStageState])(self->pReal, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    ((FN_SetTSS)vt[SLOT_SetTextureStageState])(self->pReal, 0, D3DTSS_TEXCOORDINDEX, 0);
    TRL_ClearTexture0TransformState(self);
    ((FN_SetTSS)vt[SLOT_SetTextureStageState])(self->pReal, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);

    ((FN_SetRS)vt[SLOT_SetRenderState])(self->pReal, D3DRS_LIGHTING, 1);
    ((FN_SetRS)vt[SLOT_SetRenderState])(self->pReal, D3DRS_AMBIENT, 0xFFFFFFFF);
    ((FN_SetRS)vt[SLOT_SetRenderState])(self->pReal, D3DRS_COLORVERTEX, 1);
    ((FN_SetRS)vt[SLOT_SetRenderState])(self->pReal, D3DRS_DIFFUSEMATERIALSOURCE, 1);
    ((FN_SetRS)vt[SLOT_SetRenderState])(self->pReal, D3DRS_CULLMODE, 1);
}

/*
 * Shader Passthrough mode: keep shaders active, apply transform overrides.
 * Shaders run normally (SHORT4 positions need the VS to decode), but we
 * call SetTransform so Remix sees decomposed W/V/P for path tracing.
 */
static void TRL_PrepDraw(WrappedDevice *self) {
    TRL_ApplyTransformOverrides(self);

    /* Inject a directional light once per frame for guaranteed illumination */
    if (!self->ffpSetup) {
        TRL_InjectLight(self);
        self->ffpSetup = 1;
    }

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

/* ---- SHORT4 → FLOAT3 position expansion ----
 *
 * Eliminates rtx.useVertexCapture by converting SHORT4 vertex positions to
 * FLOAT3 on the CPU, then drawing in FFP mode. Remix reads stable model-space
 * FLOAT3 positions from the vertex buffer; SetTransform provides W/V/P.
 *
 * With useVertexCapture=false, Remix no longer hashes VS constants (which
 * include the per-frame WVP matrix), giving stable geometry hashes.
 */

/* Size in bytes of each D3DDECLTYPE */
static int DeclTypeSize(int type) {
    switch (type) {
        case 0: return 4;   /* FLOAT1 */
        case 1: return 8;   /* FLOAT2 */
        case 2: return 12;  /* FLOAT3 */
        case 3: return 16;  /* FLOAT4 */
        case 4: return 4;   /* D3DCOLOR */
        case 5: return 4;   /* UBYTE4 */
        case 6: return 4;   /* SHORT2 */
        case 7: return 8;   /* SHORT4 */
        case 8: return 4;   /* UBYTE4N */
        case 9: return 4;   /* SHORT2N */
        case 10: return 8;  /* SHORT4N */
        case 11: return 4;  /* USHORT2N */
        case 12: return 8;  /* USHORT4N */
        case 13: return 4;  /* UDEC3 */
        case 14: return 4;  /* DEC3N */
        case 15: return 4;  /* FLOAT16_2 */
        case 16: return 8;  /* FLOAT16_4 */
        default: return 4;
    }
}

/* Create an expanded vertex declaration: SHORT4 POSITION → FLOAT3.
 * Offsets of elements after POSITION are shifted by +4 (FLOAT3=12 - SHORT4=8).
 * Returns cached expanded decl, or NULL on failure. */
static void *S4_GetExpandedDecl(WrappedDevice *self, void *origDecl,
    unsigned char *elemBuf, unsigned int numElems, int posOff, int *outStride)
{
    typedef int (__stdcall *FN_CreateDecl)(void*, void*, void**);
    int i, sizeDelta;
    unsigned char expanded[8 * 32];
    unsigned int outIdx = 0;
    void *newDecl = NULL;

    /* Check cache */
    for (i = 0; i < self->s4DeclCount; i++) {
        if (self->s4DeclOrig[i] == origDecl) {
            *outStride = self->s4Stride[i];
            return self->s4DeclExp[i];
        }
    }

    sizeDelta = 0;

    /* First pass: compute total size delta from all expansions */
    for (i = 0; (unsigned int)i < numElems; i++) {
        unsigned char *el = &elemBuf[i * 8];
        unsigned short stream = *(unsigned short*)&el[0];
        unsigned char  type   = el[4];
        unsigned char  usage  = el[6];
        if (stream == 0xFF || stream == 0xFFFF) break;
        if (stream != 0) continue;
        if (usage == D3DDECLUSAGE_POSITION && type == D3DDECLTYPE_SHORT4)
            sizeDelta += 12 - 8;   /* SHORT4(8) → FLOAT3(12) = +4 */
        else if (usage == D3DDECLUSAGE_TEXCOORD && type == D3DDECLTYPE_SHORT2)
            sizeDelta += 8 - 4;    /* SHORT2(4) → FLOAT2(8) = +4 */
    }

    /* Second pass: build expanded declaration with running offset adjustment */
    {
        int runDelta = 0;
        for (i = 0; (unsigned int)i < numElems; i++) {
            unsigned char *el = &elemBuf[i * 8];
            unsigned short stream = *(unsigned short*)&el[0];
            unsigned short offset = *(unsigned short*)&el[2];
            unsigned char  type   = el[4];
            unsigned char  usage  = el[6];
            unsigned char  uIdx   = el[7];

            if (stream == 0xFF || stream == 0xFFFF) {
                memcpy(&expanded[outIdx * 8], el, 8);
                outIdx++;
                break;
            }

            memcpy(&expanded[outIdx * 8], el, 8);

            /* Apply running offset adjustment */
            if (stream == 0)
                *(unsigned short*)&expanded[outIdx * 8 + 2] = offset + runDelta;

            if (usage == D3DDECLUSAGE_POSITION && stream == 0 && uIdx == 0
                && type == D3DDECLTYPE_SHORT4) {
                /* SHORT4 → FLOAT3 */
                expanded[outIdx * 8 + 4] = (unsigned char)D3DDECLTYPE_FLOAT3;
                runDelta += 12 - 8;
            } else if (usage == D3DDECLUSAGE_TEXCOORD && stream == 0
                       && type == D3DDECLTYPE_SHORT2) {
                /* SHORT2 → FLOAT2 */
                expanded[outIdx * 8 + 4] = (unsigned char)D3DDECLTYPE_FLOAT2;
                runDelta += 8 - 4;
            }
            outIdx++;
        }
    }

    /* Compute expanded stride */
    {
        int maxEnd = 0;
        unsigned int e;
        for (e = 0; e < outIdx; e++) {
            unsigned char *el = &expanded[e * 8];
            unsigned short s = *(unsigned short*)&el[0];
            if (s == 0xFF || s == 0xFFFF) break;
            if (s == 0) {
                unsigned short off = *(unsigned short*)&el[2];
                int sz = DeclTypeSize(el[4]);
                if ((int)(off + sz) > maxEnd) maxEnd = off + sz;
            }
        }
        *outStride = maxEnd;
    }

    if (((FN_CreateDecl)RealVtbl(self)[SLOT_CreateVertexDeclaration])(
            self->pReal, expanded, &newDecl) == 0 && newDecl) {
        if (self->s4DeclCount < 64) {
            self->s4DeclOrig[self->s4DeclCount] = origDecl;
            self->s4DeclExp[self->s4DeclCount] = newDecl;
            self->s4Stride[self->s4DeclCount] = *outStride;
            self->s4DeclCount++;
        }
        log_hex("  S4: expanded decl created: ", (unsigned int)newDecl);
        log_int("    stride: ", *outStride);
        return newDecl;
    }

    return NULL;
}

/* Release all cached expanded VBs. Called every frame to prevent stale data
 * from dynamic VBs where the game reuses the same VB pointer with new content. */
static void S4_FlushVBCache(WrappedDevice *self) {
    typedef unsigned long (__stdcall *FN_Rel)(void*);
    int i;
    for (i = 0; i < self->s4VBCacheCount; i++) {
        if (self->s4VBCache[i].expVB)
            ((FN_Rel)(*(void***)self->s4VBCache[i].expVB)[2])(self->s4VBCache[i].expVB);
        self->s4VBCache[i].expVB = NULL;
        self->s4VBCache[i].srcVB = NULL;
    }
    self->s4VBCacheCount = 0;
}

/* Find or create a cached expanded VB for the given source VB region.
 * Returns the expanded VB pointer and stride, or NULL if cache miss and
 * expansion is needed. On cache miss, performs the expansion and caches it. */
static void *S4_GetCachedExpVB(WrappedDevice *self,
    void *srcVB, unsigned int srcOff, unsigned int srcStride, int posOff,
    unsigned int baseVtx, unsigned int nv, int expStride, unsigned int *outExpStride,
    void *origDecl)
{
    typedef int (__stdcall *FN_CreateVB)(void*, unsigned int, unsigned int, unsigned int, unsigned int, void**, void*);
    typedef int (__stdcall *FN_VBLock)(void*, unsigned int, unsigned int, void**, unsigned int);
    typedef int (__stdcall *FN_VBUnlock)(void*);
    int i, slot;
    unsigned int totalSrc, totalDst, fp;
    void **srcVt;
    void **dstVt;
    unsigned char *srcData = NULL;
    unsigned char *dstData = NULL;
    void *newVB = NULL;
    unsigned int v;

    /* Compute content fingerprint: lock source, XOR first 32 bytes.
     * Detects dynamic VBs where the game reuses the pointer for new content. */
    srcVt = *(void***)srcVB;
    {
        unsigned char *fpData = NULL;
        unsigned int fpSize = 32;
        if (nv * srcStride < fpSize) fpSize = nv * srcStride;
        if (((FN_VBLock)srcVt[11])(srcVB, srcOff + baseVtx * srcStride,
                fpSize, (void**)&fpData, D3DLOCK_READONLY) == 0 && fpData) {
            unsigned int *w = (unsigned int*)fpData;
            unsigned int n = fpSize / 4;
            fp = 0;
            for (i = 0; (unsigned int)i < n; i++) fp ^= w[i];
            ((FN_VBUnlock)srcVt[12])(srcVB);
        } else {
            fp = 0; /* can't fingerprint — treat as always-miss */
        }
    }

    /* Check cache (includes fingerprint for staleness detection) */
    for (i = 0; i < self->s4VBCacheCount; i++) {
        if (self->s4VBCache[i].srcVB == srcVB &&
            self->s4VBCache[i].srcOff == srcOff &&
            self->s4VBCache[i].bvi == (int)baseVtx &&
            self->s4VBCache[i].nv == nv &&
            self->s4VBCache[i].fingerprint == fp) {
            *outExpStride = self->s4VBCache[i].expStride;
            return self->s4VBCache[i].expVB;
        }
    }

    /* Cache miss or stale — expand and store */
    totalSrc = nv * srcStride;
    totalDst = nv * (unsigned int)expStride;

    /* Create a managed VB for the expanded data (survives device state changes) */
    if (((FN_CreateVB)RealVtbl(self)[SLOT_CreateVertexBuffer])(
            self->pReal, totalDst,
            D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED,
            &newVB, NULL) != 0 || !newVB)
        return NULL;

    /* Lock source */
    if (((FN_VBLock)srcVt[11])(srcVB, srcOff + baseVtx * srcStride,
            totalSrc, (void**)&srcData, D3DLOCK_READONLY) != 0 || !srcData) {
        typedef unsigned long (__stdcall *FN_Rel)(void*);
        ((FN_Rel)(*(void***)newVB)[2])(newVB);
        return NULL;
    }

    /* Lock destination */
    dstVt = *(void***)newVB;
    if (((FN_VBLock)dstVt[11])(newVB, 0, totalDst,
            (void**)&dstData, 0) != 0 || !dstData) {
        typedef unsigned long (__stdcall *FN_Rel)(void*);
        ((FN_VBUnlock)srcVt[12])(srcVB);
        ((FN_Rel)(*(void***)newVB)[2])(newVB);
        return NULL;
    }

    /* Expand vertices: walk elements, expand SHORT4→FLOAT3 and SHORT2→FLOAT2.
     * Uses the original element layout to know what to expand. Reads element
     * info from origDecl via GetDeclaration. */
    {
        typedef int (__stdcall *FN_GetDecl)(void*, void*, unsigned int*);
        void **declVt = *(void***)origDecl;
        unsigned char elBuf[8 * 32];
        unsigned int nEl = 0;
        ((FN_GetDecl)declVt[4])(origDecl, NULL, &nEl);
        if (nEl > 32) nEl = 32;
        ((FN_GetDecl)declVt[4])(origDecl, elBuf, &nEl);

        for (v = 0; v < nv; v++) {
            unsigned char *src = srcData + v * srcStride;
            unsigned char *dst = dstData + v * expStride;
            int srcCursor = 0, dstCursor = 0;
            unsigned int e;

            for (e = 0; e < nEl; e++) {
                unsigned char *el = &elBuf[e * 8];
                unsigned short stream = *(unsigned short*)&el[0];
                unsigned short offset = *(unsigned short*)&el[2];
                unsigned char  type   = el[4];
                unsigned char  usage  = el[6];

                if (stream == 0xFF || stream == 0xFFFF) break;
                if (stream != 0) continue;

                /* Copy any gap bytes between elements */
                if ((int)offset > srcCursor) {
                    int gap = (int)offset - srcCursor;
                    memcpy(dst + dstCursor, src + srcCursor, gap);
                    dstCursor += gap;
                    srcCursor += gap;
                }

                if (usage == D3DDECLUSAGE_POSITION && type == D3DDECLTYPE_SHORT4) {
                    /* SHORT4 (8 bytes) → FLOAT3 (12 bytes) */
                    short *sp = (short*)(src + offset);
                    float *dp = (float*)(dst + dstCursor);
                    dp[0] = (float)sp[0];
                    dp[1] = (float)sp[1];
                    dp[2] = (float)sp[2];
                    srcCursor = (int)offset + 8;
                    dstCursor += 12;
                } else if (usage == D3DDECLUSAGE_TEXCOORD && type == D3DDECLTYPE_SHORT2) {
                    /* SHORT2 (4 bytes) → FLOAT2 (8 bytes): normalize to [0,1] UV range.
                     * TRL stores UVs as short * 4096: divide to recover float UV. */
                    short *sp = (short*)(src + offset);
                    float *dp = (float*)(dst + dstCursor);
                    dp[0] = (float)sp[0] / 4096.0f;
                    dp[1] = (float)sp[1] / 4096.0f;
                    srcCursor = (int)offset + 4;
                    dstCursor += 8;
                } else {
                    /* Copy element as-is */
                    int sz = DeclTypeSize(type);
                    memcpy(dst + dstCursor, src + offset, sz);
                    srcCursor = (int)offset + sz;
                    dstCursor += sz;
                }
            }
            /* Copy any trailing bytes */
            if (srcCursor < (int)srcStride) {
                int tail = (int)srcStride - srcCursor;
                if (dstCursor + tail <= expStride)
                    memcpy(dst + dstCursor, src + srcCursor, tail);
            }
        }
    }

    ((FN_VBUnlock)dstVt[12])(newVB);
    ((FN_VBUnlock)srcVt[12])(srcVB);

    /* Store in cache */
    slot = self->s4VBCacheCount;
    if (slot >= S4_VB_CACHE_SIZE) {
        /* Cache full — evict oldest (slot 0), shift down */
        typedef unsigned long (__stdcall *FN_Rel)(void*);
        if (self->s4VBCache[0].expVB)
            ((FN_Rel)(*(void***)self->s4VBCache[0].expVB)[2])(self->s4VBCache[0].expVB);
        for (i = 0; i < S4_VB_CACHE_SIZE - 1; i++)
            self->s4VBCache[i] = self->s4VBCache[i + 1];
        slot = S4_VB_CACHE_SIZE - 1;
    } else {
        self->s4VBCacheCount++;
    }
    self->s4VBCache[slot].srcVB = srcVB;
    self->s4VBCache[slot].srcOff = srcOff;
    self->s4VBCache[slot].bvi = (int)baseVtx;
    self->s4VBCache[slot].nv = nv;
    self->s4VBCache[slot].fingerprint = fp;
    self->s4VBCache[slot].expVB = newVB;
    self->s4VBCache[slot].expStride = (unsigned int)expStride;

    *outExpStride = (unsigned int)expStride;
    return newVB;
}

/* Expand SHORT4 → FLOAT3 (cached), null both shaders, draw FFP, restore.
 * Returns 1 on success, 0 on failure (falls through to original draw). */
static int S4_ExpandAndDraw(WrappedDevice *self,
    void *srcVB, unsigned int srcOff, unsigned int srcStride, void *origDecl, int posOff,
    unsigned int pt, int bvi, unsigned int mi, unsigned int nv, unsigned int si, unsigned int pc,
    float *world, float *view, float *proj)
{
    typedef int (__stdcall *FN_SetTransform)(void*, unsigned int, float*);
    typedef int (__stdcall *FN_SetSS)(void*, unsigned int, void*, unsigned int, unsigned int);
    typedef int (__stdcall *FN_SetDecl)(void*, void*);
    typedef int (__stdcall *FN_SetVS)(void*, void*);
    typedef int (__stdcall *FN_DIP)(void*, unsigned int, int, unsigned int, unsigned int, unsigned int, unsigned int);
    void **vt = RealVtbl(self);
    void *expDecl;
    void *expVB;
    int expStride = 0;
    unsigned int cachedStride = 0;
    unsigned int baseVtx = (unsigned int)bvi + mi;

    /* Get expanded declaration */
    {
        typedef int (__stdcall *FN_GetDecl)(void*, void*, unsigned int*);
        void **declVt = *(void***)origDecl;
        unsigned char elemBuf[8 * 32];
        unsigned int numElems = 0;
        int hr = ((FN_GetDecl)declVt[4])(origDecl, NULL, &numElems);
        if (hr != 0 || numElems == 0 || numElems > 32) return 0;
        hr = ((FN_GetDecl)declVt[4])(origDecl, elemBuf, &numElems);
        if (hr != 0) return 0;
        expDecl = S4_GetExpandedDecl(self, origDecl, elemBuf, numElems, posOff, &expStride);
    }
    if (!expDecl || expStride == 0) return 0;

    /* Get cached expanded VB (expands on first call, reuses after) */
    expVB = S4_GetCachedExpVB(self, srcVB, srcOff, srcStride, posOff,
                              baseVtx, nv, expStride, &cachedStride, origDecl);
    if (!expVB) return 0;

    /* Set expanded VB + declaration */
    ((FN_SetSS)vt[SLOT_SetStreamSource])(self->pReal, 0, expVB, 0, cachedStride);
    ((FN_SetDecl)vt[SLOT_SetVertexDeclaration])(self->pReal, expDecl);

    /* Null vertex shader for FFP vertex transform.
     * Keep pixel shader active — Remix uses it to identify the albedo texture,
     * and TRL's world PS expects standard FFP interpolators (t0, v0). */
    ((FN_SetVS)vt[SLOT_SetVertexShader])(self->pReal, NULL);

    /* Set transforms (cached helper skips when matrix unchanged from last applied) */
    TRL_ApplyTransformsCached(self, world, view, proj);

    TRL_ApplyFFPDrawState(self, 1);

    /* Expanded VB contains vertices [baseVtx .. baseVtx+nv-1].
     * Rebase the original indices into [0 .. nv-1] via BaseVertexIndex=-mi. */
    ((FN_DIP)vt[SLOT_DrawIndexedPrimitive])(self->pReal, pt, -(int)mi, mi, nv, si, pc);

    /* Restore original state */
    ((FN_SetSS)vt[SLOT_SetStreamSource])(self->pReal, 0, srcVB, srcOff, srcStride);
    ((FN_SetDecl)vt[SLOT_SetVertexDeclaration])(self->pReal, origDecl);
    if (self->lastVS)
        ((FN_SetVS)vt[SLOT_SetVertexShader])(self->pReal, self->lastVS);

    return 1;
}

/* ---- Draw call cache ---- */
#if DRAW_CACHE_ENABLED
static CachedDraw s_drawCache[DRAW_CACHE_MAX];
static int s_drawCacheCount = 0;
static int s_cacheLogOnce = 0;

/* Release COM resources held by a single cache entry and mark it inactive. */
static void DrawCache_ReleaseEntry(CachedDraw *c) {
    if (!c->active) return;
    com_release(c->vb);
    com_release(c->ib);
    com_release(c->decl);
    com_release(c->tex0);
    c->vb   = NULL;
    c->ib   = NULL;
    c->decl = NULL;
    c->tex0 = NULL;
    c->active = 0;
}

/* Release all cached resources and empty the cache. */
static void DrawCache_Clear(void) {
    int i;
    for (i = 0; i < s_drawCacheCount; i++)
        DrawCache_ReleaseEntry(&s_drawCache[i]);
    s_drawCacheCount = 0;
    s_cacheLogOnce   = 0;
}

static void DrawCache_Record(WrappedDevice *self, unsigned int pt, int bvi,
    unsigned int mi, unsigned int nv, unsigned int si, unsigned int pc)
{
    void *vb = self->streamVB[0];
    void *ib = NULL;
    void *tex0 = self->curTexture[self->albedoStage];
    int i, slot = -1;

    /* Get current index buffer — GetIndices AddRefs; keep that ref as our cache ref */
    {
        typedef int (__stdcall *FN_GetIndices)(void*, void**);
        ((FN_GetIndices)RealVtbl(self)[SLOT_GetIndices])(self->pReal, &ib);
    }

    /* Find existing entry or free slot */
    for (i = 0; i < s_drawCacheCount; i++) {
        if (s_drawCache[i].active &&
            s_drawCache[i].vb == vb &&
            s_drawCache[i].ib == ib &&
            s_drawCache[i].tex0 == tex0 &&
            s_drawCache[i].pt == pt &&
            s_drawCache[i].bvi == bvi &&
            s_drawCache[i].mi == mi &&
            s_drawCache[i].si == si &&
            s_drawCache[i].pc == pc &&
            s_drawCache[i].nv == nv &&
            s_drawCache[i].streamOff == self->streamOffset[0] &&
            s_drawCache[i].streamStr == self->streamStride[0] &&
            s_drawCache[i].decl == self->lastDecl) {
            slot = i;
            break;
        }
    }
    if (slot < 0 && s_drawCacheCount < DRAW_CACHE_MAX) {
        slot = s_drawCacheCount++;
    }
    if (slot < 0) {
        /* Cache full — ib was AddRef'd by GetIndices, must release */
        com_release(ib);
        return;
    }

    {
        CachedDraw *c = &s_drawCache[slot];

        /* Release previously held resources before overwriting the slot */
        if (c->active) DrawCache_ReleaseEntry(c);

        c->vb = vb;   com_addref(c->vb);
        c->ib = ib;   /* already AddRef'd by GetIndices above */
        c->si = si;
        c->pc = pc;
        c->nv = nv;
        c->pt = pt;
        c->bvi = bvi;
        c->mi = mi;
        c->decl = self->lastDecl; com_addref(c->decl);
        c->tex0 = tex0; com_addref(c->tex0);
        c->streamOff = self->streamOffset[0];
        c->streamStr = self->streamStride[0];
        c->isShort4 = (self->curDeclPosType == D3DDECLTYPE_SHORT4) ? 1 : 0;
        c->posOff = self->s4PosOff;
        c->isViewSpace = mat4_approx_equal(self->savedView, s_identity4x4, 1e-6f);
        c->lastSeenFrame = self->frameCount;
        c->active = 1;
        memcpy(c->world, self->savedWorld, 64);
    }
}

static void DrawCache_Replay(WrappedDevice *self) {
    typedef int (__stdcall *FN_SetTransform)(void*, unsigned int, float*);
    typedef int (__stdcall *FN_SetStreamSrc)(void*, unsigned int, void*, unsigned int, unsigned int);
    typedef int (__stdcall *FN_SetIndices)(void*, void*);
    typedef int (__stdcall *FN_SetTexture)(void*, unsigned int, void*);
    typedef int (__stdcall *FN_SetDecl)(void*, void*);
    typedef int (__stdcall *FN_SetVS)(void*, void*);
    typedef int (__stdcall *FN_DIP)(void*, unsigned int, int, unsigned int, unsigned int, unsigned int, unsigned int);
    void **vt = RealVtbl(self);
    float view[16], proj[16];
    float identityView[16];
    const float *gameView = (const float *)TRL_VIEW_MATRIX_ADDR;
    const float *gameProj = (const float *)TRL_PROJ_MATRIX_ADDR;
    int i, replayed = 0;
    float savedTex0Scroll[4];
    float savedTex0Transform[16];
    void *savedTex0VS;
    int savedTex0ScrollValid;
    int savedTex0TransformValid;

    /* Read current view/proj from game memory */
    for (i = 0; i < 16; i++) { view[i] = gameView[i]; proj[i] = gameProj[i]; }
    mat4_identity(identityView);

    for (i = 0; i < 4; i++)
        savedTex0Scroll[i] = self->tex0ScrollConst[i];
    for (i = 0; i < 16; i++)
        savedTex0Transform[i] = self->tex0TransformConst[i];
    savedTex0VS = self->tex0TransformVS;
    savedTex0ScrollValid = self->tex0ScrollValid;
    savedTex0TransformValid = self->tex0TransformValid;
    TRL_ResetTexture0AnimationState(self);
    TRL_ClearTexture0TransformState(self);

    for (i = 0; i < s_drawCacheCount; i++) {
        CachedDraw *c = &s_drawCache[i];
        float *viewForDraw;
        /* Replay draws that were active recently but missing this frame */
        if (!c->active) continue;
        if (c->lastSeenFrame == self->frameCount) continue; /* seen this frame, skip */
        if (self->frameCount - c->lastSeenFrame > 120) {
            DrawCache_ReleaseEntry(c); /* stale — release COM refs and mark inactive */
            continue;
        }
        /* Skip if resources are gone (VB/IB could be freed) */
        if (!c->vb || !c->ib || !c->decl) continue;
        viewForDraw = c->isViewSpace ? identityView : view;

        /* Restore state and replay */
        ((FN_SetIndices)vt[SLOT_SetIndices])(self->pReal, c->ib);
        if (c->tex0) ((FN_SetTexture)vt[SLOT_SetTexture])(self->pReal, 0, c->tex0);

        if (c->isShort4 && c->vb && c->decl) {
            /* SHORT4 draw: expand and draw in FFP mode */
            S4_ExpandAndDraw(self, c->vb, c->streamOff, c->streamStr, c->decl, c->posOff,
                c->pt, c->bvi, c->mi, c->nv, c->si, c->pc, c->world, viewForDraw, proj);
        } else {
            ((FN_SetTransform)vt[SLOT_SetTransform])(self->pReal, D3DTS_WORLD, c->world);
            ((FN_SetTransform)vt[SLOT_SetTransform])(self->pReal, D3DTS_VIEW, viewForDraw);
            ((FN_SetTransform)vt[SLOT_SetTransform])(self->pReal, D3DTS_PROJECTION, proj);
            ((FN_SetStreamSrc)vt[SLOT_SetStreamSource])(self->pReal, 0, c->vb, c->streamOff, c->streamStr);
            if (c->decl) ((FN_SetDecl)vt[SLOT_SetVertexDeclaration])(self->pReal, c->decl);
            ((FN_SetVS)vt[SLOT_SetVertexShader])(self->pReal, NULL);
            TRL_ApplyFFPDrawState(self, 0);
            ((FN_DIP)vt[SLOT_DrawIndexedPrimitive])(self->pReal, c->pt, c->bvi, c->mi, c->nv, c->si, c->pc);
        }
        replayed++;
    }

    if (self->lastVS)
        ((FN_SetVS)vt[SLOT_SetVertexShader])(self->pReal, self->lastVS);

    if (!s_cacheLogOnce && replayed > 0) {
        log_int("DrawCache: replayed ", replayed);
        log_str(" culled draws\r\n");
        s_cacheLogOnce = 1;
    }

    self->tex0TransformVS = savedTex0VS;
    self->tex0ScrollValid = savedTex0ScrollValid;
    self->tex0TransformValid = savedTex0TransformValid;
    for (i = 0; i < 4; i++)
        self->tex0ScrollConst[i] = savedTex0Scroll[i];
    for (i = 0; i < 16; i++)
        self->tex0TransformConst[i] = savedTex0Transform[i];
}
#endif /* DRAW_CACHE_ENABLED */

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
        PinnedDraw_ReleaseAll(self);
        /* Release S4 expanded VBs — owned COM refs, must Release before HeapFree */
        S4_FlushVBCache(self);
        SkyIso_Clear(self);
        /* Clear draw cache — releases COM refs held for anchor mesh replay */
#if DRAW_CACHE_ENABLED
        DrawCache_Clear();
#endif
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

    /* Pinned draws hold device resources — release before Reset */
    PinnedDraw_ReleaseAll(self);
    /* Release S4 expanded VBs — they hold D3DPOOL_MANAGED resources invalidated by Reset */
    S4_FlushVBCache(self);
    /* Clear draw cache — releases COM refs before Reset invalidates device resources */
#if DRAW_CACHE_ENABLED
    DrawCache_Clear();
#endif
    self->pinnedCaptureComplete = 0;

    shader_release(self->lastVS);
    shader_release(self->lastPS);
    self->lastVS = NULL;
    self->lastPS = NULL;
    self->viewProjValid = 0;
    self->ffpSetup = 0;
    self->worldDirty = 0;
    self->viewProjDirty = 0;
    /* Invalidate cached "applied" transforms — device resource state is reset */
    {
        int z;
        for (z = 0; z < 16; z++) { self->appliedWorld[z] = 0.0f; self->appliedView[z] = 0.0f; self->appliedProj[z] = 0.0f; }
    }
    self->psConstDirty = 0;
    self->ffpActive = 0;
    self->vpInverseValid = 0;
    TRL_ResetTexture0AnimationState(self);
    SkyIso_Clear(self);
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

#if DIAG_ENABLED
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
#endif

    /* Close capture window after PINNED_CAPTURE_FRAMES */
    if (self->frameCount == PINNED_CAPTURE_FRAMES && !self->pinnedCaptureComplete) {
        self->pinnedCaptureComplete = 1;
        log_str("== PinnedDraw: capture complete, ");
        log_int("", self->pinnedDrawCount);
        log_str(" unique draws cached\r\n");
    }

    /* Replay pinned draws not submitted this frame (after capture window,
     * every PINNED_REPLAY_INTERVAL frames to keep anti-culling alive) */
    if (self->pinnedCaptureComplete &&
        self->frameCount % PINNED_REPLAY_INTERVAL == 0)
    {
        PinnedDraw_ReplayMissing(self);
    }

    self->frameCount++;

#if PERF_LOG_ENABLED
    /* Emit one FPS sample every PERF_LOG_INTERVAL frames. Cost: 1 GetTickCount per interval. */
    if ((self->frameCount - self->perfLastFrame) >= PERF_LOG_INTERVAL) {
        unsigned int nowTick = GetTickCount();
        unsigned int dtMs = nowTick - self->perfLastTick;
        unsigned int frames = self->frameCount - self->perfLastFrame;
        unsigned int fps = (dtMs > 0) ? (frames * 1000u) / dtMs : 0u;
        log_str("PERF ");
        log_int("frames=", (int)frames);
        log_int(" ms=", (int)dtMs);
        log_int(" fps=", (int)fps);
        log_str("\r\n");
        self->perfLastTick = nowTick;
        self->perfLastFrame = self->frameCount;
    }
#endif

    self->ffpSetup = 0;
    self->ffpActive = 0;
    self->drawCallCount = 0;
    self->sceneCount = 0;
    self->drawsProcessed = 0;
    self->drawsSkippedQuad = 0;
    self->drawsPassthrough = 0;
    self->drawsTotal = 0;
    self->transformsBlocked = 0;
#if DIAG_ENABLED
    if (DIAG_ACTIVE(self)) {
        int r;
        for (r = 0; r < 256; r++) self->vsConstWriteLog[r] = 0;
    }
#endif

    /* Reset per-frame submission tracking for pinned draws */
    PinnedDraw_FrameReset(self);

    hr = ((FN)RealVtbl(self)[SLOT_Present])(self->pReal, a, b, c, d);

    return hr;
}

static void TRL_ApplyMemoryPatches(WrappedDevice *self);

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
    self->skySceneCounter++;

    /* Defer memory patches until a real gameplay camera exists. The front-end
     * Lara scene writes VS constants too, but keeps the raw game View matrix
     * at identity and is not safe for level-only traversal patches. */
    if (self->levelSceneDetected && self->viewProjValid &&
        TRL_HasGameplayCamera() && !self->memoryPatchesApplied) {
        log_int("TRL: applying gameplay memory patches at scene=", (int)self->sceneCount);
        log_str("\r\n");
        TRL_ApplyMemoryPatches(self);
        /* Flush draws cached during loading screens — they carry wrong world matrices
         * for the gameplay scene. Also releases their COM refs so freed menu resources
         * aren't replayed after the transition. */
#if DRAW_CACHE_ENABLED
        DrawCache_Clear();
#endif
    } else if (!self->levelSceneDetected && !self->memoryPatchesApplied &&
               self->sceneCount > 500 && self->sceneCount < 1500 &&
               (self->sceneCount % 120) == 0) {
        log_int("TRL: memory patches deferred until level latch scene=", (int)self->sceneCount);
        log_str("\r\n");
    }

    /* Re-stamp game globals every scene, but only after patches are applied
     * (data pages are permanently unlocked by TRL_ApplyMemoryPatches). */
    if (self->memoryPatchesApplied) {
        *(float*)TRL_FRUSTUM_THRESHOLD_ADDR = -1e30f;
        *(float*)0x010FC910 = 1e30f;                          /* far clip */
        *(unsigned int*)TRL_CULL_MODE_PASS1_ADDR     = 1;     /* D3DCULL_NONE */
        *(unsigned int*)TRL_CULL_MODE_PASS2_ADDR     = 1;
        *(unsigned int*)TRL_CULL_MODE_PASS2_INV_ADDR = 1;
        *(unsigned int*)TRL_LIGHT_CULLING_DISABLE_FLAG = 1;
        *(unsigned int*)TRL_RENDER_FLAGS_ADDR &= ~0x00100000u; /* clear bit 20 */
        *(unsigned char*)TRL_POSTSECTOR_ENABLE_ADDR = 1;
        *(unsigned int*)TRL_POSTSECTOR_GATE_ADDR = 0;
        *(unsigned int*)TRL_POSTSECTOR_SECTOR_BITS_ADDR = 0xFFFFFFFF;
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
    unsigned int sceneDrawsProcessed = self->drawsProcessed;

#if DIAG_ENABLED
    /* Log per-scene draw counts for culling investigation.
     * Start after scene 500 (past startup), log every 2nd scene for 1000 scenes. */
    if (self->drawsTotal > 0 && self->sceneCount > 500 && self->sceneCount < 1500 && (self->sceneCount % 2) == 0) {
        log_int("S", self->sceneCount);
        log_int(" d=", self->drawsProcessed);
        log_int(" s4=", self->drawsS4);
        log_int(" f3=", self->drawsF3);
        log_int(" p=", self->drawsPassthrough);
        log_int(" q=", self->transformsBlocked);
        log_str("\r\n");
    }
#endif
    if (!self->levelSceneDetected && TRL_IsMenuLikeScene(self)) {
        if (!self->sawMenuScene)
            TRL_LogTransitionScene(self, "Transition: front-end scene observed");
        self->sawMenuScene = 1;
    } else if (!self->levelSceneDetected && self->sawMenuScene &&
               TRL_IsTransitionBlipScene(self)) {
        if (!self->sawTransitionBlip)
            TRL_LogTransitionScene(self, "Transition: handoff blip observed");
        self->sawTransitionBlip = 1;
    } else if (!self->levelSceneDetected && TRL_IsGameplayReadyScene(self) &&
               (self->sawMenuScene || self->sawTransitionBlip)) {
        TRL_OnLevelSceneDetected(self);
    } else if (!self->levelSceneDetected && self->sawMenuScene &&
               TRL_IsGameplayReadyScene(self)) {
        TRL_LogTransitionScene(self, "Transition: gameplay-like scene seen before latch");
    }

#if DIAG_ENABLED
    if (self->levelSceneDetected && self->drawsTotal > 0 &&
        self->postLatchSceneLogsRemaining > 0) {
        log_int("PostLatch scene=", (int)self->sceneCount);
        log_int(" d=", (int)self->drawsProcessed);
        log_int(" s4=", (int)self->drawsS4);
        log_int(" f3=", (int)self->drawsF3);
        log_int(" replay=", self->replayFeaturesEnabled);
        log_int(" patches=", self->memoryPatchesApplied);
        log_str("\r\n");
        self->postLatchSceneLogsRemaining--;
    }
#endif
    self->lastSceneDrawsProcessed = sceneDrawsProcessed;
    /* Reset per-scene draw counters */
    if (self->drawsTotal > 0) {
        self->drawsProcessed = 0;
        self->drawsSkippedQuad = 0;
        self->drawsPassthrough = 0;
        self->drawsTotal = 0;
        self->transformsBlocked = 0;
        self->drawsS4 = 0;
        self->drawsF3 = 0;
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

    /* Replay culled draws before ending the scene */
#if DRAW_CACHE_ENABLED
    if (self->replayFeaturesEnabled && self->viewProjValid && self->frameCount > 60) {
        DrawCache_Replay(self);
    }
#endif

    self->frameCount++;
    return ((FN)RealVtbl(self)[SLOT_EndScene])(self->pReal);
}

/* 81: DrawPrimitive — GAME-SPECIFIC draw routing for non-indexed draws */
static int __stdcall WD_DrawPrimitive(WrappedDevice *self, unsigned int pt, unsigned int sv, unsigned int pc) {
    typedef int (__stdcall *FN)(void*, unsigned int, unsigned int, unsigned int);
    int hr;
    int animatedTex0 = 0;
    int float3Route = FLOAT3_ROUTE_NULL_VS;
    int short4ShaderRoute = 0;
    void *skyIsoOrigTex = NULL;
    SkyIsoEntry *skyIsoEntry = NULL;
    unsigned int skyStage = (unsigned int)self->albedoStage;
    unsigned int nv = SkyIso_EstimateVertexCount(pt, pc);
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
            typedef int (__stdcall *FN_SetVS)(void*, void*);
            void **vt = RealVtbl(self);

            TRL_PrepDraw(self);
            animatedTex0 = TRL_DrawHasAnimatedTexture0(self);
            if (self->curDeclPosType == D3DDECLTYPE_FLOAT3)
                float3Route = TRL_GetEffectiveFloat3Route(self);
            if (animatedTex0 && self->curDeclPosType == D3DDECLTYPE_FLOAT3)
                float3Route = FLOAT3_ROUTE_SHADER;
            short4ShaderRoute = TRL_ShouldShaderRouteAnimatedShort4Draw(self, animatedTex0);
            skyIsoEntry = SkyIso_ObserveDraw(self, skyStage, nv, pc, animatedTex0);
            if (self->curDeclPosType == D3DDECLTYPE_FLOAT3)
                skyIsoOrigTex = SkyIso_BindForDraw(self, skyStage, skyIsoEntry, nv, pc, animatedTex0);
            if (self->curDeclPosType == D3DDECLTYPE_FLOAT3 &&
                float3Route == FLOAT3_ROUTE_SHADER) {
                /* Keep animated TEX0 FLOAT3 draws on the shader path so water motion survives. */
                hr = ((FN)vt[SLOT_DrawPrimitive])(self->pReal, pt, sv, pc);
            } else if (short4ShaderRoute) {
                /* Preserve shader-driven UV scroll or mesh deformation on animated SHORT4 draws. */
                hr = ((FN)vt[SLOT_DrawPrimitive])(self->pReal, pt, sv, pc);
            } else {
                /* Legacy null-VS fallback path. */
                ((FN_SetVS)vt[SLOT_SetVertexShader])(self->pReal, NULL);
                TRL_ApplyFFPDrawState(self, 1);
                hr = ((FN)vt[SLOT_DrawPrimitive])(self->pReal, pt, sv, pc);
                if (self->lastVS)
                    ((FN_SetVS)vt[SLOT_SetVertexShader])(self->pReal, self->lastVS);
            }

            if (skyIsoOrigTex)
                SkyIso_RestoreAfterDraw(self, skyStage, skyIsoOrigTex);

            if (self->curDeclPosType == D3DDECLTYPE_SHORT4)
                self->drawsS4++;
            else if (self->curDeclPosType == D3DDECLTYPE_FLOAT3)
                self->drawsF3++;
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
    int animatedTex0 = 0;
    int float3Route = FLOAT3_ROUTE_NULL_VS;
    int shaderRouteFloat3 = 0;
    int shaderRouteShort4 = 0;
    void *skyIsoOrigTex = NULL;
    SkyIsoEntry *skyIsoEntry = NULL;
    unsigned int skyStage = (unsigned int)self->albedoStage;
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
            animatedTex0 = TRL_DrawHasAnimatedTexture0(self);
            if (self->curDeclPosType == D3DDECLTYPE_FLOAT3)
                float3Route = TRL_GetEffectiveFloat3Route(self);
            if (animatedTex0 && self->curDeclPosType == D3DDECLTYPE_FLOAT3)
                float3Route = FLOAT3_ROUTE_SHADER;
            if (self->curDeclPosType == D3DDECLTYPE_SHORT4)
                shaderRouteShort4 = TRL_ShouldShaderRouteAnimatedShort4Draw(self, animatedTex0);
            skyIsoEntry = SkyIso_ObserveDraw(self, skyStage, nv, pc, animatedTex0);

            if (self->curDeclPosType == D3DDECLTYPE_FLOAT3)
                skyIsoOrigTex = SkyIso_BindForDraw(self, skyStage, skyIsoEntry, nv, pc, animatedTex0);

            /* SHORT4 positions: expand to FLOAT3 on CPU, draw in FFP mode.
             * This eliminates useVertexCapture and gives stable position hashes. */
            if (self->curDeclPosType == D3DDECLTYPE_SHORT4 &&
                !shaderRouteShort4 &&
                self->streamVB[0] && self->lastDecl) {
                if (!S4_ExpandAndDraw(self, self->streamVB[0], self->streamOffset[0],
                        self->streamStride[0], self->lastDecl, self->s4PosOff,
                        pt, bvi, mi, nv, si, pc,
                        self->savedWorld, self->savedView, self->savedProj)) {
                    /* Fallback: draw as-is */
                    hr = ((FN)RealVtbl(self)[SLOT_DrawIndexedPrimitive])(self->pReal, pt, bvi, mi, nv, si, pc);
                } else {
                    hr = 0;
                }
                self->drawsS4++;
            } else {
                if ((self->curDeclPosType == D3DDECLTYPE_FLOAT3 && float3Route == FLOAT3_ROUTE_SHADER) ||
                    shaderRouteShort4) {
                    /* Shader route: keep VS/PS bound for FLOAT3 draws and for animated/deformed
                     * SHORT4 draws that rely on shader-side UV scroll, morphing, or extra streams. */
                    hr = ((FN)RealVtbl(self)[SLOT_DrawIndexedPrimitive])(self->pReal, pt, bvi, mi, nv, si, pc);
                    if (self->curDeclPosType == D3DDECLTYPE_FLOAT3)
                        shaderRouteFloat3 = 1;
                } else {
                    /* Legacy null-VS fallback for FLOAT3 when vertex capture is off (or forced). */
                    typedef int (__stdcall *FN_SetVS)(void*, void*);
                    void **vt = RealVtbl(self);
                    ((FN_SetVS)vt[SLOT_SetVertexShader])(self->pReal, NULL);
                    TRL_ApplyFFPDrawState(self, 1);
                    hr = ((FN)vt[SLOT_DrawIndexedPrimitive])(self->pReal, pt, bvi, mi, nv, si, pc);
                    if (self->lastVS)
                        ((FN_SetVS)vt[SLOT_SetVertexShader])(self->pReal, self->lastVS);
                }
                if (self->curDeclPosType == D3DDECLTYPE_SHORT4)
                    self->drawsS4++;
                else
                    self->drawsF3++;
            }

            if (skyIsoOrigTex)
                SkyIso_RestoreAfterDraw(self, skyStage, skyIsoOrigTex);

#if DRAW_CACHE_ENABLED
            if (self->replayFeaturesEnabled && !animatedTex0 && !shaderRouteFloat3 && !shaderRouteShort4)
                DrawCache_Record(self, pt, bvi, mi, nv, si, pc);
#endif
            self->drawsProcessed++;

            /* Draw call replay cache: skip shader-route draws to avoid stale replays. */
            if (self->replayFeaturesEnabled &&
                !animatedTex0 && !shaderRouteFloat3 && !shaderRouteShort4 &&
                self->frameCount < PINNED_CAPTURE_FRAMES && !self->pinnedCaptureComplete) {
                PinnedDraw *pd;
                int idx = self->pinnedDrawCount; /* will be set if capture succeeds */
                PinnedDraw_Capture(self);
                /* If capture just added a new entry, fill in the draw params */
                if (self->pinnedDrawCount > idx) {
                    pd = &self->pinnedDraws[idx];
                    pd->primType = pt;
                    pd->baseVertexIndex = bvi;
                    pd->minVertexIndex = mi;
                    pd->numVertices = nv;
                    pd->startIndex = si;
                    pd->primCount = pc;
                }
            }
            if (self->replayFeaturesEnabled && !animatedTex0 && !shaderRouteFloat3 && !shaderRouteShort4)
                PinnedDraw_MarkSubmitted(self);
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
    unsigned int endReg = startReg + count;
    unsigned int reg;
    unsigned int j;

    if (pData && startReg + count <= 256) {
        for (i = 0; i < count * 4; i++) {
            self->vsConst[(startReg * 4) + i] = pData[i];
        }

        if (self->lastVS) {
            if (startReg < VS_REG_TEX0_XFORM_END && endReg > VS_REG_TEX0_XFORM_START) {
                unsigned int copyStart = startReg;
                unsigned int copyEnd = endReg;
                if (copyStart < VS_REG_TEX0_XFORM_START) copyStart = VS_REG_TEX0_XFORM_START;
                if (copyEnd > VS_REG_TEX0_XFORM_END) copyEnd = VS_REG_TEX0_XFORM_END;
                for (reg = copyStart; reg < copyEnd; reg++) {
                    unsigned int dstBase = (reg - VS_REG_TEX0_XFORM_START) * 4;
                    unsigned int srcBase = (reg - startReg) * 4;
                    for (j = 0; j < 4; j++)
                        self->tex0TransformConst[dstBase + j] = pData[srcBase + j];
                }
                self->tex0TransformValid = 1;
                self->tex0TransformVS = self->lastVS;
            }
        }

        /* Dirty tracking: WVP (c0-c3), World (c4-c7), or ViewProject (c12-c15) triggers recompute */
        {
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

#if DIAG_ENABLED
        if (DIAG_ACTIVE(self)) {
            for (i = 0; i < count; i++) {
                if (startReg + i < 256)
                    self->vsConstWriteLog[startReg + i] = 1;
            }
        }
#endif

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
    self->curDeclUsesAuxStreams = 0;
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
                    if (stream != 0)
                        self->curDeclUsesAuxStreams = 1;

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
                        self->s4PosOff = offset;
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

                /* Normal stripping DISABLED for hash stability testing.
                 * Remix may assert on non-FLOAT3 normals but the geometry descriptor
                 * hash must match what was used when lights were placed. */
                (void)hasNonFloat3Normal;

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
/* Verify we're running against the expected TRL executable (Steam PC, v1.0).
 * Checks a known code signature at 0x401000 to prevent memory patches from
 * writing to wrong addresses on different game versions (GOG, console ports,
 * updated executables). Returns 1 if matched, 0 otherwise. */
static int TRL_VerifyGameVersion(void) {
    /* First 4 bytes at .text entry (0x401000) — unique to Steam v1.0 build */
    static const unsigned char expected[] = { 0xA0, 0xB4, 0x9D, 0x15 };
    DWORD oldProtect;
    unsigned char *p = (unsigned char *)0x00401000;
    int i;

    if (!VirtualProtect(p, 4, PAGE_EXECUTE_READ, &oldProtect))
        return 0;

    for (i = 0; i < 4; i++) {
        if (p[i] != expected[i]) {
            log_str("WARNING: Game version mismatch at 0x401000 — patches disabled\r\n");
            log_hex("  Expected: ", *(unsigned int*)expected);
            log_hex("  Found:    ", *(unsigned int*)p);
            return 0;
        }
    }

    /* Secondary check: PE image size from optional header.
     * Steam v1.0 trl.exe has SizeOfImage = 0x1175000 (18,305,024 bytes). */
    {
        unsigned char *base = (unsigned char *)0x00400000;
        unsigned int peOff = *(unsigned int *)(base + 0x3C);
        unsigned int sizeOfImage = *(unsigned int *)(base + peOff + 0x50);
        if (sizeOfImage != 0x01175000) {
            log_str("WARNING: PE SizeOfImage mismatch — patches disabled\r\n");
            log_hex("  Expected: ", 0x01175000);
            log_hex("  Got:      ", sizeOfImage);
            return 0;
        }
    }

    log_str("Game version verified: TRL Steam v1.0\r\n");
    return 1;
}

static void TRL_ApplyMemoryPatches(WrappedDevice *self) {
    DWORD oldProtect;

    if (self->memoryPatchesApplied) return;
    self->memoryPatchesApplied = 1;

    if (!TRL_VerifyGameVersion()) {
        log_str("SKIPPING all memory patches — version check failed\r\n");
        return;
    }

    /* Frustum threshold: set to -1e30 so distance-based culling never triggers.
     * The game skips objects when distance <= threshold. Negative infinity catches
     * objects behind the camera that 0.0 would still cull. */
    if (VirtualProtect((void*)TRL_FRUSTUM_THRESHOLD_ADDR, 4, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        *(float*)TRL_FRUSTUM_THRESHOLD_ADDR = -1e30f;
        VirtualProtect((void*)TRL_FRUSTUM_THRESHOLD_ADDR, 4, oldProtect, &oldProtect);
        log_str("  Patched frustum threshold to -1e30\r\n");
    }

    /* NOP scene traversal cull jumps inside 0x407150 (SceneTraversal_CullAndSubmit).
     * These conditional jumps skip geometry based on distance, screen boundary,
     * object flags, far clip, and draw distance checks.
     * Each is a 6-byte conditional near jump (0x0F 0x8x ...) replaced with 6x NOP. */
    {
        static const unsigned int cullJumpAddrs[] = {
            0x004072BD,  /* distance cull jump 1 (Phase A) */
            0x004072D2,  /* distance cull jump 2 (Phase A) */
            0x00407AF1,  /* depth cull jump (Phase B) */
            0x00407B30,  /* screen boundary jump 1 */
            0x00407B49,  /* screen boundary jump 2 */
            0x00407B62,  /* screen boundary jump 3 */
            0x00407B7B,  /* screen boundary jump 4 */
            0x004071CE,  /* object disable flag (Phase A) — bit 0x10 at [node+8] */
            0x00407976,  /* object disable flag (Phase B) — bit 0x10 at [obj+8] */
            0x00407B06,  /* far clip distance rejection */
            0x00407ABC,  /* draw distance fade-out rejection */
        };
        int nopCount = 0;
        int i;
        for (i = 0; i < 11; i++) {
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
        log_str("/11\r\n");
    }

    /* Null-check trampoline for SceneTraversal_CullAndSubmit.
     * At 0x4071D9: mov edx,[ebx+0x8C] loads RenderContext* which can be NULL.
     * At 0x4071E2: mov [edx+0x40],eax crashes when edx=NULL.
     * Solution: redirect 0x4071D9 to code cave at 0xEDF9E3 which does the
     * mov, tests for NULL, and either continues (0x4071DF) or skips to
     * next node (0x4078CD). This lets the function run and submit geometry
     * while safely handling nodes without a RenderContext. */
    {
        /* Code cave at 0xEDF9E3 (29 bytes of INT3 padding in .text):
         *   mov edx, [ebx+0x8C]   ; displaced original instruction
         *   test edx, edx          ; null check
         *   jnz +5                 ; if not null, continue normal path
         *   jmp 0x4078CD           ; null -> skip to next node
         *   jmp 0x4071DF           ; not null -> return to normal flow
         */
        unsigned char cave[] = {
            0x8B, 0x93, 0x8C, 0x00, 0x00, 0x00,  /* mov edx, [ebx+0x8C] */
            0x85, 0xD2,                            /* test edx, edx */
            0x75, 0x05,                            /* jnz +5 (skip the null jmp) */
            0xE9, 0x00, 0x00, 0x00, 0x00,          /* jmp 0x4078CD (skip node) */
            0xE9, 0x00, 0x00, 0x00, 0x00           /* jmp 0x4071DF (continue) */
        };
        /* Compute relative jump targets */
        unsigned int caveAddr = 0xEDF9E3;
        unsigned int skipTarget = 0x4078CD;
        unsigned int contTarget = 0x4071DF;
        /* jmp skip: from caveAddr+10+5=caveAddr+15 to skipTarget */
        *(int*)(cave + 11) = (int)(skipTarget - (caveAddr + 15));
        /* jmp continue: from caveAddr+15+5=caveAddr+20 to contTarget */
        *(int*)(cave + 16) = (int)(contTarget - (caveAddr + 20));

        unsigned char *p;
        /* Write the code cave */
        p = (unsigned char *)caveAddr;
        if (VirtualProtect(p, sizeof(cave), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            int ci;
            for (ci = 0; ci < (int)sizeof(cave); ci++) p[ci] = cave[ci];
            VirtualProtect(p, sizeof(cave), oldProtect, &oldProtect);
        }
        /* Patch call site: replace 6-byte mov with jmp to cave + nop */
        p = (unsigned char *)0x4071D9;
        if (VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0] = 0xE9;  /* jmp rel32 */
            *(int*)(p + 1) = (int)(caveAddr - (0x4071D9 + 5));
            p[5] = 0x90;  /* nop (pad to 6 bytes) */
            VirtualProtect(p, 6, oldProtect, &oldProtect);
        }
        log_str("  Patched 0x4071D9: null-check trampoline via cave at 0xEDF9E3\r\n");
    }

    /* ProcessPendingRemovals (0x436680) crashes on stale field_48 pointers
     * when culling is disabled — nodes with freed/garbage field_48 reach
     * `test byte ptr [eax+0xA4], 0x20` and fault. Patch the JE at 0x436740
     * and 0x4367CD to unconditional JMP, skipping the dereference. Cleanup
     * still works through the secondary path via [esi+8] & 2. */
    {
        unsigned char *p;
        p = (unsigned char *)0x436740;
        if (VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0] = 0xEB;  /* je -> jmp (skip field_48 deref, List 2) */
            VirtualProtect(p, 1, oldProtect, &oldProtect);
        }
        p = (unsigned char *)0x4367CD;
        if (VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0] = 0xEB;  /* je -> jmp (skip field_48 deref, List 3) */
            VirtualProtect(p, 1, oldProtect, &oldProtect);
        }
        log_str("  Patched ProcessPendingRemovals: skip stale field_48 deref\r\n");
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

    /* NOP the camera-sector proximity filter in RenderSector (0x46B7D0).
     * The 2-byte JNE at 0x46B85A skips objects that don't have flag 0x200000
     * when they are NOT in the camera's current sector. This is the root cause
     * of Remix-anchored geometry vanishing when Lara walks to another sector.
     * The other flag checks (disabled=0x01, hidden=0x20000) are safety guards
     * that prevent crashes — leave those intact. */
    {
        unsigned char *p = (unsigned char *)0x0046B85A;
        if (VirtualProtect(p, 2, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0] = 0x90; p[1] = 0x90;
            VirtualProtect(p, 2, oldProtect, &oldProtect);
            log_str("  NOPed sector-object camera proximity filter at 0x0046B85A\r\n");
        }
    }

    /* NOP sector already-rendered skip in Sector_RenderMeshes (0x46B7D0).
     * The 6-byte JE at 0x46B7F2 skips entire sectors when render-pass state
     * matches the current sector — prevents re-rendering sectors that the
     * portal traversal already visited. NOP to force all sectors to always
     * submit their objects. */
    {
        unsigned char *p = (unsigned char *)0x0046B7F2;
        if (VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0] = 0x90; p[1] = 0x90; p[2] = 0x90;
            p[3] = 0x90; p[4] = 0x90; p[5] = 0x90;
            VirtualProtect(p, 6, oldProtect, &oldProtect);
            log_str("  NOPed sector already-rendered skip at 0x0046B7F2\r\n");
        }
    }

    /* NOP frustum screen-size rejection in Sector_IterateMeshArray.
     * Two 2-byte JNP instructions at 0x46C242 and 0x46C25B skip sectors
     * whose screen-space projection is smaller than thresholds at
     * [0x10FC920] and [0x10FC924]. This is the primary distance-based
     * culling gate — sectors that are far away project small on screen
     * and get rejected, even though they're forced visible by our other
     * patches. NOP both to force all sectors through regardless of size.
     *
     * A null-check is needed: sectors without loaded mesh data will crash
     * in Sector_RenderMeshes. Add a guard by checking the sector's mesh
     * count at [sectorData+0x14] before the render call at 0x46C26B. */
    {
        unsigned char *p;
        p = (unsigned char *)0x0046C242;
        if (VirtualProtect(p, 2, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0] = 0x90; p[1] = 0x90;
            VirtualProtect(p, 2, oldProtect, &oldProtect);
        }
        p = (unsigned char *)0x0046C25B;
        if (VirtualProtect(p, 2, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0] = 0x90; p[1] = 0x90;
            VirtualProtect(p, 2, oldProtect, &oldProtect);
        }
        log_str("  NOPed frustum screen-size rejection at 0x0046C242 and 0x0046C25B\r\n");
    }

    /* NOTE: Per-object flag NOPs at 0x46B83C (bit 0, hidden) and 0x46B844
     * (bit 17, 0x20000 cull) cause crash at 0x40C43F — these flags guard
     * objects with NULL data pointers. Allowing them through crashes in
     * Sector_SubmitObject when it dereferences [ecx+4] with ecx=0.
     * Need a null-check trampoline like the SceneTraversal one. */

    /* SectorPortalVisibility bounds persistence (0x46D1D0): prevent the per-frame
     * reset of sector bounding rects to inverted (invisible) values. The reset
     * loop writes 6 fields per sector: x=512, y=448, w=-512, h=-448, flags=0, ?=0.
     * NOP the 6 write instructions in the loop body so sectors keep whatever
     * bounds they had from their last portal-visible frame. The portal walk still
     * overwrites portal-reachable sectors with proper bounds — only unreachable
     * sectors benefit from keeping their previous (valid) bounds.
     *
     * NOTE: fullscreen bounds (Patch A) caused black screen — bounds are used as
     * clip rects downstream. Persistence avoids this by keeping the portal walk's
     * natural bounds for each sector.
     *
     * Loop body writes to NOP (23 bytes total):
     *   0x46D1F1: 66 89 50 FE       mov [eax-2], dx     (x = 0x200)
     *   0x46D1F5: 66 C7 00 C0 01    mov [eax], 0x1C0    (y = 0x1C0)
     *   0x46D1FA: 66 89 78 02       mov [eax+2], di     (w = -512)
     *   0x46D1FE: 66 89 70 04       mov [eax+4], si     (h = -448)
     *   0x46D202: 89 68 06          mov [eax+6], ebp    (flags = 0)
     *   0x46D205: 89 68 0A          mov [eax+0xA], ebp  (flags2 = 0)
     * Loop counter/increment (0x46D208+) stays intact. */
    {
        unsigned char *p;
        int noped = 0;

        /* 0x46D1F1: 66 89 50 FE — mov [eax-2], dx (4 bytes) */
        p = (unsigned char *)0x0046D1F1;
        if (VirtualProtect(p, 4, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0]=0x90; p[1]=0x90; p[2]=0x90; p[3]=0x90;
            VirtualProtect(p, 4, oldProtect, &oldProtect); noped++;
        }
        /* 0x46D1F5: 66 C7 00 C0 01 — mov [eax], 0x1C0 (5 bytes) */
        p = (unsigned char *)0x0046D1F5;
        if (VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0]=0x90; p[1]=0x90; p[2]=0x90; p[3]=0x90; p[4]=0x90;
            VirtualProtect(p, 5, oldProtect, &oldProtect); noped++;
        }
        /* 0x46D1FA: 66 89 78 02 — mov [eax+2], di (4 bytes) */
        p = (unsigned char *)0x0046D1FA;
        if (VirtualProtect(p, 4, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0]=0x90; p[1]=0x90; p[2]=0x90; p[3]=0x90;
            VirtualProtect(p, 4, oldProtect, &oldProtect); noped++;
        }
        /* 0x46D1FE: 66 89 70 04 — mov [eax+4], si (4 bytes) */
        p = (unsigned char *)0x0046D1FE;
        if (VirtualProtect(p, 4, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0]=0x90; p[1]=0x90; p[2]=0x90; p[3]=0x90;
            VirtualProtect(p, 4, oldProtect, &oldProtect); noped++;
        }
        /* 0x46D202 and 0x46D205: flags/flags2 clear — NOT patched.
         * These zero the sector portal-visible flags each frame. Without
         * them, stale flags from a previous chapter persist and cause the
         * portal walk to dereference freed sector data on chapter reload. */

        log_str("  NOPed SectorPortalVisibility reset writes: ");
        log_int("", noped);
        log_str("/4 (bounds NOPed, flags clear preserved)\r\n");
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

    /* Light_VisibilityTest: force always-TRUE so all lights pass visibility.
     * Patch: mov al, 1; ret 4 (5 bytes). This ensures lights anchored to
     * mesh hashes remain visible regardless of camera distance/angle. */
    {
        unsigned char *p = (unsigned char *)TRL_LIGHT_VISIBILITY_TEST_ADDR;
        if (VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0] = 0xB0; p[1] = 0x01;  /* mov al, 1 */
            p[2] = 0xC2; p[3] = 0x04; p[4] = 0x00;  /* ret 4 */
            VirtualProtect(p, 5, oldProtect, &oldProtect);
            log_str("  Patched Light_VisibilityTest to always TRUE (0x60B050)\r\n");
        }
    }

    /* Sector light count gate: NOP the JZ at 0xEC6337 so all sectors
     * load their static light count regardless of visibility flag. */
    {
        unsigned char *p = (unsigned char *)TRL_SECTOR_LIGHT_GATE_ADDR;
        if (VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0] = 0x90; p[1] = 0x90; p[2] = 0x90;
            p[3] = 0x90; p[4] = 0x90; p[5] = 0x90;
            VirtualProtect(p, 6, oldProtect, &oldProtect);
            log_str("  NOPed sector light count gate at 0xEC6337\r\n");
        }
    }

    /* RenderLights gate: NOP the JE at 0x60E3B1 so light rendering
     * proceeds even for sectors with zero light count. */
    {
        unsigned char *p = (unsigned char *)TRL_RENDER_LIGHTS_GATE_ADDR;
        if (VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0] = 0x90; p[1] = 0x90; p[2] = 0x90;
            p[3] = 0x90; p[4] = 0x90; p[5] = 0x90;
            VirtualProtect(p, 6, oldProtect, &oldProtect);
            log_str("  NOPed RenderLights gate at 0x60E3B1\r\n");
        }
    }

    /* ---- Terrain rendering path patches ---- */

    /* NOP terrain draw cull gate. The 6-byte JNE at 0x40AE3E in the terrain
     * draw function (0x40AE20) skips all terrain rendering when flag 0x20000
     * is set in the drawable's render flags. This is the primary reason terrain
     * anchor geometry vanishes when Lara moves to a different sector. */
    {
        unsigned char *p = (unsigned char *)TRL_TERRAIN_CULL_GATE_ADDR;
        if (VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0] = 0x90; p[1] = 0x90; p[2] = 0x90;
            p[3] = 0x90; p[4] = 0x90; p[5] = 0x90;
            VirtualProtect(p, 6, oldProtect, &oldProtect);
            log_str("  NOPed terrain cull gate at 0x0040AE3E\r\n");
        }
    }

    /* Patch MeshSubmit_VisibilityGate to always return 0 (don't cull).
     * The original function walks visibility lists and returns 1 to cull.
     * Patch: xor eax, eax; ret (33 C0 C3 = 3 bytes). */
    {
        unsigned char *p = (unsigned char *)TRL_MESH_VISIBILITY_GATE_ADDR;
        if (VirtualProtect(p, 3, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0] = 0x33; p[1] = 0xC0;  /* xor eax, eax */
            p[2] = 0xC3;               /* ret */
            VirtualProtect(p, 3, oldProtect, &oldProtect);
            log_str("  Patched MeshSubmit_VisibilityGate to always return 0 (0x454AB0)\r\n");
        }
    }

    /* ---- Post-sector rendering loop patches ---- */

    /* Stamp the post-sector loop enable flag to 1. The loop at 0x40E2C0
     * checks byte [0xF12016] and skips entirely if zero. */
    {
        unsigned char *p = (unsigned char *)TRL_POSTSECTOR_ENABLE_ADDR;
        if (VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            *p = 1;
            VirtualProtect(p, 1, oldProtect, &oldProtect);
            log_str("  Stamped post-sector enable flag to 1 (0xF12016)\r\n");
        }
    }

    /* NOP the write to [0x10024E8] — this global controls streaming unit
     * unloading. When non-zero, the game calls StreamUnitDataDumped (0x5C2010)
     * to evict mesh data for distant sectors. The 5-byte MOV at 0x415C51
     * copies [object+0x198] into this global each frame. NOP it to keep
     * the value at 0, preventing mesh data from being unloaded. This is
     * more robust than stamping the value once (the write overwrites it). */
    {
        unsigned char *p = (unsigned char *)0x00415C51;
        if (VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0] = 0x90; p[1] = 0x90; p[2] = 0x90;
            p[3] = 0x90; p[4] = 0x90;
            VirtualProtect(p, 5, oldProtect, &oldProtect);
            log_str("  NOPed stream unload gate write at 0x00415C51\r\n");
        }
    }

    /* Also stamp the value to 0 in case it was set before our patch. */
    if (VirtualProtect((void*)TRL_POSTSECTOR_GATE_ADDR, 4, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        *(unsigned int*)TRL_POSTSECTOR_GATE_ADDR = 0;
        VirtualProtect((void*)TRL_POSTSECTOR_GATE_ADDR, 4, oldProtect, &oldProtect);
        log_str("  Cleared post-sector/stream gate at 0x10024E8\r\n");
    }

    /* NOP post-sector per-sector bitmask check (6-byte JE at 0x40E30F).
     * Forces all sectors to process their object lists. */
    {
        unsigned char *p = (unsigned char *)TRL_POSTSECTOR_BITMASK_CULL_ADDR;
        if (VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0] = 0x90; p[1] = 0x90; p[2] = 0x90;
            p[3] = 0x90; p[4] = 0x90; p[5] = 0x90;
            VirtualProtect(p, 6, oldProtect, &oldProtect);
            log_str("  NOPed post-sector bitmask cull at 0x0040E30F\r\n");
        }
    }

    /* Post-sector per-object culls: alpha (0x40E33A), flag 0x800 (0x40E349),
     * and flag 0x10000 (0x40E359) are safety guards that prevent processing
     * objects being destroyed or marked invisible. Leave these intact.
     * Only NOP the distance/LOD fade cull which gates draw submission. */
    {
        unsigned char *p = (unsigned char *)TRL_POSTSECTOR_DIST_CULL_ADDR;
        if (VirtualProtect(p, 2, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0] = 0x90; p[1] = 0x90;
            VirtualProtect(p, 2, oldProtect, &oldProtect);
            log_str("  NOPed post-sector distance cull at 0x0040E3B0\r\n");
        }
    }

    /* Stamp post-sector sector bitmask to all-1s so all 8 sectors are
     * processed by the post-sector object loop. */
    if (VirtualProtect((void*)TRL_POSTSECTOR_SECTOR_BITS_ADDR, 4, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        *(unsigned int*)TRL_POSTSECTOR_SECTOR_BITS_ADDR = 0xFFFFFFFF;
        VirtualProtect((void*)TRL_POSTSECTOR_SECTOR_BITS_ADDR, 4, oldProtect, &oldProtect);
        log_str("  Stamped post-sector bitmask to 0xFFFFFFFF\r\n");
    }

    /* ---- Sector_SubmitObject (0x40C650) submission gates ----
     *
     * Two early-exit JNE instructions kill ALL object submissions:
     *   Gate #10 (0x40C666): checks [_object+0x10] renderer state flag.
     *     When non-zero, the entire function exits — no geometry submitted.
     *     This flag is set per-render-context and blocks non-camera sectors.
     *   Gate #12 (0x40C68B): checks [0x10024E8] global submission lock.
     *     Already stamped to 0, but may be rewritten by other code paths.
     * NOP both to allow all objects to reach the draw submission path. */
    {
        unsigned char *p;
        int submitNops = 0;

        /* Gate #10: renderer state flag */
        p = (unsigned char *)0x0040C666;
        if (VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0] = 0x90; p[1] = 0x90; p[2] = 0x90;
            p[3] = 0x90; p[4] = 0x90; p[5] = 0x90;
            VirtualProtect(p, 6, oldProtect, &oldProtect);
            submitNops++;
        }
        /* Gate #12: global submission lock */
        p = (unsigned char *)0x0040C68B;
        if (VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0] = 0x90; p[1] = 0x90; p[2] = 0x90;
            p[3] = 0x90; p[4] = 0x90; p[5] = 0x90;
            VirtualProtect(p, 6, oldProtect, &oldProtect);
            submitNops++;
        }
        log_str("  NOPed Sector_SubmitObject gates: ");
        log_int("", submitNops);
        log_str("/2 (0x40C666 + 0x40C68B)\r\n");
    }

    /* NOTE: Light-has-data check (0x6037D0) was tested but causes crash at
     * 0xEE88AD — always returning 1 allows code to proceed with NULL/garbage
     * light volume pointers. The native light volume system needs valid data
     * to render; forcing it on without data causes access violations.
     * The Remix lights are hash-anchored and don't need native light rendering. */

    /* NOTE: Mesh cull flag NOP at 0x46C33E causes crash at 0xEE88AD —
     * the 0x02000000 bit marks meshes with invalid/uninitialized data.
     * Allowing them through leads to garbage pointer dereferences. */

    /* ---- Object Tracker / Mesh Streaming patches ----
     *
     * The Object Tracker at 0x11585D8 manages 94 slots for loaded mesh data.
     * MeshSubmit calls ObjectTracker_Resolve (0x5D4240) which returns NULL
     * for unloaded meshes, silently skipping the draw. Two eviction systems
     * unload mesh data each frame:
     *   - SectorEviction_ScanAndUnload (0x5D4F30): unloads entire sectors
     *     not accessed this frame
     *   - ObjectTracker_EvictUnneeded (0x5D44C0): frees individual objects
     *     to make room for new ones
     *
     * NOP both eviction call sites so meshes stay loaded once loaded.
     * This is the ROOT CAUSE of the 2,800→178 draw count drop. */

    /* NOP SectorEviction_ScanAndUnload call #1 (0x5D31D9) */
    {
        unsigned char *p = (unsigned char *)0x005D31D9;
        if (VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0] = 0x90; p[1] = 0x90; p[2] = 0x90;
            p[3] = 0x90; p[4] = 0x90;
            VirtualProtect(p, 5, oldProtect, &oldProtect);
        }
    }
    /* NOP SectorEviction_ScanAndUnload call #2 (0x5D5F59) */
    {
        unsigned char *p = (unsigned char *)0x005D5F59;
        if (VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0] = 0x90; p[1] = 0x90; p[2] = 0x90;
            p[3] = 0x90; p[4] = 0x90;
            VirtualProtect(p, 5, oldProtect, &oldProtect);
        }
    }
    /* NOP ObjectTracker_EvictUnneeded call (0x5D5436) */
    {
        unsigned char *p = (unsigned char *)0x005D5436;
        if (VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0] = 0x90; p[1] = 0x90; p[2] = 0x90;
            p[3] = 0x90; p[4] = 0x90;
            VirtualProtect(p, 5, oldProtect, &oldProtect);
        }
    }
    log_str("  NOPed mesh eviction: SectorEviction x2 + ObjectTracker_Evict\r\n");

    /* ---- Layer 3: Render queue frustum culler ----
     *
     * RenderQueue_FrustumCull (0x40C430) tests bounding volumes against the
     * camera frustum. The far-clip test uses _level (0x10FC910). With _level
     * stamped to 1e30 in BeginScene, far-clip never triggers. The remaining
     * tests (near/side/behind) keep GPU load sane by culling backface geometry.
     *
     * NOTE: Full redirect to DirectDispatch (0x40C390) was tested but caused
     * VK_ERROR_DEVICE_LOST — too many draw calls overwhelmed the GPU. */

    /* NOP _level writers: two 6-byte MOV instructions overwrite the far-clip
     * global at 0x10FC910 each frame, replacing our 1e30 stamp with the game's
     * real far clip value. NOP both so objects at any distance survive. */
    {
        unsigned char *p;
        int noped = 0;
        p = (unsigned char *)TRL_LEVEL_WRITER_1_ADDR;
        if (VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0]=0x90; p[1]=0x90; p[2]=0x90;
            p[3]=0x90; p[4]=0x90; p[5]=0x90;
            VirtualProtect(p, 6, oldProtect, &oldProtect);
            noped++;
        }
        p = (unsigned char *)TRL_LEVEL_WRITER_2_ADDR;
        if (VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0]=0x90; p[1]=0x90; p[2]=0x90;
            p[3]=0x90; p[4]=0x90; p[5]=0x90;
            VirtualProtect(p, 6, oldProtect, &oldProtect);
            noped++;
        }
        log_str("  NOPed _level writers: ");
        log_int("", noped);
        log_str("/2 (0x46CCB4 + 0x4E6DFA)\r\n");
    }

    /* Layer 30: Redirect RenderQueue_FrustumCull (0x40C430) to
     * RenderQueue_NoCull (0x40C390). The NoCull path is the engine's own
     * "fully inside frustum" codepath — same BVH traversal, same submission
     * functions, but all 10 frustum plane tests removed. */
    {
        unsigned char *p = (unsigned char *)0x40C430;
        if (VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            p[0] = 0xE9;  /* jmp rel32 */
            *(int*)(p + 1) = (int)(0x40C390 - (0x40C430 + 5));
            VirtualProtect(p, 5, oldProtect, &oldProtect);
            log_str("  Patched RenderQueue_FrustumCull -> NoCull (0x40C430 -> 0x40C390)\r\n");
        }
    }

    /* Permanently unlock data pages that BeginScene stamps every frame.
     * This eliminates ~28 VirtualProtect kernel calls per frame (7 pairs x 2 scenes).
     * These are .data section addresses — safe to leave writable permanently. */
    {
        static const unsigned int dataAddrs[] = {
            TRL_FRUSTUM_THRESHOLD_ADDR,         /* 0x00EFDD64 */
            0x010FC910,                          /* far clip distance */
            TRL_CULL_MODE_PASS1_ADDR,            /* 0x00F2A0D4 (covers D8, DC on same page) */
            TRL_LIGHT_CULLING_DISABLE_FLAG,      /* 0x01075BE0 */
            TRL_RENDER_FLAGS_ADDR,               /* 0x010E5384 */
            TRL_POSTSECTOR_ENABLE_ADDR,          /* 0x00F12016 */
            TRL_POSTSECTOR_GATE_ADDR,            /* 0x010024E8 */
            TRL_POSTSECTOR_SECTOR_BITS_ADDR,     /* 0x00FFA718 */
        };
        int di;
        int unlocked = 0;
        for (di = 0; di < 8; di++) {
            if (VirtualProtect((void*)dataAddrs[di], 16, PAGE_READWRITE, &oldProtect))
                unlocked++;
        }
        log_str("  Permanently unlocked data pages for per-scene stamps: ");
        log_int("", unlocked);
        log_str("/8\r\n");
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
    TRL_ResetTexture0AnimationState(w);
    w->lastDecl = NULL;
    w->curDeclIsSkinned = 0;
    w->curDeclHasTexcoord = 0;
    w->curDeclHasNormal = 0;
    w->curDeclHasColor = 0;
    w->curDeclColorOff = -1;
    w->curDeclHasPosT = 0;
    w->curDeclHasMorph = 0;
    w->curDeclUsesAuxStreams = 0;
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
    w->pinnedDrawCount = 0;
    w->pinnedCaptureComplete = 0;
    w->useVertexCaptureEffective = 1;
    w->float3RoutingMode = FLOAT3_ROUTE_AUTO;
    w->short4AnimatedRoutingMode = FLOAT3_ROUTE_AUTO;
    w->skyIsolationEnable = 1;
    w->skyCandidateMinVerts = 8000;
    w->skyCandidateMinPrims = 18;
    w->skyWarmupScenes = 120;
    w->skyWarmupStartScene = 0;
    w->skyWarmupStarted = 0;
    w->skyIsoCount = 0;
    w->skyIsoLastOrigTex = NULL;
    w->skyIsoLastEntry = NULL;
    w->skyIsoRefreshScene = 0;
    w->skyIsoRefreshDirty = 0;
    w->skySceneCounter = 0;
    w->lastSceneDrawsProcessed = 0;
    w->postLatchSceneLogsRemaining = 0;
    w->sawMenuScene = 0;
    w->sawTransitionBlip = 0;
    w->levelSceneDetected = 0;
    w->replayFeaturesEnabled = 0;
    { int ts; for (ts = 0; ts < 8; ts++) w->diagTexUniq[ts] = 0; }
    w->createTick = GetTickCount();
    w->perfLastTick = w->createTick;
    w->perfLastFrame = 0;
    w->diagLoggedFrames = 0;

    /* Read proxy.ini config */
    {
        char iniBuf[MAX_PATH];
        char float3Mode[32];
        char short4AnimatedMode[32];
        int skyEnable;
        int skyMinVerts;
        int skyMinPrims;
        int skyWarmup;

        get_dll_sibling_path_local(iniBuf, MAX_PATH, "proxy.ini");
        w->albedoStage = GetPrivateProfileIntA("FFP", "AlbedoStage", 0, iniBuf);
        if (w->albedoStage < 0 || w->albedoStage > 7) w->albedoStage = 0;

        GetPrivateProfileStringA("FFP", "Float3RoutingMode", "auto", float3Mode, 32, iniBuf);
        w->float3RoutingMode = parse_float3_routing_mode(float3Mode);
        GetPrivateProfileStringA("FFP", "Short4AnimatedRoutingMode", "auto", short4AnimatedMode, 32, iniBuf);
        w->short4AnimatedRoutingMode = parse_float3_routing_mode(short4AnimatedMode);

        skyEnable = GetPrivateProfileIntA("Sky", "EnableIsolation", 1, iniBuf);
        skyMinVerts = GetPrivateProfileIntA("Sky", "CandidateMinVerts", 8000, iniBuf);
        skyMinPrims = GetPrivateProfileIntA("Sky", "CandidateMinPrims", 18, iniBuf);
        skyWarmup = GetPrivateProfileIntA("Sky", "WarmupScenes", 120, iniBuf);

        if (skyMinVerts < 0) skyMinVerts = 0;
        if (skyMinPrims < 0) skyMinPrims = 0;
        if (skyWarmup < 0) skyWarmup = 0;

        w->skyIsolationEnable = skyEnable ? 1 : 0;
        w->skyCandidateMinVerts = (unsigned int)skyMinVerts;
        w->skyCandidateMinPrims = (unsigned int)skyMinPrims;
        w->skyWarmupScenes = (unsigned int)skyWarmup;
    }
    w->useVertexCaptureEffective = TRL_ReadEffectiveUseVertexCapture();

    log_str("WrappedDevice created with shader passthrough + transform override\r\n");
    log_int("  Diag delay (ms): ", DIAG_DELAY_MS);
    log_int("  AlbedoStage: ", w->albedoStage);
    if (w->float3RoutingMode == FLOAT3_ROUTE_AUTO) log_str("  Float3RoutingMode: auto\r\n");
    if (w->float3RoutingMode == FLOAT3_ROUTE_NULL_VS) log_str("  Float3RoutingMode: null_vs\r\n");
    if (w->float3RoutingMode == FLOAT3_ROUTE_SHADER) log_str("  Float3RoutingMode: shader\r\n");
    if (w->short4AnimatedRoutingMode == FLOAT3_ROUTE_AUTO) log_str("  Short4AnimatedRoutingMode: auto\r\n");
    if (w->short4AnimatedRoutingMode == FLOAT3_ROUTE_NULL_VS) log_str("  Short4AnimatedRoutingMode: null_vs\r\n");
    if (w->short4AnimatedRoutingMode == FLOAT3_ROUTE_SHADER) log_str("  Short4AnimatedRoutingMode: shader\r\n");
    if (TRL_GetEffectiveFloat3Route(w) == FLOAT3_ROUTE_SHADER)
        log_str("  Float3Route effective: shader\r\n");
    else
        log_str("  Float3Route effective: null_vs\r\n");
    log_int("  SkyIsolation: ", w->skyIsolationEnable);
    log_int("  SkyCandidateMinVerts: ", (int)w->skyCandidateMinVerts);
    log_int("  SkyCandidateMinPrims: ", (int)w->skyCandidateMinPrims);
    log_int("  SkyWarmupScenes: ", (int)w->skyWarmupScenes);
    log_hex("  Real device: ", (unsigned int)pRealDevice);
    log_hex("  View matrix addr: ", TRL_VIEW_MATRIX_ADDR);
    log_hex("  Proj matrix addr: ", TRL_PROJ_MATRIX_ADDR);

    /* Memory patches deferred to first BeginScene with viewProjValid —
     * applying at device creation crashes the main menu, which uses the
     * same code paths but with NULL scene graph pointers. */

    return w;
}
