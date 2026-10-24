// The labelling mode's model: phrase grid and done flag in the labels, section marking, and the
// track list's statuses.

#include "io/label_editor.h"
#include "io/labels.h"
#include "io/section_marks.h"
#include "io/track_library.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

using namespace psyight;

namespace {

// 150 BPM: a bar is 1.6 s; the first downbeat at 0.5 s.
LabelGrid grid150() { return {150.0, 0.5}; }

std::filesystem::path fresh_dir(const char *name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

} // namespace

TEST_CASE("labels: the phrase grid and the done flag round-trip; older files take defaults") {
    Labels l;
    l.audio = "a.mp3";
    l.phrases = {true, 13.3, 16};
    l.sections_done = true;
    l.sections = {{0.0, "intro"}, {13.3, "groove"}};
    l.sections_edited = true;
    const Result<std::string> text = to_labels_json(l);
    REQUIRE(text.ok());
    const Result<Labels> back = parse_labels_json(text.value());
    REQUIRE(back.ok());
    CHECK(back.value() == l);
    Labels plain;
    plain.audio = "b.mp3";
    const Result<Labels> old = parse_labels_json(to_labels_json(plain).value());
    REQUIRE(old.ok());
    CHECK_FALSE(old.value().phrases.set);
    CHECK_FALSE(old.value().sections_done);
    CHECK_FALSE(parse_labels_json(R"({"schema": 1, "audio": "x", "sample_rate": 48000,
        "grid": {"bpm": 150, "first_downbeat_s": 0}, "phrases": {"start_s": 1, "bars": 0}})")
                    .ok());
}

TEST_CASE("labels: phrases and done are undoable edits, and shift with everything else") {
    LabelEditor editor;
    editor.set_phrases({true, 5.0, 8});
    editor.set_sections_done(true);
    CHECK(editor.labels().phrases.start_s == 5.0);
    CHECK(editor.labels().sections_done);
    CHECK(editor.dirty());
    REQUIRE(editor.undo());
    CHECK_FALSE(editor.labels().sections_done);
    REQUIRE(editor.undo());
    CHECK_FALSE(editor.labels().phrases.set);
    REQUIRE(editor.redo());
    CHECK(editor.shift_all(0.25) == doctest::Approx(0.25));
    CHECK(editor.labels().phrases.start_s == doctest::Approx(5.25));
}

TEST_CASE("section marks: bars, phrases and where a time falls") {
    const LabelGrid g = grid150();
    CHECK(bar_length_s(g) == doctest::Approx(1.6));
    CHECK(snap_to_bar(g, 2.0) == doctest::Approx(2.1));
    CHECK(snap_to_bar(g, 2.8) == doctest::Approx(2.1));
    CHECK(snap_to_bar(g, 3.0) == doctest::Approx(3.7));
    CHECK(bar_index_at(g, 0.5) == 0);
    CHECK(bar_index_at(g, 2.09) == 0);
    CHECK(bar_index_at(g, 2.1) == 1);
    CHECK(bar_index_at(g, 0.0) == -1);
    const LabelPhrases phrases{true, 0.5 + 2 * 1.6, 8}; // Phrases start at bar 2 (from 0).
    CHECK(bar_in_phrase(g, phrases, 0.5 + 2 * 1.6) == 0);
    CHECK(bar_in_phrase(g, phrases, 0.5 + 9.5 * 1.6) == 7);
    CHECK(bar_in_phrase(g, phrases, 0.5 + 1 * 1.6) == 7); // Before the labelled start.
    CHECK(snap_to_phrase(g, phrases, 0.5 + 7 * 1.6) == doctest::Approx(0.5 + 10 * 1.6));
    CHECK(snap_to_phrase(g, phrases, 0.5 + 5 * 1.6) == doctest::Approx(0.5 + 2 * 1.6));
    CHECK(snap_to_phrase(g, LabelPhrases{}, 2.0) == doctest::Approx(2.1)); // No grid: bars.
    CHECK(bar_in_phrase(g, LabelPhrases{}, 3.0) == -1);
}

