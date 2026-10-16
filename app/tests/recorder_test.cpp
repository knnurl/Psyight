// Session recording: file formats, and the recorder writing them from its own thread.

#include "io/session_record.h"
#include "io/text_file.h"
#include "output/recorder.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <memory>

using namespace psyight;

namespace {

RecordedFrame frame(std::int64_t index, int pixels) {
    RecordedFrame f;
    f.t_s = 1.0 + static_cast<double>(index) / 60.0;
    f.index = index;
    f.config_version = 3 + static_cast<std::uint64_t>(index / 50);
    f.flags = index % 7 == 0 ? kFrameHold : 0;
    for (int i = 0; i < 3 * pixels; ++i) {
        f.rgb.push_back(static_cast<std::uint8_t>(i * 5 + index));
    }
    return f;
}

void check_same(const RecordedFrame &a, const RecordedFrame &b) {
    CHECK(a.t_s == b.t_s);
    CHECK(a.index == b.index);
    CHECK(a.config_version == b.config_version);
    CHECK(a.flags == b.flags);
    CHECK(a.rgb == b.rgb);
}

} // namespace

TEST_CASE("session files: meta, params and frames round trip; a truncated frame is dropped") {
    SessionMeta meta;
    meta.app_version = "0.1.0";
    meta.track = std::filesystem::path(u8"C:/music/Ünïcode track.mp3");
    meta.source = AnalysisSource::stems;
    meta.analysis_grid = {150.0, 770112, 48000};
    meta.pixels = 444;
    const Result<SessionMeta> m = parse_meta_json(meta_json(meta).value());
    REQUIRE(m.ok());
    CHECK(m.value().track == meta.track);
    CHECK(m.value().source == AnalysisSource::stems);
    CHECK(m.value().analysis_grid.first_downbeat == 770112);
    CHECK(m.value().pixels == 444);

    Project p = default_project();
    p.latency_offset_ms = 35.0;
    RuntimeState r;
    r.manual_scene = 2;
    r.hold = true;
    r.grid = {152.0, 1234, 48000};
    const std::string lines = params_line(7, p, r).value() + params_line(8, p, {}).value();
    const Result<std::vector<ParamsEntry>> entries = parse_params_jsonl(lines);
    REQUIRE(entries.ok());
    REQUIRE(entries.value().size() == 2);
    CHECK(entries.value()[0].version == 7);
    CHECK(entries.value()[0].project.latency_offset_ms == 35.0);
    CHECK(entries.value()[0].runtime.manual_scene == 2);
    CHECK(entries.value()[0].runtime.hold);
    CHECK(entries.value()[0].runtime.grid.bpm == 152.0);
    CHECK(entries.value()[1].runtime.manual_scene == -1);

    std::vector<std::uint8_t> bytes = frames_header(4);
    for (int i = 0; i < 3; ++i) {
        append_frame(bytes, frame(i, 4));
    }
    bytes.resize(bytes.size() - 5); // A crash mid-write.
    const Result<FramesFile> file = parse_frames(bytes);
    REQUIRE(file.ok());
    CHECK(file.value().pixels == 4);
    REQUIRE(file.value().frames.size() == 2);
    check_same(file.value().frames[1], frame(1, 4));
    CHECK_FALSE(parse_frames(std::vector<std::uint8_t>(20, 0)).ok());
}

TEST_CASE("recorder: writes every frame and configuration in order, and refuses an old folder") {
    const auto folder = std::filesystem::temp_directory_path() / "psyight_recorder_test";
    std::filesystem::remove_all(folder);
    SessionMeta meta;
    meta.app_version = "test";
    meta.track = "track.mp3";
    meta.pixels = 10;
    {
        Result<std::unique_ptr<Recorder>> started = Recorder::start(folder, meta, "{}\n");
        REQUIRE(started.ok());
        Recorder &rec = *started.value();
        CHECK(rec.push_params(std::make_shared<const std::string>("{\"a\": 1}\n")));
        for (int i = 0; i < 300; ++i) {
            CHECK(rec.push_frame(std::make_shared<const RecordedFrame>(frame(i, 10))));
        }
        CHECK_FALSE(rec.push_frame(std::make_shared<const RecordedFrame>(frame(0, 9)))); // Size.
        CHECK(rec.dropped() == 1);
    } // Destroying it writes the rest.
    const Result<FramesFile> file = read_frames(folder / "frames.bin");
    REQUIRE(file.ok());
    REQUIRE(file.value().frames.size() == 300);
    for (int i = 0; i < 300; i += 37) {
        check_same(file.value().frames[static_cast<size_t>(i)], frame(i, 10));
    }
    CHECK(read_text_file(folder / "params.jsonl").value() == "{\"a\": 1}\n");
    CHECK(read_text_file(folder / "events.jsonl").value() == "{}\n");
    CHECK(parse_meta_json(read_text_file(folder / "meta.json").value()).ok());
    CHECK_FALSE(Recorder::start(folder, meta, "").ok()); // Never over an old session.
    std::filesystem::remove_all(folder);
}
