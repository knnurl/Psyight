#include "analysis/live_analysis.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace psyight {

namespace {

constexpr double kResidualMinHz = 400.0;

float sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }

bool is_peak(float before, float peak, float after, double threshold) {
    return peak >= static_cast<float>(threshold) && peak >= before && peak > after;
}

} // namespace

const char *live_section_name(LiveSection section) {
    switch (section) {
    case LiveSection::intro:
        return "intro";
    case LiveSection::groove:
        return "groove";
    case LiveSection::breakdown:
        return "breakdown";
    case LiveSection::build:
        return "build";
    case LiveSection::drop:
        return "drop";
    }
    return "groove";
}

Result<void> LiveAnalyser::prepare(int sample_rate_hz, const StudentModel &model,
                                   const LiveParams &params) {
    model_ = &model;
    params_ = params;
    sample_rate_hz_ = sample_rate_hz;
    if (Result<void> r = features_.prepare(sample_rate_hz); !r) {
        return r;
    }
    if (features_.dims() != model.dims) {
        return Error{"live analysis: the student expects " + std::to_string(model.dims) +
                     " features, not " + std::to_string(features_.dims())};
    }
    const StudentFeatureParams &fp = features_.params();
    frame_s_ = static_cast<double>(fp.hop_samples) / sample_rate_hz;
    student_ = std::make_unique<StudentStream>(model);
    pending_.assign(static_cast<size_t>(fp.hop_samples), 0.0f);
    feature_frame_.assign(static_cast<size_t>(model.dims), 0.0f);
    binary_.assign(kStudentBinaryHeads, 0.0f);
    pitch_.assign(static_cast<size_t>(model.pitches), 0.0f);
    pitch_counts_.assign(static_cast<size_t>(model.pitches), 0);
    beat_.prepare(params);
    // As student_data.cpp's log bands: count + 2 log-spaced edges, band k centred on edge k + 1.
    short_centres_hz_.resize(static_cast<size_t>(fp.short_bands));
    for (int k = 0; k < fp.short_bands; ++k) {
        short_centres_hz_[static_cast<size_t>(k)] = static_cast<float>(
            fp.short_min_hz * std::pow(fp.short_max_hz / fp.short_min_hz,
                                       static_cast<double>(k + 1) / (fp.short_bands + 1)));
    }
    reset();
    return {};
}

void LiveAnalyser::reset() {
    quiet_frames_ = 0;
    features_.reset();
    student_->reset();
    pending_count_ = 0;
    input_frames_ = 0;
    ring_ = {};
    last_kick_frame_ = last_hat_frame_ = last_onset_frame_ = -1000;
    note_frame_ = -1;
    scan_ = 0;
    candidate_count_ = 0;
    recent_kick_frames_.fill(-1000);
    std::fill(pitch_counts_.begin(), pitch_counts_.end(), 0);
    beat_.reset();
    state_ = LiveState{};
    state_.phrase_bars = params_.phrase_bars;
    state_.latency_s = (model_->lookahead + 3) * frame_s_;
    recorded_kicks_.clear();
    recorded_notes_.clear();
    recorded_states_.clear();
    beat_now_ = std::numeric_limits<long long>::min();
    beat_sum_ = {};
    beat_frames_ = 0;
    beat_means_ = {};
    beats_summed_ = 0;
    phase_novelty_ = {};
    novelty_mean_ = 0.0;
    novelty_count_ = 0;
    run_frame(); // Frame 0 describes the silence before the first sample, as in file mode.
}

void LiveAnalyser::apply(const LiveCommand &c) {
    switch (c.kind) {
    case LiveCommand::Kind::downbeat:
        LiveBeat::set_downbeat(state_, c.value);
        break;
    case LiveCommand::Kind::nudge:
        LiveBeat::nudge(state_, c.value);
        break;
    case LiveCommand::Kind::relock:
        beat_.relock(state_);
        break;
    }
}

void LiveAnalyser::process(std::span<const float> samples) {
    for (const float s : samples) {
        pending_[pending_count_++] = s;
        if (pending_count_ == pending_.size()) {
            features_.push(pending_);
            pending_count_ = 0;
            run_frame();
        }
    }
}

