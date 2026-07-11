#pragma once

#include "platform/platform.hpp"
#include "skyland/scene.hpp"
#include "skyland/entity.hpp"
#include "skyland/skyland.hpp"



namespace skyland
{



namespace
{



#define INT16_BITS (8 * sizeof(int16_t))
#ifndef INT16_MAX
#define INT16_MAX ((1 << (INT16_BITS - 1)) - 1)
#endif

#define TABLE_BITS (5)
#define TABLE_SIZE (1 << TABLE_BITS)
#define TABLE_MASK (TABLE_SIZE - 1)

#define LOOKUP_BITS (TABLE_BITS + 2)
#define LOOKUP_MASK ((1 << LOOKUP_BITS) - 1)
#define FLIP_BIT (1 << TABLE_BITS)
#define NEGATE_BIT (1 << (TABLE_BITS + 1))
#define INTERP_BITS (INT16_BITS - 1 - LOOKUP_BITS)
#define INTERP_MASK ((1 << INTERP_BITS) - 1)


static constexpr const int16_t sin90[TABLE_SIZE + 1] = {
    0x0000, 0x0647, 0x0c8b, 0x12c7, 0x18f8, 0x1f19, 0x2527, 0x2b1e, 0x30fb,
    0x36b9, 0x3c56, 0x41cd, 0x471c, 0x4c3f, 0x5133, 0x55f4, 0x5a81, 0x5ed6,
    0x62f1, 0x66ce, 0x6a6c, 0x6dc9, 0x70e1, 0x73b5, 0x7640, 0x7883, 0x7a7c,
    0x7c29, 0x7d89, 0x7e9c, 0x7f61, 0x7fd7, 0x7fff};



constexpr inline s16 sine(s16 angle)
{
    s16 v0 = 0;
    s16 v1 = 0;
    if (angle < 0) {
        angle += INT16_MAX;
        angle += 1;
    }
    v0 = (angle >> INTERP_BITS);
    if (v0 & FLIP_BIT) {
        v0 = ~v0;
        v1 = ~angle;
    } else {
        v1 = angle;
    }
    v0 &= TABLE_MASK;
    v1 = sin90[v0] +
         (s16)(((int32_t)(sin90[v0 + 1] - sin90[v0]) * (v1 & INTERP_MASK)) >>
               INTERP_BITS);
    if ((angle >> INTERP_BITS) & NEGATE_BIT)
        v1 = -v1;
    return v1;
}


constexpr inline s16 cosine(s16 angle)
{
    if (angle < 0) {
        angle += INT16_MAX;
        angle += 1;
    }
    return sine(angle - s16((270.f / 360.f) * INT16_MAX));
}



namespace detail
{
template <std::size_t... Is> struct seq
{
};
template <std::size_t N, std::size_t... Is>
struct gen_seq : gen_seq<N - 1, N - 1, Is...>
{
};
template <std::size_t... Is> struct gen_seq<0, Is...> : seq<Is...>
{
};



constexpr inline Vec2<Float> rotv(const Vec2<Float>& input, Float angle)
{
    const s16 converted_angle = INT16_MAX * (angle / 360.f);
    const Float cos_theta = Float(cosine(converted_angle)) / INT16_MAX;
    const Float sin_theta = Float(sine(converted_angle)) / INT16_MAX;

    return {input.x * cos_theta - input.y * sin_theta,
            input.x * sin_theta + input.y * cos_theta};
}



template <class Generator, std::size_t... Is>
constexpr auto generate_array_helper(Generator g, seq<Is...>)
    -> std::array<decltype(g(std::size_t{}, sizeof...(Is))), sizeof...(Is)>
{
    return {{g(Is, sizeof...(Is))...}};
}



template <std::size_t tcount, class Generator>
constexpr auto generate_array(Generator g)
    -> decltype(generate_array_helper(g, gen_seq<tcount>{}))
{
    return generate_array_helper(g, gen_seq<tcount>{});
}
} // namespace detail



inline constexpr auto make_rotation_lut(float v)
{
    return detail::generate_array<360>(
        [](std::size_t curr, std::size_t total) -> Vec2<Fixnum> {
            auto off = detail::rotv({1.f, 0}, curr);
            return Vec2<Fixnum>{Fixnum(off.x), Fixnum(off.y)};
        });
}

}



static constexpr const auto rotation_lut = make_rotation_lut(0.f);



class SailingSimulatorScene : public Scene
{
public:


