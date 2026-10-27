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
    CHECK(effect_option_name(infos[0], 6) == "Hex rings");
    CHECK(effect_option_name(infos[0], 8).empty());
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
    in.beat_s = 0.4;
    in.section = "drop"; // The strobe's default trigger, just after it.
    in.since_section_s = 0.01;
    in.kicks_so_far = 5; // The bursts' default trigger.
    in.since_kick_s = 0.05;
    for (const EffectType type :
         {EffectType::chase, EffectType::wave, EffectType::strobe, EffectType::stutter,
          EffectType::noise, EffectType::fire, EffectType::bursts, EffectType::spiral}) {
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

TEST_CASE("value noise: bounded, deterministic, smooth, and varied") {
    double lo = 1.0;
    double hi = 0.0;
    for (int i = 0; i < 400; ++i) {
        const double x = -3.7 + 0.031 * i;
        const double n = value_noise(x, 0.4 * x, 1.3, 9);
        CHECK(n >= 0.0);
        CHECK(n <= 1.0);
        CHECK(n == value_noise(x, 0.4 * x, 1.3, 9));
        CHECK(std::fabs(value_noise(x + 1e-3, 0.4 * x, 1.3, 9) - n) < 0.01);
        lo = std::min(lo, n);
        hi = std::max(hi, n);
    }
    CHECK(hi - lo > 0.4);
    CHECK(value_noise(0.5, 0.5, 0.5, 1) != value_noise(0.5, 0.5, 0.5, 2));
}

TEST_CASE("strobe: rolls on 16ths after the drop starts and in a build's last beat") {
    const Rig rig = line_rig();
    const std::shared_ptr<const Effect> drop = make_strobe({});
    EngineInputs in;
    in.beat_s = 0.4; // 16ths of 0.1 s.
    in.section = "drop";
    in.since_section_s = 0.0;
    CHECK(level(render(*drop, rig, in)[0]) == doctest::Approx(1.0f));
    in.since_section_s = 0.1; // The second flash.
    CHECK(level(render(*drop, rig, in)[0]) == doctest::Approx(1.0f));
    in.since_section_s = 0.15;
    CHECK(level(render(*drop, rig, in)[0]) < 0.4f);
    in.since_section_s = 0.2; // Two flashes only.
    CHECK(level(render(*drop, rig, in)[0]) == 0.0f);
    in.section = "groove";
    in.since_section_s = 0.0;
    CHECK(level(render(*drop, rig, in)[0]) == 0.0f);

    StrobeParams roll;
    roll.trigger = 1; // Build end.
    roll.flashes = 4;
    const std::shared_ptr<const Effect> build = make_strobe(roll);
    in.section = "build";
    in.section_length_s = 10.0;
    in.since_section_s = 9.0;
    CHECK(level(render(*build, rig, in)[0]) == 0.0f);
    in.since_section_s = 9.9; // The last beat, its fourth 16th.
    CHECK(level(render(*build, rig, in)[0]) == doctest::Approx(1.0f));

    StrobeParams bars;
    bars.trigger = 4;
    bars.every = 2;
    bars.flashes = 1;
    in.section = "groove";
    in.bar_index = 3;
    in.bar_phase = 0.0;
    CHECK(level(render(*make_strobe(bars), rig, in)[0]) == 0.0f); // Odd bar.
    in.bar_index = 4;
    CHECK(level(render(*make_strobe(bars), rig, in)[0]) == doctest::Approx(1.0f));
}

TEST_CASE("stutter: gates on the 16th grid, with the bass gate, and ducks after kicks") {
    const Rig rig = line_rig();
    CHECK(default_blend(EffectType::stutter) == BlendMode::multiply);
    CHECK(default_blend(EffectType::wave) == BlendMode::add);
    StutterParams p;
    p.release_s = 0.001;
    EngineInputs in;
    in.beat_s = 0.4;
    in.beat_phase = 0.1; // 16th position 0.4: open.
    CHECK(level(render(*make_stutter(p), rig, in)[0]) == doctest::Approx(1.0f));
    in.beat_phase = 0.2; // 16th position 0.8: closed.
    CHECK(level(render(*make_stutter(p), rig, in)[0]) < 0.01f);
    p.depth = 0.5;
    CHECK(level(render(*make_stutter(p), rig, in)[0]) == doctest::Approx(0.5f).epsilon(0.02));

    StutterParams gate;
    gate.mode = 1;
    in.bass_active = true;
    CHECK(level(render(*make_stutter(gate), rig, in)[0]) == doctest::Approx(1.0f));
    in.bass_active = false;
    in.since_bass_offset_s = 0.2;
    CHECK(level(render(*make_stutter(gate), rig, in)[0]) < 0.01f);

    StutterParams duck;
    duck.mode = 2;
    in.since_kick_s = 0.0;
    CHECK(level(render(*make_stutter(duck), rig, in)[0]) == 0.0f);
    in.since_kick_s = 0.3; // Past half a beat: open again.
    CHECK(level(render(*make_stutter(duck), rig, in)[0]) == doctest::Approx(1.0f));
    StutterParams gap;
    gap.mode = 3; // Pre-drop gap: the build's last beat.
    gap.release_s = 0.005;
    in.section = "build";
    in.section_length_s = 10.0;
    in.since_section_s = 9.5;
    CHECK(level(render(*make_stutter(gap), rig, in)[0]) == doctest::Approx(1.0f));
    in.since_section_s = 9.7;
    CHECK(level(render(*make_stutter(gap), rig, in)[0]) < 0.01f);
    in.section = "drop";
    CHECK(level(render(*make_stutter(gap), rig, in)[0]) == doctest::Approx(1.0f));
}

TEST_CASE("noise: a varied field that drifts smoothly") {
    const Rig rig = default_rig();
    const std::shared_ptr<const Effect> noise = make_noise({});
    EngineInputs in;
    in.t_s = 12.0;
    const Frame a = render(*noise, rig, in);
    in.t_s = 12.016;
    const Frame b = render(*noise, rig, in);
    float lo = 1.0f;
    float hi = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        lo = std::min(lo, level(a[i]));
        hi = std::max(hi, level(a[i]));
        CHECK(std::fabs(level(a[i]) - level(b[i])) < 0.02f);
    }
    CHECK(hi - lo > 0.1f);
}

