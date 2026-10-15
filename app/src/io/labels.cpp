#include "io/labels.h"

#include "core/paths.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <system_error>

namespace psyight {

namespace {

using nlohmann::json;

// nlohmann/json reports type and lookup errors by exception; they are caught at the API boundary
// below and turned into Result errors.
template <typename T> T optional_field(const json &object, const char *key, T fallback) {
    const auto it = object.find(key);
    return it == object.end() ? fallback : it->get<T>();
}

std::vector<LabelEvent> parse_events(const json &root, const char *key) {
    std::vector<LabelEvent> events;
    if (const auto it = root.find(key); it != root.end()) {
        for (const json &item : it->get_ref<const json::array_t &>()) {
            events.push_back({item.at("t_s").get<double>()});
        }
    }
    return events;
}

json events_to_json(const std::vector<LabelEvent> &events) {
    json array = json::array();
    for (const LabelEvent &event : events) {
        array.push_back({{"t_s", event.t_s}});
    }
    return array;
}

} // namespace

Result<Labels> parse_labels_json(std::string_view json_text) {
    try {
        const json root = json::parse(json_text);
        const int schema = root.at("schema").get<int>();
        if (schema != kLabelsSchemaVersion) {
            return Error{"unsupported labels schema " + std::to_string(schema) + ", expected " +
                         std::to_string(kLabelsSchemaVersion)};
        }
        Labels labels;
        labels.audio = root.at("audio").get<std::string>();
        labels.sample_rate = root.at("sample_rate").get<int>();
        const json &grid = root.at("grid");
        labels.grid.bpm = grid.at("bpm").get<double>();
        labels.grid.first_downbeat_s = grid.at("first_downbeat_s").get<double>();
        if (const auto it = root.find("regions"); it != root.end()) {
            for (const json &item : it->get_ref<const json::array_t &>()) {
                labels.regions.push_back({item.at("start_s").get<double>(),
                                          item.at("end_s").get<double>(),
                                          item.at("complete").get<bool>()});
            }
        }
        labels.kicks = parse_events(root, "kicks");
        if (const auto it = root.find("bass_notes"); it != root.end()) {
            for (const json &item : it->get_ref<const json::array_t &>()) {
                BassNoteLabel note;
                note.onset_s = item.at("onset_s").get<double>();
                note.offset_s = item.at("offset_s").get<double>();
                note.midi = item.at("midi").get<int>();
                note.cents = optional_field(item, "cents", 0.0);
                note.slide_in = optional_field(item, "slide_in", false);
                note.slide_out = optional_field(item, "slide_out", false);
                note.velocity = optional_field(item, "velocity", 1.0);
                labels.bass_notes.push_back(note);
            }
        }
        labels.beats = parse_events(root, "beats");
        labels.downbeats = parse_events(root, "downbeats");
        if (const auto it = root.find("sections"); it != root.end()) {
            for (const json &item : it->get_ref<const json::array_t &>()) {
                labels.sections.push_back(
                    {item.at("start_s").get<double>(), item.at("label").get<std::string>()});
            }
        }
        return labels;
    } catch (const json::exception &e) {
        return Error{std::string("invalid labels JSON: ") + e.what()};
    }
}

std::string to_labels_json(const Labels &labels) {
    json regions = json::array();
    for (const LabelRegion &region : labels.regions) {
        regions.push_back(
            {{"start_s", region.start_s}, {"end_s", region.end_s}, {"complete", region.complete}});
    }
    json notes = json::array();
    for (const BassNoteLabel &note : labels.bass_notes) {
        notes.push_back({{"onset_s", note.onset_s},
                         {"offset_s", note.offset_s},
                         {"midi", note.midi},
                         {"cents", note.cents},
                         {"slide_in", note.slide_in},
                         {"slide_out", note.slide_out},
                         {"velocity", note.velocity}});
    }
    json sections = json::array();
    for (const SectionLabel &section : labels.sections) {
        sections.push_back({{"start_s", section.start_s}, {"label", section.label}});
    }
    const json root = {
        {"schema", kLabelsSchemaVersion},
        {"audio", labels.audio},
        {"sample_rate", labels.sample_rate},
        {"grid", {{"bpm", labels.grid.bpm}, {"first_downbeat_s", labels.grid.first_downbeat_s}}},
        {"regions", regions},
        {"kicks", events_to_json(labels.kicks)},
        {"bass_notes", notes},
        {"beats", events_to_json(labels.beats)},
        {"downbeats", events_to_json(labels.downbeats)},
        {"sections", sections},
    };
    return root.dump(2) + "\n";
}

std::filesystem::path labels_path_for(const std::filesystem::path &audio_path) {
    std::filesystem::path path = audio_path;
    path += ".labels.json";
    return path;
}

Result<Labels> load_labels(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return Error{"cannot open " + path_to_utf8(path)};
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    Result<Labels> labels = parse_labels_json(contents.str());
    if (!labels) {
        return Error{path_to_utf8(path) + ": " + labels.error().message};
    }
    return labels;
}

Result<void> save_labels(const std::filesystem::path &path, const Labels &labels) {
    // Write to a temporary file, then rename over the target, so a failed write never truncates
    // existing hand-made labels.
    std::filesystem::path temp_path = path;
    temp_path += ".tmp";
    {
        std::ofstream file(temp_path, std::ios::binary | std::ios::trunc);
        if (!file) {
            return Error{"cannot write " + path_to_utf8(temp_path)};
        }
        file << to_labels_json(labels);
        file.close();
        if (!file) {
            return Error{"failed writing " + path_to_utf8(temp_path)};
        }
    }
    std::error_code error;
    std::filesystem::rename(temp_path, path, error);
    if (error) {
        const std::string reason = error.message();
        std::filesystem::remove(temp_path, error);
        return Error{"cannot replace " + path_to_utf8(path) + ": " + reason};
    }
    return {};
}

} // namespace psyight
