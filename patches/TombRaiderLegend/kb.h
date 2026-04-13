/*
 * Knowledge Base -- Tomb Raider Legend (trl.exe)
 * D3D8 game using dxwrapper (D3d8to9=1) to convert to D3D9
 * Shaders compiled with "D3DX9 Shader Compiler 5.04.00.2904"
 */

// ============================================================
// DrawIndexedPrimitive call sites (9 total, D3D9 vtable offset 0x148)
// ============================================================
// 0x40B1A0 in 0x40ADF0 — small mesh DIP (vtable dispatch, no direct callers)
// 0x415C0E in 0x415A90 — post-sector object DIP (called from PostSector at 0x40E323)
// 0x60188F in 0x601590 — standard mesh draw (5 callers from material renderer 0x602660)
// 0x602012 in 0x601920 — complex mesh draw (1 caller from material renderer)
// 0x60EBD7 in 0x60EB50 — simple indexed draw (6 callers from various material handlers)
// 0x610112 in 0x61005C — textured draw (vtable dispatch)
// 0x610D39 in 0x610850 — complex skinned/lit draw (1 caller from 0x611026)
// 0x61303C in 0x613000 — shadow/decal draw (1 caller from 0x6135E5)
// 0x6137F3 in 0x6137BA — shadow/decal variant (vtable dispatch)
//
// Material renderer vtable at 0xF084AC (no RTTI), slot 7 (+0x1C) = 0x602660
// Majority of DIP calls flow through this vtable from the render command buffer flush.

// ============================================================
// D3D9 vtable offsets used by the engine
// ============================================================
// 0x0E4 = SetRenderState          (method 57)
// 0x104 = SetTextureStageState    (method 65)
// 0x10C = SetSamplerState         (method 67)
// 0x114 = SetTexture              (method 69) -- actually CreateStateBlock area? double check
// 0x164 = SetFVF                  (method 89)
// 0x170 = SetVertexShader         (method 92)
// 0x178 = SetVertexShaderConstantF (method 94)
// 0x1AC = SetPixelShader          (method 107)

// ============================================================
// VS Constant Register Layout
// ============================================================
// c0-c3   (reg 0,  cnt 4): WorldViewProjection matrix (transposed, row0-row3)
//                            OR first 4 rows of combined 8-register upload
// c4-c7   (reg 4,  cnt 4): Fog / lighting parameters
// c6      (reg 6,  cnt 1): Fog distance {0, 0, fogEnd, epsilon} (standalone upload)
// c8-c11  (reg 8,  cnt 4): View matrix (transposed, from this+0x480 * this+0x4C0)
//                            First half of 8-register batch upload
// c12-c15 (reg 12, cnt 4): Projection-related (second half of 8-register batch)
//                            From this+0x500 copy
// c16+    (reg 16, cnt N): Per-object bone/skin matrices (variable count = bones*2)
// c17     (reg 17, cnt 1): Single per-draw constant
// c18     (reg 18, cnt 1): Ambient / material color
// c19     (reg 19, cnt 1): Light direction / color
// c21     (reg 21, cnt 1): Object-space camera or object data
// c22-c23 (reg 22, cnt 2): Normal map / additional lighting data
// c24-c27 (reg 24, cnt 4): Texture transform / UV animation matrix
// c28     (reg 28, cnt 1): Per-object parameter
// c30     (reg 30, cnt 1): Screen / viewport params (zNear, zFar related)
// c37     (reg 37, cnt 1): Light-related parameter {value, 0, 0, 0}
// c38     (reg 38, cnt 1): Scale / bias constant
// c39     (reg 39, cnt 1): Utility constants {2.0, 0.5, 0.0, 1.0} (set once at init)
// c40-c41 (reg 40, cnt 2): Camera position / parameters
// c44     (reg 44, cnt 1): Camera direction {x, y, z, 0.5}
// c48+    (reg 48, cnt N): Skinning / bone matrices (alternative slot, variable count)
// c96     (reg 96, cnt 1): Far clip / depth bias parameters

// ============================================================
// Renderer object layout (at offset from 'this')
// ============================================================
struct TRLRenderer {
    // ... (partial)
    void* vtable;                   // +0x00
    int pad1[2];                    // +0x04, +0x08
    IDirect3DDevice9* pDevice;      // +0x0C  *** D3D device — accessed as *(g_pEngineRoot+0x214)+0x0C
    int field_10;                   // +0x10
    int field_14;                   // +0x14
    // ...
    int field_68;                   // +0x68  current vertex shader
    // ...
    // render state cache starts around +0xEC
    int renderStateCache[0xD2];     // +0xEC .. ~+0x43C (0xD2 entries, initialized to -1)
    int field_460;                  // +0x460  = 10 (blend mode count?)
    int field_464;                  // +0x464  flags (bit 6 = 0x40 disables cull mode change)
    int field_468;                  // +0x468
    int field_46C;                  // +0x46C
    int blendMode;                  // +0x470  current blend mode (switch in ECBC20)
    int cullModeGlobalPtr;          // +0x474  last value written from g_cullMode_pass1/pass2
    // padding to +0x480
    float viewMatrix1[16];          // +0x480  first view-related matrix (input A to MatrixMultiply)
    float viewMatrix2[16];          // +0x4C0  second view-related matrix (input B to MatrixMultiply)
    float projMatrix1[16];          // +0x500  first projection-related matrix (input A)
    float projMatrix2[16];          // +0x540  second projection-related matrix (input B)
    char viewDirty;                 // +0x580  flag: view matrices need re-upload to c8-c15
    char projDirty;                 // +0x581  flag: projection matrices need re-upload to c0-c7
    // ...
    int alphaRef;                   // +0x584  alpha reference value for blend modes

    // D3D render state cache (direct SetRenderState mirrors, checked before issuing call)
    // Offset = 0xEC + (stateEnum - base) * 4  (approximate — base is ~7)
    int cached_ZFUNC;               // +0x148  D3DRS_ZFUNC (23 = 0x17)  current value
    int cached_CULLMODE;            // +0x144  D3DRS_CULLMODE (22 = 0x16) current value
    int cached_STENCILENABLE;       // +0x1BC  D3DRS_STENCILENABLE (52 = 0x34) current value
    int cached_COLORWRITEENABLE;    // +0x3D0  D3DRS_COLORWRITEENABLE (185 = 0xB9) current value
    int cached_STENCILZFAIL;        // +0x1C4  D3DRS_STENCILZFAIL (54 = 0x36) current value
    int cached_STENCILPASS;         // +0x1C8  D3DRS_STENCILPASS (55 = 0x37) current value
    int cached_STENCILFAIL;         // +0x1CC  D3DRS_STENCILFAIL (56 = 0x38) current value
    int cached_STENCILREF;          // +0x1D4  D3DRS_STENCILREF (58 = 0x3A) current value
    int cached_STENCILMASK;         // +0x1D8  D3DRS_STENCILMASK (59 = 0x3B) current value
};

// ============================================================
// Matrix operation functions
// ============================================================

@ 0x005DD910 void __cdecl MatrixMultiply4x4(float* result, float* matA, float* matB);
@ 0x00402990 void __thiscall MatrixCopy4x4(float* this_dst, float* src);
@ 0x00ECBAA0 void __fastcall MatrixTranspose4x4(float* dst, float* src);

// ============================================================
// Renderer functions
// ============================================================

@ 0x00ECBA40 void __thiscall Renderer_SetVSConstantF(TRLRenderer* this, unsigned short startReg, void* data, unsigned short count);
@ 0x00ECBA60 void __thiscall Renderer_SetVertexShader(TRLRenderer* this, void* shader);
@ 0x00ECBB00 void __fastcall Renderer_UploadViewProjMatrices(TRLRenderer* this);
             //   Uploads: startReg=8, count=8 (c8-c15, viewMatrix1 * viewMatrix2)
             //   Then:    startReg=0, count=8 (c0-c7,  projMatrix1 * projMatrix2)
             //   Guarded by viewDirty (+0x580) and projDirty (+0x581) flags
             //   Call sites: 0x00ECBB89, 0x00ECBC01
@ 0x00413950 void __cdecl cdcRender_SetWorldMatrix(int startReg, float* matrix);
             //   Transposes matrix (game col-major → HLSL row-major) then calls
             //   Renderer_SetVSConstantF(startReg, transposed, 4)
             //   Called by many render-path functions with startReg=0x28 (c40) for secondary
             //   Call site in Renderer_SetVSConstantF wrapper: 0x00ECBA57
@ 0x00ECBC20 void __thiscall Renderer_SetBlendMode(TRLRenderer* this, int mode);
@ 0x00ECC160 void __thiscall Renderer_SetBlendMode_Wrapper(TRLRenderer* this, int mode);
@ 0x00ECC180 uint __fastcall Renderer_Init(TRLRenderer* this);
@ 0x0040E470 void __thiscall Renderer_SetRenderStateCached(TRLRenderer* this, int state, int value);
@ 0x0040EAB0 void __thiscall Renderer_ApplyRenderStateChanges(TRLRenderer* this, uint desiredStates);
@ 0x0040E980 void __cdecl SetTextureStageState_Cached(uint mask);  // uses g_pEngineRoot->[+0x214]->[+0xC] as device
@ 0x004072A0 void FrustumCull_SceneTraversal(int objectList);
@ 0x0060C7D0 void __thiscall RenderLights_FrustumCull(void* this);
             //   Iterates [ebx+0x1B0] lights, calls FUN_0060b050 for broad visibility,
             //   then 6-plane frustum dot-product test per light.
             //   Lights passing all 6 planes: drawn immediately with mode=1 via vtable[0x18].
             //   Lights failing any plane: deferred to array at 0x13107F4/FC, drawn later with mode=0.
             //   CULL JUMP 1: 0x60CDE2 (je +0x61, 2 bytes: 74 61) — skips light if FUN_0060b050 returns 0
             //   CULL JUMP 2: 0x60CE20 (jnp +0x18D, 6 bytes: 0F 8B 8D 01 00 00) — defers light on plane fail
             //   Vtable dispatch: [edx+0x14] = GetBoundingSphere (slot 5), [eax+0x18] = Draw (slot 6)
