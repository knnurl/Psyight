// Small helpers on per-frame series, shared by the analysis blocks. Pure functions.
#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace psyight {

// Linear interpolation between order statistics; 0 for an empty input.
inline double quantile(std::vector<float> values, double q) {
    if (values.empty()) {
        return 0.0;
    }
    const double position = q * static_cast<double>(values.size() - 1);
    const auto lower = static_cast<size_t>(position);
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(lower),
                     values.end());
    const double low = values[lower];
    if (lower + 1 >= values.size()) {
        return low;
    }
    const double high =
        *std::min_element(values.begin() + static_cast<std::ptrdiff_t>(lower) + 1, values.end());
    return low + (position - static_cast<double>(lower)) * (high - low);
}

// Centred mean over 2 * half + 1 values, shortened at the ends.
inline std::vector<float> box_mean(const std::vector<float> &x, int half) {
    std::vector<double> prefix(x.size() + 1, 0.0);
    for (size_t i = 0; i < x.size(); ++i) {
        prefix[i + 1] = prefix[i] + x[i];
    }
    std::vector<float> result(x.size());
    const auto h = static_cast<size_t>(std::max(0, half));
    for (size_t i = 0; i < x.size(); ++i) {
        const size_t begin = i >= h ? i - h : 0;
        const size_t end = std::min(x.size(), i + h + 1);
        result[i] = static_cast<float>((prefix[end] - prefix[begin]) / (end - begin));
    }
    return result;
}

// Vertex offset of a parabola through three equally spaced points, in [-0.5, 0.5].
inline double parabolic_offset(double left, double centre, double right) {
    const double curvature = left - 2.0 * centre + right;
    if (curvature >= 0.0) {
        return 0.0;
    }
    return std::clamp(0.5 * (left - right) / curvature, -0.5, 0.5);
}

} // namespace psyight
