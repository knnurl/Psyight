// Timebase: the audio sample index is the master clock (SPEC section 4.1). Musical positions are
// derived from it through the grid; nothing here reads wall-clock time.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

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

enum class GridLevel { bar, beat, slot };

struct GridLine {
    SampleIndex sample = 0;
    GridLevel level = GridLevel::slot;
};

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

    // Grid lines in [from, to]. When there would be more than max_lines, finer levels are dropped
    // (slots, then beats); when even the bars exceed it, the result is empty.
    std::vector<GridLine> lines(SampleIndex from, SampleIndex to, size_t max_lines) const {
        const auto first = static_cast<std::int64_t>(std::ceil(slot_position(from)));
        const auto last = static_cast<std::int64_t>(std::floor(slot_position(to)));
        std::vector<GridLine> result;
        if (last < first) {
            return result;
        }
        const auto slots = static_cast<size_t>(last - first + 1);
        const std::int64_t step = slots <= max_lines                   ? 1
                                  : slots / kSlotsPerBeat <= max_lines ? kSlotsPerBeat
                                  : slots / kSlotsPerBar <= max_lines  ? kSlotsPerBar
                                                                       : 0;
        if (step == 0) {
            return result;
        }
        const auto floor_mod = [](std::int64_t a, std::int64_t b) { return ((a % b) + b) % b; };
        for (std::int64_t slot = first + floor_mod(-first, step); slot <= last; slot += step) {
            const GridLevel level = floor_mod(slot, kSlotsPerBar) == 0    ? GridLevel::bar
                                    : floor_mod(slot, kSlotsPerBeat) == 0 ? GridLevel::beat
                                                                          : GridLevel::slot;
            result.push_back({slot_to_sample(static_cast<double>(slot)), level});
        }
        return result;
    }
};

} // namespace psyight
