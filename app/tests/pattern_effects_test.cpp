// Spatial mappings, clocks and the pattern effects (chase, wave).

#include "engine/effect_spec.h"
#include "engine/rig.h"
#include "engine/scene.h"
#include "engine/space.h"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using namespace psyight;

namespace {

// One straight strip of 100 pixels along x, 1 m long.
Rig line_rig() {
    StripSpec s;
    s.name = "line";
    s.polyline = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}};
    s.pixels_per_m = 100.0;
    s.count = 100;
    return build_rig({s});
}

float level(const Rgb &c) { return std::max({c.r, c.g, c.b}); }

Frame render(const Effect &effect, const Rig &rig, const EngineInputs &in) {
    const Palette palette{"test", {{1, 0, 0}, {0, 0, 1}}};
    Frame out(rig.pixels.size());
    effect.render({rig, in, palette, 7}, out);
    return out;
}

} // namespace

TEST_CASE("space: mappings span 0 to 1; wings mirror; option names") {
    const Rig rig = default_rig();
    for (int m = 0; m < kMappingCount; ++m) {
        CAPTURE(m);
        const std::vector<double> u = map_pixels(rig, static_cast<Mapping>(m), 3);
        REQUIRE(u.size() == rig.pixels.size());
        for (const double v : u) {
            CHECK(v >= 0.0);
            CHECK(v <= 1.0);
        }
    }
    const Rig line = line_rig();
    const std::vector<double> along = map_pixels(line, Mapping::along_strip, 0);
    CHECK(along.front() == doctest::Approx(0.005));
    CHECK(along.back() == doctest::Approx(0.995));
    const std::vector<double> radial = map_pixels(line, Mapping::radial, 0);
    CHECK(radial[50] < 0.02); // The middle of the line.
    CHECK(radial[0] > 0.98);
    CHECK(fold_wings(0.25, 1) == doctest::Approx(0.25));
    CHECK(fold_wings(0.25, 2) == doctest::Approx(0.5));
    CHECK(fold_wings(0.75, 2) == doctest::Approx(0.5));
    CHECK(fold_wings(1.0, 2) == doctest::Approx(0.0));
    CHECK(fold_wings(0.5, 3) == doctest::Approx(0.5));

    const std::vector<EffectParamInfo> infos = effect_param_infos(EffectType::wave);
    REQUIRE(infos[0].options != nullptr);
    CHECK(effect_option_name(infos[0], 0) == "Along strip");
    CHECK(effect_option_name(infos[0], 5) == "Random");
    CHECK(effect_option_name(infos[0], 6).empty());
    CHECK(effect_option_name(infos[3], 0).empty()); // Rate: no names.
}

TEST_CASE("space: clocks count free seconds, grid divisions, notes and kicks") {
    EngineInputs in;
    in.t_s = 10.0;
    in.beat_index = 3;
    in.beat_phase = 0.5;
    in.bar_index = 2;
    in.bar_phase = 0.25;
    in.phrase_index = 1;
    in.phrase_phase = 0.5;
    in.bass_notes_so_far = 5;
    in.since_bass_onset_s = 0.02; // Half way through the 40 ms glide.
    in.kicks_so_far = 0;
    CHECK(clock_cycles(in, Clock::free, 0.5) == doctest::Approx(5.0));
    CHECK(clock_cycles(in, Clock::sixteenth, 1.0) == doctest::Approx(14.0));
    CHECK(clock_cycles(in, Clock::beat, 0.5) == doctest::Approx(1.75));
    CHECK(clock_cycles(in, Clock::bar, 1.0) == doctest::Approx(2.25));
    CHECK(clock_cycles(in, Clock::phrase, 2.0) == doctest::Approx(3.0));
    CHECK(clock_cycles(in, Clock::bass_note, 1.0) == doctest::Approx(4.5));
    CHECK(clock_cycles(in, Clock::kick, 1.0) == doctest::Approx(0.0));
}