    class WakeRipple : public Entity
    {
    public:
        WakeRipple(Vec2<Fixnum> pos) : Entity({})
        {
            sprite_.set_size(Sprite::Size::w8_h8);
            sprite_.set_tidx_8x8(30, 0);
            sprite_.set_position(pos);
            sprite_.set_origin({});
        }


        void update(Time delta) override
        {
            // The game manipulates the time delta for slow motion stuff, etc. But
            // we always want this UI effect to play at the same rate.
            delta = PLATFORM.delta_clock().last_delta();

            timer_ += delta / 2;
            if (timer_ >= milliseconds(80)) {
                timer_ -= milliseconds(80);
                auto t = sprite_.get_texture_index();
                if (t == 30 * 8 + 5) {
                    kill();
                    return;
                }

                ++t;
                sprite_.set_texture_index(t);
            }
        }


        void rewind(Time delta) override
        {
            kill();
        }


        Sprite& sprite()
        {
            return sprite_;
        }

    private:
        Time timer_ = 0;
    };



    static void make_wake_effect(Vec2<Fixnum> pos)
    {
        auto segment = [&](Fixnum xoff, Fixnum yoff, bool xflip, bool yflip) {
            auto p = pos;
            p.x += xoff;
            p.y += yoff;
            if (auto e = APP.alloc_entity<WakeRipple>(p)) {
                e->sprite().set_flip({xflip, yflip});
                APP.effects().push(std::move(e));
            }
        };

        segment(Fixnum::from_integer(-4), Fixnum::from_integer(-4), false, false);
    }




    using Wind = Fixnum; // angle out of 360


    class Boat
    {
    private:
        Vec2<Fixnum> position_;
        Fixnum heel_;
        Fixnum rotation_;
        Fixnum sail_force_;
        u16 hardware_rotation_ = 0;
        u8 wake_timer_;

    public:


        int relative_wind_angle(Fixnum wind_from_deg) const
        {
            int diff = rotation_.as_integer() - wind_from_deg.as_integer();
            diff %= 360;
            if (diff < 0)   diff += 360;
            if (diff > 180) diff = 360 - diff;   // fold: port and starboard are symmetric
            return diff;
        }


        Fixnum sail_efficiency(Fixnum wind_from_deg) const
        {
            const int rel = relative_wind_angle(wind_from_deg);

            const int no_go = 40;
            if (rel < no_go) {
                return 0.0_fixed;             // sails luffing, no drive
            }

            // sin(rel): ~0.71 close-hauled, 1.0 on the beam, back toward 0 at a run.
            Fixnum hump = rotation_lut[rel].y;

            if (rel > 90) {
                // A boat still runs downwind, just slower than it reaches -- don't let
                // a dead run collapse to zero.
                const Fixnum downwind_floor = 0.85_fixed;
                if (hump < downwind_floor) hump = downwind_floor;
            }

            return hump;
        }


        const char* point_of_sail(Fixnum wind_from_deg) const
        {
            const int rel = relative_wind_angle(wind_from_deg);

            if (rel < 40)  return "in irons";
            if (rel < 60)  return "close hauled";
            if (rel < 80)  return "close reach";
            if (rel < 100) return "beam reach";
            if (rel < 160) return "broad reach";
            return "running";
        }


        const Vec2<Fixnum>& get_position()
        {
            return position_;
        }


        Boat()
        {
            position_ = {120.0_fixed, 80.0_fixed};
            sail_force_ = 1.0_fixed;
        }


