// Project file: round trips, defaults for missing fields, and rejected input.

#include "io/project.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

using namespace psyight;

TEST_CASE("project: the default round-trips and builds the default rig and patch") {
    const Project p = default_project();
    REQUIRE(p.strips.size() == 4);
    CHECK(rig_of(p).pixels.size() == default_rig().pixels.size());
    CHECK(patch_of(p).universes == std::vector<int>{1, 2, 3});
    const Result<std::string> text = to_project_json(p);
    REQUIRE(text.ok());
    const Result<Project> back = parse_project_json(text.value());
    REQUIRE(back.ok());
    CHECK(to_project_json(back.value()).value() == text.value());
}

TEST_CASE("project: every stored setting survives a round trip") {
    Project p = default_project();
    p.strips[1].order = ColourOrder::bgr;
    p.strips[1].spec.groups = {"booth", "left"};
    p.strips.push_back({{"canopy", {{0, 2, 0}, {1, 2.5, 0.5}, {2, 2, 0}}, 30.0, 45, {"canopy"}},
                        ColourOrder::rgb});
    p.first_universe = 10;
    p.pixels_per_universe = 128;
    p.output.protocol = OutputProtocol::art_net;
    p.output.multicast = false;
    p.output.address = {192, 168, 1, 77};
    p.output.sync = false;
    p.output.sync_universe = 500;
    p.output.priority = 150;
    p.source_name = "Booth laptop";
    p.limiter.brightness_cap = 0.6;
    p.limiter.budget_a = 30.0;
    p.limiter.gamma = 2.5;
    p.latency_offset_ms = 42.5;
    p.automation.crossfade_beats = 8.0;
    p.automation.palette_every_phrases = 4;
    const Result<Project> back = parse_project_json(to_project_json(p).value());
    REQUIRE(back.ok());
    const Project &q = back.value();
    REQUIRE(q.strips.size() == 5);
    CHECK(q.strips[1].order == ColourOrder::bgr);
    CHECK(q.strips[1].spec.groups == std::vector<std::string>{"booth", "left"});
    CHECK(q.strips[4].spec.name == "canopy");
    CHECK(q.strips[4].spec.polyline.size() == 3);
    CHECK(q.strips[4].spec.polyline[1].z == 0.5);
    CHECK(q.strips[4].spec.pixels_per_m == 30.0);
    CHECK(q.strips[4].spec.count == 45);
    CHECK(q.first_universe == 10);
    CHECK(q.pixels_per_universe == 128);
    CHECK(q.output.protocol == OutputProtocol::art_net);
    CHECK_FALSE(q.output.multicast);
    CHECK(q.output.address == std::array<std::uint8_t, 4>{192, 168, 1, 77});
    CHECK_FALSE(q.output.sync);
    CHECK(q.output.sync_universe == 500);
    CHECK(q.output.priority == 150);
    CHECK(q.source_name == "Booth laptop");
    CHECK(q.output.cid == cid_from_name("Booth laptop"));
    CHECK(q.limiter.brightness_cap == 0.6);
    CHECK(q.limiter.budget_a == 30.0);
    CHECK(q.limiter.gamma == 2.5);
    CHECK(q.latency_offset_ms == 42.5);
    CHECK(q.automation.crossfade_beats == 8.0);
    CHECK(q.automation.palette_every_phrases == 4);
    // 444 + 45 pixels at 128 per universe: universes 10 to 13.
    CHECK(patch_of(q).universes == std::vector<int>{10, 11, 12, 13});
}

TEST_CASE("project: missing sections take defaults; bad input is rejected") {
    const Result<Project> minimal = parse_project_json(R"({"schema": 1, "unknown": 3})");
    REQUIRE(minimal.ok());
    CHECK(minimal.value().strips.size() == 4);
    CHECK(minimal.value().output.protocol == OutputProtocol::sacn);
    CHECK(minimal.value().limiter.flash_limiter);

    const char *bad[] = {
        R"({"schema": 2})",
        R"({"rig": {"strips": []}})",
        R"({"schema": 1, "rig": {"strips": [{"polyline": [[0, 0, 0]], "count": 5}]}})",
        R"({"schema": 1, "rig": {"strips": [{"polyline": [[0, 0], [1, 0]], "count": 5}]}})",
        R"({"schema": 1, "rig": {"strips": [{"polyline": [[0,0,0],[1,0,0]], "count": 1.5}]}})",
        R"({"schema": 1, "rig": {"strips": [{"polyline": [[0,0,0],[1,0,0]],
             "colour_order": "xyz"}]}})",
        R"({"schema": 1, "output": {"address": "1.2.3"}})",
        R"({"schema": 1, "output": {"protocol": "dmx"}})",
        R"({"schema": 1, "patch": {"pixels_per_universe": 171}})",
        R"({"schema": 1, "limiter": {"brightness_cap": 2.0}})",
        R"({"schema": 1, "limiter": {"gamma": "high"}})",
        R"(not json)",
    };
    for (const char *text : bad) {
        CAPTURE(text);
        CHECK_FALSE(parse_project_json(text).ok());
    }
}

TEST_CASE("project: polylines as text, and pixels to fill a polyline") {
    const auto points = parse_polyline_text("0,0 2.5,0;2.5,1.2,0.5");
    REQUIRE(points.has_value());
    REQUIRE(points->size() == 3);
    CHECK((*points)[1].x == 2.5);
    CHECK((*points)[2].z == 0.5);
    CHECK(polyline_text(*points) == "0,0 2.5,0 2.5,1.2,0.5");
    CHECK(parse_polyline_text(polyline_text(*points))->size() == 3);
    CHECK_FALSE(parse_polyline_text("0,0").has_value());         // One point.
    CHECK_FALSE(parse_polyline_text("0 1,1").has_value());       // A lone coordinate.
    CHECK_FALSE(parse_polyline_text("0,0,0,0 1,1").has_value()); // Four coordinates.
    CHECK_FALSE(parse_polyline_text("0,0 1,x").has_value());
    CHECK_FALSE(parse_polyline_text("").has_value());
    CHECK(pixels_to_fill({"s", {{0, 0, 0}, {2.5, 0, 0}}, 60.0, 0, {}}) == 150);
    CHECK(pixels_to_fill({"s", {{0, 0, 0}, {3, 4, 0}, {3, 4, 1}}, 30.0, 0, {}}) == 180);
}

TEST_CASE("project: save and load through a file") {
    const auto path = std::filesystem::temp_directory_path() / "psyight_project_test.psyproj";
    Project p = default_project();
    p.latency_offset_ms = -12.0;
    REQUIRE(save_project(path, p).ok());
    const Result<Project> back = load_project(path);
    REQUIRE(back.ok());
    CHECK(back.value().latency_offset_ms == -12.0);
    std::filesystem::remove(path);
    CHECK_FALSE(load_project(path).ok());
}
