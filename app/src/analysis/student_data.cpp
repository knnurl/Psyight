#include "analysis/student_data.h"

#include "analysis/stft.h"

#include <algorithm>
#include <cmath>

namespace psyight {

namespace {

constexpr double kFloorDb = -120.0;
constexpr double kOffsetDb = 60.0; // Scaled value = (dB + 60) / 60, clamped to -1..1.

// Triangular bands on log-spaced centres, as bin weights (rows: bands).
struct Bands {
    int count = 0;
    std::vector<std::vector<std::pair<int, float>>> weights;
};

// A band narrower than two bins takes the power interpolated between the two bins around its
// centre (a smooth log-frequency resampling at the low end); a wider band sums the power under a
// triangle peaking at 1 on its centre (band energy).
Bands log_bands(const Stft &stft, int count, double min_hz, double max_hz) {
    Bands b;
    b.count = count;
    b.weights.resize(static_cast<size_t>(count));
    // count + 2 edges: band k spans edges k to k + 2, peaking at edge k + 1.
    std::vector<double> edge(static_cast<size_t>(count) + 2);
    for (size_t i = 0; i < edge.size(); ++i) {
        edge[i] = min_hz * std::pow(max_hz / min_hz, static_cast<double>(i) / (count + 1));
    }
    const double bin_width_hz = stft.bin_hz(1);
    for (int k = 0; k < count; ++k) {
        const double lo = edge[static_cast<size_t>(k)];
        const double mid = edge[static_cast<size_t>(k) + 1];
        const double hi = edge[static_cast<size_t>(k) + 2];
        auto &w = b.weights[static_cast<size_t>(k)];
        if (hi - lo < 2.0 * bin_width_hz) {
            const double position = mid / bin_width_hz;
            const int below = std::min(static_cast<int>(position), stft.bins() - 2);
            const double frac = std::clamp(position - below, 0.0, 1.0);
            w.emplace_back(below, static_cast<float>(1.0 - frac));
            w.emplace_back(below + 1, static_cast<float>(frac));
            continue;
        }
        for (int bin = 0; bin < stft.bins(); ++bin) {
            const double f = stft.bin_hz(bin);
            if (f <= lo || f >= hi) {
                continue;
            }
            const double v = f < mid ? (f - lo) / (mid - lo) : (hi - f) / (hi - mid);
            w.emplace_back(bin, static_cast<float>(v));
        }
    }
    return b;
}

void add_frame(const std::vector<std::vector<std::pair<int, float>>> &bands,
               std::span<const float> magnitudes, float *out) {
    for (size_t k = 0; k < bands.size(); ++k) {
        double power = 0.0;
        for (const auto &[bin, w] : bands[k]) {
            const double m = magnitudes[static_cast<size_t>(bin)];
            power += w * m * m;
        }
        const double db = power > 0.0 ? 10.0 * std::log10(power) : kFloorDb;
        out[k] = static_cast<float>(
            std::clamp((std::max(db, kFloorDb) + kOffsetDb) / kOffsetDb, -1.0, 1.0));
    }
}

void mark_event(std::vector<float> &target, double t_s, int frames, double frame_s) {
    const long k = std::lround(t_s / frame_s);
    for (long d = -1; d <= 1; ++d) {
        const long i = k + d;
        if (i >= 0 && i < frames) {
            float &v = target[static_cast<size_t>(i)];
            v = std::max(v, d == 0 ? 1.0f : 0.5f);
        }
    }
}

} // namespace

Result<StudentFeatures> student_features(std::span<const float> mono, int sample_rate_hz,
                                         const StudentFeatureParams &p) {
    Stft short_stft;
    Stft long_stft;
    if (Result<void> r = short_stft.prepare(sample_rate_hz, p.short_window); !r) {
        return r.error();
    }
    if (Result<void> r = long_stft.prepare(sample_rate_hz, p.long_window); !r) {
        return r.error();
    }
    const Bands short_bands = log_bands(short_stft, p.short_bands, p.short_min_hz, p.short_max_hz);
    const Bands long_bands = log_bands(long_stft, p.long_bands, p.long_min_hz, p.long_max_hz);
    StudentFeatures out;
    out.frames = static_cast<int>(mono.size() / static_cast<size_t>(p.hop_samples));
    out.dims = p.short_bands + p.long_bands;
    out.values.assign(static_cast<size_t>(out.frames) * static_cast<size_t>(out.dims), 0.0f);
    std::vector<float> short_mag(static_cast<size_t>(short_stft.bins()));
    std::vector<float> long_mag(static_cast<size_t>(long_stft.bins()));
    for (int k = 0; k < out.frames; ++k) {
        // Causal: the window ends at the frame's sample.
        const SampleIndex end = static_cast<SampleIndex>(k) * p.hop_samples;
        short_stft.process(mono, end - p.short_window / 2, short_mag);
        long_stft.process(mono, end - p.long_window / 2, long_mag);
        float *row = out.values.data() + static_cast<size_t>(k) * static_cast<size_t>(out.dims);
        add_frame(short_bands.weights, short_mag, row);
        add_frame(long_bands.weights, long_mag, row + p.short_bands);
    }
    return out;
}

Result<void> StudentFeatureStream::prepare(int sample_rate_hz, const StudentFeatureParams &p) {
    params_ = p;
    if (Result<void> r = short_stft_.prepare(sample_rate_hz, p.short_window); !r) {
        return r;
    }
    if (Result<void> r = long_stft_.prepare(sample_rate_hz, p.long_window); !r) {
        return r;
    }
    if (p.short_window > p.long_window || p.hop_samples > p.long_window) {
        return Error{"student features: the long window must be the longest"};
    }
    short_bands_ = log_bands(short_stft_, p.short_bands, p.short_min_hz, p.short_max_hz).weights;
    long_bands_ = log_bands(long_stft_, p.long_bands, p.long_min_hz, p.long_max_hz).weights;
    history_.assign(static_cast<size_t>(p.long_window), 0.0f);
    short_mag_.assign(static_cast<size_t>(short_stft_.bins()), 0.0f);
    long_mag_.assign(static_cast<size_t>(long_stft_.bins()), 0.0f);
    return {};
}

void StudentFeatureStream::reset() { std::fill(history_.begin(), history_.end(), 0.0f); }

void StudentFeatureStream::push(std::span<const float> hop) {
    const size_t n = std::min(hop.size(), history_.size());
    std::copy(history_.begin() + static_cast<std::ptrdiff_t>(n), history_.end(), history_.begin());
    std::copy(hop.end() - static_cast<std::ptrdiff_t>(n), hop.end(),
              history_.end() - static_cast<std::ptrdiff_t>(n));
}

void StudentFeatureStream::frame(std::span<float> out) {
    // The window ends at the newest sample, as the batch code's window ends at the frame's sample;
    // the history starts as zeros, as the batch code reads zeros before the signal.
    const auto end = static_cast<SampleIndex>(history_.size());
    short_stft_.process(history_, end - params_.short_window / 2, short_mag_);
    long_stft_.process(history_, end - params_.long_window / 2, long_mag_);
    add_frame(short_bands_, short_mag_, out.data());
    add_frame(long_bands_, long_mag_, out.data() + params_.short_bands);
}

TeacherTargets teacher_targets(const TrackAnalysis &a, int frames, int hop_samples) {
    TeacherTargets t;
    t.frames = frames;
    const auto n = static_cast<size_t>(frames);
    t.kick_onset.assign(n, 0.0f);
    t.hat_onset.assign(n, 0.0f);
    t.bass_onset.assign(n, 0.0f);
    t.bass_gate.assign(n, 0.0f);
    t.bass_pitch.assign(n, -1);
    t.residual_activity.assign(n, 0.0f);
    const double frame_s = static_cast<double>(hop_samples) / a.sample_rate_hz;
    for (const double k : a.bass.kick_times_s) {
        mark_event(t.kick_onset, k, frames, frame_s);
    }
    for (const HatOnset &h : a.residual.hats) {
        mark_event(t.hat_onset, h.t_s, frames, frame_s);
    }
    for (const BassNote &note : a.bass.notes) {
        mark_event(t.bass_onset, note.onset_s, frames, frame_s);
        const long first = std::max(0L, std::lround(note.onset_s / frame_s));
        const long last = std::min(static_cast<long>(frames), std::lround(note.offset_s / frame_s));
        for (long i = first; i < last; ++i) {
            t.bass_gate[static_cast<size_t>(i)] = 1.0f;
            t.bass_pitch[static_cast<size_t>(i)] = static_cast<std::int16_t>(note.pitch_midi);
        }
    }
    const ResidualFeatures &r = a.residual;
    if (!r.energy_db.empty() && a.mix_features.hop_samples > 0) {
        const double residual_frame_s =
            static_cast<double>(a.mix_features.hop_samples) / a.mix_features.sample_rate_hz;
        for (size_t i = 0; i < n; ++i) {
            const auto j = static_cast<size_t>(
                std::clamp(std::lround(static_cast<double>(i) * frame_s / residual_frame_s), 0L,
                           static_cast<long>(r.energy_db.size()) - 1));
            t.residual_activity[i] =
                static_cast<float>(std::clamp((r.energy_db[j] + 90.0) / 90.0, 0.0, 1.0));
        }
    }
    return t;
}

std::vector<float> fingerprint(const StudentFeatures &f, int sample_rate_hz,
                               const StudentFeatureParams &p) {
    std::vector<float> out;
    const int per_second = std::max(1, sample_rate_hz / p.hop_samples);
    const int group = std::max(1, p.short_bands / kFingerprintBands); // Short bands come first.
    for (int start = 0; start + per_second <= f.frames; start += per_second) {
        for (int band = 0; band < kFingerprintBands; ++band) {
            double sum = 0.0;
            for (int k = start; k < start + per_second; ++k) {
                for (int g = 0; g < group; ++g) {
                    sum += f.values[static_cast<size_t>(k) * static_cast<size_t>(f.dims) +
                                    static_cast<size_t>(band * group + g)];
                }
            }
            out.push_back(static_cast<float>(sum / (per_second * group)));
        }
    }
    return out;
}

} // namespace psyight
