#include "engine/limiters.h"

#include <algorithm>
#include <cmath>

namespace psyight {

double luminance(const Rgb &c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }

double estimated_current_a(const Frame &frame, double current_per_channel_ma) {
    double sum = 0.0;
    for (const Rgb &c : frame) {
        sum += static_cast<double>(c.r) + c.g + c.b;
    }
    return sum * current_per_channel_ma * 1e-3;
}

void Limiters::reset() {
    previous_luminance_.clear();
    flash_times_s_.clear();
}

LimiterReport Limiters::process(Frame &frame, double t_s) {
    LimiterReport report;
    // Brightness cap.
    if (p_.brightness_cap < 1.0) {
        for (Rgb &c : frame) {
            c = scaled(c, std::max(0.0, p_.brightness_cap));
        }
    }
    // Flash limiter.
    if (previous_luminance_.size() != frame.size()) {
        previous_luminance_.assign(frame.size(), 0.0);
    }
    while (!flash_times_s_.empty() && t_s - flash_times_s_.front() >= 1.0) {
        flash_times_s_.pop_front();
    }
    if (p_.flash_limiter && !frame.empty()) {
        size_t rising = 0;
        for (size_t i = 0; i < frame.size(); ++i) {
            rising += luminance(frame[i]) - previous_luminance_[i] >= p_.flash_delta;
        }
        const bool flash =
            static_cast<double>(rising) >= p_.flash_area * static_cast<double>(frame.size());
        if (flash) {
            if (static_cast<double>(flash_times_s_.size()) < p_.max_flashes_per_s) {
                flash_times_s_.push_back(t_s);
            } else {
                // Clip each rising pixel to just under the flash threshold.
                constexpr double kUnder = 0.99;
                for (size_t i = 0; i < frame.size(); ++i) {
                    const double now = luminance(frame[i]);
                    const double allowed = previous_luminance_[i] + kUnder * p_.flash_delta;
                    if (now > allowed && now > 0.0) {
                        frame[i] = scaled(frame[i], allowed / now);
                    }
                }
                report.flash_limited = true;
            }
        }
    }
    // Power budget.
    report.current_a = estimated_current_a(frame, p_.current_per_channel_ma);
    if (p_.budget_a > 0.0 && report.current_a > p_.budget_a) {
        report.power_scale = p_.budget_a / report.current_a;
        for (Rgb &c : frame) {
            c = scaled(c, report.power_scale);
        }
        report.current_a = estimated_current_a(frame, p_.current_per_channel_ma);
    }
    for (size_t i = 0; i < frame.size(); ++i) {
        previous_luminance_[i] = luminance(frame[i]);
    }
    return report;
}

std::vector<std::uint8_t> to_bytes(const Frame &frame, double gamma) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(frame.size() * 3);
    const auto byte = [gamma](float v) {
        return static_cast<std::uint8_t>(
            std::lround(255.0 * std::pow(std::clamp(static_cast<double>(v), 0.0, 1.0), gamma)));
    };
    for (const Rgb &c : frame) {
        bytes.push_back(byte(c.r));
        bytes.push_back(byte(c.g));
        bytes.push_back(byte(c.b));
    }
    return bytes;
}

} // namespace psyight
