#include "std_include.hpp"
#include "shared/common/flags.hpp"

namespace comp::game
{
	volatile int g_render_skinned = 0;
	volatile int g_bone_reset_pending = 0;
	int g_hooks_installed = 0;

	void init_game_addresses()
	{
		// FNV uses hardcoded addresses — no pattern scanning needed
		shared::common::log("Game", "FalloutNV: Using hardcoded addresses", shared::common::LOG_TYPE::LOG_TYPE_DEFAULT, false);
	}
}
