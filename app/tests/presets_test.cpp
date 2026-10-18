// The preset library (engine/presets.h) and preset files (io/presets.h).

#include "engine/effect_spec.h"
#include "engine/presets.h"
#include "io/presets.h"

#include <doctest/doctest.h>

#include <algorithm>
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