@ 0x006037D0 int __thiscall LightVolume_HasData(void* this);
             //   Iterates [this+0x1C] array (count at [this+0x14]), checks each entry's
             //   [+0x94]+[+0x84]. Returns 1 if any nonzero, 0 if all zero.
             //   Called at 0x60A52F — if returns 0, entire light render path is skipped
             //   (je at 0x60A53C jumps past RenderLights_Caller call at 0x60A62C).
             //   PATCH: B0 01 C3 (mov al, 1; ret) — always report lights present.
@ 0x00603810 void __thiscall LightVolume_DispatchAll(void* this);
             //   Iterates [this+0x1C] array, calls RenderLights_Caller (0x60E2D0)
             //   for each entry where [entry+0x94]+[entry+0x84] != 0.
             //   Called from 0x60A62C. Single xref caller.
@ 0x0060E2D0 void __thiscall RenderLights_Caller(void* this);
             //   Outer caller of RenderLights_FrustumCull. Sets up shadow/stencil state.
             //   THREE CONDITIONS must be true to reach FrustumCull:
             //   1. [this+0x84] != 0 (light data ptr exists)
             //   2. [g_pEngineRoot+0x166] != 0 (scene-level light enable flag)
             //   3. [this+0x74]->[+0x444] & 1 (renderer light capability bit)
             //   If all 3 true AND [this+0x150] != 0, shadow flag (esp+0x16) set.
             //   FrustumCull gate: [this+0x1B0] != 0 sets esp+0x17 = 1 at 0x60E34D.
             //   0x60E3B1: JE 0x60E4B6 skips FrustumCull if esp+0x17 == 0.
             //   Called from 0x603810 (LightGroupArray_RenderAll).
@ 0x00603810 void __fastcall LightGroupArray_RenderAll(int lightGroupArray);
             //   Iterates [param+0x14] light groups via array at [param+0x1C].
             //   For each group: checks [group+0x94]+[group+0x84] != 0 (has light data).
             //   If so, stores group ptr at [param+0x228] and calls RenderLights_Caller.
             //   Called from RenderLights_TopLevel at 0x60A62C.
@ 0x0060A0F0 void __thiscall RenderLights_TopLevel(void* this);
             //   Top-level render lights function. Entry at 0x60A0F0 in a larger function.
             //   THREE EARLY-EXIT conditions at entry (all jump to cleanup at 0x60AB40):
             //   1. [this+0xFA2E] != 0 (rendering disabled flag)
             //   2. [this+0xFA2C] == 0 (lights not initialized)
             //   3. [g_pEngineRoot+0x10] != 0 (scene loading/transition flag)
             //   Called via VTABLE slot [6] (+0x18) at vtable 0xF08480.
             //   NO direct call xrefs -- only reachable via virtual dispatch.
@ 0x0060BCF0 void __thiscall RenderLights_PreSetup(void* this);
             //   Pre-light renderer setup, called before RenderLights_FrustumCull
@ 0x0060E150 void __thiscall RenderLights_ShadowSetup(void* this);
             //   Shadow/stencil setup, called conditionally if shadow flag is set
// ============================================================
// Light object class hierarchy (NO RTTI)
// ============================================================
// LightBase: outer vtable 0xF085D4, inner MI vtable 0xF085E8 at this+8
//   Constructor: 0x60B320, size=0x1D0, field +0x420 = enable flag
// LightGroup (container): vtable 0xF08618, secondary 0xF08614 at this+4
//   Constructor: 0x60C240, inherits from LightBase
//   +0x74 = type, +0x7C = param, +0x1B0 = light count, +0x1B8 = light obj ptr array
// LightVolume (individual light): vtable 0xF08740, secondary 0xF08738 at this+4
//   Constructor: 0x610170, size=0x1F0
//   vtable[5] (+0x14) = GetBoundingSphere (0x612C80)
//   vtable[6] (+0x18) = Draw (0x6124E0)
// SceneLight: vtable 0xF08688, secondary 0xF086A0
//   Set at 0x60F68D
// LightEffect: vtable 0xF087DC
//   Set at 0x611A53, no Draw implementation

@ 0x0060B320 void __thiscall LightBase_Constructor(void* this, void* param1, void* param2);
             //   Sets vtables: [this]=0xF085C0 -> 0xF085D4, [this+8]=0xF085E8
@ 0x0060C240 void __thiscall LightGroup_Constructor(void* this, void* param1, void* param2);
             //   Sets vtables: [this]=0xF085EC -> 0xF08618, [this+4]=0xF08614
@ 0x00610170 void __thiscall LightVolume_Constructor(void* this);
             //   Sets vtables: [this]=0xF08740, [this+4]=0xF08738
@ 0x006124E0 void __thiscall LightVolume_Draw(void* this, void* renderCtx, void* lightParams, int enabled);
             //   The per-light draw function called from RenderLights_FrustumCull vtable[6]
@ 0x00612C80 void* __thiscall LightVolume_GetBoundingSphere(void* this);
             //   Returns bounding sphere for frustum culling, called via vtable[5]

@ 0x0060B050 char __thiscall LightVisibilityCheck(void* this, void* lightData);
             //   Mode-dependent broad visibility check. thiscall with 1 stack arg, ret 4.
             //   Reads mode from [this+0x74]->[+0x448] (3-way switch):
             //   mode 0: calls 0x60AD20 (spotlight path)
             //   mode 1: calls 0x60AC80 + 0x5F9BE0 (pointlight AABB test -- DISTANCE DEPENDENT)
             //   mode 2: calls 0x60AC80 + 0x5F9A60 (directional AABB test)
             //   default (>=3): returns AL=1 (always visible)
             //   CRITICAL: This is the primary culling gate for lights at distance.
             //   Called at 0x60CDDB; result checked at 0x60CDE2 (je skips light if AL=0).
             //   Patch: B0 01 C2 04 00 (mov al,1; ret 4) to force all lights visible.
@ 0x0060AC80 void __thiscall LightVisibility_ComputeAABB(void* this, float radius, float scaling);
             //   Computes AABB for light visibility check (modes 1 and 2)
@ 0x0060AD20 void __thiscall LightVisibility_SpotlightTest(void* this, float param1, float param2);
             //   Spotlight visibility test (mode 0)
@ 0x005F9BE0 char __cdecl AABB_IntersectionTest(void* aabbA, void* aabbB);
             //   AABB intersection test, returns bool. Used by LightVisibilityCheck mode 1.
@ 0x005F9A60 char __cdecl AABB_IntersectionTest_Alt(void* aabbA, void* aabbB);
             //   Alternate AABB intersection test. Used by LightVisibilityCheck mode 2.
@ 0x00402DA0 void __cdecl TransformObjectToScreen(void* output, void* input);
@ 0x00406240 void __cdecl SubmitBillboard(void* billboard);
@ 0x00406DA0 void __cdecl SubmitAxisAlignedSprite(float x, float y, float w, float h, float z, int color);
@ 0x00406ED0 void __cdecl SubmitRotatedSprite(float x, float y, float w, float h, float z, int color);
@ 0x00406EF0 void __cdecl FrustumCull_ExpandBounds(void* bounds);
@ 0x00ECB900 void __thiscall Renderer_SetSamplerState(TRLRenderer* this, int sampler, int value);

// ============================================================
// Scene traversal / visibility pipeline
// ============================================================

// Scene node linked-list element (EBX in SceneTraversal_CullAndSubmit)
struct SceneNode {
    void* vtable_or_type;       // +0x00
    SceneNode* next;            // +0x04  linked list next pointer
    uint32_t flags;             // +0x08  bit 4=skip, bit 10=alt path, bit 7=negative sign check
    uint16_t meshId;            // +0x0C  mesh/object ID passed to submit functions
    float posX;                 // +0x10
    float posY;                 // +0x14
    float posZ;                 // +0x18
    // gap
    uint16_t renderFlags;       // +0x2E  bits: &7=type switch, bit 4=alt submit
    uint8_t extraFlags;         // +0x2F  bit 6=orientation flag
    // gap
    uint32_t sortKey;           // +0x3C  stored/restored across iterations
    // gap
    RenderContext* ctx;          // +0x8C  render/transform context — CAN BE NULL (crash source)
    // gap
    uint8_t lodLevel;           // +0xB7  LOD blend factor
};

// Render context sub-object (at SceneNode +0x8C)
struct RenderContext {
    // +0x00 .. +0x0F: unknown
    float matrix[16];           // +0x10  4x4 transform matrix
    float posX;                 // +0x40  copied from SceneNode.posX
    float posY;                 // +0x44  copied from SceneNode.posY
    float posZ;                 // +0x48  copied from SceneNode.posZ
    float posW;                 // +0x4C  always 1.0f (0x3F800000)
};