void LiveAnalyser::run_frame() {
    // Silence (the music stopped): the hop's RMS below the silence level for silence_s.
    double energy = 0.0;
    for (const float x : pending_) {
        energy += static_cast<double>(x) * x;
    }
    const double rms = pending_.empty() ? 0.0 : std::sqrt(energy / pending_.size());
    if (rms < params_.silence_rms) {
        ++quiet_frames_;
    } else {
        quiet_frames_ = 0;
        state_.silent = false;
    }
    if (!state_.silent && static_cast<double>(quiet_frames_) * frame_s_ >= params_.silence_s) {
        state_.silent = true;
        state_.silent_since_s = static_cast<double>(input_frames_) * frame_s_;
        beat_.on_silence(state_);
    }
    features_.frame(feature_frame_);
    // The residual's timbre from this input frame's bands above 400 Hz.
    double power_sum = 0.0, weighted = 0.0, log_sum = 0.0;
    int count = 0;
    for (size_t k = 0; k < short_centres_hz_.size(); ++k) {
        if (short_centres_hz_[k] < kResidualMinHz) {
            continue;
        }
        const double db = 60.0 * feature_frame_[k] - 60.0; // Undo (dB + 60) / 60.
        const double power = std::pow(10.0, db / 10.0);
        power_sum += power;
        weighted += power * short_centres_hz_[k];
        log_sum += db / 10.0 * std::log(10.0);
        ++count;
    }
    Frame &own = at(input_frames_);
    own.centroid_hz = power_sum > 0.0 ? static_cast<float>(weighted / power_sum) : 0.0f;
    own.flatness = count > 0 && power_sum > 0.0
                       ? static_cast<float>(std::exp(log_sum / count) / (power_sum / count))
                       : 0.0f;

    add_beat_features();
    quantise_student_features(feature_frame_, model_->features_scale);
    student_->process(feature_frame_, binary_, pitch_);
    const long long j = input_frames_ - model_->lookahead;
    ++input_frames_;
    if (j >= 0) {
        decode_frame(j);
    }
}

void LiveAnalyser::add_beat_features() {
    if (!state_.tempo_known) {
        return;
    }
    const double t_s = static_cast<double>(input_frames_) * frame_s_;
    const auto beat = static_cast<long long>(std::floor(LiveBeat::position(state_, t_s)));
    if (beat != beat_now_) {
        if (beat_frames_ > 0 && beat == beat_now_ + 1) {
            std::array<double, kGroups> &mean = beat_means_[static_cast<size_t>(beat_now_ & 7)];
            for (int g = 0; g < kGroups; ++g) {
                mean[static_cast<size_t>(g)] = beat_sum_[static_cast<size_t>(g)] / beat_frames_;
            }
            ++beats_summed_;
            if (beats_summed_ >= 8) {
                // Novelty at k = beat_now_ - 3: beats k..k+3 against k-4..k-1.
                const long long k = beat_now_ - 3;
                double sum = 0.0;
                for (int g = 0; g < kGroups; ++g) {
                    double after = 0.0, before = 0.0;
                    for (int i = 0; i < 4; ++i) {
                        after +=
                            beat_means_[static_cast<size_t>((k + i) & 7)][static_cast<size_t>(g)];
                        before += beat_means_[static_cast<size_t>((k - 4 + i) & 7)]
                                             [static_cast<size_t>(g)];
                    }
                    const double d = (after - before) / 4.0;
                    sum += d * d;
                }
                const double novelty = std::sqrt(sum);
                ++novelty_count_;
                novelty_mean_ += (novelty - novelty_mean_) / static_cast<double>(novelty_count_);
                const double clipped = std::min(novelty, 3.0 * novelty_mean_);
                const double a = 1.0 / (4.0 * params_.novelty_memory_bars / 4.0 + 1.0);
                double &phase = phase_novelty_[static_cast<size_t>(((k % 4) + 4) % 4)];
                phase += a * 4.0 * (clipped - phase); // Each phase is updated once a bar.
                // Only until the first kick break: on OculusDrift (labelled by ear) novelty
                // overrode correct break downbeats (bar position 90% to 77%).
                const bool any_break =
                    state_.last_break_beat != std::numeric_limits<long long>::min();
                if (!any_break && novelty_count_ >= 4LL * params_.novelty_min_bars) {
                    const auto best = static_cast<int>(
                        std::max_element(phase_novelty_.begin(), phase_novelty_.end()) -
                        phase_novelty_.begin());
                    double second = 0.0;
                    for (int p = 0; p < 4; ++p) {
                        if (p != best) {
                            second = std::max(second, phase_novelty_[static_cast<size_t>(p)]);
                        }
                    }
                    const double top = phase_novelty_[static_cast<size_t>(best)];
                    if (top > 0.0 && (top - second) / top >= params_.downbeat_margin) {
                        const long long current = ((state_.downbeat_beat % 4) + 4) % 4;
                        if (current != best) {
                            state_.downbeat_beat += best - current;
                        }
                        state_.downbeat_from_novelty = true;
                    }
                }
            }
        } else if (beat != beat_now_ + 1) {
            beats_summed_ = 0; // A jump (a reacquisition): start the beat history again.
        }
        beat_now_ = beat;
        beat_sum_ = {};
        beat_frames_ = 0;
    }
    const StudentFeatureParams &fp = features_.params();
    for (int g = 0; g < 24; ++g) { // 24 groups of 4 short bands, then of 4 long bands.
        double short_sum = 0.0, long_sum = 0.0;
        for (int i = 0; i < 4; ++i) {
            short_sum += feature_frame_[static_cast<size_t>(4 * g + i)];
            long_sum += feature_frame_[static_cast<size_t>(fp.short_bands + 4 * g + i)];
        }
        beat_sum_[static_cast<size_t>(g)] += short_sum / 4.0;
        beat_sum_[static_cast<size_t>(24 + g)] += long_sum / 4.0;
    }
    ++beat_frames_;
}

