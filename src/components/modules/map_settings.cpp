#include "std_include.hpp"

namespace components
{
	void map_settings::set_settings_for_map(const std::string& map_name)
	{
		m_map_settings.mapname = !map_name.empty() ? map_name : game::get_map_name();
		utils::replace_all(m_map_settings.mapname, std::string("maps/"), "");		// if sp map
		utils::replace_all(m_map_settings.mapname, std::string(".bsp"), "");

		parse_toml();

		static bool disable_map_configs = flags::has_flag("xo_disable_map_conf");
		if (api::m_initialized && !disable_map_configs)
		{
			// resets all modified variables back to rtx.conf level
			api::remix_vars::reset_all_modified();

			// auto apply {map_name}.conf (if it exists)
			open_and_set_var_config(m_map_settings.mapname + ".conf", true);

			// apply other manually defined configs
			for (const auto& f : m_map_settings.api_var_configs) {
				open_and_set_var_config(f);
			}
		}
	}

	// cannot be called in the current on_map_load stub (too early)
	// called from 'once_per_frame_cb()' instead
	void map_settings::spawn_markers_once()
	{
		// only spawn markers once
		if (m_spawned_markers) {
			return;
		}

		m_spawned_markers = true;

		// spawn map markers
		for (auto& m : m_map_settings.map_markers)
		{
			const auto mdl_num = m.index / 10u;
			const auto skin_num = m.index % 10u;
			const auto model_name = utils::va("models/props_xo/mapmarker%03d.mdl", mdl_num * 10);

			void* mdlcache = reinterpret_cast<void*>(*(DWORD*)(SERVER_BASE + USE_OFFSET(0x86B07C, 0x8618FC)));

			// mdlcache->BeginLock
			utils::hook::call_virtual<30, void>(mdlcache);

			// mdlcache->FindMDL
			const auto mdl_handle = utils::hook::call_virtual<9, std::uint16_t>(mdlcache, model_name);
			if (mdl_handle != 0xFFFF)
			{
				// save precache state - CBaseEntity::m_bAllowPrecache
				const bool old_precache_state = *reinterpret_cast<bool*>(SERVER_BASE + USE_OFFSET(0x7BC2B0, 0x7B2C58));

				// allow precaching - CBaseEntity::m_bAllowPrecache
				*reinterpret_cast<bool*>(SERVER_BASE + USE_OFFSET(0x7BC2B0, 0x7B2C58)) = true;

				// CreateEntityByName - CBaseEntity *__cdecl CreateEntityByName(const char *className, int iForceEdictIndex, bool bNotify)
				m.handle = utils::hook::call<void* (__cdecl)(const char* className, int iForceEdictIndex, bool bNotify)>(SERVER_BASE + USE_OFFSET(0x19F2C0, 0x19A090))
					("dynamic_prop", -1, true);

				if (m.handle)
				{
					// ent->KeyValue
					utils::hook::call_virtual<35, void>(m.handle, "origin", utils::va("%.10f %.10f %.10f", m.origin[0], m.origin[1], m.origin[2]));
					utils::hook::call_virtual<35, void>(m.handle, "model", model_name);
					utils::hook::call_virtual<35, void>(m.handle, "solid", "2");

					struct skin_offset
					{
						char pad[0x37C];
						int m_nSkin;
					}; STATIC_ASSERT_OFFSET(skin_offset, m_nSkin, 0x37C);

					auto skin_val = static_cast<skin_offset*>(m.handle);
					skin_val->m_nSkin = skin_num;

					// ent->Precache
					utils::hook::call_virtual<25, void>(m.handle);

					// DispatchSpawn
					utils::hook::call<void(__cdecl)(void* pEntity, bool bRunVScripts)>(SERVER_BASE + USE_OFFSET(0x27F520, 0x279480))
						(m.handle, true);

					// ent->Activate
					utils::hook::call_virtual<37, void>(m.handle);
				}

				// restore precaching state - CBaseEntity::m_bAllowPrecache
				*reinterpret_cast<bool*>(SERVER_BASE + USE_OFFSET(0x7BC2B0, 0x7B2C58)) = old_precache_state;
			}

			utils::hook::call_virtual<31, void>(mdlcache); // mdlcache->EndLock
		}
	}

	void map_settings::destroy_markers()
	{
		// destroy active markers
		for (auto& m : m_map_settings.map_markers)
		{
			if (m.handle)
			{
				game::cbaseentity_remove(m.handle);
				m.handle = nullptr;
			}
		}

		m_map_settings.map_markers.clear();
		m_spawned_markers = false;
	}

