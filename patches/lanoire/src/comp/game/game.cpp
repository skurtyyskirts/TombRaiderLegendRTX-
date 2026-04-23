#include "std_include.hpp"
#include "game.hpp"

#include "shared/common/flags.hpp"

namespace comp::game
{
	void* get_renderer()
	{
		return *reinterpret_cast<void**>(renderer_singleton_ptr_va);
	}

	const float* get_projection_matrix_ptr()
	{
		if (void* renderer = get_renderer())
		{
			return reinterpret_cast<const float*>(
				static_cast<std::uint8_t*>(renderer) + renderer_off_projection_matrix);
		}
		return nullptr;
	}

	packed_handle get_model_matrix_handle()
	{
		if (auto* r = static_cast<std::uint8_t*>(get_renderer()))
			return { *reinterpret_cast<std::uint32_t*>(r + renderer_off_model_matrix_hdl) };
		return {};
	}

	packed_handle get_view_inverse_handle()
	{
		if (auto* r = static_cast<std::uint8_t*>(get_renderer()))
			return { *reinterpret_cast<std::uint32_t*>(r + renderer_off_view_inverse_hdl) };
		return {};
	}

	void init_game_addresses()
	{
		shared::common::log("Game",
			std::format("Renderer singleton ptr @ 0x{:08X} (value=0x{:08X})",
				renderer_singleton_ptr_va,
				reinterpret_cast<std::uintptr_t>(get_renderer())),
			shared::common::LOG_TYPE::LOG_TYPE_DEFAULT, false);

		install_hooks();
	}
}