void LiveAnalyser::decode_frame(long long j) {
    const StudentDecoderParams &d = model_->decoder;
    Frame &f = at(j);
    f.kick = sigmoid(binary_[kKickOnset]);
    f.hat = sigmoid(binary_[kHatOnset]);
    f.onset = sigmoid(binary_[kBassOnset]);
    f.gate = sigmoid(binary_[kBassGate]);
    f.residual = sigmoid(binary_[kResidual]);
    f.pitch_class =
        static_cast<int>(std::max_element(pitch_.begin(), pitch_.end()) - pitch_.begin());
    const double t_s = static_cast<double>(j) * frame_s_;
    state_.frames = j + 1;
    state_.analysed_s = t_s;
    const double alpha = 1.0 - std::exp(-frame_s_ / params_.residual_smoothing_s);
    // The timbre frame is two frames newer than the heads' (the student's lookahead); close
    // enough for a smoothed colour input.
    const Frame &newest = at(input_frames_ - 1);
    state_.residual_level += alpha * (f.residual - state_.residual_level);
    state_.residual_centroid_hz += alpha * (newest.centroid_hz - state_.residual_centroid_hz);
    state_.residual_flatness += alpha * (newest.flatness - state_.residual_flatness);

    if (j >= 2) {
        const Frame &a = at(j - 2);
        const Frame &b = at(j - 1);
        const long long peak = j - 1;
        if (is_peak(a.kick, b.kick, f.kick, params_.kick_threshold) &&
            peak - last_kick_frame_ >= params_.event_min_gap_frames) {
            last_kick_frame_ = peak;
            std::rotate(recent_kick_frames_.begin(), recent_kick_frames_.begin() + 1,
                        recent_kick_frames_.end());
            recent_kick_frames_.back() = peak;
            on_kick(static_cast<double>(peak) * frame_s_);
        }
        if (is_peak(a.hat, b.hat, f.hat, params_.hat_threshold) &&
            peak - last_hat_frame_ >= params_.event_min_gap_frames) {
            last_hat_frame_ = peak;
            state_.last_hat_s = static_cast<double>(peak) * frame_s_;
            ++state_.hats;
        }
        if (is_peak(a.onset, b.onset, f.onset, d.onset_threshold) &&
            candidate_count_ < static_cast<int>(candidates_.size())) {
            candidates_[static_cast<size_t>(candidate_count_++)] = peak;
        }
    }
    // Bass onsets three frames old: every kick that could mask them is known.
    int kept = 0;
    for (int i = 0; i < candidate_count_; ++i) {
        const long long c = candidates_[static_cast<size_t>(i)];
        if (c > j - 3) {
            candidates_[static_cast<size_t>(kept++)] = c;
            continue;
        }
        const bool masked =
            d.kick_mask_frames > 0 &&
            std::any_of(recent_kick_frames_.begin(), recent_kick_frames_.end(),
                        [&](long long k) { return std::abs(k - c) <= d.kick_mask_frames; });
        if (!masked && c - last_onset_frame_ >= d.min_gap_frames) {
            double offset = 0.0;
            if (d.subframe) { // As m4_decode.parabolic.
                const float y0 = at(c - 1).onset, y1 = at(c).onset, y2 = at(c + 1).onset;
                const float denominator = y0 - 2.0f * y1 + y2;
                offset = denominator < 0.0f
                             ? std::clamp(0.5f * (y0 - y2) / denominator, -0.5f, 0.5f)
                             : 0.0f;
            }
            begin_note(c, (static_cast<double>(c) + offset) * frame_s_);
        }
    }
    candidate_count_ = kept;
    if (state_.bass_active) {
        scan_note(j);
    }
    beat_.update(state_, t_s);
    if (recording_) {
        recorded_states_.push_back(state_);
    }
}

