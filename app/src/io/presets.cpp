#include "io/presets.h"

#include "core/paths.h"
#include "io/text_file.h"

#include <algorithm>
#include <fstream>
#include <system_error>

namespace psyight {

namespace {

const char *folder_of(PresetKind kind) {
    switch (kind) {
    case PresetKind::scene:
        return "scenes";
    case PresetKind::palette:
        return "palettes";
    case PresetKind::show:
        return "shows";
    case PresetKind::rig:
        return "rigs";
    }
    return "scenes";
}

// The .json files in dir/<folder>, sorted by name.
std::vector<std::filesystem::path> files_in(const std::filesystem::path &dir, PresetKind kind) {
    std::vector<std::filesystem::path> files;
    std::error_code error;
    const std::filesystem::path folder = dir / folder_of(kind);
    if (!std::filesystem::is_directory(folder, error)) {
        return files;
    }
    for (const auto &entry : std::filesystem::directory_iterator(folder, error)) {
        if (entry.is_regular_file(error) && entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

Result<void> write(const std::filesystem::path &path, const Result<std::string> &text) {
    if (!text) {
        return text.error();
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text.value();
    if (!out) {
        return Error{"cannot write " + path_to_utf8(path)};
    }
    return {};
}

int palette_index(Project &project, const Palette &palette) {
    for (size_t i = 0; i < project.palettes.size(); ++i) {
        if (project.palettes[i].name == palette.name) {
            return static_cast<int>(i);
        }
    }
    project.palettes.push_back(palette);
    return static_cast<int>(project.palettes.size()) - 1;
}

} // namespace

UserPresets load_user_presets(const std::filesystem::path &dir) {
    UserPresets out;
    const auto load = [&](PresetKind kind, auto parse, auto &into) {
        for (const std::filesystem::path &file : files_in(dir, kind)) {
            const Result<std::string> text = read_text_file(file);
            if (!text) {
                out.errors.push_back(text.error().message);
                continue;
            }
            auto parsed = parse(text.value());
            if (!parsed) {
                out.errors.push_back(path_to_utf8(file.filename()) + ": " + parsed.error().message);
                continue;
            }
            into.push_back(std::move(parsed.value()));
        }
    };
    load(PresetKind::scene, parse_scene_preset, out.scenes);
    load(PresetKind::palette, parse_palette_preset, out.palettes);
    load(PresetKind::show, parse_show_pack, out.shows);
    load(PresetKind::rig, parse_rig_preset, out.rigs);
    return out;
}

std::filesystem::path preset_path(const std::filesystem::path &dir, PresetKind kind,
                                  const std::string &name) {
    std::string file;
    for (const char c : name) {
        const bool unsafe = c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' ||
                            c == '|' || c == '?' || c == '*' || static_cast<unsigned char>(c) < 32;
        file += unsafe ? '_' : c;
    }
    while (!file.empty() && (file.back() == ' ' || file.back() == '.')) {
        file.pop_back(); // Windows drops them.
    }
    if (file.empty()) {
        file = "preset";
    }
    const std::u8string utf8(file.begin(), file.end());
    return dir / folder_of(kind) / std::filesystem::path(utf8 + u8".json");
}

Result<void> save_scene_preset(const std::filesystem::path &dir, const ScenePreset &preset) {
    return write(preset_path(dir, PresetKind::scene, preset.name), scene_preset_json(preset));
}

Result<void> save_palette_preset(const std::filesystem::path &dir, const Palette &palette) {
    return write(preset_path(dir, PresetKind::palette, palette.name), palette_preset_json(palette));
}

Result<void> save_show_pack(const std::filesystem::path &dir, const ShowPack &pack) {
    return write(preset_path(dir, PresetKind::show, pack.name), show_pack_json(pack));
}

Result<void> save_rig_preset(const std::filesystem::path &dir, const RigPreset &preset) {
    return write(preset_path(dir, PresetKind::rig, preset.name), rig_preset_json(preset));
}

int apply_scene_preset(Project &project, const ScenePreset &preset, int index) {
    Scene scene = preset.scene;
    scene.palette = palette_index(project, preset.palette);
    if (index >= 0 && index < static_cast<int>(project.scenes.size())) {
        project.scenes[static_cast<size_t>(index)] = std::move(scene);
        return index;
    }
    project.scenes.push_back(std::move(scene));
    return static_cast<int>(project.scenes.size()) - 1;
}

void apply_show_pack(Project &project, const ShowPack &pack) {
    if (pack.scenes.empty() || pack.palettes.empty()) {
        return;
    }
    project.scenes = pack.scenes;
    project.palettes = pack.palettes;
    project.automation = pack.automation;
    project.macro_names = pack.macro_names;
    project.show_pack = pack.name;
}

int add_palette(Project &project, const Palette &palette) {
    for (size_t i = 0; i < project.palettes.size(); ++i) {
        if (project.palettes[i].name == palette.name) {
            project.palettes[i].colours = palette.colours;
            return static_cast<int>(i);
        }
    }
    project.palettes.push_back(palette);
    return static_cast<int>(project.palettes.size()) - 1;
}

ScenePreset scene_preset_of(const Project &project, int index) {
    ScenePreset preset;
    preset.scene = project.scenes.at(static_cast<size_t>(index));
    preset.name = preset.scene.name;
    if (!project.palettes.empty()) {
        preset.palette = project.palettes[static_cast<size_t>(
            std::clamp(preset.scene.palette, 0, static_cast<int>(project.palettes.size()) - 1))];
    }
    preset.scene.palette = 0;
    return preset;
}

ShowPack show_pack_of(const Project &project, const std::string &name) {
    ShowPack pack;
    pack.name = name;
    pack.scenes = project.scenes;
    pack.palettes = project.palettes;
    pack.automation = project.automation;
    pack.macro_names = project.macro_names;
    return pack;
}

} // namespace psyight
