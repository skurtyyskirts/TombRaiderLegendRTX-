#pragma once
#include "structs.hpp"

namespace comp::game
{
	void*       get_renderer();
	const float* get_projection_matrix_ptr();
	packed_handle get_model_matrix_handle();
	packed_handle get_view_inverse_handle();

	void install_hooks();
	void uninstall_hooks();

	void init_game_addresses();
}
