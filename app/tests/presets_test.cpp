// The preset library (engine/presets.h) and preset files (io/presets.h).

#include "engine/effect_spec.h"
#include "engine/presets.h"
#include "engine/rig_templates.h"
#include "io/presets.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

using namespace psyight;

namespace {

bool has_white(const Palette &p) {
    return std::any_of(p.colours.begin(), p.colours.end(),
                       [](const Rgb &c) { return c.r > 0.9f && c.g > 0.9f && c.b > 0.9f; });
}

// Every route points at a layer that exists, and a setting route at a setting its effect has.
void check_routes(const Scene &scene) {
    for (const ModRoute &r : scene.routes) {
        CAPTURE(scene.name);
        CAPTURE(r.param);
        if (r.target == ModTarget::brightness) {
            continue;
        }
        REQUIRE(r.layer >= 0);
        REQUIRE(r.layer < static_cast<int>(scene.layers.size()));
        if (r.target == ModTarget::setting) {
            const std::vector<EffectParamInfo> infos =
                effect_param_infos(scene.layers[static_cast<size_t>(r.layer)].spec.type());
            CHECK(std::any_of(infos.begin(), infos.end(),
                              [&](const EffectParamInfo &i) { return r.param == i.id; }));
        }
    }
}

std::filesystem::path fresh_dir(const char *name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    return dir;
}

} // namespace

TEST_CASE("presets: the built-in library is complete and consistent") {
    const std::vector<ScenePreset> looks = builtin_scene_presets();
    CHECK(looks.size() >= 8);
    for (const ScenePreset &p : looks) {
        CAPTURE(p.name);
        CHECK_FALSE(p.scene.layers.empty());
        CHECK_FALSE(p.scene.sections.empty());
        CHECK_FALSE(p.description.empty());
        CHECK(p.scene.name == p.name);
        CHECK_FALSE(has_white(p.palette));
        check_routes(p.scene);
    }
    for (const Palette &p : builtin_palettes()) {
        CAPTURE(p.name);
        CHECK(p.colours.size() >= 2);
        CHECK_FALSE(has_white(p));
    }
    for (const ShowPack &pack : builtin_show_packs()) {
        CAPTURE(pack.name);
        REQUIRE_FALSE(pack.scenes.empty());
        bool groove = false;
        for (const Scene &s : pack.scenes) {
            CHECK(s.palette >= 0);
            CHECK(s.palette < static_cast<int>(pack.palettes.size()));
            check_routes(s);
            groove |= std::find(s.sections.begin(), s.sections.end(), "groove") != s.sections.end();
        }
        CHECK(groove); // Every pack has a scene for grooves.
    }
}

TEST_CASE("presets: scene presets, palettes and show packs round-trip as JSON") {
    for (const ScenePreset &p : builtin_scene_presets()) {
        const Result<std::string> text = scene_preset_json(p);
        REQUIRE(text.ok());
        const Result<ScenePreset> back = parse_scene_preset(text.value());
        REQUIRE(back.ok());
        CHECK(back.value().name == p.name);
        CHECK(back.value().scene.routes == p.scene.routes);
        CHECK(back.value().scene.layers.size() == p.scene.layers.size());
        CHECK(scene_preset_json(back.value()).value() == text.value());
    }
    const Palette ice = builtin_palettes().back();
    const Result<Palette> palette = parse_palette_preset(palette_preset_json(ice).value());
    REQUIRE(palette.ok());
    CHECK(palette.value().name == ice.name);
    CHECK(palette.value().colours.size() == ice.colours.size());
    for (const ShowPack &pack : builtin_show_packs()) {
        const Result<std::string> text = show_pack_json(pack);
        REQUIRE(text.ok());
        const Result<ShowPack> back = parse_show_pack(text.value());
        REQUIRE(back.ok());
        CHECK(back.value().macro_names == pack.macro_names);
        CHECK(show_pack_json(back.value()).value() == text.value());
    }
    // The wrong kind of file is refused.
    CHECK_FALSE(parse_show_pack(palette_preset_json(ice).value()).ok());
    CHECK_FALSE(parse_scene_preset("{not json").ok());
}

