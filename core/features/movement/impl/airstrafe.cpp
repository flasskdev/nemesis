#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>

namespace features::movement {

namespace {
    constexpr auto k_max_subticks{ 16 };
    constexpr auto k_min_strafe_speed{ 5.0f };
    constexpr auto k_mouse_strafe_threshold{ 0.5f };

    [[nodiscard]] float get_max_subtick_when(proto::base_usercmd_pb* base)
    {
        auto max_when{ 0.0f };
        for (auto i = 0; i < base->subtick_moves_size(); ++i)
        {
            if (const auto step = base->mutable_subtick_moves(i))
            {
                max_when = std::fmaxf(max_when, step->when());
            }
        }
        return max_when;
    }

    // Математически идеальный угол БЕЗ жестких ограничений
    [[nodiscard]] float get_ideal_strafe_angle(float speed, float dt, float wishspeed, float air_accel, float air_max_wishspeed)
    {
        if (speed < 1.0f)
        {
            return 45.0f; // 45° для быстрого старта с места
        }

        const auto accel_speed = wishspeed * air_accel * dt;
        float cos_theta{};

        if (accel_speed >= air_max_wishspeed)
        {
            cos_theta = air_max_wishspeed / (2.0f * speed);
        }
        else
        {
            cos_theta = (air_max_wishspeed - accel_speed) / speed;
        }

        cos_theta = std::clamp(cos_theta, -1.0f, 1.0f);
        
        // ВАЖНО: Убираем clamp(15.0f)! 
        // При 500 u/s идеальный угол ~5°, а не 15°
        const auto angle = std::acosf(cos_theta) * (180.0f / std::numbers::pi_v<float>);
        return std::clamp(angle, 1.0f, 89.0f);
    }