void LiveAnalyser::begin_note(long long frame, double onset_s) {
    if (state_.bass_active) {
        finish_note(onset_s); // The next onset ends the sounding note.
    }
    last_onset_frame_ = frame;
    note_frame_ = frame;
    scan_ = frame;
    std::fill(pitch_counts_.begin(), pitch_counts_.end(), 0);
    state_.bass_active = true;
    state_.bass_onset_s = onset_s;
    state_.bass_confidence = at(frame).onset;
    state_.bass_midi = model_->first_midi + at(frame).pitch_class;
    ++state_.bass_notes;
}

void LiveAnalyser::scan_note(long long j) {
    const StudentDecoderParams &d = model_->decoder;
    const auto threshold = static_cast<float>(d.gate_threshold);
    for (; scan_ <= j; ++scan_) {
        const Frame &f = at(scan_);
        if (scan_ >= note_frame_ + d.min_frames && f.gate < threshold) {
            double end = static_cast<double>(scan_);
            const float before = at(scan_ - 1).gate;
            if (d.subframe && before > f.gate) {
                end = static_cast<double>(scan_ - 1) + (before - threshold) / (before - f.gate);
            }
            finish_note(end * frame_s_);
            return;
        }
        ++pitch_counts_[static_cast<size_t>(f.pitch_class)];
        state_.bass_midi =
            model_->first_midi +
            static_cast<int>(std::max_element(pitch_counts_.begin(), pitch_counts_.end()) -
                             pitch_counts_.begin());
    }
}

void LiveAnalyser::finish_note(double end_s) {
    state_.bass_active = false;
    state_.bass_end_s = std::max(end_s, state_.bass_onset_s);
    const double length_s = state_.bass_end_s - state_.bass_onset_s;
    state_.bass_typical_s += 0.3 * (length_s - state_.bass_typical_s);
    if (recording_) {
        BassNote note;
        note.onset_s = state_.bass_onset_s;
        note.offset_s = state_.bass_end_s;
        note.duration_ms = 1e3 * length_s;
        note.pitch_midi = state_.bass_midi;
        note.octave = state_.bass_midi / 12 - 1;
        note.velocity = 1.0;
        note.confidence = state_.bass_confidence;
        if (state_.tempo_known) {
            note.slot_len_ms = 1e3 * state_.period_s / 4.0;
            note.gate_ratio = length_s / (state_.period_s / 4.0);
        }
        recorded_notes_.push_back(note);
    }
}

