#include "analysis/bass_tracker.h"

#include "analysis/series.h"
#include "analysis/stft.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace psyight {

namespace {

constexpr double kDbPerNeper = 8.685889638065035; // 20 / ln(10).
constexpr int kPeakHalfWidthFrames = 2;           // A peak is the maximum of 5 frames.
constexpr int kKickRiseFrames = 4;                // Low-band rise measured over +-21 ms.
constexpr int kActivityFrames = 6;                // Bass activity checked over 32 ms.
constexpr double kMinNoteS = 0.001;
constexpr float kFluxCompression = 100.0f; // log(1 + C * |X|), as in the front end.

int band_index(const FrontEndResult &features, std::string_view name) {
    for (size_t b = 0; b < features.bands.size(); ++b) {
        if (features.bands[b].band.name == name) {
            return static_cast<int>(b);
        }
    }
    return -1;
}

// Per-hop powers in dB averaged over 2 * half + 1 frames: a centred moving mean square.
std::vector<float> smoothed_db(const std::vector<float> &db, int half) {
    std::vector<double> prefix(db.size() + 1, 0.0);
    for (size_t i = 0; i < db.size(); ++i) {
        prefix[i + 1] = prefix[i] + std::pow(10.0, db[i] / 10.0);
    }
    std::vector<float> result(db.size());
    const auto h = static_cast<size_t>(std::max(0, half));
    for (size_t i = 0; i < db.size(); ++i) {
        const size_t begin = i >= h ? i - h : 0;
        const size_t end = std::min(db.size(), i + h + 1);
        result[i] = static_cast<float>(
            10.0 * std::log10(std::max((prefix[end] - prefix[begin]) / (end - begin), 1e-12)));
    }
    return result;
}

double db_sum(double a_db, double b_db) {
    return 10.0 * std::log10(std::pow(10.0, a_db / 10.0) + std::pow(10.0, b_db / 10.0));
}

bool is_peak(const std::vector<float> &x, int f) {
    for (int k = 1; k <= kPeakHalfWidthFrames; ++k) {
        if ((f - k >= 0 && x[f - k] > x[f]) ||
            (f + k < static_cast<int>(x.size()) && x[f + k] >= x[f])) {
            return false;
        }
    }
    return true;
}

double peak_position(const std::vector<float> &x, int f) {
    if (f <= 0 || f + 1 >= static_cast<int>(x.size())) {
        return f;
    }
    return f + parabolic_offset(x[f - 1], x[f], x[f + 1]);
}

struct Peak {
    double frame = 0.0; // Interpolated.
    float value = 0.0f;
    float threshold = 0.0f;
};

// Keeps the stronger of two peaks closer than min_gap_frames.
void add_with_gap(std::vector<Peak> &peaks, const Peak &peak, double min_gap_frames) {
    if (!peaks.empty() && peak.frame - peaks.back().frame < min_gap_frames) {
        if (peak.value > peaks.back().value) {
            peaks.back() = peak;
        }
        return;
    }
    peaks.push_back(peak);
}

class Tracker {
  public:
    Tracker(std::span<const float> mono, const FrontEndResult &features, const Grid &grid,
            const BassTrackerParams &params)
        : mono_(mono), f_(features), grid_(grid), p_(params),
          frame_s_(static_cast<double>(features.hop_samples) / features.sample_rate_hz) {}

    Result<BassTrackResult> run();

  private:
    double frames(double ms) const { return ms * 1e-3 / frame_s_; }
    double time_of(double frame) const { return frame * frame_s_; }
    int frame_at(double t_s) const {
        return std::clamp(static_cast<int>(std::lround(t_s / frame_s_)), 0, f_.frames - 1);
    }
    // Onsets are masked from before the kick, where the onset function first sees it; offsets
    // and the decay fit only from kick_mask_pre_ms before it (drops before a kick are real).
    double onset_mask_lead_s() const;
    // Half width, in frames, of the centred envelope window (about one bass period).
    int envelope_half_frames() const {
        return static_cast<int>(std::lround(frames(p_.envelope_window_ms) / 2.0));
    }
    double offset_mask_lead_s() const { return 1e-3 * p_.kick_mask_pre_ms; }
    bool in_kick_mask(double t_s, double lead_s) const;
    void detect_kicks();
    Result<void> compute_onset_function();
    void detect_onsets();
    void build_notes();
    // Pitch of [onset_s, offset_s]: MIDI, cents and salience margin over the runner-up in dB.
    void measure_pitch(double onset_s, double offset_s, BassNote &note, double &margin_db) const;
    void split_slides();
    void finish_notes();