@ 0x00407150 void __cdecl SceneTraversal_CullAndSubmit(void* sceneGraph);
             //   4049 bytes, traverses two linked lists from sceneGraph:
             //   List 1 ([arg+0x24]): SceneNode chain via +0x04, copies position to RenderContext
             //   List 2 ([arg+0x2C]): second node chain with frustum culling and mesh submission
             //   CRASH BUG: [node+0x8C] (RenderContext*) can be NULL — no null check before
             //   dereferencing at 0x4071E2 (mov [edx+0x40], eax). Nodes with flags bit 4 set
             //   are skipped safely; nodes without the flag but with NULL ctx crash.
             //   RET patch at entry (0xC3) prevents crash and disables all scene culling.
@ 0x00443C20 void __cdecl RenderScene(void* sceneData, void* cameraMatrix);
             //   Calls: CopyMatrixToGlobal(cameraMatrix) -> SceneTraversal_CullAndSubmit(sceneData)
             //   -> RenderScene_PostProcess -> SubmitMesh_Generic -> 0x446D90(0) -> 0x446D90(1)
             //   This populates the draw queue that is later flushed by DrawPass_FlushBatches (0x415260)
// ============================================================
// Sector visibility pipeline — mesh-level culling
// ============================================================

@ 0x0046C180 void __cdecl SectorVisibility_RenderVisibleSectors(int sceneData);
             //   Three render paths:
             //   Path 1 (type==1): Sector_RenderMeshes (0x46B7D0) — per-sector mesh submission
             //   Path 2 (type==2): Sector_RenderFullscreen (0x46B890) — fullscreen overlay sectors
             //   Path 3: PostSector_ObjectLoop (tail-call to 0x40E2C0) — per-sector object iteration
             //   PATCHED: 0x46C194 (je->nop) and 0x46C19D (jne->nop) force all 8 sectors visible
             //   UNPATCHED GATES:
             //   0x46C237/0x46C242: frustum width check — sector screen-space width < [0x10FC920]
             //   0x46C250/0x46C25B: frustum height check — sector screen-space height < [0x10FC924]
@ 0x0046B7D0 void __cdecl Sector_RenderMeshes(void* sectorData);
             //   Iterates objects in sector mesh array, checks flags & 1 and & 0x20000 to skip.
             //   Calls Sector_SubmitObject (0x40C650) for each passing object.
             //   GATE: (flags & 0x200000) + proximity check at 0x46B85A (PATCHED)
@ 0x0046B890 void __cdecl Sector_RenderFullscreen(void* sectorData);
             //   Iterates objects, filters: (flags & 0x400000) must be set, (flags & 0x20001) must be clear
@ 0x0046C320 void __cdecl Sector_IterateMeshArray(void* meshArray, void* sceneCtx);
             //   Iterates meshes (stride 0x70), skips if (flags+0x5C & 0x82000000) != 0
             //   Calls MeshSubmit (0x458630) for each mesh. Called from 0x44FB60 and 0x452060.
@ 0x00458630 int __cdecl MeshSubmit(void* meshEntry, short sectorIdx, char forceVisible);
             //   GATE 1: MeshSubmit_VisibilityGate (0x454AB0) — PVS bitfield check
             //     if gate returns 1, mesh already visible (skip), unless forceVisible
             //   GATE 2: material check via 0x5D4240 — must return non-null with type==2
             //   Sets mesh flag 0x80000000 when submitted. Many sub-operations for transform setup.
@ 0x00454AB0 uint __cdecl MeshSubmit_VisibilityGate(void* meshEntry);
             //   Three-tier visibility check:
             //   1. Linked list at [0x10C5AA4]: check if meshEntry+0x54 matches [entry+0x1D0]
             //   2. SectorCommandBuffer_Search (0x5BE540): search command buffer for mesh ID
             //   3. SectorVisibility_BitfieldCheck (0x5BE7B0): PVS bitfield at [0x11397C0]+0x33D8
             //   Returns 1 (already visible/reject) or 0 (not yet visible, proceed with submit)
@ 0x005BE7B0 bool __cdecl SectorVisibility_BitfieldCheck(void* meshEntry);
             //   Reads mesh ID from meshEntry+0x54, checks bit in PVS bitfield
             //   Bitfield: [0x11397C0] + 0x33D8, size 0x47C bytes (9184 mesh bits)
@ 0x005BE540 uint16_t* __cdecl SectorCommandBuffer_Search(int sectorId);
             //   Searches render command buffer at [g_pRenderStructure]+0x3854
             //   Returns matching command entry or NULL
@ 0x00455780 uint32_t __cdecl MeshSubmit_RenderObjectFilter(int meshEntry);
             //   Checks mesh against render object table at 0x10C5BC0 (stride 0x260)
             //   Returns render object ptr if [renderObj+0xA4] & 0x20 is set, else 0
@ 0x00455700 int __cdecl MeshSubmit_ObjectTrackerSearch(int meshEntry, int* outSectorId);
             //   Searches ObjectTracker entries (0x1158300, stride 0x5C) for mesh by sector ID
             //   Returns mesh entry ptr from tracker, or 0 if not found
@ 0x00461A90 int __cdecl MeshSubmit_AlternatePath(void* meshEntry, short sectorIdx);
             //   Alternate submission path for LOD/instance meshes
             //   Used when ObjectTracker data flags bit 0x100 or mesh flags bit 0x200000 set
@ 0x0044FB60 void __cdecl ObjectTracker_IterateAndSubmitMeshes(void);
             //   Iterates all ObjectTracker slots (0x11582F8, stride 0x5C, up to 0x11585D8)
             //   For state==2 entries, calls Sector_IterateMeshArray with mesh array data

// MeshEntry struct — 0x70 bytes per mesh in sector mesh arrays
struct MeshEntry {
    float bounds0[4];               // +0x00 bounding box / position row 0
    float bounds1[4];               // +0x10 bounding box / position row 1
    // gap 0x20..0x3F
    float scale[4];                 // +0x40 scale vector (used if flags & 0x100000)
    int16_t resourceKey;            // +0x50 ObjectTracker resource key
    uint16_t field_52;              // +0x52 copied to render cmd +0x92
    uint32_t sectorId;              // +0x54 sector ID for visibility checks
    // gap 0x58..0x5B
    uint32_t flags;                 // +0x5C visibility/state flags — see flag table below
    uint32_t field_64;              // +0x64 copied to render cmd +0x1C4
};
// MeshEntry.flags bit definitions:
//   0x00100000 — use mesh scale from +0x40 (else default 1.0)
//   0x00200000 — divert to alternate submission path (0x461A90)
//   0x00400000 — sets render flag 0x08 at [cmd+0xAC]
//   0x00800000 — sets render flag 0x20000 at [cmd+0xA8]
//   0x01000000 — triggers material/sort function 0x465A60
//   0x02000000 — BLOCKS Sector_IterateMeshArray (tested at 0x46C337)
//   0x04000000 — triggers sort key path (if objData[4]==0xFFFFFFFF)
//   0x10000000 — shadow/decal path
//   0x80000000 — "already submitted" marker, set by VisibilityGate on success

// ObjectTracker entry layout (stride 0x24, base 0x11585D8)
// +0x00: data pointer (points to mesh/resource data)
// +0x08: data sub-pointer (flags at [data+4])
// +0x0E: state byte (must be 2 = loaded for draw submission)
// +0x10: reference count (uint16_t)
             //   Returns true if mesh bit is SET (mesh already in PVS)
@ 0x005BE540 uint16_t* __cdecl SectorCommandBuffer_Search(int sectorId);
             //   Searches command buffer at [0x11397C0]+0x3854 for matching sector ID
@ 0x005BEE30 void __cdecl SectorVisibility_PopulateBitfield(void);
             //   Reads mesh IDs from [0x113F854]+0x6F2 array, sets corresponding bits in PVS bitfield
             //   Called during sector setup at 0x421058 and 0x5BFC95
@ 0x005BFC10 uint __cdecl SectorVisibility_InitializeRenderStructure(uint param);
             //   Copies PVS source bitfield from 0x113F3D8 into [0x11397C0]+0x33D8 (0x11F dwords)
             //   Then calls SectorVisibility_PopulateBitfield to set dynamic mesh bits
@ 0x005BE860 void __cdecl SectorVisibility_ClearMeshBit(int meshObj);
             //   Clears mesh visibility bit: reads ID from meshObj+0x1D0, clears bit in PVS bitfield
@ 0x0040C650 void __cdecl Sector_SubmitObject(void* meshBase, void* objectEntry);
             //   GATE: [_object+0x10]==0 AND (objectEntry+0x20 >= 0 OR [_object+0x182]!=0) AND [0x10024E8]==0
             //   Sets up matrices, builds lighting mask, calls 0x40C430 and 0x40C040 to submit
@ 0x0040C430 void __cdecl RenderQueue_FrustumCull(void* node, uint32_t shadowMask, uint32_t sectorMask);
             //   RECURSIVE bounding-volume frustum culler for queued render commands.
             //   Tests node bounding sphere against view-space frustum planes (viewMatrix at 0xF48A70,
             //   secondary matrix at 0xF48AB0, far boundary from _level at 0x10FC910).
             //   If fully inside: dispatches all children via 0x40C390 (no per-child test).
             //   If partially visible: recurses on each child.
             //   If outside: RETURNS EARLY — object never reaches DIP.
             //   Leaf nodes (childCount==0): calls 0x40D9B0 (PostSector_AddVisibility) and 0x40ACB0
             //   (RenderQueue_InsertCommand). This is the THIRD culling layer — objects that pass
             //   portal visibility (Layer 1) and mesh submission gates (Layer 2) can still be
             //   frustum-culled here before any DrawIndexedPrimitive.
             //   **UNPATCHED** — this is the likely bottleneck for ~650 draw count.
