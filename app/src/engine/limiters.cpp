#include "engine/limiters.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace psyight {

namespace {

constexpr double kRiseWindowS = 0.1; // Pixels that rose this recently still count as rising.
constexpr double kUnder = 0.99;      // Held rises stop just under flash_delta.
constexpr int kPowerSteps = 16;      // Duty model: at most this many steps down...
constexpr double kPowerStep = 0.98;  // ...of this, after the closed-form scale.

// One channel's byte as sent: gamma to 8 bits, a channel at or above kVisibleFloor at least 1.
std::uint8_t to_byte(float v, double gamma) {
    const double level = std::clamp(static_cast<double>(v), 0.0, 1.0);
    const long out = std::lround(255.0 * std::pow(level, gamma));
    return static_cast<std::uint8_t>(out == 0 && level >= kVisibleFloor ? 1 : out);
}

// The duty model's current of the frame scaled by `scale`.
double duty_current_a(const Frame &frame, double scale, const LimiterParams &p) {
    long long bytes = 0;
    const auto s = static_cast<float>(scale);
    for (const Rgb &c : frame) {
        bytes += to_byte(c.r * s, p.gamma) + to_byte(c.g * s, p.gamma) + to_byte(c.b * s, p.gamma);
    }
    return static_cast<double>(bytes) / 255.0 * p.current_per_channel_ma * 1e-3 +
           static_cast<double>(frame.size()) * p.quiescent_ma * 1e-3;
}

void expire(std::deque<double> &starts_s, double t_s) {
    while (!starts_s.empty() && t_s - starts_s.front() >= 1.0) {
        starts_s.pop_front();
    }
}

} // namespace

std::vector<float> group_caps_per_pixel(const Rig &rig, const std::vector<GroupCap> &caps) {
    std::vector<float> per_strip(rig.strips.size(), 1.0f);
    for (size_t s = 0; s < rig.strips.size(); ++s) {
        for (const GroupCap &c : caps) {
            const std::vector<std::string> &groups = rig.strips[s].groups;
            if (std::find(groups.begin(), groups.end(), c.group) != groups.end()) {
                per_strip[s] =
                    std::min(per_strip[s], static_cast<float>(std::clamp(c.cap, 0.0, 1.0)));
            }
        }
    }
    std::vector<float> out;
    out.reserve(rig.pixels.size());
    for (const Pixel &p : rig.pixels) {
        const auto s = static_cast<size_t>(p.strip);
        out.push_back(s < per_strip.size() ? per_strip[s] : 1.0f);
    }
    return out;
}

double luminance(const Rgb &c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }

double estimated_current_a(const Frame &frame, double current_per_channel_ma) {
    double sum = 0.0;
    for (const Rgb &c : frame) {
        sum += static_cast<double>(c.r) + c.g + c.b;
    }
    return sum * current_per_channel_ma * 1e-3;
}

double estimated_current_a(const Frame &frame, const LimiterParams &params) {
    return params.power_model == PowerModel::linear
               ? estimated_current_a(frame, params.current_per_channel_ma)
               : duty_current_a(frame, 1.0, params);
}

const char *power_model_id(PowerModel model) {
    return model == PowerModel::linear ? "linear" : "duty";
}

std::optional<PowerModel> power_model_from_id(const std::string &id) {
    if (id == "linear") {
        return PowerModel::linear;
    }
    if (id == "duty") {
        return PowerModel::duty;
    }
    return std::nullopt;
}

void FlashTracker::reset() {
    armed_.clear();
    ref_.clear();
    rose_s_.clear();
    allowed_starts_s_.clear();
    all_starts_s_.clear();
    in_flash_ = false;
    allowed_ = false;
    started_ = false;
}

