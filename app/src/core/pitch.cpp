#include "core/pitch.h"

#include <cctype>
#include <charconv>

namespace psyight {

namespace {

constexpr const char *kSharpNames[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                                         "F#", "G",  "G#", "A",  "A#", "B"};
constexpr int kNaturalSemitones[7] = {9, 11, 0, 2, 4, 5, 7}; // A to G.

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

bool parse_int(std::string_view text, int &value) {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc() && end == text.data() + text.size() && !text.empty();
}

} // namespace

std::string midi_to_note_name(int midi) {
    const int octave = (midi >= 0 ? midi / 12 : (midi - 11) / 12) - 1;
    const int pitch_class = ((midi % 12) + 12) % 12;
    return std::string(kSharpNames[pitch_class]) + std::to_string(octave);
}

Result<int> parse_pitch(std::string_view text) {
    text = trim(text);
    const Error invalid{"not a pitch: use a MIDI number (29) or a note name (F1, F#1, Gb1)"};
    int midi = 0;
    if (parse_int(text, midi)) {
        if (midi < 0 || midi > 127) {
            return Error{"MIDI number out of range 0 to 127"};
        }
        return midi;
    }
    if (text.empty()) {
        return invalid;
    }
    const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    if (letter < 'A' || letter > 'G') {
        return invalid;
    }
    int semitone = kNaturalSemitones[letter - 'A'];
    text.remove_prefix(1);
    if (!text.empty() && (text[0] == '#' || text[0] == 'b')) {
        semitone += text[0] == '#' ? 1 : -1;
        text.remove_prefix(1);
    }
    int octave = 0;
    if (!parse_int(text, octave) || octave < -1 || octave > 9) {
        return invalid;
    }
    midi = (octave + 1) * 12 + semitone;
    if (midi < 0 || midi > 127) {
        return Error{"pitch out of MIDI range 0 to 127"};
    }
    return midi;
}

} // namespace psyight
