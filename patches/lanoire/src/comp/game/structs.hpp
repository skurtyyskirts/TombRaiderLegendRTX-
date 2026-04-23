#pragma once

namespace comp::game
{
	// LA Noire packs each named shader constant into a 32-bit handle.
	// Layout (see patches/lanoire/findings.txt section 3):
	//   bits 24-31 : float4 register count
	//   bits 16-23 : unused / flags
	//   bits  8-15 : Vertex Shader register (0xFF = not used)
	//   bits  0- 7 : Pixel Shader register  (0xFF = not used)
	struct packed_handle
	{
		std::uint32_t raw = 0;

		bool       empty()      const { return raw == 0; }
		std::uint8_t count()    const { return static_cast<std::uint8_t>((raw >> 24) & 0xFF); }
		std::uint8_t vs_reg()   const { return static_cast<std::uint8_t>((raw >>  8) & 0xFF); }
		std::uint8_t ps_reg()   const { return static_cast<std::uint8_t>((raw >>  0) & 0xFF); }
		bool       in_vs()      const { return vs_reg() != 0xFF; }
		bool       in_ps()      const { return ps_reg() != 0xFF; }
	};

	// LA Noire renderer singleton pointer lives at the absolute VA below.
	// The renderer object itself has 1065 xrefs and its vtable is at 0x124CC54.
	//
	// Known byte offsets into the renderer object (renderer-init at 0x00D58E00
	// calls GetConstantByName for each named constant and stores the returned
	// packed handle at these slots):
	//
	//   0x280 (DWORD 0xA0) : ElapsedTime
	//   0x284 (DWORD 0xA1) : ModelMatrix         <-- per-draw World
	//   0x288 (DWORD 0xA2) : EyePosition
	//   0x290 (DWORD 0xA4) : ViewInverse
	//   0x29C (DWORD 0xA7) : ModelViewMatrix
	//   0x2A0 (DWORD 0xA8) : ModelViewProjectionMatrix
	//   0x2A4 (DWORD 0xA9) : ModelViewMatrixInverse
	//   0x2A8 (DWORD 0xAA) : NormalMatrix
	//   0x5B8              : pointer to the DX9 AbstractDevice9 wrapper
	//   0x5C0              : 4x4 Projection matrix (16 floats)
	//   0x690              : window width  (float)
	//   0x691              : window height (float)
	constexpr std::uint32_t renderer_singleton_ptr_va = 0x015264E0;

	constexpr std::uintptr_t renderer_off_model_matrix_hdl   = 0x284;
	constexpr std::uintptr_t renderer_off_mvp_handle         = 0x2A0;
	constexpr std::uintptr_t renderer_off_view_inverse_hdl   = 0x290;
	constexpr std::uintptr_t renderer_off_eye_position_hdl   = 0x288;
	constexpr std::uintptr_t renderer_off_normal_matrix_hdl  = 0x2A8;
	constexpr std::uintptr_t renderer_off_projection_matrix  = 0x5C0;
}