@ 0x0040C390 void __cdecl RenderQueue_DirectDispatch(void* node, uint32_t shadowMask, uint32_t sectorMask);
             //   Skip-frustum-test path: recursively dispatches all children of a node
             //   without bounding volume tests. Called from 0x40C430 when node is fully inside frustum.
             //   Leaf nodes: calls 0x40D9B0 and 0x40ACB0 directly.
@ 0x0040ACB0 void __fastcall RenderQueue_InsertCommand(void* renderCmd);
             //   Inserts a render command into the final render list at [esi+0x90].
             //   This is the gateway to actual rendering — only commands reaching here
             //   will eventually produce DrawIndexedPrimitive calls.
@ 0x0040D9B0 void __cdecl PostSector_AddToVisibilityMask(uint32_t param1, uint32_t sectorMask, void* meshBase, void* objectEntry);
             //   Sets bits in g_postSectorVisibilityMask (0xFFA718) based on sectorMask.
             //   Also appends to render queue at [0xFF9710 + g_queueIndex*0x10].
             //   Called from leaf nodes of RenderQueue_FrustumCull and RenderQueue_DirectDispatch.
@ 0x0040C040 void __cdecl RenderQueue_DispatchMeshGroups(void* objectEntry, void* meshBase);
             //   Iterates mesh groups within an object. For each group:
             //   Allocates render command (0x413D60), calls 0x40ACF0 (large render submit),
             //   then processes material/LOD via 0x40BD10 or 0x413D70+EC9DC0.
             //   Called from Sector_SubmitObject after matrix setup.
@ 0x0040E2C0 void __cdecl PostSector_ObjectLoop(uint32_t* sectorArray);
             //   Post-sector object iteration with distance culling.
             //   GATE 1: [0xF12016] must be nonzero (enable flag)
             //   GATE 2: [0x10024E8] must be zero (global render suppression)
             //   GATE 3: per-sector visibility mask at [0xFFA718] (bit per sector)
             //   GATE 4: Object_ComputeDistance (0x455A50) — distance culling against [0xEFDDB0]
             //   Also checks object flags: [obj+0xA4] & 0x800, [obj+0xA8] & 0x10000

// PVS (Potentially Visible Set) globals
$ 0x11397C0 void* g_pRenderStructure
             //   Pointer to render structure. PVS bitfield at +0x33D8 (0x47C bytes)
$ 0x113F3D8 uint8_t g_pvsSourceBitfield[0x47C]
             //   Source PVS bitfield, copied into render structure during sector init
$ 0x113F854 void* g_pSectorData
             //   Sector/level data structure. Mesh ID array at +0x6F2.
$ 0x10C5AA4 void* g_hiddenMeshList
             //   Linked list of explicitly hidden meshes (checked first in VisibilityGate)
$ 0xFFA718 uint32_t g_postSectorVisibilityMask
             //   Per-sector visibility bitmask for post-sector object loop
$ 0x10FC920 float g_minSectorScreenWidth
             //   Minimum screen-space width for sector to be rendered
$ 0x10FC924 float g_minSectorScreenHeight
             //   Minimum screen-space height for sector to be rendered
$ 0xEFDDB0 float g_objectDistanceCullThreshold
             //   Distance threshold for post-sector object culling
$ 0xEFD40C float g_objectDistanceBoundary
             //   Distance boundary value for post-sector culling formula
$ 0xF12016 uint8_t g_postSectorEnabled
             //   If zero, entire post-sector object loop is skipped

@ 0x00450B00 void __cdecl RenderFrame(void* frameData);
             //   735-byte mid-level render loop. CRITICAL GATE at 0x450B97: je 0x450CB0
             //   jumps over ALL sector + scene rendering if 0x4F7410 returns 0 (level not loaded).
             //   Calls: 0x46C4F0 (SetupCameraSector), 0x46D1D0, object loop (0x10C5AA4 linked list),
             //   0x46C180 (SectorVisibility_RenderVisibleSectors) [PATCHED],
             //   0x443C20 (RenderScene) [calls SceneTraversal_CullAndSubmit, PATCHED],
             //   then post-frame work.
             //   The sector bypass flag at 0xF17904 gates whether 0x46C180 or 0x5C3C50 is called.
@ 0x00450DE0 void __cdecl RenderFrame_TopLevel(void* context);
             //   Calls: 0x40CA60(1) -> RenderFrame(context) -> fade/flip.
             //   Fade path: if g_fadeActive (0xF127B8) != 0, animates screen opacity.
             //   Both paths call 0x415A40 (DrawPass_Gate) -> 0x415260 (DrawPass_FlushBatches) -> 0x5D42C0
@ 0x0040CBE0 void __cdecl FadeController_RenderAndFlip(int param);
             //   Checks g_fadeActive (0xF127B8). If fade active: interpolates fade value.
             //   Then: sets g_fadeAlpha (0xF127D4) and calls DrawPass_Gate -> 0x5D42C0 -> SubmitMesh_Generic(0x604BE0)
             //   32 callers across game loop, loading screens, menu transitions.
             //   Return address 0x40CD1A is from 0x5D42C0 call (NOT from DrawIndexedPrimitive).
@ 0x00415A40 void __cdecl DrawPass_Gate(void);
             //   GATE 1: if g_drawSubmitReady (0x10024F4) == 0, returns immediately (no draws)
             //   GATE 2: if g_drawSubmitMode (0x10024E8) != 0, calls 0x419160 instead of flush
             //   Normal path: calls 0x40E970 (clears draw state) -> DrawPass_FlushBatches (0x415260)
             //   -> 0x418AE0 (finalize) -> 0x4150F0 (tail call)
             //   g_drawSubmitReady set to 1 by BeginDrawPass (0x414940) during RenderFrame
@ 0x00415260 void __cdecl DrawPass_FlushBatches(void);
             //   2003-byte draw flush function. Iterates queued draw batches and calls
             //   EC9320 (DrawBatch_Execute) for each batch. Uses global batch lists at
             //   0x1002484..0x10024B8 (multiple render queues: opaque, alpha, fog, etc.)
             //   Return addresses 0x415613 and 0x415654 are from EC9320 calls (DrawIndexedPrimitive)
             //   within this function. These draws are NOT gated here — they were queued upstream
             //   by SceneTraversal, SectorVisibility, and PostSector_ObjectLoop during RenderFrame.
@ 0x00EC9320 int* __thiscall DrawBatch_Execute(void* this, uint param, int* prevShader);
             //   Iterates linked list at [this+8]. Per entry: activates shader if changed (vtable calls),
             //   then dispatches draw via vtable[4](param, prevEntry). This is the final draw submission
             //   that issues DrawIndexedPrimitive to D3D9.
@ 0x00414940 void __cdecl BeginDrawPass(int doSetup);
             //   Called at start of each render frame. Sets g_drawSubmitReady (0x10024F4) = 1.
             //   When g_drawSubmitMode (0x10024E8) != 0: calls 0x419140 then sets flag and returns.
             //   When mode == 0: initializes all draw batch lists, render state, and flush buffers.
@ 0x004F7410 char __cdecl IsLevelRenderable(void);
             //   Returns nonzero if a level is loaded and renderable.
             //   Checks global object count at _fails > 0, then walks state array 0x1116158.
             //   If all entries have bit 27 or 28 set, returns 0 (not renderable).
             //   Fallback: returns (loadDoneType != 7).
@ 0x00452510 void __cdecl RenderScene_FullPipeline(void* context);
             //   Outer render pipeline: calls 0x4E0690 (BeginScene?), 0x452140 (setup),
             //   SubmitMesh_Generic, 0x5D51C0, then RenderFrame_TopLevel(context), then 0x450E80 (post-frame)
             //   Called from 0x45DA5D inside GameLoop_Render (0x45CF80)
@ 0x0045CF80 void __cdecl GameLoop_Render(void);
             //   3715-byte main game loop render orchestrator. Called from WinMain (0x401F50).
             //   Contains the 0x45DA5D call to RenderScene_FullPipeline which is the primary game render path.
             //   Also manages scene transitions, camera setup, state blocks, and calls 0x452510.
@ 0x00415A40 void __cdecl Renderer_InitAndDraw(void);
             //   Checks g_renderNeedsInit (0x10024F4), calls 0x415260 (Renderer_DrawPass),
             //   then 0x4150F0 (Renderer_SetupBaseStates). Called from 0x40CBE0 fade controller.
@ 0x00415260 void __cdecl Renderer_DrawPass(void);
             //   2003-byte renderer draw pass. Sets up D3D render targets, frustum distance,
             //   uploads VS constants (c0-c15), sets sampler states, calls all render targets.
             //   Vtable calls at +0xE4 (SetRenderState), +0x10 (render target), +0xAC (SetSamplerState).
             //   Calls 0x0040EA60, 0x0040E9C0, 0x0040E580, 0x618450/0x618470 (light render),
             //   0x0040EAB0 (Renderer_ApplyRenderStateChanges), 0xEC9320/EC9710 (VS constant ops).
@ 0x0040CBE0 void __cdecl FadeController_DrawScene(int fadeMode);
             //   32 callers. Manages screen fade animation using globals at 0xF127B8-0xF127D4.
             //   Sets g_fadeTargetAlpha (0xF127D4) then calls 0x415A40 -> 0x415260 -> draw pass.
             //   Also calls 0x5D42C0 (post-draw cleanup). Critical link in ALL render dispatch paths.
