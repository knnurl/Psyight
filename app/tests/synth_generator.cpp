#include "synth_generator.h"

#include <algorithm>
#include <cmath>

namespace psyight::synth {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kSlotsPerBeat = 4;
constexpr int kSlotsPerBar = 16;

constexpr double kKickStartHz = 170.0;
constexpr double kKickEndHz = 45.0;
constexpr double kKickSweepMs = 110.0;
constexpr double kKickDecayTauMs = 60.0;
constexpr double kKickLengthMs = 400.0;
constexpr double kClickHighpassHz = 3000.0;
constexpr double kClickDecayTauMs = 2.0;
constexpr double kClickLengthMs = 10.0;
constexpr double kClickLevel = 0.3; // Relative to the kick level.
constexpr double kHatHighpassHz = 8000.0;
constexpr double kHatDecayTauMs = 20.0;
constexpr double kHatLengthMs = 80.0;
constexpr double kAttackMs = 1.0;

// Deterministic on every platform, unlike std:: distributions.
class SplitMix64 {
  public:
    explicit SplitMix64(std::uint64_t seed) : state_(seed) {}

    // Uniform in [-1, 1).
    double next_bipolar() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return static_cast<double>(z >> 11) * 0x1.0p-52 - 1.0;
    }

  private:
    std::uint64_t state_;
};

// Topology-preserving-transform one-pole filter (stable at any cutoff below Nyquist).
class OnePole {
  public:
    double lowpass(double x, double cutoff_hz, double sample_rate_hz) {
        const double g = std::tan(kPi * cutoff_hz / sample_rate_hz);
        const double v = (x - state_) * g / (1.0 + g);
        const double y = v + state_;
        state_ = y + v;
        return y;
    }
    double highpass(double x, double cutoff_hz, double sample_rate_hz) {
        return x - lowpass(x, cutoff_hz, sample_rate_hz);
    }

  private:
    double state_ = 0.0;
};