void LiveAnalyser::on_kick(double t_s) {
    ++state_.kicks;
    state_.last_kick_s = t_s;
    if (recording_) {
        recorded_kicks_.push_back(t_s);
    }
    beat_.on_kick(state_, t_s);
}

void LiveBeat::set_downbeat(LiveState &s, double t_s) {
    if (!s.tempo_known) {
        return;
    }
    s.downbeat_beat = std::llround(position(s, t_s));
    s.last_break_beat = s.downbeat_beat;
    s.downbeat_from_novelty = false;
}

void LiveBeat::nudge(LiveState &s, double seconds) {
    if (s.tempo_known) {
        s.anchor_s += seconds;
    }
}

void LiveBeat::on_silence(LiveState &s) {
    relock(s);
    s.tempo_known = false; // No beat to predict: the lights stop pulsing.
    s.kick_streak = 0;
    s.riser = 0.0;
    s.section_energy = 0;
    if (s.section != LiveSection::intro) {
        set_section(s, LiveSection::intro, s.silent_since_s);
    }
}

void LiveBeat::relock(LiveState &s) {
    acquiring_ = true;
    s.locked = false;
    acquire_kicks_s_.clear();
    off_grid_kicks_ = 0;
    in_lock_kicks_ = 0;
}

void LiveBeat::prepare(const LiveParams &params) {
    params_ = params;
    acquire_kicks_s_.reserve(static_cast<size_t>(params.acquire_kicks) + 1);
    reset();
}

void LiveBeat::reset() {
    acquire_kicks_s_.clear();
    acquiring_ = true;
    off_grid_kicks_ = 0;
    in_lock_kicks_ = 0;
    breakdown_floor_ = 1.0;
    flatness_slow_ = 0.0;
    level_slow_ = 0.0;
    flatness_floor_ = 1.0;
    groove_mean_ = -1.0;
    energy_phrase_ = 0;
    energy_section_ = -1;
    last_update_s_ = -1.0;
    drop_end_beat_ = 0;
}

double LiveBeat::time_of(const LiveState &s, long long beat) {
    return s.anchor_s + static_cast<double>(beat - s.anchor_beat) * s.period_s;
}

double LiveBeat::position(const LiveState &s, double t_s) {
    return static_cast<double>(s.anchor_beat) + (t_s - s.anchor_s) / s.period_s;
}

void LiveBeat::set_section(LiveState &s, LiveSection section, double start_s) {
    s.has_previous = s.section_count > 0;
    s.previous_section = s.section;
    s.previous_occurrence = std::max(0, s.section_occurrences[static_cast<size_t>(s.section)] - 1);
    s.previous_energy = s.section_energy;
    ++s.section_occurrences[static_cast<size_t>(section)];
    s.section = section;
    s.section_start_s = start_s;
    ++s.section_count;
    if (s.tempo_known) {
        s.phrase_beat = std::llround(position(s, start_s));
    }
}

