#include "io/labels.h"

#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

using namespace psyight;

namespace {

// Values chosen to need full double precision to survive a round trip.
Labels make_awkward_labels() {
    Labels labels;
    labels.audio = "Occulus Drift.flac";
    labels.sample_rate = 44100;
    labels.grid = {159.97, 0.1 + 0.2};
    labels.regions = {{60.0, 120.0, true}, {130.0, 131.0 / 3.0 + 100.0, false}};
    labels.kicks = {{60.012}, {1.0 / 3.0}};
    labels.bass_notes = {{60.105, 60.171, 29, 0.0, false, false, 1.0},
                         {60.1995, 60.29375, 41, -12.5, true, true, 0.37}};
    labels.beats = {{60.0}};
    labels.downbeats = {{60.0}};
    labels.sections = {{0.0, "intro"}, {57.3, "groove"}};
    return labels;
}

} // namespace

TEST_CASE("labels: to_labels_json then parse_labels_json reproduces every field exactly") {
    const Labels original = make_awkward_labels();
    const Result<std::string> json = to_labels_json(original);
    REQUIRE(json.ok());
    const Result<Labels> parsed = parse_labels_json(json.value());
    REQUIRE(parsed.ok());
    CHECK(parsed.value() == original);
}

TEST_CASE("labels: parses the SPEC 6.10 example verbatim") {
    const Result<Labels> parsed = parse_labels_json(R"({
      "schema": 1,
      "audio": "Occulus Drift.flac",
      "sample_rate": 48000,
      "grid": { "bpm": 160.0, "first_downbeat_s": 0.000 },
      "regions": [ { "start_s": 60.0, "end_s": 120.0, "complete": true } ],
      "kicks": [ { "t_s": 60.012 } ],
      "bass_notes": [
        { "onset_s": 60.105, "offset_s": 60.171, "midi": 29, "cents": 0,
          "slide_in": false, "slide_out": false, "velocity": 1.0 }
      ],
      "beats": [], "downbeats": [],
      "sections": [ { "start_s": 0.0, "label": "intro" } ]
    })");
    REQUIRE(parsed.ok());
    const Labels &labels = parsed.value();
    CHECK(labels.audio == "Occulus Drift.flac");
    CHECK(labels.sample_rate == 48000);
    CHECK(labels.grid.bpm == 160.0);
    REQUIRE(labels.regions.size() == 1);
    CHECK(labels.regions[0] == LabelRegion{60.0, 120.0, true});
    REQUIRE(labels.kicks.size() == 1);
    CHECK(labels.kicks[0].t_s == 60.012);
    REQUIRE(labels.bass_notes.size() == 1);
    CHECK(labels.bass_notes[0] == BassNoteLabel{60.105, 60.171, 29, 0.0, false, false, 1.0});
    CHECK(labels.beats.empty());
    REQUIRE(labels.sections.size() == 1);
    CHECK(labels.sections[0].label == "intro");
}

TEST_CASE("labels: optional fields take their documented defaults") {
    const Result<Labels> parsed = parse_labels_json(R"({
      "schema": 1, "audio": "a.wav", "sample_rate": 48000,
      "grid": { "bpm": 160.0, "first_downbeat_s": 0.5 },
      "bass_notes": [ { "onset_s": 1.0, "offset_s": 1.05, "midi": 30 } ]
    })");
    REQUIRE(parsed.ok());
    const Labels &labels = parsed.value();
    CHECK(labels.regions.empty());
    CHECK(labels.kicks.empty());
    CHECK(labels.sections.empty());
    REQUIRE(labels.bass_notes.size() == 1);
    CHECK(labels.bass_notes[0] == BassNoteLabel{1.0, 1.05, 30, 0.0, false, false, 1.0});
}

