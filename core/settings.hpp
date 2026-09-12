#pragma once

#include <utilities/cstypes.hpp>
#include <utilities/math/math.hpp>
#include <external/config.hpp>
#include <utilities/skin_options.hpp>
#include <limits>

namespace settings {

	struct combat
	{
		struct ragebot
		{
			static constexpr auto k_group_count{ 6u };

			xui::setting enabled{ true,{}, "enabled", "ragebot" };

			struct weapon_group
			{
				xui::setting silent{ true,{}, "silent", "ragebot" };
				xui::setting no_spread{ false,{}, "no spread", "ragebot" };
				xui::setting body_aim{ false,{}, "force b-aim", "ragebot" };
				xui::setting force_shot_air{ false,{}, "force shot in air", "ragebot" };
				xui::setting force_shot{ false,{}, "force shot on ground", "ragebot" };
				xui::setting autostop{ true,{}, "autostop", "ragebot" };

				config::val<float> max_fov{ 180.0f };

				config::val<int> hitchance{ 80 };
				config::val<int> min_damage{ 101 };

				config::val<int> min_damage_override_value{ 11 };
				xui::setting min_damage_override{ false,{}, "min damage override", "ragebot" };

				config::val<int> hitchance_override_value{ 75 };
				xui::setting hitchance_override{ false,{}, "hit chance override", "ragebot" };

				config::val<float> pointscale{ 85.0f };
				xui::setting dynamic_pointscale{ true,{}, "dynamic point scale", "ragebot" };
				xui::setting debug_multipoints{ false,{}, "debug multipoints", "ragebot" };

				config::bools<6> hitboxes{ { true, true, true, true, true, true } };

				void init(std::string_view cat)
				{
					const auto s = std::string(cat);

					this->silent.category = s;
					this->no_spread.category = s;
					this->body_aim.category = s;
					this->force_shot_air.category = s;
					this->force_shot.category = s;
					this->autostop.category = s;
					this->min_damage_override.category = s;
					this->hitchance_override.category = s;
					this->dynamic_pointscale.category = s;
					this->debug_multipoints.category = s;

					this->max_fov.reg(s, "max fov");
					this->hitchance.reg(s, "hit chance");
					this->min_damage.reg(s, "min damage");
					this->min_damage_override_value.reg(s, "min damage override value");
					this->hitchance_override_value.reg(s, "hit chance override value");
					this->pointscale.reg(s, "point scale");
					this->hitboxes.reg(s, "hitboxes");
				}

				void copy_values_from(const weapon_group& other)
				{
					this->silent.value = other.silent.value;
					this->silent.bind = other.silent.bind;
					this->no_spread.value = other.no_spread.value;
					this->no_spread.bind = other.no_spread.bind;
					this->body_aim.value = other.body_aim.value;
					this->body_aim.bind = other.body_aim.bind;
					this->force_shot_air.value = other.force_shot_air.value;
					this->force_shot_air.bind = other.force_shot_air.bind;
					this->force_shot.value = other.force_shot.value;
					this->force_shot.bind = other.force_shot.bind;
					this->autostop.value = other.autostop.value;
					this->autostop.bind = other.autostop.bind;
					this->max_fov.value = other.max_fov.value;
					this->hitchance.value = other.hitchance.value;
					this->min_damage.value = other.min_damage.value;
					this->min_damage_override.value = other.min_damage_override.value;
					this->min_damage_override.bind = other.min_damage_override.bind;
					this->min_damage_override_value.value = other.min_damage_override_value.value;
					this->hitchance_override.value = other.hitchance_override.value;
					this->hitchance_override.bind = other.hitchance_override.bind;
					this->hitchance_override_value.value = other.hitchance_override_value.value;
					this->pointscale.value = other.pointscale.value;
					this->dynamic_pointscale.value = other.dynamic_pointscale.value;
					this->dynamic_pointscale.bind = other.dynamic_pointscale.bind;
					this->debug_multipoints.value = other.debug_multipoints.value;
					this->debug_multipoints.bind = other.debug_multipoints.bind;
					this->hitboxes = other.hitboxes;
				}

				void set_default_binds()
				{
					this->force_shot_air.bind = { .key = VK_XBUTTON1, .mode = xui::bind_mode::hold_on };
					this->min_damage_override.bind = { .key = VK_XBUTTON2, .mode = xui::bind_mode::hold_on };
					this->hitchance_override.bind = { .key = VK_SPACE, .mode = xui::bind_mode::hold_on };
				}
			};

			struct individual_weapon
			{
				weapon_group cfg{};
				xui::setting override_group{ false, {}, "override", "" };

				void init(std::string_view cat)
				{
					const auto s = std::string(cat);
					this->override_group.category = s;
					this->cfg.init(s);
				}
			};

			static constexpr auto k_weapon_count{ cstypes::weapons::k_total_weapons };
			std::array<weapon_group, k_group_count> groups{};
			std::array<individual_weapon, k_weapon_count> weapons{};

			ragebot()
			{
				for (std::uint32_t i = 0; i < k_group_count; ++i)
				{
					this->groups[i].init(std::string("ragebot - ") + cstypes::weapons::k_groups[i].config_name);
				}

				this->groups[0].set_default_binds();
				this->groups[4].set_default_binds();

				for (std::size_t i = 0; i < k_weapon_count; ++i)
				{
					this->weapons[i].init(std::string("ragebot - ") + cstypes::weapons::k_weapons[i].config_name);
					const auto grp_idx = cstypes::weapons::k_weapons[i].group_idx;
					if (grp_idx < k_group_count)
					{
						this->weapons[i].cfg.copy_values_from(this->groups[grp_idx]);
					}
				}
			}

			[[nodiscard]] bool is_weapon_overridden(std::uint16_t item_def_idx) const
			{
				const auto idx = cstypes::weapons::get_weapon_index(item_def_idx);
				return idx >= 0 && this->weapons[idx].override_group.value;
			}

			[[nodiscard]] bool is_group_overridden(std::size_t group_idx) const
			{
				if (group_idx >= k_group_count) return false;
				const auto& g = cstypes::weapons::k_groups[group_idx];
				for (std::size_t i = 0; i < g.count; ++i)
				{
					if (this->weapons[g.start_idx + i].override_group.value)
						return true;
				}
				return false;
			}

			weapon_group& get_group(std::uint32_t weapon_type, std::uint16_t item_def_idx = 0)
			{
				const auto w_idx = cstypes::weapons::get_weapon_index(item_def_idx);
				if (w_idx >= 0 && this->weapons[w_idx].override_group.value)
				{
					return this->weapons[w_idx].cfg;
				}

				const auto idx = weapon_type - cstypes::weapon_type::pistol;
				const auto safe_idx = idx < k_group_count ? idx : 2;

				return this->groups[safe_idx];
			}

			const weapon_group& get_group(std::uint32_t weapon_type, std::uint16_t item_def_idx = 0) const
			{
				const auto w_idx = cstypes::weapons::get_weapon_index(item_def_idx);
				if (w_idx >= 0 && this->weapons[w_idx].override_group.value)
				{
					return this->weapons[w_idx].cfg;
				}

				const auto idx = weapon_type - cstypes::weapon_type::pistol;
				const auto safe_idx = idx < k_group_count ? idx : 2;

				return this->groups[safe_idx];
			}
		} m_ragebot{};

		struct legitbot
		{
			static constexpr auto k_group_count{ 6u };

			struct weapon_group
			{
				xui::setting aimbot{ false,{ VK_XBUTTON2, xui::bind_mode::hold_on }, "aimbot", "legitbot" };
				config::val<float> fov{ 5.0f };
				config::val<int> smooth{ 5 };
				config::bools<5> hitboxes{ { true, false, false, false, false } };

				xui::setting rcs{ true,{}, "recoil control", "legitbot" };
				config::val<int> rcs_min{ 95 };
				config::val<int> rcs_max{ 105 };

				xui::setting standalone_rcs{ false,{}, "standalone rcs", "legitbot" };
				config::val<int> standalone_rcs_strength{ 100 };
				config::val<int> standalone_rcs_min{ 95 };
				config::val<int> standalone_rcs_max{ 105 };

				xui::setting triggerbot{ false,{ VK_XBUTTON1, xui::bind_mode::hold_on }, "triggerbot", "legitbot" };
				config::val<int> trigger_delay{ 5 };
				config::val<int> trigger_hitchance{ 80 };
				xui::setting trigger_head_only{ false,{}, "trigger head only", "legitbot" };
				xui::setting give_me_your_seed{ false,{}, "trigger seed mode", "legitbot" };

				xui::setting autowall{ true,{}, "autowall", "legitbot" };
				config::val<int> min_damage{ 101 };

				xui::setting smoke_check{ false,{}, "smoke check", "legitbot" };
				xui::setting scope_check{ false,{}, "scope check", "legitbot" };
				xui::setting flash_check{ false,{}, "flash check", "legitbot" };
				xui::setting ground_check{ false,{}, "ground check", "legitbot" };
				// Approximate smoke geometry, not the engine's voxel visibility.
				config::val<float> smoke_radius{ 160.0f };
				config::val<float> smoke_lifetime{ 20.0f };

				xui::setting visualize_fov{ true,{}, "visualize fov", "legitbot" };
				config::col fov_color{ { 255, 255, 255, 150 } };

