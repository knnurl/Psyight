// Timebase: the audio sample index is the master clock (SPEC section 4.1). Musical positions are
// derived from it through the grid; nothing here reads wall-clock time.
#pragma once

#include <cmath>
#include <cstdint>

namespace psyight {

using SampleIndex = std::int64_t;

inline constexpr int kSlotsPerBeat = 4; // A slot is one 16th note.
inline constexpr int kSlotsPerBar = 16;

inline double samples_to_seconds(SampleIndex samples, int sample_rate_hz) {
    return static_cast<double>(samples) / sample_rate_hz;
}

// Rounds to the nearest sample.
inline SampleIndex seconds_to_samples(double seconds, int sample_rate_hz) {
    return static_cast<SampleIndex>(std::llround(seconds * sample_rate_hz));
}

// Manual musical grid (M1): constant tempo anchored at a first downbeat.
struct Grid {
    double bpm = 160.0;
    SampleIndex first_downbeat = 0;
    int sample_rate_hz = 48000;

    // 4500 samples at 160 BPM and 48 kHz. Fractional in general.
    double slot_length_samples() const { return sample_rate_hz * 60.0 / bpm / kSlotsPerBeat; }

    // Position in slots since the first downbeat; negative before it.
    double slot_position(SampleIndex sample) const {
        return static_cast<double>(sample - first_downbeat) / slot_length_samples();
    }

    // Sample of a (possibly fractional) slot position, rounded to the nearest sample.
    SampleIndex slot_to_sample(double slot) const {
        return first_downbeat +
               static_cast<SampleIndex>(std::llround(slot * slot_length_samples()));
    }

    // Nearest slot boundary, for 1/16 snapping.
    SampleIndex snap_to_slot(SampleIndex sample) const {
        return slot_to_sample(std::round(slot_position(sample)));
    }

    // Slot index within its bar, 0 to 15, for a sample inside that slot (also before the
    // first downbeat).
    int slot_in_bar(SampleIndex sample) const {
        const auto slot = static_cast<std::int64_t>(std::floor(slot_position(sample)));
        return static_cast<int>(((slot % kSlotsPerBar) + kSlotsPerBar) % kSlotsPerBar);
    }
};

} // namespace psyight
