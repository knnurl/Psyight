// The hexagon effects (engine/hex_effects.cpp) and hex mappings (engine/space.h), on the hexagon
// mandala preset: rings at hex distance 1, 2/3 and 1/3, spokes at every corner.

#include "engine/effect_spec.h"
#include "engine/presets.h"
#include "engine/rig.h"
#include "engine/rig_templates.h"
#include "engine/scene.h"
#include "engine/space.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace psyight;

namespace {

constexpr double kPi = 3.14159265358979323846;

Rig mandala() {
    for (const RigPreset &p : builtin_rig_presets()) {
        if (p.name == "Hexagon mandala") {
            return build_rig(p.strips);
        }
    }
    FAIL("no hexagon mandala");
    return {};
}

float level(const Rgb &c) { return std::max({c.r, c.g, c.b}); }

Frame render(const EffectSpec &spec, const Rig &rig, const EngineInputs &in) {
    const Palette palette{"test", {{1, 0, 0}, {0, 0, 1}}};
    Frame out(rig.pixels.size());
    make_effect(spec)->render({rig, in, palette, 7}, out);
    return out;
}

std::vector<size_t> pixels_of(const Rig &rig, int strip) {
    std::vector<size_t> out;
    for (size_t i = 0; i < rig.pixels.size(); ++i) {
        if (rig.pixels[i].strip == strip) {
            out.push_back(i);
        }
    }
    return out;
}

EngineInputs grid_at(long long beat, double phase) {
    EngineInputs in;
    in.beat_s = 0.4;
    in.beat_index = beat;
    in.beat_phase = phase;
    in.beat_in_bar = static_cast<int>(beat % 4);
    in.bar_index = beat / 4;
    in.bar_phase = (static_cast<double>(beat % 4) + phase) / 4.0;
    return in;
}

} // namespace

TEST_CASE("hex frame: each hexagon ring is one hex distance; around runs clockwise from the top") {
    const Rig rig = mandala();
    const HexFrame frame = hex_frame(rig);
    CHECK(frame.corner_deg == doctest::Approx(90.0)); // Taller than wide: corners up and down.
    const std::vector<double> rings = map_pixels(rig, Mapping::hex_rings, 0);
    for (int strip = 0; strip < 3; ++strip) { // Outer, middle, inner.
        CAPTURE(strip);
        for (const size_t i : pixels_of(rig, strip)) {
            CHECK(rings[i] == doctest::Approx(1.0 - strip / 3.0).epsilon(1e-6));
        }
    }
    CHECK(hex_turn(frame, frame.cx, frame.cy + 1.0) == doctest::Approx(0.0));
    CHECK(hex_turn(frame, frame.cx + 1.0, frame.cy) == doctest::Approx(0.25));
    CHECK(hex_turn(frame, frame.cx - 1.0, frame.cy) == doctest::Approx(0.75));
}

TEST_CASE("hex pulse: a kick's hexagon lights a whole ring at once") {
    const Rig rig = mandala();
    EngineInputs in = grid_at(8, 0.0);
    in.kicks_so_far = 9;
    in.since_kick_s = 0.4 * 2.0 / 3.0; // Two thirds of the way out: on the middle ring.
    HexPulseParams p;
    p.keep = 1;
    const Frame out = render({p}, rig, in);
    const std::vector<size_t> middle = pixels_of(rig, 1);
    const float lit = level(out[middle.front()]);
    CHECK(lit > 0.5f);
    for (const size_t i : middle) {
        CHECK(level(out[i]) == doctest::Approx(lit).epsilon(1e-4));
    }
    for (const size_t i : pixels_of(rig, 2)) { // The inner ring, passed.
        CHECK(level(out[i]) < 0.05f * lit);
    }
    // Inwards: the same moment lights the inner ring's place in from the edge (1 - 2/3).
    p.direction = 1;
    const Frame in_ward = render({p}, rig, in);
    CHECK(level(in_ward[pixels_of(rig, 2).front()]) > 0.5f);
    CHECK(level(in_ward[middle.front()]) < 0.1f);
}