TEST_CASE("fire: brighter at the base, and higher with the riser") {
    const Rig rig = default_rig(); // A frame: bottom strip at the lowest y.
    const std::shared_ptr<const Effect> fire = make_fire({});
    const auto lit_above = [&](double riser, double from_y) {
        EngineInputs in;
        in.t_s = 5.0;
        in.riser = riser;
        const Frame f = render(*fire, rig, in);
        int lit = 0;
        for (size_t i = 0; i < f.size(); ++i) {
            lit += rig.pixels[i].position.y >= from_y && level(f[i]) > 0.02f;
        }
        return lit;
    };
    const double mid_y = 0.5 * (rig.min.y + rig.max.y);
    const double low_y = rig.min.y;
    CHECK(lit_above(0.0, low_y) > lit_above(0.0, mid_y));
    CHECK(lit_above(1.0, mid_y) > lit_above(0.0, mid_y));
}

TEST_CASE("bursts: rings grow from a seeded point, fade, and overlap on the grid") {
    const Rig rig = default_rig();
    BurstsParams p;
    p.trigger = 2; // Beats.
    p.keep = 1;
    const auto lit = [&](const BurstsParams &params, double beat_phase, long long beat) {
        EngineInputs in;
        in.beat_s = 0.4;
        in.beat_index = beat;
        in.beat_phase = beat_phase;
        const Frame f = render(*make_bursts(params), rig, in);
        double sum = 0.0;
        int count = 0;
        for (const Rgb &c : f) {
            sum += level(c);
            count += level(c) > 0.05f;
        }
        return std::pair{sum, count};
    };
    // Fading: less light late in the beat than early, all from one burst.
    CHECK(lit(p, 0.9, 10).first < lit(p, 0.2, 10).first);
    // Growing: more pixels on the ring as it widens (before it leaves the rig).
    CHECK(lit(p, 0.25, 10).second > lit(p, 0.02, 10).second);
    // Overlap: earlier beats' bursts add light.
    BurstsParams overlap = p;
    overlap.keep = 3;
    CHECK(lit(overlap, 0.2, 10).first > lit(p, 0.2, 10).first);
    // Sparks darken sectors.
    BurstsParams sparks = p;
    sparks.sparks = 0.6;
    CHECK(lit(sparks, 0.2, 10).first < lit(p, 0.2, 10).first);
    // Different beats start from different points.
    EngineInputs a;
    a.beat_s = 0.4;
    a.beat_index = 10;
    a.beat_phase = 0.05;
    EngineInputs b = a;
    b.beat_index = 11;
    const Frame fa = render(*make_bursts(p), rig, a);
    const Frame fb = render(*make_bursts(p), rig, b);
    bool differs = false;
    for (size_t i = 0; i < fa.size(); ++i) {
        differs = differs || level(fa[i]) != level(fb[i]);
    }
    CHECK(differs);
}

