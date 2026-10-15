#include "core/params.h"
#include "core/spsc_queue.h"
#include "core/timebase.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <thread>

using namespace psyight;

TEST_CASE("timebase: a 16th at 160 BPM and 48 kHz is exactly 4500 samples") {
    const Grid grid{160.0, 24000, 48000};
    CHECK(grid.slot_length_samples() == 4500.0);
    CHECK(grid.slot_to_sample(1.0) == 28500);
    CHECK(grid.slot_position(28500 + 2250) == 1.5);
    CHECK(grid.slot_position(24000 - 4500) == -1.0);
    CHECK(seconds_to_samples(0.09375, 48000) == 4500);
    CHECK(samples_to_seconds(4500, 48000) == 0.09375);
}

TEST_CASE("timebase: snapping goes to the nearest slot, before and after the downbeat") {
    const Grid grid{160.0, 24000, 48000};
    CHECK(grid.snap_to_slot(24000 + 2249) == 24000);
    CHECK(grid.snap_to_slot(24000 + 2251) == 28500);
    CHECK(grid.snap_to_slot(24000 - 2251) == 19500);
    CHECK(grid.snap_to_slot(24000 - 2249) == 24000);
}

TEST_CASE("timebase: slot in bar wraps 0 to 15, including before the first downbeat") {
    const Grid grid{160.0, 24000, 48000};
    CHECK(grid.slot_in_bar(24000) == 0);
    CHECK(grid.slot_in_bar(24000 + 15 * 4500 + 10) == 15);
    CHECK(grid.slot_in_bar(24000 + 16 * 4500) == 0);
    CHECK(grid.slot_in_bar(24000 - 1) == 15);
}

TEST_CASE("timebase: fractional slot lengths do not drift over a long track") {
    const Grid grid{145.0, 0, 48000}; // 4965.517... samples per slot.
    const double slot = 10'000.0;     // About 15 minutes in.
    const SampleIndex sample = grid.slot_to_sample(slot);
    CHECK(std::abs(static_cast<double>(sample) - slot * grid.slot_length_samples()) <= 0.5);
    // Rounding to a whole sample moves the position by at most half a sample.
    CHECK(std::abs(grid.slot_position(sample) - slot) <= 0.5 / grid.slot_length_samples());
}

TEST_CASE("spsc queue: FIFO order, full and empty behaviour, power-of-two capacity") {
    SpscQueue<int> queue(5);
    CHECK(queue.capacity() == 7);
    int value = 0;
    CHECK_FALSE(queue.try_pop(value));
    for (int i = 0; i < 7; ++i) {
        CHECK(queue.try_push(i));
    }
    CHECK_FALSE(queue.try_push(99));
    CHECK(queue.size() == 7);
    for (int i = 0; i < 7; ++i) {
        REQUIRE(queue.try_pop(value));
        CHECK(value == i);
    }
    CHECK_FALSE(queue.try_pop(value));
}

TEST_CASE("spsc queue: two threads pass two million items in order without loss") {
    constexpr std::int64_t kItems = 2'000'000;
    SpscQueue<std::int64_t> queue(1024);
    std::thread producer([&] {
        for (std::int64_t i = 0; i < kItems;) {
            if (queue.try_push(i)) {
                ++i;
            }
        }
    });
    std::int64_t expected = 0;
    bool in_order = true;
    while (expected < kItems) {
        std::int64_t value = 0;
        if (queue.try_pop(value)) {
            in_order = in_order && value == expected;
            ++expected;
        }
    }
    producer.join();
    CHECK(in_order);
    CHECK(queue.size() == 0);
}

namespace {

ParamSpec spec(std::string id, double min, double max, double default_value) {
    ParamSpec s;
    s.id = std::move(id);
    s.min = min;
    s.max = max;
    s.default_value = default_value;
    return s;
}

} // namespace

TEST_CASE("params: declaration validates ids, ranges and defaults") {
    ParamRegistry registry;
    CHECK(registry.declare(spec("bass.offset_k", 0.0, 1.0, 0.25)).ok());
    CHECK_FALSE(registry.declare(spec("bass.offset_k", 0.0, 1.0, 0.5)).ok()); // duplicate
    CHECK_FALSE(registry.declare(spec("", 0.0, 1.0, 0.5)).ok());
    CHECK_FALSE(registry.declare(spec("a", 1.0, 0.0, 0.5)).ok()); // min > max
    CHECK_FALSE(registry.declare(spec("b", 0.0, 1.0, 1.5)).ok()); // default out of range
    CHECK(registry.specs().size() == 1);
    CHECK(registry.get("bass.offset_k") == 0.25);
}

TEST_CASE("params: set clamps to the range and rejects unknown ids and NaN") {
    ParamRegistry registry;
    REQUIRE(registry.declare(spec("kick.mask_ms", 0.0, 100.0, 60.0)).ok());
    CHECK(registry.set("kick.mask_ms", 150.0).ok());
    CHECK(registry.get("kick.mask_ms") == 100.0);
    CHECK(registry.set("kick.mask_ms", -5.0).ok());
    CHECK(registry.get("kick.mask_ms") == 0.0);
    CHECK_FALSE(registry.set("missing", 1.0).ok());
    CHECK_FALSE(registry.set("kick.mask_ms", std::nan("")).ok());
    registry.reset_to_defaults();
    CHECK(registry.get("kick.mask_ms") == 60.0);
}

TEST_CASE("params: presets round trip by id and survive unknown ids") {
    ParamRegistry a;
    REQUIRE(a.declare(spec("x", 0.0, 10.0, 1.0)).ok());
    REQUIRE(a.declare(spec("y", 0.0, 10.0, 2.0)).ok());
    REQUIRE(a.set("x", 1.0 / 3.0).ok());
    const std::string preset = a.to_preset_json();

    ParamRegistry b;
    REQUIRE(b.declare(spec("x", 0.0, 10.0, 5.0)).ok());
    REQUIRE(b.declare(spec("z", 0.0, 10.0, 5.0)).ok());
    const Result<PresetLoadReport> report = b.apply_preset_json(preset);
    REQUIRE(report.ok());
    CHECK(b.get("x") == 1.0 / 3.0);
    CHECK(b.get("z") == 5.0);
    REQUIRE(report.value().unknown_ids.size() == 1);
    CHECK(report.value().unknown_ids[0] == "y");

    const Result<PresetLoadReport> clamped =
        b.apply_preset_json(R"({"schema": 1, "values": {"x": 99}})");
    REQUIRE(clamped.ok());
    CHECK(b.get("x") == 10.0);
    CHECK(clamped.value().clamped_ids == std::vector<std::string>{"x"});
}

TEST_CASE("params: an invalid preset changes nothing") {
    ParamRegistry registry;
    REQUIRE(registry.declare(spec("x", 0.0, 10.0, 1.0)).ok());
    REQUIRE(registry.declare(spec("y", 0.0, 10.0, 2.0)).ok());
    CHECK_FALSE(
        registry.apply_preset_json(R"({"schema": 1, "values": {"x": 3, "y": "high"}})").ok());
    CHECK(registry.get("x") == 1.0);
    CHECK_FALSE(registry.apply_preset_json("not json").ok());
    CHECK_FALSE(registry.apply_preset_json(R"({"schema": 2, "values": {}})").ok());
}
