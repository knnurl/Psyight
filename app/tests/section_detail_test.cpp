// Finer sections (analysis/section_detail.h) on bars built here, where the answer is known: two
// groove textures in the order A B A, a drop reprising A, a lead and a peak, a pre-drop gap, a
// break and a fill.

#include "analysis/section_detail.h"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using namespace psyight;

namespace {

constexpr double kBarS = 1.6; // 150 BPM.

// A bar of texture `t` (two values, A or B), with its kick, level, hats and residual.
SectionBar bar(int index, double t, int kick_beats, double level_db = -20.0, double flatness = 0.3,
               double centroid_hz = 2000.0) {
    SectionBar b;
    b.start_s = index * kBarS;
    b.kick = kick_beats > 0;
    b.kick_beats = kick_beats;
    b.level_db = level_db;
    b.high_db = level_db - 10.0;
    b.flatness = flatness;
    b.centroid_hz = centroid_hz;
    b.texture = {t, -t, 0.5 * t};
    return b;
}

} // namespace

TEST_CASE("section detail: phases A B A, a drop reprising A, roles, a pre-drop, a break, a fill") {
    std::vector<SectionBar> bars;
    std::vector<DetectedSection> sections;
    const auto add = [&](const char *label, int count, double texture, int kicks,
                         double level = -20.0, double flatness = 0.3, double centroid = 2000.0) {
        DetectedSection s;
        s.start_s = static_cast<double>(bars.size()) * kBarS;
        s.label = label;
        for (int k = 0; k < count; ++k) {
            bars.push_back(
                bar(static_cast<int>(bars.size()), texture, kicks, level, flatness, centroid));
        }
        s.end_s = static_cast<double>(bars.size()) * kBarS;
        sections.push_back(s);
    };
    add("groove", 16, 0.0, 4);                     // A.
    add("groove", 16, 4.0, 4, -20.0, 0.1, 4000.0); // B: tonal and bright, a lead.
    add("groove", 16, 0.0, 4);                     // A again.
    add("breakdown", 8, -3.0, 0, -30.0);
    add("build", 8, -1.0, 0, -22.0);
    add("drop", 16, 0.2, 4);   // Close to A.
    add("groove", 16, 12.0, 4); // C: the peak (intensity below).
    // Bars: grooves 0 to 47, breakdown 48 to 55, build 56 to 63, drop 64 to 79, groove 80 to 95.
    // The build's last two bars are the gap before the drop.
    bars[62].level_db = -40.0;
    bars[63].level_db = -40.0;
    // A two-bar break in the first groove; the second groove's phrases end in a fill.
    bars[5].kick_beats = 0;
    bars[6].kick_beats = 0;
    bars[16 + 7].texture = {20.0, -20.0, 10.0};
    std::vector<float> intensity(bars.size(), 0.4f);
    for (size_t b = 80; b < 96; ++b) {
        intensity[b] = 0.95f;
    }
    for (size_t b = 64; b < 80; ++b) {
        intensity[b] = 0.7f;
    }
    std::vector<double> phrases;
    for (size_t b = 0; b < bars.size(); b += 8) {
        phrases.push_back(static_cast<double>(b) * kBarS);
    }

    const std::vector<DetectedEvent> events =
        detect_section_detail(sections, bars, {}, intensity, phrases, kBarS);

    CHECK(sections[0].phase == 0);
    CHECK(sections[1].phase == 1);
    CHECK(sections[2].phase == 0); // A returns.
    CHECK(sections[5].phase == 0); // The drop reprises A.
    CHECK(sections[6].phase == 2);
    CHECK(sections[3].phase == -1); // Breakdowns have none.
    CHECK(sections[0].role == "main");
    CHECK(sections[1].role == "lead");
    CHECK(sections[6].role == "peak");
    CHECK(sections[5].role == "climax");
    CHECK(sections[4].predrop_s == doctest::Approx(62 * kBarS)); // The last two bars.
    CHECK(sections[3].predrop_s < 0.0);                          // Not before a drop.
    REQUIRE(events.size() >= 2);
    CHECK(events[0].kind == "break");
    CHECK(events[0].start_s == doctest::Approx(5 * kBarS));
    CHECK(events[0].length_s == doctest::Approx(2 * kBarS));
    bool fill = false;
    for (const DetectedEvent &e : events) {
        fill |= e.kind == "fill" && std::abs(e.start_s - 23 * kBarS) < 1e-6;
    }
    CHECK(fill);
}