bool FlashTracker::detect(Frame &frame, double t_s, bool limit) {
    const size_t n = frame.size();
    if (armed_.size() != n) {
        armed_.assign(n, 1);
        ref_.assign(n, 0.0); // From black.
        rose_s_.assign(n, -std::numeric_limits<double>::infinity());
    }
    candidate_.assign(n, 0);
    size_t rising = 0;
    for (size_t i = 0; i < n; ++i) {
        if (armed_[i]) {
            candidate_[i] = luminance(frame[i]) - ref_[i] >= p_.flash_delta;
            rising += candidate_[i];
        } else { // Still up after a recent rise (a pixel falling now no longer counts).
            rising +=
                t_s - rose_s_[i] < kRiseWindowS && luminance(frame[i]) > ref_[i] - p_.flash_delta;
        }
    }
    expire(allowed_starts_s_, t_s);
    expire(all_starts_s_, t_s);
    const bool flashing =
        n > 0 && static_cast<double>(rising) >= p_.flash_area * static_cast<double>(n);
    started_ = flashing && !in_flash_;
    if (started_) {
        all_starts_s_.push_back(t_s);
        ++flashes_;
        max_in_any_second_ = std::max(max_in_any_second_, static_cast<int>(all_starts_s_.size()));
        allowed_ = false;
    }
    in_flash_ = flashing;
    // A held flash is let through as soon as the window has room.
    if (flashing && !allowed_ &&
        (!limit || static_cast<double>(allowed_starts_s_.size()) < p_.max_flashes_per_s)) {
        allowed_ = true;
        allowed_starts_s_.push_back(t_s);
    }
    const bool hold = limit && flashing && !allowed_;
    bool held = false;
    for (size_t i = 0; i < n; ++i) {
        if (!candidate_[i]) {
            continue;
        }
        if (hold) {
            const double now = luminance(frame[i]);
            const double allowed = ref_[i] + kUnder * p_.flash_delta;
            if (now > allowed && now > 0.0) {
                frame[i] = scaled(frame[i], allowed / now);
                held = true;
            }
        } else {
            armed_[i] = 0; // Risen: wait for the fall.
            rose_s_[i] = t_s;
            ref_[i] = luminance(frame[i]);
        }
    }
    return held;
}

void FlashTracker::commit(const Frame &frame) {
    for (size_t i = 0; i < frame.size() && i < armed_.size(); ++i) {
        const double l = luminance(frame[i]);
        if (armed_[i]) {
            ref_[i] = std::min(ref_[i], l);
        } else if (l <= ref_[i] - p_.flash_delta) {
            armed_[i] = 1; // Fallen: may rise again, from here.
            ref_[i] = l;
        } else {
            ref_[i] = std::max(ref_[i], l);
        }
    }
}

void Limiters::reset() { flashes_.reset(); }

LimiterReport Limiters::process(Frame &frame, double t_s) {
    LimiterReport report;
    // Brightness cap.
    if (p_.brightness_cap < 1.0) {
        for (Rgb &c : frame) {
            c = scaled(c, std::max(0.0, p_.brightness_cap));
        }
    }
    if (pixel_caps_.size() == frame.size()) {
        for (size_t i = 0; i < frame.size(); ++i) {
            if (pixel_caps_[i] < 1.0f) {
                frame[i] = scaled(frame[i], std::max(0.0f, pixel_caps_[i]));
            }
        }
    }
    // Power budget.
    const double current_a = estimated_current_a(frame, p_);
    if (p_.budget_a > 0.0 && current_a > p_.budget_a) {
        if (p_.power_model == PowerModel::linear) {
            report.power_scale = p_.budget_a / current_a;
        } else {
            const double quiescent_a = static_cast<double>(frame.size()) * p_.quiescent_ma * 1e-3;
            double s = 0.0;
            if (p_.budget_a > quiescent_a) {
                s = std::pow((p_.budget_a - quiescent_a) / (current_a - quiescent_a),
                             1.0 / std::max(1.0, p_.gamma));
                // The visible floor and rounding make the current step, so the closed form can
                // land just over the budget.
                for (int k = 0; k < kPowerSteps && duty_current_a(frame, s, p_) > p_.budget_a;
                     ++k) {
                    s *= kPowerStep;
                }
            }
            report.power_scale = s;
        }
        for (Rgb &c : frame) {
            c = scaled(c, report.power_scale);
        }
    }
    // Flash limiter, last, so it judges the frame as shown (tracked even when off, so turning it
    // on starts from the true state). It only dims, so the power budget still holds.
    report.flash_limited = flashes_.detect(frame, t_s, p_.flash_limiter);
    flashes_.commit(frame);
    report.current_a = estimated_current_a(frame, p_);
    return report;
}

bool FlashMeter::add(const Frame &frame, double t_s) {
    Frame copy = frame;
    tracker_.detect(copy, t_s, false);
    tracker_.commit(copy);
    return tracker_.flash_started();
}

std::vector<std::uint8_t> to_bytes(const Frame &frame, double gamma) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(frame.size() * 3);
    for (const Rgb &c : frame) {
        bytes.push_back(to_byte(c.r, gamma));
        bytes.push_back(to_byte(c.g, gamma));
        bytes.push_back(to_byte(c.b, gamma));
    }
    return bytes;
}

} // namespace psyight