				void init(std::string_view cat)
				{
					const auto s = std::string(cat);

					this->aimbot.category = s;
					this->rcs.category = s;
					this->standalone_rcs.category = s;
					this->triggerbot.category = s;
					this->trigger_head_only.category = s;
					this->give_me_your_seed.category = s;
					this->autowall.category = s;
					this->visualize_fov.category = s;
					this->smoke_check.category = s;
					this->scope_check.category = s;
					this->flash_check.category = s;
					this->ground_check.category = s;

					this->fov.reg(s, "fov");
					this->smooth.reg(s, "smooth");
					this->hitboxes.reg(s, "hitboxes");
					this->rcs_min.reg(s, "rcs min");
					this->rcs_max.reg(s, "rcs max");
					this->standalone_rcs_strength.reg(s, "standalone rcs strength");
					this->standalone_rcs_min.reg(s, "standalone rcs min");
					this->standalone_rcs_max.reg(s, "standalone rcs max");
					this->trigger_delay.reg(s, "trigger delay");
					this->trigger_hitchance.reg(s, "trigger hitchance");
					this->min_damage.reg(s, "min damage");
					this->fov_color.reg(s, "fov color");
					this->smoke_radius.reg(s, "smoke radius");
					this->smoke_lifetime.reg(s, "smoke lifetime");
				}
				void copy_values_from(const weapon_group& other)
				{
					this->aimbot.value = other.aimbot.value;
					this->fov.value = other.fov.value;
					this->smooth.value = other.smooth.value;
					this->hitboxes = other.hitboxes;
					this->aimbot.bind = other.aimbot.bind;
					this->rcs.value = other.rcs.value;
					this->rcs.bind = other.rcs.bind;
					this->standalone_rcs.value = other.standalone_rcs.value;
					this->standalone_rcs.bind = other.standalone_rcs.bind;
					this->rcs_min.value = other.rcs_min.value;
					this->rcs_max.value = other.rcs_max.value;
					this->smooth.value = other.smooth.value;
					this->fov.value = other.fov.value;
					this->hitboxes = other.hitboxes;
					this->triggerbot.value = other.triggerbot.value;
					this->triggerbot.bind = other.triggerbot.bind;
					this->trigger_delay.value = other.trigger_delay.value;
					this->trigger_hitchance.value = other.trigger_hitchance.value;
					this->trigger_head_only.value = other.trigger_head_only.value;
					this->trigger_head_only.bind = other.trigger_head_only.bind;
					this->give_me_your_seed.value = other.give_me_your_seed.value;
					this->give_me_your_seed.bind = other.give_me_your_seed.bind;
					this->autowall.value = other.autowall.value;
					this->autowall.bind = other.autowall.bind;
					this->min_damage.value = other.min_damage.value;
					this->smoke_check.value = other.smoke_check.value;
					this->smoke_check.bind = other.smoke_check.bind;
					this->scope_check.value = other.scope_check.value;
					this->scope_check.bind = other.scope_check.bind;
					this->flash_check.value = other.flash_check.value;
					this->flash_check.bind = other.flash_check.bind;
					this->ground_check.value = other.ground_check.value;
					this->ground_check.bind = other.ground_check.bind;
					this->smoke_radius.value = other.smoke_radius.value;
					this->smoke_lifetime.value = other.smoke_lifetime.value;
					this->visualize_fov.value = other.visualize_fov.value;
					this->visualize_fov.bind = other.visualize_fov.bind;
					this->fov_color = other.fov_color;
				}
			};

			struct individual_weapon
			{
				weapon_group cfg{};
				xui::setting override_group{ false, {}, "override", "" };

				void init(std::string_view cat)
				{
					const auto s = std::string(cat);
					this->override_group.category = s;
					this->cfg.init(s);
				}
			};

			static constexpr auto k_weapon_count{ cstypes::weapons::k_total_weapons };
			xui::setting enabled{ false,{}, "enabled", "legitbot" };
			std::array<weapon_group, k_group_count> groups{};
			std::array<individual_weapon, k_weapon_count> weapons{};

			legitbot()
			{
				for (auto i = 0u; i < k_group_count; ++i)
				{
					this->groups[i].init(std::string("legitbot - ") + cstypes::weapons::k_groups[i].config_name);
				}

				for (std::size_t i = 0; i < k_weapon_count; ++i)
				{
					this->weapons[i].init(std::string("legitbot - ") + cstypes::weapons::k_weapons[i].config_name);
					const auto grp_idx = cstypes::weapons::k_weapons[i].group_idx;
					if (grp_idx < k_group_count)
					{
						this->weapons[i].cfg.copy_values_from(this->groups[grp_idx]);
					}
				}
			}

			[[nodiscard]] bool is_weapon_overridden(std::uint16_t item_def_idx) const
			{
				const auto idx = cstypes::weapons::get_weapon_index(item_def_idx);
				return idx >= 0 && this->weapons[idx].override_group.value;
			}

			[[nodiscard]] bool is_group_overridden(std::size_t group_idx) const
			{
				if (group_idx >= k_group_count) return false;
				const auto& g = cstypes::weapons::k_groups[group_idx];
				for (std::size_t i = 0; i < g.count; ++i)
				{
					if (this->weapons[g.start_idx + i].override_group.value)
						return true;
				}
				return false;
			}

			weapon_group& get_group(std::uint32_t weapon_type, std::uint16_t item_def_idx = 0)
			{
				const auto w_idx = cstypes::weapons::get_weapon_index(item_def_idx);
				if (w_idx >= 0 && this->weapons[w_idx].override_group.value)
				{
					return this->weapons[w_idx].cfg;
				}

				const auto idx = weapon_type - cstypes::weapon_type::pistol;
				const auto safe_idx = idx < k_group_count ? idx : 2;

				return this->groups[safe_idx];
			}

			const weapon_group& get_group(std::uint32_t weapon_type, std::uint16_t item_def_idx = 0) const
			{
				const auto w_idx = cstypes::weapons::get_weapon_index(item_def_idx);
				if (w_idx >= 0 && this->weapons[w_idx].override_group.value)
				{
					return this->weapons[w_idx].cfg;
				}

				const auto idx = weapon_type - cstypes::weapon_type::pistol;
				const auto safe_idx = idx < k_group_count ? idx : 2;

				return this->groups[safe_idx];
			}
		} m_legitbot{};

		struct antiaim
		{
			enum class pitch_mode : std::uint8_t
			{
				none,
				down,
				up
			};

			xui::setting enabled{ true,{}, "anti aim", "anti aim" };
			config::enm<pitch_mode> pitch{ pitch_mode::down, "anti aim", "pitch" };
			xui::setting auto_yaw_adjust{ true,{}, "correct yaw to compensate for the models inherit sideways roll", "anti aim" };
			xui::setting manual_left{ false,{ 'Z', xui::bind_mode::toggle }, "force left", "anti aim" };
			xui::setting manual_right{ false,{ 'C', xui::bind_mode::toggle }, "force right", "anti aim" };
			xui::setting hide_shots{ true,{}, "hide onshot", "anti aim" };
			xui::setting avoid_backstab{ true,{}, "avoid backstab", "anti aim" };
			xui::setting direction_indicator{ true,{}, "direction indicator", "anti aim" };
			config::col direction_indicator_color{ { 173, 192, 255, 220 }, "anti aim", "direction indicator color" };
			xui::setting direction_indicator_glow{ true,{}, "direction indicator glow", "anti aim" };
			config::val<float> direction_indicator_glow_strength{ 0.55f, "anti aim", "direction indicator glow strength" };

			antiaim()
			{
				this->manual_left.bind.excludes = &this->manual_right;
				this->manual_right.bind.excludes = &this->manual_left;
			}
		} m_antiaim{};

		struct quickpeek
		{
			xui::setting enabled{ false,{ 'V', xui::bind_mode::hold_on }, "quick peek", "peek assistance" };
			config::col color{ { 173, 192, 255, 255 }, "peek assistance", "quick peek color" };
			config::col retrack_color{ { 255, 171, 234, 255 }, "peek assistance", "retracting color" };
		} m_quickpeek{};

		struct duckpeek
		{
			xui::setting enabled{ false,{ 'C', xui::bind_mode::hold_on }, "duck peek", "peek assistance" };
		} m_duckpeek{};

		struct lagcomp_settings
		{
			config::val<int> max_backtrack_ticks{ 12, "ragebot", "max backtrack ticks" };
			xui::setting extrapolation{ true,{}, "extrapolation", "ragebot" };
			config::val<int> max_extrapolate_ticks{ 8, "ragebot", "max extrapolate ticks" };
		} m_lagcomp{};

		struct zeusbot
		{
			xui::setting enabled{ true,{}, "zeusbot", "other 'bots'" };
			xui::setting drop_after{ true,{}, "drop after", "zeusbot" };
			config::val<float> max_fov{ 180.0f, "zeusbot", "max fov" };
		} m_zeusbot{};

		struct autos
		{
			xui::setting revolver{ true,{}, "auto revolver", "autos" };
			xui::setting revolver_quick{ true,{}, "revolver quick shot", "autos" };
			xui::setting scope{ true,{}, "auto scope", "autos" };
		} m_autos{};

		struct knifebot
		{
			xui::setting enabled{ true,{}, "knifebot", "other 'bots'" };
			config::val<float> max_fov{ 180.0f, "knifebot", "max fov" };
		} m_knifebot{};

		struct penetration_crosshair
		{
			xui::setting enabled{ false,{}, "penetration crosshair", "pen crosshair" };
			config::col can_penetrate_fill{ { 173, 192, 255, 120 }, "pen crosshair", "can penetrate fill" };
			config::col can_penetrate_outline{ { 173, 192, 255, 210 }, "pen crosshair", "can penetrate outline" };
			config::col blocked_fill{ { 252, 217, 240, 80 }, "pen crosshair", "blocked fill" };
			config::col blocked_outline{ { 252, 217, 240, 160 }, "pen crosshair", "blocked outline" };
			xui::setting glow{ true,{}, "glow", "pen crosshair" };
			config::val<float> glow_strength{ 1.0f, "pen crosshair", "glow strength" };
		} m_penetration_crosshair{};
	};

	struct esp
	{
		enum class cham_ids : std::uint8_t
		{
			liquid, metallic, matte, flat, bloom, outlines, glow, electric, distortion, hologram, pearl,
			liquid_ignorez, matte_ignorez, flat_ignorez, bloom_ignorez, outlines_ignorez, glow_ignorez, distortion_ignorez, hologram_ignorez,
			outline_glow, outline_glow_ignorez,
			count
		};

		static constexpr bool is_outline_material(cham_ids id) noexcept
		{
			return id == cham_ids::outlines ||
				id == cham_ids::outlines_ignorez ||
				id == cham_ids::outline_glow ||
				id == cham_ids::outline_glow_ignorez ||
				id == cham_ids::glow ||
				id == cham_ids::glow_ignorez;
		}

		struct outline_glow_config
		{
			config::val<float> intensity{ 18.0f };
			config::val<float> thickness{ 4.0f };
			config::val<float> softness{ 1.2f };
			config::val<float> opacity{ 1.0f };
			config::val<float> inner_spread{ 0.0f };
			config::val<float> pulse_speed{ 0.0f };

			outline_glow_config() = default;

