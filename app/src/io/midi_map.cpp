#include "io/midi_map.h"

#include "core/pitch.h"

#include <algorithm>

namespace psyight {

namespace {

constexpr std::uint8_t kNoteOff = 0x80;
constexpr std::uint8_t kNoteOn = 0x90;
constexpr std::uint8_t kControl = 0xB0;
constexpr std::uint8_t kButtonEdge = 64;

} // namespace

bool midi_target_is_button(const std::string &target) {
    return target != "brightness" && target != "latency";
}

std::string describe(const MidiBinding &b) {
    const std::string channel = ", channel " + std::to_string(b.channel + 1);
    return b.kind == MidiKind::cc ? "CC " + std::to_string(b.number) + channel
                                  : "note " + midi_to_note_name(b.number) + channel;
}

void MidiMap::clear(const std::string &target) {
    std::erase_if(bindings, [&](const MidiBinding &b) { return b.target == target; });
}

const MidiBinding *MidiMap::binding_of(const std::string &target) const {
    for (const MidiBinding &b : bindings) {
        if (b.target == target) {
            return &b;
        }
    }
    return nullptr;
}

std::vector<MidiAction> MidiMap::handle(const MidiMessage &m) {
    const std::uint8_t type = m.status & 0xF0;
    const int channel = m.status & 0x0F;
    const bool note_on = type == kNoteOn && m.data2 > 0;
    const bool control = type == kControl;
    std::vector<MidiAction> actions;
    if (!note_on && !control && type != kNoteOff && type != kNoteOn) {
        return actions; // Other messages (clock, pitch bend...) are not mapped.
    }
    const int number = m.data1 & 0x7F;
    const int value = m.data2 & 0x7F;
    std::uint8_t &last = cc_last_[static_cast<size_t>(channel * 128 + number)];
    const std::uint8_t previous = last;
    if (control) {
        last = static_cast<std::uint8_t>(value);
    }
    if (!learning_.empty()) {
        if (note_on || control) {
            const MidiBinding bound{learning_, control ? MidiKind::cc : MidiKind::note, channel,
                                    number};
            std::erase_if(bindings, [&](const MidiBinding &b) {
                return b.target == bound.target ||
                       (b.kind == bound.kind && b.channel == bound.channel &&
                        b.number == bound.number);
            });
            bindings.push_back(bound);
            learning_.clear();
        }
        return actions;
    }
    for (const MidiBinding &b : bindings) {
        if (b.channel != channel || b.number != number || (b.kind == MidiKind::cc) != control) {
            continue;
        }
        if (midi_target_is_button(b.target)) {
            const bool fires =
                note_on || (control && previous < kButtonEdge && value >= kButtonEdge);
            if (fires) {
                actions.push_back({b.target, 1.0, true});
            }
        } else if (control) {
            actions.push_back({b.target, value / 127.0, false});
        } else if (note_on) {
            actions.push_back({b.target, 1.0, false});
        }
    }
    return actions;
}

} // namespace psyight
