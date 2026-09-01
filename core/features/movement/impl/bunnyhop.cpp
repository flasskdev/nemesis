#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include "../movement.hpp"
#include <protection/game_addresses.hpp>

namespace features::movement {

namespace {

	// ── Константы ──────────────────────────────────────────────

	namespace offsets {
		// CCSPlayer_MovementServices -> m_pPawn
		constexpr std::uintptr_t movement_services_pawn = 56;
		// CBasePlayerPawn -> m_fFlags -related mask fields
		constexpr std::uintptr_t pawn_trace_mask       = 0xD48;
		constexpr std::uintptr_t pawn_flags            = 0x3F8;
		constexpr std::uint32_t   flag_in_water        = 0x10;
		constexpr std::uintptr_t water_trace_bit       = 0x20;
		// Fallback, если не удалось прочитать маску
		constexpr std::uintptr_t mask_playersolid      = 0x200400B;
	}

	namespace landing {
		constexpr float min_fraction = 0.001f;
		constexpr float max_fraction = 0.999f;
	}

	// ── Предсказание приземления ───────────────────────────────

	[[nodiscard]] std::optional<float> predict_landing_fraction(
		std::uintptr_t local_pawn,
		std::uintptr_t movement_services,
		const systems::prediction::state& prestate,
		const math::vector3& start_velocity)
	{
		if (start_velocity.z > 0.0f)
			return std::nullopt;

		// Кэшируем offset коллизии, чтобы не читать дважды
		const auto collision_offset =
			SCHEMA("C_BaseModelEntity", "m_Collision"_hash);

		const auto mins = memory::read<math::vector3>(
			local_pawn + collision_offset +
			SCHEMA("CCollisionProperty", "m_vecMins"_hash));

		const auto maxs = memory::read<math::vector3>(
			local_pawn + collision_offset +
			SCHEMA("CCollisionProperty", "m_vecMaxs"_hash));

		// Определяем trace mask
		auto trace_mask{ 0ull };
		if (const auto pawn_ptr = memory::read<std::uintptr_t>(
				movement_services + offsets::movement_services_pawn);
			pawn_ptr)
		{
			trace_mask = memory::read<std::uintptr_t>(
				pawn_ptr + offsets::pawn_trace_mask);

			if (memory::read<std::uint32_t>(pawn_ptr + offsets::pawn_flags)
				& offsets::flag_in_water)
			{
				trace_mask |= offsets::water_trace_bit;
			}
		}

		if (!trace_mask)
			trace_mask = offsets::mask_playersolid;

		const auto filter = systems::g_tracing.make_player_movement_filter(
			local_pawn, trace_mask, 11);

		// ConVar'ы с проверкой на null
		const auto* sv_gravity_convar = CONVAR("sv_gravity");
		const auto* sv_standable_convar = CONVAR("sv_standable_normal");
		if (!sv_gravity_convar || !sv_standable_convar)
			return std::nullopt;

		const auto sv_gravity          = sv_gravity_convar->get<float>();
		const auto sv_standable_normal = sv_standable_convar->get<float>();

		const auto gravity_scale = memory::read<float>(
			local_pawn + SCHEMA("C_BaseEntity", "m_flGravityScale"_hash));

		const auto effective_gravity =
			(gravity_scale > 0.0f ? gravity_scale : 1.0f) * sv_gravity;

		// Полушаг гравитации (Source использует半-step интеграцию)
		auto velocity = start_velocity;
		velocity.z -= effective_gravity * cstypes::tick_interval * 0.5f;

		const auto& o = prestate.origin;
		const math::vector3 trace_start = o;
		const math::vector3 trace_end{
			o.x + velocity.x * cstypes::tick_interval,
			o.y + velocity.y * cstypes::tick_interval,
			o.z + velocity.z * cstypes::tick_interval,
		};

		const auto result = systems::g_tracing.trace_player_bbox(
			trace_start, trace_end, { mins, maxs }, filter, movement_services);

		const auto min_normal = std::max(0.55f, sv_standable_normal * 0.9f);

		if (result.fraction < 1.0f && result.normal.z >= min_normal)
		{
			const float when = result.fraction <= 0.0f
				? landing::min_fraction
				: result.fraction;
			return std::clamp(when, landing::min_fraction, landing::max_fraction);
		}

		return std::nullopt;
	}

	// ── Subtick jump helpers ───────────────────────────────────

