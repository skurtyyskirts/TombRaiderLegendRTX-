---
name: TRL Renderer Architecture
description: Tomb Raider Legend renderer VS constant register layout and matrix upload pipeline discovered via static analysis of trl.exe
type: project
---

Tomb Raider Legend uses a custom D3D8 renderer (converted to D3D9 via dxwrapper). Key architectural findings:

- Main renderer object accessed via global at 0x01392E18 -> [root+0x20] -> device at this+0xC
- Matrices stored row-major internally, transposed to column-major for HLSL upload
- Primary matrix upload via Renderer_UploadViewProjMatrices (0xECBB00): uploads c0-c7 (WVP) and c8-c15 (VP) as two 8-register batches
- Dirty flags at this+0x580 (view) and this+0x581 (proj) control which batch uploads
- Generic SetVSConstantF wrapper at 0xECBA40 with 34 callers covering c0 through c96
- Blend mode switcher at 0xECBC20 is NOT a constant upload function (maps blend IDs to render states)

**Why:** Understanding the constant register layout is needed for RTX Remix FFP proxy shader authoring.
**How to apply:** Use the register map in kb.h when writing replacement vertex shaders. c0-c3 = WVP, c8-c11 = View (transposed), c39 = utility {2,0.5,0,1}.
