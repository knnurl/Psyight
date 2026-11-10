// engine/shape.h: sizes relative to the rig, times relative to the beat, hit envelopes, and the
// size_units and time_units settings of the older effects (docs/PLAN_HEX_VISUALS.md 4.1 to 4.3).

#include "effect_test_helpers.h"
#include "engine/shape.h"

#include <doctest/doctest.h>

#include <cmath>
#include <string>
#include <vector>

using namespace psyight;
using test::grid_at;
using test::level;
using test::mandala;
using test::render;

namespace {

// The mandala scaled to this reach (1 m: exactly).
Rig mandala_with_reach(double reach_m) {
    Rig rig = mandala();
    const double s = reach_m / rig_reach_m(rig);
    const double cx = 0.5 * (rig.min.x + rig.max.x);
    const double cy = 0.5 * (rig.min.y + rig.max.y);
    for (Pixel &p : rig.pixels) {
        p.position = {cx + (p.position.x - cx) * s, cy + (p.position.y - cy) * s, p.position.z};
    }
    rig.min = {cx + (rig.min.x - cx) * s, cy + (rig.min.y - cy) * s, rig.min.z};
    rig.max = {cx + (rig.max.x - cx) * s, cy + (rig.max.y - cy) * s, rig.max.z};
    return rig;
}

// Something happening for every effect: a kick, hats, a bass note, in a drop (or a build).
EngineInputs busy(double t_s, const std::string &section) {
    EngineInputs in = grid_at(40, 0.3);
    in.t_s = t_s;
    in.section = section;
    in.since_section_s = 0.3;
    in.section_length_s = 12.8;
    in.section_progress = 0.4;
    in.riser = 0.5;
    in.kicks_so_far = 41;
    in.since_kick_s = 0.12;
    in.hats_so_far = 80;
    in.since_hat_s = 0.05;
    in.hat_ages_s = {0.05, 0.25, 0.45, 0.65};
    in.bass_notes_so_far = 120;
    in.since_bass_onset_s = 0.03;
    in.bass_onset_ages_s = {0.03, 0.13, 0.23, 0.33};
    in.residual_centroid_hz = 3000.0;
    return in;
}

void check_same(const Frame &a, const Frame &b) {
    REQUIRE(a.size() == b.size());
    bool any = false;
    for (size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].r == doctest::Approx(b[i].r).epsilon(1e-5));
        CHECK(a[i].g == doctest::Approx(b[i].g).epsilon(1e-5));
        CHECK(a[i].b == doctest::Approx(b[i].b).epsilon(1e-5));
        any |= level(a[i]) > 0.01f;
    }
    CHECK(any); // The comparison saw something lit.
}

// The spec with the named integer setting set.
EffectSpec with(EffectSpec spec, const char *id, double value) {
    const std::vector<EffectParamInfo> infos = effect_param_infos(spec.type());
    for (size_t i = 0; i < infos.size(); ++i) {
        if (std::string(infos[i].id) == id) {
            set_effect_param(spec, i, value);
            return spec;
        }
    }
    FAIL("no setting " << id);
    return spec;
}

} // namespace

TEST_CASE("shape: reach, sizes, times, the hit envelope and the overshoot ease") {
    const Rig rig = mandala();
    const double reach = rig_reach_m(rig);
    CHECK(reach > 0.1);
    CHECK(size_in_m(0.25, 0, rig) == 0.25);
    CHECK(size_in_m(0.25, 1, rig) == doctest::Approx(0.25 * reach));
    EngineInputs in;
    CHECK(time_in_s(0.5, 1, in) == doctest::Approx(0.5)); // No tempo: the reference beat.
    in.beat_s = 60.0 / 184.0;
    CHECK(time_in_s(0.5, 0, in) == 0.5);
    CHECK(time_in_s(0.5, 1, in) == doctest::Approx(0.5 * (60.0 / 184.0) / 0.4));
    CHECK(hit_envelope(-0.01, 0.04, 0.2) == 0.0);
    CHECK(hit_envelope(0.0, 0.04, 0.2) == 1.0);
    CHECK(hit_envelope(0.04, 0.04, 0.2) == 1.0);
    CHECK(hit_envelope(0.24, 0.04, 0.2) == doctest::Approx(std::exp(-1.0)));
    CHECK(hit_envelope(0.2, 0.0, 0.2) == doctest::Approx(std::exp(-1.0))); // No hold: as before.
    CHECK(overshoot_ease(0.0, 0.1) == doctest::Approx(0.0));
    CHECK(overshoot_ease(1.0, 0.1) == doctest::Approx(1.0));
    CHECK(overshoot_ease(2.0, 0.1) == doctest::Approx(1.0));
    double peak = 0.0;
    for (int k = 0; k <= 100; ++k) {
        peak = std::max(peak, overshoot_ease(k / 100.0, 0.1));
    }
    CHECK(peak == doctest::Approx(1.1).epsilon(0.02)); // About 10% past.
    for (int k = 0; k < 100; ++k) {                    // No overshoot: monotonic, never past 1.
        CHECK(overshoot_ease((k + 1) / 100.0, 0.0) >= overshoot_ease(k / 100.0, 0.0));
    }
}

