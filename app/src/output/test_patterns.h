// Test patterns (SPEC section 11, preflight): frames that check the wiring instead of showing the
// music. The show renderer draws one in place of the scenes while it is on, still through the
// limiters (brightness cap, power budget, flash limiter) and the patch. Blackout still wins.
// - chase: a short lit segment running along each strip from its first pixel, in the strip's own
//   colour (or along one strip): shows each strip's direction and its order in the chain.
// - colour order: the whole rig steps red, green, blue, kColourStepS each; a strip showing another
//   colour than the panel names has the wrong colour order.
// - identify universe: only the pixels patched to that universe light, in cyan.
// - all on: white at full, so the limiters show what full white really draws.
// Pure: a function of the pattern, the rig, the patch and the pattern's own clock (seconds since
// it started, counted in show frames so it runs while the transport is stopped).
#pragma once

#include "engine/colour.h"
#include "engine/rig.h"
#include "output/patch.h"

namespace psyight {

enum class TestPatternKind { off, chase, colour_order, identify_universe, all_on };

struct TestPattern {
    TestPatternKind kind = TestPatternKind::off;
    int strip = -1;                   // Chase: one strip by index, or -1 for every strip.
    int universe = 1;                 // Identify: the universe number.
    double chase_pixels_per_s = 30.0; // Chase speed.
    int chase_length = 6;             // Lit pixels, fading towards the tail.
    double level = 0.6;               // Chase, colour order and identify (all on is 1).
    bool send = true;                 // To the lights, even in Preflight and Preview.

    bool operator==(const TestPattern &) const = default;
};

inline constexpr double kColourStepS = 1.0;

Frame render_test_pattern(const TestPattern &pattern, const Rig &rig, const Patch &patch,
                          double clock_s);
// 0 red, 1 green, 2 blue: the colour-order step showing at clock_s.
int colour_order_step(double clock_s);
const char *test_pattern_name(TestPatternKind kind);

} // namespace psyight
