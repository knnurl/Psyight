// Sections edited by hand: from the labels to the engine's timeline, and through a recording.

#include "analysis/sections.h"
#include "engine/automation.h"
#include "engine/inputs.h"
#include "io/label_editor.h"
#include "io/labels.h"
#include "io/session_record.h"

#include <doctest/doctest.h>

#include <string>
#include <utility>
#include <vector>

using namespace psyight;

TEST_CASE("edited sections: from their starts, sorted, covering the track") {
    const std::vector<DetectedSection> s = sections_from_starts(
        {{30.0, "groove"}, {5.0, "intro"}, {60.0, "breakdown"}, {500.0, "drop"}}, 100.0);
    REQUIRE(s.size() == 3); // The start past the end is dropped.
    CHECK(s[0].label == "intro");
    CHECK(s[0].start_s == 0.0); // Stretched back to the start.
    CHECK(s[0].end_s == 30.0);
    CHECK(s[1].label == "groove");
    CHECK(s[2].end_s == 100.0);
    CHECK(s[2].confidence == 1.0);
}

TEST_CASE("edited sections: the engine follows the timeline instead of the detection") {
    TrackAnalysis analysis;
    analysis.duration_s = 100.0;
    analysis.sections = {{0.0, 100.0, "groove", 1.0}};
    const Grid grid{150.0, 0, 48000};
    SectionTimeline timeline = section_timeline(
        analysis, grid, sections_from_starts({{0.0, "groove"}, {40.0, "breakdown"}}, 100.0));
    CHECK_FALSE(timeline.phrase_starts_s.empty());
    CHECK(inputs_at(analysis, grid, 50.0).section == "groove");
    const EngineInputs in = inputs_at(analysis, grid, 50.0, &timeline);
    CHECK(in.section == "breakdown");
    CHECK(in.section_index == 1);
    // Scene automation picks the breakdown's scene from the timeline.
    std::vector<Scene> scenes(2);
    scenes[0].sections = {"groove"};
    scenes[1].sections = {"breakdown"};
    const SceneMix mix = automate(scenes, analysis, grid, in, {}, -1, &timeline);
    CHECK(mix.to == 1);
}

TEST_CASE("edited sections: the labels keep them and the flag, undoably") {
    LabelEditor editor;
    CHECK_FALSE(editor.labels().sections_edited);
    editor.set_sections({{40.0, "breakdown"}, {0.0, "groove"}}, true);
    REQUIRE(editor.labels().sections.size() == 2);
    CHECK(editor.labels().sections[0].label == "groove"); // Sorted.
    CHECK(editor.labels().sections_edited);
    const Result<Labels> back = parse_labels_json(to_labels_json(editor.labels()).value());
    REQUIRE(back.ok());
    CHECK(back.value().sections_edited);
    CHECK(back.value().sections == editor.labels().sections);
    CHECK(editor.undo());
    CHECK(editor.labels().sections.empty());
    CHECK_FALSE(editor.labels().sections_edited);
}

TEST_CASE("edited sections: a recording keeps them for replay") {
    RuntimeState runtime;
    runtime.sections = {{0.0, 40.0, "groove", 1.0}, {40.0, 100.0, "breakdown", 1.0}};
    const Result<std::string> line = params_line(3, Project{}, runtime);
    REQUIRE(line.ok());
    const Result<std::vector<ParamsEntry>> entries = parse_params_jsonl(line.value());
    REQUIRE(entries.ok());
    REQUIRE(entries.value().size() == 1);
    const std::vector<DetectedSection> &back = entries.value()[0].runtime.sections;
    REQUIRE(back.size() == 2);
    CHECK(back[1].label == "breakdown");
    CHECK(back[1].start_s == 40.0);
    CHECK(back[1].end_s == 100.0);
}
