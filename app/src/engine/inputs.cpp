#include "engine/inputs.h"

#include <algorithm>
#include <cmath>

namespace psyight {

namespace {

// Index of the last value <= t in an ascending vector, or -1.
template <typename T, typename Key>
long long last_at_or_before(const std::vector<T> &values, double t, Key key) {
    const auto it = std::upper_bound(values.begin(), values.end(), t,
                                     [&](double x, const T &v) { return x < key(v); });
    return static_cast<long long>(it - values.begin()) - 1;
}

} // namespace

EngineInputs inputs_at(const TrackAnalysis &a, const Grid &grid, double t_s) {
    EngineInputs in;
    in.t_s = t_s;

    // Grid.
    const double slot = grid.slot_position(seconds_to_samples(t_s, grid.sample_rate_hz));
    const double beat = slot / kSlotsPerBeat;
    const double bar = slot / kSlotsPerBar;
    in.beat_index = static_cast<long long>(std::floor(beat));
    in.beat_phase = beat - std::floor(beat);
    in.beat_in_bar = static_cast<int>(((in.beat_index % 4) + 4) % 4);
    in.bar_index = static_cast<long long>(std::floor(bar));
    in.bar_phase = bar - std::floor(bar);
    in.slot_phase = slot - std::floor(slot);

    // Phrase.
    const long long phrase = last_at_or_before(a.phrase_starts_s, t_s, [](double x) { return x; });
    if (phrase >= 0) {
        const double start = a.phrase_starts_s[static_cast<size_t>(phrase)];
        const double end = static_cast<size_t>(phrase + 1) < a.phrase_starts_s.size()
                               ? a.phrase_starts_s[static_cast<size_t>(phrase + 1)]
                               : a.duration_s;
        in.phrase_index = static_cast<int>(phrase);
        in.phrase_phase = end > start ? std::clamp((t_s - start) / (end - start), 0.0, 1.0) : 0.0;
    }

    // Kick.
    const long long kick = last_at_or_before(a.bass.kick_times_s, t_s, [](double x) { return x; });
    if (kick >= 0) {
        in.since_kick_s = t_s - a.bass.kick_times_s[static_cast<size_t>(kick)];
    }

    // Bass.
    const std::vector<BassNote> &notes = a.bass.notes;
    const long long note =
        last_at_or_before(notes, t_s, [](const BassNote &n) { return n.onset_s; });
    in.bass_notes_so_far = note + 1;
    if (note >= 0) {
        const BassNote &n = notes[static_cast<size_t>(note)];
        in.bass_midi = n.pitch_midi;
        in.bass_cents = n.cents;
        in.bass_gate_ratio = n.gate_ratio;
        in.bass_decay_tau_ms = n.decay_tau_ms;
        in.bass_velocity = n.velocity;
        in.bass_confidence = n.confidence;
        in.bass_slide = n.slide_in;
        in.since_bass_onset_s = t_s - n.onset_s;
        in.bass_active = t_s < n.offset_s;
        in.since_bass_offset_s = in.bass_active ? 0.0 : t_s - n.offset_s;
        in.bass_note_phase =
            n.offset_s > n.onset_s
                ? std::clamp((t_s - n.onset_s) / (n.offset_s - n.onset_s), 0.0, 1.0)
                : 1.0;
    }

    // Section.
    for (size_t i = 0; i < a.sections.size(); ++i) {
        const DetectedSection &section = a.sections[i];
        if (t_s >= section.start_s && t_s < section.end_s) {
            in.section = section.label;
            in.section_index = static_cast<int>(i);
            in.since_section_s = t_s - section.start_s;
            in.section_length_s = section.end_s - section.start_s;
            in.section_progress =
                in.section_length_s > 0.0 ? in.since_section_s / in.section_length_s : 0.0;
            break;
        }
    }

    // Residual and hats.
    const ResidualFeatures &r = a.residual;
    if (!r.energy_db.empty() && a.mix_features.hop_samples > 0) {
        const double frame_s =
            static_cast<double>(a.mix_features.hop_samples) / a.mix_features.sample_rate_hz;
        const auto frame = static_cast<size_t>(
            std::clamp(std::lround(t_s / frame_s), 0L, static_cast<long>(r.energy_db.size()) - 1));
        in.residual_level = std::clamp((r.energy_db[frame] + 90.0) / 90.0, 0.0, 1.0);
        in.residual_centroid_hz = r.centroid_hz[frame];
        in.residual_flatness = r.flatness[frame];
    }
    const long long hat = last_at_or_before(r.hats, t_s, [](const HatOnset &h) { return h.t_s; });
    in.hats_so_far = hat + 1;
    if (hat >= 0) {
        in.since_hat_s = t_s - r.hats[static_cast<size_t>(hat)].t_s;
    }
    return in;
}

} // namespace psyight