TEST_CASE("spiral: the arms turn with the clock and repeat after one arm's spacing") {
    const Rig rig = default_rig();
    SpiralParams p;
    p.clock = static_cast<int>(Clock::free);
    p.rate = 0.5;
    p.arms = 4;
    EngineInputs in;
    in.t_s = 1.0;
    const Frame a = render(*make_spiral(p), rig, in);
    in.t_s = 1.0 + 1.0 / (p.arms * p.rate); // A quarter turn later.
    const Frame b = render(*make_spiral(p), rig, in);
    in.t_s = 1.2;
    const Frame moved = render(*make_spiral(p), rig, in);
    double same = 0.0;
    double changed = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        same = std::max(same, static_cast<double>(std::fabs(level(a[i]) - level(b[i]))));
        changed += std::fabs(level(a[i]) - level(moved[i]));
    }
    CHECK(same < 1e-3);
    CHECK(changed > 1.0);
}

TEST_CASE("bass bars: the marker sits at the note's pitch; dark without a note") {
    const Rig rig = line_rig();
    BassBarsParams p;
    p.mapping = static_cast<int>(Mapping::across_x);
    p.low_midi = 24;
    p.high_midi = 48;
    EngineInputs in;
    CHECK(level(render(*make_bass_bars(p), rig, in)[50]) == 0.0f); // No note yet.
    in.bass_notes_so_far = 1;
    in.bass_active = true;
    in.bass_velocity = 1.0;
    in.since_bass_onset_s = 0.0;
    in.bass_midi = 36; // Half way.
    Frame f = render(*make_bass_bars(p), rig, in);
    CHECK(level(f[50]) > 0.9f);
    CHECK(level(f[10]) == 0.0f);
    CHECK(level(f[90]) == 0.0f);
    in.bass_midi = 24;
    f = render(*make_bass_bars(p), rig, in);
    CHECK(level(f[0]) > 0.9f);
    CHECK(level(f[50]) == 0.0f);
    p.mode = 1; // Bar: lit up to the pitch.
    in.bass_midi = 36;
    f = render(*make_bass_bars(p), rig, in);
    CHECK(level(f[10]) > 0.9f);
    CHECK(level(f[45]) > 0.9f);
    CHECK(level(f[80]) < 0.01f);
    in.bass_active = false; // Released.
    in.since_bass_offset_s = 1.0;
    f = render(*make_bass_bars(p), rig, in);
    CHECK(level(f[10]) < 0.01f);
}