			void reg(std::string_view cat, std::string_view prefix)
			{
				const auto s = std::string(cat);
				const auto p = prefix.empty() ? "" : (std::string(prefix) + " ");
				this->intensity.reg(s, p + "glow intensity");
				this->thickness.reg(s, p + "glow thickness");
				this->softness.reg(s, p + "glow softness");
				this->opacity.reg(s, p + "glow opacity");
				this->inner_spread.reg(s, p + "glow inner spread");
				this->pulse_speed.reg(s, p + "glow pulse speed");
			}
		};

		struct chams_layer
		{
			xui::setting enabled{ false,{}, "chams layer", "chams" };
			config::col color{ { 255, 255, 255, 255 } };
			config::enm<cham_ids> material{ cham_ids::matte };
			xui::setting filled{ true,{}, "filled", "chams" };
			outline_glow_config glow{};

			void init(std::string_view cat, std::string_view layer_prefix)
			{
				const auto s = std::string(cat);
				const auto p = std::string(layer_prefix);
				this->enabled.name = p + " layer";
				this->enabled.category = s;
				this->color.reg(s, p + " color");
				this->material.reg(s, p + " material");
				this->filled.name = p + " filled";
				this->filled.category = s;
				this->filled.value = true;
				this->glow.reg(s, p);
			}
		};

		struct chams_config
		{
			xui::setting enabled{ false,{}, "chams", "chams" };
			chams_layer primary{};
			chams_layer secondary{};
			chams_layer overlay{};

			void init(std::string_view cat, std::string_view toggle_name = "chams")
			{
				const auto s = std::string(cat);
				this->enabled.name = std::string(toggle_name);
				this->enabled.category = s;
				this->primary.init(s, "primary");
				this->secondary.init(s, "secondary");
				this->overlay.init(s, "overlay");
			}
		};

		struct glow_target
		{
			xui::setting enabled{ false,{}, "glow", "glow" };
			config::col color{ { 173, 192, 255, 75 } };

			void init(std::string_view cat, std::string_view color_name = "color", std::string_view toggle_name = "glow")
			{
				this->enabled.category = std::string(cat);
				this->enabled.name = std::string(toggle_name);
				this->color.reg(cat, color_name);
			}
		};

		struct player
		{
			struct overlay
			{
				xui::setting enabled{ true,{}, "esp overlay", "esp" };

				struct box
				{
					enum class style_type : std::uint8_t { full, cornered };

					xui::setting enabled{};
					config::enm<style_type> style{ style_type::cornered };
					xui::setting fill{};
					xui::setting outline{};
					config::val<float> corner_length{ 10.0f };

					config::col visible_color{ { 173, 192, 255, 255 } };
					config::col occluded_color{ { 255, 171, 234, 255 } };

					box() = default;

					explicit box(const std::string& prefix)
						: enabled{ false,{}, "bounding box", prefix + " box" }
						, fill{ true,{}, "fill", prefix + " box" }
						, outline{ true,{}, "outline", prefix + " box" }
					{
						const auto cat = prefix + " box";
						this->style.reg(cat, "style");
						this->corner_length.reg(cat, "corner length");
						this->visible_color.reg(cat, "visible color");
						this->occluded_color.reg(cat, "occluded color");
					}
				};

				struct skeleton
				{
					enum class mode : std::uint8_t { normal, backtrack };

					xui::setting enabled{};
					config::enm<mode> type{ mode::backtrack };
					config::val<float> thickness{ 1.5f };

					config::col visible_color{ { 173, 192, 255, 255 } };
					config::col occluded_color{ { 220, 225, 240, 255 } };

					skeleton() = default;

					explicit skeleton(const std::string& prefix)
						: enabled{ false,{}, "skeleton", prefix + " skeleton" }
					{
						const auto cat = prefix + " skeleton";
						this->type.reg(cat, "mode");
						this->thickness.reg(cat, "thickness");
						this->visible_color.reg(cat, "visible color");
						this->occluded_color.reg(cat, "occluded color");
					}
				};

				struct health_bar
				{
					enum class position_type : std::uint8_t { left, top, bottom };

					xui::setting enabled{};
					config::enm<position_type> position{ position_type::left };
					xui::setting outline_setting{};
					xui::setting gradient{};
					xui::setting show_value{};
					xui::setting glow{};

					config::col full_color{ { 173, 192, 255, 255 } };
					config::col low_color{ { 130, 145, 200, 255 } };
					config::col background_color{ { 0, 0, 0, 255 } };
					config::col outline_color{ { 0, 0, 0, 255 } };
					config::col text_color{ { 255, 255, 255, 255 } };
					config::col glow_color{ { 173, 192, 255, 255 } };
					config::val<float> glow_strength{ 0.55f };

					health_bar() = default;

					explicit health_bar(const std::string& prefix)
						: enabled{ true,{}, "health bar", prefix + " health" }
						, outline_setting{ true,{}, "outline", prefix + " health" }
						, gradient{ true,{}, "gradient", prefix + " health" }
						, show_value{ true,{}, "show value", prefix + " health" }
						, glow{ true,{}, "glow", prefix + " health" }
					{
						const auto cat = prefix + " health";
						this->position.reg(cat, "position");
						this->full_color.reg(cat, "full color");
						this->low_color.reg(cat, "low color");
						this->background_color.reg(cat, "background");
						this->outline_color.reg(cat, "outline color");
						this->text_color.reg(cat, "text color");
						this->glow_color.reg(cat, "glow color");
						this->glow_strength.reg(cat, "glow strength");
					}
				};

				struct ammo_bar
				{
					enum class position_type : std::uint8_t { left, top, bottom };

					xui::setting enabled{};
					config::enm<position_type> position{ position_type::bottom };
					xui::setting outline_setting{};
					xui::setting gradient{};
					xui::setting show_value{};
					xui::setting glow{};

					config::col full_color{ { 255, 171, 234, 255 } };
					config::col low_color{ { 255, 210, 244, 255 } };
					config::col background_color{ { 0, 0, 0, 255 } };
					config::col outline_color{ { 0, 0, 0, 255 } };
					config::col text_color{ { 255, 255, 255, 255 } };
					config::col glow_color{ { 173, 192, 255, 255 } };
					config::val<float> glow_strength{ 0.55f };

					ammo_bar() = default;

					explicit ammo_bar(const std::string& prefix)
						: enabled{ true,{}, "ammo bar", prefix + " ammo" }
						, outline_setting{ true,{}, "outline", prefix + " ammo" }
						, gradient{ true,{}, "gradient", prefix + " ammo" }
						, show_value{ false,{}, "show value", prefix + " ammo" }
						, glow{ true,{}, "glow", prefix + " ammo" }
					{
						const auto cat = prefix + " ammo";
						this->position.reg(cat, "position");
						this->full_color.reg(cat, "full color");
						this->low_color.reg(cat, "low color");
						this->background_color.reg(cat, "background");
						this->outline_color.reg(cat, "outline color");
						this->text_color.reg(cat, "text color");
						this->glow_color.reg(cat, "glow color");
						this->glow_strength.reg(cat, "glow strength");
					}
				};

				struct info_flags
				{
					enum flag : std::uint8_t
					{
						money = 0, armor, kit, scoped, defusing, flashed, ping, distance, count
					};

					xui::setting enabled{};
					config::bools<count> flags{ { false, false, false, true, true, true, true, false } };

					config::col money_color{ { 160, 210, 140, 255 } };
					config::col armor_color{ { 220, 225, 240, 255 } };
					config::col kit_color{ { 173, 192, 255, 255 } };
					config::col scoped_color{ { 220, 225, 240, 255 } };
					config::col defusing_color{ { 173, 192, 255, 255 } };
					config::col flashed_color{ { 240, 230, 170, 255 } };
					config::col distance_color{ { 185, 190, 205, 255 } };

					info_flags() = default;

					explicit info_flags(const std::string& prefix) : enabled{ true,{}, "info flags", prefix + " flags" }
					{
						const auto cat = prefix + " flags";
						this->flags.reg(cat, "flags");
						this->money_color.reg(cat, "money color");
						this->armor_color.reg(cat, "armor color");
						this->kit_color.reg(cat, "kit color");
						this->scoped_color.reg(cat, "scoped color");
						this->defusing_color.reg(cat, "defusing color");
						this->flashed_color.reg(cat, "flashed color");
						this->distance_color.reg(cat, "distance color");
					}

					[[nodiscard]] bool has(flag f) const { return this->flags[f]; }
				};

				struct name
				{
					xui::setting enabled{};
					config::col color{ { 255, 255, 255, 225 } };

					name() = default;

					explicit name(const std::string& prefix) : enabled{ true,{}, "name", prefix + " name" }
					{
						this->color.reg(prefix + " name", "color");
					}
				};

				struct weapon
				{
					enum class display_type : std::uint8_t { text, icon, text_and_icon };

					xui::setting enabled{};
					config::enm<display_type> display{ display_type::text_and_icon };

					config::col text_color{ { 255, 255, 255, 225 } };
					config::col icon_color{ { 255, 255, 255, 225 } };

					weapon() = default;

					explicit weapon(const std::string& prefix) : enabled{ true,{}, "weapon", prefix + " weapon" }
					{
						const auto cat = prefix + " weapon";
						this->display.reg(cat, "display");
						this->text_color.reg(cat, "text color");
						this->icon_color.reg(cat, "icon color");
					}
				};

				struct oof_arrow
				{
					xui::setting enabled{};
					xui::setting glow{};
					config::val<float> width{ 20.0f };
					config::val<float> height{ 15.0f };
					config::val<float> radius_x{ 200.0f };
					config::val<float> radius_y{ 200.0f };
					config::val<float> glow_strength{ 1.0f };

					config::col visible_color{ { 255, 171, 234, 255 } };
					config::col occluded_color{ { 173, 192, 255, 255 } };

					oof_arrow() = default;

					explicit oof_arrow(const std::string& prefix) : enabled{ true,{}, "oof arrow", prefix + " oof" }, glow{ true,{}, "glow", prefix + " oof" }
					{
						const auto cat = prefix + " oof";
						this->width.reg(cat, "width");
						this->height.reg(cat, "height");
						this->radius_x.reg(cat, "radius x");
						this->radius_y.reg(cat, "radius y");
						this->glow_strength.reg(cat, "glow strength");
						this->visible_color.reg(cat, "visible color");
						this->occluded_color.reg(cat, "occluded color");
					}
				};

