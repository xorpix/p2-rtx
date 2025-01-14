#include "std_include.hpp"

namespace glob
{
	bool spawned_external_console = false;
}

namespace game
{
	std::vector<std::string> loaded_modules;
	std::string root_path;
	DWORD shaderapidx9_module = 0u;
	DWORD studiorender_module = 0u;
	DWORD materialsystem_module = 0u;
	DWORD engine_module = 0u;
	DWORD client_module = 0u;
	DWORD server_module = 0u;
	DWORD vstdlib_module = 0u;

	const D3DXMATRIX IDENTITY =
	{
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};

	const D3DXMATRIX TC_TRANSLATE_TO_CENTER =
	{
		 1.0f,  0.0f, 0.0f, 0.0f,	// identity
		 0.0f,  1.0f, 0.0f, 0.0f,	// identity
		 0.0f,  0.0f, 1.0f, 0.0f,	// identity
		-0.5f, -0.5f, 0.0f, 1.0f,	// translate to center
	};

	const D3DXMATRIX TC_TRANSLATE_FROM_CENTER_TO_TOP_LEFT =
	{
		1.0f, 0.0f, 0.0f, 0.0f,	// identity
		0.0f, 1.0f, 0.0f, 0.0f,	// identity
		0.0f, 0.0f, 1.0f, 0.0f,	// identity
		0.5f, 0.5f, 0.0f, 1.0f,	// translate back to the top left corner
	};

	view_id saved_view_id = VIEW_ILLEGAL;

	// ----------

	// adds a simple console command
	void con_add_command(ConCommand* cmd, const char* name, void(__cdecl* callback)(), const char* desc)
	{
		// ConCommand *this, const char *pName, void (__cdecl *callback)(), const char *pHelpString, int flags, int (__cdecl *completionFunc)(const char *, char (*)[64]
		utils::hook::call<void(__fastcall)(ConCommand* this_ptr, void* null, const char*, void(__cdecl*)(), const char*, int, int(__cdecl*)(const char*, char(*)[64]))>(CLIENT_BASE + USE_OFFSET(0x631F00, 0x6298D0))
			(cmd, nullptr, name, callback, desc, 0x20000, nullptr);
	}

	/**
	 * Calls CDebugOverlay::AddTextOverlay
	 * @param pos		Position of text in 3D Space
	 * @param duration	Duration in which text is visible - use 0.0f for per frame stuff
	 * @param text		The text
	 */
	void debug_add_text_overlay(const float* pos, float duration, const char* text)
	{
		utils::hook::call<void(__cdecl)(const float*, float, const char*)>(ENGINE_BASE + USE_OFFSET(0xC4640, 0xC3FE0))
			(pos, duration, text);
	}

	/**
	 * Calls CDebugOverlay::AddTextOverlay
	 * @param pos			Position of text in 3D Space
	 * @param text			The text
	 * @param line_offset	Offset text position
	 * @param r				red (0-1)
	 * @param g				green (0-1)
	 * @param b				blue (0-1)
	 * @param a				alpha (0-1)
	 */
	void debug_add_text_overlay(const float* pos, const char* text, const int line_offset, const float r, const float g, const float b, const float a)
	{
		utils::hook::call<void(__cdecl)(const float*, int, float, float, float, float, float, const char*)>(ENGINE_BASE + USE_OFFSET(0xC4B30, 0xC4460))
			(pos, line_offset, 0.0f, r, g, b, a, text);
	}

	// remove/destroy a given CBaseEntity
	void cbaseentity_remove(void* cbaseentity_ptr)
	{
		if (cbaseentity_ptr)
		{
			// UTIL_Remove
			utils::hook::call<void(__cdecl)(void* cbaseentity)>(SERVER_BASE + USE_OFFSET(0x283770, 0x27D690))(cbaseentity_ptr);
		}
	}

	int get_visframecount() {
		return *reinterpret_cast<int*>(ENGINE_BASE + USE_OFFSET(0x6AAE6C, 0x6A56B4));
	}

	const char* get_map_name() {
		return utils::hook::call<const char*(__cdecl)()>(CLIENT_BASE + USE_OFFSET(0x1F4040, 0x1EEEE0))();
	}

	void r_flow_through_area(const int area, const Vector* vec_vis_origin, const CPortalRect* clip_rect, const VisOverrideData_t* vis_data, float* reflection_water_height)
	{
		utils::hook::call<void(__cdecl)(int, const Vector*, const CPortalRect*, const VisOverrideData_t*, float*)>(ENGINE_BASE + USE_OFFSET(0x10FB70, 0x10EA00))
			(area, vec_vis_origin, clip_rect, vis_data, reflection_water_height);
	}

	// Frustum_t::SetPlanes
	void frustum_set_planes(Frustum_t* frustum, const VPlane* planes)
	{
		utils::hook::call<void(__fastcall)(Frustum_t*, void* null, const VPlane*)>(ENGINE_BASE + USE_OFFSET(0x270050, 0x26CED0))
			(frustum, nullptr, planes);
	}

	// Frustum_t::CullBox
	bool frustum_cull_box(Frustum_t* frustum, const Vector* mins, const Vector* maxs)
	{
		return utils::hook::call<bool(__fastcall)(void* this_ptr, void* null, const Vector*, const Vector*)>(ENGINE_BASE + USE_OFFSET(0x270100, 0x26CF80))
			(frustum, nullptr, mins, maxs);
	}

	void cvar_uncheat(const char* name)
	{
		if (const auto ivar = game::get_icvar(); ivar)
		{
			if (auto var = ivar->vftable->FindVar(ivar, name); var) {
				var->m_nFlags &= ~0x4000;
			}
		}
	}

	void cvar_uncheat_and_set_int(const char* name, const int val)
	{
		if (const auto ivar = game::get_icvar(); ivar)
		{
			if (auto var = ivar->vftable->FindVar(ivar, name); var)
			{
				var->vtbl->SetValue_Int(var, val);
				var->m_nFlags &= ~0x4000;
			}
		}
	}

	void cvar_uncheat_and_set_float(const char* name, const float val)
	{
		if (const auto ivar = game::get_icvar(); ivar)
		{
			if (auto var = ivar->vftable->FindVar(ivar, name); var)
			{
				var->vtbl->SetValue_Float(var, val);
				var->m_nFlags &= ~0x4000;
			}
		}
	}

	typedef void(__cdecl* msg_fn)(const char* msg, va_list);
	void print_ingame(const char* msg, ...)
	{
		if (msg == nullptr) {
			return;
		}

		static msg_fn fn = (msg_fn)GetProcAddress(GetModuleHandleA("tier0.dll"), "Msg");
		char buffer[989];

		va_list list;
		va_start(list, msg);
		vsprintf(buffer, msg, list);
		perror(buffer);
		va_end(list);
		fn(buffer, list);
	}
}
