#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>
#include "../movement.hpp"
#include "../movement_utils.hpp"

namespace features::movement {
namespace {
constexpr float edge_epsilon = 1.0f / 1024.0f;
std::optional<float> landing_fraction(std::uintptr_t pawn, std::uintptr_t movement,
                                    const systems::prediction::state& state)
{
    auto velocity = utils::pick_velocity(state);
    if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y) ||
        !std::isfinite(velocity.z) || velocity.z > 0.0f) return std::nullopt;
    if (!memory::read<std::uintptr_t>(pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash))) return std::nullopt;
    const auto origin = state.origin;
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z)) return std::nullopt;
    const auto collision = pawn + SCHEMA("C_BaseModelEntity", "m_Collision"_hash);
    const auto mins = memory::read<math::vector3>(collision + SCHEMA("CCollisionProperty", "m_vecMins"_hash));
    const auto maxs = memory::read<math::vector3>(collision + SCHEMA("CCollisionProperty", "m_vecMaxs"_hash));
    if (!std::isfinite(mins.x) || !std::isfinite(mins.y) || !std::isfinite(mins.z) ||
        !std::isfinite(maxs.x) || !std::isfinite(maxs.y) || !std::isfinite(maxs.z) ||
        maxs.x <= mins.x || maxs.y <= mins.y || maxs.z <= mins.z) return std::nullopt;
    const auto owner = memory::read<std::uintptr_t>(movement + 56);
    if (!owner) return std::nullopt;
    auto mask = memory::read<std::uintptr_t>(owner + 0xd48);
    if (memory::read<std::uint32_t>(owner + 0x3f8) & 0x10) mask |= 0x20;
    const auto gravity_cvar = CONVAR("sv_gravity");
    const auto normal_cvar = CONVAR("sv_standable_normal");
    if (!gravity_cvar || !normal_cvar) return std::nullopt;
    const float gravity = gravity_cvar->get<float>();
    const float normal = normal_cvar->get<float>();
    const float scale = memory::read<float>(pawn + SCHEMA("C_BaseEntity", "m_flGravityScale"_hash));
    if (!std::isfinite(gravity) || !std::isfinite(scale) || !std::isfinite(normal) ||
        gravity < 0.0f || scale < 0.0f || normal < 0.0f || normal > 1.0f) return std::nullopt;
    velocity.z -= gravity * scale * cstypes::tick_interval * 0.5f;
    const math::vector3 end{origin.x + velocity.x * cstypes::tick_interval,
                            origin.y + velocity.y * cstypes::tick_interval,
                            origin.z + velocity.z * cstypes::tick_interval};
    const auto filter = systems::g_tracing.make_player_movement_filter(pawn, mask, 11);
    const auto hit = systems::g_tracing.trace_player_bbox(origin, end, {mins, maxs}, filter, movement);
    // A collision fraction is not a clock when the trace is extended below the physical path.
    // Trace only this tick's trajectory; the next grounded command handles snap-only contacts.
    if (!std::isfinite(hit.fraction) || !std::isfinite(hit.normal.z) || hit.normal.z < normal ||
        hit.fraction <= 0.0f || hit.fraction >= 1.0f - edge_epsilon) return std::nullopt;
    return std::clamp(hit.fraction + edge_epsilon, edge_epsilon, 1.0f - edge_epsilon);
}

bool jump_event(proto::base_usercmd_pb* base, bool pressed, float when)
{
    const auto step = systems::g_input.acquire_subtick_step(base->mutable_subtick_moves());
    if (!step) return false;
    step->set_button(cstypes::command_buttons::in_jump);
    step->set_pressed(pressed);
    step->set_when(when);
    step->set_analog_forward_delta(0.0f);
    step->set_analog_left_delta(0.0f);
    step->set_pitch_delta(0.0f);
    step->set_yaw_delta(0.0f);
    return true;
}

bool jump_pair(proto::base_usercmd_pb* base, float when)
{
    const auto steps = base->mutable_subtick_moves();
    if (!steps) return false;
    const int checkpoint = steps->m_current_size;
    if (jump_event(base, false, 0.0f) && jump_event(base, true, when)) return true;
    steps->m_current_size = checkpoint;
    return false;
}
}

void bhop::on_create_move(systems::input::usercmd* cmd) const
{
    if (!cmd || !settings::g_movement.bhop.value || g_jumpbug.active_this_tick() || g_edgebug.active_this_tick()) return;
    const auto automatic = CONVAR("sv_autobunnyhopping");
    if (automatic && automatic->get<bool>()) return;
    constexpr auto jump = static_cast<std::uintptr_t>(cstypes::command_buttons::in_jump);
    if (!(cmd->buttons.value & jump)) return;
    const auto local = systems::g_local.get();
    const auto base = cmd->csgo_user_cmd.mutable_base();
    if (!base || !local.is_alive || !local.pawn || utils::is_movement_blocked(local.pawn)) return;
    const auto& state = systems::g_prediction.pre();
    const bool grounded = utils::is_on_ground(state, local.pawn);
    std::optional<float> press_when;
    if (grounded) press_when = edge_epsilon;
    else
    {
        const auto movement = memory::read<std::uintptr_t>(local.pawn + SCHEMA("C_BasePlayerPawn", "m_pMovementServices"_hash));
        if (movement) press_when = landing_fraction(local.pawn, movement, state);
    }
    const bool scheduled = press_when && jump_pair(base, *press_when);
    if (scheduled || grounded)
    {
        cmd->buttons.value |= jump;
        cmd->buttons.value_changed |= jump;
    }
    else
    {
        cmd->buttons.value &= ~jump;
        cmd->buttons.value_changed |= jump;
        jump_event(base, false, 0.0f);
    }
    cmd->buttons.value_scroll &= ~jump;
}
}