				box m_box{};
				skeleton m_skeleton{};
				health_bar m_health_bar{};
				ammo_bar m_ammo_bar{};
				info_flags m_info_flags{};
				name m_name{};
				weapon m_weapon{};
				oof_arrow m_oof_arrow{};

				overlay() = default;

				explicit overlay(const char* prefix, bool enabled_default = true)
					: overlay{ std::string{ prefix }, enabled_default }
				{
				}

				explicit overlay(const std::string& prefix, bool enabled_default = true)
					: enabled{ enabled_default,{}, "esp overlay", prefix }
					, m_box{ prefix }
					, m_skeleton{ prefix }
					, m_health_bar{ prefix }
					, m_ammo_bar{ prefix }
					, m_info_flags{ prefix }
					, m_name{ prefix }
					, m_weapon{ prefix }
					, m_oof_arrow{ prefix }
				{
				}
			};

			std::array<overlay, 2> m_overlay{ { overlay{ "esp enemy" }, overlay{ "esp team", false } } };

			struct chams
			{
				chams_config enemy{};
				chams_config enemy_ragdoll{};
				chams_config team{};
				chams_config team_ragdoll{};
				chams_config local{};
				chams_config local_ragdoll{};
				chams_config backtrack{};
				chams_config onshot{};
				config::val<float> onshot_fade_time{ 0.8f, "chams onshot", "fade time" };

				chams()
				{
					this->enemy.init("chams enemy", "chams");
					this->enemy.enabled.value = true;
					this->enemy.primary.enabled.value = true;
					this->enemy.primary.color.value = { 173, 192, 255, 150 };
					this->enemy.primary.material.value = cham_ids::flat;
					this->enemy.secondary.enabled.value = true;
					this->enemy.secondary.color.value = { 255, 208, 243, 118 };
					this->enemy.secondary.material.value = cham_ids::flat_ignorez;

					this->enemy_ragdoll.init("chams enemy ragdoll", "ragdoll chams");

					this->team.init("chams team", "chams");

					this->team_ragdoll.init("chams team ragdoll", "ragdoll chams");

					this->local.init("chams local", "chams");
					this->local.enabled.value = true;
					this->local.overlay.enabled.value = true;
					this->local.overlay.color.value = { 173, 192, 255, 175 };
					this->local.overlay.material.value = cham_ids::outlines;

					this->local_ragdoll.init("chams local ragdoll", "ragdoll chams");

					this->backtrack.init("chams backtrack", "backtrack chams");
					this->backtrack.primary.color.value = { 173, 192, 255, 25 };
					this->backtrack.primary.material.value = cham_ids::flat;
					this->backtrack.secondary.color.value = { 173, 192, 255, 255 };
					this->backtrack.secondary.material.value = cham_ids::outlines;

					this->onshot.init("chams onshot", "onshot chams");
					this->onshot.primary.enabled.value = true;
					this->onshot.primary.color.value = { 255, 100, 100, 200 };
					this->onshot.primary.material.value = cham_ids::flat;
					this->onshot.secondary.color.value = { 255, 100, 100, 100 };
					this->onshot.secondary.material.value = cham_ids::flat_ignorez;
					this->onshot.overlay.color.value = { 255, 100, 100, 255 };
					this->onshot.overlay.material.value = cham_ids::outlines;
				}
			} m_chams{};

			struct glow
			{
				glow_target enemy{ .enabled = { true,{}, "glow", "glow enemy" }, .color = { { 173, 192, 255, 40 }, "glow enemy", "color" } };
				glow_target enemy_ragdoll{ .enabled = { false,{}, "ragdoll glow", "glow enemy" }, .color = { { 173, 192, 255, 40 }, "glow enemy", "ragdoll color" } };
				glow_target team{ .enabled = { true,{}, "glow", "glow team" }, .color = { { 225, 225, 225, 40 }, "glow team", "color" } };
				glow_target team_ragdoll{ .enabled = { false,{}, "ragdoll glow", "glow team" }, .color = { { 173, 192, 255, 40 }, "glow team", "ragdoll color" } };
				glow_target local{ .enabled = { false,{}, "glow", "glow local" }, .color = { { 252, 217, 240, 50 }, "glow local", "color" } };
				glow_target local_ragdoll{ .enabled = { false,{}, "ragdoll glow", "glow local" }, .color = { { 173, 192, 255, 40 }, "glow local", "ragdoll color" } };
			} m_glow{};

		} m_player{};

		struct viewmodel
		{
			chams_config weapon{};
			chams_config arms{};

			viewmodel()
			{
				this->weapon.init("viewmodel weapon", "weapon chams");
				this->weapon.enabled.category = "viewmodel";
				this->weapon.enabled.value = true;
				this->weapon.primary.color.value = { 255, 255, 255, 255 };
				this->weapon.primary.material.value = cham_ids::matte;
				this->weapon.overlay.enabled.value = true;
				this->weapon.overlay.color.value = { 217, 173, 202, 175 };
				this->weapon.overlay.material.value = cham_ids::glow;

				this->arms.init("viewmodel arms", "arms chams");
				this->arms.enabled.category = "viewmodel";
				this->arms.enabled.value = true;
				this->arms.primary.color.value = { 173, 192, 255, 255 };
				this->arms.primary.material.value = cham_ids::outlines;
				this->arms.overlay.enabled.value = true;
				this->arms.overlay.color.value = { 173, 192, 255, 255 };
				this->arms.overlay.material.value = cham_ids::outlines;
			}
		} m_viewmodel{};

		struct local_alpha
		{
			xui::setting enabled{ true,{}, "lower opacity", "chams local" };
			config::val<float> opacity{ 0.5f, "chams local", "opacity" };
			xui::setting only_scoped{ true,{}, "only when scoped", "chams local" };
		} m_local_alpha{};

		struct legacy_outline_glow_config : outline_glow_config
		{
			legacy_outline_glow_config()
			{
				this->intensity.reg("chams outline glow", "intensity");
				this->thickness.reg("chams outline glow", "thickness");
				this->softness.reg("chams outline glow", "softness");
				this->opacity.reg("chams outline glow", "opacity");
				this->inner_spread.reg("chams outline glow", "inner spread");
				this->pulse_speed.reg("chams outline glow", "pulse speed");
			}
		} m_outline_glow{};

		struct item
		{
			static constexpr auto k_group_count{ 6u };
			static constexpr const char* k_group_names[]{ "pistol", "smg", "rifle", "shotgun", "sniper", "utility" };

			struct overlay
			{
				struct group
				{
					enum class display_type : std::uint8_t { text, icon, text_and_icon };

					config::enm<display_type> display{ display_type::icon };
					config::val<float> max_distance{ 50.0f };
					config::col text_color{ { 255, 255, 255, 225 } };
					config::col icon_color{ { 255, 255, 255, 225 } };

					void init(std::string_view cat)
					{
						const auto s = std::string(cat);
						this->display.reg(s, "display");
						this->max_distance.reg(s, "max distance");
						this->text_color.reg(s, "text color");
						this->icon_color.reg(s, "icon color");
					}
				};

				xui::setting enabled{ true,{}, "item esp", "esp items" };
				xui::setting pistol{ false,{}, "pistol", "esp items" };
				xui::setting smg{ false,{}, "smg", "esp items" };
				xui::setting rifle{ false,{}, "rifle", "esp items" };
				xui::setting shotgun{ false,{}, "shotgun", "esp items" };
				xui::setting sniper{ true,{}, "sniper", "esp items" };
				xui::setting utility{ true,{}, "utility", "esp items" };

				std::array<group, k_group_count> groups{};

				overlay()
				{
					for (auto i = 0u; i < k_group_count; ++i)
					{
						this->groups[i].init(std::string("esp items - ") + k_group_names[i]);
					}

					this->groups[4].display = group::display_type::text_and_icon;
					this->groups[4].max_distance = 100.0f;
					this->groups[5].display = group::display_type::text_and_icon;
					this->groups[5].max_distance = 100.0f;
				}

				xui::setting& group_toggle(std::uint32_t id)
				{
					switch (id)
					{
					case 0: return this->pistol;
					case 1: return this->smg;
					case 2: return this->rifle;
					case 3: return this->shotgun;
					case 4: return this->sniper;
					case 5: return this->utility;
					default: return this->pistol;
					}
				}

				[[nodiscard]] bool is_active(std::uint32_t group_id) const
				{
					switch (group_id)
					{
					case 0: return this->pistol.value;
					case 1: return this->smg.value;
					case 2: return this->rifle.value;
					case 3: return this->shotgun.value;
					case 4: return this->sniper.value;
					case 5: return this->utility.value;
					default: return false;
					}
				}

				group& get_group(std::uint32_t group_id)
				{
					return this->groups[group_id < k_group_count ? group_id : 2];
				}

				const group& get_group(std::uint32_t group_id) const
				{
					return this->groups[group_id < k_group_count ? group_id : 2];
				}
			} m_overlay{};

			struct chams
			{
				xui::setting enabled{ true,{}, "item chams", "chams items" };
				xui::setting pistol{ false,{}, "pistol", "chams items" };
				xui::setting smg{ false,{}, "smg", "chams items" };
				xui::setting rifle{ false,{}, "rifle", "chams items" };
				xui::setting shotgun{ false,{}, "shotgun", "chams items" };
				xui::setting sniper{ true,{}, "sniper", "chams items" };
				xui::setting utility{ true,{}, "utility", "chams items" };

				std::array<chams_config, k_group_count> groups{};

				chams()
				{
					for (auto i = 0u; i < k_group_count; ++i)
					{
						const auto cat = std::string("chams items - ") + k_group_names[i];
						this->groups[i].init(cat, "item chams");
					}

					this->groups[4].primary.enabled.value = true;
					this->groups[4].primary.color = { 173, 192, 255, 255 };
					this->groups[4].primary.material = cham_ids::flat;

					this->groups[5].primary.enabled.value = true;
					this->groups[5].primary.color = { 173, 192, 255, 255 };
					this->groups[5].primary.material = cham_ids::flat;
				}

				xui::setting& group_toggle(std::uint32_t id)
				{
					switch (id)
					{
					case 0: return this->pistol;
					case 1: return this->smg;
					case 2: return this->rifle;
					case 3: return this->shotgun;
					case 4: return this->sniper;
					case 5: return this->utility;
					default: return this->pistol;
					}
				}

