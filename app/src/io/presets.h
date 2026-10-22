// Preset files and applying presets (SPEC 7.6): scene presets, palettes and show packs
// (engine/presets.h), and rig presets (engine/rig_templates.h), as small JSON files. The built-in
// library is compiled in; the user's own presets live in a folder in the working directory:
// presets/scenes, presets/palettes, presets/shows and presets/rigs, one file each, named after
// the preset. The formats share the project file's scene, palette, automation and strip JSON
// (io/project.cpp implements them, as one schema). A rig preset keeps the strips' geometry and
// groups; colour orders stay the project's.
#pragma once

#include "core/result.h"
#include "engine/presets.h"
#include "engine/rig_templates.h"
#include "io/project.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace psyight {

Result<std::string> scene_preset_json(const ScenePreset &preset);
Result<ScenePreset> parse_scene_preset(std::string_view text);
Result<std::string> palette_preset_json(const Palette &palette);
Result<Palette> parse_palette_preset(std::string_view text);
Result<std::string> show_pack_json(const ShowPack &pack);
Result<ShowPack> parse_show_pack(std::string_view text);
Result<std::string> rig_preset_json(const RigPreset &preset);
Result<RigPreset> parse_rig_preset(std::string_view text);

enum class PresetKind { scene, palette, show, rig };

// The user's presets in `dir`, sorted by name. Unreadable files are skipped and named in errors.
struct UserPresets {
    std::vector<ScenePreset> scenes;
    std::vector<Palette> palettes;
    std::vector<ShowPack> shows;
    std::vector<RigPreset> rigs;
    std::vector<std::string> errors;
};
UserPresets load_user_presets(const std::filesystem::path &dir);

// Where a preset of this kind and name is saved: dir/scenes/<name>.json and so on, with
// characters that file names cannot hold replaced.
std::filesystem::path preset_path(const std::filesystem::path &dir, PresetKind kind,
                                  const std::string &name);
// Writes the preset there (making the folders), replacing a file of the same name.
Result<void> save_scene_preset(const std::filesystem::path &dir, const ScenePreset &preset);
Result<void> save_palette_preset(const std::filesystem::path &dir, const Palette &palette);
Result<void> save_show_pack(const std::filesystem::path &dir, const ShowPack &pack);
Result<void> save_rig_preset(const std::filesystem::path &dir, const RigPreset &preset);

// Applying presets to a project.
// The scene `index` replaced by the preset's (or the preset added at the end when index is out of
// range); its palette is the project's of the same name, or the preset's added. Returns the
// scene's index.
int apply_scene_preset(Project &project, const ScenePreset &preset, int index);
// Scenes, palettes, automation and macro names from the pack (macro values stay).
void apply_show_pack(Project &project, const ShowPack &pack);
// Adds the palette, or replaces the colours of one with the same name. Returns its index.
int add_palette(Project &project, const Palette &palette);
// Presets made from the project, to save.
ScenePreset scene_preset_of(const Project &project, int index);
ShowPack show_pack_of(const Project &project, const std::string &name);

} // namespace psyight
