// Show baseline (docs/PLAN_HEX_VISUALS.md phase 1): the Hexagon, Psy club, Dark forest and
// Hexagon 2 packs
// rendered as a show on the hexagon mandala over a synthetic track with hand-set sections (one of
// each label, a one-bar build among them), measured by run_show_check (output/show_check.h) and
// compared with tests/golden/show_baseline.json. Any change fails, so every phase that changes
// the show shows its effect as a diff of that file. After an intended change, rewrite it with
//   set PSYIGHT_UPDATE_GOLDEN=1 && psyight_tests -tc="golden*"
// and explain every changed line in the commit message. The Cagan ember pack has its own file,
// show_cagan_baseline.json, measured on a stand-in for the piece with no brightness cap.

#include "analysis/track_analysis.h"
#include "engine/inputs.h"
#include "engine/presets.h"
#include "engine/rig.h"
#include "engine/rig_templates.h"
#include "io/session_record.h"
#include "output/show_check.h"
#include "synth_generator.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace psyight;

namespace {

constexpr int kSampleRateHz = 48000;
constexpr double kBpm = 150.0;
constexpr int kBars = 57;
constexpr double kFps = 20.0;
constexpr double kSlack = 1e-6; // Runs are bit-identical; this only absorbs JSON round trips.

std::filesystem::path golden_path(const char *name = "show_baseline.json") {
    return std::filesystem::path(PSYIGHT_TESTS_DIR) / "golden" / name;
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

// The same track with the finer detail labelled (Batch D): grooves A (bar 4), B (bar 17) and A
// (bar 45), the drop reprising A, a pre-drop over the build's last bar, a two-bar break in the
// first groove and a one-bar fill at the second groove's phrase end.
std::shared_ptr<const SectionTimeline> detailed_sections() {
    const Track &t = track();
    std::vector<DetectedSection> sections = t.sections->sections;
    const double bar_s = 4.0 * 60.0 / kBpm;
    const double first_s = sections[1].start_s - 4 * bar_s;
    for (DetectedSection &s : sections) {
        const auto bar = static_cast<int>(std::lround((s.start_s - first_s) / bar_s));
        if (s.label == "groove") {
            s.phase = bar == 17 ? 1 : 0;
        } else if (s.label == "drop") {
            s.phase = 0;
        } else if (s.label == "build" && bar == 33) {
            s.predrop_s = first_s + 36 * bar_s;
        }
    }
    return std::make_shared<const SectionTimeline>(section_timeline(
        *t.analysis, t.grid, std::move(sections),
        {{"break", first_s + 8 * bar_s, 2 * bar_s}, {"fill", first_s + 24 * bar_s, bar_s}}));
}

// The Batch D options on (each off by default).
AutomationParams with_options(AutomationParams a) {
    a.fill_source = FillSource::both;
    a.break_fill = PhraseFill::gap;
    a.scene_by = SceneBy::phase;
    return a;
}

// A stand-in for the Cagan piece (docs/PLAN_CAGAN_PACK.md section 2; the real rig is private):
// its zones and groups, not its shape. Twelve straight arms (odd ones wired inwards), three domes
// of two rings each, six panel loops, six rim loops, a halo ring and a short centre stub.
Rig cagan_stand_in() {
    constexpr double kPi = 3.14159265358979323846;
    std::vector<StripSpec> strips;
    const auto add = [&](std::string name, std::vector<Vec3> line, double per_m, int count,
                         std::vector<std::string> groups) {
        StripSpec s;
        s.name = std::move(name);
        s.polyline = std::move(line);
        s.pixels_per_m = per_m;
        s.count = count;
        s.fills = false;
        s.groups = std::move(groups);
        strips.push_back(std::move(s));
    };
    // A circle of radius r about (cx, cy), 36 sides, from the top.
    const auto circle = [&](double cx, double cy, double r) {
        std::vector<Vec3> p;
        for (int k = 0; k <= 36; ++k) {
            const double a = 2.0 * kPi * k / 36.0;
            p.push_back({cx + r * std::sin(a), cy + r * std::cos(a), 0.0});
        }
        return p;
    };
    const auto at = [&](double r, double deg) {
        return std::pair<double, double>{r * std::sin(deg * kPi / 180.0),
                                         r * std::cos(deg * kPi / 180.0)};
    };
    add("Halo", circle(0.0, 0.0, 0.95), 30.0, 170, {"halo", "glow"});
    for (int k = 0; k < 12; ++k) {
        const auto [x0, y0] = at(0.05, 30.0 * k);
        const auto [x1, y1] = at(0.55, 30.0 * k);
        std::vector<Vec3> line = {{x0, y0, 0.02}, {x1, y1, 0.02}};
        if (k % 2 == 1) {
            std::reverse(line.begin(), line.end());
        }
        add("Arm", line, 60.0, 30, {"arms", k % 2 == 0 ? "arms_inner" : "arms_outer", "sharp"});
    }
    for (int k = 0; k < 6; ++k) {
        const auto [px, py] = at(0.40, 15.0 + 60.0 * k);
        add("Panel", circle(px, py, 0.06), 60.0, 22, {"panels", "glow"});
        const auto [rx, ry] = at(0.72, 60.0 * k);
        add("Rim", circle(rx, ry, 0.10), 60.0, 37, {"rim", "glow"});
    }
    add("Centre", {{0.0, 0.0, 0.03}, {0.0, 0.0, 0.09}}, 60.0, 3, {"centre", "glow"});
    for (int k = 0; k < 3; ++k) {
        const auto [dx, dy] = at(0.40, 45.0 + 120.0 * k);
        std::vector<Vec3> dome = circle(dx, dy, 0.034);
        const std::vector<Vec3> outer = circle(dx, dy, 0.066);
        dome.insert(dome.end(), outer.begin(), outer.end());
        add("Dome", dome, 60.0, 36, {"domes", "dome_" + std::to_string(k + 1), "glow"});
    }
    return build_rig(strips);
}

nlohmann::json measure(const ShowPack &pack,
                       std::shared_ptr<const SectionTimeline> sections = nullptr,
                       std::optional<AutomationParams> automation = std::nullopt,
                       const Rig *rig = nullptr, double brightness_cap = 0.4375) {
    const Track &t = track();
    ShowConfig config;
    config.analysis = t.analysis;
    config.grid = t.grid;
    config.sections = sections != nullptr ? sections : t.sections;
    config.rig = rig != nullptr ? *rig : mandala();
    config.patch = build_patch(config.rig);
    config.limiter.brightness_cap = brightness_cap; // 0.4375: the owner's hexagon project.
    config.limiter.budget_a = 3.75;
    config.limiter.flash_limiter = false;
    config.scenes = pack.scenes;
    config.palettes = pack.palettes;
    config.automation = automation.value_or(pack.automation);
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
        if (pack.name == "Hexagon" || pack.name == "Psy club" || pack.name == "Dark forest" ||
            pack.name == "Hexagon 2") {
            current[pack.name] = measure(pack);
        }
    }
    REQUIRE(current.size() == 4);
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

TEST_CASE("golden show options: breaks, fills, pre-drop and phases on, Hexagon 4 over the "
          "synthetic track") {
    nlohmann::json current;
    for (const ShowPack &pack : builtin_show_packs()) {
        if (pack.name == "Hexagon 4") {
            current["Hexagon 4"] =
                measure(pack, detailed_sections(), with_options(pack.automation));
            current["Hexagon 4, options off"] = measure(pack, detailed_sections());
        }
    }
    REQUIRE(current.size() == 2);
    CHECK(current["Hexagon 4"] != current["Hexagon 4, options off"]); // The options do something.
    if (update_requested()) {
        std::ofstream(golden_path("show_options_baseline.json")) << current.dump(2) << "\n";
        MESSAGE("wrote " << golden_path("show_options_baseline.json").string());
        return;
    }
    std::ifstream file(golden_path("show_options_baseline.json"));
    REQUIRE_MESSAGE(file.good(), "missing show_options_baseline.json; set "
                                 "PSYIGHT_UPDATE_GOLDEN=1 to create it");
    compare(nlohmann::json::parse(file), current, "");
}

TEST_CASE("golden show Cagan: Cagan ember on a stand-in for the piece, uncapped, with and without "
          "the Batch D options") {
    const Rig rig = cagan_stand_in();
    nlohmann::json current;
    for (const ShowPack &pack : builtin_show_packs()) {
        if (pack.name == "Cagan ember") {
            // No brightness cap: the owner's decision for the piece (2026-11-17).
            current["Cagan ember"] =
                measure(pack, detailed_sections(), with_options(pack.automation), &rig, 1.0);
            current["Cagan ember, options off"] = measure(pack, nullptr, std::nullopt, &rig, 1.0);
        }
    }
    REQUIRE(current.size() == 2);
    // A scene for every section of the track plays. Not played here: First light (the intro and
    // outro are a phrase each and both take the first intro scene), Strike (the one drop takes the
    // first drop scene), Lure (tagged low; the track's grooves are mid and high), Held breath and
    // Afterimage (the track has no
    // short breakdown between grooves, which is what plays the break scenes).
    for (const char *scene :
         {"Dormant", "Nerve", "Tide", "Spine", "Inhale", "Swarm", "Bloom", "Drift"}) {
        CAPTURE(scene);
        CHECK(current["Cagan ember"]["scenes"][scene]["seconds"].get<double>() +
                  current["Cagan ember, options off"]["scenes"][scene]["seconds"].get<double>() >
              0.0);
    }
    if (update_requested()) {
        std::ofstream(golden_path("show_cagan_baseline.json")) << current.dump(2) << "\n";
        MESSAGE("wrote " << golden_path("show_cagan_baseline.json").string());
        return;
    }
    std::ifstream file(golden_path("show_cagan_baseline.json"));
    REQUIRE_MESSAGE(file.good(), "missing show_cagan_baseline.json; set "
                                 "PSYIGHT_UPDATE_GOLDEN=1 to create it");
    compare(nlohmann::json::parse(file), current, "");
}

TEST_CASE("replay: a recording with the Batch D options and labelled detail replays identically") {
    const Track &t = track();
    ShowPack pack;
    for (const ShowPack &p : builtin_show_packs()) {
        if (p.name == "Hexagon 4") {
            pack = p;
        }
    }
    ShowConfig config;
    config.analysis = t.analysis;
    config.grid = t.grid;
    config.sections = detailed_sections();
    config.rig = mandala();
    config.patch = build_patch(config.rig);
    config.scenes = pack.scenes;
    config.palettes = pack.palettes;
    config.automation = with_options(pack.automation);
    // As the app records it (PreviewView::record_params) and psyight_replay rebuilds it.
    Project project = default_project();
    project.scenes = config.scenes;
    project.palettes = config.palettes;
    project.automation = config.automation;
    RuntimeState runtime;
    runtime.grid = config.grid;
    runtime.sections = config.sections->sections;
    runtime.events = config.sections->events;
    const Result<std::vector<ParamsEntry>> entries =
        parse_params_jsonl(params_line(1, project, runtime).value());
    REQUIRE(entries.ok());
    const ParamsEntry &e = entries.value()[0];
    ShowConfig replayed = config;
    replayed.automation = e.project.automation;
    replayed.scenes = e.project.scenes;
    replayed.palettes = e.project.palettes;
    replayed.sections = std::make_shared<const SectionTimeline>(
        section_timeline(*t.analysis, e.runtime.grid, e.runtime.sections, e.runtime.events));
    const auto frames = [&](const ShowConfig &c) {
        std::vector<Frame> out;
        ShowCheckOptions options;
        options.fps = kFps;
        options.on_frame = [&](const ShowFrame &f) { out.push_back(f.frame); };
        run_show_check(c, t.duration_s, options);
        return out;
    };
    const std::vector<Frame> recorded = frames(config);
    const std::vector<Frame> again = frames(replayed);
    REQUIRE(recorded.size() == again.size());
    size_t different = 0;
    for (size_t i = 0; i < recorded.size(); ++i) {
        different += recorded[i] != again[i];
    }
    CHECK(different == 0);
}
