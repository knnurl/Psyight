#include "analysis/front_end.h"

#include "analysis/filterbank.h"
#include "analysis/stft.h"
#include "core/timebase.h"

#include <algorithm>
#include <cmath>
#include <future>

namespace psyight {

namespace {

constexpr double kPowerFloor = 1e-12;      // -120 dB.
constexpr float kFluxCompression = 100.0f; // log(1 + C * |X|) before differencing.
constexpr double kSalienceFloor = 1e-5;    // -100 dB, so missing harmonics stay finite.

float power_to_db(double power) {
    return static_cast<float>(10.0 * std::log10(std::max(power, kPowerFloor)));
}

// How one spectrogram row reads the linear-frequency bins.
struct RowMapping {
    int first_bin = 0; // Average |X|^2 over [first_bin, end_bin) ...
    int end_bin = 0;
    double weight = 0.0; // ... or, when the row is narrower than a bin, interpolate between
                         // first_bin and first_bin + 1 with this weight on the upper bin.
};

std::vector<RowMapping> map_rows(const std::vector<double> &row_hz, double bin_hz, int bins) {
    std::vector<RowMapping> mappings;
    const auto rows = static_cast<int>(row_hz.size());
    for (int r = 0; r < rows; ++r) {
        const double low_hz = r == 0 ? row_hz[0] : std::sqrt(row_hz[r - 1] * row_hz[r]);
        const double high_hz = r == rows - 1 ? row_hz[r] : std::sqrt(row_hz[r] * row_hz[r + 1]);
        RowMapping mapping;
        mapping.first_bin = static_cast<int>(std::ceil(low_hz / bin_hz));
        mapping.end_bin = std::min(bins, static_cast<int>(std::ceil(high_hz / bin_hz)));
        if (mapping.end_bin <= mapping.first_bin) {
            const double position = row_hz[r] / bin_hz;
            mapping.first_bin = std::min(bins - 2, static_cast<int>(std::floor(position)));
            mapping.end_bin = mapping.first_bin; // Marks interpolation.
            mapping.weight = position - mapping.first_bin;
        }
        mappings.push_back(mapping);
    }
    return mappings;
}

double row_power(const RowMapping &mapping, const std::vector<float> &magnitudes) {
    if (mapping.end_bin == mapping.first_bin) {
        const double lower = magnitudes[mapping.first_bin];
        const double upper = magnitudes[mapping.first_bin + 1];
        return (1.0 - mapping.weight) * lower * lower + mapping.weight * upper * upper;
    }
    double sum = 0.0;
    for (int bin = mapping.first_bin; bin < mapping.end_bin; ++bin) {
        sum += static_cast<double>(magnitudes[bin]) * magnitudes[bin];
    }
    return sum / (mapping.end_bin - mapping.first_bin);
}

Result<void> validate(const FrontEndParams &params, int sample_rate_hz) {
    const double nyquist_hz = sample_rate_hz / 2.0;
    if (params.hop_samples <= 0 || params.spectrogram_rows < 2 ||
        !(params.spectrogram_min_hz > 0.0 &&
          params.spectrogram_min_hz < params.spectrogram_max_hz &&
          params.spectrogram_max_hz < nyquist_hz)) {
        return Error{"invalid front-end parameters"};
    }
    for (const BandSpec &band : params.bands) {
        if (!(band.low_hz > 0.0 && band.low_hz < band.high_hz && band.high_hz < nyquist_hz)) {
            return Error{"invalid band " + band.name};
        }
    }
    return {};
}

} // namespace

std::vector<BandSpec> default_bands() {
    return {{"sub", 30.0, 80.0},
            {"bass", 80.0, 400.0},
            {"low mid", 400.0, 2000.0},
            {"click", 2500.0, 6000.0},
            {"hats", 8000.0, 12000.0}};
}

Result<FrontEndResult> analyse_front_end(std::span<const float> mono, int sample_rate_hz,
                                         const FrontEndParams &params) {
    if (Result<void> valid = validate(params, sample_rate_hz); !valid) {
        return valid.error();
    }
    Stft spectrogram_stft;
    Stft flux_stft;
    if (Result<void> r =
            spectrogram_stft.prepare(sample_rate_hz, params.spectrogram_window_samples);
        !r) {
        return r.error();
    }
    if (Result<void> r = flux_stft.prepare(sample_rate_hz, params.flux_window_samples); !r) {
        return r.error();
    }
    Stft salience_stft;
    if (Result<void> r = salience_stft.prepare(sample_rate_hz, params.salience_window_samples);
        !r) {
        return r.error();
    }

    const auto length = static_cast<SampleIndex>(mono.size());
    const int hop = params.hop_samples;
    FrontEndResult result;
    result.sample_rate_hz = sample_rate_hz;
    result.hop_samples = hop;
    result.frames = static_cast<int>((length + hop - 1) / hop);

    // The four parts below are independent and each writes only its own members of `result`, so
    // they run in parallel; each is a pure function of the input, so the output stays
    // bit-identical.
    result.bands.resize(params.bands.size());
    for (size_t b = 0; b < params.bands.size(); ++b) {
        result.bands[b].band = params.bands[b];
    }

    // Log-frequency spectrogram.
    LogSpectrogram &spec = result.spectrogram;
    spec.rows = params.spectrogram_rows;
    spec.frames = result.frames;
    for (int r = 0; r < spec.rows; ++r) {
        spec.row_hz.push_back(params.spectrogram_min_hz *
                              std::pow(params.spectrogram_max_hz / params.spectrogram_min_hz,
                                       static_cast<double>(r) / (spec.rows - 1)));
    }
    const std::vector<RowMapping> rows =
        map_rows(spec.row_hz, spectrogram_stft.bin_hz(1), spectrogram_stft.bins());
    spec.db.resize(static_cast<size_t>(spec.frames) * spec.rows);

    // Pitch salience, on the same frame clock with its own longer window.
    LogSpectrogram &salience = result.pitch_salience;
    salience.rows = kSalienceLastMidi - kSalienceFirstMidi + 1;
    salience.frames = result.frames;
    for (int midi = kSalienceFirstMidi; midi <= kSalienceLastMidi; ++midi) {
        salience.row_hz.push_back(440.0 * std::pow(2.0, (midi - 69) / 12.0));
    }
    salience.db.resize(static_cast<size_t>(salience.frames) * salience.rows);
    const double bin_hz = salience_stft.bin_hz(1);
    const int last_bin = salience_stft.bins() - 1;
    std::vector<float> salience_magnitudes(salience_stft.bins());

    auto spectrogram_task = std::async(std::launch::async, [&] {
        std::vector<float> magnitudes(spectrogram_stft.bins());
        for (int frame = 0; frame < result.frames; ++frame) {
            spectrogram_stft.process(mono, static_cast<SampleIndex>(frame) * hop, magnitudes);
            for (int r = 0; r < spec.rows; ++r) {
                spec.db[static_cast<size_t>(frame) * spec.rows + r] =
                    power_to_db(row_power(rows[r], magnitudes));
            }
        }
    });
    auto salience_task = std::async(std::launch::async, [&] {
        for (int frame = 0; frame < result.frames; ++frame) {
            salience_stft.process(mono, static_cast<SampleIndex>(frame) * hop, salience_magnitudes);
            for (int r = 0; r < salience.rows; ++r) {
                double log_sum = 0.0;
                for (int k = 1; k <= kSalienceHarmonics; ++k) {
                    const double position = k * salience.row_hz[r] / bin_hz;
                    const int lower = std::min(last_bin - 1, static_cast<int>(position));
                    const double weight = std::min(1.0, position - lower);
                    const double magnitude = (1.0 - weight) * salience_magnitudes[lower] +
                                             weight * salience_magnitudes[lower + 1];
                    log_sum += std::log10(magnitude + kSalienceFloor);
                }
                salience.db[static_cast<size_t>(frame) * salience.rows + r] =
                    static_cast<float>(20.0 * log_sum / kSalienceHarmonics);
            }
        }
    });

    // Spectral flux: rectified increase of compressed magnitude, per band and over all bins.
    const int flux_bins = flux_stft.bins();
    std::vector<float> previous(flux_bins, 0.0f);
    std::vector<float> current(flux_bins);
    std::vector<std::pair<int, int>> band_bins; // [first, end) per band; at least one bin.
    for (const BandSpec &band : params.bands) {
        int first = static_cast<int>(std::ceil(band.low_hz / flux_stft.bin_hz(1)));
        int end = static_cast<int>(std::ceil(band.high_hz / flux_stft.bin_hz(1)));
        if (end <= first) {
            first = static_cast<int>(
                std::lround(std::sqrt(band.low_hz * band.high_hz) / flux_stft.bin_hz(1)));
            end = first + 1;
        }
        band_bins.emplace_back(first, std::min(end, flux_bins));
    }
    for (BandTrack &track : result.bands) {
        track.flux.resize(result.frames);
    }
    result.flux_total.resize(result.frames);
    auto flux_task = std::async(std::launch::async, [&] {
        for (int frame = 0; frame < result.frames; ++frame) {
            flux_stft.process(mono, static_cast<SampleIndex>(frame) * hop, current);
            for (float &m : current) {
                m = std::log1p(kFluxCompression * m);
            }
            double total = 0.0;
            for (int bin = 0; bin < flux_bins; ++bin) {
                total += std::max(0.0f, current[bin] - previous[bin]);
            }
            result.flux_total[frame] = static_cast<float>(total / flux_bins);
            for (size_t b = 0; b < band_bins.size(); ++b) {
                double sum = 0.0;
                for (int bin = band_bins[b].first; bin < band_bins[b].second; ++bin) {
                    sum += std::max(0.0f, current[bin] - previous[bin]);
                }
                result.bands[b].flux[frame] =
                    static_cast<float>(sum / (band_bins[b].second - band_bins[b].first));
            }
            std::swap(previous, current);
        }
    });

    // Filterbank energies and envelopes (on this thread), one band at a time to bound memory.
    std::vector<float> filtered(mono.size());
    for (size_t b = 0; b < params.bands.size(); ++b) {
        BandFilter filter;
        filter.prepare(sample_rate_hz, params.bands[b].low_hz, params.bands[b].high_hz);
        filter.process(mono, filtered);
        BandTrack &track = result.bands[b];
        track.energy_db.resize(result.frames);
        track.envelope.resize(result.frames);
        EnvelopeFollower follower;
        follower.prepare(sample_rate_hz, params.envelope_attack_ms, params.envelope_release_ms);
        SampleIndex next_sample = 0;
        for (int frame = 0; frame < result.frames; ++frame) {
            const SampleIndex centre = static_cast<SampleIndex>(frame) * hop;
            const SampleIndex begin = std::max<SampleIndex>(0, centre - hop / 2);
            const SampleIndex end = std::min(length, centre - hop / 2 + hop);
            double sum = 0.0;
            for (SampleIndex i = begin; i < end; ++i) {
                sum += static_cast<double>(filtered[i]) * filtered[i];
            }
            track.energy_db[frame] = power_to_db(end > begin ? sum / (end - begin) : 0.0);
            double level = 0.0;
            for (; next_sample <= centre && next_sample < length; ++next_sample) {
                level = follower.process(filtered[next_sample]);
            }
            track.envelope[frame] = static_cast<float>(level);
        }
    }
    spectrogram_task.get();
    salience_task.get();
    flux_task.get();
    return result;
}

} // namespace psyight
