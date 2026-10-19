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

SectionTimeline section_timeline(const TrackAnalysis &analysis, const Grid &grid,
                                 std::vector<DetectedSection> sections) {
    SectionTimeline timeline;
    timeline.phrase_starts_s = phrase_starts(
        grid, sections, static_cast<int>(std::lround(BeatTrackerParams{}.bars_per_phrase)),
        analysis.beats.phrase_bar_offset, analysis.duration_s);
    assign_energy(sections, analysis.section_bars);
    if (analysis.mix_features.sample_rate_hz > 0) {
        timeline.riser = riser_curve(analysis.residual, sections,
                                     static_cast<double>(analysis.mix_features.hop_samples) /
                                         analysis.mix_features.sample_rate_hz);
    }
    timeline.sections = std::move(sections);
    return timeline;
}

EngineInputs inputs_at(const TrackAnalysis &a, const Grid &grid, double t_s,
                       const SectionTimeline *timeline) {
    EngineInputs in;
    in.t_s = t_s;
    const std::vector<double> &phrase_starts =
        timeline != nullptr ? timeline->phrase_starts_s : a.phrase_starts_s;
    const std::vector<DetectedSection> &sections =
        timeline != nullptr ? timeline->sections : a.sections;

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
    in.beat_s = grid.bpm > 0.0 ? 60.0 / grid.bpm : 0.0;

    // Phrase.
    const long long phrase = last_at_or_before(phrase_starts, t_s, [](double x) { return x; });
    if (phrase >= 0) {
        const double start = phrase_starts[static_cast<size_t>(phrase)];
        const double end = static_cast<size_t>(phrase + 1) < phrase_starts.size()
                               ? phrase_starts[static_cast<size_t>(phrase + 1)]
                               : a.duration_s;
        in.phrase_index = static_cast<int>(phrase);
        in.phrase_phase = end > start ? std::clamp((t_s - start) / (end - start), 0.0, 1.0) : 0.0;
    }

    // Kick.
    const long long kick = last_at_or_before(a.bass.kick_times_s, t_s, [](double x) { return x; });
    in.kicks_so_far = kick + 1;
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
    for (size_t i = 0; i < sections.size(); ++i) {
        const DetectedSection &section = sections[i];
        if (t_s >= section.start_s && t_s < section.end_s) {
            in.section = section.label;
            in.section_index = static_cast<int>(i);
            in.since_section_s = t_s - section.start_s;
            in.section_length_s = section.end_s - section.start_s;
            in.section_progress =
                in.section_length_s > 0.0 ? in.since_section_s / in.section_length_s : 0.0;
            in.section_energy = section.energy;
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
        const std::vector<float> &riser = timeline != nullptr ? timeline->riser : a.riser;
        if (frame < riser.size()) {
            in.riser = riser[frame];
        }
    }
    const long long hat = last_at_or_before(r.hats, t_s, [](const HatOnset &h) { return h.t_s; });
    in.hats_so_far = hat + 1;
    if (hat >= 0) {
        in.since_hat_s = t_s - r.hats[static_cast<size_t>(hat)].t_s;
    }
    return in;
}

EngineInputs inputs_at(const LiveState &s, double t_s) {
    EngineInputs in;
    in.t_s = t_s;
    if (s.tempo_known && s.period_s > 0.0) {
        const double beat = static_cast<double>(s.anchor_beat) + (t_s - s.anchor_s) / s.period_s;
        in.beat_index = static_cast<long long>(std::floor(beat));
        in.beat_phase = beat - std::floor(beat);
        const double from_downbeat = beat - static_cast<double>(s.downbeat_beat);
        const long long whole = static_cast<long long>(std::floor(from_downbeat));
        in.beat_in_bar = static_cast<int>(((whole % 4) + 4) % 4);
        const double bar = from_downbeat / 4.0;
        in.bar_index = static_cast<long long>(std::floor(bar));
        in.bar_phase = bar - std::floor(bar);
        const double slot = beat * 4.0;
        in.slot_phase = slot - std::floor(slot);
        const double phrase = (beat - static_cast<double>(s.phrase_beat)) / (4.0 * s.phrase_bars);
        if (phrase >= 0.0) {
            in.phrase_index = static_cast<int>(std::floor(phrase));
            in.phrase_phase = phrase - std::floor(phrase);
        }
        in.beat_s = s.period_s;
        in.kicks_so_far = s.kicks;
        if (s.locked && s.kick_streak >= 4) {
            const double last_beat_s =
                s.anchor_s + (std::floor(beat) - static_cast<double>(s.anchor_beat)) * s.period_s;
            in.since_kick_s = t_s - last_beat_s;
            // Predicted kicks count as they land, ahead of their detection.
            in.kicks_so_far += std::max(0LL, in.beat_index - s.last_kick_beat);
        }
    } else {
        in.kicks_so_far = s.kicks;
    }
    if (in.since_kick_s == kNever && s.last_kick_s > kLiveNever && t_s >= s.last_kick_s) {
        in.since_kick_s = t_s - s.last_kick_s;
    }

    in.bass_notes_so_far = s.bass_notes;
    if (s.bass_notes > 0) {
        in.bass_midi = s.bass_midi;
        in.bass_confidence = s.bass_confidence;
        in.bass_velocity = 1.0;
        in.since_bass_onset_s = std::max(0.0, t_s - s.bass_onset_s);
        in.bass_active = s.bass_active;
        const double slot_s = s.tempo_known ? s.period_s / 4.0 : 0.0;
        if (s.bass_active) {
            in.since_bass_offset_s = 0.0;
            in.bass_note_phase =
                s.bass_typical_s > 0.0
                    ? std::clamp(in.since_bass_onset_s / s.bass_typical_s, 0.0, 1.0)
                    : 0.0;
            in.bass_gate_ratio = slot_s > 0.0 ? in.since_bass_onset_s / slot_s : 0.0;
        } else {
            in.since_bass_offset_s = std::max(0.0, t_s - s.bass_end_s);
            in.bass_note_phase = 1.0;
            in.bass_gate_ratio = slot_s > 0.0 ? (s.bass_end_s - s.bass_onset_s) / slot_s : 0.0;
        }
    }

    in.section = live_section_name(s.section);
    in.section_index = s.section_count - 1;
    in.since_section_s = std::max(0.0, t_s - s.section_start_s);
    in.section_length_s = s.tempo_known ? 4.0 * s.phrase_bars * s.period_s : 0.0;
    in.section_progress =
        in.section_length_s > 0.0 ? std::min(1.0, in.since_section_s / in.section_length_s) : 0.0;
    in.section_energy = s.section_energy;
    in.riser = s.riser;

    in.residual_level = std::clamp(s.residual_level, 0.0, 1.0);
    in.residual_centroid_hz = s.residual_centroid_hz;
    in.residual_flatness = s.residual_flatness;
    in.hats_so_far = s.hats;
    if (s.last_hat_s > kLiveNever && t_s >= s.last_hat_s) {
        in.since_hat_s = t_s - s.last_hat_s;
    }
    return in;
}

} // namespace psyight