TEST_CASE("presets: applying a scene preset reuses or adds its palette") {
    Project project = default_project();
    const size_t scenes = project.scenes.size();
    const size_t palettes = project.palettes.size();
    const std::vector<ScenePreset> looks = builtin_scene_presets();
    const ScenePreset &rolling = looks[0]; // UV violet: already in the project.
    CHECK(apply_scene_preset(project, rolling, 1) == 1);
    CHECK(project.scenes.size() == scenes);
    CHECK(project.scenes[1].name == rolling.name);
    CHECK(project.palettes.size() == palettes);
    CHECK(project.palettes[static_cast<size_t>(project.scenes[1].palette)].name == "UV violet");

    const auto minimal = std::find_if(
        looks.begin(), looks.end(), [](const ScenePreset &p) { return p.name == "Minimal dark"; });
    REQUIRE(minimal != looks.end());
    const int added = apply_scene_preset(project, *minimal, -1); // Mono violet: new.
    CHECK(added == static_cast<int>(scenes));
    CHECK(project.palettes.size() == palettes + 1);
    CHECK(project.palettes[static_cast<size_t>(project.scenes.back().palette)].name ==
          "Mono violet");

    // Saving a scene back as a preset carries its palette.
    const ScenePreset again = scene_preset_of(project, added);
    CHECK(again.palette.name == "Mono violet");
    CHECK(again.scene.palette == 0);
}

TEST_CASE("presets: a show pack replaces scenes, palettes, automation and macro names") {
    Project project = default_project();
    project.macro_values[2] = 0.7;
    const std::vector<ShowPack> packs = builtin_show_packs();
    const auto live = std::find_if(packs.begin(), packs.end(),
                                   [](const ShowPack &p) { return p.name == "Live macros"; });
    REQUIRE(live != packs.end());
    apply_show_pack(project, *live);
    CHECK(project.scenes.size() == live->scenes.size());
    CHECK(project.palettes.size() == live->palettes.size());
    CHECK(project.automation.palette_every_phrases == 0);
    CHECK(macro_name(project, 0) == "Ambience");
    CHECK(project.macro_values[2] == 0.7); // Values stay.
    // And back out of the project into a pack.
    const ShowPack mine = show_pack_of(project, "Mine");
    CHECK(mine.scenes.size() == live->scenes.size());
    CHECK(mine.macro_names == live->macro_names);
}