// PolyBLEP correction for a band-limited sawtooth step.
double poly_blep(double phase, double phase_step) {
    if (phase < phase_step) {
        const double t = phase / phase_step;
        return t + t - t * t - 1.0;
    }
    if (phase > 1.0 - phase_step) {
        const double t = (phase - 1.0) / phase_step;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

double midi_to_hz(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }

std::int64_t ms_to_samples(double ms, int sample_rate_hz) {
    return static_cast<std::int64_t>(std::llround(ms * 1e-3 * sample_rate_hz));
}

// A bass note resolved to sample positions.
struct ResolvedNote {
    std::int64_t onset = 0;
    std::int64_t gate_off = 0;
    int midi = 0;
    double velocity = 1.0; // Of the attack that is sounding (slides keep the previous attack's).
    bool slide_in = false;
    bool slide_out = false;
};

std::vector<ResolvedNote> resolve_notes(std::vector<BassEvent> events, std::int64_t lead_in,
                                        double slot_samples, std::int64_t total_samples) {
    std::stable_sort(events.begin(), events.end(), [](const BassEvent &a, const BassEvent &b) {
        return a.start_slots < b.start_slots;
    });
    std::vector<ResolvedNote> notes;
    for (const BassEvent &event : events) {
        ResolvedNote note;
        note.onset = lead_in + std::llround(event.start_slots * slot_samples);
        note.gate_off = note.onset + std::llround(event.length_slots * slot_samples);
        note.midi = event.midi;
        note.velocity = event.velocity;
        note.slide_in = event.slide_in && !notes.empty();
        if (note.onset >= total_samples) {
            break;
        }
        note.gate_off = std::min(note.gate_off, total_samples);
        if (!notes.empty()) {
            ResolvedNote &previous = notes.back();
            if (note.slide_in) {
                previous.gate_off = note.onset; // Held until the glide starts.
                previous.slide_out = true;
                note.velocity = previous.velocity;
            } else {
                previous.gate_off = std::min(previous.gate_off, note.onset); // Monophonic.
            }
        }
        notes.push_back(note);
    }
    return notes;
}

} // namespace

std::vector<BassEvent> make_pattern(Pattern pattern, int bars, int root_midi, double gate_ratio) {
    std::vector<BassEvent> events;
    const auto add = [&](double start_slots, double step_slots, int midi, double velocity) {
        events.push_back({start_slots, gate_ratio * step_slots, midi, velocity, false});
    };
    for (int beat = 0; beat < bars * kSlotsPerBar / kSlotsPerBeat; ++beat) {
        const double base = beat * kSlotsPerBeat;
        switch (pattern) {
        case Pattern::rolling:
        case Pattern::rolling_ghost:
        case Pattern::rolling_octave_jump:
            add(base + 1, 1.0, root_midi, pattern == Pattern::rolling_ghost ? 0.5 : 1.0);
            add(base + 2, 1.0, root_midi, 1.0);
            add(base + 3, 1.0, pattern == Pattern::rolling_octave_jump ? root_midi + 12 : root_midi,
                1.0);
            break;
        case Pattern::triplet_gallop: {
            const double step = kSlotsPerBeat / 3.0;
            add(base + step, step, root_midi, 1.0);
            add(base + 2.0 * step, step, root_midi, 1.0);
            break;
        }
        case Pattern::offbeat_double:
            add(base + 2, 1.0, root_midi, 1.0);
            add(base + 3, 1.0, root_midi, 1.0);
            break;
        case Pattern::offbeat:
            add(base + 2, 1.0, root_midi, 1.0);
            break;
        }
    }
    return events;
}

SynthOutput render(const SynthParams &params, const std::vector<BassEvent> &events) {
    const int sr = params.sample_rate_hz;
    const double slot_samples = sr * 60.0 / params.bpm / kSlotsPerBeat;
    const std::int64_t lead_in = std::llround(params.lead_in_s * sr);
    const int total_slots = params.bars * kSlotsPerBar;
    const std::int64_t total_samples =
        lead_in + std::llround(total_slots * slot_samples) + std::llround(params.tail_s * sr);
    std::vector<double> mix(static_cast<size_t>(total_samples), 0.0);
    SplitMix64 rng(params.seed);

    std::vector<std::int64_t> kick_onsets;
    for (int beat = 0; beat < total_slots / kSlotsPerBeat; ++beat) {
        kick_onsets.push_back(lead_in + std::llround(beat * kSlotsPerBeat * slot_samples));
    }

    if (params.kick) {
        const std::int64_t length = ms_to_samples(kKickLengthMs, sr);
        const std::int64_t click_length = ms_to_samples(kClickLengthMs, sr);
        for (const std::int64_t onset : kick_onsets) {
            double phase = 0.0;
            OnePole click_filter;
            for (std::int64_t i = 0; i < length && onset + i < total_samples; ++i) {
                const double t_ms = 1e3 * i / sr;
                const double frequency_hz =
                    t_ms < kKickSweepMs
                        ? kKickStartHz * std::pow(kKickEndHz / kKickStartHz, t_ms / kKickSweepMs)
                        : kKickEndHz;
                double sample = std::exp(-t_ms / kKickDecayTauMs) * std::sin(2.0 * kPi * phase);
                phase += frequency_hz / sr;
                if (params.kick_click && i < click_length) {
                    sample += kClickLevel * std::exp(-t_ms / kClickDecayTauMs) *
                              click_filter.highpass(rng.next_bipolar(), kClickHighpassHz, sr);
                }
                mix[static_cast<size_t>(onset + i)] += params.kick_level * sample;
            }
        }
    }

    if (params.hats) {
        const std::int64_t length = ms_to_samples(kHatLengthMs, sr);
        for (int beat = 0; beat < total_slots / kSlotsPerBeat; ++beat) {
            const std::int64_t onset =
                lead_in + std::llround((beat * kSlotsPerBeat + 2) * slot_samples);
            OnePole filter;
            for (std::int64_t i = 0; i < length && onset + i < total_samples; ++i) {
                const double t_ms = 1e3 * i / sr;
                mix[static_cast<size_t>(onset + i)] +=
                    params.hat_level * std::exp(-t_ms / kHatDecayTauMs) *
                    filter.highpass(rng.next_bipolar(), kHatHighpassHz, sr);
            }
        }
    }

    // Monophonic bass voice.
    const std::vector<ResolvedNote> notes =
        resolve_notes(events, lead_in, slot_samples, total_samples);
    const double attack_samples = kAttackMs * 1e-3 * sr;
    const double decay_tau_samples = params.bass_decay_tau_ms * 1e-3 * sr;
    const double release_samples = params.bass_release_ms * 1e-3 * sr;
    const double slide_samples = params.slide_time_ms * 1e-3 * sr;
    const double filter_tau_samples = params.filter_tau_ms * 1e-3 * sr;
    const double sidechain_tau_samples = params.sidechain_release_ms * 1e-3 * sr;
    const auto envelope = [&](double since_attack, double velocity) {
        return velocity * std::min(1.0, since_attack / attack_samples) *
               std::exp(-since_attack / decay_tau_samples);
    };
    size_t current = 0;
    std::int64_t attack_start = 0;
    double glide_from_hz = 0.0;
    double frequency_hz = 0.0;
    double phase = 0.0;
    OnePole filter_a;
    OnePole filter_b;
    size_t next_kick = 0;
    std::int64_t last_kick = -1;
    for (std::int64_t n = 0; !notes.empty() && n < total_samples; ++n) {
        if (n < notes[0].onset) {
            continue;
        }
        while (current + 1 < notes.size() && n >= notes[current + 1].onset) {
            ++current;
            if (notes[current].slide_in) {
                glide_from_hz = frequency_hz;
            } else {
                attack_start = n;
            }
        }
        const ResolvedNote &note = notes[current];
        if (n == notes[0].onset) {
            attack_start = n;
        }
        const double target_hz = midi_to_hz(note.midi);
        if (note.slide_in) {
            const double progress =
                std::min(1.0, static_cast<double>(n - note.onset) / slide_samples);
            frequency_hz = glide_from_hz * std::pow(target_hz / glide_from_hz, progress);
        } else {
            frequency_hz = target_hz;
        }

        double amplitude = envelope(static_cast<double>(n - attack_start), note.velocity);
        if (n >= note.gate_off) {
            const double at_gate_off =
                envelope(static_cast<double>(note.gate_off - attack_start), note.velocity);
            amplitude = at_gate_off * std::max(0.0, 1.0 - static_cast<double>(n - note.gate_off) /
                                                              release_samples);
        }

        while (next_kick < kick_onsets.size() && kick_onsets[next_kick] <= n) {
            last_kick = kick_onsets[next_kick++];
        }
        const double duck =
            last_kick < 0
                ? 1.0
                : 1.0 - params.sidechain_depth *
                            std::exp(-static_cast<double>(n - last_kick) / sidechain_tau_samples);

        const double phase_step = frequency_hz / sr;
        const double saw = 2.0 * phase - 1.0 - poly_blep(phase, phase_step);
        phase += phase_step;
        phase -= std::floor(phase);
        const double cutoff_hz =
            params.filter_end_hz +
            (params.filter_start_hz - params.filter_end_hz) *
                std::exp(-static_cast<double>(n - attack_start) / filter_tau_samples);
        const double filtered =
            filter_b.lowpass(filter_a.lowpass(saw, cutoff_hz, sr), cutoff_hz, sr);
        mix[static_cast<size_t>(n)] += params.bass_level * duck * amplitude * filtered;
    }

    if (params.noise_level > 0.0) {
        for (double &sample : mix) {
            sample += params.noise_level * rng.next_bipolar();
        }
    }

    SynthOutput output;
    output.samples.reserve(mix.size());
    for (const double sample : mix) {
        output.samples.push_back(static_cast<float>(sample)); // Mixed in double, stored as float.
    }
    Labels &labels = output.labels;
    const auto seconds = [sr](std::int64_t samples) { return static_cast<double>(samples) / sr; };
    labels.audio = "synthetic";
    labels.sample_rate = sr;
    labels.grid = {params.bpm, seconds(lead_in)};
    labels.regions.push_back({0.0, seconds(total_samples), true});
    for (size_t beat = 0; beat < kick_onsets.size(); ++beat) {
        const double t_s = seconds(kick_onsets[beat]);
        if (params.kick) {
            labels.kicks.push_back({t_s});
        }
        labels.beats.push_back({t_s});
        if (beat % (kSlotsPerBar / kSlotsPerBeat) == 0) {
            labels.downbeats.push_back({t_s});
        }
    }
    for (const ResolvedNote &note : notes) {
        labels.bass_notes.push_back({seconds(note.onset), seconds(note.gate_off), note.midi, 0.0,
                                     note.slide_in, note.slide_out, note.velocity});
    }
    return output;
}

} // namespace psyight::synth
