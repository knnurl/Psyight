#include "analysis/intensity.h"

#include <algorithm>
#include <array>
#include <numeric>

namespace psyight {

namespace {

constexpr std::array<double, 5> kWeights = {0.35, 0.25, 0.15, 0.15, 0.1};

// Each value's rank among all of them, 0 to 1 (ties share their mean rank).
std::vector<double> ranks(const std::vector<double> &values) {
    const size_t n = values.size();
    std::vector<double> out(n, 0.5);
    if (n < 2) {
        return out;
    }
    std::vector<size_t> order(n);
    std::iota(order.begin(), order.end(), size_t{0});
    std::sort(order.begin(), order.end(),
              [&](size_t a, size_t b) { return values[a] < values[b]; });
    for (size_t i = 0; i < n;) {
        size_t j = i;
        while (j + 1 < n && values[order[j + 1]] == values[order[i]]) {
            ++j;
        }
        const double rank = 0.5 * static_cast<double>(i + j) / static_cast<double>(n - 1);
        for (size_t k = i; k <= j; ++k) {
            out[order[k]] = rank;
        }
        i = j + 1;
    }
    return out;
}

// Bass notes in the grid bar holding t_s (0 outside them).
double bass_notes_at(const std::vector<BassAggregate> &bass_bars, double t_s) {
    const auto it =
        std::upper_bound(bass_bars.begin(), bass_bars.end(), t_s,
                         [](double t, const BassAggregate &b) { return t < b.start_s; });
    if (it == bass_bars.begin()) {
        return 0.0;
    }
    const BassAggregate &bar = *(it - 1);
    return t_s < bar.end_s ? static_cast<double>(bar.notes) : 0.0;
}

} // namespace

std::vector<float> intensity_curve(const std::vector<SectionBar> &bars,
                                   const std::vector<BassAggregate> &bass_bars) {
    const size_t n = bars.size();
    std::array<std::vector<double>, 5> features;
    for (std::vector<double> &f : features) {
        f.reserve(n);
    }
    for (size_t i = 0; i < n; ++i) {
        const SectionBar &b = bars[i];
        const double mid_s = i + 1 < n ? 0.5 * (b.start_s + bars[i + 1].start_s) : b.start_s + 0.5;
        features[0].push_back(b.level_db);
        features[1].push_back(b.high_db);
        features[2].push_back(b.centroid_hz);
        features[3].push_back(bass_notes_at(bass_bars, mid_s));
        features[4].push_back(b.kick_beats);
    }
    std::vector<double> raw(n, 0.0);
    for (size_t f = 0; f < features.size(); ++f) {
        const std::vector<double> r = ranks(features[f]);
        for (size_t i = 0; i < n; ++i) {
            raw[i] += kWeights[f] * r[i];
        }
    }
    // Smoothed over the neighbouring bars (1-2-1).
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i) {
        const double before = i > 0 ? raw[i - 1] : raw[i];
        const double after = i + 1 < n ? raw[i + 1] : raw[i];
        out[i] =
            static_cast<float>(std::clamp(0.25 * before + 0.5 * raw[i] + 0.25 * after, 0.0, 1.0));
    }
    return out;
}

double intensity_at(const std::vector<SectionBar> &bars, const std::vector<float> &intensity,
                    double t_s) {
    const size_t n = std::min(bars.size(), intensity.size());
    if (n == 0) {
        return 0.5;
    }
    // Bar centres: halfway to the next bar's start (the last bar as long as the one before).
    const auto centre = [&](size_t i) {
        const double next =
            i + 1 < bars.size()
                ? bars[i + 1].start_s
                : bars[i].start_s + (i > 0 ? bars[i].start_s - bars[i - 1].start_s : 1.0);
        return 0.5 * (bars[i].start_s + next);
    };
    if (t_s <= centre(0)) {
        return intensity[0];
    }
    for (size_t i = 0; i + 1 < n; ++i) {
        const double a = centre(i);
        const double b = centre(i + 1);
        if (t_s <= b) {
            const double f = b > a ? (t_s - a) / (b - a) : 1.0;
            return intensity[i] + f * (intensity[i + 1] - intensity[i]);
        }
    }
    return intensity[n - 1];
}

} // namespace psyight