void LiveBeat::on_kick(LiveState &s, double t_s) {
    const double min_period = 60.0 / params_.max_bpm;
    const double max_period = 60.0 / params_.min_bpm;

    if (acquiring_) {
        if (static_cast<int>(acquire_kicks_s_.size()) > params_.acquire_kicks) {
            acquire_kicks_s_.erase(acquire_kicks_s_.begin());
        }
        acquire_kicks_s_.push_back(t_s);
        if (static_cast<int>(acquire_kicks_s_.size()) <= params_.acquire_kicks) {
            return;
        }
        // Inter-onset intervals folded into the tempo range by octaves; the median, if most
        // agree with it.
        std::array<double, 64> folded{};
        int n = 0;
        for (size_t i = 1; i < acquire_kicks_s_.size() && n < 64; ++i) {
            double ioi = acquire_kicks_s_[i] - acquire_kicks_s_[i - 1];
            if (ioi <= 0.0 || ioi > 4.0 * max_period) {
                continue;
            }
            while (ioi < min_period) {
                ioi *= 2.0;
            }
            while (ioi > max_period) {
                ioi *= 0.5;
            }
            folded[static_cast<size_t>(n++)] = ioi;
        }
        if (n < params_.acquire_kicks / 2) {
            return;
        }
        std::sort(folded.begin(), folded.begin() + n);
        const double median = folded[static_cast<size_t>(n / 2)];
        const auto agree = std::count_if(folded.begin(), folded.begin() + n, [&](double v) {
            return std::abs(v - median) <= params_.acquire_spread * median;
        });
        if (agree * 4 < n * 3) {
            return; // Not a steady pulse yet.
        }
        // Keep beat numbering continuous through a reacquisition (a new track in a mix).
        const long long beat =
            s.tempo_known ? static_cast<long long>(std::llround(position(s, t_s))) : 0;
        const bool first = !s.tempo_known;
        s.tempo_known = true;
        s.locked = false;
        s.period_s = median;
        s.anchor_s = t_s;
        s.anchor_beat = beat;
        s.downbeat_beat = beat;
        s.last_kick_beat = beat;
        s.last_run_beat = beat;
        s.kick_streak = 1;
        acquiring_ = false;
        acquire_kicks_s_.clear();
        off_grid_kicks_ = 0;
        in_lock_kicks_ = 0;
        if (first || s.section != LiveSection::groove) {
            set_section(s, LiveSection::groove, t_s);
        }
        return;
    }

    const double period = s.period_s;
    const auto n = static_cast<long long>(std::llround((t_s - s.anchor_s) / period));
    const double predicted = s.anchor_s + static_cast<double>(n) * period;
    const double error_s = t_s - predicted;
    if (std::abs(error_s) > params_.capture_beats * period) {
        if (++off_grid_kicks_ >= params_.reacquire_kicks) {
            acquiring_ = true; // The beat keeps running until the new tempo is measured.
            s.locked = false;
            acquire_kicks_s_.clear();
            acquire_kicks_s_.push_back(t_s);
        }
        return;
    }
    off_grid_kicks_ = 0;
    const long long beat = s.anchor_beat + n;
    if (beat == s.last_kick_beat) {
        return; // A second detection on the same beat (a double trigger): it neither steers the
                // loop again nor breaks the run.
    }
    const long long beats_since = std::max(1LL, beat - s.last_kick_beat);
    s.anchor_s = predicted + params_.pll_phase_gain * error_s;
    s.anchor_beat = beat;
    s.period_s =
        std::clamp(period + params_.pll_period_gain * error_s / static_cast<double>(beats_since),
                   min_period, max_period);
    s.last_error_ms = 1e3 * error_s;
    in_lock_kicks_ = std::abs(error_s) <= params_.lock_beats * period ? in_lock_kicks_ + 1 : 0;
    // Once locked, the loop stays locked while kicks keep within the capture range (and runs
    // free through breakdowns); only a reacquisition unlocks it.
    s.locked = s.locked || in_lock_kicks_ >= params_.lock_kicks;

    s.kick_streak = beat - s.last_kick_beat == 1 ? s.kick_streak + 1 : 1;
    s.last_kick_beat = beat;
    if (s.kick_streak == params_.run_kicks) { // A run started run_kicks - 1 beats ago.
        const long long start = beat - (params_.run_kicks - 1);
        const long long gap = start - s.last_run_beat;
        const double start_s = time_of(s, start);
        if (gap >= 4) {
            s.downbeat_beat = start; // The kick returns after a bar or more: a downbeat.
            s.last_break_beat = start;
            s.downbeat_from_novelty = false;
        }
        if (gap >= 4LL * params_.drop_after_bars) {
            set_section(s, LiveSection::drop, start_s);
            drop_end_beat_ = start + 4LL * params_.drop_bars;
        } else if (gap >= 4LL * params_.breakdown_bars || s.section == LiveSection::intro ||
                   s.section == LiveSection::breakdown || s.section == LiveSection::build) {
            // A run ends a breakdown or build whatever the gap: the run's start is backdated
            // up to run_kicks - 1 beats, so it can sit nearer the last run than the 2 bars that
            // declared the breakdown.
            set_section(s, LiveSection::groove, start_s);
        }
    }
    if (s.kick_streak >= params_.run_kicks) {
        s.last_run_beat = beat;
    }
}

