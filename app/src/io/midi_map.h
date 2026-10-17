// MIDI learn and mapping (SPEC section 9, M5): controller messages to show controls. Pure: the
// GUI feeds it the messages the MIDI input received and applies the actions it returns.
//
// Targets: "blackout", "hold", "tap" and "downbeat" (buttons), "auto" (back to the section
// automation), "scene:<n>" (hold scene n, from 0), "brightness" (the global cap, 0 to 1) and
// "latency" (the offset, -200 to 200 ms). Buttons fire on a note on, or on a controller crossing
// from 63 or below to 64 or above (so a toggle switch or a pad works). Continuous targets take a
// controller's value over 0 to 127; a note on a continuous target sets its maximum. Learn: arm a
// target; the next controller or note on (any channel) binds it, replacing the target's earlier
// binding and any other target's binding of that control.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace psyight {

enum class MidiKind { cc, note };

struct MidiBinding {
    std::string target;
    MidiKind kind = MidiKind::cc;
    int channel = 0; // 0 to 15 (shown as 1 to 16).
    int number = 0;  // Controller or note number.

    bool operator==(const MidiBinding &) const = default;
};

struct MidiMessage {
    std::uint8_t status = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
};

struct MidiAction {
    std::string target;
    double value = 1.0;   // Continuous targets: 0 to 1.
    bool trigger = false; // Buttons: fire once.
};

bool midi_target_is_button(const std::string &target);
std::string describe(const MidiBinding &binding); // "CC 7, channel 1" or "note C3, channel 10".

class MidiMap {
  public:
    std::vector<MidiBinding> bindings;

    void learn(const std::string &target) { learning_ = target; }
    void cancel_learn() { learning_.clear(); }
    const std::string &learning() const { return learning_; }
    void clear(const std::string &target);
    const MidiBinding *binding_of(const std::string &target) const;

    // Actions for one message (none while learning: the message binds the armed target).
    std::vector<MidiAction> handle(const MidiMessage &message);

  private:
    std::string learning_;
    std::array<std::uint8_t, 16 * 128> cc_last_{}; // Last controller values, for button edges.
};

} // namespace psyight