@ 0x00442D40 void __cdecl RenderScene_PostProcess(void* data);  // called after scene traversal
@ 0x00402B10 void __cdecl CopyMatrixToGlobal(void* srcMatrix);  // copies 4x4 matrix to g_cameraMatrix (0xF3C5C0)
@ 0x00407010 void __fastcall ComputeProjectedSize(void* sizeParams);  // computes screen-space projection, clamps to bounds
@ 0x00446580 uint __cdecl LOD_AlphaBlend(uint baseColor, uint lodDistance);  // LOD fade: returns ARGB with alpha based on distance. esi=blend factor (0-0x1000)
@ 0x00406640 void __cdecl SubmitMesh_WithFlags(int flags, int meshData, void* bounds, int lodColor);  // mesh submission with oriented bounds
@ 0x00604BE0 void __cdecl SubmitMesh_Generic(int flags, int meshData, void* bounds, int count);  // generic mesh submission

// ============================================================
// Globals
// ============================================================

$ 0x01392E18 void* g_pEngineRoot       // pointer to engine base object
                                       // g_pEngineRoot + 0x000 = vtable
                                       // g_pEngineRoot + 0x00C = ref count (decremented by FUN_00ec72d0)
                                       // g_pEngineRoot + 0x020 = pointer to draw submitter object
                                       // g_pEngineRoot + 0x214 = pointer to TRLRenderer
$ 0x00F2A0D4 int g_cullMode_pass1      // D3DRS_CULLMODE value for first render pass (opaque)
                                       // 1=D3DCULL_NONE, 2=D3DCULL_CW, 3=D3DCULL_CCW
                                       // To disable all culling: write 1 here and to g_cullMode_pass2
$ 0x00F2A0D8 int g_cullMode_pass2      // D3DRS_CULLMODE value for second render pass (transparent/stencil)
$ 0x00EFDD64 float g_frustumDistanceThreshold  // = 16.0f, .rdata, used at 0x407162 for initial scale (16.0 * 1/512)
$ 0x00EFD404 float g_screenBoundsMin           // = -1.0, NDC left/bottom cull boundary
$ 0x00EFD40C float g_screenBoundsMax           // = 1.0, NDC right/top cull boundary
$ 0x010FC910 float g_farClipDistance            // far clip plane distance, varies per level
$ 0x00EFDD60 float g_smallObjectThreshold      // = 0.00390625 (1/256), min screen-space size before clamp
$ 0x00EFDD4C float g_lodFadeDistance           // = 5000.0, used for LOD fade offset
$ 0x00EFD8E4 float g_sceneScaleFactor          // = 0.001953 (1/512), multiplied with g_frustumDistanceThreshold
$ 0x00F11D0C float g_viewDistance              // = 512.0, base view distance
$ 0x00F0ECFC float g_zeroConstant              // = 0.0, comparison constant
$ 0x00F3C5C0 float[16] g_cameraMatrix          // current camera/view matrix (copied by 0x402B10)
$ 0x00F127E0 uint g_cachedTextureStageStateMask
$ 0x00F127E4 uint g_lastTextureStageStateMask
$ 0x00FFA720 uint g_currentDesiredRenderStates  // bitfield: bit21=cullmode, bit20=zwrite, bit12=alphatest, etc.
$ 0x013107F4 int g_deferredLightCount         // count of lights deferred by frustum cull
$ 0x013107F8 int g_deferredLightCapacity      // capacity of deferred light array
$ 0x010024E8 int g_drawSubmitLock             // when non-zero, Sector_SubmitObject skips ALL submissions (gate at 0x40C68B)
$ 0x010E5384 int g_renderPassState            // render pass flags; bits 8-15 checked in sector skip at 0x46B7EA
$ 0x010E5438 void* g_currentSectorPtr         // current sector pointer; compared at 0x46B7F2 to skip already-rendered sector
$ 0x013107FC int* g_deferredLightIndices      // pointer to array of deferred light indices
$ 0x01310800 int g_deferredLightInitFlag       // bit 0: set once during first RenderLights call
$ 0x010E537C void* g_pCurrentScene             // current scene/level object pointer
$ 0x010E5380 void* g_pPostProcessData          // passed to RenderScene_PostProcess
$ 0x01089E40 void* g_pSpecialRenderCallback1   // if non-null, calls 0x446920 after scene
$ 0x01089E44 void* g_pSpecialRenderCallback2   // if non-null, calls 0x4495C0 after scene

// ============================================================
// Sector/Portal Visibility System
// ============================================================

// Sector table: 8 entries of 0x5C bytes at fixed address
// Per entry layout:
//   [+0x00] dword  sectorIndex
//   [+0x04] byte   sectorState (0=free, 1=loading, 2=loaded, 4=dumping)
//   [+0x05] byte   flags1
//   [+0x06] byte   flags2 (bit 3 = loaded/visible, set by Sector_Activate 0x403720)
//   [+0x08] ptr    sectorDataPtr
//   [+0x3C] word   boundX (screen-space bounding rect left)
//   [+0x3E] word   boundY (screen-space bounding rect top)
//   [+0x40] word   boundW (screen-space width, SIGNED — negative = invisible)
//   [+0x42] word   boundH (screen-space height, SIGNED — negative = invisible)
//   [+0x43] dword  sectorType (1=standard mesh, 2=fullscreen overlay)
//   [+0x44] dword  portalFlags (OR'd from portal visibility results each frame)
$ 0x011582F8 char[0x2E0] g_sectorTable       // 8 sector entries, 0x5C bytes each
$ 0x010C5AA4 void* g_pObjectListHead          // head of active scene object linked list (next at +8)
$ 0x010E5384 uint32 g_renderFlags             // bit 20 = skip entire object loop rendering
$ 0x010E5438 void* g_pCameraSectorData        // current camera sector data (from GetCameraSector)
$ 0x010FCAE8 void* g_pPlayerCamera            // player/camera struct (sector index at +0xB2)
$ 0x010E5458 int g_fallbackSectorIndex        // fallback sector index when primary lookup fails
$ 0x010FC900 float[4] g_sectorScissorRect     // current sector viewport scissor (x_min, x_max, y_min, y_max)
$ 0x010FC920 float g_sectorMinWidth           // minimum sector width threshold for rendering
$ 0x010FC924 float g_sectorMinHeight          // minimum sector height threshold for rendering
$ 0x010E579C void* g_pCameraOverrideObj       // camera override object (vtable[0x28] = GetSectorIndex)
$ 0x00FFA718 uint32 g_postSectorVisibilityMask  // bitmask: bit N = post-sector object N is visible
$ 0x00F17904 byte g_sectorBypassFlag           // if set, 0x5C3C50 called instead of RenderVisibleSectors
$ 0x010E5424 int g_frameCounter                // incremented each RenderFrame call
$ 0x010024F4 int g_drawSubmitReady             // 1 = draw batches ready to flush, 0 = skip. Set by BeginDrawPass (0x414940)
$ 0x010024E8 int g_drawSubmitMode              // 0 = normal render, nonzero = special mode (0x419160 path)
$ 0x00F127B8 int g_fadeActive                  // nonzero = screen fade in progress
$ 0x00F127D4 int g_fadeAlpha                   // current fade alpha (0x80 = fully opaque)
$ 0x00F127C8 float g_fadeDelta                 // fade speed/direction
$ 0x00F48A70 float[16] g_viewMatrixForCull     // view matrix used by RenderQueue_FrustumCull (0x40C430)
$ 0x00F48AB0 float[16] g_secondaryMatrixForCull // secondary matrix used by RenderQueue_FrustumCull
$ 0x00F48A50 void* g_currentMeshBase           // set by Sector_SubmitObject before calling 0x40C430
$ 0x00F48A54 void* g_currentObjectEntry        // set by Sector_SubmitObject before calling 0x40C430
$ 0x00FFA714 int g_renderQueueIndex            // index into render queue at 0xFF9710
$ 0x010024D4 void* g_renderCommandBuffer       // returned by 0x413D60, render command buffer pointer
$ 0x01002510 void* g_renderCommandBuffer2      // returned by 0x413D70, secondary render buffer
$ 0x00F127B8 int g_fadeActive                  // nonzero = fade animation in progress
$ 0x00F127BC int g_fadeCurrentAlpha            // current alpha level (0-128, 0x80=fully opaque)
$ 0x00F127C0 int g_fadeTargetAlpha             // target alpha level for fade
$ 0x00F127C4 int g_fadeDeltaAccum             // accumulated fade delta from frame timing
$ 0x00F127C8 float g_fadeRateMultiplier       // fade speed multiplier
$ 0x00F127CC float g_fadeProgress             // current fade progress (float)
$ 0x00F127D4 int g_fadeTargetOpacity          // target opacity set by FadeController (0x80=normal)
$ 0x010024F4 int g_renderNeedsInit            // if set, Renderer_InitAndDraw reinits before drawing
$ 0x010024E8 int g_postDrawMode               // if nonzero, uses alternate post-draw path (0x419160 vs 0x40E970)
$ 0x01117560 void* g_pCutsceneObject           // if non-null, calls 0x44B7A0 for cutscene rendering
$ 0x0010024E8 int g_postSectorLoopDisable      // if non-zero, post-sector object loop at 0x40E2C0 is skipped
$ 0x00EFDE58 float g_maxObjectDrawDistance     // max draw distance for post-sector objects
$ 0x00EFDDB0 float g_nearLODThreshold         // near LOD threshold for post-sector objects
$ 0x00EFDE50 float g_farLODThreshold          // far LOD threshold for post-sector objects
$ 0x00F12016 byte g_postSectorLoopEnable       // if 0, entire post-sector object loop at 0x40E2C0 is skipped
$ 0x011397C0 void* g_pSectorDataBase           // base of sector data structure
             //   +0x3854: sector command buffer start (scanned by 0x5BE540)
             //   +0x33D8: 0x47C-byte per-sector visibility bitfield (checked by 0x5BE7B0)