	void add_jump_step(proto::base_usercmd_pb* base, float when, bool pressed)
	{
		const auto subtick_moves = base->mutable_subtick_moves();
		if (!subtick_moves)
			return;

		const auto step = systems::g_input.acquire_subtick_step(subtick_moves);
		if (!step)
			return;

		step->set_button(cstypes::command_buttons::in_jump);
		step->set_pressed(pressed);
		// when уже ограничен вызывающей стороной, но подстрахуемся
		step->set_when(std::clamp(when, 0.0f, landing::max_fraction));
	}

	void apply_landing_jump(proto::base_usercmd_pb* base, float when)
	{
		// Сначала отпускаем прыжок в начале тика, затем нажимаем в предсказанной доле.
		// Порядок критичен: release -> press даёт движку корректный edge.
		add_jump_step(base, 0.0f, false);
		add_jump_step(base, when, true);
	}

	// ── Утилиты для кнопок ─────────────────────────────────────

	enum class jump_button_state : std::uint8_t {
		set,
		clear,
		unchanged,
	};

	void apply_jump_button(
		systems::input::usercmd* cmd,
		jump_button_state state)
	{
		switch (state)
		{
		case jump_button_state::set:
			cmd->buttons.value |= cstypes::command_buttons::in_jump;
			cmd->buttons.value_changed |= cstypes::command_buttons::in_jump;
			break;
		case jump_button_state::clear:
			cmd->buttons.value &= ~cstypes::command_buttons::in_jump;
			cmd->buttons.value_changed |= cstypes::command_buttons::in_jump;
			break;
		case jump_button_state::unchanged:
			break;
		}
		// value_scroll не трогаем: нативный инпут скролла должен работать как есть
	}

} // namespace

// ── Главный обработчик ────────────────────────────────────────

void bhop::on_create_move(systems::input::usercmd* cmd) const
{
	if (!settings::g_movement.bhop.value)
		return;

	const auto local = systems::g_local.get();
	if (!local.pawn)
		return;

	const auto move_type = memory::read<std::uint8_t>(
		local.pawn + SCHEMA("C_BaseEntity", "m_nActualMoveType"_hash));

	if (move_type == cstypes::move_type::ladder ||
		move_type == cstypes::move_type::noclip)
		return;

	auto* base = cmd->csgo_user_cmd.mutable_base();
	if (!base)
		return;

	const auto& prestate = systems::g_prediction.pre();

	const bool space_pressed =
		(GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
	const bool jump_held =
		(cmd->buttons.value & cstypes::command_buttons::in_jump) != 0;
	const bool jump_scrolled =
		(cmd->buttons.value_scroll & cstypes::command_buttons::in_jump) != 0;

	const bool wants_jump = jump_held || jump_scrolled || space_pressed;
	if (!wants_jump)
		return;

	// ── Auto-bhop: сервер сам обрабатывает, просто держим кнопку ──

	if (CONVAR("sv_autobunnyhopping")->get<bool>())
	{
		apply_jump_button(cmd, jump_button_state::set);
		return;
	}

	// ── Уже на земле: прыгаем немедленно ──────────────────────────

	if (prestate.on_ground)
	{
		apply_jump_button(cmd, jump_button_state::set);
		apply_landing_jump(base, landing::min_fraction);
		return;
	}

	// ── В воздухе: проверяем, приземляемся ли в этом тике ────────

	const auto movement_services = memory::read<std::uintptr_t>(
		local.pawn + SCHEMA("C_BasePlayerPawn", "m_pMovementServices"_hash));

	if (!movement_services)
	{
		apply_jump_button(cmd, jump_button_state::clear);
		return;
	}

	// Выбираем лучшую оценку скорости
	auto velocity = prestate.velocity;
	if (velocity.length_2d() < 1.0f &&
		prestate.networked_velocity.length_2d() > 1.0f)
	{
		velocity = prestate.networked_velocity;
	}

	const auto landing_frac = predict_landing_fraction(
		local.pawn, movement_services, prestate, velocity);

	if (landing_frac.has_value())
	{
		// Приземляемся внутри тика: очищаем основную кнопку,
		// чтобы движок не засчитал прыжок в начале тика,
		// и нажимаем только в предсказанной суб-тик доле.
		apply_jump_button(cmd, jump_button_state::clear);
		apply_landing_jump(base, *landing_frac);
	}
	else
	{
		// В воздухе, не приземляемся:
		// - зажат Space/кнопка -> оставляем для буферизации на следующий тик
		// - только скролл -> очищаем (импульс скролла разовый)
		apply_jump_button(
			cmd,
			(jump_held || space_pressed)
				? jump_button_state::set
				: jump_button_state::clear);
	}
}

} // namespace features::movement