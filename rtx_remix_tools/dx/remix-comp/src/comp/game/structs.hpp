#pragma once

namespace comp::game
{
	// NiDX9Renderer singleton — stores World, View, Projection matrices at fixed offsets.
	// Addresses from NewVegasRTXHelper RE / GameNi.h.
	inline constexpr uintptr_t GAME_RENDERER_SINGLETON_ADDR = 0x11C73B4;
	inline constexpr uint32_t RENDERER_WORLD_OFF  = 0x940;
	inline constexpr uint32_t RENDERER_VIEW_OFF   = 0x980;
	inline constexpr uint32_t RENDERER_PROJ_OFF   = 0x9C0;

	// ShadowSceneNode* — primary shadow/light scene graph node
	inline constexpr uintptr_t SHADOW_SCENE_NODE_ADDR = 0x011F91C8;

	// Camera world position (NiPoint3)
	inline constexpr uintptr_t CAMERA_POSITION_ADDR = 0x011F8E9C;

	// NiShadeProperty shader type for sky
	inline constexpr uint32_t KPROP_SKY = 0x0D;

	// Property chain offsets: NiDX9Renderer +0x0C -> NiPropertyState +0x0C -> NiShadeProperty +0x1C
	inline constexpr uint32_t RENDERER_PROP_STATE_OFF  = 0x0C;
	inline constexpr uint32_t PROP_STATE_SHADE_PROP_OFF = 0x0C;
	inline constexpr uint32_t SHADE_PROP_SHADER_TYPE_OFF = 0x1C;

	// ShadowSceneNode structure offsets
	inline constexpr uint32_t SSN_LIGHTS_START_OFF = 0xB4;

	// NiTList<T>::Entry offsets
	inline constexpr uint32_t ENTRY_NEXT_OFF = 0x00;
	inline constexpr uint32_t ENTRY_DATA_OFF = 0x08;

	// ShadowSceneLight offset
	inline constexpr uint32_t SSL_SOURCE_LIGHT_OFF = 0xF8;

	// NiAVObject world position offset
	inline constexpr uint32_t NI_WORLD_POS_OFF = 0x8C;

	// NiLight offsets
	inline constexpr uint32_t NI_DIMMER_OFF = 0xC4;
	inline constexpr uint32_t NI_AMB_OFF    = 0xC8;
	inline constexpr uint32_t NI_DIFF_OFF   = 0xD4;
	inline constexpr uint32_t NI_SPEC_OFF   = 0xE0;

	// NiPointLight attenuation offsets
	inline constexpr uint32_t NI_ATTEN0_OFF = 0xF0;
	inline constexpr uint32_t NI_ATTEN1_OFF = 0xF4;
	inline constexpr uint32_t NI_ATTEN2_OFF = 0xF8;

	// Game engine hook addresses (skinning)
	inline constexpr uintptr_t SKINNING_PATCH_ADDR     = 0xB992F2;  // conditional -> unconditional JMP
	inline constexpr uintptr_t RENDER_SKINNED_HOOK_ADDR = 0xB99598; // wraps skinned render batch call
	inline constexpr uintptr_t RENDER_SKINNED_TARGET    = 0xB99110; // the actual skinned render function
	inline constexpr uintptr_t RENDER_SKINNED_RETURN    = 0xB9959D; // return after hook
	inline constexpr uintptr_t RESET_BONES_HOOK_ADDR    = 0xB991E7; // wraps per-object bone init
	inline constexpr uintptr_t RESET_BONES_TARGET       = 0x43D450; // per-object bone init function
	inline constexpr uintptr_t RESET_BONES_RETURN       = 0xB991EC; // return after hook

	// Engine matrix accessors
	inline float* get_renderer_ptr()
	{
		auto** ptr = reinterpret_cast<void**>(GAME_RENDERER_SINGLETON_ADDR);
		return ptr ? reinterpret_cast<float*>(*ptr) : nullptr;
	}

	inline float* get_renderer_matrix(uint32_t byte_offset)
	{
		auto* ren = get_renderer_ptr();
		if (!ren) return nullptr;
		return reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(ren) + byte_offset);
	}

	inline bool is_2d()
	{
		auto* proj = get_renderer_matrix(RENDERER_PROJ_OFF);
		if (!proj) return false;
		return (proj[15] == 1.0f && proj[11] == 0.0f);
	}

	inline bool is_sky()
	{
		auto* ren = get_renderer_ptr();
		if (!ren) return false;
		auto* prop_state = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(ren) + RENDERER_PROP_STATE_OFF);
		if (!prop_state) return false;
		auto* shade_prop = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(prop_state) + PROP_STATE_SHADE_PROP_OFF);
		if (!shade_prop) return false;
		auto shader_type = *reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(shade_prop) + SHADE_PROP_SHADER_TYPE_OFF);
		return (shader_type == KPROP_SKY);
	}

	// Memory read helpers for light extraction
	inline float game_float(void* base, uint32_t off)
	{
		return *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(base) + off);
	}

	inline float* game_float_ptr(void* base, uint32_t off)
	{
		return reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(base) + off);
	}

	inline void* game_ptr(void* base, uint32_t off)
	{
		return *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(base) + off);
	}
}
