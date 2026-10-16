#include "analysis/align.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace psyight {

namespace {

constexpr std::int64_t kDecimation = 16;

// Box-filtered and decimated by kDecimation.
std::vector<float> decimate(std::span<const float> x) {
    std::vector<float> y(x.size() / kDecimation);
    for (size_t i = 0; i < y.size(); ++i) {
        double sum = 0.0;
        for (std::int64_t k = 0; k < kDecimation; ++k) {
            sum += x[i * kDecimation + k];
        }
        y[i] = static_cast<float>(sum / kDecimation);
    }
    return y;
}

// Normalised correlation of reference[begin, end) with signal shifted by lag.
double correlation(std::span<const float> reference, std::span<const float> signal,
                   std::int64_t begin, std::int64_t end, std::int64_t lag) {
    double sum = 0.0;
    double energy = 0.0;
    for (std::int64_t i = begin; i < end; ++i) {
        const std::int64_t j = i + lag;
        if (j >= 0 && j < static_cast<std::int64_t>(signal.size())) {
            sum += static_cast<double>(reference[i]) * signal[j];
            energy += static_cast<double>(signal[j]) * signal[j];
        }
    }
    return energy > 0.0 ? sum / std::sqrt(energy) : 0.0;
}

std::int64_t best_lag(std::span<const float> reference, std::span<const float> signal,
                      std::int64_t begin, std::int64_t end, std::int64_t from, std::int64_t to) {
    std::int64_t best = 0;
    double best_value = -std::numeric_limits<double>::infinity();
    for (std::int64_t lag = from; lag <= to; ++lag) {
        const double value = correlation(reference, signal, begin, end, lag);
        if (value > best_value) {
            best_value = value;
            best = lag;
        }
    }
    return best;
}

} // namespace

std::int64_t estimate_lag_samples(std::span<const float> reference, std::span<const float> signal,
                                  std::int64_t max_lag_samples, std::int64_t window_samples) {
    const auto length = static_cast<std::int64_t>(std::min(reference.size(), signal.size()));
    const std::int64_t window = std::min(window_samples, length - 2 * max_lag_samples);
    if (window <= kDecimation * 4) {
        return 0;
    }
    const std::int64_t begin = (length - window) / 2;
    const std::vector<float> coarse_reference = decimate(reference);
    const std::vector<float> coarse_signal = decimate(signal);
    const std::int64_t coarse_range = max_lag_samples / kDecimation + 1;
    const std::int64_t coarse =
        best_lag(coarse_reference, coarse_signal, begin / kDecimation,
                 (begin + window) / kDecimation, -coarse_range, coarse_range) *
        kDecimation;
    // Sample-exact refinement on a shorter stretch around the coarse peak.
    const std::int64_t fine_window = std::min<std::int64_t>(window, 2 * 48000);
    const std::int64_t fine_begin = begin + (window - fine_window) / 2;
    return best_lag(reference, signal, fine_begin, fine_begin + fine_window,
                    coarse - 2 * kDecimation, coarse + 2 * kDecimation);
}

} // namespace psyight