    [[nodiscard]] bool apply_yaw_subtick(proto::base_usercmd_pb* base, float when, float yaw_delta)
    {
        math::helpers::normalize_angle(yaw_delta);
        if (std::fabsf(yaw_delta) <= 0.01f)
        {
            return false;
        }

        const auto subtick_moves = base->mutable_subtick_moves();
        if (!subtick_moves)
        {
            return false;
        }

        const auto step = systems::g_input.acquire_subtick_step(subtick_moves);
        if (!step)
        {
            return false;
        }

        step->set_when(std::clamp(when, 0.0f, 0.999f));
        step->set_button(0);
        step->set_pressed(false);
        step->set_analog_forward_delta(0.0f);
        step->set_analog_left_delta(0.0f);
        step->set_yaw_delta(yaw_delta);
        step->set_pitch_delta(0.0f);

        return true;
    }
} // namespace

void airstrafe::on_create_move(systems::input::usercmd* cmd)
{
    if (features::movement::g_jumpbug.active_this_tick())
    {
        return;
    }

    if (features::movement::g_test_strafer.handled_this_tick())
    {
        return;
    }

    if (!settings::g_movement.airstrafe.value)
    {
        return;
    }

    if (features::combat::g_rage.is_firing_this_tick())
    {
        return;
    }

    const auto local = systems::g_local.get();
    if (!local.pawn)
    {
        return;
    }

    const auto move_type = memory::read<std::uint8_t>(
        local.pawn + SCHEMA("C_BaseEntity", "m_nActualMoveType"_hash));

    if (move_type == cstypes::move_type::ladder || move_type == cstypes::move_type::noclip)
    {
        return;
    }

    const auto& prestate = systems::g_prediction.pre();

    if (prestate.on_ground)
    {
        this->m_old_yaw = this->m_angles.y;
        this->m_side_switch = false;
        return;
    }

    const auto base = cmd->csgo_user_cmd.mutable_base();
    if (!base)
    {
        return;
    }

    const auto current_buttons = cmd->buttons.value;
    if (current_buttons & static_cast<std::uintptr_t>(cstypes::command_buttons::in_sprint))
    {
        return;
    }

    auto velocity = prestate.velocity;
    if (velocity.length_2d() < 1.0f && prestate.networked_velocity.length_2d() > 1.0f)
    {
        velocity = prestate.networked_velocity;
    }

    const auto speed_2d = velocity.length_2d();

    float real_yaw = this->m_angles.y;
    math::helpers::normalize_angle(real_yaw);

    const bool has_moveleft = (current_buttons & static_cast<std::uintptr_t>(cstypes::command_buttons::in_moveleft)) != 0;
    const bool has_moveright = (current_buttons & static_cast<std::uintptr_t>(cstypes::command_buttons::in_moveright)) != 0;
    const bool has_forward = (current_buttons & static_cast<std::uintptr_t>(cstypes::command_buttons::in_forward)) != 0;
    const bool has_back = (current_buttons & static_cast<std::uintptr_t>(cstypes::command_buttons::in_back)) != 0;

    auto yaw_offset = 0.0f;
    if (has_moveleft && !has_moveright)
    {
        yaw_offset = has_forward ? 45.0f : (has_back ? 135.0f : 90.0f);
    }
    else if (has_moveright && !has_moveleft)
    {
        yaw_offset = has_forward ? -45.0f : (has_back ? -135.0f : -90.0f);
    }
    else if (has_back && !has_forward)
    {
        yaw_offset = 180.0f;
    }

    float target_yaw = real_yaw + yaw_offset;
    math::helpers::normalize_angle(target_yaw);

    if (speed_2d < k_min_strafe_speed)
    {
        const auto delta_rad = (target_yaw - real_yaw) * (std::numbers::pi_v<float> / 180.0f);
        base->set_forwardmove(std::clamp(std::cosf(delta_rad), -1.0f, 1.0f));
        base->set_leftmove(std::clamp(std::sinf(delta_rad), -1.0f, 1.0f));
        this->m_old_yaw = real_yaw;
        return;
    }

    const auto sv_airaccelerate = CONVAR("sv_airaccelerate")->get<float>();
    const auto sv_maxspeed = CONVAR("sv_maxspeed")->get<float>();
    const auto sv_air_max_wishspeed = CONVAR("sv_air_max_wishspeed")->get<float>();

    // Вычисляем идеальный угол БЕЗ убийственного clamp(15.0f)
    const auto ideal_angle = get_ideal_strafe_angle(
        speed_2d,
        cstypes::tick_interval,
        sv_maxspeed,
        sv_airaccelerate,
        sv_air_max_wishspeed);

    auto velocity_angle = std::atan2f(velocity.y, velocity.x) * (180.0f / std::numbers::pi_v<float>);
    math::helpers::normalize_angle(velocity_angle);

    auto delta_view_yaw = math::helpers::normalize_yaw(real_yaw - this->m_old_yaw);
    this->m_old_yaw = real_yaw;

    bool strafe_left = false;

    // 1. Приоритет: Движение мыши
    if (std::fabsf(delta_view_yaw) > k_mouse_strafe_threshold)
    {
        strafe_left = (delta_view_yaw > 0.0f);
        this->m_side_switch = strafe_left;
    }
    // 2. Ручной стрейф на A/D
    else if (has_moveleft && !has_moveright)
    {
        strafe_left = true;
        this->m_side_switch = true;
    }
    else if (has_moveright && !has_moveleft)
    {
        strafe_left = false;
        this->m_side_switch = false;
    }
    // 3. Автоматическая S-curve осцилляция
    else
    {
        auto vel_delta = math::helpers::normalize_yaw(target_yaw - velocity_angle);
        if (std::fabsf(vel_delta) > 1.0f)
        {
            strafe_left = (vel_delta > 0.0f);
            this->m_side_switch = strafe_left;
        }
        else
        {
            this->m_side_switch = !this->m_side_switch;
            strafe_left = this->m_side_switch;
        }
    }

    auto wish_yaw = velocity_angle + (strafe_left ? ideal_angle : -ideal_angle);
    math::helpers::normalize_angle(wish_yaw);

    // === КРИТИЧЕСКИ ВАЖНО ДЛЯ CS2: Саб-тик инъекция ===
    // Без этого forwardmove/leftmove квантуются и стрейф получается рваным
    const auto start_when = get_max_subtick_when(base);
    if (start_when < 0.99f)
    {
        const auto when_step = (1.0f - start_when) / static_cast<float>(k_max_subticks + 1);
        auto acc_yaw = real_yaw;
        
        // Инжектируем 4 саб-тика для плавного поворота камеры
        for (auto i = 1; i <= std::min(4, k_max_subticks); ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(std::min(4, k_max_subticks));
            const auto target_subtick_yaw = real_yaw + math::helpers::normalize_yaw(wish_yaw - real_yaw) * t;
            
            auto yaw_delta = target_subtick_yaw - acc_yaw;
            math::helpers::normalize_angle(yaw_delta);

            const auto when_frac = start_when + static_cast<float>(i) * when_step;
            if (!apply_yaw_subtick(base, when_frac, yaw_delta))
            {
                break;
            }
            acc_yaw = target_subtick_yaw;
        }
    }

    // Синхронизируем движение с ТЕКУЩИМИ viewangles (с учетом Anti-Aim)
    const auto va = base->viewangles();
    const auto current_view_yaw = va ? va->y() : real_yaw;
    const auto delta_rad = (wish_yaw - current_view_yaw) * (std::numbers::pi_v<float> / 180.0f);
    
    base->set_forwardmove(std::clamp(std::cosf(delta_rad), -1.0f, 1.0f));
    base->set_leftmove(std::clamp(std::sinf(delta_rad), -1.0f, 1.0f));
}

void airstrafe::store_angles()
{
    this->m_angles = systems::g_input.get_view_angles();
}

void airstrafe::check_button(std::uintptr_t current_buttons, std::uintptr_t button)
{
    (void)current_buttons;
    (void)button;
}

void airstrafe::rotate_movement(proto::base_usercmd_pb* base, float target_yaw, float view_yaw) const
{
    const auto forward_move = base->forwardmove();
    const auto side_move = base->leftmove();
    const auto rot_rad = (target_yaw - view_yaw) * (std::numbers::pi_v<float> / 180.0f);
    const auto cos_rot = std::cosf(rot_rad);
    const auto sin_rot = std::sinf(rot_rad);
    const auto corrected_forward = cos_rot * forward_move - sin_rot * side_move;
    const auto corrected_side = sin_rot * forward_move + cos_rot * side_move;
    base->set_forwardmove(std::clamp(corrected_forward, -1.0f, 1.0f));
    base->set_leftmove(std::clamp(corrected_side, -1.0f, 1.0f));
}

void airstrafe::rotate_to_stop(proto::base_usercmd_pb* base, const math::vector3& velocity) const
{
    const auto speed = velocity.length_2d();
    const auto wish_yaw =
        std::atan2f(velocity.y, velocity.x) * (180.0f / std::numbers::pi_v<float>) + 180.0f;
    {
        const auto& ctx = features::combat::g_shared.ctx();
        const auto max_speed =
            (ctx.valid && ctx.weapon_vdata)
            ? memory::read<float>(ctx.weapon_vdata + SCHEMA("CCSWeaponBaseVData", "m_flMaxSpeed"_hash))
            : 250.0f;
        base->set_forwardmove(std::clamp(speed / max_speed, 0.0f, 1.0f));
        base->set_leftmove(0.0f);
    }
    const auto view_yaw = this->m_angles.y;
    const auto rotation = (view_yaw - wish_yaw) * (std::numbers::pi_v<float> / 180.0f);
    const auto fwd = base->forwardmove();
    const auto side = base->leftmove();
    base->set_forwardmove(std::clamp(
        std::cosf(rotation) * fwd - std::sinf(rotation) * side,
        -1.0f,
        1.0f));
    base->set_leftmove(std::clamp(
        (std::sinf(rotation) * fwd + std::cosf(rotation) * side) * -1.0f,
        -1.0f,
        1.0f));
}

} // namespace features::movement