				[[nodiscard]] bool is_active(std::uint32_t group_id) const
				{
					switch (group_id)
					{
					case 0: return this->pistol.value;
					case 1: return this->smg.value;
					case 2: return this->rifle.value;
					case 3: return this->shotgun.value;
					case 4: return this->sniper.value;
					case 5: return this->utility.value;
					default: return false;
					}
				}

				chams_config& get_group(std::uint32_t group_id)
				{
					return this->groups[group_id < k_group_count ? group_id : 2];
				}

				const chams_config& get_group(std::uint32_t group_id) const
				{
					return this->groups[group_id < k_group_count ? group_id : 2];
				}
			} m_chams{};

			struct glow
			{
				xui::setting enabled{ true,{}, "item glow", "glow items" };
				xui::setting pistol{ false,{}, "pistol", "glow items" };
				xui::setting smg{ false,{}, "smg", "glow items" };
				xui::setting rifle{ false,{}, "rifle", "glow items" };
				xui::setting shotgun{ false,{}, "shotgun", "glow items" };
				xui::setting sniper{ true,{}, "sniper", "glow items" };
				xui::setting utility{ true,{}, "utility", "glow items" };

				std::array<glow_target, k_group_count> groups{};

				glow()
				{
					for (auto i = 0u; i < k_group_count; ++i)
					{
						const auto cat = std::string("glow items - ") + k_group_names[i];
						this->groups[i].init(cat, "color", k_group_names[i]);
					}

					this->groups[4].color = { 173, 192, 255, 50 };
					this->groups[5].color = { 173, 192, 255, 50 };
				}

				xui::setting& group_toggle(std::uint32_t id)
				{
					switch (id)
					{
					case 0: return this->pistol;
					case 1: return this->smg;
					case 2: return this->rifle;
					case 3: return this->shotgun;
					case 4: return this->sniper;
					case 5: return this->utility;
					default: return this->pistol;
					}
				}

				[[nodiscard]] bool is_active(std::uint32_t group_id) const
				{
					switch (group_id)
					{
					case 0: return this->pistol.value;
					case 1: return this->smg.value;
					case 2: return this->rifle.value;
					case 3: return this->shotgun.value;
					case 4: return this->sniper.value;
					case 5: return this->utility.value;
					default: return false;
					}
				}

				glow_target& get_group(std::uint32_t group_id)
				{
					return this->groups[group_id < k_group_count ? group_id : 2];
				}

				const glow_target& get_group(std::uint32_t group_id) const
				{
					return this->groups[group_id < k_group_count ? group_id : 2];
				}
			} m_glow{};
		} m_item{};

		struct projectile
		{
			static constexpr auto k_group_count{ 6u };
			static constexpr const char* k_group_names[]{ "he grenade", "flashbang", "smoke", "molotov", "decoy", "inferno" };

			struct overlay
			{
				struct infernos
				{
					config::col fill_color{ { 173, 192, 255, 50 }, "esp inferno", "fill color" };
					config::col outline_color{ { 255, 171, 234, 150 }, "esp inferno", "outline color" };
					config::val<float> outline_thickness{ 1.5f, "esp inferno", "outline thickness" };
					xui::setting glow{ true,{}, "glow", "esp inferno" };
					config::val<float> glow_strength{ 0.55f, "esp inferno", "glow strength" };
				} m_infernos{};

				struct indicator
				{
					static constexpr auto k_group_count{ 3u };
					static constexpr const char* k_group_names[]{ "he grenade", "molotov", "inferno" };

					struct group
					{
						xui::setting enabled{ true,{}, "", "" };
						config::col arc_color{ { 173, 192, 255, 225 } };
						config::col icon_color{ { 255, 255, 255, 225 } };
						config::col background_color{ { 0, 0, 0, 175 } };
						xui::setting glow{ true,{}, "", "" };
						config::val<float> glow_strength{ 1.0f };

						void init(std::string_view cat)
						{
							const auto s = std::string(cat);
							this->enabled.name = "indicator " + s;
							this->enabled.category = s;
							this->glow.name = "glow";
							this->glow.category = s;
							this->arc_color.reg(s, "arc color");
							this->icon_color.reg(s, "icon color");
							this->background_color.reg(s, "background color");
							this->glow_strength.reg(s, "glow strength");
						}
					};

					std::array<group, k_group_count> groups{};

					indicator()
					{
						for (auto i = 0u; i < k_group_count; ++i)
						{
							this->groups[i].init(std::string("esp indicator - ") + k_group_names[i]);
						}

						this->groups[2].arc_color = { 255, 171, 234, 225 };
					}

					group& get_group(std::uint32_t id)
					{
						return this->groups[id < k_group_count ? id : 0];
					}

					const group& get_group(std::uint32_t id) const
					{
						return this->groups[id < k_group_count ? id : 0];
					}
				} m_indicator{};

				struct group
				{
					enum class display_type : std::uint8_t { text, icon, text_and_icon };

					config::enm<display_type> display{ display_type::text_and_icon };
					config::val<float> max_distance{ 100.0f };
					config::col text_color{ { 255, 255, 255, 225 } };
					config::col icon_color{ { 255, 255, 255, 225 } };

					void init(std::string_view cat)
					{
						const auto s = std::string(cat);
						this->display.reg(s, "display");
						this->max_distance.reg(s, "max distance");
						this->text_color.reg(s, "text color");
						this->icon_color.reg(s, "icon color");
					}
				};

				xui::setting enabled{ true,{}, "projectile esp", "esp projectiles" };
				xui::setting he_grenade{ true,{}, "he grenade", "esp projectiles" };
				xui::setting flashbang{ true,{}, "flashbang", "esp projectiles" };
				xui::setting smoke{ true,{}, "smoke", "esp projectiles" };
				xui::setting molotov{ true,{}, "molotov", "esp projectiles" };
				xui::setting decoy{ true,{}, "decoy", "esp projectiles" };
				xui::setting inferno{ true,{}, "inferno", "esp projectiles" };

				std::array<group, 5> groups{};

				overlay()
				{
					for (auto i = 0u; i < 5u; ++i)
					{
						this->groups[i].init(std::string("esp projectiles - ") + k_group_names[i]);
					}
				}

				xui::setting& group_toggle(std::uint32_t id)
				{
					switch (id)
					{
					case 0: return this->he_grenade;
					case 1: return this->flashbang;
					case 2: return this->smoke;
					case 3: return this->molotov;
					case 4: return this->decoy;
					case 5: return this->inferno;
					default: return this->he_grenade;
					}
				}

				[[nodiscard]] bool is_active(std::uint32_t group_id) const
				{
					switch (group_id)
					{
					case 0: return this->he_grenade.value;
					case 1: return this->flashbang.value;
					case 2: return this->smoke.value;
					case 3: return this->molotov.value;
					case 4: return this->decoy.value;
					case 5: return this->inferno.value;
					default: return false;
					}
				}

				group& get_group(std::uint32_t group_id)
				{
					return this->groups[group_id < 5 ? group_id : 0];
				}

				const group& get_group(std::uint32_t group_id) const
				{
					return this->groups[group_id < 5 ? group_id : 0];
				}
			} m_overlay{};

			struct tracers
			{

			} m_tracers{};
		} m_projectile{};

		struct other
		{
			xui::setting bomb_timer{ true,{}, "bomb timer", "other esp" };
		} m_other{};
	};

	struct changer
	{
		struct applied_skin
		{
			int paint_kit_id{};
			float wear{ 0.01f };
			int seed{};
			bool stattrak{};
			int stattrak_count{};

			bool operator==(const applied_skin&) const = default;
		};

		struct skin_map_field : config::custom_field
		{
			std::unordered_map<std::int16_t, applied_skin> data{};

			static int bounded_integer(const nlohmann::json& object, const char* key, int fallback, int maximum)
			{
				const auto it = object.find(key);
				if (it == object.end()) return fallback;
				if (it->is_number_unsigned())
					return static_cast<int>(std::min(it->get<std::uint64_t>(), static_cast<std::uint64_t>(maximum)));
				if (it->is_number_integer())
					return static_cast<int>(std::clamp(it->get<std::int64_t>(), std::int64_t{ 0 }, static_cast<std::int64_t>(maximum)));
				return fallback;
			}

			nlohmann::json serialize() const override
			{
				auto j = nlohmann::json::object();
				for (const auto& [def, s] : data)
				{
					j[std::to_string(def)] = nlohmann::json
					{
						{ "p", s.paint_kit_id },
						{ "w", s.wear },
						{ "s", s.seed },
						{ "t", s.stattrak },
						{ "c", s.stattrak_count }
					};
				}

				return j;
			}

			void deserialize(const nlohmann::json& j) override
			{
				data.clear();

				if (!j.is_object())
				{
					return;
				}

				for (auto it = j.begin(); it != j.end(); ++it)
				{
					try
					{
						std::size_t parsed{};
						const auto def = std::stoi(it.key(), &parsed);
						if (parsed != it.key().size() || def <= 0 || def > std::numeric_limits<std::int16_t>::max() || !it.value().is_object())
							continue;

						applied_skin s{};
						s.paint_kit_id = bounded_integer(it.value(), "p", 0, std::numeric_limits<int>::max());
						s.wear = skin_options::clamp_wear(it.value().value("w", 0.01f));
						s.seed = bounded_integer(it.value(), "s", 0, 1000);
						s.stattrak = it.value().value("t", false);
						s.stattrak_count = bounded_integer(it.value(), "c", 0, std::numeric_limits<int>::max());
						data.emplace(static_cast<std::int16_t>(def), s);
					}
					catch (...) {}
				}
			}
		};

		struct agent_selection_field : config::custom_field
		{
			std::int16_t ct_def{};
			std::int16_t t_def{};

			nlohmann::json serialize() const override
			{
				return nlohmann::json
				{
					{ "ct", ct_def },
					{ "t", t_def }
				};
			}

			void deserialize(const nlohmann::json& j) override
			{
				if (!j.is_object())
				{
					return;
				}

				ct_def = j.value("ct", static_cast<std::int16_t>(0));
				t_def = j.value("t", static_cast<std::int16_t>(0));
			}
		};

		struct music_field : config::custom_field
		{
			int id{};

			nlohmann::json serialize() const override
			{
				return nlohmann::json{ { "id", id } };
			}

			void deserialize(const nlohmann::json& j) override
			{
				if (!j.is_object())
				{
					id = 0;
					return;
				}

				id = j.value("id", 0);
			}
		};

