#include "analysis/beat_tracker.h"

#include "analysis/series.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>

namespace psyight {

namespace {

constexpr int kBeatsPerBar = kSlotsPerBar / kSlotsPerBeat;
constexpr int kFeatureBands = 24; // Log-spectrogram rows grouped for the novelty features.
constexpr int kNoveltyBars = 4;   // Bars compared on each side of a candidate phrase start.
constexpr double kNoveltyClipQuantile = 0.95;
constexpr double kOtherPeakFraction = 0.05; // Tempo peaks within 5% are the same peak.
constexpr int kFitIterations = 3;

int band_index(const FrontEndResult &features, std::string_view name) {
    for (size_t b = 0; b < features.bands.size(); ++b) {
        if (features.bands[b].band.name == name) {
            return static_cast<int>(b);
        }
    }
    return -1;
}

std::vector<float> normalised(std::vector<float> x) {
    const double norm = quantile(x, 0.99);
    for (float &v : x) {
        v = norm > 0.0 ? static_cast<float>(v / norm) : 0.0f;
    }
    return x;
}

// Euclidean distance between the means of features[a, a + n) and [b, b + n), rows of width.
double mean_distance(const std::vector<std::vector<double>> &features, int a, int b, int n) {
    double sum = 0.0;
    const size_t width = features.front().size();
    for (size_t j = 0; j < width; ++j) {
        double mean_a = 0.0;
        double mean_b = 0.0;
        for (int k = 0; k < n; ++k) {
            mean_a += features[a + k][j];
            mean_b += features[b + k][j];
        }
        const double d = (mean_a - mean_b) / n;
        sum += d * d;
    }
    return std::sqrt(sum);
}

// Best of `period` phases of novelty[i] (phase of i is (i + offset) mod period), and its
// margin over the next best, relative to the best. Negative values are missing. Values are
// clipped at their 95th percentile first: one boundary next to silence (an intro, an ending)
// would otherwise decide the phase alone, while the rare real changes stay near the top.
void best_phase(const std::vector<double> &novelty, int offset, int period, int &phase,
                double &confidence) {
    std::vector<float> present;
    for (const double v : novelty) {
        if (v >= 0.0) {
            present.push_back(static_cast<float>(v));
        }
    }
    const double clip = quantile(present, kNoveltyClipQuantile);
    std::vector<double> sum(period, 0.0);
    std::vector<int> count(period, 0);
    for (int i = 0; i < static_cast<int>(novelty.size()); ++i) {
        if (novelty[i] >= 0.0) {
            const int p = ((i + offset) % period + period) % period;
            sum[p] += std::min(novelty[i], clip);
            ++count[p];
        }
    }
    std::vector<double> mean(period, 0.0);
    for (int p = 0; p < period; ++p) {
        mean[p] = count[p] > 0 ? sum[p] / count[p] : 0.0;
    }
    phase = static_cast<int>(std::max_element(mean.begin(), mean.end()) - mean.begin());
    double second = 0.0;
    for (int p = 0; p < period; ++p) {
        if (p != phase) {
            second = std::max(second, mean[p]);
        }
    }
    confidence = mean[phase] > 0.0 ? (mean[phase] - second) / mean[phase] : 0.0;
}

} // namespace

Result<BeatTrackResult> track_beats(const FrontEndResult &f, const BeatTrackerParams &p) {
    const int sub = band_index(f, "sub");
    const int bass = band_index(f, "bass");
    const int click = band_index(f, "click");
    if (sub < 0 || bass < 0 || click < 0) {
        return Error{"beat tracker needs the front-end bands sub, bass and click"};
    }
    if (!(p.min_bpm > 0.0 && p.min_bpm < p.max_bpm) || p.bars_per_phrase < 1.0) {
        return Error{"invalid beat tracker parameters"};
    }
    const double frame_s = static_cast<double>(f.hop_samples) / f.sample_rate_hz;
    const int frames = f.frames;
    BeatTrackResult result;
    const double max_period_frames = 60.0 / p.min_bpm / frame_s;
    if (frames < 8 * max_period_frames) {
        return result; // Too short for a tempo: no beats.
    }

    // 1. Onset function.
    const int half = static_cast<int>(std::lround(p.envelope_window_ms * 1e-3 / frame_s / 2.0));
    std::vector<float> low_power(frames);
    for (int i = 0; i < frames; ++i) {
        low_power[i] = static_cast<float>(std::pow(10.0, f.bands[sub].energy_db[i] / 10.0) +
                                          std::pow(10.0, f.bands[bass].energy_db[i] / 10.0));
    }
    const std::vector<float> smooth = box_mean(low_power, half);
    std::vector<float> rise(frames, 0.0f);
    for (int i = 0; i < frames; ++i) {
        const double after = smooth[std::min(frames - 1, i + half + 1)];
        const double before = smooth[std::max(0, i - half - 1)];
        rise[i] = static_cast<float>(
            std::max(0.0, 10.0 * std::log10((after + 1e-12) / (before + 1e-12))));
    }
    const std::vector<float> rise_n = normalised(rise);
    const std::vector<float> click_n = normalised(f.bands[click].flux);
    std::vector<float> &odf = result.onset_function;
    odf.resize(frames);
    for (int i = 0; i < frames; ++i) {
        odf[i] = std::sqrt(rise_n[i] * click_n[i]);
    }

    // 2. Tempo.
    double mean = 0.0;
    for (const float v : odf) {
        mean += v;
    }
    mean /= frames;
    const int min_lag = static_cast<int>(std::ceil(60.0 / p.max_bpm / frame_s));
    const int max_lag = static_cast<int>(std::floor(max_period_frames));
    const auto autocorrelation = [&](int lag) {
        double sum = 0.0;
        for (int i = 0; i + lag < frames; ++i) {
            sum += (odf[i] - mean) * (odf[i + lag] - mean);
        }
        return sum / (frames - lag);
    };
    std::vector<double> score(max_lag + 2, 0.0);
    for (int lag = min_lag - 1; lag <= max_lag + 1; ++lag) {
        const double bpm = 60.0 / (lag * frame_s);
        const double octaves = std::log2(bpm / p.prior_bpm) / p.prior_width_octaves;
        score[lag] = (autocorrelation(lag) + 0.5 * autocorrelation(2 * lag) +
                      0.25 * autocorrelation(4 * lag)) *
                     std::exp(-0.5 * octaves * octaves);
    }
    int best_lag = min_lag;
    for (int lag = min_lag; lag <= max_lag; ++lag) {
        best_lag = score[lag] > score[best_lag] ? lag : best_lag;
    }
    if (score[best_lag] <= 0.0) {
        return result; // No periodicity: no beats.
    }
    const double period_frames =
        best_lag + parabolic_offset(score[best_lag - 1], score[best_lag], score[best_lag + 1]);
    double other = 0.0;
    for (int lag = min_lag; lag <= max_lag; ++lag) {
        if (std::abs(lag - period_frames) > kOtherPeakFraction * period_frames) {
            other = std::max(other, score[lag]);
        }
    }
    result.tempo_confidence = std::clamp((score[best_lag] - other) / score[best_lag], 0.0, 1.0);

    // 3. Beats by dynamic programming over frames.
    double sq = 0.0;
    for (const float v : odf) {
        sq += (v - mean) * (v - mean);
    }
    const double deviation = std::sqrt(sq / frames);
    std::vector<double> cumulative(frames);
    std::vector<int> back(frames, -1);
    const int earliest = static_cast<int>(std::lround(2.0 * period_frames));
    const int latest = static_cast<int>(std::lround(0.5 * period_frames));
    for (int i = 0; i < frames; ++i) {
        const double local = deviation > 0.0 ? odf[i] / deviation : 0.0;
        double best = -std::numeric_limits<double>::infinity();
        for (int j = std::max(0, i - earliest); j <= i - latest; ++j) {
            const double deviation_log = std::log((i - j) / period_frames);
            const double candidate = cumulative[j] - p.tightness * deviation_log * deviation_log;
            if (candidate > best) {
                best = candidate;
                back[i] = j;
            }
        }
        cumulative[i] = local + (back[i] >= 0 ? std::max(0.0, best) : 0.0);
        if (back[i] >= 0 && best <= 0.0) {
            back[i] = -1; // Starting a new chain beats a poor predecessor.
        }
    }
    int last = frames - 1;
    for (int i = std::max(0, frames - static_cast<int>(period_frames)); i < frames; ++i) {
        last = cumulative[i] > cumulative[last] ? i : last;
    }
    std::vector<int> beat_frames;
    for (int i = last; i >= 0; i = back[i]) {
        beat_frames.push_back(i);
    }
    std::reverse(beat_frames.begin(), beat_frames.end());
    for (const int i : beat_frames) {
        const double position =
            i > 0 && i + 1 < frames ? i + parabolic_offset(odf[i - 1], odf[i], odf[i + 1]) : i;
        result.beat_times_s.push_back(position * frame_s - p.beat_bias_ms * 1e-3);
    }

    // 4. Constant-tempo fit to the strong beats.
    std::vector<float> strengths;
    for (const int i : beat_frames) {
        strengths.push_back(odf[i]);
    }
    const double strong = quantile(strengths, p.fit_strength_quantile);
    std::vector<double> anchors;
    for (size_t k = 0; k < beat_frames.size(); ++k) {
        if (odf[beat_frames[k]] >= strong) {
            anchors.push_back(result.beat_times_s[k]);
        }
    }
    if (anchors.size() < 4) {
        return result;
    }
    double period_s = period_frames * frame_s;
    double origin_s = anchors.front();
    std::vector<bool> used(anchors.size(), true);
    for (int iteration = 0; iteration < kFitIterations; ++iteration) {
        double n = 0.0, sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
        for (size_t k = 0; k < anchors.size(); ++k) {
            const double index = std::round((anchors[k] - origin_s) / period_s);
            const double residual = anchors[k] - (origin_s + index * period_s);
            used[k] = iteration == 0 || std::abs(residual) <= p.fit_outlier_ms * 1e-3;
            if (used[k]) {
                n += 1.0;
                sx += index;
                sy += anchors[k];
                sxx += index * index;
                sxy += index * anchors[k];
            }
        }
        const double denominator = n * sxx - sx * sx;
        if (n < 4.0 || denominator <= 0.0) {
            break;
        }
        period_s = (n * sxy - sx * sy) / denominator;
        origin_s = (sy - period_s * sx) / n;
    }
    result.bpm = 60.0 / period_s;

    // 5. Downbeat and phrase from bar-level novelty of band energies and bass pitch salience. Each
    // group is centred per dimension and scaled by one deviation for the whole group, so neither
    // group dominates and near-constant dimensions stay small (scaling each dimension alone
    // amplified frame-rounding noise into a false two-beat pattern).
    const LogSpectrogram &spec = f.spectrogram;
    const LogSpectrogram &salience = f.pitch_salience;
    const int dimensions = kFeatureBands + salience.rows;
    const int first_beat = static_cast<int>(std::ceil(-origin_s / period_s));
    const int last_beat =
        static_cast<int>(std::floor((frames * frame_s - origin_s) / period_s)) - 1;
    std::vector<std::vector<double>> beat_features;
    for (int k = first_beat; k <= last_beat; ++k) {
        const int begin = std::clamp(
            static_cast<int>(std::lround((origin_s + k * period_s) / frame_s)), 0, frames - 1);
        const int end =
            std::clamp(static_cast<int>(std::lround((origin_s + (k + 1) * period_s) / frame_s)),
                       begin + 1, frames);
        std::vector<double> feature(dimensions, 0.0);
        for (int i = begin; i < end; ++i) {
            for (int r = 0; r < spec.rows; ++r) {
                feature[r * kFeatureBands / spec.rows] +=
                    spec.db[static_cast<size_t>(i) * spec.rows + r] * kFeatureBands / spec.rows;
            }
            for (int r = 0; r < salience.rows; ++r) {
                feature[kFeatureBands + r] +=
                    salience.db[static_cast<size_t>(i) * salience.rows + r];
            }
        }
        for (double &v : feature) {
            v /= end - begin;
        }
        beat_features.push_back(feature);
    }
    for (const auto [group_begin, group_end] :
         {std::pair{0, kFeatureBands}, std::pair{kFeatureBands, dimensions}}) {
        double group_sq = 0.0;
        for (int j = group_begin; j < group_end && !beat_features.empty(); ++j) {
            double sum = 0.0;
            for (const std::vector<double> &feature : beat_features) {
                sum += feature[j];
            }
            const double feature_mean = sum / static_cast<double>(beat_features.size());
            for (std::vector<double> &feature : beat_features) {
                feature[j] -= feature_mean;
                group_sq += feature[j] * feature[j];
            }
        }
        const double count = static_cast<double>(beat_features.size()) * (group_end - group_begin);
        const double sd = count > 0.0 ? std::sqrt(group_sq / count) : 0.0;
        for (std::vector<double> &feature : beat_features) {
            for (int j = group_begin; j < group_end; ++j) {
                feature[j] = sd > 1e-9 ? feature[j] / sd : 0.0;
            }
        }
    }
    const int beats = static_cast<int>(beat_features.size());
    std::vector<double> beat_novelty(beats, -1.0);
    for (int k = kBeatsPerBar; k + kBeatsPerBar <= beats; ++k) {
        beat_novelty[k] = mean_distance(beat_features, k, k - kBeatsPerBar, kBeatsPerBar);
    }
    int downbeat_phase = 0;
    best_phase(beat_novelty, first_beat, kBeatsPerBar, downbeat_phase, result.downbeat_confidence);
    // Beat index k (from origin_s) is a downbeat when k = downbeat_phase (mod 4).
    int first_downbeat = first_beat;
    while (((first_downbeat % kBeatsPerBar) + kBeatsPerBar) % kBeatsPerBar != downbeat_phase) {
        ++first_downbeat;
    }
    const double first_downbeat_s = origin_s + first_downbeat * period_s;
    result.grid = {result.bpm, seconds_to_samples(first_downbeat_s, f.sample_rate_hz),
                   f.sample_rate_hz};

    const auto phrase = static_cast<int>(std::lround(p.bars_per_phrase));
    std::vector<std::vector<double>> bar_features;
    for (int k = first_downbeat - first_beat; k + kBeatsPerBar <= beats; k += kBeatsPerBar) {
        std::vector<double> feature(dimensions, 0.0);
        for (int j = 0; j < kBeatsPerBar; ++j) {
            for (int b = 0; b < dimensions; ++b) {
                feature[b] += beat_features[k + j][b] / kBeatsPerBar;
            }
        }
        bar_features.push_back(feature);
    }
    const int bars = static_cast<int>(bar_features.size());
    std::vector<double> bar_novelty(bars, -1.0);
    for (int m = kNoveltyBars; m + kNoveltyBars <= bars; ++m) {
        bar_novelty[m] = mean_distance(bar_features, m, m - kNoveltyBars, kNoveltyBars);
    }
    if (bars >= 2 * phrase) {
        best_phase(bar_novelty, 0, phrase, result.phrase_bar_offset, result.phrase_confidence);
    }
    return result;
}

namespace {

struct ParamEntry {
    double BeatTrackerParams::*member;
    const char *id;
    const char *label;
    const char *unit;
    double min;
    double max;
    ParamTier tier;
    const char *description;
};

const BeatTrackerParams kDefaults;

// clang-format off
const ParamEntry kParamTable[] = {
    {&BeatTrackerParams::min_bpm, "beat.min_bpm", "Minimum tempo", "BPM", 40.0, 300.0, ParamTier::tune, "Lowest tempo considered. Keep above half the expected tempo to avoid half-tempo errors."},
    {&BeatTrackerParams::max_bpm, "beat.max_bpm", "Maximum tempo", "BPM", 40.0, 300.0, ParamTier::tune, "Highest tempo considered. Keep below twice the expected tempo to avoid double-tempo errors."},
    {&BeatTrackerParams::prior_bpm, "beat.prior_bpm", "Expected tempo", "BPM", 40.0, 300.0, ParamTier::tune, "Centre of the tempo prior."},
    {&BeatTrackerParams::prior_width_octaves, "beat.prior_width_octaves", "Tempo prior width", "octaves", 0.05, 4.0, ParamTier::engineer, "Standard deviation of the log2 tempo prior."},
    {&BeatTrackerParams::tightness, "beat.tightness", "Beat tightness", "", 0.0, 1000.0, ParamTier::engineer, "Cost on deviations from the tempo between beats (Ellis 2007)."},
    {&BeatTrackerParams::envelope_window_ms, "beat.envelope_window_ms", "Low-band window", "ms", 5.0, 100.0, ParamTier::engineer, "The low band's level is averaged over this before its rise is measured."},
    {&BeatTrackerParams::fit_strength_quantile, "beat.fit_strength_quantile", "Grid fit strength", "", 0.0, 0.95, ParamTier::engineer, "Beats stronger than this quantile anchor the constant-tempo grid."},
    {&BeatTrackerParams::fit_outlier_ms, "beat.fit_outlier_ms", "Grid fit outlier", "ms", 1.0, 200.0, ParamTier::engineer, "Beats further than this from the fitted grid are dropped from the fit."},
    {&BeatTrackerParams::beat_bias_ms, "beat.beat_bias_ms", "Beat time bias", "ms", -30.0, 30.0, ParamTier::engineer, "Subtracted from detected beat times."},
    {&BeatTrackerParams::bars_per_phrase, "beat.bars_per_phrase", "Bars per phrase", "bars", 1.0, 64.0, ParamTier::tune, "Phrases are counted in groups of this many bars."},
};
// clang-format on

} // namespace

Result<void> declare_beat_tracker_params(ParamRegistry &registry) {
    for (const ParamEntry &entry : kParamTable) {
        ParamSpec spec;
        spec.id = entry.id;
        spec.label = entry.label;
        spec.unit = entry.unit;
        spec.min = entry.min;
        spec.max = entry.max;
        spec.default_value = kDefaults.*entry.member;
        spec.step = entry.member == &BeatTrackerParams::bars_per_phrase ? 1.0 : 0.0;
        spec.tier = entry.tier;
        spec.description = entry.description;
        spec.group = "Beat tracker";
        if (Result<void> declared = registry.declare(spec); !declared) {
            return declared;
        }
    }
    return {};
}

BeatTrackerParams beat_tracker_params_from(const ParamValues &values) {
    BeatTrackerParams params;
    for (const ParamEntry &entry : kParamTable) {
        if (const auto it = values.find(entry.id); it != values.end()) {
            params.*entry.member = it->second;
        }
    }
    return params;
}

} // namespace psyight