void LiveBeat::update(LiveState &s, double t_s) {
    if (last_update_s_ >= 0.0 && t_s > last_update_s_) {
        const double alpha =
            1.0 - std::exp(-(t_s - last_update_s_) / params_.build_flatness_smoothing_s);
        flatness_slow_ += alpha * (s.residual_flatness - flatness_slow_);
        level_slow_ += alpha * (s.residual_level - level_slow_);
        if (s.section == LiveSection::groove || s.section == LiveSection::drop) {
            groove_mean_ =
                groove_mean_ < 0.0
                    ? level_slow_
                    : groove_mean_ +
                          (1.0 - std::exp(-(t_s - last_update_s_) / params_.energy_memory_s)) *
                              (level_slow_ - groove_mean_);
        }
    }
    last_update_s_ = t_s;
    if (!s.tempo_known) {
        return;
    }
    const double now_beat = position(s, t_s);
    const double since_kick_beats = now_beat - static_cast<double>(s.last_run_beat);
    const LiveSection section = s.section;
    if ((section == LiveSection::groove || section == LiveSection::drop) &&
        since_kick_beats >= 4.0 * params_.breakdown_bars) {
        // The breakdown began on the beat after the last kick of a run.
        set_section(s, LiveSection::breakdown, time_of(s, s.last_run_beat + 1));
        breakdown_floor_ = level_slow_;
        flatness_floor_ = flatness_slow_;
    } else if (section == LiveSection::breakdown) {
        breakdown_floor_ = std::min(breakdown_floor_, level_slow_);
        flatness_floor_ = std::min(flatness_floor_, flatness_slow_);
        const bool riser = flatness_slow_ >= params_.build_min_flatness &&
                           flatness_slow_ >= params_.build_flatness_ratio * flatness_floor_;
        if (riser || level_slow_ - breakdown_floor_ >= params_.build_rise) {
            set_section(s, LiveSection::build, t_s);
        }
    } else if (section == LiveSection::drop && now_beat >= static_cast<double>(drop_end_beat_)) {
        set_section(s, LiveSection::groove, time_of(s, drop_end_beat_));
    }

    // The riser: 0 when a build starts (flatness build_flatness_ratio over its floor, or the
    // level rule's rise), 1 at the ratio squared (twice the rise); it never falls in the build.
    if (s.section == LiveSection::build) {
        double level = 0.0;
        if (flatness_floor_ > 0.0 && flatness_slow_ > 0.0 && params_.build_flatness_ratio > 1.0) {
            level = std::log(flatness_slow_ / (flatness_floor_ * params_.build_flatness_ratio)) /
                    std::log(params_.build_flatness_ratio);
        }
        if (params_.build_rise > 0.0) {
            level = std::max(level, (level_slow_ - breakdown_floor_ - params_.build_rise) /
                                        params_.build_rise);
        }
        s.riser = std::max(s.riser, std::clamp(level, 0.0, 1.0));
    } else {
        s.riser = 0.0;
    }

    // Energy: low in intros and breakdowns, mid in builds, high in drops; a groove is judged once
    // a phrase (and when it starts) against the grooves' mean activity.
    const long long phrase = static_cast<long long>(
        std::floor((now_beat - static_cast<double>(s.phrase_beat)) / (4.0 * s.phrase_bars)));
    switch (s.section) {
    case LiveSection::intro:
    case LiveSection::breakdown:
        s.section_energy = 0;
        break;
    case LiveSection::build:
        s.section_energy = 1;
        break;
    case LiveSection::drop:
        s.section_energy = 2;
        break;
    case LiveSection::groove:
        if (energy_section_ != s.section_count || phrase != energy_phrase_) {
            const double above = groove_mean_ < 0.0 ? 0.0 : level_slow_ - groove_mean_;
            s.section_energy = above >= params_.energy_step    ? 2
                               : above <= -params_.energy_step ? 0
                                                               : 1;
        }
        break;
    }
    energy_section_ = s.section_count;
    energy_phrase_ = phrase;
}

} // namespace psyight