TEST_CASE("shape: on a rig of reach 1 m, sizes in metres and relative to the rig are the same") {
    const Rig rig = mandala_with_reach(1.0);
    CHECK(rig_reach_m(rig) == doctest::Approx(1.0).epsilon(1e-12));
    const EffectSpec specs[] = {{PlasmaParams{}}, {ShockwaveParams{}}, {BurstsParams{}},
                                {NoiseParams{}},  {FireParams{}},      {InterferenceParams{}},
                                {BuildParams{}},  {DropBurstParams{}}};
    for (const EffectSpec &spec : specs) {
        CAPTURE(effect_type_id(spec.type()));
        const std::string section = spec.type() == EffectType::build ? "build" : "drop";
        const EngineInputs in = busy(30.0, section);
        check_same(render(with(spec, "size_units", 0), rig, in),
                   render(with(spec, "size_units", 1), rig, in));
    }
}

TEST_CASE("shape: at 150 BPM, times in seconds and in beats are the same; faster, beats shorten") {
    const Rig rig = mandala();
    const EffectSpec specs[] = {
        {ShockwaveParams{}}, {BurstsParams{}}, {HexPulseParams{}}, {DropBurstParams{}}};
    for (const EffectSpec &spec : specs) {
        CAPTURE(effect_type_id(spec.type()));
        EngineInputs in = busy(30.0, "drop");
        check_same(render(with(spec, "time_units", 0), rig, in),
                   render(with(spec, "time_units", 1), rig, in));
    }
    // A shockwave 0.3 s after a kick at 184 BPM is dimmer in beats than in seconds.
    EngineInputs in = busy(30.0, "drop");
    in.beat_s = 60.0 / 184.0;
    in.since_kick_s = 0.3;
    float seconds = 0.0f;
    float beats = 0.0f;
    const EffectSpec shock{ShockwaveParams{}};
    for (const Rgb &c : render(with(shock, "time_units", 0), rig, in)) {
        seconds = std::max(seconds, level(c));
    }
    for (const Rgb &c : render(with(shock, "time_units", 1), rig, in)) {
        beats = std::max(beats, level(c));
    }
    CHECK(beats < seconds);
}

TEST_CASE("shape: on the 0.2 m hexagon a rig-relative shockwave is a ring, not the whole rig") {
    const Rig rig = mandala_with_reach(0.2);
    EngineInputs in = busy(30.0, "groove");
    in.since_kick_s = 0.25 / 6.0; // A quarter of the reach out, at 6 reaches a second.
    // The outer ring's brightest pixel (strip 0).
    const auto outer = [&](int size_units) {
        const Frame out = render(with({ShockwaveParams{}}, "size_units", size_units), rig, in);
        float most = 0.0f;
        for (const size_t i : test::pixels_of(rig, 0)) {
            most = std::max(most, level(out[i]));
        }
        return most;
    };
    CHECK(rig_reach_m(rig) < 0.5);
    CHECK(outer(0) > 0.3f);  // In metres the ring is wider than the rig: the outer ring is lit.
    CHECK(outer(1) < 0.05f); // Relative to the rig it has not reached the outer ring yet.
}
