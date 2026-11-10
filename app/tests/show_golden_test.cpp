// Show baseline (docs/PLAN_HEX_VISUALS.md phase 1): the Hexagon, Psy club and Dark forest packs
// rendered as a show on the hexagon mandala over a synthetic track with hand-set sections (one of
// each label, a one-bar build among them), measured by run_show_check (output/show_check.h) and
// compared with tests/golden/show_baseline.json. Any change fails, so every phase that changes
// the show shows its effect as a diff of that file. After an intended change, rewrite it with
//   set PSYIGHT_UPDATE_GOLDEN=1 && psyight_tests -tc="golden*"
// and explain every changed line in the commit message.

#include "analysis/track_analysis.h"
#include "engine/inputs.h"
#include "engine/presets.h"
#include "engine/rig.h"
#include "engine/rig_templates.h"
#include "output/show_check.h"
#include "synth_generator.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace psyight;

namespace {

constexpr int kSampleRateHz = 48000;
constexpr double kBpm = 150.0;
constexpr int kBars = 57;
constexpr double kFps = 20.0;
constexpr double kSlack = 1e-6; // Runs are bit-identical; this only absorbs JSON round trips.

std::filesystem::path golden_path() {
    return std::filesystem::path(PSYIGHT_TESTS_DIR) / "golden" / "show_baseline.json";
}

bool update_requested() {
    char *value = nullptr;
    size_t length = 0;
    const bool set = _dupenv_s(&value, &length, "PSYIGHT_UPDATE_GOLDEN") == 0 && value != nullptr &&
                     std::string(value) == "1";
    std::free(value);
    return set;
}

Rig mandala() {
    for (const RigPreset &p : builtin_rig_presets()) {
        if (p.name == "Hexagon mandala") {
            return build_rig(p.strips);
        }
    }
    FAIL("no hexagon mandala");
    return {};
}

// A rolling bassline with an octave jump, hats and sidechain, and the track's sections by bar.
struct Track {
    std::shared_ptr<const TrackAnalysis> analysis;
    Grid grid;
    std::shared_ptr<const SectionTimeline> sections;
    double duration_s = 0.0;
};

const Track &track() {
    static const Track t = [] {
        synth::SynthParams p;
        p.bpm = kBpm;
        p.bars = kBars;
        p.hats = true;
        p.sidechain_depth = 0.5;
        p.noise_level = 0.01;
        const synth::SynthOutput audio = synth::render(
            p, synth::make_pattern(synth::Pattern::rolling_octave_jump, kBars, 28, 0.8));
        TrackAnalysisOptions options;
        options.grid =
            Grid{p.bpm, seconds_to_samples(audio.labels.grid.first_downbeat_s, kSampleRateHz),
                 kSampleRateHz};
        Result<TrackAnalysis> analysed = analyse_track(audio.samples, kSampleRateHz, {}, options);
        REQUIRE(analysed.ok());
        Track out;
        out.analysis = std::make_shared<const TrackAnalysis>(std::move(analysed.value()));
        out.grid = out.analysis->grid;
        out.duration_s = out.analysis->duration_s;
        const double bar_s = 4.0 * 60.0 / kBpm;
        const double first_s = audio.labels.grid.first_downbeat_s;
        // (first bar, label): a one-bar build before an 8-bar groove (L3), a 4-bar build to a drop.
        const std::pair<int, const char *> starts[] = {
            {0, "intro"},  {4, "groove"}, {16, "build"},  {17, "groove"}, {25, "breakdown"},
            {33, "build"}, {37, "drop"},  {45, "groove"}, {53, "outro"}};
        std::vector<DetectedSection> sections;
        for (size_t i = 0; i < std::size(starts); ++i) {
            DetectedSection s;
            s.start_s = i == 0 ? 0.0 : first_s + starts[i].first * bar_s;
            s.end_s =
                i + 1 < std::size(starts) ? first_s + starts[i + 1].first * bar_s : out.duration_s;
            s.label = starts[i].second;
            s.confidence = 1.0;
            s.energy = s.label == "drop" || starts[i].first == 45 ? 2 : s.label == "groove" ? 1 : 0;
            sections.push_back(s);
        }
        out.sections = std::make_shared<const SectionTimeline>(
            section_timeline(*out.analysis, out.grid, std::move(sections)));
        return out;
    }();
    return t;
}

nlohmann::json measure(const ShowPack &pack) {
    const Track &t = track();
    ShowConfig config;
    config.analysis = t.analysis;
    config.grid = t.grid;
    config.sections = t.sections;
    config.rig = mandala();
    config.patch = build_patch(config.rig);
    config.limiter.brightness_cap = 0.4375; // The owner's hexagon project.
    config.limiter.budget_a = 3.75;
    config.limiter.flash_limiter = false;
    config.scenes = pack.scenes;
    config.palettes = pack.palettes;
    config.automation = pack.automation;
    ShowCheckOptions options;
    options.fps = kFps;
    const ShowCheckResult r = run_show_check(config, t.duration_s, options);
    nlohmann::json j;
    j["flashes_before"] = r.flashes_before;
    j["max_before_in_1s"] = r.max_before_in_1s;
    j["flashes_after"] = r.flashes_after;
    j["max_after_in_1s"] = r.max_after_in_1s;
    j["power_limited_frames"] = r.power_limited;
    j["current_mean_a"] = r.current_mean_a;
    j["current_max_a"] = r.current_max_a;
    j["dark_mean"] = r.dark_mean;
    j["dark_before_mean"] = r.dark_before_mean;
    for (const ShowCheckScene &s : r.scenes) {
        j["scenes"][s.name] = {{"seconds", s.seconds},
                               {"max_byte", s.max_byte},
                               {"median_lit_pixels", s.median_lit_pixels},
                               {"quarter_above_20", s.quarter_above_20},
                               {"flashes_alone", s.flashes_alone},
                               {"max_flashes_alone_in_1s", s.max_flashes_alone_in_1s}};
    }
    for (const ShowCheckGroup &g : r.groups) {
        j["groups"][g.name] = {
            {"mean_byte", g.mean_byte}, {"max_byte", g.max_byte}, {"dark_share", g.dark_share}};
    }
    return j;
}

// Every number in `golden` equals the one at the same place in `current`.
void compare(const nlohmann::json &golden, const nlohmann::json &current, const std::string &at) {
    if (golden.is_object()) {
        CHECK_MESSAGE(current.is_object(), at << " is no longer an object");
        if (!current.is_object()) {
            return;
        }
        CHECK_MESSAGE(golden.size() == current.size(), at << ": keys changed");
        for (const auto &[key, value] : golden.items()) {
            if (!current.contains(key)) {
                CHECK_MESSAGE(false, at << "/" << key << " is missing");
                continue;
            }
            compare(value, current.at(key), at + "/" + key);
        }
        return;
    }
    const double g = golden.get<double>();
    const double c = current.get<double>();
    CHECK_MESSAGE(std::abs(g - c) <= kSlack, at << ": " << g << " is now " << c);
}

} // namespace

TEST_CASE("golden show: the three packs on the hexagon over a synthetic track match the stored "
          "baseline") {
    nlohmann::json current;
    for (const ShowPack &pack : builtin_show_packs()) {
        if (pack.name == "Hexagon" || pack.name == "Psy club" || pack.name == "Dark forest") {
            current[pack.name] = measure(pack);
        }
    }
    REQUIRE(current.size() == 3);
    if (update_requested()) {
        std::filesystem::create_directories(golden_path().parent_path());
        std::ofstream(golden_path()) << current.dump(2) << "\n";
        MESSAGE("wrote " << golden_path().string());
        return;
    }
    std::ifstream file(golden_path());
    REQUIRE_MESSAGE(file.good(), "missing " << golden_path().string()
                                            << "; set PSYIGHT_UPDATE_GOLDEN=1 to create it");
    compare(nlohmann::json::parse(file), current, "");
}
