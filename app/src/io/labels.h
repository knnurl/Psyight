// Ground-truth labels file, schema 1 (SPEC section 6.10): `<audio filename>.labels.json`, saved
// next to the audio. All times are in seconds from the start of the audio file.
#pragma once

#include "core/result.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace psyight {

inline constexpr int kLabelsSchemaVersion = 1;

struct LabelGrid {
    double bpm = 160.0;
    double first_downbeat_s = 0.0;

    bool operator==(const LabelGrid &) const = default;
};

// A region marked complete has every event inside it labelled, so missed detections count.
struct LabelRegion {
    double start_s = 0.0;
    double end_s = 0.0;
    bool complete = false;

    bool operator==(const LabelRegion &) const = default;
};

// One timestamped event: a kick, beat or downbeat.
struct LabelEvent {
    double t_s = 0.0;

    bool operator==(const LabelEvent &) const = default;
};

struct BassNoteLabel {
    double onset_s = 0.0;
    double offset_s = 0.0;
    int midi = 0;
    double cents = 0.0;
    bool slide_in = false;
    bool slide_out = false;
    double velocity = 1.0;

    bool operator==(const BassNoteLabel &) const = default;
};

struct SectionLabel {
    double start_s = 0.0;
    std::string label;

    bool operator==(const SectionLabel &) const = default;
};

struct Labels {
    std::string audio; // Audio file name, without directory.
    int sample_rate = 48000;
    LabelGrid grid;
    std::vector<LabelRegion> regions;
    std::vector<LabelEvent> kicks;
    std::vector<BassNoteLabel> bass_notes;
    std::vector<LabelEvent> beats;
    std::vector<LabelEvent> downbeats;
    std::vector<SectionLabel> sections;

    bool operator==(const Labels &) const = default;
};

// Parses labels JSON. Fails on malformed JSON, a schema other than kLabelsSchemaVersion, or a
// missing or wrongly typed field (integer fields must not have a fractional part). Optional fields:
// slide_in, slide_out (false), velocity (1.0), cents (0) and every event list (empty).
Result<Labels> parse_labels_json(std::string_view json);

// Serialises labels as schema-1 JSON. Fails on a non-finite number or a string that is not valid
// UTF-8. Otherwise parse_labels_json reproduces the labels exactly.
Result<std::string> to_labels_json(const Labels &labels);

// "<dir>/<name>.<ext>" becomes "<dir>/<name>.<ext>.labels.json".
std::filesystem::path labels_path_for(const std::filesystem::path &audio_path);

Result<Labels> load_labels(const std::filesystem::path &path);
Result<void> save_labels(const std::filesystem::path &path, const Labels &labels);

} // namespace psyight
