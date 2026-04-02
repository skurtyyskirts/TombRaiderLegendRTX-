#pragma once
#include "structs.hpp"

namespace comp::game
{
	// Skinning hook state — written by code cave stubs installed at device creation
	extern volatile int g_render_skinned;       // 1 while inside skinned render batch
	extern volatile int g_bone_reset_pending;   // 1 when per-object bone reset needed
	extern int g_hooks_installed;               // 1 if game hooks were installed

	extern void init_game_addresses();
}