        void update(const Wind& wind)
        {
            if (PLATFORM.input().pressed<Button::left>()) {
                rotation_ -= 2.0_fixed;
                if (rotation_ < 0.0_fixed) {
                    rotation_ = 359.0_fixed;
                }
            } else if (PLATFORM.input().pressed<Button::right>()) {
                rotation_ += 2.0_fixed;
                if (rotation_ > 359.0_fixed) {
                    rotation_ = 0.0_fixed;
                }
            }

            hardware_rotation_ = -1 * ((rotation_ * 0.002777_fixed) * Fixnum::from_integer(65535 / 2)).as_integer() + 65535 / 8;


            static const auto wind_strength = 1.5_fixed;
            Fixnum target = sail_efficiency(wind) * wind_strength;

            // Smaller K == heavier boat / more inertia. At ~60 fps, 0.03 gives a boat
            // that takes roughly a second to spin up or coast down.
            const Fixnum K = 0.01_fixed;
            sail_force_ += (target - sail_force_) * K;

            position_.x += sail_force_ * rotation_lut[rotation_.as_integer()].x;
            position_.y += sail_force_ * rotation_lut[rotation_.as_integer()].y;

            // if (heel_ < 0.75_fixed) {
            //     heel_ = heel_ + 0.01_fixed;
            // } else {
            //     heel_ = 0.0_fixed;
            // }

            wake_timer_++;
            if (wake_timer_ > 5) {
                wake_timer_ = 0;
                make_wake_effect(rng::sample<3>(get_position(),
                                                rng::utility_state));
            }
        }


        void display()
        {
            draw_mast();

            for (int i = 4; i > -1; --i) {
                draw_slice(i);
            }
        }

        void draw_slice(int n)
        {
            Sprite spr;
            spr.set_size(Sprite::Size::w16_h32);
            spr.set_texture_index(12 + n);
            auto pos = position_;
            pos.x -= 8.0_fixed;
            pos.y -= 16.0_fixed;
            pos.x -= heel_ * Fixnum::from_integer(n);
            pos.y -= Fixnum::from_integer(n * 2);
            spr.set_position(pos);
            spr.set_rotation(hardware_rotation_);
            PLATFORM.screen().draw(spr);
        }

        void draw_mast()
        {
            Sprite spr;
            spr.set_size(Sprite::Size::w16_h32);
            spr.set_texture_index(17);
            auto pos = position_;
            pos.x -= 8.0_fixed;
            pos.y -= 16.0_fixed;
            pos.y -= 24.0_fixed;
            pos.x += 10.0_fixed * rotation_lut[rotation_.as_integer()].x;
            pos.y += 10.0_fixed * rotation_lut[rotation_.as_integer()].y;
            spr.set_rotation((Fixnum::from_integer(2100) * heel_).as_integer());
            spr.set_position(pos);
            PLATFORM.screen().draw(spr);
        }
    };


    void enter(Scene& prev) override
    {
        PLATFORM.load_sprite_texture("spritesheet_sailboat");
        PLATFORM.screen().schedule_fade(0);
        PLATFORM.fill_overlay(0);
        PLATFORM.clear_layer(Layer::map_0);
        PLATFORM.clear_layer(Layer::map_1);
        globals().entity_pools_.create("entity-mem");
    }


    void exit(Scene& next) override
    {

    }


    ScenePtr update(Time delta) override
    {
        static const auto north_wind = 270.0_fixed;
        auto wind = north_wind;

        update_entities(milliseconds(17), APP.effects());

        boat_.update(wind);
        auto view = PLATFORM.screen().get_view();
        auto center = boat_.get_position();
        center.x -= 120.0_fixed;
        center.y -= 80.0_fixed;
        view.set_center(fvec(center));
        PLATFORM.screen().set_view(view);

        for (int x = 0; x < 30; ++x) {
            PLATFORM.set_tile(Layer::overlay, x, 19, 0);
        }
        Text::print(boat_.point_of_sail(wind), {0, 19});

        return null_scene();
    }


    void display() override
    {
        boat_.display();

        for (auto& effect : APP.effects()) {
            PLATFORM.screen().draw(effect->sprite());
        }

    }


private:
    Boat boat_;
};



}
