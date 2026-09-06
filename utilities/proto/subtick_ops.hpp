#pragma once
#include "proto.hpp"
#include <cmath>
#include <optional>

namespace proto::subtick_ops {
// Keep arena-owned removed objects in the inactive tail for acquire_subtick_step().
// Never duplicate an active pointer into that tail: reusing it would corrupt another event.
template<class Predicate>
inline void erase_if(repeated_ptr_field<subtick_move_step>* steps, Predicate predicate)
{
    if (!steps || !steps->m_rep) return;
    for (int i = 0; i < steps->m_current_size;)
    {
        auto* raw = steps->m_rep->elements[i];
        auto* step = impl_ptr<subtick_move_step>(raw);
        if (!step || !predicate(step)) { ++i; continue; }
        for (int j = i + 1; j < steps->m_current_size; ++j)
            steps->m_rep->elements[j - 1] = steps->m_rep->elements[j];
        steps->m_rep->elements[--steps->m_current_size] = raw;
    }
}
inline void strip_buttons(repeated_ptr_field<subtick_move_step>* steps, std::uint64_t mask,
                          const subtick_move_step* keep1 = nullptr, const subtick_move_step* keep2 = nullptr)
{
    erase_if(steps, [&](subtick_move_step* step) {
        if (step == keep1 || step == keep2 || !(step->button() & mask)) return false;
        step->set_button(step->button() & ~mask);
        return !step->button() && step->analog_forward_delta() == 0.0f &&
            step->analog_left_delta() == 0.0f && step->pitch_delta() == 0.0f && step->yaw_delta() == 0.0f;
    });
}
// Allocate the entire replacement before changing existing events. The caller owns the button mask.
template<class Allocate>
inline bool replace_button_events(repeated_ptr_field<subtick_move_step>* steps, std::uint64_t button,
                                  std::optional<float> press_when, Allocate allocate)
{
    if (!steps || !button || (press_when && (!std::isfinite(*press_when) || *press_when <= 0.0f || *press_when >= 1.0f)))
        return false;
    const int checkpoint = steps->m_current_size;
    const auto release = allocate(steps);
    const auto press = release && press_when ? allocate(steps) : nullptr;
    if (!release || (press_when && !press)) { steps->m_current_size = checkpoint; return false; }
    const auto initialize = [&](subtick_move_step* step, bool down, float fraction) {
        step->set_button(button);
        step->set_pressed(down);
        step->set_when(fraction);
        step->set_analog_forward_delta(0.0f);
        step->set_analog_left_delta(0.0f);
        step->set_pitch_delta(0.0f);
        step->set_yaw_delta(0.0f);
    };
    initialize(release, false, 0.0f);
    if (press) initialize(press, true, *press_when);
    strip_buttons(steps, button, release, press);
    return true;
}
inline void stable_sort(repeated_ptr_field<subtick_move_step>* steps)
{
    if (!steps || !steps->m_rep) return;
    auto* elements = steps->m_rep->elements;
    for (int i = 1; i < steps->m_current_size; ++i)
    {
        auto* key = elements[i];
        const auto* step = impl_ptr<subtick_move_step>(key);
        if (!step) continue;
        int j = i - 1;
        while (j >= 0)
        {
            const auto* other = impl_ptr<subtick_move_step>(elements[j]);
            if (!other || other->when() <= step->when()) break;
            elements[j + 1] = elements[j];
            --j;
        }
        elements[j + 1] = key;
    }
}
}