TEST_CASE("labels: invalid input returns an error instead of throwing") {
    const char *const kCases[] = {
        "{ not json",
        R"({"schema": 2, "audio": "a.wav", "sample_rate": 48000,
            "grid": {"bpm": 160.0, "first_downbeat_s": 0.0}})",
        R"({"schema": 1, "sample_rate": 48000,
            "grid": {"bpm": 160.0, "first_downbeat_s": 0.0}})",
        R"({"schema": 1, "audio": "a.wav", "sample_rate": "48000",
            "grid": {"bpm": 160.0, "first_downbeat_s": 0.0}})",
        R"({"schema": 1, "audio": "a.wav", "sample_rate": 48000,
            "grid": {"bpm": 160.0, "first_downbeat_s": 0.0},
            "bass_notes": [ {"onset_s": 1.0, "midi": 30} ]})",
        "[]",
        // Fractional values in integer fields would be silently truncated.
        R"({"schema": 1.9, "audio": "a.wav", "sample_rate": 48000,
            "grid": {"bpm": 160.0, "first_downbeat_s": 0.0}})",
        R"({"schema": 1, "audio": "a.wav", "sample_rate": 48000.7,
            "grid": {"bpm": 160.0, "first_downbeat_s": 0.0}})",
        R"({"schema": 1, "audio": "a.wav", "sample_rate": 48000,
            "grid": {"bpm": 160.0, "first_downbeat_s": 0.0},
            "bass_notes": [ {"onset_s": 1.0, "offset_s": 1.1, "midi": 29.9} ]})",
        // A region must say whether it is complete.
        R"({"schema": 1, "audio": "a.wav", "sample_rate": 48000,
            "grid": {"bpm": 160.0, "first_downbeat_s": 0.0},
            "regions": [ {"start_s": 0.0, "end_s": 1.0} ]})",
    };
    for (const char *json : kCases) {
        CAPTURE(json);
        const Result<Labels> parsed = parse_labels_json(json);
        CHECK_FALSE(parsed.ok());
        if (!parsed.ok()) {
            CHECK_FALSE(parsed.error().message.empty());
        }
    }
}

TEST_CASE("labels: the labels file sits next to the audio with .labels.json appended") {
    CHECK(labels_path_for("data/Occulus Drift.flac") ==
          std::filesystem::path("data/Occulus Drift.flac.labels.json"));
    CHECK(labels_path_for("track.mp3") == std::filesystem::path("track.mp3.labels.json"));
}

TEST_CASE("labels: save and load round trip, and saving replaces an existing file") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "psyight_labels_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = labels_path_for(dir / "track.wav");
    {
        std::ofstream stale(path);
        stale << "stale contents";
    }

    const Labels original = make_awkward_labels();
    const Result<void> saved = save_labels(path, original);
    REQUIRE(saved.ok());
    const Result<Labels> loaded = load_labels(path);
    REQUIRE(loaded.ok());
    CHECK(loaded.value() == original);
    std::filesystem::path temp_path = path;
    temp_path += ".tmp";
    CHECK_FALSE(std::filesystem::exists(temp_path));

    CHECK_FALSE(load_labels(dir / "missing.labels.json").ok());
    std::filesystem::remove_all(dir);
}

TEST_CASE("labels: writing rejects non-finite numbers and invalid UTF-8 instead of throwing") {
    Labels nan_cents = make_awkward_labels();
    nan_cents.bass_notes[0].cents = std::nan("");
    CHECK_FALSE(to_labels_json(nan_cents).ok());
    Labels infinite_onset = make_awkward_labels();
    infinite_onset.kicks[0].t_s = std::numeric_limits<double>::infinity();
    CHECK_FALSE(to_labels_json(infinite_onset).ok());
    Labels bad_text = make_awkward_labels();
    bad_text.audio = "\xE9.wav"; // Latin-1, not UTF-8.
    const Result<std::string> json = to_labels_json(bad_text);
    CHECK_FALSE(json.ok());

    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "psyight_labels_invalid";
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / "x.labels.json";
    CHECK_FALSE(save_labels(path, nan_cents).ok());
    CHECK_FALSE(std::filesystem::exists(path));
    std::filesystem::remove_all(dir);
}

TEST_CASE("labels: a failed save leaves the existing labels file untouched") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "psyight_labels_atomic";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / "track.wav.labels.json";
    {
        std::ofstream existing(path, std::ios::binary);
        existing << "hand-made labels";
    }
    // A directory where the temporary file must go makes the write fail.
    std::filesystem::path temp_path = path;
    temp_path += ".tmp";
    std::filesystem::create_directories(temp_path);

    CHECK_FALSE(save_labels(path, make_awkward_labels()).ok());
    std::ifstream check(path, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(check)),
                               std::istreambuf_iterator<char>());
    CHECK(contents == "hand-made labels");
    check.close();
    std::filesystem::remove_all(dir);
}