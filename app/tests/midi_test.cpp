// MIDI learn and mapping (io/midi_map.h).

#include "io/midi_map.h"

#include <doctest/doctest.h>

using namespace psyight;

namespace {

MidiMessage cc(int channel, int number, int value) {
    return {static_cast<std::uint8_t>(0xB0 | channel), static_cast<std::uint8_t>(number),
            static_cast<std::uint8_t>(value)};
}

MidiMessage note_on(int channel, int number, int velocity = 100) {
    return {static_cast<std::uint8_t>(0x90 | channel), static_cast<std::uint8_t>(number),
            static_cast<std::uint8_t>(velocity)};
}

} // namespace

TEST_CASE("midi: learn binds the next control and replaces earlier bindings") {
    MidiMap map;
    map.learn("blackout");
    CHECK(map.learning() == "blackout");
    CHECK(map.handle(cc(0, 20, 127)).empty()); // Binds, fires nothing.
    CHECK(map.learning().empty());
    REQUIRE(map.binding_of("blackout") != nullptr);
    CHECK(describe(*map.binding_of("blackout")) == "CC 20, channel 1");

    map.learn("hold");
    map.handle(cc(0, 20, 0)); // The same control: hold takes it from blackout.
    CHECK(map.binding_of("blackout") == nullptr);
    CHECK(map.binding_of("hold") != nullptr);
    map.learn("hold");
    map.handle(note_on(9, 36));
    CHECK(map.bindings.size() == 1);
    CHECK(describe(*map.binding_of("hold")) == "note C2, channel 10");
    map.clear("hold");
    CHECK(map.bindings.empty());
}

TEST_CASE("midi: buttons fire on note on and on a controller's rising edge") {
    MidiMap map;
    map.bindings = {{"blackout", MidiKind::cc, 0, 20}, {"tap", MidiKind::note, 9, 36}};
    CHECK(map.handle(cc(0, 20, 30)).empty());
    std::vector<MidiAction> a = map.handle(cc(0, 20, 100));
    REQUIRE(a.size() == 1);
    CHECK(a[0].target == "blackout");
    CHECK(a[0].trigger);
    CHECK(map.handle(cc(0, 20, 120)).empty()); // Still high: no new edge.
    CHECK(map.handle(cc(0, 20, 0)).empty());
    CHECK(map.handle(cc(0, 20, 127)).size() == 1);
    CHECK(map.handle(cc(1, 20, 0)).empty()); // Another channel.
    CHECK(map.handle(note_on(9, 36)).size() == 1);
    CHECK(map.handle(note_on(9, 36, 0)).empty()); // Velocity 0 is a note off.
    CHECK(map.handle({0x89, 36, 64}).empty());    // Note off.
    CHECK(map.handle({0xF8, 0, 0}).empty());      // Clock.
}

TEST_CASE("midi: continuous targets follow a controller") {
    MidiMap map;
    map.bindings = {{"brightness", MidiKind::cc, 2, 7}};
    std::vector<MidiAction> a = map.handle(cc(2, 7, 127));
    REQUIRE(a.size() == 1);
    CHECK(a[0].target == "brightness");
    CHECK_FALSE(a[0].trigger);
    CHECK(a[0].value == doctest::Approx(1.0));
    a = map.handle(cc(2, 7, 0));
    REQUIRE(a.size() == 1);
    CHECK(a[0].value == 0.0);
    CHECK(midi_target_is_button("scene:3"));
    CHECK_FALSE(midi_target_is_button("latency"));
    CHECK_FALSE(midi_target_is_button("macro:0"));
    map.bindings = {{"macro:4", MidiKind::cc, 0, 21}};
    a = map.handle(cc(0, 21, 64));
    REQUIRE(a.size() == 1);
    CHECK(a[0].target == "macro:4");
    CHECK(a[0].value == doctest::Approx(64.0 / 127.0));
}