$ 0x011397CC void* g_pSectorCommandBufferEnd   // end pointer for sector command buffer scan

@ 0x0046C180 void __cdecl SectorVisibility_RenderVisibleSectors(void* sceneData);
             //   Iterates g_sectorTable[0..7]. Per sector: checks [+5]&8 (visible flag) and [+4]==0 (enabled).
             //   Sectors failing = SKIPPED. Type 1 sectors render via Sector_RenderMeshes (0x46B7D0),
             //   type 2 via Sector_RenderFullscreen (0x46B890).
             //   CULL GATE 1: je at 0x46C194 (6 bytes) -- skips if visibility flag not set
             //   CULL GATE 2: jne at 0x46C19D (6 bytes) -- skips if enabled byte != 0
@ 0x0046B7D0 void __cdecl Sector_RenderMeshes(void* sectorData);
             //   Renders meshes in a sector. Checks mesh flags [+0x20]&1 (disabled), &0x20000, &0x200000.
             //   Inner mesh submission via 0x412F20 then 0x458630.
@ 0x0046B890 void __cdecl Sector_RenderFullscreen(void* sectorData);
             //   Alternate sector render for type 2 (fullscreen) sectors.
@ 0x0046C320 void __cdecl Sector_IterateMeshArray(void* meshArray, void* sceneCtx);
             //   Iterates mesh array, checks [mesh+0x5C]&0x82000000 cull flags. Calls 0x458630.
@ 0x0046C4F0 void __cdecl SetupCameraSector(void* sectorEntry);
             //   Portal walk from camera's sector. Iterates portal connection array
             //   (stride 0xA0, count at *(*sectorData+0xC), entries at *(*sectorData+0x10)).
             //   Per portal: checks connected sector loaded/active, dot product vs portal plane,
             //   2D rect clipping. For visible portals: computes screen-space bounding rect
             //   in sector[+0x3C..+0x42]. Unreachable sectors keep default (negative) bounds.
@ 0x0046D1D0 void __cdecl SectorPortalVisibility(void* sectorEntry);
             //   Called after SetupCameraSector, before RenderVisibleSectors.
             //   RESETS all 8 sector bounding rects to x=0x200, y=0x1C0, w=-512, h=-448
             //   (making unreachable sectors fail the width/height check).
             //   Then for portal-visible sectors: ORs portal results into sector[+0x44].
             //   PATCH TARGET: 0x46D1E0/E5/EA — change negative width/height defaults to
             //   positive fullscreen values (x=0, w=0x200, h=0x1C0) to force all sectors visible.
@ 0x0046D0A0 bool __cdecl PortalVisibilityTest(void* portalEntry, void* boundsRect);
             //   Dot product of camera position vs portal plane normal.
             //   Returns false if camera is behind portal, true if in front.
@ 0x0046C360 int __cdecl PortalRectClip(void* portalGeom, void* clipRect);
             //   2D portal rectangle clipping against current frustum scissor.
@ 0x00403720 void __cdecl Sector_ActivateAndResolveTextures(void* sectorEntry);
             //   Sets sector[+6] |= 8 (loaded/visible flag). Resolves texture references
             //   via lookup table at 0xF3C600. Called from sector load finalization (0x5D59D7).
@ 0x0046B900 void __cdecl Sector_GetCameraFrustumParams(void* sectorEntry, ...);
             //   Extracts camera frustum near/far planes and fog parameters from sector data.
@ 0x00450B00 void __cdecl RenderFrame(void);
             //   Main render pipeline: GetCameraSector -> SetupCameraSector -> SectorPortalVisibility
             //   -> object loop -> RenderVisibleSectors (0x46C180) -> RenderScene (0x443C20).
             //   Controls sector bypass via g_sectorBypassFlag (0xF17904).
@ 0x00450A80 void* __cdecl GetCameraSector(void);
             //   Returns sector data for camera position. Uses g_pCameraOverrideObj vtable[0x28],
             //   or falls back to Sector_FindByIndex via player struct sector index at +0xB2.
@ 0x005D4870 void* __cdecl Sector_FindByIndex(int sectorIndex);
             //   Linear search of g_sectorTable for entry with [+4]==2 (active) and [+0]==sectorIndex.
@ 0x00458630 void __cdecl MeshSubmit(void* meshEntry, short sectorIdx, char forceVisible);
             //   Per-mesh draw submission. Calls MeshSubmit_VisibilityCheck (0x454AB0) first.
             //   If that returns nonzero and forceVisible==0, mesh is skipped.
@ 0x00454AB0 int __cdecl MeshSubmit_VisibilityCheck(void* meshEntry);
             //   Returns nonzero if mesh should be culled (not visible).
@ 0x0040C650 void __cdecl Sector_SubmitObject(void* meshBase, void* objectEntry);
             //   Object submission called from Sector_RenderMeshes (0x46B7D0) after flag filters pass
@ 0x0040D290 void __cdecl PostSector_SubmitObject(void* timestamp, void* lodData, void* sectorBase, int flags, int handle, void* morphData);
             //   Object submission called from post-sector object loop (0x40E2C0)
             //   CRASH BUG: lodData (param2) can be NULL when anti-culling patches force submission.
             //   Crash at 0x40D2AF: mov ecx,[esi+0x20] with esi=NULL. Needs null guard.
@ 0x0040D7E0 void __cdecl PostSector_PrepareObject(void* timestamp, int objIndex, void* arrayBase);
             //   Pre-submission setup in post-sector loop
@ 0x0040DA40 void __cdecl PostSector_FinishObject(void* timestamp, int objIndex, void* arrayBase);
             //   Post-submission cleanup in post-sector loop
@ 0x0040E2C0 void __cdecl PostSector_ObjectLoop(void* objectArray);
             //   Post-sector moveable object iteration loop. Checks bitmask at 0xFFA718,
             //   walks linked list at [entry+0x110] -> [+0x228]/[+0x22C],
             //   filters by flags [+0xA4]&0x800, [+0xA8]&0x10000, distance against 0xEFDE58/0xEFDDB0.
             //   Entries spaced 0x130 bytes. Part of RenderVisibleSectors tail code.
@ 0x00455A50 float __cdecl Object_ComputeDistance(void* obj);
             //   Computes distance from camera to object, used by post-sector distance culling
@ 0x00531B10 int __cdecl Object_HasComponentType(void* obj, short typeId);
             //   Checks [obj+0x1C0] for magic 0xB00B at word[+4] and matching typeId at word[+2].

// ============================================================
// Scene Graph (Path 2 — effects/particles/billboards, NOT sector meshes)
// ============================================================
// Scene graph root at 0x107F6B8 has three linked lists:
//   +0x24: mesh effect nodes (walked by SceneTraversal loop 1)
//   +0x2c: billboard/sprite nodes (walked by SceneTraversal loop 2)
//   +0x34: scene objects (walked by SceneGraphBuilder 0x408130)
// Node layout: [+0]=listPtr, [+4]=next, [+8]=flags, [+0xC]=meshID,
//   [+0x10..+0x1C]=position, [+0x20..+0x3C]=bounding box, [+0x3C]=color
$ 0x0107F6B8 void* g_sceneGraphRoot      // scene graph root, passed to RenderScene
@ 0x00443C20 void __cdecl RenderScene(void* sceneData, void* cameraMatrix);
             //   Copies camera matrix, calls SceneTraversal_CullAndSubmit, then post-processing.
@ 0x00436AF0 void __cdecl SceneGraphBuilder(void* sceneGraph, void* cameraMatrix);
             //   If sceneGraph+0x34 list non-empty, calls SceneObjectWalker (0x408130).
@ 0x00408130 void __cdecl SceneObjectWalker(void* cameraMatrix, void* sceneGraph);
             //   Walks scene object list at sceneGraph+0x34. Per object: frustum cull,
             //   matrix setup, lighting, then submits via 0x408CC0 or async path.
@ 0x00436110 void* __cdecl SceneNodeAllocator(void* sceneGraph);
             //   Allocates scene node from free list at sceneGraph+0x38, fallback to sceneGraph+0x40.
@ 0x0045BD30 void __cdecl ListLinker(void* listHead, void* node);
             //   Inserts node into doubly-linked list. node[0]=listHead, node[1]=next, updates prev.
@ 0x0045BDA0 void* __cdecl ListUnlinker(void* listHead);
             //   Removes first node from doubly-linked list, returns it.

// ============================================================
// Render state bitfield (ebp in ApplyRenderStateChanges)
// ============================================================
// Bit 11 (0x800):      D3DRS_SHADEMODE (8)
// Bit 12 (0x1000):     Alpha test enable/disable (via 0x40EA10)
// Bit 20 (0x100000):   D3DRS_ZWRITEENABLE (28)
// Bit 21 (0x200000):   D3DRS_CULLMODE (22) -- 0=D3DCULL_NONE, 1=D3DCULL_CW
// Bit 22 (0x400000):   Sampler min filter
// Bit 23 (0x800000):   Sampler mag filter
// Bit 29 (0x20000000): D3DRS_DESTBLEND (55)
// Bit 30 (0x40000000): D3DRS_ALPHATESTENABLE (56)
// Bit 31 (0x80000000): D3DRS_FILLMODE (24)