TEST_CASE("presets: the user's presets are files beside the project") {
    const std::filesystem::path dir = fresh_dir("psyight_presets_test");
    CHECK(load_user_presets(dir).scenes.empty()); // No folder: nothing, no errors.
    ScenePreset look = builtin_scene_presets()[2];
    look.name = "My look: v2/final?";
    REQUIRE(save_scene_preset(dir, look).ok());
    const std::filesystem::path file = preset_path(dir, PresetKind::scene, look.name);
    CHECK(file.filename() == "My look_ v2_final_.json");
    CHECK(std::filesystem::exists(file));
    REQUIRE(save_palette_preset(dir, builtin_palettes()[5]).ok());
    REQUIRE(save_show_pack(dir, builtin_show_packs()[1]).ok());
    std::ofstream(dir / "scenes" / "broken.json") << "{\"format\": \"psyight scene preset\"}";
    const UserPresets user = load_user_presets(dir);
    REQUIRE(user.scenes.size() == 1);
    CHECK(user.scenes[0].name == look.name);
    REQUIRE(user.palettes.size() == 1);
    REQUIRE(user.shows.size() == 1);
    REQUIRE(user.errors.size() == 1); // broken.json, named.
    CHECK(user.errors[0].find("broken.json") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("presets: the choreographed packs cover every section, grooves by energy") {
    for (const char *name : {"Psy club", "Dark forest", "Hexagon 2", "Hexagon 3: Tunnel",
                             "Hexagon 3: Geometry", "Hexagon 3: Pulse", "Hexagon 4"}) {
        CAPTURE(name);
        const std::vector<ShowPack> packs = builtin_show_packs();
        const auto pack = std::find_if(packs.begin(), packs.end(),
                                       [&](const ShowPack &p) { return p.name == name; });
        REQUIRE(pack != packs.end());
        for (const char *label : {"intro", "groove", "breakdown", "build", "drop", "outro"}) {
            CAPTURE(label);
            bool covered = false;
            for (const Scene &s : pack->scenes) {
                covered |=
                    std::find(s.sections.begin(), s.sections.end(), label) != s.sections.end();
            }
            CHECK(covered);
        }
        // A high-energy groove and a low or mid one get different scenes.
        CHECK(scene_for_label(pack->scenes, "groove", 0, 2) !=
              scene_for_label(pack->scenes, "groove", 0, 0));
    }
}

TEST_CASE("presets: the user's rigs are files in presets/rigs, geometry and groups kept") {
    const std::filesystem::path dir = fresh_dir("psyight_rig_presets_test");
    CHECK(load_user_presets(dir).rigs.empty());
    RigPreset rig = builtin_rig_presets()[0]; // The DJ booth chevron.
    rig.name = "Garden stage";
    rig.description = "3 strips, 408 pixels, saved from Test.";
    REQUIRE(save_rig_preset(dir, rig).ok());
    CHECK(std::filesystem::exists(dir / "rigs" / "Garden stage.json"));
    const UserPresets user = load_user_presets(dir);
    REQUIRE(user.rigs.size() == 1);
    const RigPreset &loaded = user.rigs[0];
    CHECK(loaded.name == rig.name);
    CHECK(loaded.description == rig.description);
    REQUIRE(loaded.strips.size() == rig.strips.size());
    for (size_t i = 0; i < rig.strips.size(); ++i) {
        CHECK(loaded.strips[i].name == rig.strips[i].name);
        CHECK(loaded.strips[i].count == rig.strips[i].count);
        CHECK(loaded.strips[i].pixels_per_m == rig.strips[i].pixels_per_m);
        CHECK(loaded.strips[i].groups == rig.strips[i].groups);
        REQUIRE(loaded.strips[i].polyline.size() == rig.strips[i].polyline.size());
        CHECK(loaded.strips[i].polyline.back().x ==
              doctest::Approx(rig.strips[i].polyline.back().x));
    }
    // A rig without strips is refused, and named among the errors.
    std::ofstream(dir / "rigs" / "empty.json")
        << "{\"format\": \"psyight rig preset\", \"name\": \"Empty\", \"strips\": []}";
    const UserPresets again = load_user_presets(dir);
    CHECK(again.rigs.size() == 1);
    REQUIRE(again.errors.size() == 1);
    CHECK(again.errors[0].find("empty.json") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("presets: Hexagon 2 and 3 render every scene on the mandala, lit over a bar, no NaN") {
    const std::vector<ShowPack> packs = builtin_show_packs();
    const auto index_of = [&](const char *name) {
        return std::find_if(packs.begin(), packs.end(),
                            [&](const ShowPack &p) { return p.name == name; }) -
               packs.begin();
    };
    // Appended after the older packs, in order.
    CHECK(index_of("Hexagon 2") == index_of("Hexagon") + 1);
    CHECK(index_of("Hexagon 3: Tunnel") == index_of("Hexagon 2") + 1);
    CHECK(index_of("Hexagon 3: Geometry") == index_of("Hexagon 2") + 2);
    CHECK(index_of("Hexagon 3: Pulse") == index_of("Hexagon 2") + 3);
    CHECK(index_of("Hexagon 4") == index_of("Hexagon 2") + 4);
    CHECK(static_cast<size_t>(index_of("Hexagon 4")) == packs.size() - 1);
    for (const char *name : {"Hexagon 2", "Hexagon 3: Tunnel", "Hexagon 3: Geometry",
                             "Hexagon 3: Pulse", "Hexagon 4"}) {
        CAPTURE(name);
        const auto pack = packs.begin() + index_of(name);
        REQUIRE(pack != packs.end());
        const std::string n = name;
        CHECK(pack->scenes.size() == (n == "Hexagon 2" ? 14u : n == "Hexagon 4" ? 20u : 8u));
        CHECK(pack->automation.palette_rotation == PaletteRotation::scene_list);
        Rig rig;
        for (const RigPreset &p : builtin_rig_presets()) {
            if (p.name == "Hexagon mandala") {
                rig = build_rig(p.strips);
            }
        }
        REQUIRE_FALSE(rig.pixels.empty());
        for (const Scene &scene : pack->scenes) {
            CAPTURE(scene.name);
            CHECK_FALSE(scene.palette_list.empty());
            for (const int i : scene.palette_list) {
                CHECK(i >= 0);
                CHECK(i < static_cast<int>(pack->palettes.size()));
            }
            float most = 0.0f;
            for (int k = 0; k < 16; ++k) { // A bar at 150 BPM, on 16ths and between.
                EngineInputs in;
                in.beat_s = 0.4;
                in.t_s = 40.0 + 0.1 * k + 0.03;
                in.beat_index = 100 + k / 4;
                in.beat_phase = (k % 4) * 0.25 + 0.075;
                in.bar_index = 25;
                in.bar_phase = in.t_s / 1.6 - std::floor(in.t_s / 1.6);
                in.phrase_index = 3;
                in.phrase_phase = 0.3 + 0.01 * k;
                in.section = scene.sections.front();
                in.since_section_s = 0.05 + 0.1 * k;
                in.section_length_s = 12.8;
                in.section_end_known = true;
                in.section_progress = in.since_section_s / 12.8;
                in.since_build_s = in.since_section_s;
                in.build_length_s = 12.8;
                in.build_end_known = true;
                in.build_progress = in.section_progress;
                in.riser = 0.3;
                in.kicks_so_far = 100 + k / 4;
                in.since_kick_s = (k % 4) * 0.1 + 0.03;
                in.hats_so_far = 200 + k / 2;
                in.since_hat_s = (k % 2) * 0.1 + 0.02;
                in.hat_ages_s = {in.since_hat_s, in.since_hat_s + 0.2, in.since_hat_s + 0.4,
                                 in.since_hat_s + 0.6};
                in.bass_notes_so_far = 300 + k;
                in.bass_active = k % 4 != 0;
                in.bass_midi = 35;
                in.bass_velocity = 0.9;
                in.bass_confidence = 0.8;
                in.bass_gate_ratio = 0.8;
                in.since_bass_onset_s = 0.04;
                in.bass_onset_ages_s = {0.04, 0.14, 0.24, 0.34};
                in.residual_centroid_hz = 2500.0;
                in.residual_level = 0.4;
                const Frame frame = render_scene(scene, rig, in, pack->palettes, 1);
                for (const Rgb &c : frame) {
                    REQUIRE(std::isfinite(c.r));
                    REQUIRE(std::isfinite(c.g));
                    REQUIRE(std::isfinite(c.b));
                    most = std::max({most, c.r, c.g, c.b});
                }
            }
            CHECK(most > 0.05f);
        }
    }
}

TEST_CASE("presets: Hexagon 4 rotates several scenes per section, the Hexagon pack untouched") {
    const std::vector<ShowPack> packs = builtin_show_packs();
    const auto named = [&](const char *name) {
        return *std::find_if(packs.begin(), packs.end(),
                             [&](const ShowPack &p) { return p.name == name; });
    };
    const ShowPack hex4 = named("Hexagon 4");
    const ShowPack hexagon = named("Hexagon");
    // The Hexagon pack's scenes are all in it, their layers and routes as they are.
    for (const Scene &original : hexagon.scenes) {
        CAPTURE(original.name);
        const auto copy = std::find_if(hex4.scenes.begin(), hex4.scenes.end(),
                                       [&](const Scene &s) { return s.name == original.name; });
        REQUIRE(copy != hex4.scenes.end());
        REQUIRE(copy->layers.size() == original.layers.size());
        for (size_t k = 0; k < original.layers.size(); ++k) {
            const Layer &c = copy->layers[k];
            const Layer &o = original.layers[k];
            REQUIRE(c.spec.type() == o.spec.type());
            for (size_t i = 0; i < effect_param_infos(o.spec.type()).size(); ++i) {
                CHECK(effect_param(c.spec, i) == effect_param(o.spec, i));
            }
            CHECK(c.blend == o.blend);
            CHECK(c.opacity == o.opacity);
            CHECK(c.group == o.group);
        }
        CHECK(copy->routes == original.routes);
    }
    // Every energy of a groove and a drop has several scenes to rotate among, each of them
    // reachable (the rotation walks through all candidates).
    for (const char *label : {"groove", "drop"}) {
        for (int energy = 0; energy < 3; ++energy) {
            CAPTURE(label);
            CAPTURE(energy);
            std::vector<int> seen;
            for (size_t occurrence = 0; occurrence < 40; ++occurrence) {
                const int s = scene_for_label(hex4.scenes, label, occurrence, energy);
                if (std::find(seen.begin(), seen.end(), s) == seen.end()) {
                    seen.push_back(s);
                }
            }
            CHECK(seen.size() >= 2);
        }
    }
    // The original Hex tunnel and Hex radar take part in the groove rotation.
    std::vector<std::string> grooves;
    for (int energy = 0; energy < 3; ++energy) {
        for (size_t occurrence = 0; occurrence < 40; ++occurrence) {
            grooves.push_back(hex4.scenes[static_cast<size_t>(scene_for_label(hex4.scenes, "groove",
                                                                              occurrence, energy))]
                                  .name);
        }
    }
    for (const char *name :
         {"Hex tunnel", "Hex radar", "Tunnel 3", "Pitch tunnel", "Note rings", "Cube 3", "Lattice",
          "Radar 3", "Ring rain", "Star radar", "Hammer", "Comet rain"}) {
        CAPTURE(name);
        CHECK(std::find(grooves.begin(), grooves.end(), name) != grooves.end());
    }
    for (const char *label : {"intro", "outro"}) {
        CAPTURE(label);
        CHECK(scene_for_label(hex4.scenes, label, 0) != scene_for_label(hex4.scenes, label, 1));
    }
}