TEST_CASE("chase: a comet at the clock's position with its tail behind; bounce turns back") {
    const Rig rig = line_rig();
    ChaseParams p;
    p.mapping = static_cast<int>(Mapping::along_strip);
    p.clock = static_cast<int>(Clock::free);
    p.rate = 0.5;
    p.count = 1;
    p.tail = 0.2;
    EngineInputs in;
    in.t_s = 1.0; // Head at 0.5.
    Frame f = render(*make_chase(p), rig, in);
    CHECK(level(f[49]) > 0.9f); // Just behind the head.
    CHECK(level(f[40]) > 0.1f); // In the tail.
    CHECK(level(f[40]) < level(f[49]));
    CHECK(level(f[55]) == 0.0f); // Ahead.
    CHECK(level(f[25]) == 0.0f); // Beyond the tail.
    // Bouncing: at 0.75 of a cycle the head runs back through 0.5, its tail now on the far side.
    p.bounce = 1;
    in.t_s = 1.5;
    f = render(*make_chase(p), rig, in);
    CHECK(level(f[50]) > 0.9f);
    CHECK(level(f[60]) > 0.1f);
    CHECK(level(f[45]) == 0.0f);
    // Three comets: three lit stretches.
    p.bounce = 0;
    p.count = 3;
    p.tail = 0.1;
    in.t_s = 0.0;
    f = render(*make_chase(p), rig, in);
    int starts = 0;
    for (size_t i = 0; i < f.size(); ++i) {
        const bool lit = level(f[i]) > 0.0f;
        const bool before = level(f[(i + f.size() - 1) % f.size()]) > 0.0f;
        starts += lit && !before;
    }
    CHECK(starts == 3);
}

TEST_CASE("wave: a square wave across x moves with the bar clock; wings mirror it") {
    const Rig rig = line_rig();
    WaveParams p;
    p.mapping = static_cast<int>(Mapping::across_x);
    p.clock = static_cast<int>(Clock::bar);
    p.rate = 1.0;
    p.shape = 3; // Square.
    p.width = 0.5;
    p.spread = 1.0;
    p.level = 1.0;
    p.colour_spread = 0.0; // Pure red.
    EngineInputs in;
    in.bar_index = 0;
    in.bar_phase = 0.0;
    Frame f = render(*make_wave(p), rig, in);
    CHECK(level(f[20]) == doctest::Approx(1.0f));
    CHECK(level(f[80]) == 0.0f);
    in.bar_phase = 0.25; // A quarter bar later the lit half has moved a quarter to the right.
    f = render(*make_wave(p), rig, in);
    CHECK(level(f[20]) == 0.0f);
    CHECK(level(f[40]) == doctest::Approx(1.0f));
    CHECK(level(f[70]) == doctest::Approx(1.0f));
    p.wings = 2; // Mirrored about the middle.
    in.bar_phase = 0.1;
    f = render(*make_wave(p), rig, in);
    for (int i = 0; i < 50; ++i) {
        CAPTURE(i);
        CHECK(level(f[static_cast<size_t>(i)]) == level(f[static_cast<size_t>(99 - i)]));
    }
}

TEST_CASE("pattern effects: deterministic, and every setting round trips") {
    const Rig rig = default_rig();
    EngineInputs in;
    in.t_s = 3.21;
    in.beat_index = 8;
    in.beat_phase = 0.3;
    in.bar_index = 2;
    in.bar_phase = 0.4;
    for (const EffectType type : {EffectType::chase, EffectType::wave}) {
        CAPTURE(effect_type_id(type));
        EffectSpec spec = default_spec(type);
        const std::vector<EffectParamInfo> infos = effect_param_infos(type);
        for (size_t k = 0; k < infos.size(); ++k) {
            set_effect_param(spec, k, infos[k].max);
            CHECK(effect_param(spec, k) == doctest::Approx(infos[k].max));
        }
        const std::shared_ptr<const Effect> effect = make_effect(default_spec(type));
        const Frame a = render(*effect, rig, in);
        const Frame b = render(*make_effect(default_spec(type)), rig, in);
        float lit = 0.0f;
        for (size_t i = 0; i < a.size(); ++i) {
            CHECK(a[i].r == b[i].r);
            CHECK(a[i].b == b[i].b);
            lit = std::max(lit, level(a[i]));
        }
        CHECK(lit > 0.0f);
        CHECK(effect_type_from_id(effect_type_id(type)) == type);
    }
}
