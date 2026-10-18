// The built-in preset library (compiled in, read-only): scene presets (a look: layers, settings,
// modulation, section tags and its palette), palettes, and show packs (a whole show style: scenes,
// palettes, automation and macro names). The user's own presets are files (io/presets.h).
//
// Every palette keeps white out (SPEC 7.5: white is for kicks and drops). Scene presets carry
// their own palette, so loading one into any project gives the same look.
#pragma once

#include "engine/automation.h"
#include "engine/colour.h"
#include "engine/inputs.h"
#include "engine/scene.h"

#include <array>
#include <string>
#include <vector>

namespace psyight {

struct ScenePreset {
    std::string name;
    std::string description; // One line for the browser.
    Scene scene;             // Its palette index is 0: `palette` below.
    Palette palette;
};

struct ShowPack {
    std::string name;
    std::string description;
    std::vector<Scene> scenes; // Palette indices into `palettes`.
    std::vector<Palette> palettes;
    AutomationParams automation;
    std::array<std::string, kMacroCount> macro_names{};
};

std::vector<ScenePreset> builtin_scene_presets();
std::vector<Palette> builtin_palettes(); // The default palettes, then more.
std::vector<ShowPack> builtin_show_packs();

} // namespace psyight