		skin_map_field skins{};
		agent_selection_field agents{};
		music_field music{};

		changer()
		{
			config::detail::register_field({ .key = config::detail::make_key("changer", "applied skins"), .type = config::field_type::custom, .ptr = &skins, .count = 1 });
			config::detail::register_field({ .key = config::detail::make_key("changer", "agents"), .type = config::field_type::custom, .ptr = &agents, .count = 1 });
			config::detail::register_field({ .key = config::detail::make_key("changer", "music"), .type = config::field_type::custom, .ptr = &music, .count = 1 });
		}
	};

	struct misc
	{
		struct scoreboard_weapons
		{
			xui::setting enabled{ false,{}, "scoreboard weapons", "misc" };
		} m_scoreboard_weapons{};

		struct name_changer
		{
			xui::setting clantag{ false,{}, "clantag", "name changer" };
			xui::setting override_name{ false,{}, "override name", "name changer" };
			config::str name{ "Player", "name changer", "name" };
		} m_name_changer{};

		struct projectile_trajectory
		{
			xui::setting enabled{ true,{}, "projectile trajectory", "trajectory" };
			xui::setting straight_throw{ true,{}, "straight throw", "trajectory" };

			config::col held_color{ { 173, 192, 255, 255 }, "trajectory", "held color" };
			config::col thrown_color{ { 220, 225, 240, 255 }, "trajectory", "thrown color" };
			config::col will_deal_damage_held_color{ { 252, 217, 240, 255 }, "trajectory", "will damage held color" };
			config::col will_deal_damage_thrown_color{ { 252, 217, 240, 255 }, "trajectory", "will damage thrown color" };

			xui::setting glow{ true,{}, "glow", "trajectory" };
			config::val<float> glow_strength{ 1.0f, "trajectory", "glow strength" };
		} m_projectile_trajectory{};

		struct impacts
		{
			enum class sound_type : int { shop_click, home_click, bell, killcard, bullet_casing, coin_pickup, item_drop, popcan, key_press, koch, custom };
			enum class marker_type : int { classic, damage, both };
			enum class bullet_impact_type : int { overlay, sparks, both };

			xui::setting hit_log{ true,{}, "hit logs", "impacts" };
			config::val<float> hit_log_duration{ 3.5f, "impacts", "hit log duration" };
			xui::setting console_log{ true,{}, "console logs", "impacts" };
			xui::setting chat_log{ false,{}, "chat logs", "impacts" };

			xui::setting miss_log{ true,{}, "miss logs", "impacts" };
			config::val<float> miss_log_duration{ 4.5f, "impacts", "miss log duration" };
			xui::setting vote_log{ true,{}, "vote logs", "impacts" };

			xui::setting hit_sound{ true,{}, "hit sound", "impacts" };
			config::enm<sound_type> hit_sound_type{ sound_type::killcard, "impacts", "hit sound type" };
			config::val<float> hit_sound_volume{ 25.0f, "impacts", "hit sound volume" };
			config::str custom_hit_sound{ "hit.wav", "impacts", "custom hit sound" };

			xui::setting death_sound{ true,{}, "death sound", "impacts" };
			config::enm<sound_type> death_sound_type{ sound_type::bell, "impacts", "death sound type" };
			config::val<float> death_sound_volume{ 20.0f, "impacts", "death sound volume" };
			config::str custom_death_sound{ "kill.wav", "impacts", "custom death sound" };

			xui::setting hit_effect{ true,{}, "hit effect", "impacts" };
			config::col hit_effect_color{ { 173, 192, 255, 255 }, "impacts", "hit effect color" };
			config::val<float> hit_effect_duration{ 0.75f, "impacts", "hit effect duration" };
			config::val<float> hit_effect_strength{ 60.0f, "impacts", "hit effect strength" };

			xui::setting death_effect{ true,{}, "death effect", "impacts" };
			config::col death_effect_color{ { 173, 192, 255, 255 }, "impacts", "death effect color" };

			xui::setting bullet_impact_effect{ true,{}, "bullet impacts", "impacts" };
			config::enm<bullet_impact_type> bullet_impact_effect_type{ bullet_impact_type::overlay, "impacts", "bullet impact type" };
			config::col bullet_impact_effect_fill_color{ { 173, 192, 255, 85 }, "impacts", "bullet impact fill color" };
			config::col bullet_impact_effect_edge_color{ { 173, 192, 255, 255 }, "impacts", "bullet impact edge color" };
			config::col bullet_impact_effect_color_spark{ { 173, 192, 255, 255 }, "impacts", "bullet impact spark color" };
			config::val<float> bullet_impact_effect_duration{ 2.5f, "impacts", "bullet impact duration" };
			xui::setting bullet_impact_effect_glow{ true,{}, "glow", "bullet impacts" };
			config::val<float> bullet_impact_effect_glow_strength{ 1.0f, "bullet impacts", "glow strength" };

			xui::setting bullet_tracers{ false,{}, "bullet tracers", "impacts" };
			config::col bullet_tracer_color{ { 173, 192, 255, 255 }, "impacts", "bullet tracer color" };
			config::val<float> bullet_tracer_duration{ 0.5f, "impacts", "bullet tracer duration" };

			xui::setting hit_marker{ true,{}, "hit marker", "impacts" };
			config::enm<marker_type> hit_marker_type{ marker_type::classic, "impacts", "hit marker type" };
			config::val<float> hit_marker_duration{ 2.5f, "impacts", "hit marker duration" };
			config::col hit_marker_color{ { 255, 255, 255, 255 }, "impacts", "hit marker color" };
			xui::setting hit_marker_glow{ true,{}, "glow", "hit marker" };
			config::val<float> hit_marker_glow_strength{ 1.0f, "hit marker", "glow strength" };
		} m_impacts{};

		struct removals
		{
			xui::setting crosshair{ true,{}, "remove crosshair", "removals" };
			xui::setting scope{ true,{}, "remove scope", "removals" };
			xui::setting skybox_fog{ true,{}, "remove skybox fog", "removals" };
			xui::setting overhead{ true,{}, "remove overhead", "removals" };
			xui::setting legs{ true,{}, "remove legs", "removals" };
			xui::setting skybox_3d{ true,{}, "remove 3d skybox", "removals" };
			xui::setting recoil{ true,{}, "remove recoil", "removals" };
			xui::setting decals{ true,{}, "remove decals", "removals" };
			xui::setting smoke{ true,{}, "remove smoke", "removals" };
			config::val<float> flash_alpha{ 25.0f, "removals", "flash alpha" };
		} m_removals{};

		struct camera
		{
			xui::setting change_fov{ true,{}, "custom fov", "camera" };
			config::val<float> fov{ 115.0f, "camera", "fov" };

			xui::setting scoped_fov_override{ false,{}, "scoped fov override", "camera" };
			config::val<float> scoped_fov{ 40.0f, "camera", "scoped fov" };

			xui::setting thirdperson{ true,{ VK_MBUTTON, xui::bind_mode::toggle }, "thirdperson", "camera" };
			config::val<float> thirdperson_distance{ 85.0f, "camera", "thirdperson distance" };
			config::val<float> thirdperson_hull_size{ 12.0f, "camera", "thirdperson hull size" };
			xui::setting spectator_thirdperson{ true,{}, "spectator thirdperson", "camera" };

			xui::setting freecam{ false,{}, "freecam", "camera" };
			config::val<float> freecam_speed{ 1000.0f, "camera", "freecam speed" };
			xui::setting freecam_block_input{ true,{}, "freecam block input", "camera" };

			xui::setting unlock_spectating{ true,{}, "unlock spectating", "camera" };

			xui::setting change_aspect_ratio{ false,{}, "custom aspect ratio", "camera" };
			config::val<float> aspect_ratio{ 1.333f, "camera", "aspect ratio" };
		} m_camera{};

		struct motion_blur
		{
			xui::setting enabled{ false,{}, "motion blur", "camera" };
			config::val<float> strength{ 1.0f, "motion blur", "strength" };
			config::val<float> smoothness{ 16.0f, "motion blur", "smoothness" };
			config::val<int> samples{ 12, "motion blur", "samples" };
			config::val<float> center_protection{ 0.20f, "motion blur", "center protection" };
			xui::setting movement_blur{ false,{}, "movement blur", "motion blur" };
		} m_motion_blur{};

		struct viewmodel_adjust
		{
			xui::setting enabled{ false,{}, "viewmodel adjust", "viewmodel" };
			config::val<float> offset_x{ 2.5f, "viewmodel", "offset x" };
			config::val<float> offset_y{ 0.0f, "viewmodel", "offset y" };
			config::val<float> offset_z{ -1.5f, "viewmodel", "offset z" };
			config::val<float> fov{ 68.0f, "viewmodel", "viewmodel fov" };
		} m_viewmodel_adjust{};

		struct hud
		{
			struct crosshair
			{
				xui::setting enabled{ true,{}, "crosshair overlay", "crosshair" };
				config::val<float> size{ 1.0f, "crosshair", "size" };
				config::val<float> outline{ 1.0f, "crosshair", "outline" };
				config::col color{ { 173, 192, 255, 255 }, "crosshair", "color" };
				config::col outline_color{ { 15, 15, 25, 200 }, "crosshair", "outline color" };
			} m_crosshair{};

			struct scope
			{
				xui::setting enabled{ true,{}, "scope overlay", "scope overlay" };
				config::val<float> line_length{ 125.0f, "scope overlay", "line length" };
				config::val<float> gap{ 8.0f, "scope overlay", "gap" };
				config::val<float> thickness{ 0.5f, "scope overlay", "thickness" };
				config::val<float> anim_speed{ 10.0f, "scope overlay", "anim speed" };
				config::col color{ { 173, 192, 255, 255 }, "scope overlay", "color" };
				xui::setting fade_in{ true,{}, "fade in", "scope overlay" };

				xui::setting glow{ true,{}, "glow", "scope overlay" };
				config::val<float> glow_strength{ 1.0f, "scope overlay", "glow strength" };
			} m_scope{};

			struct hat
			{
				enum class hat_type : std::uint8_t { kasa, bucket };

				xui::setting enabled{ false,{}, "hat", "hat" };
				config::enm<hat_type> type{ hat_type::kasa, "hat", "type" };
				config::col color{ { 255, 171, 234, 160 }, "hat", "color" };
				config::col secondary_color{ { 173, 192, 255, 160 }, "hat", "secondary color" };
				xui::setting glow{ true,{}, "glow", "hat" };
				config::val<float> glow_strength{ 1.0f, "hat", "glow strength" };
			} m_hat{};

