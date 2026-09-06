#include "../core/features/movement/movement_math.hpp"
#include "../core/features/combat/angle_math.hpp"
#include "../utilities/proto/subtick_ops.hpp"
#include <array>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <tuple>
#include <vector>

namespace m = features::movement::math2d;
namespace ops = proto::subtick_ops;
constexpr std::uint64_t jump = 2, duck = 4, use = 32;
struct raw_step { std::byte header[16]{}; proto::subtick_move_step step{}; };
static_assert(offsetof(raw_step, step) == proto::message_impl_offset);
struct pool
{
    using field_type = proto::repeated_ptr_field<proto::subtick_move_step>;
    static constexpr int capacity = 16;
    std::array<raw_step, capacity> objects{};
    std::unique_ptr<std::byte[]> allocation{new std::byte[offsetof(field_type::rep_t, elements) + capacity * sizeof(void*)]{}};
    field_type field{};
    pool()
    {
        field.m_rep = reinterpret_cast<field_type::rep_t*>(allocation.get());
        field.m_rep->allocated_size = capacity;
        field.m_total_size = capacity;
        for (int i=0;i<capacity;++i) field.m_rep->elements[i] = &objects[i];
    }
    static proto::subtick_move_step* acquire(field_type* f)
    {
        auto* step = f->add();
        if (step) *step = {};
        return step;
    }
    void append(std::uint64_t button, bool down, float when)
    {
        auto* step=acquire(&field); assert(step);
        step->set_button(button); step->set_pressed(down); step->set_when(when);
    }
    void assert_unique() const
    {
        std::set<void*> pointers;
        for (int i=0;i<capacity;++i) assert(pointers.insert(field.m_rep->elements[i]).second);
    }
};
using event = std::tuple<std::uint64_t,bool,float,float,float,float,float>;
std::vector<event> snapshot(pool& p)
{
    std::vector<event> result;
    for (int i=0;i<p.field.size();++i)
    {
        const auto* s=p.field.mutable_at(i);
        result.emplace_back(s->button(),s->pressed(),s->when(),s->analog_forward_delta(),
            s->analog_left_delta(),s->pitch_delta(),s->yaw_delta());
    }
    return result;
}
void test_subticks()
{
    pool p;
    p.append(duck,true,.4f); p.append(jump,true,.2f); p.append(use,true,.1f);
    p.append(jump|duck,false,.7f);
    p.field.mutable_at(3)->set_analog_forward_delta(.25f);
    auto rewrite=[&](std::optional<float> when){return ops::replace_button_events(&p.field,jump,when,pool::acquire);};
    assert(rewrite(.8f)); ops::stable_sort(&p.field); p.assert_unique();
    const auto first=snapshot(p);
    assert(first.size()==5);
    assert(std::get<0>(first[0])==jump && !std::get<1>(first[0]));
    assert(std::get<0>(first[1])==use && std::get<2>(first[1])==.1f);
    assert(std::get<0>(first[3])==duck && std::get<3>(first[3])==.25f);
    for(int i=0;i<1000;++i)
    {
        assert(rewrite(.8f));ops::stable_sort(&p.field);
        assert(snapshot(p)==first);p.assert_unique();
    }
    assert(rewrite(std::nullopt));ops::stable_sort(&p.field);
    auto released=snapshot(p);assert(released.size()==4);
    for(const auto& e:released) assert(!(std::get<0>(e)&jump) || !std::get<1>(e));
    assert(rewrite(std::nullopt));ops::stable_sort(&p.field);assert(snapshot(p)==released);
    // Roll back the active size on a failed second allocation without touching native events.
    const auto saved=snapshot(p);int calls=0;
    auto fail_second=[&](auto* field)->proto::subtick_move_step* {
        return ++calls==2 ? nullptr : pool::acquire(field);
    };
    assert(!ops::replace_button_events(&p.field,jump,.8f,fail_second));
    assert(snapshot(p)==saved);p.assert_unique();
    assert(rewrite(.8f));ops::stable_sort(&p.field);assert(snapshot(p)==first);p.assert_unique();
    for(float invalid:{-1.f,0.f,1.f,std::numeric_limits<float>::quiet_NaN()})
    {assert(!rewrite(invalid));assert(snapshot(p)==first);}
    // Same-time release/press ordering must be stable.
    pool equal;equal.append(jump,false,0.f);equal.append(jump,true,0.f);
    ops::stable_sort(&equal.field);assert(!equal.field.mutable_at(0)->pressed());
    assert(equal.field.mutable_at(1)->pressed());
    // Removing everything preserves every allocated object exactly once in the reusable tail.
    ops::erase_if(&p.field,[](auto*){return true;});assert(p.field.size()==0);p.assert_unique();
    for(int i=0;i<pool::capacity;++i)assert(pool::acquire(&p.field));
    assert(!pool::acquire(&p.field));p.assert_unique();
}
void test_spin()
{
    features::combat::angle_math::spin_clock clock;
    assert(clock.update(17,100,1,360,1.f/64)==17);
    const float first=clock.update(17,101,1,360,1.f/64);
    assert(first==22.625f);assert(clock.update(17,101,1,360,1.f/64)==first);
    assert(clock.update(17,201,1,360,1.f/64)==28.25f);
    assert(clock.update(32,202,2,360,1.f/64)==32); // pawn switch
    assert(clock.update(-20,1,2,360,1.f/64)==-20); // command restart
    assert(clock.update(90,10000,2,360,1.f/64)==90); // stream discontinuity
    clock.reset();assert(clock.update(10,10001,2,360,1.f/64)==10);
}
struct result { int switches{}; float speed{},max_heading{}; };
result simulate(bool quantized, bool aligned, float spin, float slack)
{
    m::vector v{350,0},previous{};int last=0;result r;
    for(int i=0;i<512;++i)
    {
        float yaw=m::yaw(i*spin/64);
        auto command=m::choose_movement(v,0,yaw,1.f/64,250,12,30,1,quantized&&!aligned,false,previous,slack);
        if(aligned)
        {
            const auto frame=m::realize_quantized(m::world(command,yaw),yaw);
            assert(std::fabs(m::yaw(frame.view_yaw-yaw))<=22.5001f);
            command=frame.movement;yaw=frame.view_yaw;
        }
        const auto wish=m::world(command,yaw);
        const float cross=v.x*wish.y-v.y*wish.x;
        const int side=cross>.001f?1:cross<-.001f?-1:0;
        if(side&&last&&side!=last)++r.switches;
        if(side)last=side;
        v=m::air_step(v,wish,1.f/64,250,12,30,1);previous=wish;
        r.max_heading=std::max(r.max_heading,std::fabs(std::atan2(v.y,v.x)/m::radians));
    }
    r.speed=m::length(v);return r;
}
void test_strafe()
{
    for(int reference=-180;reference<=180;reference+=5)
        for(int world=-180;world<=180;++world)
        {
            const auto wish=m::direction(float(world));
            const auto frame=m::realize_quantized(wish,float(reference));
            auto actual=m::world(frame.movement,frame.view_yaw);const float n=m::length(actual);
            assert(std::fabs(actual.x/n-wish.x)<.00001f && std::fabs(actual.y/n-wish.y)<.00001f);
            assert(std::fabs(m::yaw(frame.view_yaw-reference))<=22.5001f);
            for(float axis:{frame.movement.x,frame.movement.y})assert(axis==-1||axis==0||axis==1);
        }
    auto neutral=m::choose_movement({},0,0,1.f/64,250,12,30,1,true,true);
    assert(neutral.x==0 && neutral.y==0);
    const auto legacy=simulate(false,false,0,0),smooth=simulate(false,false,0,4);
    assert(smooth.switches<legacy.switches && std::fabs(smooth.speed-legacy.speed)<.1f);
    const auto fixed=simulate(true,false,0,0),aligned=simulate(true,true,0,4);
    assert(fixed.speed<352 && aligned.speed>700); // model-only, no server speed limits
    const auto spinning=simulate(true,false,360,4),old_spin=simulate(true,false,360,0);
    assert(spinning.switches<old_spin.switches);
    std::cout<<"MODEL 512 airborne ticks, no server speed/landing clamps:\n"
        <<"  analog switches: "<<legacy.switches<<" -> "<<smooth.switches<<"\n"
        <<"  quantized fixed/aligned speed: "<<fixed.speed<<" / "<<aligned.speed<<"\n"
        <<"  quantized spin switches: "<<old_spin.switches<<" -> "<<spinning.switches<<"\n";
}
int main(){test_subticks();test_spin();test_strafe();std::cout<<"command_regression_tests: PASS\n";}