// ============================================================
// LightVolume vtable methods (vtable-dispatched, no direct xrefs)
// ============================================================
@ 0x6124E0 void __thiscall LightVolume_UpdateVisibility(int materialFilter, float alpha);
@ 0x612810 void __thiscall LightVolume_UpdateColors(int materialFilter, uint8_t channelIdx, float* colorRGBA);
@ 0x611EB0 void __thiscall LightVolume_EnsureRenderSlots(void);
@ 0x611990 void __thiscall LightVolume_InitRenderData(void* lightVol);
@ 0x6101C0 void* __cdecl TransientHeap_Alloc(void);
@ 0x600060 void* __thiscall TransientHeap_AllocBlock(int size);
@ 0x5DA0C0 void __cdecl TransientHeap_Panic(const char* msg);

$ 0xEFD40C float g_fOne = 1.0f
$ 0xF0ECFC float g_fZero = 0.0f
$ 0xEFDE04 float g_OneOver255 = 0.003922f
$ 0xEFDDD0 const char* g_szOutOfTransientHeap = "Out of transient heap space!"

// ============================================================
// Water/Animated Texture System
// ============================================================
// Texture animation uses two VS constant paths:
//   c6:      textureScroll {offsetU, offsetV, ?, scale} — proxy handles this
//   c24-c27: texture transform matrix (3 rows) — NOT handled by proxy
//   c17:     UV scroll offset {scrollU, scrollV, 0, 0} — NOT handled by proxy
//
// Material flag 0x40 at [material+0x20] enables UV scroll animation.
// Scroll index packed in uint16 at [material+0x2C]: lo=U_idx, hi=V_idx.
// Scroll = idx * (1/255) * g_scrollMultiplier(runtime at 0xF0ECFC).
//
// Water vertex displacement (Gerstner wave) is CPU-side via Lock/Unlock.
@ 0x60EE40 void __thiscall TextureMatrix_Setup(void* drawableCtx, void* renderCtx);
@ 0x60F9AF void WaterDrawable_Submit(void);
@ 0x5E7A70 void __thiscall WaterVertex_GerstnerSimulate(float* waveState, short* srcVerts, short* dstVerts, int vertCount);
@ 0x619310 void __thiscall WaterFX_Init(void* this, char enabled, unsigned int p3, unsigned int p4, unsigned int p5, unsigned int p6);
@ 0x607B50 void TextureStage_SetSamplerAndTexture(int stageIdx, int texturePtr);
@ 0x604B20 void __fastcall Material_SetFogMode(int objPtr);
@ 0xECBA40 void __thiscall Renderer_SetVSConstantF_Wrapper(int startReg, float* data, int count);

// ============================================================
// Code caves in .text section (INT3 padding between functions)
// ============================================================
// 0xEDF9E3: 29 bytes CC padding (0xEDF9E3-0xEDF9FF) -- used for null-check trampoline
// 0xEE2602: 30 bytes CC padding (0xEE2602-0xEE261F) -- spare cave

// ============================================================
// Terrain rendering path — PATCHED
// ============================================================
// Separate from SceneTraversal_CullAndSubmit and SectorVisibility paths.
@ 0x0040ACF0 void* __thiscall TerrainDrawable_Ctor(void* this, void* pMeshBlock, void* pTerrainData, void* pFlags, void* pContext);
             //   Constructor for terrain draw descriptor (0x30 bytes at this/esi).
             //   Sets vtables [this]=0xEFDE08, [this+4]=0xF12864.
             //   Copies mesh flags from [pMeshBlock] to [this+0x1C].
@ 0x0040ADF0 void __thiscall TerrainDrawable_Submit(void* this, void* pPrevDrawable);
             //   Dispatch: calls vtable[0] on the allocated batch at [this+0x28].
             //   Increments global draw counter at 0x10024CC.
@ 0x0040AE20 void __thiscall TerrainDrawable_Execute(void* this, int mode, void* pPrevDrawable);
             //   Actual terrain rendering: matrix setup, batch draw, LOD selection.
             //   CULL GATE: 0x40AE3E — 6-byte JNE, skips when flag 0x20000 set AND mode==0x1000.
             //   NULL GATE: 0x40B0F4 — 6-byte JE, skips when [0x1392E18+0x20]==NULL.
             //   Called via vtable (0 direct xrefs). 0x40AE20 is [vtable+0] for terrain objects.
             //   Calls 0x414280 (shader selection), 0xECB0B0 (VB lookup).
             //   Returns descriptor ptr. NO culling jumps — pure setup.
             //   Called from terrain loop at 0x40C0E9 (single call site).
             //   ret 0x10 at 0x40ADED.
@ 0x0040AE20 void __thiscall TerrainDrawable_Dispatch(void* this, int drawMode, void* prevDesc);
             //   Terrain draw dispatch — the actual rendering function.
             //   CULL JUMP 1: 0x40AE3E (jne 0x40B1A6, 6 bytes: 0F 85 62 03 00 00)
             //     Skips draw if drawMode==0x1000 AND [this+0x1C]&0x20000.
             //   NULL CHECK: 0x40B0F4 (je 0x40B1A6, 6 bytes) — DO NOT NOP (crash).
             //   Sets world matrix via FPU 4x4 multiply loop at 0x40AFB0-0x40B06C.
             //   Final draw via 0xEC91B0 or vtable [eax+0x148].
             //   ret 8 at 0x40B1AC.
@ 0x0040ADF0 void __thiscall TerrainDrawable_Submit(void* this, void* meshData);
             //   Small thiscall that calls 0x413D70 -> 0xEC9DC0, then vtable dispatch
             //   [ecx]->[edx] to submit the terrain descriptor. ret 4 at 0x40AE16.
@ 0x00454AB0 int __fastcall MeshSubmit_VisibilityGate(void* meshEntry);
             //   Per-mesh visibility check. ESI = meshEntry on entry.
             //   Walks linked list at g_pObjectListHead (0x10C5AA4), comparing
             //   [node+0x1D0] to mesh sector ID [esi+0x54].
             //   Fallback 1: sub_5BE540 scans sector command buffer.
             //   Fallback 2: sub_5BE7B0 checks per-sector visibility bitfield
             //     at [0x11397C0+0x33D8+idx/8], bit (idx%8).
             //   Returns 1 = CULL (sets [esi+0x5C] |= 0x80000000).
             //   Returns 0 = VISIBLE.
             //   PATCH: 33 C0 C3 at 0x454AB0 (xor eax,eax; ret) forces all visible.
             //   Single caller: MeshSubmit at 0x458641.
@ 0x005BE540 int __cdecl SectorCommandBuffer_Search(int sectorId);
             //   Scans sector command buffer at [0x11397C0+0x3854] for matching
             //   sector ID. Word-aligned entries, type 3 checks word [+0xA],
             //   type 4 checks [+0xC]>>15 & 0x7FFF. Returns nonzero if found.
@ 0x005BE7B0 int __cdecl SectorVisibility_BitfieldCheck(void* meshEntry);
             //   Reads sector index from [obj+0x54], bounds-checks 0..0x3FFF,
             //   tests bit in 0x47C-byte bitfield at [0x11397C0+0x33D8].
             //   Returns 1 if bit SET (sector already rendered = cull to avoid double-draw).
             //   Returns 0 if bit CLEAR (sector unvisited = should draw).

// ============================================================
// Streaming / Object Tracker System
// ============================================================
// Object Tracker: 94 entries (MAX_OBJECTS=0x5E) at 0x11585D8, stride 0x24
// States: 0=free, 1=loading, 2=loaded(drawable), 3=marked-evict, 4=evicting, 5=freed
// Sector Table: 8 entries at 0x11582F8, stride 0x5C
// Sector states: 0=free, 1=loading, 2=loaded, 4=dumping
// Sector +0x06 bit 0 = KEEP_LOADED (protects from eviction by SectorEviction_ScanAndUnload)

struct ObjectTrackerEntry {
    uint32_t loadHandle;        // +0x00
    uint32_t resourceHash;      // +0x04
    void* objectData;           // +0x08  mesh header, materials, textures
    int16_t resourceKey;        // +0x0C  index into resource map at [0x10EFC90]
    int16_t state;              // +0x0E  0=free, 1=loading, 2=loaded, 3=evict, 5=freed
    uint32_t refCount;          // +0x10
    int8_t depCount;            // +0x12  dependency count
    int8_t deps[17];            // +0x13  dependency indices
};  // total 0x24 bytes

@ 0x005D5390 int __cdecl ObjectTracker_FindOrLoad(int resourceKey, void* gameTracker, int param3);
             //   Main object lookup. Searches 94-entry table at 0x11585D8.
             //   If not found, initiates async load via AsyncLoader_StartLoad (0x5BADD0).
             //   When tracker is full, calls ObjectTracker_EvictUnneeded (0x5D44C0) to free slots.
             //   Returns tracker index (0..93) or -1 on failure.
             //   MAX_OBJECTS limit checked at: 0x5D53F2, 0x5D53FA, 0x5D5424, 0x5D542F,
             //   0x5D5459, 0x5D5464, 0x5D5481, 0x5D55AC (all cmp reg, 0x5E)
@ 0x005D4240 int __cdecl ObjectTracker_Resolve(int resourceKey, int param2);
             //   Returns pointer to object data (entry_base + index * 0x24 + 0x11585D8)
             //   or 0 if not found. MeshSubmit checks return + 0x0E == 2 (loaded state).
@ 0x005D44C0 void __cdecl ObjectTracker_EvictUnneeded(void* sectorEntry);
             //   Marks unreferenced objects as state 3, then frees them via ObjectTracker_FreeEntry.
             //   Eviction criteria: state==2, not used this frame, not in dependency list,
             //   not in g_hiddenMeshList or active scene list.
             //   9 call sites across game code. Key site: 0x5D5436 (inside FindOrLoad when full).