TEST_CASE("cube: three faces in three shades, flipping every bar") {
    const Rig rig = mandala();
    CubeParams p;
    p.fade_s = 0.0;
    const Frame bar0 = render({p}, rig, grid_at(1, 0.5)); // Bar 0.
    const Frame bar1 = render({p}, rig, grid_at(5, 0.5)); // Bar 1: flipped.
    const Frame bar2 = render({p}, rig, grid_at(9, 0.5)); // Bar 2: as bar 0.
    CHECK(bar0 == bar2);
    CHECK(bar0 != bar1);
    // Seen from above, the faces are three shades: the outer ring's levels take three values
    // (and the edges' at its corners).
    std::vector<float> levels;
    for (const size_t i : pixels_of(rig, 0)) {
        const float v = level(bar0[i]);
        if (std::none_of(levels.begin(), levels.end(),
                         [&](float l) { return std::fabs(l - v) < 1e-4f; })) {
            levels.push_back(v);
        }
    }
    CHECK(levels.size() >= 3);
    // The top spoke (spoke 4) lies in the top face from above, and on an edge from below.
    const std::vector<size_t> top = pixels_of(rig, 6);
    const size_t mid = top[top.size() / 2];
    CHECK(level(bar0[mid]) == doctest::Approx(0.55f)); // The top face, full tone.
    CHECK(bar1[mid] != bar0[mid]);                     // An edge: the palette's other end.
}

TEST_CASE("sectors: a chase steps one slice clockwise per 16th") {
    const Rig rig = mandala();
    SectorsParams p;       // Chase, six slices on the corners, a 16th a step.
    p.colour_spread = 0.0; // One colour, so a level is a brightness.
    const size_t top = pixels_of(rig, 6)[pixels_of(rig, 6).size() / 2];   // Spoke 4, up.
    const size_t right = pixels_of(rig, 5)[pixels_of(rig, 5).size() / 2]; // Spoke 3, 30 deg.
    const Frame step0 = render({p}, rig, grid_at(0, 0.0));
    const Frame step1 = render({p}, rig, grid_at(0, 0.25));
    CHECK(level(step0[top]) == doctest::Approx(0.9f));
    CHECK(level(step1[right]) == doctest::Approx(0.9f));
    CHECK(level(step1[top]) < 0.9f); // Fading behind.
}

TEST_CASE("interference: the lattice looks the same every 60 degrees round the centre") {
    // Six single pixels, a 60 degree turn apart round the origin.
    std::vector<StripSpec> dots;
    for (int k = 0; k < 6; ++k) {
        const double a = (10.0 + 60.0 * k) * kPi / 180.0;
        const Vec3 at{0.3 * std::cos(a), 0.3 * std::sin(a), 0.0};
        const Vec3 on{at.x + 0.001 * std::cos(a), at.y + 0.001 * std::sin(a), 0.0};
        dots.push_back({"dot", {at, on}, 1000.0, 1, {}});
    }
    const Rig rig = build_rig(dots);
    InterferenceParams p;
    p.rate = 0.0;
    p.corners = 1;
    const Frame out = render({p}, rig, grid_at(0, 0.0));
    for (const Rgb &c : out) {
        CHECK(level(c) == doctest::Approx(level(out[0])).epsilon(1e-4));
    }
}

TEST_CASE("hexagon pack: ids, settings and every scene renders on the mandala") {
    for (const EffectType t :
         {EffectType::hex_pulse, EffectType::cube, EffectType::sectors, EffectType::interference}) {
        CAPTURE(effect_type_id(t));
        CHECK(effect_type_from_id(effect_type_id(t)) == t);
        const std::vector<EffectParamInfo> infos = effect_param_infos(t);
        CHECK(std::string(infos.back().id) == "corners");
        CHECK(effect_option_name(infos.back(), 2) == "Sideways");
    }
    const auto packs = builtin_show_packs();
    const auto hexagon = std::find_if(packs.begin(), packs.end(),
                                      [](const ShowPack &p) { return p.name == "Hexagon"; });
    REQUIRE(hexagon != packs.end());
    CHECK(hexagon->scenes.size() == 8);
    const Rig rig = mandala();
    EngineInputs in = grid_at(13, 0.3);
    in.kicks_so_far = 14;
    in.since_kick_s = 0.1;
    in.bass_notes_so_far = 50;
    in.since_bass_onset_s = 0.05;
    in.phrase_index = 1;
    in.phrase_phase = 0.4;
    for (const Scene &scene : hexagon->scenes) {
        CAPTURE(scene.name);
        const Frame frame = render_scene(scene, rig, in, hexagon->palettes, 3);
        REQUIRE(frame.size() == rig.pixels.size());
        float most = 0.0f;
        for (const Rgb &c : frame) {
            CHECK(std::isfinite(c.r));
            most = std::max(most, level(c));
        }
        CHECK(most > 0.05f); // Something lit.
    }
}

