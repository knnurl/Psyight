#include "io/labels.h"

#include "core/paths.h"
#include "io/text_file.h"

#include <nlohmann/json.hpp>

#include <cmath>

namespace psyight {

namespace {

using nlohmann::json;

// nlohmann/json reports type and lookup errors by exception, and so do the checks below; all are
// caught at the API boundary and turned into Result errors.
struct FieldError {
    std::string message;
};

// get<int>() silently truncates 1.9 to 1; integer fields must be written as integers.
int integer_field(const json &object, const char *key) {
    const json &value = object.at(key);
    if (!value.is_number_integer()) {
        throw FieldError{std::string("field ") + key + " must be an integer"};
    }
    return value.get<int>();
}

// JSON has no NaN or infinity; nlohmann would write null and the file would not load again.
double finite(double value, const char *field) {
    if (!std::isfinite(value)) {
        throw FieldError{std::string("field ") + field + " is not a finite number"};
    }
    return value;
}

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
        array.push_back({{"t_s", finite(event.t_s, "t_s")}});
    }
    return array;
}

} // namespace

Result<Labels> parse_labels_json(std::string_view json_text) {
    try {
        const json root = json::parse(json_text);
        const int schema = integer_field(root, "schema");
        if (schema != kLabelsSchemaVersion) {
            return Error{"unsupported labels schema " + std::to_string(schema) + ", expected " +
                         std::to_string(kLabelsSchemaVersion)};
        }
        Labels labels;
        labels.audio = root.at("audio").get<std::string>();
        labels.sample_rate = integer_field(root, "sample_rate");
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
                note.midi = integer_field(item, "midi");
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
    } catch (const FieldError &e) {
        return Error{"invalid labels JSON: " + e.message};
    }
}

Result<std::string> to_labels_json(const Labels &labels) {
    try {
        json regions = json::array();
        for (const LabelRegion &region : labels.regions) {
            regions.push_back({{"start_s", finite(region.start_s, "start_s")},
                               {"end_s", finite(region.end_s, "end_s")},
                               {"complete", region.complete}});
        }
        json notes = json::array();
        for (const BassNoteLabel &note : labels.bass_notes) {
            notes.push_back({{"onset_s", finite(note.onset_s, "onset_s")},
                             {"offset_s", finite(note.offset_s, "offset_s")},
                             {"midi", note.midi},
                             {"cents", finite(note.cents, "cents")},
                             {"slide_in", note.slide_in},
                             {"slide_out", note.slide_out},
                             {"velocity", finite(note.velocity, "velocity")}});
        }
        json sections = json::array();
        for (const SectionLabel &section : labels.sections) {
            sections.push_back(
                {{"start_s", finite(section.start_s, "start_s")}, {"label", section.label}});
        }
        const json root = {
            {"schema", kLabelsSchemaVersion},
            {"audio", labels.audio},
            {"sample_rate", labels.sample_rate},
            {"grid",
             {{"bpm", finite(labels.grid.bpm, "bpm")},
              {"first_downbeat_s", finite(labels.grid.first_downbeat_s, "first_downbeat_s")}}},
            {"regions", regions},
            {"kicks", events_to_json(labels.kicks)},
            {"bass_notes", notes},
            {"beats", events_to_json(labels.beats)},
            {"downbeats", events_to_json(labels.downbeats)},
            {"sections", sections},
        };
        return root.dump(2) + "\n"; // Throws on strings that are not valid UTF-8.
    } catch (const json::exception &e) {
        return Error{std::string("cannot write labels: ") + e.what()};
    } catch (const FieldError &e) {
        return Error{"cannot write labels: " + e.message};
    }
}

std::filesystem::path labels_path_for(const std::filesystem::path &audio_path) {
    std::filesystem::path path = audio_path;
    path += ".labels.json";
    return path;
}

Result<Labels> load_labels(const std::filesystem::path &path) {
    const Result<std::string> contents = read_text_file(path);
    if (!contents) {
        return contents.error();
    }
    Result<Labels> labels = parse_labels_json(contents.value());
    if (!labels) {
        return Error{path_to_utf8(path) + ": " + labels.error().message};
    }
    return labels;
}

Result<void> save_labels(const std::filesystem::path &path, const Labels &labels) {
    // Atomic, so a failed write never truncates existing hand-made labels.
    const Result<std::string> json_text = to_labels_json(labels);
    if (!json_text) {
        return json_text.error();
    }
    return write_text_file_atomic(path, json_text.value());
}

} // namespace psyight
