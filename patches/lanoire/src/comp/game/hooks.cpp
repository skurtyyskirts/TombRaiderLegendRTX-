#include "std_include.hpp"
#include "game.hpp"

#include "shared/common/ffp_state.hpp"

namespace comp::game
{
	namespace
	{
		// LA Noire absolute addresses — Steam build v1.2.1.0 (build 2382).
		//
		//   SetCameraMatrices (0x00D5E910) — __fastcall
		//     ECX = renderer
		//     EDX = viewMatrix*  (64-byte 4x4 + trailing camera-pos vec4)
		//     [esp+4]  baseMatrix*
		//     [esp+8]  flag0
		//     [esp+0xC] writeToSnapshot
		//
		//   SetShaderConstant (0x00D60AB0) — __thiscall
		//     ECX = packed_handle
		//     [esp+4] = data*   (count * 16 bytes; count = handle.bits[24..31])
		//
		//   SetPerDrawConstants (0x00E14B70) — __fastcall, not currently hooked.
		//     ModelMatrix already arrives via SetShaderConstant matched on the
		//     handle at renderer+0x284. A direct hook here is an alternative if
		//     SetShaderConstant is ever bypassed.
		constexpr std::uintptr_t ADDR_SET_CAMERA_MATRICES = 0x00D5E910;
		constexpr std::uintptr_t ADDR_SET_SHADER_CONSTANT = 0x00D60AB0;

		using set_camera_matrices_t = void (__fastcall *)(void*       renderer   /*ecx*/,
		                                                  const float* view_matrix /*edx*/,
		                                                  const void*  base_matrix,
		                                                  std::uint32_t flag0,
		                                                  std::uint32_t write_to_snapshot);
		using set_shader_constant_t = void (__fastcall *)(std::uint32_t packed_handle /*ecx*/,
		                                                  std::uint32_t /*edx unused by thiscall*/,
		                                                  const void*   data);

		set_camera_matrices_t set_camera_matrices_og = nullptr;
		set_shader_constant_t set_shader_constant_og = nullptr;

		void __fastcall set_camera_matrices_hook(void*        renderer,
		                                         const float* view_matrix,
		                                         const void*  base_matrix,
		                                         std::uint32_t flag0,
		                                         std::uint32_t write_to_snapshot)
		{
			if (view_matrix)
				shared::common::ffp_state::get().set_lanoire_view(view_matrix);

			set_camera_matrices_og(renderer, view_matrix, base_matrix, flag0, write_to_snapshot);
		}

		void __fastcall set_shader_constant_hook(std::uint32_t packed_handle_raw,
		                                         std::uint32_t edx_unused,
		                                         const void*   data)
		{
			if (data)
			{
				auto& ffp = shared::common::ffp_state::get();

				// ModelMatrix — 7-reg count (first 16 floats = World matrix).
				// The count=7 guard avoids matching an uninitialized handle
				// slot during renderer init.
				if (const auto mm = get_model_matrix_handle();
				    !mm.empty() && mm.count() == 7 && packed_handle_raw == mm.raw)
				{
					ffp.set_lanoire_world(static_cast<const float*>(data));
				}
				// ViewInverse — 3-reg count (3 rows × 4 floats = compact affine
				// 3x4). Reconstruct as 4x4 with [0,0,0,1] 4th row and invert
				// via D3DX to produce the View matrix that FFP SetTransform
				// expects. This path replaces the removed SetCameraMatrices
				// hook that crashed on some call sites with a bad EDX.
				else if (const auto vi = get_view_inverse_handle();
				         !vi.empty() && vi.count() == 3 && packed_handle_raw == vi.raw)
				{
					const auto* s = static_cast<const float*>(data);
					D3DXMATRIX view_inv(
						s[0],  s[1],  s[2],  s[3],
						s[4],  s[5],  s[6],  s[7],
						s[8],  s[9],  s[10], s[11],
						0.0f,  0.0f,  0.0f,  1.0f);
					D3DXMATRIX view;
					if (D3DXMatrixInverse(&view, nullptr, &view_inv))
						ffp.set_lanoire_view(reinterpret_cast<const float*>(&view));
				}
			}

			set_shader_constant_og(packed_handle_raw, edx_unused, data);
		}
	}

	void install_hooks()
	{
		auto create = [](std::uintptr_t addr, void* hook_fn, void** og, const char* name)
		{
			const auto st = MH_CreateHook(reinterpret_cast<LPVOID>(addr), hook_fn, og);
			if (st != MH_OK)
				shared::common::log("Game",
					std::format("MH_CreateHook({}) failed: {}", name, static_cast<int>(st)),
					shared::common::LOG_TYPE::LOG_TYPE_ERROR, true);
		};

		// NOTE: SetCameraMatrices (0x00D5E910) hook is intentionally NOT
		// installed. Initial testing crashed with AV at the first viewMatrix
		// dereference — at least one call site invokes this function with
		// an EDX that is not a valid float*. Static RE only confirmed the
		// __fastcall(renderer@ECX, viewMatrix@EDX) pattern for three call
		// sites; there are more. View matrix will be captured later via
		// ViewInverse (renderer+0x290 packed_handle) through the
		// SetShaderConstant hook path instead. Keep the hook function
		// defined so the iteration diff stays small.
		(void)&set_camera_matrices_hook;
		(void)&set_camera_matrices_og;

		create(ADDR_SET_SHADER_CONSTANT,
		       reinterpret_cast<void*>(&set_shader_constant_hook),
		       reinterpret_cast<void**>(&set_shader_constant_og),
		       "SetShaderConstant");
	}

	void uninstall_hooks()
	{
		MH_DisableHook(reinterpret_cast<LPVOID>(ADDR_SET_SHADER_CONSTANT));
	}
}