TEST_CASE("section marks: marking relabels near boundaries, adds new ones, keeps order") {
    std::vector<SectionLabel> s = mark_section({}, 0.0, "intro", 0.8);
    REQUIRE(s.size() == 1);
    CHECK(s[0].start_s == 0.0);
    s = mark_section(s, 20.0, "groove", 0.8);
    s = mark_section(s, 60.0, "breakdown", 0.8);
    s = mark_section(s, 40.0, "groove", 0.8); // A groove that steps up: two grooves.
    REQUIRE(s.size() == 4);
    CHECK(s[1].start_s == 20.0);
    CHECK(s[2].start_s == 40.0);
    CHECK(s[3].label == "breakdown");
    s = mark_section(s, 60.5, "build", 0.8); // Within tolerance: relabel.
    REQUIRE(s.size() == 4);
    CHECK(s[3].label == "build");
    CHECK(s[3].start_s == 60.0);
    s = mark_section(s, 0.3, "drop", 0.8); // Near the start: the first section.
    CHECK(s[0].label == "drop");
    // Without sections, a mark later in the track leaves an intro before it.
    const std::vector<SectionLabel> fresh = mark_section({}, 30.0, "groove", 0.8);
    REQUIRE(fresh.size() == 2);
    CHECK(fresh[0].label == "intro");
    CHECK(fresh[1].label == "groove");
}

TEST_CASE("section marks: removing and moving boundaries; neighbours and lookups") {
    std::vector<SectionLabel> s = {{0.0, "intro"}, {20.0, "groove"}, {40.0, "breakdown"}};
    CHECK(remove_boundary(s, 30.0, 1.6).size() == 3); // None near.
    std::vector<SectionLabel> r = remove_boundary(s, 39.0, 1.6);
    REQUIRE(r.size() == 2);
    CHECK(r[1].label == "groove");
    CHECK(remove_boundary(s, 0.0, 1.6).size() == 3);              // Never the first.
    std::vector<SectionLabel> m = move_boundary(s, 1, 45.0, 1.6); // Kept before the next.
    CHECK(m[1].start_s == doctest::Approx(38.4));
    m = move_boundary(s, 1, -5.0, 1.6);
    CHECK(m[1].start_s == doctest::Approx(1.6));
    CHECK(move_boundary(s, 0, 5.0, 1.6) == s);
    CHECK(previous_boundary(s, 20.0).value() == 0.0);
    CHECK(previous_boundary(s, 25.0).value() == 20.0);
    CHECK_FALSE(previous_boundary(s, 0.0).has_value());
    CHECK(next_boundary(s, 20.0).value() == 40.0);
    CHECK_FALSE(next_boundary(s, 40.0).has_value());
    CHECK(section_at(s, 19.9) == 0);
    CHECK(section_at(s, 20.0) == 1);
    CHECK(section_at(s, 99.0) == 2);
}

TEST_CASE("section marks: the detection as labels, snapped to bars") {
    const LabelGrid g = grid150();
    const std::vector<DetectedSection> detected = {{0.0, 13.3, "intro", 1.0},
                                                   {13.3, 29.5, "groove", 1.0},
                                                   {29.5, 29.9, "breakdown", 1.0},
                                                   {29.9, 60.0, "build", 1.0}};
    const std::vector<SectionLabel> s = sections_from_detection(detected, g);
    // 13.3 snaps to 13.3 (bar 8: 0.5 + 8 * 1.6); 29.5 and 29.9 both to 29.3 (bar 18): the later
    // label wins.
    REQUIRE(s.size() == 3);
    CHECK(s[0].start_s == 0.0);
    CHECK(s[1].start_s == doctest::Approx(13.3));
    CHECK(s[2].start_s == doctest::Approx(29.3));
    CHECK(s[2].label == "build");
}