	bool map_settings::parse_toml()
	{
		try 
		{
			auto config = toml::parse("portal2-rtx\\map_settings.toml");

			// #
			auto to_float = [](const toml::value& entry, const float default_val = 0.0f)
				{
					if (entry.is_floating()) {
						return static_cast<float>(entry.as_floating());
					}

					if (entry.is_integer()) {
						return static_cast<float>(entry.as_integer());
					}

					try { // this will fail and let the user know whats wrong
						return static_cast<float>(entry.as_floating());
					}
					catch (toml::type_error& err) {
						game::console(); printf("%s\n", err.what());
					}

					return default_val;
				};

			// #
			auto to_int = [](const toml::value& entry, const int default_val = 0)
				{
					if (entry.is_floating()) {
						return static_cast<int>(entry.as_floating());
					}

					if (entry.is_integer()) {
						return static_cast<int>(entry.as_integer());
					}

					try { // this will fail and let the user know whats wrong
						return static_cast<int>(entry.as_integer());
					}
					catch (toml::type_error& err) {
						game::console(); printf("%s\n", err.what());
					}

					return default_val;
				};


			// ####################
			// parse 'FOG' table
			if (config.contains("FOG"))
			{
				auto& fog_table = config["FOG"];

				// try to find the loaded map
				if (fog_table.contains(m_map_settings.mapname))
				{
					if (const auto map = fog_table[m_map_settings.mapname];
						!map.is_empty())
					{
						if (map.contains("distance") && map.contains("color"))
						{
							const auto dist = map.at("distance");
							m_map_settings.fog_dist = to_float(dist);

							if (const auto& color = map.at("color").as_array(); 
								color.size() == 3)
							{
								const auto r = static_cast<std::uint8_t>(to_int(color[0]));
								const auto g = static_cast<std::uint8_t>(to_int(color[1]));
								const auto b = static_cast<std::uint8_t>(to_int(color[2]));
								m_map_settings.fog_color = D3DCOLOR_XRGB(r, g, b);
							}
						}
					}
				}
			} // end 'CULL'


			// ####################
			// parse 'WATER' table
			if (config.contains("WATER"))
			{
				auto& water_table = config["WATER"];

				// try to find the loaded map
				if (water_table.contains(m_map_settings.mapname))
				{
					if (const auto map = water_table[m_map_settings.mapname];
						!map.is_empty())
					{
						m_map_settings.water_uv_scale = to_float(map, 1.0f);
					}
				}
			} // end 'WATER'


			// ####################
			// parse 'CULL' table
			if (config.contains("CULL"))
			{
				auto& cull_table = config["CULL"];

				// #
				auto process_cull_entry = [to_int](const toml::value& entry)
					{
						const auto contains_leafs = entry.contains("leafs");
						const auto contains_areas = entry.contains("areas");
						const auto contains_hidden_leafs = entry.contains("hide_leafs");
						const auto contains_hidden_areas = entry.contains("hide_areas");
						const auto contains_cull = entry.contains("cull");

						if (entry.contains("area") && (contains_leafs || contains_areas || contains_hidden_leafs || contains_hidden_areas || contains_cull))
						{
							const auto area = to_int(entry.at("area"));

							// forced leafs
							std::unordered_set<std::uint32_t> leaf_set;
							if (contains_leafs)
							{
								auto& leafs = entry.at("leafs").as_array();

								for (const auto& leaf : leafs) {
									leaf_set.insert(to_int(leaf));
								}
							}

							// forced areas
							std::unordered_set<std::uint32_t> area_set;
							if (contains_areas)
							{
								auto& areas = entry.at("areas").as_array();

								for (const auto& a : areas) {
									area_set.insert(to_int(a));
								}
							}

							// culling mode
							AREA_CULL_MODE cmode = g_cull_disable_frustum_culling ? map_settings::AREA_CULL_MODE_NO_FRUSTUM : map_settings::AREA_CULL_MODE_DEFAULT;
							if (contains_cull)
							{
								auto m = to_int(entry.at("cull"));
								if (m >= AREA_CULL_COUNT) 
								{
									game::console(); printf("MapSettings: param 'cull' was out-of-range (%d)\n", m);
									m = 0u;
								}
								cmode = (AREA_CULL_MODE)(std::uint8_t)m;
							}

							// hidden leafs
							std::unordered_set<std::uint32_t> hidden_leaf_set;
							if (contains_hidden_leafs)
							{
								auto& leafs = entry.at("hide_leafs").as_array();

								for (const auto& leaf : leafs) {
									hidden_leaf_set.insert(to_int(leaf));
								}
							}

							// hidden areas
							std::vector<hide_area_s> temp_hidden_areas_set;
							if (contains_hidden_areas)
							{
								auto& hide_areas = entry.at("hide_areas").as_array();
								for (const auto& elem : hide_areas)
								{
									if (elem.contains("areas"))
									{
										std::unordered_set<std::uint32_t> temp_area_set;
										const auto& areas = elem.at("areas").as_array();

										for (const auto& a : areas) {
											temp_area_set.insert(to_int(a));
										}

										std::unordered_set<std::uint32_t> temp_not_in_leaf_set;
										if (elem.contains("N_leafs"))
										{
											const auto& nleafs = elem.at("N_leafs").as_array();
											for (const auto& nl : nleafs) {
												temp_not_in_leaf_set.insert(to_int(nl));
											}
										}

										temp_hidden_areas_set.emplace_back(std::move(temp_area_set), std::move(temp_not_in_leaf_set));
									}
								}
							}

							m_map_settings.area_settings.emplace(area, 
								area_overrides_s(
									std::move(leaf_set), 
									std::move(area_set), 
									std::move(hidden_leaf_set), 
									std::move(temp_hidden_areas_set),
									cmode,
									area));
						}
					};

				// try to find the loaded map
				if (cull_table.contains(m_map_settings.mapname))
				{
					if (const auto map = cull_table[m_map_settings.mapname]; 
						!map.is_empty() && !map.as_array().empty())
					{
						for (const auto& entry : map.as_array()) {
							process_cull_entry(entry);
						}
					}
				}
			} // end 'CULL'


			// ####################
			// parse 'HIDEMODEL' table
			if (config.contains("HIDEMODEL"))
			{
				// try to find the loaded map
				if (auto& hidemdl_table = config["HIDEMODEL"]; 
					hidemdl_table.contains(m_map_settings.mapname))
				{
					if (const auto map = hidemdl_table[m_map_settings.mapname];
						!map.is_empty())
					{
						if (map.contains("name"))
						{
							if (auto& names = map.at("name");
								!names.is_empty())
							{
								if (const auto& narray = map.at("name").as_array();
									!narray.empty())
								{
									for (auto& str : narray) {
										m_map_settings.hide_models.substrings.insert(str.as_string());
									}
								}
							}
						}

						if (map.contains("radius"))
						{
							if (auto& radii = map.at("radius");
								!radii.is_empty())
							{
								if (const auto& rarray = map.at("radius").as_array();
									!rarray.empty())
								{
									for (auto& r : rarray) {
										m_map_settings.hide_models.radii.insert(to_float(r, -1.0f));
									}
								}
							}
						}
						
					}
				}
			} // end 'HIDEMODEL'


			// ####################
			// parse 'MARKER' table
			if (config.contains("MARKER"))
			{
				auto& marker_table = config["MARKER"];

				// #
				auto process_marker_entry = [to_int, to_float](const toml::value& entry)
					{
						if (entry.contains("marker") && entry.contains("position"))
						{
							if (const auto& pos = entry.at("position").as_array();
								pos.size() == 3)
							{
								m_map_settings.map_markers.emplace_back(
									marker_settings_s
									{
										.index = static_cast<std::uint32_t>(to_int(entry.at("marker"))),
										.origin = {to_float(pos[0]), to_float(pos[1]), to_float(pos[2])}
									});
							}
						}
					};

				// try to find the loaded map
				if (marker_table.contains(m_map_settings.mapname))
				{
					if (const auto map = marker_table[m_map_settings.mapname];
						!map.is_empty() && !map.as_array().empty())
					{
						for (const auto& entry : map.as_array()) {
							process_marker_entry(entry);
						}
					}
				}
			} // end 'MARKER'


			// ####################
			// parse 'CONFIGVARS' table
			{
				auto& configvar_table = config["CONFIGVARS"];

				// #TODO
				auto process_transition_entry = [to_int, to_float](const toml::value& entry)
					{
						// we NEED conf, leafs and duration or speed
						if (entry.contains("conf") && (entry.contains("leafs") || entry.contains("choreo")) && (entry.contains("duration") || entry.contains("speed")))
						{
							std::string config_name;

							try { config_name = entry.at("conf").as_string(); }
							catch (toml::type_error& err) 
							{
								game::console(); printf("%s\n", err.what());
								return;
							}

							if (!config_name.empty()) 
							{
								std::uint8_t mode = 0u;
								api::remix_vars::EASE_TYPE ease = api::remix_vars::EASE_TYPE_LINEAR;
								float delay_in = 0.0f, delay_out = 0.0f, duration = 0.0f;

								if (entry.contains("mode")) {
									mode = (std::uint8_t)to_int(entry.at("mode"));
								}

								if (entry.contains("ease")) {
									ease = (api::remix_vars::EASE_TYPE)to_int(entry.at("ease"));
								}

								if (entry.contains("delay_in")) {
									delay_in = to_float(entry.at("delay_in"));
								}

								if (entry.contains("delay_out")) {
									delay_out = to_float(entry.at("delay_out"));
								}

								if (entry.contains("duration")) {
									duration = to_float(entry.at("duration"));
								}

								const bool choreo_mode = entry.contains("choreo");
								if (choreo_mode)
								{
									std::string choreo_name;
									try { choreo_name = entry.at("choreo").as_string(); }
									catch (toml::type_error& err)
									{
										game::console(); printf("%s\n", err.what());
										return;
									}

									if (!choreo_name.empty())
									{
										const auto hash = utils::string_hash64(utils::va("%s%s%.2f", choreo_name.c_str(), config_name.c_str(), duration));
										m_map_settings.choreo_transitions.emplace_back(
											std::move(choreo_name),
											config_name,
											(CHOREO_TRANS_MODE)mode,
											ease,
											delay_in,
											delay_out,
											duration,
											hash);
									}
								}
								else
								{
									const bool leaf_mode = !choreo_mode && entry.contains("leafs");
									if (leaf_mode)
									{
										std::unordered_set<std::uint32_t> leaf_set;
										const auto& leafs = entry.at("leafs").as_array();
										if (!leafs.empty())
										{
											for (const auto& leaf : leafs) {
												leaf_set.insert(to_int(leaf));
											}

											// create a unique hash for this transition
											int leaf_sum = 0;
											for (const auto& leaf : leaf_set) {
												leaf_sum += leaf;
											}

											const auto hash = utils::string_hash64(utils::va("%d%s%.2f", leaf_sum, config_name.c_str(), duration));
											m_map_settings.leaf_transitions.emplace_back(
												std::move(leaf_set),
												config_name,
												(LEAF_TRANS_MODE)mode,
												ease,
												delay_in,
												delay_out,
												duration,
												hash);
										}
									}
								}
							}
						}
					};

				// try to find the loaded map
				if (configvar_table.contains(m_map_settings.mapname))
				{
					if (const auto map = configvar_table[m_map_settings.mapname];
						!map.is_empty())
					{
						if (map.contains("startup"))
						{
							if (auto& startup = map.at("startup").as_array(); 
								!startup.empty())
							{
								for (const auto& conf : startup)
								{
									try {
										m_map_settings.api_var_configs.emplace_back(conf.as_string());
									}
									catch (toml::type_error& err) {
										game::console(); printf("%s\n", err.what());
									}
								}
							}
						}

						if (map.contains("transitions"))
						{
							if (auto& transitions = map.at("transitions").as_array();
								!transitions.empty())
							{
								for (const auto& entry : transitions) {
									process_transition_entry(entry);
								}
							}
						}
					}
				}
			} // end 'CONFIGVARS'


			// ####################
			// parse 'PORTALS' table
			if (config.contains("PORTALS"))
			{
				auto& portal_table = config["PORTALS"];

				// #
				auto process_portal_pair_entry = [to_int, to_float](const toml::value& entry)
					{
						if (entry.contains("pair") && entry.contains("portals"))
						{
							if (auto& portals = entry.at("portals");
								!portals.is_empty())
							{
								if (const auto& parray = entry.at("portals").as_array();
									!parray.empty() && parray.size() == 2)
								{
									if (parray[0].contains("position") && parray[0].contains("rotation") && parray[0].contains("scale") && parray[0].contains("square_mask")
										&& parray[1].contains("position") && parray[1].contains("rotation") && parray[1].contains("scale") && parray[1].contains("square_mask"))
									{
										const auto& p0_pos = parray[0].at("position").as_array();
										const auto& p0_rot = parray[0].at("rotation").as_array();
										const auto& p0_scale = parray[0].at("scale").as_array();
										const auto& p0_mask = to_int(parray[0].at("square_mask"));

										const auto& p1_pos = parray[1].at("position").as_array();
										const auto& p1_rot = parray[1].at("rotation").as_array();
										const auto& p1_scale = parray[1].at("scale").as_array();
										const auto& p1_mask = to_int(parray[1].at("square_mask"));

										if (p0_pos.size() == 3 && p0_rot.size() == 3 && p0_scale.size() == 2
											&& p1_pos.size() == 3 && p1_rot.size() == 3 && p1_scale.size() == 2)
										{
											api::remix_rayportal::get()->add_pair(
												(api::remix_rayportal::PORTAL_PAIR)static_cast<std::uint32_t>(to_int(entry.at("pair"))),
												{ to_float(p0_pos[0]),   to_float(p0_pos[1]), to_float(p0_pos[2]) },
												{ to_float(p0_rot[0]),   to_float(p0_rot[1]), to_float(p0_rot[2]) },
												{ to_float(p0_scale[0]), to_float(p0_scale[1]) },
												p0_mask,
												{ to_float(p1_pos[0]),   to_float(p1_pos[1]), to_float(p1_pos[2]) },
												{ to_float(p1_rot[0]),   to_float(p1_rot[1]), to_float(p1_rot[2]) },
												{ to_float(p1_scale[0]), to_float(p1_scale[1]) },
												p1_mask);
										}
									}
								}
							}
						}
					};

				// try to find the loaded map
				if (portal_table.contains(m_map_settings.mapname))
				{
					if (const auto& map = portal_table[m_map_settings.mapname];
						!map.is_empty() && !map.as_array().empty())
					{
						for (const auto& entry : map.as_array()) {
							process_portal_pair_entry(entry);
						}
					}
				}
			} // end 'PORTALS'
		}

		catch (const toml::syntax_error& err)
		{
			game::console();
			printf("%s\n", err.what());
			return false;
		}

		return true;
	}