    std::span<const float> mono_;
    const FrontEndResult &f_;
    const Grid &grid_;
    const BassTrackerParams &p_;
    double frame_s_;
    std::vector<float> bass_db_; // Bass band energy.
    std::vector<float> low_db_;  // Sub plus bass energy.
    double bass_level_db_ = 0.0; // Track's p95 bass energy.
    BassTrackResult result_;
    std::vector<Peak> onsets_;
    struct Working {
        BassNote note;
        double peak_db = 0.0;
        double onset_confidence = 0.0;
        double pitch_margin_db = 0.0;
    };
    std::vector<Working> notes_;
};

double Tracker::onset_mask_lead_s() const {
    // Half the STFT window, plus the reach of the smoothed, centred difference.
    return 0.5 * p_.onset_window_samples / f_.sample_rate_hz +
           (2 * envelope_half_frames() + 1) * frame_s_ + 1e-3 * p_.kick_mask_pre_ms;
}

bool Tracker::in_kick_mask(double t_s, double lead_s) const {
    const auto it =
        std::upper_bound(result_.kick_times_s.begin(), result_.kick_times_s.end(), t_s + lead_s);
    return it != result_.kick_times_s.begin() && t_s - *(it - 1) <= p_.kick_mask_ms * 1e-3;
}

void Tracker::detect_kicks() {
    const std::vector<float> &click = f_.bands[band_index(f_, "click")].flux;
    const std::vector<float> &lowmid = f_.bands[band_index(f_, "low mid")].flux;
    const double norm = quantile(click, 0.99);
    if (norm <= 0.0) {
        return;
    }
    std::vector<Peak> kicks;
    for (int f = 0; f < f_.frames; ++f) {
        if (click[f] < p_.kick_threshold * norm || !is_peak(click, f) ||
            click[f] < p_.kick_min_click_ratio * lowmid[f]) {
            continue;
        }
        float before = low_db_[f];
        float after = low_db_[f];
        for (int k = 1; k <= kKickRiseFrames; ++k) {
            before = std::min(before, low_db_[std::max(0, f - k)]);
            after = std::max(after, low_db_[std::min(f_.frames - 1, f + k)]);
        }
        // Off the beat grid by more than the tolerance: a bass attack's click, not a kick.
        const double beat = grid_.slot_position(seconds_to_samples(time_of(peak_position(click, f)),
                                                                   grid_.sample_rate_hz)) /
                            kSlotsPerBeat;
        const double beat_s = 4.0 * grid_.slot_length_samples() / grid_.sample_rate_hz;
        const bool on_grid =
            p_.kick_grid_tolerance_ms <= 0.0 ||
            std::abs(beat - std::round(beat)) * beat_s <= p_.kick_grid_tolerance_ms * 1e-3;
        if (on_grid && after - before >= p_.kick_min_low_rise_db) {
            add_with_gap(kicks, {peak_position(click, f), click[f], 0.0f},
                         frames(p_.kick_min_gap_ms));
        }
    }
    // Kicks come in runs on consecutive beats; a bright bass attack that lands on a beat in a
    // breakdown usually stands alone. Keep a candidate when another lies one beat away.
    const double beat_frames =
        frames(4.0 * 1e3 * grid_.slot_length_samples() / grid_.sample_rate_hz);
    const double tolerance_frames = frames(std::max(p_.kick_grid_tolerance_ms, 1e3 * frame_s_));
    for (size_t k = 0; k < kicks.size(); ++k) {
        bool in_run = p_.kick_min_run_beats <= 1.0;
        for (size_t j = k > 0 ? k - 1 : 0; j < std::min(kicks.size(), k + 2) && !in_run; ++j) {
            in_run = j != k && std::abs(std::abs(kicks[j].frame - kicks[k].frame) - beat_frames) <=
                                   tolerance_frames;
        }
        if (in_run) {
            result_.kick_times_s.push_back(time_of(kicks[k].frame) - p_.kick_bias_ms * 1e-3);
        }
    }
}

Result<void> Tracker::compute_onset_function() {
    Stft stft;
    if (Result<void> r = stft.prepare(f_.sample_rate_hz, static_cast<int>(p_.onset_window_samples));
        !r) {
        return r;
    }
    const int first_bin =
        std::max(1, static_cast<int>(std::ceil(p_.onset_low_hz / stft.bin_hz(1))));
    const int end_bin =
        std::min(stft.bins(), static_cast<int>(std::ceil(p_.onset_high_hz / stft.bin_hz(1))));
    if (end_bin <= first_bin) {
        return Error{"bass onset band holds no STFT bin"};
    }
    // An STFT window of a few bass periods still ripples at the period rate inside a held note,
    // and rectifying frame-to-frame differences turns that ripple into flux. So each bin's
    // compressed magnitude is first averaged over the envelope window (about one period), then
    // differenced across that span, centred: the ripple cancels before rectification and peaks
    // stay at the onset.
    const int bins = end_bin - first_bin;
    std::vector<float> magnitudes(stft.bins());
    std::vector<float> log_magnitude(static_cast<size_t>(f_.frames) * bins); // Frame-major.
    for (int f = 0; f < f_.frames; ++f) {
        stft.process(mono_, static_cast<SampleIndex>(f) * f_.hop_samples, magnitudes);
        for (int b = 0; b < bins; ++b) {
            log_magnitude[static_cast<size_t>(f) * bins + b] =
                std::log1p(kFluxCompression * magnitudes[first_bin + b]);
        }
    }
    const int half = envelope_half_frames();
    std::vector<float> column(f_.frames);
    std::vector<double> rise(f_.frames, 0.0);
    for (int b = 0; b < bins; ++b) {
        for (int f = 0; f < f_.frames; ++f) {
            column[f] = log_magnitude[static_cast<size_t>(f) * bins + b];
        }
        const std::vector<float> smooth = box_mean(column, half);
        for (int f = 0; f < f_.frames; ++f) {
            const int after = std::min(f_.frames - 1, f + half + 1);
            const int before = std::max(0, f - half - 1);
            rise[f] += std::max(0.0f, smooth[after] - smooth[before]);
        }
    }
    std::vector<float> &odf = result_.onset_function;
    odf.resize(f_.frames);
    for (int f = 0; f < f_.frames; ++f) {
        odf[f] = static_cast<float>(rise[f] / bins);
    }
    return {};
}

void Tracker::detect_onsets() {
    std::vector<float> &odf = result_.onset_function;
    const double norm = quantile(odf, 0.99);
    for (int f = 0; f < f_.frames; ++f) {
        odf[f] = norm > 0.0 ? static_cast<float>(odf[f] / norm) : 0.0f;
        if (in_kick_mask(time_of(f), onset_mask_lead_s())) {
            odf[f] *= static_cast<float>(p_.kick_mask_onset_weight);
        }
    }
    // Local mean over a centred window, by prefix sums.
    std::vector<double> prefix(f_.frames + 1, 0.0);
    for (int f = 0; f < f_.frames; ++f) {
        prefix[f + 1] = prefix[f] + odf[f];
    }
    const int half =
        std::max(1, static_cast<int>(std::lround(frames(p_.onset_mean_window_ms) / 2)));
    for (int f = 0; f < f_.frames; ++f) {
        const int begin = std::max(0, f - half);
        const int end = std::min(f_.frames, f + half + 1);
        const double mean = (prefix[end] - prefix[begin]) / (end - begin);
        const auto threshold = static_cast<float>(std::max(mean + p_.onset_delta, p_.onset_floor));
        if (odf[f] < threshold || !is_peak(odf, f)) {
            continue;
        }
        float activity = bass_db_[f];
        for (int k = 1; k <= kActivityFrames && f + k < f_.frames; ++k) {
            activity = std::max(activity, bass_db_[f + k]);
        }
        if (activity < bass_level_db_ + p_.activity_floor_db) {
            continue;
        }
        // An onset keeps or raises the bass level; the click of an abrupt offset drops it. The
        // levels are read where the envelope window no longer reaches across the peak.
        const int reach = static_cast<int>(std::lround(frames(p_.envelope_window_ms) / 2.0)) + 1;
        if (bass_db_[std::min(f_.frames - 1, f + reach)] - bass_db_[std::max(0, f - reach)] <
            p_.onset_min_level_change_db) {
            continue;
        }
        add_with_gap(onsets_, {peak_position(odf, f), odf[f], threshold},
                     frames(p_.onset_min_gap_ms));
    }
}

void Tracker::build_notes() {
    const double end_s = time_of(f_.frames - 1);
    const double floor_db = bass_level_db_ + p_.activity_floor_db;
    const double ratio_db = 20.0 * std::log10(p_.offset_ratio);
    // A centred mean of width W over a step down crosses k^2 of the power W * (1/2 - k^2) late.
    const auto half = static_cast<int>(std::lround(frames(p_.envelope_window_ms) / 2.0));
    const double lag_s = (2 * half + 1) * frame_s_ * (0.5 - p_.offset_ratio * p_.offset_ratio);
    for (size_t i = 0; i < onsets_.size(); ++i) {
        Working w;
        const double onset_s = time_of(onsets_[i].frame) - p_.onset_bias_ms * 1e-3;
        double boundary_s = i + 1 < onsets_.size()
                                ? time_of(onsets_[i + 1].frame) - p_.onset_bias_ms * 1e-3
                                : end_s;
        const auto kick = std::upper_bound(result_.kick_times_s.begin(), result_.kick_times_s.end(),
                                           onset_s + p_.peak_search_ms * 1e-3);
        if (kick != result_.kick_times_s.end()) {
            boundary_s = std::min(boundary_s, *kick);
        }
        const int first = frame_at(onset_s);
        const int peak_end = std::min(frame_at(onset_s + p_.peak_search_ms * 1e-3),
                                      std::max(first, frame_at(boundary_s) - 1));
        int peak_frame = first;
        for (int f = first; f <= peak_end; ++f) {
            if (bass_db_[f] > bass_db_[peak_frame]) {
                peak_frame = f;
            }
        }
        w.peak_db = bass_db_[peak_frame];
        const double threshold_db = std::max(w.peak_db + ratio_db, floor_db);
        double offset_s = boundary_s;
        for (int f = peak_frame + 1; f < f_.frames && time_of(f) < boundary_s; ++f) {
            if (bass_db_[f] < threshold_db && !in_kick_mask(time_of(f), offset_mask_lead_s())) {
                const double before = bass_db_[f - 1];
                const double fraction =
                    before > bass_db_[f] ? (before - threshold_db) / (before - bass_db_[f]) : 0.0;
                offset_s = std::min(boundary_s, time_of(f - 1 + std::clamp(fraction, 0.0, 1.0)) -
                                                    lag_s - p_.offset_bias_ms * 1e-3);
                break;
            }
        }
        w.note.onset_s = onset_s;
        w.note.offset_s = std::max(offset_s, onset_s + kMinNoteS);
        w.onset_confidence =
            std::clamp(1.0 - onsets_[i].threshold / std::max(onsets_[i].value, 1e-6f), 0.0, 1.0);
        measure_pitch(w.note.onset_s, w.note.offset_s, w.note, w.pitch_margin_db);
        notes_.push_back(w);
    }
}

void Tracker::measure_pitch(double onset_s, double offset_s, BassNote &note,
                            double &margin_db) const {
    const LogSpectrogram &salience = f_.pitch_salience;
    const double trim_s = 0.5 * (1.0 - p_.pitch_centre_fraction) * (offset_s - onset_s);
    int first = static_cast<int>(std::ceil((onset_s + trim_s) / frame_s_));
    int last = static_cast<int>(std::floor((offset_s - trim_s) / frame_s_));
    if (last < first) {
        first = last = frame_at(0.5 * (onset_s + offset_s));
    }
    first = std::clamp(first, 0, salience.frames - 1);
    last = std::clamp(last, first, salience.frames - 1);
    std::vector<double> mean(salience.rows, 0.0);
    for (int f = first; f <= last; ++f) {
        for (int r = 0; r < salience.rows; ++r) {
            mean[r] += salience.db[static_cast<size_t>(f) * salience.rows + r];
        }
    }
    const int best = static_cast<int>(std::max_element(mean.begin(), mean.end()) - mean.begin());
    const double count = last - first + 1;
    double runner_up = -1e300;
    for (int r = 0; r < salience.rows; ++r) {
        if (std::abs(r - best) > 1) {
            runner_up = std::max(runner_up, mean[r]);
        }
    }
    margin_db = (mean[best] - runner_up) / count;
    note.pitch_midi = kSalienceFirstMidi + best;
    note.cents = best > 0 && best + 1 < salience.rows
                     ? 100.0 * parabolic_offset(mean[best - 1], mean[best], mean[best + 1])
                     : 0.0;
}

void Tracker::split_slides() {
    // A note long enough to hold two parts is measured in halves. When the halves differ in pitch
    // the note is split where the second pitch's salience overtakes the first's: comparing just
    // these two rows keeps other sounds (a kick's low salience) from splitting notes.
    const LogSpectrogram &salience = f_.pitch_salience;
    const double min_part_s = p_.slide_min_part_ms * 1e-3;
    std::vector<Working> split;
    for (const Working &w : notes_) {
        const double onset_s = w.note.onset_s;
        const double offset_s = w.note.offset_s;
        if (offset_s - onset_s < 2.0 * min_part_s) {
            split.push_back(w);
            continue;
        }
        const double middle_s = 0.5 * (onset_s + offset_s);
        BassNote first = w.note;
        BassNote second = w.note;
        double margin_db = 0.0;
        measure_pitch(onset_s, middle_s, first, margin_db);
        measure_pitch(middle_s, offset_s, second, margin_db);
        if (first.pitch_midi == second.pitch_midi) {
            split.push_back(w);
            continue;
        }
        const int row_a = first.pitch_midi - kSalienceFirstMidi;
        const int row_b = second.pitch_midi - kSalienceFirstMidi;
        const int begin = frame_at(onset_s + min_part_s);
        const int end = frame_at(offset_s - min_part_s);
        int best_frame = begin;
        double best_score = -1e300;
        double score = 0.0; // Sum over f < split of (b - a): maximise a before, b after.
        for (int f = frame_at(onset_s); f < begin; ++f) {
            score -= salience.db[static_cast<size_t>(f) * salience.rows + row_b] -
                     salience.db[static_cast<size_t>(f) * salience.rows + row_a];
        }
        for (int f = begin; f <= end; ++f) {
            if (score > best_score) {
                best_score = score;
                best_frame = f;
            }
            score -= salience.db[static_cast<size_t>(f) * salience.rows + row_b] -
                     salience.db[static_cast<size_t>(f) * salience.rows + row_a];
        }
        const double split_s = std::clamp(time_of(best_frame) - p_.slide_bias_ms * 1e-3,
                                          onset_s + kMinNoteS, offset_s - kMinNoteS);
        Working out = w;
        out.note.offset_s = split_s;
        out.note.slide_out = true;
        measure_pitch(onset_s, split_s, out.note, out.pitch_margin_db);
        Working in = w;
        in.note.onset_s = split_s;
        in.note.slide_in = true;
        in.onset_confidence = 1.0; // No onset to judge; confidence is the pitch's.
        measure_pitch(split_s, offset_s, in.note, in.pitch_margin_db);
        split.push_back(out);
        split.push_back(in);
    }
    notes_ = std::move(split);
}

void Tracker::finish_notes() {
    const double slot_s = grid_.slot_length_samples() / grid_.sample_rate_hz;
    for (size_t i = 0; i < notes_.size(); ++i) {
        Working &w = notes_[i];
        BassNote &note = w.note;
        note.duration_ms = 1e3 * (note.offset_s - note.onset_s);
        note.slot_len_ms = 1e3 * slot_s;
        note.gate_ratio = (note.offset_s - note.onset_s) / slot_s;
        note.octave = note.pitch_midi / 12 - 1;
        note.is_long = note.offset_s - note.onset_s > p_.long_note_slots * slot_s;
        note.slot_index = grid_.slot_in_bar(
            grid_.snap_to_slot(seconds_to_samples(note.onset_s, grid_.sample_rate_hz)));
        double loudest_db = w.peak_db;
        for (const Working &other : notes_) {
            if (std::abs(other.note.onset_s - note.onset_s) <= 0.5 * p_.velocity_window_s) {
                loudest_db = std::max(loudest_db, other.peak_db);
            }
        }
        note.velocity = std::pow(10.0, (w.peak_db - loudest_db) / 20.0);
        // Decay: least-squares slope of the bass energy in dB from the peak to the offset.
        const int first = frame_at(note.onset_s);
        int peak = first;
        for (int f = first; f <= frame_at(note.offset_s); ++f) {
            peak = bass_db_[f] > bass_db_[peak] ? f : peak;
        }
        double n = 0.0, sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
        for (int f = peak; time_of(f) < note.offset_s && f < f_.frames; ++f) {
            if (in_kick_mask(time_of(f), offset_mask_lead_s())) {
                continue;
            }
            const double x = time_of(f);
            const double y = bass_db_[f];
            n += 1.0;
            sx += x;
            sy += y;
            sxx += x * x;
            sxy += x * y;
        }
        const double denominator = n * sxx - sx * sx;
        const double slope_db_per_s =
            n >= 3.0 && denominator > 0.0 ? (n * sxy - sx * sy) / denominator : 0.0;
        note.decay_tau_ms = slope_db_per_s < 0.0 ? 1e3 * kDbPerNeper / -slope_db_per_s : 0.0;
        const double pitch_confidence =
            std::clamp(w.pitch_margin_db / p_.confidence_margin_db, 0.0, 1.0);
        note.confidence = std::sqrt(w.onset_confidence * pitch_confidence);
        result_.notes.push_back(note);
    }
}

Result<BassTrackResult> Tracker::run() {
    for (const char *name : {"sub", "bass", "low mid", "click"}) {
        if (band_index(f_, name) < 0) {
            return Error{std::string("bass tracker needs the front-end band ") + name};
        }
    }
    if (f_.frames < 3 || f_.pitch_salience.frames != f_.frames) {
        return Error{"bass tracker needs at least three analysis frames with pitch salience"};
    }
    const auto half = static_cast<int>(std::lround(frames(p_.envelope_window_ms) / 2.0));
    bass_db_ = smoothed_db(f_.bands[band_index(f_, "bass")].energy_db, half);
    const std::vector<float> sub_db = smoothed_db(f_.bands[band_index(f_, "sub")].energy_db, half);
    low_db_.resize(f_.frames);
    for (int f = 0; f < f_.frames; ++f) {
        low_db_[f] = static_cast<float>(db_sum(sub_db[f], bass_db_[f]));
    }
    bass_level_db_ = quantile(bass_db_, 0.95);
    detect_kicks();
    if (Result<void> r = compute_onset_function(); !r) {
        return r.error();
    }
    detect_onsets();
    build_notes();
    split_slides();
    finish_notes();
    return std::move(result_);
}

struct ParamEntry {
    double BassTrackerParams::*member;
    const char *id;
    const char *label;
    const char *unit;
    double min;
    double max;
    ParamTier tier;
    const char *description;
};

const BassTrackerParams kDefaults;

// clang-format off
const ParamEntry kParamTable[] = {
    {&BassTrackerParams::kick_threshold, "bass.kick_threshold", "Kick click threshold", "", 0.0, 1.0, ParamTier::engineer, "Click-band flux peak needed for a kick, as a fraction of the track's 99th percentile."},
    {&BassTrackerParams::kick_min_low_rise_db, "bass.kick_min_low_rise_db", "Kick low rise", "dB", 0.0, 30.0, ParamTier::engineer, "Rise of sub plus bass energy across a kick."},
    {&BassTrackerParams::kick_min_click_ratio, "bass.kick_min_click_ratio", "Kick click ratio", "", 0.0, 10.0, ParamTier::engineer, "Click-band flux over low-mid flux needed for a kick: a kick's click is broadband, a bass attack's is not."},
    {&BassTrackerParams::kick_min_gap_ms, "bass.kick_min_gap_ms", "Kick minimum gap", "ms", 50.0, 1000.0, ParamTier::engineer, "Of two kicks closer than this, the stronger is kept."},
    {&BassTrackerParams::kick_grid_tolerance_ms, "bass.kick_grid_tolerance_ms", "Kick grid tolerance", "ms", 0.0, 200.0, ParamTier::tune, "A kick must lie this close to a beat of the grid; 0 accepts kicks anywhere."},
    {&BassTrackerParams::kick_min_run_beats, "bass.kick_min_run_beats", "Kick run", "beats", 1.0, 2.0, ParamTier::engineer, "2: a kick needs another kick candidate one beat away (kicks come in runs); 1: any candidate."},
    {&BassTrackerParams::kick_bias_ms, "bass.kick_bias_ms", "Kick time bias", "ms", -20.0, 20.0, ParamTier::engineer, "Subtracted from detected kick times."},
    {&BassTrackerParams::kick_mask_pre_ms, "bass.kick_mask_pre_ms", "Kick mask lead", "ms", 0.0, 30.0, ParamTier::engineer, "Margin by which the kick mask starts before the onset function can first see the kick."},
    {&BassTrackerParams::kick_mask_ms, "bass.kick_mask_ms", "Kick mask", "ms", 0.0, 150.0, ParamTier::tune, "After a kick, bass onsets are down-weighted and energy drops do not end notes."},
    {&BassTrackerParams::onset_window_samples, "bass.onset_window_samples", "Onset window", "samples", 1024.0, 8192.0, ParamTier::engineer, "STFT window of the onset function: a power of two at least one bass period long."},
    {&BassTrackerParams::onset_low_hz, "bass.onset_low_hz", "Onset band low", "Hz", 20.0, 500.0, ParamTier::engineer, "Lowest frequency of the onset function."},
    {&BassTrackerParams::onset_high_hz, "bass.onset_high_hz", "Onset band high", "Hz", 200.0, 8000.0, ParamTier::engineer, "Highest frequency of the onset function."},
    {&BassTrackerParams::envelope_window_ms, "bass.envelope_window_ms", "Envelope window", "ms", 5.0, 100.0, ParamTier::engineer, "Bass power is averaged over this: at least one period of the lowest note."},
    {&BassTrackerParams::onset_delta, "bass.onset_delta", "Onset sensitivity", "", 0.0, 1.0, ParamTier::tune, "Onset function peak above its local mean, as a fraction of the track's 99th percentile. Lower finds more onsets."},
    {&BassTrackerParams::onset_floor, "bass.onset_floor", "Onset floor", "", 0.0, 1.0, ParamTier::engineer, "Minimum onset function peak, as a fraction of the track's 99th percentile."},
    {&BassTrackerParams::onset_mean_window_ms, "bass.onset_mean_window_ms", "Onset mean window", "ms", 10.0, 500.0, ParamTier::engineer, "Length of the centred local mean for the onset threshold."},
    {&BassTrackerParams::onset_min_level_change_db, "bass.onset_min_level_change_db", "Onset level change", "dB", -30.0, 10.0, ParamTier::engineer, "Bass level after an onset minus the level before it must be at least this: rejects the click of an abrupt offset."},
    {&BassTrackerParams::onset_min_gap_ms, "bass.onset_min_gap_ms", "Onset minimum gap", "ms", 5.0, 200.0, ParamTier::engineer, "Of two onsets closer than this, the stronger is kept."},
    {&BassTrackerParams::kick_mask_onset_weight, "bass.kick_mask_onset_weight", "Onset weight in kick mask", "", 0.0, 1.0, ParamTier::engineer, "Onset function scale inside kick masks."},
    {&BassTrackerParams::activity_floor_db, "bass.activity_floor_db", "Bass activity floor", "dB", -80.0, 0.0, ParamTier::tune, "Bass level, relative to the track's 95th percentile, below which there is no note."},
    {&BassTrackerParams::onset_bias_ms, "bass.onset_bias_ms", "Onset time bias", "ms", -20.0, 20.0, ParamTier::engineer, "Subtracted from detected onset times."},
    {&BassTrackerParams::offset_ratio, "bass.offset_ratio", "Offset ratio", "", 0.01, 0.99, ParamTier::tune, "A note ends when its amplitude falls below this fraction of its peak."},
    {&BassTrackerParams::peak_search_ms, "bass.peak_search_ms", "Peak search", "ms", 5.0, 100.0, ParamTier::engineer, "A note's peak is searched this long after its onset."},
    {&BassTrackerParams::offset_bias_ms, "bass.offset_bias_ms", "Offset time bias", "ms", -20.0, 20.0, ParamTier::engineer, "Subtracted from offsets found by the energy threshold."},
    {&BassTrackerParams::pitch_centre_fraction, "bass.pitch_centre_fraction", "Pitch centre fraction", "", 0.05, 1.0, ParamTier::engineer, "Central part of the note whose salience gives its pitch."},
    {&BassTrackerParams::slide_min_part_ms, "bass.slide_min_part_ms", "Slide minimum part", "ms", 10.0, 300.0, ParamTier::tune, "Each pitch of a slide must hold this long; shorter notes are never split."},
    {&BassTrackerParams::slide_bias_ms, "bass.slide_bias_ms", "Slide time bias", "ms", -50.0, 50.0, ParamTier::engineer, "Subtracted from slide split times."},
    {&BassTrackerParams::long_note_slots, "bass.long_note_slots", "Long note", "slots", 1.0, 8.0, ParamTier::tune, "A note longer than this many 16ths is a long note."},
    {&BassTrackerParams::velocity_window_s, "bass.velocity_window_s", "Velocity window", "s", 0.1, 10.0, ParamTier::engineer, "Velocity is relative to the loudest note within this window."},
    {&BassTrackerParams::confidence_margin_db, "bass.confidence_margin_db", "Pitch confidence margin", "dB", 0.5, 30.0, ParamTier::engineer, "Salience margin over the runner-up pitch that gives full pitch confidence."},
};
// clang-format on

} // namespace

