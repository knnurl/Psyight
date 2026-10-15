// Note names for MIDI numbers, with C4 = 60 (so MIDI 29 is F1, the usual psytrance bass root).
#pragma once

#include "core/result.h"

#include <string>
#include <string_view>

namespace psyight {

// "F1", "F#1": sharps only, as piano-roll labels.
std::string midi_to_note_name(int midi);

// Accepts a MIDI number ("29") or a note name with optional # or b and an octave from -1 to 9
// ("F1", "f#1", "Gb1", "C-1"). Whitespace around the text is ignored. Range 0 to 127.
Result<int> parse_pitch(std::string_view text);

} // namespace psyight