			struct velocity
			{
				xui::setting counter{ false,{}, "velocity counter", "velocity hud" };
				xui::setting chart{ false,{}, "velocity chart", "velocity hud" };
			} m_velocity{};
		} m_hud{};

		struct post_process
		{
			struct chromatic_aberration
			{
				xui::setting enabled{ false,{}, "chromatic aberration", "post process" };
				config::val<float> intensity{ 0.003f, "post process", "chromatic aberration intensity" };
			} m_chromatic_aberration{};
		} m_post_process{};

		struct dlight
		{
			xui::setting enabled{ false,{}, "dynamic light", "misc" };
			config::col color{ { 255, 255, 255, 255 }, "dlight", "color" };
			config::val<float> radius{ 300.0f, "dlight", "radius" };
			config::val<float> z_offset{ 2.0f, "dlight", "z offset" };
		} m_dlight{};

		struct autobuy
		{
			xui::setting enabled{ true,{}, "auto buy", "autobuy" };
			config::val<int> primary_weapon{ 3, "autobuy", "primary weapon" };
			config::val<int> secondary_weapon{ 3, "autobuy", "secondary weapon" };
			xui::setting armor{ true,{}, "armor", "autobuy" };
			xui::setting defuser{ true,{}, "defuser", "autobuy" };
			xui::setting taser{ true,{}, "taser", "autobuy" };
			config::bools<5> grenades{ { true, true, true, false, false }, "autobuy", "grenades" };
		} m_autobuy{};

		struct kill_say
		{
			xui::setting enabled{ false,{}, "kill say", "misc" };
			config::str message{ "1", "misc", "kill say message" };
		} m_kill_say{};

		struct chat_spam
		{
			xui::setting enabled{ false,{}, "chat spam", "misc" };
			config::str message{ "", "misc", "chat spam message" };
			config::val<float> delay{ 1.0f, "misc", "chat spam delay" };
			config::bools<2> targets{ { true, false }, "misc", "chat spam targets" };
		} m_chat_spam{};

		xui::setting preserve_killfeed{ true,{}, "preserve killfeed", "misc" };
		xui::setting reveal_radar{ true,{}, "reveal radar", "misc" };
		xui::setting disable_game_logs{ true,{}, "disable game logs", "misc" };
		xui::setting vote_kick_self{ false,{}, "vote kick self", "misc" };
		xui::setting auto_accept{ false,{}, "auto accept", "misc" };
		config::val<int> menu_key{ VK_INSERT, "misc", "menu key" };
		config::val<int> menu_palette{ 0, "interface", "color palette" };
		xui::setting tooltips{ true, {}, "tooltips", "interface" };

		struct watermark_cfg
		{
			xui::setting enabled{ true,{}, "watermark",       "watermark" };
			xui::setting show_fps{ true,{}, "show fps",        "watermark" };
			xui::setting show_ping{ true,{}, "show ping",       "watermark" };
			xui::setting show_time{ true,{}, "show time",       "watermark" };
			xui::setting show_user{ true,{}, "show user",       "watermark" };
			xui::setting show_map{ true,{}, "show map",        "watermark" };
			xui::setting show_tick{ true,{}, "show tick",       "watermark" };
			xui::setting show_velocity{ true,{}, "show velocity", "watermark" };
			config::val<float> opacity{ 100.0f, "watermark", "opacity" };
			config::val<int>   position{ 2, "watermark", "position" }; // 0=top-left, 1=top-center, 2=top-right, 3=bottom-left, 4=bottom-center, 5=bottom-right
		} m_watermark{};

		struct widgets_cfg
		{
			xui::setting keybinds_list{ true,{}, "keybinds list", "widgets" };
			config::val<float> keybinds_x{ -1.0f, "widgets", "keybinds x" };
			config::val<float> keybinds_y{ -1.0f, "widgets", "keybinds y" };

			xui::setting spectator_list{ true,{}, "spectator list", "widgets" };
			config::val<float> spectator_x{ -1.0f, "widgets", "spectator x" };
			config::val<float> spectator_y{ -1.0f, "widgets", "spectator y" };

			enum class style : std::uint8_t { modern, classic, neo, glass };

			config::enm<style> widget_style{ style::modern, "widgets", "style" };

			struct glass_cfg
			{
				config::col text_color{ { 235, 238, 248, 255 }, "glass widget", "text color" };
				config::col icon_color{ { 173, 192, 255, 255 }, "glass widget", "icon color" };
				xui::setting per_stat_icon_colors{ false,{}, "per stat icon colors", "glass widget" };
				config::col logo_icon_color{ { 173, 192, 255, 255 }, "glass widget", "logo icon color" };
				config::col fps_icon_color{ { 173, 192, 255, 255 }, "glass widget", "fps icon color" };
				config::col ping_icon_color{ { 173, 192, 255, 255 }, "glass widget", "ping icon color" };
				config::col time_icon_color{ { 173, 192, 255, 255 }, "glass widget", "time icon color" };
				config::col vel_icon_color{ { 173, 192, 255, 255 }, "glass widget", "velocity icon color" };
				config::col warn_text_color{ { 255, 92, 92, 255 }, "glass widget", "warn text color" };
				config::col warn_icon_color{ { 255, 92, 92, 255 }, "glass widget", "warn icon color" };
				config::val<int> ping_warn_threshold{ 80, "glass widget", "ping warn threshold" };
				config::col bg_color{ { 12, 14, 20, 155 }, "glass widget", "background color" };
				config::col shadow_color{ { 0, 0, 0, 255 }, "glass widget", "shadow color" };
				config::col avatar_ring_color{ { 255, 255, 255, 40 }, "glass widget", "avatar ring color" };
				config::val<float> blur_strength{ 1.0f, "glass widget", "blur strength" };
				config::val<float> shadow_strength{ 1.4f, "glass widget", "shadow strength" };
				config::val<float> shadow_spread{ 1.2f, "glass widget", "shadow spread" };
				config::val<float> icon_size{ 15.0f, "glass widget", "icon size" };
				config::val<float> pill_height{ 32.0f, "glass widget", "pill height" };
				config::val<float> section_gap{ 16.0f, "glass widget", "section gap" };
				config::val<float> pad_x{ 14.0f, "glass widget", "padding x" };
				xui::setting show_avatar{ true,{}, "show avatar", "glass widget" };
			} m_glass{};
		} m_widgets{};
	};

	struct movement
	{
		xui::setting bhop{ true,{}, "bhop", "movement" };
		xui::setting airstrafe{ true,{}, "airstrafe", "movement" };
		xui::setting airstrafe_fully_directional{ true,{}, "fully directional", "movement - airstrafe" };
		xui::setting fastladder{ true,{}, "fastladder", "movement" };
		xui::setting edgejump{ false,{ 'E', xui::bind_mode::hold_on }, "edgejump", "movement" };
		xui::setting quickstop{ false,{ 'N', xui::bind_mode::hold_on }, "quick stop", "movement" };
		// Legacy edgebug keys intentionally preserve the previous feature settings.
		xui::setting jumpbug{ false,{}, "edgebug", "movement" };
		/// 0: auto / adaptive, 1: edge trace, 2: no jump held, 3: min speed, 4: strict vz
		config::val<int> jumpbug_mode{ 0, "movement", "edgebug mode" };
		/// 0: auto / dynamic (simulates ahead up to 64 ticks), 1..64: custom tick count
		config::val<int> jumpbug_passes{ 0, "movement", "edgebug passes" };
		/// Optional timed landing jump; not a guarantee of avoiding server fall damage
		xui::setting jumpbug_include_jump_steps{ true,{}, "edgebug jump steps", "movement" };
		xui::setting slowwalk{ false,{}, "slowwalk", "movement" };
		config::val<float> slowwalk_speed{ 33.0f, "movement", "slowwalk speed" };

		struct test_strafer
		{
			xui::setting enabled{ false,{}, "air strafer", "movement" };
		} m_test_strafer{};

		struct velocity_debug
		{
			xui::setting enabled{ false,{}, "velocity debug", "movement" };
			xui::setting reset_on_land{ true,{}, "reset peak on land", "movement - velocity debug" };
		} m_velocity_debug{};
	};

	struct world
	{
		struct weather
		{
			enum class weather_type : std::uint8_t { snow, rain, stars };

			xui::setting enabled;
			config::enm<weather_type> type;
			config::col color;

			xui::setting fog_enabled;
			config::val<float> fog_density;
			config::val<float> fog_anisotropy;
			config::val<float> fog_draw_distance;
			config::col fog_color;

			xui::setting wetness;
			config::val<float> wetness_density;
			config::val<float> wetness_speed;

			xui::setting wind;
			config::val<float> wind_strength;
			config::val<float> wind_direction;
			config::val<float> wind_turbulence;

			weather(std::string_view cat = "weather")
				: enabled{ true,{}, "weather", std::string(cat) },
				type{ weather_type::snow, cat, "type" },
				color{ { 117, 120, 142, 144 }, cat, "color" },
				fog_enabled{ true,{}, "fog", std::string(cat) },
				fog_density{ 0.5f, cat, "fog density" },
				fog_anisotropy{ 0.5f, cat, "fog anisotropy" },
				fog_draw_distance{ 8000.0f, cat, "fog draw distance" },
				fog_color{ { 160, 175, 210, 255 }, cat, "fog color" },
				wetness{ false,{}, "wetness", std::string(cat) },
				wetness_density{ 1.8f, cat, "wetness density" },
				wetness_speed{ 0.8f, cat, "wetness speed" },
				wind{ true,{}, "wind", std::string(cat) },
				wind_strength{ 3.0f, cat, "wind strength" },
				wind_direction{ 0.0f, cat, "wind direction" },
				wind_turbulence{ 1.0f, cat, "wind turbulence" }
			{
			}
		};

		struct scene
		{
			struct skyboxing
			{
				xui::setting custom_skybox;
				config::val<int> selected_skybox;

				xui::setting custom_color;
				config::col skybox_color;
				config::col cloud_color;
				config::col sun_color;