Result<BassTrackResult> track_bass(std::span<const float> mono, const FrontEndResult &features,
                                   const Grid &grid, const BassTrackerParams &params) {
    return Tracker(mono, features, grid, params).run();
}

std::vector<BassNoteLabel> to_labels(const std::vector<BassNote> &notes) {
    std::vector<BassNoteLabel> labels;
    for (const BassNote &note : notes) {
        labels.push_back({note.onset_s, note.offset_s, note.pitch_midi, note.cents, note.slide_in,
                          note.slide_out, note.velocity});
    }
    return labels;
}

Result<void> declare_bass_tracker_params(ParamRegistry &registry) {
    for (const ParamEntry &entry : kParamTable) {
        ParamSpec spec;
        spec.id = entry.id;
        spec.label = entry.label;
        spec.unit = entry.unit;
        spec.min = entry.min;
        spec.max = entry.max;
        spec.default_value = kDefaults.*entry.member;
        spec.tier = entry.tier;
        spec.description = entry.description;
        spec.group = "Bass tracker";
        if (Result<void> declared = registry.declare(spec); !declared) {
            return declared;
        }
    }
    return {};
}

BassTrackerParams bass_tracker_params_from(const ParamValues &values) {
    BassTrackerParams params;
    for (const ParamEntry &entry : kParamTable) {
        if (const auto it = values.find(entry.id); it != values.end()) {
            params.*entry.member = it->second;
        }
    }
    return params;
}

} // namespace psyight