TEST_CASE("hex pressure: brighter as the riser rises, a kick pushes out, dark for the last beat") {
    const auto packs = builtin_show_packs();
    const auto hexagon = std::find_if(packs.begin(), packs.end(),
                                      [](const ShowPack &p) { return p.name == "Hexagon"; });
    REQUIRE(hexagon != packs.end());
    const auto scene = std::find_if(hexagon->scenes.begin(), hexagon->scenes.end(),
                                    [](const Scene &s) { return s.name == "Hex pressure"; });
    REQUIRE(scene != hexagon->scenes.end());
    CHECK(scene->sections == std::vector<std::string>{"build"});
    const Rig rig = mandala();
    // A 64-beat build at 150 BPM.
    const auto at = [](double progress, double riser) {
        EngineInputs in = grid_at(static_cast<long long>(64 * progress), 0.5);
        in.section = "build";
        in.section_length_s = 64 * 0.4;
        in.section_progress = progress;
        in.since_section_s = progress * in.section_length_s;
        in.riser = riser;
        return in;
    };
    const auto total = [&](const EngineInputs &in) {
        double sum = 0.0;
        for (const Rgb &c : render_scene(*scene, rig, in, hexagon->palettes, 3)) {
            sum += level(c);
        }
        return sum;
    };
    const double early = total(at(0.1, 0.0));
    const double late = total(at(0.9, 1.0));
    CAPTURE(early);
    CAPTURE(late);
    CHECK(late > 2.0 * early); // The rise.

    EngineInputs kick = at(0.5, 0.5);
    kick.kicks_so_far = 32;
    kick.since_kick_s = 0.1;
    CHECK(total(kick) > total(at(0.5, 0.5)) + 1.0); // A hexagon pushed out.

    EngineInputs last = at(0.5, 1.0);
    last.since_section_s = last.section_length_s - 0.2; // Halfway through the last beat.
    last.since_kick_s = 0.1;
    for (const Rgb &c : render_scene(*scene, rig, last, hexagon->palettes, 3)) {
        CHECK(level(c) < 0.01f); // The held breath before the drop.
    }
}

TEST_CASE("hex echoes: intros; each hat and bass note draws, recent repeats overlapping") {
    const auto packs = builtin_show_packs();
    const auto hexagon = std::find_if(packs.begin(), packs.end(),
                                      [](const ShowPack &p) { return p.name == "Hexagon"; });
    REQUIRE(hexagon != packs.end());
    const Scene &intro =
        hexagon->scenes[static_cast<size_t>(scene_for_label(hexagon->scenes, "intro", 0))];
    CHECK(intro.name == "Hex echoes");
    CHECK(hexagon->scenes[static_cast<size_t>(scene_for_label(hexagon->scenes, "outro", 0))].name ==
          "Deep space");
    // Bursts on hats: an echo's earlier repeats still show while they fade.
    const Rig rig = mandala();
    EngineInputs one = grid_at(8, 0.2);
    one.hats_so_far = 3;
    one.since_hat_s = 0.05;
    one.hat_ages_s = {0.05, kNever, kNever, kNever};
    EngineInputs echoed = one;
    echoed.hat_ages_s = {0.05, 0.2, 0.35, kNever}; // Repeats 150 ms apart.
    BurstsParams b;
    b.trigger = 1;
    b.keep = 4;
    b.decay_s = 0.35;
    const auto lit = [&](const EngineInputs &in) {
        int n = 0;
        for (const Rgb &c : render({b}, rig, in)) {
            n += level(c) > 0.02f ? 1 : 0;
        }
        return n;
    };
    CHECK(lit(echoed) > lit(one));
    // Hex pulses on bass notes likewise.
    HexPulseParams h;
    h.trigger = 3;
    h.keep = 4;
    h.travel_beats = 2.0;
    EngineInputs note = grid_at(8, 0.2);
    note.bass_notes_so_far = 4;
    note.since_bass_onset_s = 0.05;
    note.bass_onset_ages_s = {0.05, kNever, kNever, kNever};
    EngineInputs notes = note;
    notes.bass_onset_ages_s = {0.05, 0.15, 0.25, 0.35};
    const auto pulses = [&](const EngineInputs &in) {
        int n = 0;
        for (const Rgb &c : render({h}, rig, in)) {
            n += level(c) > 0.02f ? 1 : 0;
        }
        return n;
    };
    CHECK(pulses(notes) > pulses(note));
    // The Hat source pulses on the latest hat.
    ModRoute r;
    r.source = ModSource::hat;
    CHECK(route_value(r, one, 1) == doctest::Approx(std::exp(-0.05 / 0.08)));
    EngineInputs none;
    CHECK(route_value(r, none, 1) == 0.0);
}