@ 0x005D4380 void __cdecl ObjectTracker_FreeEntry(void* entry);
             //   Releases object data, removes dependencies, sets state to 5 then 0.
@ 0x005D5200 void __cdecl ObjectTracker_LoadComplete(void* objectData, void* trackerEntry);
             //   Async load completion callback. Sets entry state to 2 (loaded/drawable).
             //   Validates model version magic 0x4C20453, resolves dependencies.
@ 0x005D4F30 void __cdecl SectorEviction_ScanAndUnload(void);
             //   Iterates all 8 sector entries. Unloads any sector where:
             //   state != 0 AND state != 4 AND lastFrameAccessed != g_frameCounter
             //   AND (flags & 1) == 0 (not keep-loaded).
             //   Called from: 0x5D31D9 and 0x5D5F59 (during level transitions).
             //   PATCH: NOP both call sites to prevent sector eviction.
@ 0x005D4DA0 void __cdecl Sector_Unload(void* sectorEntry, char preserveData);
             //   Unloads a single sector: clears dependencies, frees resources,
             //   sets sector state to 4 (dumping).
@ 0x005D5B60 void* __cdecl Sector_LoadOrFind(char* name, void* param2, char keepLoaded);
             //   Finds existing sector by name or allocates new slot and initiates async load.
             //   Sets sector state to 1 (loading), flags, copies name to entry.
@ 0x005D5D80 void __cdecl Sector_WaitForLoad(void* sectorEntry, char doFinalSetup);
             //   Blocks until sector async load completes (spins with 5ms Sleep).
@ 0x005C3C20 void __cdecl StreamUnitDataLoaded(void* streamUnit);
             //   Callback when stream unit data finishes loading from BIGFILE.DAT.
@ 0x005C2010 void __cdecl StreamUnitDataDumped(void* streamUnit);
             //   Callback when stream unit data is evicted/dumped.
@ 0x005BADD0 int __cdecl AsyncLoader_StartLoad(void* params);
             //   Initiates async file read from BIGFILE.DAT.
@ 0x005D4470 char __cdecl ObjectTracker_IsReferencedBySector(void* entry);
             //   Checks if object is referenced by any loaded sector dependency list.
             //   Returns 1 if referenced (protected from eviction), 0 if orphaned.

$ 0x011585D8 ObjectTrackerEntry g_objectTracker[94]
             //   Object tracker table. 94 entries x 0x24 bytes = 0x870 bytes.
             //   Immediately followed by data at 0x1159310.
$ 0x010EFC90 void* g_resourceMap
             //   Resource key -> tracker index mapping. [key*8] = tracker index, [key*8+4] = hash.
$ 0x01159314 void* g_sectorObjectMapping
             //   Maps sector indices to object tracker entries.
$ 0x010E5424 int g_frameCounter
             //   Incremented each frame. Used for LRU eviction in sector/object systems.
$ 0x01140890 void* g_asyncLoaderVtable
             //   Async loader vtable pointer (used in StreamUnitDataLoaded).
$ 0x011408C0 int g_pendingLoadCount
             //   Number of pending cross-sector load operations.
$ 0x011408C8 void** g_pendingLoadArray
             //   Array of pending cross-sector load entries.
$ 0x01140898 int g_streamLoadCompleteFlag
             //   Set to 1 when stream unit data loading completes.

// ============================================================
// Game Loop / Level Loading
// ============================================================
// GameTracker struct lives at 0x010E5370 (global, ~2KB+)
//   +0xC0: flags (bit 0 = level change requested)
//   +0xCC: char[?] levelName (destination level name buffer)
//   +0xE1: char busy flag (if nonzero, level change is rejected)

$ 0x010E5370 GameTracker gameTracker
$ 0x010E5430 uint32_t gameTrackerFlags
$ 0x010E5434 uint32_t gameTrackerFlags2
$ 0x010E5450 int loadDoneType
$ 0x010E5451 char loadState

@ 0x00451970 void __cdecl GAMELOOP_RequestLevelChangeByName(char* name, GameTracker* gameTracker, int doneType);
@ 0x00451870 void __cdecl GAMELOOP_SetLoadDoneType(int doneType);
@ 0x005C9A40 void __cdecl GAMELOOP_ResetSomething(void);
@ 0x00452900 void __cdecl GAMELOOP_SetStateVar(int index, uint32_t value, char callHandler);

// ============================================================
// CRT Functions (MSVC statically linked)
// ============================================================
@ 0x00EE8383 int __cdecl _output(void* stream, char* format, va_list argptr);
@ 0x00EE534E int __cdecl sprintf(char* buf, char* format, ...);
@ 0x00EE52F5 int __cdecl sprintf_variant(char* buf, char* format, ...);
@ 0x00EE6724 int __cdecl _snprintf(char* buf, int maxlen, char* format, ...);
@ 0x00EE7043 int __cdecl _vsnprintf(char* buf, int maxlen, char* format, va_list argptr);

$ 0x00F2B3F8 char* g_null_string_narrow
$ 0x00F2B3FC wchar_t* g_null_string_wide
$ 0x00F0A398 uint8_t _chartype_table[256]

// ============================================================
// ProcessPendingRemovals — node cleanup for 3 linked lists
// ============================================================
@ 0x00436680 void __cdecl ProcessPendingRemovals(void);
// Iterates 3 linked lists, checks field_48->0xA4 and field_AC->0xA4
// for "pending removal" flag (bit 0x20), unlinks matching nodes.
// Callers: 0x457A0A, 0x461999, 0x5D4D24

$ 0x0107F6E4 void* g_pendingRemovalList1_head
$ 0x0107F6DC void* g_pendingRemovalList2_head
$ 0x0107F6EC void* g_pendingRemovalList3_head
$ 0x0107F6F8 void* g_pendingRemoval_current
$ 0x0107F6C0 void** g_slotArray_base
$ 0x0107F6F0 void* g_pendingRemoval_freeList

@ 0x0045BD50 void __cdecl PendingRemoval_Unlink(void* node);
@ 0x0045BD30 void __cdecl ListLinker(void* listHead, void* node);
@ 0x00461540 void __cdecl PendingRemoval_CleanupAC(void* field_AC);

// ============================================================
// Mesh submission pipeline — crash root cause analysis
// ============================================================
@ 0x0046C320 void __cdecl Sector_IterateMeshArray(int meshArray, int sceneCtx);
// meshArray+4 = count, meshArray+8 = meshEntry* (stride 0x70)
// Flag test at 0x46C337: test [meshEntry+0x5C], 0x82000000
// Bit 0x80000000 = already submitted, Bit 0x02000000 = not-submittable (resource not loaded)

@ 0x00458630 int __cdecl MeshSubmit(int meshEntry, int sectorIdx, int forceVisible);
// Uses meshEntry+0x50 (resource key) -> ObjectTracker_Resolve
// Alternate path at 0x45896C when obj flags bit 0x100 OR mesh flags bit 0x200000

@ 0x00461A90 int __cdecl MeshSubmit_AlternatePath(int meshEntry, int sectorIdx);
// Lighter mesh submission — no VisibilityGate check, calls command dispatch (0x4458B0)

@ 0x005D4240 int __cdecl ObjectTracker_Resolve(int resourceKey, int param2);
// Returns (slot_index * 0x24 + 0x11585D8) or 0 if not found

@ 0x005D5390 int __cdecl ObjectTracker_FindOrLoad(int resourceKey);
// Looks up resource in tracker (0x11585D8 array, 0x5E slots, stride 0x24)
// On miss: constructs path via 0x45C730 -> sprintf("%s%s.%s", base, name, "drm")
// CRASH: if resourceKey is stale, _g_resourceMap[resourceKey*8-4] -> garbage -> sprintf %s crash

@ 0x005D4870 int __cdecl ResourceLookup_ByHash(int hashKey);
// Linear search through 0x11582F8 (stride 0x5C) for matching hash

@ 0x004266A0 void __cdecl DebugPrint(char* fmt, ...);
// sprintf -> OutputDebugString wrapper. 10 call sites in the binary.

@ 0x0045C730 void __cdecl ConstructObjectPath(char* buf, int namePtr, char* ext);
// sprintf(buf, "%s%s.%s", ...) with ext typically "drm"

@ 0x00EE534E int __cdecl sprintf_impl(char* buf, char* fmt, ...);
// Statically linked MSVC CRT sprintf

@ 0x00EE52F6 int __cdecl sprintf_impl2(char* buf, char* fmt, ...);
// Alternate sprintf entry, calls _output at 0xEE8383 directly

@ 0x00EE8383 int __cdecl _output(void* stream, char* fmt, va_list args);
// MSVC CRT _output (printf formatter). Crash at 0xEE88AD when %s ptr is invalid.

@ 0x00454A00 int __cdecl RenderContext_Alloc(void);
// Allocates from free list at 0x10C5AAC. Returns render context ptr or 0.

@ 0x004458B0 int __cdecl RenderCommand_Dispatch(int obj, int param2, uchar* cmdData, int param4, int param5, uint param6);
// 49-case switch table at 0x4460F0 for rendering commands

$ 0x011585D8 ObjectTrackerEntry g_objectTracker[0x5E]
// stride 0x24, slot+0x0C = object name string (used by DebugPrint %s)
// slot+0x0E = state (2 = loaded)

$ 0x011582F8 ResourceEntry g_resourceEntries[]
// stride 0x5C, searched by ResourceLookup_ByHash
