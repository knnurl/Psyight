// Compatibility goldens (docs/PLAN_HEX_VISUALS.md 1.1, 4.5 and 11): the built-in scene presets,
// show packs and their variations, serialised as a project file holds them, keep every setting
// they had when tests/golden/presets_baseline.json was written (before the hexagon-visuals
// changes that could reach them). Settings added since (new keys) are not compared, and presets
// or packs added since are not in the file. Rewrite only for an intended change:
//   set PSYIGHT_UPDATE_GOLDEN=1 && psyight_tests -tc="golden*"

#include "engine/presets.h"
#include "engine/variations.h"
#include "io/project.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace psyight;
using nlohmann::json;

namespace {

std::filesystem::path golden_path() {
    return std::filesystem::path(PSYIGHT_TESTS_DIR) / "golden" / "presets_baseline.json";
}

bool update_requested() {
    char *value = nullptr;
    size_t length = 0;
    const bool set = _dupenv_s(&value, &length, "PSYIGHT_UPDATE_GOLDEN") == 0 && value != nullptr &&
                     std::string(value) == "1";
    std::free(value);
    return set;
}

json as_project(const std::vector<Scene> &scenes, const std::vector<Palette> &palettes,
                const AutomationParams &automation) {
    Project p = default_project();
    p.scenes = scenes;
    p.palettes = palettes;
    p.automation = automation;
    const json all = json::parse(to_project_json(p).value());
    return {{"scenes", all.at("scenes")},
            {"palettes", all.at("palettes")},
            {"automation", all.at("automation")}};
}

json current() {
    json out;
    for (const ScenePreset &preset : builtin_scene_presets()) {
        out["scene_presets"][preset.name] = as_project({preset.scene}, {preset.palette}, {});
    }
    for (const ShowPack &pack : builtin_show_packs()) {
        out["show_packs"][pack.name] = as_project(pack.scenes, pack.palettes, pack.automation);
        std::vector<Scene> variants;
        for (const std::vector<Scene> &scene :
             all_variations(pack.scenes, pack.automation.vary, 1)) {
            variants.insert(variants.end(), scene.begin(), scene.end());
        }
        out["variations"][pack.name] = as_project(variants, pack.palettes, pack.automation);
    }
    return out;
}

// Every value in `golden` is at the same place in `now`; keys only in `now` are new settings.
void check_kept(const json &golden, const json &now, const std::string &at) {
    if (golden.is_object()) {
        if (!now.is_object()) {
            CHECK_MESSAGE(false, at << " is no longer an object");
            return;
        }
        for (const auto &[key, value] : golden.items()) {
            if (!now.contains(key)) {
                CHECK_MESSAGE(false, at << "/" << key << " is gone");
                continue;
            }
            check_kept(value, now.at(key), at + "/" + key);
        }
    } else if (golden.is_array()) {
        if (!now.is_array() || now.size() != golden.size()) {
            CHECK_MESSAGE(false, at << ": the list changed length");
            return;
        }
        for (size_t i = 0; i < golden.size(); ++i) {
            check_kept(golden[i], now[i], at + "/" + std::to_string(i));
        }
    } else {
        CHECK_MESSAGE(golden == now, at << ": " << golden.dump() << " is now " << now.dump());
    }
}

} // namespace

TEST_CASE("golden presets: built-in scene presets, show packs and their variations keep every "
          "setting they had") {
    const json now = current();
    if (update_requested()) {
        std::filesystem::create_directories(golden_path().parent_path());
        std::ofstream(golden_path()) << now.dump(1) << "\n";
        MESSAGE("wrote " << golden_path().string());
        return;
    }
    std::ifstream file(golden_path());
    REQUIRE_MESSAGE(file.good(), "missing " << golden_path().string()
                                            << "; set PSYIGHT_UPDATE_GOLDEN=1 to create it");
    check_kept(json::parse(file), now, "");
}