	bool map_settings::matches_map_name()
	{
		return utils::str_to_lower(m_args[0]) == m_map_settings.mapname;
	}

	void map_settings::open_and_set_var_config(const std::string& config, const bool no_error, const bool ignore_hashes, const char* custom_path)
	{
		std::string path = "portal2-rtx\\map_configs";
		if (custom_path)
		{
			path = custom_path;
		}

		std::ifstream file;
		if (utils::open_file_homepath(path, config, file))
		{
			std::string input;
			while (std::getline(file, input))
			{
				if (utils::starts_with(input, "#")) {
					continue;
				}

				if (auto pair = utils::split(input, '=');
					pair.size() == 2u)
				{
					utils::trim(pair[0]);
					utils::trim(pair[1]);

					if (ignore_hashes && pair[1].starts_with("0x")) {
						continue;
					}

					if (pair[1].empty()) {
						continue;
					}

					if (const auto o = api::remix_vars::get_option(pair[0].c_str()); o)
					{
						const auto& v = api::remix_vars::string_to_option_value(o->second.type, pair[1]);
						api::remix_vars::set_option(o, v, true);
					}
				}
			}

			file.close();
		}
		else if (!no_error)
		{
			game::console();
			printf("[MapSettings] Failed to find config: \"%s\" in %s \n", config.c_str(), custom_path ? custom_path : "\"portal2-rtx\\map_configs\"");
		}
	}

	void map_settings::on_map_load(const std::string& map_name)
	{
		get()->clear_map_settings();
		get()->set_settings_for_map(map_name);

		is_level.reset();
		is_level.update(get_map_name());
	}

	void map_settings::clear_map_settings()
	{
		api::remix_rayportal::get()->destroy_all_pairs();

		m_map_settings.area_settings.clear();
		m_map_settings.leaf_transitions.clear();
		m_map_settings.choreo_transitions.clear();

		destroy_markers();
		m_map_settings.map_markers.clear();

		m_map_settings.api_var_configs.clear();
		m_map_settings = {};

		main_module::trigger_vis_logic();
	}

	ConCommand xo_mapsettings_update {};
	void xo_mapsettings_update_fn()
	{
		map_settings::get()->clear_map_settings();
		map_settings::get()->set_settings_for_map("");
	}

	map_settings::map_settings()
	{
		game::con_add_command(&xo_mapsettings_update, "xo_mapsettings_update", xo_mapsettings_update_fn, "Reloads the map_settings.toml file + map.conf");
	}
}