TEST_CASE("track library: folders scanned with each track's labelling status") {
    const std::filesystem::path dir = fresh_dir("psyight_track_library_test");
    std::filesystem::create_directories(dir / "album");
    std::ofstream(dir / "a.mp3") << "x";
    std::ofstream(dir / "album" / "b.WAV") << "x";
    std::ofstream(dir / "c.flac") << "x";
    std::ofstream(dir / "notes.txt") << "x";
    Labels done;
    done.audio = "a.mp3";
    done.sections = {{0.0, "intro"}, {10.0, "groove"}};
    done.sections_edited = true;
    done.sections_done = true;
    REQUIRE(save_labels(labels_path_for(dir / "a.mp3"), done).ok());
    Labels started;
    started.audio = "b.WAV";
    started.phrases = {true, 1.0, 8};
    REQUIRE(save_labels(labels_path_for(dir / "album" / "b.WAV"), started).ok());
    Labels notes_only; // Bass labels, no sections: not started for sections.
    notes_only.audio = "c.flac";
    notes_only.bass_notes.push_back({1.0, 1.1, 28});
    REQUIRE(save_labels(labels_path_for(dir / "c.flac"), notes_only).ok());

    // The folder twice: each track once.
    const std::vector<TrackEntry> tracks = scan_track_folders({dir, dir});
    REQUIRE(tracks.size() == 3);
    CHECK(tracks[0].name == "a.mp3");
    CHECK(tracks[0].status == LabelStatus::done);
    CHECK(tracks[0].sections == 2);
    CHECK(tracks[1].name == "b.WAV");
    CHECK(tracks[1].folder == "album");
    CHECK(tracks[1].status == LabelStatus::started);
    CHECK(tracks[2].name == "c.flac");
    CHECK(tracks[2].status == LabelStatus::none);
    CHECK(next_unfinished(tracks, 0).value() == 1);
    CHECK(next_unfinished(tracks, 2).value() == 1); // Wraps past the done one.
    CHECK(next_unfinished(tracks, tracks.size()).value() == 1);
    std::vector<TrackEntry> all_done = tracks;
    for (TrackEntry &t : all_done) {
        t.status = LabelStatus::done;
    }
    CHECK_FALSE(next_unfinished(all_done, 0).has_value());
    CHECK(scan_track_folders({dir / "missing"}).empty());
    std::filesystem::remove_all(dir);
}

TEST_CASE("section marks: scoring detected sections against labelled ones") {
    const LabelGrid g{150.0, 0.0}; // Bars of 1.6 s from 0.
    const std::vector<SectionLabel> labelled = {
        {0.0, "intro"}, {16.0, "groove"}, {48.0, "breakdown"}, {64.0, "drop"}};
    // One boundary a bar off (in tolerance), one two bars off (missed), one extra.
    const std::vector<SectionLabel> detected = {
        {0.0, "intro"}, {17.6, "groove"}, {32.0, "groove"}, {51.2, "breakdown"}, {64.0, "drop"}};
    const SectionScore s = score_sections(labelled, detected, g, 80.0);
    CHECK(s.labelled_boundaries == 3);
    CHECK(s.detected_boundaries == 4);
    CHECK(s.matched == 2); // 16.0 to 17.6 and 64.0; 48.0 to 51.2 is two bars.
    CHECK(s.precision() == doctest::Approx(0.5));
    CHECK(s.recall() == doctest::Approx(2.0 / 3.0));
    CHECK(s.f1() == doctest::Approx(4.0 / 7.0));
    // 50 bars; they differ on the groove bar at 16 to 17.6 and the two at 48 to 51.2.
    CHECK(s.bars == 50);
    CHECK(s.bars_agreeing == 47);
    const SectionScore perfect = score_sections(labelled, labelled, g, 80.0);
    CHECK(perfect.f1() == doctest::Approx(1.0));
    CHECK(perfect.bar_accuracy() == doctest::Approx(1.0));
    SectionScore sum = s;
    sum += perfect;
    CHECK(sum.matched == 5);
    CHECK(sum.bars == 100);
}

TEST_CASE("labelling: bar 1 may lie before the audio; analyses start at the first bar in it") {
    // 192 BPM (1.25 s bars) with bar 27 at 31.6 s: bar 1 at -0.9 s.
    const LabelGrid grid{192.0, 31.6 - 26 * 1.25};
    CHECK(bar_index_at(grid, 31.6) + 1 == 27);
    CHECK(bar_index_at(grid, 0.0) + 1 == 1);
    CHECK(snap_to_bar(grid, 31.5) == doctest::Approx(31.6));
    CHECK(first_downbeat_in_audio_s(grid) == doctest::Approx(0.35));
    const Grid g = analysis_grid(grid, 48000);
    CHECK(g.first_downbeat == 16800);                                         // 0.35 s.
    CHECK(first_downbeat_in_audio_s({192.0, 0.35}) == doctest::Approx(0.35)); // Already in it.
    CHECK(first_downbeat_in_audio_s({192.0, -1.25}) == doctest::Approx(0.0)); // On 0 s exactly.
}