				skyboxing(std::string_view cat = "scene")
					: custom_skybox{ true,{}, "skybox material", std::string(cat) },
					selected_skybox{ 0, cat, "selected skybox" },
					custom_color{ true,{}, "skybox color", std::string(cat) },
					skybox_color{ { 249, 103, 206, 255 }, cat, "skybox color value" },
					cloud_color{ { 173, 192, 255, 0 }, cat, "cloud color" },
					sun_color{ { 173, 192, 255, 0 }, cat, "sun color" }
				{
				}
			};

			skyboxing skybox;

			xui::setting lighting;
			config::col lighting_color;
			config::val<float> lighting_intensity;
			config::vec3 lighting_rotation;

			xui::setting world_setting;
			config::col world_color;

			xui::setting bloom;
			config::val<float> bloom_value;

			xui::setting gamma;
			config::val<float> gamma_value;

			xui::setting dof;
			config::val<float> dof_near_blurry;
			config::val<float> dof_near_crisp;
			config::val<float> dof_far_crisp;
			config::val<float> dof_far_blurry;

			xui::setting ambient;
			config::col ambient_color;
			config::val<float> ambient_intensity;

			scene(std::string_view cat = "scene")
				: skybox{ cat },
				lighting{ true,{}, "lighting", std::string(cat) },
				lighting_color{ { 173, 192, 255, 255 }, cat, "lighting color" },
				lighting_intensity{ 0.85f, cat, "lighting intensity" },
				lighting_rotation{ { -0.9f, 0.3f, 0.2f }, cat, "lighting rotation" },
				world_setting{ true,{}, "world color", std::string(cat) },
				world_color{ { 115, 125, 160, 255 }, cat, "world color value" },
				bloom{ true,{}, "bloom", std::string(cat) },
				bloom_value{ 2.0f, cat, "bloom value" },
				gamma{ true,{}, "gamma", std::string(cat) },
				gamma_value{ 2.2f, cat, "gamma value" },
				dof{ true,{}, "depth of field", std::string(cat) },
				dof_near_blurry{ 0.0f, cat, "dof near blurry" },
				dof_near_crisp{ 5.0f, cat, "dof near crisp" },
				dof_far_crisp{ 600.0f, cat, "dof far crisp" },
				dof_far_blurry{ 1400.0f, cat, "dof far blurry" },
				ambient{ true,{}, "ambient", std::string(cat) },
				ambient_color{ { 233, 145, 255, 255 }, cat, "ambient color" },
				ambient_intensity{ 1.1f, cat, "ambient intensity" }
			{
			}
		};

		enum map_id : int
		{
			map_global = 0,
			map_dust2,
			map_mirage,
			map_inferno,
			map_nuke,
			map_overpass,
			map_vertigo,
			map_ancient,
			map_anubis,
			map_office,
			map_italy,
			map_max
		};

		struct map_entry
		{
			const char* id_name;
			const char* display_name;
			const char* monogram;
			xdraw::color grad_top;
			xdraw::color grad_bot;
		};

		static constexpr map_entry k_map_entries[11]{
			{ "",            "All maps", "★",   xdraw::color{ 52,  34,  98, 255 }, xdraw::color{ 16,  12,  34, 255 } },
			{ "de_dust2",    "Dust II",  "D2",  xdraw::color{ 198, 132,  48, 255 }, xdraw::color{ 68,  34,  10, 255 } },
			{ "de_mirage",   "Mirage",   "MRG", xdraw::color{ 188,  62,  94, 255 }, xdraw::color{ 58,  18,  50, 255 } },
			{ "de_inferno",  "Inferno",  "INF", xdraw::color{ 192,  56,  42, 255 }, xdraw::color{ 68,  18,  14, 255 } },
			{ "de_nuke",     "Nuke",     "NUK", xdraw::color{ 28, 138, 142, 255 }, xdraw::color{ 10,  36,  48, 255 } },
			{ "de_overpass", "Overpass", "OVP", xdraw::color{ 46, 132,  68, 255 }, xdraw::color{ 14,  46,  26, 255 } },
			{ "de_vertigo",  "Vertigo",  "VTG", xdraw::color{ 36, 102, 222, 255 }, xdraw::color{ 14,  28,  72, 255 } },
			{ "de_ancient",  "Ancient",  "ANC", xdraw::color{ 32, 108,  52, 255 }, xdraw::color{ 10,  36,  16, 255 } },
			{ "de_anubis",   "Anubis",   "ANB", xdraw::color{ 30,  72, 148, 255 }, xdraw::color{ 148,  98,  30, 255 } },
			{ "cs_office",   "Office",   "OFF", xdraw::color{ 62, 152, 222, 255 }, xdraw::color{ 20,  36,  56, 255 } },
			{ "cs_italy",    "Italy",    "ITL", xdraw::color{ 202,  82,  42, 255 }, xdraw::color{ 72,  28,  14, 255 } }
		};

		struct map_preset
		{
			xui::setting override_map;
			scene m_scene;
			weather m_weather;

			map_preset(std::string_view scene_cat, std::string_view weather_cat, std::string_view name)
				: override_map{ false,{}, "override " + std::string(name), std::string(scene_cat) },
				m_scene{ scene_cat },
				m_weather{ weather_cat }
			{
			}
		};

		static void copy_scene(scene& dst, const scene& src)
		{
			dst.skybox.custom_skybox.value = src.skybox.custom_skybox.value;
			dst.skybox.selected_skybox.value = src.skybox.selected_skybox.value;
			dst.skybox.custom_color.value = src.skybox.custom_color.value;
			dst.skybox.skybox_color.value = src.skybox.skybox_color.value;
			dst.skybox.cloud_color.value = src.skybox.cloud_color.value;
			dst.skybox.sun_color.value = src.skybox.sun_color.value;

			dst.lighting.value = src.lighting.value;
			dst.lighting_color.value = src.lighting_color.value;
			dst.lighting_intensity.value = src.lighting_intensity.value;
			dst.lighting_rotation.value = src.lighting_rotation.value;

			dst.world_setting.value = src.world_setting.value;
			dst.world_color.value = src.world_color.value;

			dst.bloom.value = src.bloom.value;
			dst.bloom_value.value = src.bloom_value.value;

			dst.gamma.value = src.gamma.value;
			dst.gamma_value.value = src.gamma_value.value;

			dst.dof.value = src.dof.value;
			dst.dof_near_blurry.value = src.dof_near_blurry.value;
			dst.dof_near_crisp.value = src.dof_near_crisp.value;
			dst.dof_far_crisp.value = src.dof_far_crisp.value;
			dst.dof_far_blurry.value = src.dof_far_blurry.value;

			dst.ambient.value = src.ambient.value;
			dst.ambient_color.value = src.ambient_color.value;
			dst.ambient_intensity.value = src.ambient_intensity.value;
		}

		static void copy_weather(weather& dst, const weather& src)
		{
			dst.enabled.value = src.enabled.value;
			dst.type.value = src.type.value;
			dst.color.value = src.color.value;

			dst.fog_enabled.value = src.fog_enabled.value;
			dst.fog_density.value = src.fog_density.value;
			dst.fog_anisotropy.value = src.fog_anisotropy.value;
			dst.fog_draw_distance.value = src.fog_draw_distance.value;
			dst.fog_color.value = src.fog_color.value;

			dst.wetness.value = src.wetness.value;
			dst.wetness_density.value = src.wetness_density.value;
			dst.wetness_speed.value = src.wetness_speed.value;

			dst.wind.value = src.wind.value;
			dst.wind_strength.value = src.wind_strength.value;
			dst.wind_direction.value = src.wind_direction.value;
			dst.wind_turbulence.value = src.wind_turbulence.value;
		}

		// Global fallback preset (uses standard "scene" & "weather" categories for config backwards-compatibility)
		map_preset m_global{ "scene",           "weather",           "global" };
		map_preset m_dust2{ "scene_dust2",     "weather_dust2",     "dust2" };
		map_preset m_mirage{ "scene_mirage",    "weather_mirage",    "mirage" };
		map_preset m_inferno{ "scene_inferno",   "weather_inferno",   "inferno" };
		map_preset m_nuke{ "scene_nuke",      "weather_nuke",      "nuke" };
		map_preset m_overpass{ "scene_overpass",  "weather_overpass",  "overpass" };
		map_preset m_vertigo{ "scene_vertigo",   "weather_vertigo",   "vertigo" };
		map_preset m_ancient{ "scene_ancient",   "weather_ancient",   "ancient" };
		map_preset m_anubis{ "scene_anubis",    "weather_anubis",    "anubis" };
		map_preset m_office{ "scene_office",    "weather_office",    "office" };
		map_preset m_italy{ "scene_italy",     "weather_italy",     "italy" };

		std::array<map_preset*, 11> presets{
			&m_global, &m_dust2, &m_mirage, &m_inferno, &m_nuke,
			&m_overpass, &m_vertigo, &m_ancient, &m_anubis, &m_office, &m_italy
		};

		// Active resolved scene & weather (read directly by scene.cpp, weather.cpp, hooks, etc.)
		scene m_scene{ "active_scene" };
		weather m_weather{ "active_weather" };

		void update_active(const std::string& current_map)
		{
			int matched_idx = -1;
			if (!current_map.empty())
			{
				std::string lower = current_map;
				for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

				for (int i = 1; i < static_cast<int>(map_max); ++i)
				{
					const char* id = k_map_entries[i].id_name;
					if (lower.find(id) != std::string::npos ||
						(std::strlen(id) > 3 && lower.find(id + 3) != std::string::npos))
					{
						matched_idx = i;
						break;
					}
				}
			}

			const map_preset* source = &m_global;
			if (matched_idx > 0 && presets[matched_idx]->override_map.value)
			{
				source = presets[matched_idx];
			}

			copy_scene(this->m_scene, source->m_scene);
			copy_weather(this->m_weather, source->m_weather);
		}

		world()
		{
			update_active("");
		}
	};

	inline combat g_combat{};
	inline esp g_esp{};
	inline changer g_changer{};
	inline misc g_misc{};
	inline movement g_movement{};
	inline world g_world{};

	inline void finalize_binds()
	{
		auto& aa = g_combat.m_antiaim;
		aa.manual_left.bind.excludes = &aa.manual_right;
		aa.manual_right.bind.excludes = &aa.manual_left;

		if (aa.manual_left.value && aa.manual_right.value)
		{
			aa.manual_right.value = false;
			aa.manual_right.bind.active = false;
		}
	}

} // namespace settings
